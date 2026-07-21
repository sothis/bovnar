/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2026 Janos Sonntag
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/*
 * Streaming/framing applications layered on the reader/writer event API. See
 * include/bovnar_stream.h for the rationale and the wire conventions. Nothing
 * here touches the lexer state machine or the grammar — it is built entirely on
 * the public event surface (bvnr_write_event, the on_verified callback) plus the
 * internal source/sink push/pull helpers for the transport-level frame codec.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "bovnar.h"
#include "bovnar_stream.h"
#include "bvn_io_impl.h"

/* ===========================================================================
 * Shared little helpers
 * =========================================================================== */

/*
 * Unsigned LEB128 varint. The channel id in the multiplexing convention is
 * encoded this way so small channel numbers (the common case) cost one byte
 * while the full 64-bit range stays addressable. Max width is 10 bytes.
 */
static uint32_t bvn_varint_encode(uint64_t v, uint8_t out[10])
{
	uint32_t n = 0;
	do {
		uint8_t byte = (uint8_t)(v & 0x7Fu);
		v >>= 7;
		if (v) byte |= 0x80u;
		out[n++] = byte;
	} while (v);
	return n;
}

/*
 * Decode a varint from buf[0..len). On success returns the number of bytes
 * consumed and writes the value to *out; returns 0 on a truncated or
 * overlong (>10 byte) encoding.
 */
static uint32_t bvn_varint_decode(const uint8_t* buf, uint32_t len, uint64_t* out)
{
	uint64_t v = 0;
	uint32_t shift = 0, i = 0;
	while (i < len && i < 10u) {
		uint8_t byte = buf[i++];
		/* The 10th byte (shift == 63) may carry only one value bit; any
		 * higher payload bit (0x7E) or a continuation bit (0x80) means the
		 * value does not fit 64 bits, so reject the overlong encoding rather
		 * than silently truncating it. */
		if (shift == 63u && (byte & 0xFEu))
			return 0;
		v |= (uint64_t)(byte & 0x7Fu) << shift;
		if (!(byte & 0x80u)) { *out = v; return i; }
		shift += 7;
	}
	return 0;
}

static void bvn_put_u64le(uint8_t out[8], uint64_t v)
{
	for (uint32_t i = 0; i < 8; i++)
		out[i] = (uint8_t)((v >> (8u * i)) & 0xFFu);
}

static uint64_t bvn_get_u64le(const uint8_t in[8])
{
	uint64_t v = 0;
	for (uint32_t i = 0; i < 8; i++)
		v |= (uint64_t)in[i] << (8u * i);
	return v;
}

/* ===========================================================================
 * 2. Multi-document record framing
 * =========================================================================== */

/*
 * Write a buffer to a sink in <=1 GiB pushes. bvnr_push_fn takes a uint32 length,
 * so a document larger than 4 GiB (legal under endless mode) must be chunked.
 */
static bool bvn_sink_push_all(bvnr_sink_t* sink, const void* buf, uint64_t len)
{
	const uint8_t* p = (const uint8_t*)buf;
	while (len) {
		uint32_t step = len > (1u << 30) ? (1u << 30) : (uint32_t)len;
		if (!bvn_sink_push(sink, p, step))
			return false;
		p   += step;
		len -= step;
	}
	return true;
}

bool bvnr_frame_write(bvnr_sink_t* sink, const void* doc, uint64_t doc_len)
{
	if (!sink || (!doc && doc_len))
		return false;
	uint8_t hdr[BVNR_FRAME_HEADER_SIZE];
	hdr[0] = BVNR_FRAME_MAGIC0;
	hdr[1] = BVNR_FRAME_MAGIC1;
	hdr[2] = BVNR_FRAME_MAGIC2;
	hdr[3] = BVNR_FRAME_MAGIC3;
	bvn_put_u64le(hdr + 4, doc_len);
	if (!bvn_sink_push_all(sink, hdr, BVNR_FRAME_HEADER_SIZE))
		return false;
	return doc_len ? bvn_sink_push_all(sink, doc, doc_len) : true;
}

/*
 * Read exactly `n` bytes from `src`, looping over short reads and pulling in
 * <=1 GiB windows (the pull fn's length is uint32). Returns:
 *    1  filled all n bytes
 *    0  clean EOF before *any* byte was read  (legal frame boundary)
 *   -1  I/O error, or EOF mid-record (truncation)
 */
static int bvn_src_read_exact(bvnr_source_t* src, uint8_t* buf, uint64_t n)
{
	uint64_t total = 0;
	while (total < n) {
		uint64_t rem  = n - total;
		uint32_t want = rem > (1u << 30) ? (1u << 30) : (uint32_t)rem;
		uint32_t got  = 0;
		if (!bvn_source_pull(src, buf + total, want, &got))
			return -1;
		if (!got)
			return total == 0 ? 0 : -1;
		total += got;
	}
	return 1;
}

/*
 * Read exactly `len` bytes from `src` into a freshly malloc'd buffer (caller
 * frees), growing the buffer geometrically rather than allocating `len`
 * up-front. `len` comes from the frame header and is therefore attacker-
 * controlled, so a pre-allocation of the declared size would let a tiny crafted
 * header force a large allocation before any payload arrives (a memory-
 * amplification vector). Growing with the data that actually arrives bounds the
 * allocation by real bytes received. Returns 1 and *out on success, 0 with
 * *out=NULL for len==0, and -1 on short stream / I/O / OOM (buffer freed).
 */
static int bvn_src_read_exact_grow(
	bvnr_source_t* src, uint64_t len, uint8_t** out)
{
	*out = NULL;
	if (len == 0) return 1;
	uint64_t cap = len < 4096u ? len : 4096u;
	uint8_t* buf = (uint8_t*)malloc((size_t)cap);
	if (!buf) return -1;
	uint64_t total = 0;
	while (total < len) {
		if (total == cap) {
			/* Double, but clamp to len before cap*2 could overflow uint64
			 * (reachable only in unlimited-cap mode with a huge len). */
			uint64_t ncap = (cap > len / 2u) ? len : cap * 2u;
			uint8_t* nb = (uint8_t*)realloc(buf, (size_t)ncap);
			if (!nb) { free(buf); return -1; }
			buf = nb;
			cap = ncap;
		}
		uint64_t room = cap - total;
		uint32_t want = room > (1u << 30) ? (1u << 30) : (uint32_t)room;
		uint32_t got  = 0;
		if (!bvn_source_pull(src, buf + total, want, &got)) { free(buf); return -1; }
		if (!got) { free(buf); return -1; }   /* EOF before len: truncation */
		total += got;
	}
	*out = buf;
	return 1;
}

bool bvnr_doc_stream_read(
	bvnr_source_t* src, const bvnr_doc_stream_opts_t* opts,
	uint64_t* out_count)
{
	if (out_count) *out_count = 0;
	if (!src || !bvn_source_impl(src)->pull)
		return false;

	uint64_t cap = (opts && opts->max_document_size)
		? opts->max_document_size : BVNR_DOC_DEFAULT_MAX;
	uint64_t index = 0;
	bvnr_reader_t* r = bvnr_reader_create();
	if (!r) return false;

	bool result = true;
	for (;;) {
		uint8_t hdr[BVNR_FRAME_HEADER_SIZE];
		int rc = bvn_src_read_exact(src, hdr, BVNR_FRAME_HEADER_SIZE);
		if (rc == 0) break;            /* clean EOF on frame boundary */
		if (rc < 0) { result = false; break; }
		if (hdr[0] != BVNR_FRAME_MAGIC0 || hdr[1] != BVNR_FRAME_MAGIC1 ||
		    hdr[2] != BVNR_FRAME_MAGIC2 || hdr[3] != BVNR_FRAME_MAGIC3) {
			result = false; break;
		}
		uint64_t len = bvn_get_u64le(hdr + 4);
		/* cap is always nonzero here (explicit value or BVNR_DOC_DEFAULT_MAX).
		 * To lift the per-frame guard, callers pass an explicit huge cap
		 * (e.g. UINT64_MAX), which a 64-bit len can never exceed. */
		if (len > cap) {
			result = false; break;
		}
		/* SIZE_MAX guard for 32-bit hosts where uint64 len can exceed size_t. */
		if (len > (uint64_t)SIZE_MAX) { result = false; break; }

		uint8_t* buf = NULL;
		if (len && bvn_src_read_exact_grow(src, len, &buf) != 1) {
			result = false; break;
		}

		bool ok = bvnr_open_read_mem(r, buf ? buf : (const void*)"", len,
					NULL, 0, opts ? opts->flags : NULL)
			&& bvnr_read(r);
		error_code_t err = bvnr_reader_get_error(r);
		free(buf);

		/* Count the document the moment it has been read and parsed, *before*
		 * any abort path. This keeps *out_count consistent across both ways a
		 * sequence can stop: a parse failure and an on_document veto now both
		 * include the document they acted on, so the count is "documents seen"
		 * regardless of which abort fired. */
		uint64_t doc_index = index;
		index++;

		if (opts && opts->on_document) {
			if (!opts->on_document(opts->userdata, doc_index, ok, err)) {
				result = false; break;
			}
		}

		/* A failed document aborts the sequence unless the caller opted into
		 * inter-document resilience. This is orthogonal to the inner reader's
		 * resync (flags->continue_on_error): each frame is independently
		 * delimited, so we can always advance to the next one. */
		if (!ok && !(opts && opts->continue_past_failed)) {
			result = false; break;
		}
	}

	bvnr_reader_destroy(r);
	if (out_count) *out_count = index;
	return result;
}

/* ===========================================================================
 * 3. Octet multiplexing (out-of-band channel convention)
 * =========================================================================== */

bool bvnr_mux_begin(bvnr_writer_t* w, const char* key)
{
	if (!w || !key) return false;
	bvnr_data_t d = {0};
	d.data   = key;
	d.length = (uint32_t)strlen(key);
	if (!bvnr_write_event(w, ev_assignment_start, &d))
		return false;
	bvnr_data_t empty = {0};
	return bvnr_write_event(w, ev_octet_stream_start, &empty);
}

/*
 * Emit one message on `channel`, split into as many octet chunks as needed.
 * The logical stream for the message is varint(len) ++ data. Each chunk is
 * varint(channel) ++ fragment; the length varint always rides in the first
 * chunk (it is tiny and the fragment capacity is ~64 KiB, so it is never
 * split). One reusable 64 KiB chunk buffer is filled and emitted per iteration.
 */
bool bvnr_mux_send(bvnr_writer_t* w, uint64_t channel,
	const void* data, uint64_t len)
{
	if (!w || (!data && len))
		return false;
	uint8_t chvar[10], lenvar[10];
	uint32_t chlen  = bvn_varint_encode(channel, chvar);
	uint32_t lvlen  = bvn_varint_encode(len, lenvar);
	uint32_t fragcap = 65536u - chlen;   /* logical bytes per chunk after channel id */

	uint8_t* chunk = (uint8_t*)malloc(65536u);
	if (!chunk) return false;

	const uint8_t* dp = (const uint8_t*)data;
	uint64_t dpos = 0;
	bool first = true;
	bool ok = true;
	do {
		uint32_t off = 0;
		memcpy(chunk, chvar, chlen); off = chlen;
		uint32_t room = fragcap;
		if (first) {
			memcpy(chunk + off, lenvar, lvlen);
			off  += lvlen;
			room -= lvlen;
			first = false;
		}
		uint64_t dleft = len - dpos;
		uint32_t take  = dleft < (uint64_t)room ? (uint32_t)dleft : room;
		if (take) { memcpy(chunk + off, dp + dpos, take); off += take; dpos += take; }

		bvnr_data_t d = {0};
		d.type   = token_is_octet_stream;
		d.data   = chunk;
		d.length = off;
		ok = bvnr_write_event(w, ev_data, &d);
	} while (ok && dpos < len);

	free(chunk);
	return ok;
}

bool bvnr_mux_end(bvnr_writer_t* w)
{
	if (!w) return false;
	bvnr_data_t empty = {0};
	return bvnr_write_event(w, ev_octet_stream_end, &empty);
}

/* ---- demultiplexer: per-channel reassembly --------------------------------*/

typedef struct demux_channel_s {
	uint64_t	channel;
	uint8_t*	buf;		/* message reassembly buffer */
	uint64_t	cap;		/* allocated size of buf */
	uint64_t	have;		/* bytes accumulated for the current message */
	uint64_t	need;		/* declared length of the current message */
	bool		active;		/* a message is in progress */
	uint8_t		lvbuf[10];	/* partial message-length varint (cross-chunk) */
	uint8_t		lvhave;		/* bytes accumulated into lvbuf so far */
} demux_channel_t;

struct bvnr_demux_s {
	bvnr_mux_on_msg_fn	on_message;
	void*			userdata;
	uint64_t		max_message;
	bool			in_octet;
	error_code_t		error;
	demux_channel_t*	chans;
	uint32_t		nchans;
	uint32_t		chans_cap;
	/* Optional key scope (bvnr_demux_set_key). When key is non-NULL the demux
	 * only treats octet streams opened under a matching assignment key as
	 * multiplexed; any other octet stream in the same document is ignored, so a
	 * document may freely mix a mux stream with ordinary binary octet payloads.
	 * key == NULL (the default) demuxes every octet stream. key_match tracks
	 * whether the current assignment key matches. */
	char*			key;
	uint32_t		key_len;
	bool			key_match;
};

#define BVNR_DEMUX_MAX_CHANNELS  4096u

bvnr_demux_t* bvnr_demux_create(bvnr_mux_on_msg_fn on_message,
	void* userdata, uint64_t max_message)
{
	bvnr_demux_t* dm = (bvnr_demux_t*)calloc(1, sizeof(*dm));
	if (!dm) return NULL;
	dm->on_message  = on_message;
	dm->userdata    = userdata;
	dm->max_message = max_message ? max_message : BVNR_MUX_DEFAULT_MAX;
	dm->error       = error_none;
	return dm;
}

void bvnr_demux_destroy(bvnr_demux_t* dm)
{
	if (!dm) return;
	for (uint32_t i = 0; i < dm->nchans; i++)
		free(dm->chans[i].buf);
	free(dm->chans);
	free(dm->key);
	free(dm);
}

bool bvnr_demux_set_key(bvnr_demux_t* dm, const char* key)
{
	if (!dm) return false;
	free(dm->key);
	dm->key       = NULL;
	dm->key_len   = 0;
	/* No filter (NULL) restores the demux-every-octet-stream default; with a
	 * filter set, octet streams default to "not ours" until a matching key. */
	dm->key_match = false;
	if (!key)
		return true;
	size_t n = strlen(key);
	if (n > UINT32_MAX) {
		dm->error = error_invalid_argument;
		return false;
	}
	dm->key = (char*)malloc(n + 1u);
	if (!dm->key) {
		dm->error = error_invalid_argument;
		return false;
	}
	memcpy(dm->key, key, n + 1u);
	dm->key_len = (uint32_t)n;
	return true;
}

error_code_t bvnr_demux_error(const bvnr_demux_t* dm)
{
	return dm ? dm->error : error_invalid_argument;
}

static demux_channel_t* demux_get_channel(bvnr_demux_t* dm, uint64_t channel)
{
	for (uint32_t i = 0; i < dm->nchans; i++)
		if (dm->chans[i].channel == channel)
			return &dm->chans[i];
	if (dm->nchans >= BVNR_DEMUX_MAX_CHANNELS) {
		dm->error = error_octet_stream_out_of_sync;
		return NULL;
	}
	if (dm->nchans == dm->chans_cap) {
		uint32_t ncap = dm->chans_cap ? dm->chans_cap * 2u : 8u;
		demux_channel_t* nc = (demux_channel_t*)realloc(
			dm->chans, ncap * sizeof(*nc));
		if (!nc) { dm->error = error_invalid_argument; return NULL; }
		dm->chans     = nc;
		dm->chans_cap = ncap;
	}
	demux_channel_t* c = &dm->chans[dm->nchans++];
	memset(c, 0, sizeof(*c));
	c->channel = channel;
	return c;
}

/*
 * Consume one chunk fragment belonging to `c`. The fragment is a slice of the
 * channel's logical stream: when no message is active it begins with a
 * varint(msg_len); thereafter it is raw message data. Loops so a fragment that
 * completes one message and starts the next is handled (a future packed
 * producer), though bvnr_mux_send never packs.
 */
static bool demux_feed(bvnr_demux_t* dm, demux_channel_t* c,
	const uint8_t* frag, uint32_t fraglen)
{
	uint32_t pos = 0;
	while (pos < fraglen) {
		if (!c->active) {
			/* Accumulate the message-length varint, which may itself span
			 * chunk boundaries: pull continuation bytes into the per-channel
			 * lvbuf until a terminator (high bit clear) arrives, then decode
			 * the whole varint. (The channel varint, by contrast, must be
			 * whole within one chunk — it is the routing prefix and is read by
			 * the caller before this function; see bvnr_demux_on_event.) */
			bool terminated = false;
			while (pos < fraglen) {
				uint8_t b = frag[pos++];
				if (c->lvhave >= 10u) {   /* >10 bytes can't fit a uint64 */
					dm->error = error_octet_stream_out_of_sync;
					return false;
				}
				c->lvbuf[c->lvhave++] = b;
				if (!(b & 0x80u)) { terminated = true; break; }
			}
			if (!terminated)
				return true;   /* fragment ended mid-varint: resume next chunk */
			uint64_t need = 0;
			uint32_t used = bvn_varint_decode(c->lvbuf, c->lvhave, &need);
			/* The accumulated run ends exactly at the terminator, so a clean
			 * varint consumes every buffered byte; any mismatch (used==0 from an
			 * overlong 10th byte, or a short decode) is a malformed encoding. */
			if (used != c->lvhave) {
				dm->error = error_octet_stream_out_of_sync; return false;
			}
			c->lvhave = 0;
			if (need > dm->max_message) {
				dm->error = error_value_out_of_range; return false;
			}
			/* SIZE_MAX guard for 32-bit hosts where the uint64 declared length
			 * can exceed size_t (parity with the doc-stream path); a caller may
			 * have set max_message above SIZE_MAX. */
			if (need > (uint64_t)SIZE_MAX) {
				dm->error = error_value_out_of_range; return false;
			}
			c->need   = need;
			c->have   = 0;
			c->active = true;
			/* Deliberately do NOT pre-allocate `need` here. The declared
			 * length is attacker-controlled, so allocating it up-front would
			 * let a single tiny chunk (varint channel + varint len) force an
			 * allocation of up to max_message — a large memory-amplification
			 * vector. Instead the buffer grows with the data that actually
			 * arrives (below), so memory is bounded by real bytes received.
			 * A zero-length message still needs a non-NULL 1-byte buffer so
			 * on_message never receives NULL. */
			if (need == 0 && !c->buf) {
				c->buf = (uint8_t*)malloc(1u);
				if (!c->buf) { dm->error = error_invalid_argument; return false; }
				c->cap = 1u;
			}
		}
		uint64_t remaining = c->need - c->have;
		uint32_t take = (fraglen - pos) < remaining
			? (fraglen - pos) : (uint32_t)remaining;
		if (take) {
			/* Grow geometrically toward the data on hand, never past the
			 * declared length (and never shrinking a larger prior buffer). */
			uint64_t want = c->have + take;
			if (want > c->cap) {
				/* Double toward `want` (which is <= need); clamp to need
				 * before newcap*2 could overflow uint64 (reachable only in
				 * unlimited-cap mode with a huge declared length). */
				uint64_t newcap = c->cap ? c->cap : 256u;
				while (newcap < want) {
					if (newcap > c->need / 2u) { newcap = c->need; break; }
					newcap *= 2u;
				}
				/* The 256-byte starting size can exceed a small declared
				 * length; cap at need so a tiny message allocates tightly
				 * (overflow-safe: a plain comparison, not a doubling). */
				if (newcap > c->need) newcap = c->need;
				uint8_t* nb = (uint8_t*)realloc(c->buf, (size_t)newcap);
				if (!nb) { dm->error = error_invalid_argument; return false; }
				c->buf = nb;
				c->cap = newcap;
			}
			memcpy(c->buf + c->have, frag + pos, take);
			c->have += take;
			pos     += take;
		}
		if (c->have == c->need) {
			c->active = false;
			if (dm->on_message &&
			    !dm->on_message(dm->userdata, c->channel, c->buf, c->need)) {
				dm->error = error_scanner_callback_failed;
				return false;
			}
		}
	}
	return true;
}

bool bvnr_demux_on_event(void* demux, bvnr_event_t ev, bvnr_data_t* d)
{
	bvnr_demux_t* dm = (bvnr_demux_t*)demux;
	if (!dm) return false;
	switch (ev) {
	case ev_assignment_start:
		/* Track whether the key now being opened is in scope. With no filter
		 * (dm->key == NULL) every octet stream is ours; with a filter, only a
		 * stream under the exact key is. Recomputed per assignment so a non-mux
		 * key clears the match before its octet stream (if any) arrives. */
		dm->key_match = dm->key && d &&
			d->length == dm->key_len &&
			(dm->key_len == 0 ||
			 memcmp(d->data, dm->key, dm->key_len) == 0);
		return true;
	case ev_octet_stream_start:
		dm->in_octet = (dm->key == NULL) || dm->key_match;
		return true;
	case ev_octet_stream_end:
		dm->in_octet = false;
		/* A message always completes within a single octet stream (mux_send
		 * never splits one across stream boundaries), so any channel still
		 * mid-reassembly here is a truncated stream. Drop that partial state
		 * rather than letting it bleed into the next stream on a reused demux,
		 * where it would mis-frame the next message as a continuation. The
		 * buffer itself is kept for reuse; only the progress (including a
		 * partial length varint) is reset.
		 *
		 * Dropping it silently made truncation indistinguishable from an empty
		 * stream: a message declaring 1000 bytes with 10 present simply vanished
		 * and bvnr_demux_error() stayed error_none, so a caller could not tell
		 * data loss from "nothing was sent". Latch it -- but keep returning true.
		 * The octet framing is well-formed, so the READ is right to succeed; it
		 * is the message layer that is short, which is why this is its own code
		 * and not error_octet_stream_out_of_sync. The partial message is still
		 * dropped and was never pre-allocated, so the amplification guard is
		 * untouched; all that changes is that the loss is now reportable. */
		for (uint32_t i = 0; i < dm->nchans; i++) {
			if (dm->chans[i].active || dm->chans[i].lvhave)
				dm->error = error_octet_stream_truncated;
			dm->chans[i].active = false;
			dm->chans[i].have   = 0;
			dm->chans[i].need   = 0;
			dm->chans[i].lvhave = 0;
		}
		return true;
	case ev_data:
		if (dm->in_octet && d && d->type == token_is_octet_stream) {
			const uint8_t* p = (const uint8_t*)d->data;
			uint64_t channel = 0;
			uint32_t used = bvn_varint_decode(p, d->length, &channel);
			if (!used) { dm->error = error_octet_stream_out_of_sync; return false; }
			demux_channel_t* c = demux_get_channel(dm, channel);
			if (!c) return false;
			return demux_feed(dm, c, p + used, d->length - used);
		}
		return true;
	default:
		return true;
	}
}

/* ===========================================================================
 * 4. Document-in-document
 * =========================================================================== */

bool bvnr_embed_document(bvnr_writer_t* w, const char* key,
	const void* doc, uint64_t doc_len)
{
	if (!w || !key || (!doc && doc_len))
		return false;
	bvnr_data_t d = {0};
	d.data   = key;
	d.length = (uint32_t)strlen(key);
	if (!bvnr_write_event(w, ev_assignment_start, &d))
		return false;
	bvnr_data_t empty = {0};
	if (!bvnr_write_event(w, ev_octet_stream_start, &empty))
		return false;

	const uint8_t* p = (const uint8_t*)doc;
	uint64_t left = doc_len;
	while (left) {
		uint32_t step = left > 65536u ? 65536u : (uint32_t)left;
		bvnr_data_t c = {0};
		c.type   = token_is_octet_stream;
		c.data   = p;
		c.length = step;
		if (!bvnr_write_event(w, ev_data, &c))
			return false;
		p    += step;
		left -= step;
	}
	return bvnr_write_event(w, ev_octet_stream_end, &empty);
}

bool bvnr_parse_embedded(const void* doc, uint64_t doc_len,
	bvnr_read_flags_t* flags)
{
	if (!doc && doc_len)
		return false;
	bvnr_reader_t* r = bvnr_reader_create();
	if (!r) return false;
	bool ok = bvnr_open_read_mem(r, doc ? doc : (const void*)"", doc_len,
				NULL, 0, flags)
		&& bvnr_read(r);
	bvnr_reader_destroy(r);
	return ok;
}
