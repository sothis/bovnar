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
#include "bvn_val_impl.h"

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

/* The frame length on the wire is little-endian. On a little-endian host the
 * store/load *is* the wire form, so one unaligned copy replaces eight shifts;
 * the byte loop below stays for the others. */
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
static void bvn_put_u64le(uint8_t out[8], uint64_t v)
{
	memcpy(out, &v, 8);
}

static uint64_t bvn_get_u64le(const uint8_t in[8])
{
	uint64_t v;
	memcpy(&v, in, 8);
	return v;
}
#else
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
#endif

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
	if (!sink || !bvn_sink_impl(sink)->push || (!doc && doc_len))
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
 *   -1  EOF mid-record (truncation)
 *   -2  I/O error
 */
static int bvn_src_read_exact(bvnr_source_t* src, uint8_t* buf, uint64_t n)
{
	uint64_t total = 0;
	while (total < n) {
		uint64_t rem  = n - total;
		uint32_t want = rem > (1u << 30) ? (1u << 30) : (uint32_t)rem;
		uint32_t got  = 0;
		if (!bvn_source_pull(src, buf + total, want, &got))
			return -2;
		if (!got)
			return total == 0 ? 0 : -1;
		total += got;
	}
	return 1;
}

/*
 * Read exactly `len` bytes from `src` into *buf, which holds *cap bytes and is
 * kept from one frame to the next (the caller frees it), growing it
 * geometrically rather than allocating `len` up-front. `len` comes from the
 * frame header and is therefore attacker-controlled, so a pre-allocation of the
 * declared size would let a tiny crafted header force a large allocation before
 * any payload arrives (a memory-amplification vector). Growing with the data
 * that actually arrives bounds the allocation by real bytes received -- now by
 * the largest frame actually received so far, since a buffer that has grown is
 * kept. A fresh buffer grows exactly as the per-frame one it replaces did:
 * min(len, 4096), then doubling, clamped to len. Returns 1 on success, -1 on a
 * short stream, -2 on an I/O error and -3 when out of memory (the buffer is
 * kept for the caller to free either way).
 */
static int bvn_src_read_exact_grow(
	bvnr_source_t* src, uint64_t len, uint8_t** buf, uint64_t* cap)
{
	uint64_t total = 0;
	while (total < len) {
		if (total == *cap) {
			/* Double, but clamp to len before cap*2 could overflow uint64
			 * (reachable only in unlimited-cap mode with a huge len). */
			uint64_t ncap;
			if (*cap > len / 2u) {
				ncap = len;
			} else {
				ncap = *cap * 2u;
				if (ncap < 4096u) ncap = 4096u;
				if (ncap > len) ncap = len;
			}
			uint8_t* nb = (uint8_t*)realloc(*buf, (size_t)ncap);
			if (!nb) return -3;
			*buf = nb;
			*cap = ncap;
		}
		uint64_t end  = *cap < len ? *cap : len;
		uint64_t room = end - total;
		uint32_t want = room > (1u << 30) ? (1u << 30) : (uint32_t)room;
		uint32_t got  = 0;
		if (!bvn_source_pull(src, *buf + total, want, &got)) return -2;
		if (!got) return -1;   /* EOF before len: truncation */
		total += got;
	}
	return 1;
}

/* What a short read of `rc` (bvn_src_read_exact, ..._grow) ends the stream
 * with: the codes the push-fed frame reader gives for the same input. */
static error_code_t doc_stream_read_error(int rc)
{
	return rc == -2 ? error_reading_from_source_fd
	     : rc == -3 ? error_invalid_argument
	     :            error_got_incomplete_bvnr_stream;
}

bool bvnr_doc_stream_read_ex(
	bvnr_source_t* src, const bvnr_doc_stream_opts_t* opts,
	uint64_t* out_count, error_code_t* out_error)
{
	if (out_count) *out_count = 0;
	if (out_error) *out_error = error_none;
	error_code_t error = error_none;
	if (!src || !bvn_source_impl(src)->pull) {
		if (out_error) *out_error = error_invalid_argument;
		return false;
	}

	uint64_t cap = (opts && opts->max_document_size)
		? opts->max_document_size : BVNR_DOC_DEFAULT_MAX;
	uint64_t index = 0;
	bvnr_reader_t* r = bvnr_reader_create();
	if (!r) {
		if (out_error) *out_error = error_invalid_argument;
		return false;
	}
	/* One payload buffer for the whole sequence, as there is one reader: a
	 * buffer per frame was an allocation per document. */
	uint8_t* buf = NULL;
	uint64_t buf_cap = 0;

	for (;;) {
		uint8_t hdr[BVNR_FRAME_HEADER_SIZE];
		int rc = bvn_src_read_exact(src, hdr, BVNR_FRAME_HEADER_SIZE);
		if (rc == 0) break;            /* clean EOF on frame boundary */
		if (rc < 0) { error = doc_stream_read_error(rc); break; }
		if (hdr[0] != BVNR_FRAME_MAGIC0 || hdr[1] != BVNR_FRAME_MAGIC1 ||
		    hdr[2] != BVNR_FRAME_MAGIC2 || hdr[3] != BVNR_FRAME_MAGIC3) {
			error = error_unexpected_input_byte; break;
		}
		uint64_t len = bvn_get_u64le(hdr + 4);
		/* cap is always nonzero here (explicit value or BVNR_DOC_DEFAULT_MAX).
		 * To lift the per-frame guard, callers pass an explicit huge cap
		 * (e.g. UINT64_MAX), which a 64-bit len can never exceed. The SIZE_MAX
		 * guard is for 32-bit hosts, where a uint64 len can exceed size_t. */
		if (len > cap || len > (uint64_t)SIZE_MAX) {
			error = error_value_out_of_range; break;
		}

		if (len) {
			rc = bvn_src_read_exact_grow(src, len, &buf, &buf_cap);
			if (rc != 1) { error = doc_stream_read_error(rc); break; }
		}

		/* The open fails only for want of memory, before the reader has
		 * an error of its own: what the header promises for that. */
		if (!bvnr_open_read_mem(r, len ? buf : (const void*)"", len,
					NULL, 0, opts ? opts->flags : NULL)) {
			error = error_invalid_argument; break;
		}
		bool ok = bvnr_read(r);
		error_code_t err = bvnr_reader_get_error(r);

		/* Count the document the moment it has been read and parsed, *before*
		 * any abort path. This keeps *out_count consistent across both ways a
		 * sequence can stop: a parse failure and an on_document veto now both
		 * include the document they acted on, so the count is "documents seen"
		 * regardless of which abort fired. */
		uint64_t doc_index = index;
		index++;

		if (opts && opts->on_document) {
			if (!opts->on_document(opts->userdata, doc_index, ok, err)) {
				error = error_scanner_callback_failed; break;
			}
		}

		/* A failed document aborts the sequence unless the caller opted into
		 * inter-document resilience. This is orthogonal to the inner reader's
		 * resync (flags->continue_on_error): each frame is independently
		 * delimited, so we can always advance to the next one. A document
		 * that failed without a code of its own (it cannot, but the stream
		 * must not end looking clean) is reported as incomplete. */
		if (!ok && !(opts && opts->continue_past_failed)) {
			error = err != error_none ? err
			                          : error_got_incomplete_bvnr_stream;
			break;
		}
	}

	free(buf);
	bvnr_reader_destroy(r);
	if (out_count) *out_count = index;
	if (out_error) *out_error = error;
	return error == error_none;
}

bool bvnr_doc_stream_read(
	bvnr_source_t* src, const bvnr_doc_stream_opts_t* opts,
	uint64_t* out_count)
{
	return bvnr_doc_stream_read_ex(src, opts, out_count, NULL);
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
 * split).
 *
 * The chunk's small header is assembled on the stack and the payload is
 * handed to the writer where the caller keeps it: a message used to be
 * copied into the writer's 64 KiB staging buffer first, a second copy of
 * every byte before the sink's own. Nothing is allocated per send, and an
 * installed observer still sees each chunk whole (the writer stages it only
 * for that).
 */
bool bvnr_mux_send(bvnr_writer_t* w, uint64_t channel,
	const void* data, uint64_t len)
{
	if (!w)
		return false;
	/* Its own refusals latch like any writer error, so a caller that checks
	 * only at bvnr_write_finish does not lose a message unnoticed (the first
	 * error is the one kept). */
	if (bvnr_writer_get_error(w) != error_none)
		return false;
	if (!data && len)
		return bvn_writer_set_error(w, error_invalid_argument);
	uint8_t chvar[10], lenvar[10];
	uint32_t chlen  = bvn_varint_encode(channel, chvar);
	uint32_t lvlen  = bvn_varint_encode(len, lenvar);
	uint32_t fragcap = 65536u - chlen;   /* logical bytes per chunk after channel id */

	const uint8_t* dp = (const uint8_t*)data;
	uint64_t dpos = 0;
	bool first = true;
	bool ok = true;
	do {
		uint8_t hdr[10 + 10];
		uint32_t off = 0;
		memcpy(hdr, chvar, chlen); off = chlen;
		uint32_t room = fragcap;
		if (first) {
			memcpy(hdr + off, lenvar, lvlen);
			off  += lvlen;
			room -= lvlen;
			first = false;
		}
		uint64_t dleft = len - dpos;
		uint32_t take  = dleft < (uint64_t)room ? (uint32_t)dleft : room;
		/* take > 0 implies len > 0, so dp is not NULL there. */
		ok = bvn_writer_write_octet_chunk(w, hdr, off,
			take ? dp + dpos : NULL, take);
		dpos += take;
	} while (ok && dpos < len);
	return ok;
}

bool bvnr_mux_end(bvnr_writer_t* w)
{
	if (!w) return false;
	bvnr_data_t empty = {0};
	return bvnr_write_event(w, ev_octet_stream_end, &empty);
}

/* ---- demultiplexer: per-channel reassembly --------------------------------*/

#define BVNR_DEMUX_MAX_CHANNELS  4096u
/* Channel ids below this get a slot in the demux's lookup table. */
#define BVN_DEMUX_SMALL          64u

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

/* One document event held for bvnr_demux_next. The byte fields of `d` are
 * copied into the demux's arena and kept as offsets, since the arena may move
 * while it grows; they become pointers again when the event is handed out. */
typedef struct demux_event_s {
	bvnr_event_t	ev;
	bvnr_data_t	d;
	size_t		data_off, frac_off, usrc_off, conv_off;
} demux_event_t;

struct bvnr_demux_s {
	bvnr_mux_on_msg_fn	on_message;
	void*			userdata;
	uint64_t		max_message;
	bool			in_octet;
	error_code_t		error;
	demux_channel_t*	chans;
	uint32_t		nchans;
	uint32_t		chans_cap;
	/* Slots whose buf/cap are set: the channels in use and, past nchans,
	 * the buffers of a previous document's channels, kept as spares. */
	uint32_t		chans_init;
	/* A chunk names its channel on every fragment, so the lookup is on the
	 * hot path, and a stream may use up to 4096 channels. Ids below
	 * BVN_DEMUX_SMALL are found in a direct table (index + 1, 0 for none);
	 * higher ones in an open-addressed hash table of the same shape, so
	 * neither costs a scan. `last` answers a producer that sends chunk after
	 * chunk on one channel even more cheaply. */
	uint16_t		small[BVN_DEMUX_SMALL];
	uint16_t*		hash;		/* index + 1, 0 empty; cap a power of two */
	uint32_t		hash_cap;
	uint32_t		last;		/* index + 1, 0 for none */
	/* Optional key scope (bvnr_demux_set_key). When key is non-NULL the demux
	 * only treats octet streams opened under a matching assignment key as
	 * multiplexed; any other octet stream in the same document is ignored, so a
	 * document may freely mix a mux stream with ordinary binary octet payloads.
	 * key == NULL (the default) demuxes every octet stream. key_match tracks
	 * whether the current assignment key matches. */
	char*			key;
	uint32_t		key_len;
	bool			key_match;
	/*
	 * Push-fed mode (bvnr_demux_open_feed): the demux owns a fed reader and
	 * sits on its on_verified, behind the caller's own callback (user_flags).
	 * Without an on_message callback it is drained with bvnr_demux_next: a mux
	 * chunk pauses the reader (bvn_lex_pause) and is held in chunk/chunk_len,
	 * where it stays valid until the reader resumes, and its messages are cut
	 * out of it one call at a time. With deliver_events, every other event of
	 * the document is copied into `events` for bvnr_demux_next as well.
	 */
	bvnr_reader_t*		reader;
	bvnr_read_flags_t	user_flags;
	bool			feeding;
	bool			deliver_events;
	bool			failed;
	bool			finished;
	bool			has_chunk;
	/* Set while a call runs the reader, so that a call from inside one of
	 * the callbacks it makes is refused (demux_reentered). */
	bool			busy;
	uint32_t		chunk_chan;
	const uint8_t*		chunk;
	uint32_t		chunk_len;
	uint32_t		chunk_pos;
	demux_event_t*		events;
	uint32_t		ev_count;
	uint32_t		ev_head;
	uint32_t		ev_cap;
	uint8_t*		arena;
	size_t			arena_len;
	size_t			arena_cap;
	bvnr_data_t		shown;
};

/* Pull mode with deliver_events: the reader pauses once this many events, or
 * this many of their bytes, wait to be handed out. */
#define BVNR_DEMUX_EVENT_BATCH   64u
#define BVNR_DEMUX_ARENA_BATCH   (64u * 1024u)

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
	for (uint32_t i = 0; i < dm->chans_init; i++)
		free(dm->chans[i].buf);
	free(dm->chans);
	free(dm->hash);
	free(dm->key);
	bvnr_reader_destroy(dm->reader);
	free(dm->events);
	free(dm->arena);
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
	if (!dm) return error_invalid_argument;
	/* In feed mode the read itself can fail -- a malformed document around
	 * the mux stream -- and that is the error when the demux has none. The
	 * demux's own error comes first, as it does for a demux installed on a
	 * reader of the whole buffer. */
	if (dm->error == error_none && dm->feeding)
		return bvnr_reader_get_error(dm->reader);
	return dm->error;
}

/*
 * The open-addressed table for channel ids of BVN_DEMUX_SMALL and up. The
 * capacity is a power of two with the channel count at half of it, so a
 * lookup ends at an empty slot; entries are the index into `chans` plus one
 * (0 empty) and the channel id is read back from `chans`, so a stale entry
 * can never match a different id. The Fibonacci constant spreads sequential
 * ids over the table.
 */
#define BVN_DEMUX_HASH_MIN 128u

static uint32_t demux_hash_slot(const bvnr_demux_t* dm, uint64_t id)
{
	uint32_t mask = dm->hash_cap - 1u;
	uint32_t s = (uint32_t)((id * 0x9E3779B97F4A7C15u) >> 32) & mask;
	while (dm->hash[s]) {
		if (dm->chans[(uint32_t)dm->hash[s] - 1u].channel == id)
			break;
		s = (s + 1u) & mask;
	}
	return s;
}

/* Index the channel at `chans[index]`, growing (and rehashing) first if the
 * table is half full. False only when the table cannot be allocated. */
static bool demux_hash_put(bvnr_demux_t* dm, uint32_t index)
{
	if (!dm->hash_cap ||
	    (uint64_t)(dm->nchans + 1u) * 2u > (uint64_t)dm->hash_cap) {
		uint32_t ncap = dm->hash_cap ? dm->hash_cap * 2u : BVN_DEMUX_HASH_MIN;
		while ((uint64_t)(dm->nchans + 1u) * 2u > (uint64_t)ncap)
			ncap *= 2u;
		uint16_t* nh = (uint16_t*)calloc(ncap, sizeof(*nh));
		if (!nh) return false;
		uint16_t* old = dm->hash;
		uint32_t oldcap = dm->hash_cap;
		dm->hash     = nh;
		dm->hash_cap = ncap;
		for (uint32_t s = 0; s < oldcap; s++) {
			if (!old[s]) continue;
			uint32_t i = (uint32_t)old[s] - 1u;
			dm->hash[demux_hash_slot(dm, dm->chans[i].channel)] =
				(uint16_t)(i + 1u);
		}
		free(old);
	}
	dm->hash[demux_hash_slot(dm, dm->chans[index].channel)] =
		(uint16_t)(index + 1u);
	return true;
}

static demux_channel_t* demux_get_channel(bvnr_demux_t* dm, uint64_t channel)
{
	/* The common cases first: a small id, or the channel of the chunk
	 * before (the one a producer keeps sending on). Neither scans. */
	if (channel < (uint64_t)BVN_DEMUX_SMALL) {
		uint16_t slot = dm->small[channel];
		if (slot) {
			dm->last = slot;
			return &dm->chans[(uint32_t)slot - 1u];
		}
	} else if (dm->hash_cap) {
		uint16_t slot = dm->hash[demux_hash_slot(dm, channel)];
		if (slot) {
			dm->last = slot;
			return &dm->chans[(uint32_t)slot - 1u];
		}
	}
	if (dm->last && dm->chans[dm->last - 1u].channel == channel)
		return &dm->chans[dm->last - 1u];
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
	/* Fill and index the slot first; the channel is committed (nchans,
	 * chans_init, last) only once the hash insert cannot fail, so an
	 * allocation failure leaves no channel counted but unreachable. */
	uint32_t index = dm->nchans;
	demux_channel_t* c = &dm->chans[index];
	/* A slot a previous document used keeps its buffer for the new id. */
	uint8_t* buf = NULL;
	uint64_t cap = 0;
	if (index < dm->chans_init) {
		buf = c->buf;
		cap = c->cap;
	}
	memset(c, 0, sizeof(*c));
	c->channel = channel;
	c->buf     = buf;
	c->cap     = cap;
	if (channel < (uint64_t)BVN_DEMUX_SMALL) {
		dm->small[channel] = (uint16_t)(index + 1u);
	} else if (!demux_hash_put(dm, index)) {
		dm->error = error_invalid_argument;
		return NULL;
	}
	dm->nchans = index + 1u;
	if (dm->chans_init < dm->nchans)
		dm->chans_init = dm->nchans;
	dm->last = index + 1u;
	return c;
}

/*
 * Cut the next message for `c` out of a chunk fragment, from *pos. The
 * fragment is a slice of the channel's logical stream: when no message is
 * active it begins with a varint(msg_len); thereafter it is raw message data.
 * Returns 1 with the message in *msg and *msg_len, 0 when the fragment is used up
 * without completing one (the progress is kept in `c` for the next chunk),
 * and -1 on a desync, an oversized length or an allocation failure.
 *
 * A message that lies whole in the fragment is handed out where it lies; only
 * one that spans chunks is reassembled in the channel's buffer. Either way it
 * is valid until the demux moves on.
 */
static int demux_take(bvnr_demux_t* dm, demux_channel_t* c,
	const uint8_t* frag, uint32_t fraglen, uint32_t* pos,
	const uint8_t** msg, uint64_t* msg_len)
{
	while (*pos < fraglen) {
		if (!c->active) {
			/* The common case: no partial length varint from an earlier
			 * fragment, and this one holds the whole varint and the whole
			 * message. Decoding it where it lies skips the byte-by-byte
			 * gathering below, which used to run even for a one-byte varint. */
			if (!c->lvhave) {
				uint64_t need = 0;
				uint32_t used = bvn_varint_decode(frag + *pos,
					fraglen - *pos, &need);
				if (used) {
					if (need > dm->max_message ||
					    need > (uint64_t)SIZE_MAX) {
						dm->error = error_value_out_of_range;
						return -1;
					}
					if ((uint64_t)(fraglen - *pos - used) >= need) {
						/* Whole in this chunk: no copy. A zero-length
						 * message gets a pointer into the chunk too, so it
						 * is never NULL. */
						*pos     += used;
						*msg      = frag + *pos;
						*msg_len  = need;
						*pos     += (uint32_t)need;
						return 1;
					}
				}
				/* A varint cut by the fragment's end, a malformed one, or
				 * a message that does not fit: the gathering path below
				 * tells these apart, exactly as it always has. */
			}
			/* Accumulate the message-length varint, which may itself span
			 * chunk boundaries: pull continuation bytes into the per-channel
			 * lvbuf until a terminator (high bit clear) arrives, then decode
			 * the whole varint. (The channel varint, by contrast, must be
			 * whole within one chunk — it is the routing prefix and is read by
			 * the caller before this function; see bvnr_demux_on_event.) */
			bool terminated = false;
			while (*pos < fraglen) {
				uint8_t b = frag[(*pos)++];
				if (c->lvhave >= 10u) {   /* >10 bytes can't fit a uint64 */
					dm->error = error_octet_stream_out_of_sync;
					return -1;
				}
				c->lvbuf[c->lvhave++] = b;
				if (!(b & 0x80u)) { terminated = true; break; }
			}
			if (!terminated)
				return 0;   /* fragment ended mid-varint: resume next chunk */
			uint64_t need = 0;
			uint32_t used = bvn_varint_decode(c->lvbuf, c->lvhave, &need);
			/* The accumulated run ends exactly at the terminator, so a clean
			 * varint consumes every buffered byte; any mismatch (used==0 from an
			 * overlong 10th byte, or a short decode) is a malformed encoding. */
			if (used != c->lvhave) {
				dm->error = error_octet_stream_out_of_sync; return -1;
			}
			c->lvhave = 0;
			if (need > dm->max_message) {
				dm->error = error_value_out_of_range; return -1;
			}
			/* SIZE_MAX guard for 32-bit hosts where the uint64 declared length
			 * can exceed size_t (parity with the doc-stream path); a caller may
			 * have set max_message above SIZE_MAX. */
			if (need > (uint64_t)SIZE_MAX) {
				dm->error = error_value_out_of_range; return -1;
			}
			if ((uint64_t)(fraglen - *pos) >= need) {
				/* Whole in this chunk: no copy. A zero-length message gets
				 * a pointer into the chunk too, so it is never NULL. */
				*msg     = frag + *pos;
				*msg_len = need;
				*pos    += (uint32_t)need;
				return 1;
			}
			c->need   = need;
			c->have   = 0;
			c->active = true;
			/* Deliberately do NOT pre-allocate `need` here. The declared
			 * length is attacker-controlled, so allocating it up-front would
			 * let a single tiny chunk (varint channel + varint len) force an
			 * allocation of up to max_message — a large memory-amplification
			 * vector. Instead the buffer grows with the data that actually
			 * arrives (below), so memory is bounded by real bytes received. */
		}
		uint64_t remaining = c->need - c->have;
		uint32_t take = (fraglen - *pos) < remaining
			? (fraglen - *pos) : (uint32_t)remaining;
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
				if (!nb) { dm->error = error_invalid_argument; return -1; }
				c->buf = nb;
				c->cap = newcap;
			}
			memcpy(c->buf + c->have, frag + *pos, take);
			c->have += take;
			*pos    += take;
		}
		if (c->have == c->need) {
			c->active = false;
			*msg      = c->buf;
			*msg_len  = c->need;
			return 1;
		}
	}
	return 0;
}

/* Every message a fragment completes, to on_message. Loops so a fragment that
 * completes one message and starts the next is handled (a packed producer),
 * though bvnr_mux_send never packs. */
static bool demux_feed(bvnr_demux_t* dm, demux_channel_t* c,
	const uint8_t* frag, uint32_t fraglen)
{
	uint32_t pos = 0;
	for (;;) {
		const uint8_t* msg = NULL;
		uint64_t msg_len = 0;
		int rc = demux_take(dm, c, frag, fraglen, &pos, &msg, &msg_len);
		if (rc < 0) return false;
		if (rc == 0) return true;
		if (dm->on_message &&
		    !dm->on_message(dm->userdata, c->channel, msg, msg_len)) {
			dm->error = error_scanner_callback_failed;
			return false;
		}
	}
}

/* The chunk's routing prefix: which channel it belongs to. NULL (with the
 * error latched) for a malformed varint or a 4097th channel. */
static demux_channel_t* demux_chunk_channel(bvnr_demux_t* dm,
	const bvnr_data_t* d, uint32_t* used)
{
	uint64_t channel = 0;
	*used = bvn_varint_decode((const uint8_t*)d->data, d->length, &channel);
	if (!*used) { dm->error = error_octet_stream_out_of_sync; return NULL; }
	return demux_get_channel(dm, channel);
}

/* Is this event a chunk of a multiplexed octet stream? */
static bool demux_is_mux_chunk(const bvnr_demux_t* dm, bvnr_event_t ev,
	const bvnr_data_t* d)
{
	return ev == ev_data && dm->in_octet && d &&
	       d->type == token_is_octet_stream;
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
		if (demux_is_mux_chunk(dm, ev, d)) {
			uint32_t used;
			demux_channel_t* c = demux_chunk_channel(dm, d, &used);
			if (!c) return false;
			return demux_feed(dm, c, (const uint8_t*)d->data + used,
					  d->length - used);
		}
		return true;
	default:
		return true;
	}
}

/* ---- demultiplexer: push-fed mode ----------------------------------------*/

/* Copy `n` bytes into the arena; *off receives where, SIZE_MAX for "none". */
static bool demux_arena_put(bvnr_demux_t* dm, const void* p, size_t n,
	size_t* off)
{
	*off = SIZE_MAX;
	if (!p) return true;
	if (dm->arena_len + n > dm->arena_cap) {
		size_t nc = dm->arena_cap ? dm->arena_cap : 4096u;
		while (nc < dm->arena_len + n) {
			if (nc > SIZE_MAX / 2u) return false;
			nc *= 2u;
		}
		uint8_t* na = (uint8_t*)realloc(dm->arena, nc);
		if (!na) return false;
		dm->arena     = na;
		dm->arena_cap = nc;
	}
	if (n) memcpy(dm->arena + dm->arena_len, p, n);
	*off = dm->arena_len;
	dm->arena_len += n;
	return true;
}

static bool demux_queue_event(bvnr_demux_t* dm, bvnr_event_t ev,
	const bvnr_data_t* d)
{
	if (dm->ev_count == dm->ev_cap) {
		uint32_t nc = dm->ev_cap ? dm->ev_cap * 2u : 64u;
		demux_event_t* ne = (demux_event_t*)realloc(dm->events,
			(size_t)nc * sizeof(*ne));
		if (!ne) return false;
		dm->events = ne;
		dm->ev_cap = nc;
	}
	demux_event_t* e = &dm->events[dm->ev_count];
	memset(e, 0, sizeof(*e));
	e->ev = ev;
	if (d) e->d = *d;
	/* The conversion's exact rational lives in the reader's scratch and is
	 * not carried over; its rendered text is. */
	e->d.conv.num = NULL;
	e->d.conv.den = NULL;
	if (!demux_arena_put(dm, d ? d->data : NULL, d ? d->length : 0,
			     &e->data_off) ||
	    !demux_arena_put(dm, d ? d->frac_data : NULL,
			     d ? d->frac_length : 0, &e->frac_off) ||
	    !demux_arena_put(dm, d ? d->unit_source : NULL,
			     d ? d->unit_source_length : 0, &e->usrc_off) ||
	    !demux_arena_put(dm, d ? d->conv.text : NULL,
			     d && d->conv.text ? (size_t)d->conv.length + 1u : 0,
			     &e->conv_off))
		return false;
	dm->ev_count++;
	return true;
}

/*
 * The fed reader's on_verified. The caller's own callback sees every event
 * first, unchanged. Then the demux keeps its scope state; a mux chunk is
 * either demultiplexed on the spot (on_message set) or held, with the reader
 * paused, for bvnr_demux_next; and with deliver_events every other event is
 * queued for bvnr_demux_next too.
 */
static bool demux_feed_on_verified(void* ud, bvnr_event_t ev, bvnr_data_t* d)
{
	bvnr_demux_t* dm = (bvnr_demux_t*)ud;
	if (dm->user_flags.on_verified &&
	    !dm->user_flags.on_verified(dm->user_flags.userdata, ev, d))
		return false;
	if (dm->on_message)
		return bvnr_demux_on_event(dm, ev, d);
	if (demux_is_mux_chunk(dm, ev, d)) {
		uint32_t used;
		demux_channel_t* c = demux_chunk_channel(dm, d, &used);
		if (!c) return false;
		dm->has_chunk  = true;
		dm->chunk_chan = (uint32_t)(c - dm->chans);
		dm->chunk      = (const uint8_t*)d->data;
		dm->chunk_len  = d->length;
		dm->chunk_pos  = used;
		bvn_lex_pause(dm->reader);
		return true;
	}
	if (!bvnr_demux_on_event(dm, ev, d))
		return false;
	if (dm->deliver_events) {
		if (!demux_queue_event(dm, ev, d)) {
			dm->error = error_invalid_argument;
			return false;
		}
		/* Hand out what is queued before reading on, so that the queue holds
		 * a batch and not every event of a large feed: a feed of small values
		 * would otherwise be copied whole, at many times its size, before the
		 * first bvnr_demux_next returned. */
		if (dm->ev_count - dm->ev_head >= BVNR_DEMUX_EVENT_BATCH ||
		    dm->arena_len >= BVNR_DEMUX_ARENA_BATCH)
			bvn_lex_pause(dm->reader);
	}
	return true;
}

static bool demux_feed_on_unverified(void* ud, bvnr_event_t ev,
	bvnr_data_t* d)
{
	bvnr_demux_t* dm = (bvnr_demux_t*)ud;
	return dm->user_flags.on_unverified(dm->user_flags.userdata, ev, d);
}
static void demux_feed_on_error(void* ud, error_code_t err, uint64_t line,
	uint64_t column, uint32_t byte, uint64_t offset)
{
	bvnr_demux_t* dm = (bvnr_demux_t*)ud;
	dm->user_flags.on_error(dm->user_flags.userdata, err, line, column,
				byte, offset);
}
static bool demux_feed_want_unit(void* ud, const bvnr_data_t* d,
	value_unit_t* want, uint32_t* want_base)
{
	bvnr_demux_t* dm = (bvnr_demux_t*)ud;
	return dm->user_flags.want_unit(dm->user_flags.userdata, d, want,
					want_base);
}

/* A feed, next, finish or re-open from inside one of the callbacks a call
 * running the reader makes: the stream fails with error_invalid_argument,
 * and the call running stops right after the event that called. */
static bool demux_reentered(bvnr_demux_t* dm)
{
	if (!dm->failed && dm->error == error_none)
		dm->error = error_invalid_argument;
	dm->failed = true;
	bvn_lex_pause(dm->reader);
	return false;
}

bool bvnr_demux_open_feed(bvnr_demux_t* dm, const bvnr_read_flags_t* flags,
	bool deliver_events)
{
	if (!dm) return false;
	/* The reader a re-open would tear down is the one running. */
	if (dm->busy)
		return demux_reentered(dm);
	/* Not open until the reader is: a failed re-open must not leave the
	 * demux feeding a reader that open has torn down. */
	dm->feeding   = false;
	dm->has_chunk = false;
	if (!dm->reader && !(dm->reader = bvnr_reader_create()))
		return false;
	bvnr_read_flags_t f;
	if (flags)
		f = *flags;
	else
		memset(&f, 0, sizeof(f));
	dm->user_flags  = f;
	/* The reader has one userdata for all its callbacks, and it is the
	 * demux; the caller's callbacks are called through it with theirs. */
	f.userdata      = dm;
	f.on_verified   = demux_feed_on_verified;
	f.on_unverified = f.on_unverified ? demux_feed_on_unverified : NULL;
	f.on_error      = f.on_error ? demux_feed_on_error : NULL;
	f.want_unit     = f.want_unit ? demux_feed_want_unit : NULL;
	if (!bvnr_open_read_feed(dm->reader, NULL, &f))
		return false;
	/* A new document is a new mux stream: its channel ids are its own, so
	 * the 4096-channel limit counts afresh, as it does for the Rust codec's
	 * MuxReader::reset. The buffers stay, as spares for the new ids; the
	 * lookup table and the last-channel hint describe the old ids. */
	dm->nchans = 0;
	memset(dm->small, 0, sizeof(dm->small));
	if (dm->hash)
		memset(dm->hash, 0, (size_t)dm->hash_cap * sizeof(*dm->hash));
	dm->last = 0;
	dm->error          = error_none;
	dm->in_octet       = false;
	dm->key_match      = false;
	dm->feeding        = true;
	dm->deliver_events = deliver_events && !dm->on_message;
	dm->failed         = false;
	dm->finished       = false;
	dm->has_chunk      = false;
	dm->ev_count = dm->ev_head = 0;
	dm->arena_len = 0;
	return true;
}

/* Pull mode with something not yet handed out: input the reader has not
 * reached, a held chunk, or queued events. */
static bool demux_undrained(const bvnr_demux_t* dm)
{
	return dm->has_chunk || dm->ev_head < dm->ev_count ||
	       bvn_lex_feed_pending(&dm->reader->lex);
}

bool bvnr_demux_feed(bvnr_demux_t* dm, const void* buf, uint64_t len)
{
	if (!dm || !dm->feeding || dm->finished || dm->failed ||
	    (!buf && len))
		return false;
	if (dm->busy)
		return demux_reentered(dm);
	if (!dm->on_message) {
		/* Pull mode reads lazily, in bvnr_demux_next, so that a message can
		 * be handed out from this very buffer. */
		if (demux_undrained(dm))
			return false;
		bvn_lex_feed_attach(dm->reader, (const uint8_t*)buf, len);
		return true;
	}
	bvn_lex_feed_attach(dm->reader, (const uint8_t*)buf, len);
	dm->busy = true;
	int rc = bvn_lex_feed_run(dm->reader);
	dm->busy = false;
	if (rc == BVN_FEED_ERROR)
		dm->failed = true;
	return !dm->failed;
}

int bvnr_demux_next(bvnr_demux_t* dm, bvnr_demux_item_t* item)
{
	if (!dm || !item || !dm->feeding || dm->on_message)
		return BVNR_NEXT_ERROR;
	if (dm->busy) {
		demux_reentered(dm);
		return BVNR_NEXT_ERROR;
	}
	for (;;) {
		if (dm->ev_head < dm->ev_count) {
			const demux_event_t* e = &dm->events[dm->ev_head++];
			dm->shown = e->d;
/* Nothing allocated yet means every byte field so far was empty: such a field
 * is still a valid pointer, as the reader passed it. */
#define BVN_DM_AT(off) ((off) == SIZE_MAX ? NULL : \
	dm->arena ? (const void*)(dm->arena + (off)) : (const void*)"")
			dm->shown.data        = BVN_DM_AT(e->data_off);
			dm->shown.frac_data   = BVN_DM_AT(e->frac_off);
			dm->shown.unit_source = BVN_DM_AT(e->usrc_off);
			dm->shown.conv.text   = (const char*)BVN_DM_AT(e->conv_off);
#undef BVN_DM_AT
			memset(item, 0, sizeof(*item));
			item->event = e->ev;
			item->value = &dm->shown;
			return BVNR_NEXT_EVENT;
		}
		if (dm->has_chunk) {
			const uint8_t* msg = NULL;
			uint64_t msg_len = 0;
			demux_channel_t* c = &dm->chans[dm->chunk_chan];
			int rc = demux_take(dm, c, dm->chunk, dm->chunk_len,
					    &dm->chunk_pos, &msg, &msg_len);
			if (rc < 0) {
				/* The reader ends as it does when on_message mode's
				 * callback refuses the chunk, so its error position is
				 * the same in both modes. */
				bvn_lex_abort_paused(dm->reader);
				dm->failed    = true;
				dm->has_chunk = false;
				return BVNR_NEXT_ERROR;
			}
			if (rc > 0) {
				memset(item, 0, sizeof(*item));
				item->event   = ev_data;
				item->channel = c->channel;
				item->data    = msg;
				item->length  = msg_len;
				return BVNR_NEXT_MESSAGE;
			}
			dm->has_chunk = false;
		}
		/* Everything handed out is now past its "until the next call". */
		dm->ev_count = dm->ev_head = 0;
		dm->arena_len = 0;
		if (dm->failed)
			return BVNR_NEXT_ERROR;
		if (!bvn_lex_feed_pending(&dm->reader->lex))
			return BVNR_NEXT_NONE;
		/* An error still lets the events queued before it out first. */
		dm->busy = true;
		int rc = bvn_lex_feed_run(dm->reader);
		dm->busy = false;
		if (rc == BVN_FEED_ERROR)
			dm->failed = true;
	}
}

bool bvnr_demux_finish(bvnr_demux_t* dm)
{
	if (!dm || !dm->feeding || dm->finished)
		return false;
	if (dm->busy)
		return demux_reentered(dm);
	if (!dm->on_message && !dm->failed && demux_undrained(dm))
		return false;
	dm->finished = true;
	if (dm->failed)
		return false;
	dm->busy = true;
	bool ok = bvn_lex_feed_end(dm->reader);
	dm->busy = false;
	if (!ok)
		dm->failed = true;
	return !dm->failed;
}

const bvnr_reader_t* bvnr_demux_get_reader(const bvnr_demux_t* dm)
{
	return dm && dm->feeding ? dm->reader : NULL;
}

/* ===========================================================================
 * 2b. Push-fed frame reader
 * =========================================================================== */

struct bvnr_frame_reader_s {
	/* Pull mode hands documents out through bvnr_frame_reader_next; parse
	 * mode parses each with `reader` as bvnr_doc_stream_read does. */
	bool			pull;
	bvnr_doc_stream_opts_t	opts;
	bvnr_read_flags_t	flags;
	bool			has_flags;
	uint64_t		cap;
	bvnr_reader_t*		reader;
	uint8_t			hdr[BVNR_FRAME_HEADER_SIZE];
	uint32_t		hdr_have;
	bool			in_payload;
	uint64_t		len;
	uint64_t		have;
	/* Pull mode: the fed bytes not yet handed out, and the buffer a document
	 * split across feeds is gathered in. */
	const uint8_t*		in;
	uint64_t		in_left;
	uint8_t*		buf;
	uint64_t		buf_cap;
	uint64_t		count;
	error_code_t		error;
	bool			failed;
	bool			finished;
	/* Parse mode: set while a feed runs, so that a feed or finish from
	 * inside on_document or the reader's callbacks is refused. */
	bool			busy;
};

static bvnr_frame_reader_t* frame_reader_new(uint64_t max_document_size)
{
	bvnr_frame_reader_t* fr = (bvnr_frame_reader_t*)calloc(1, sizeof(*fr));
	if (!fr) return NULL;
	fr->cap   = max_document_size ? max_document_size : BVNR_DOC_DEFAULT_MAX;
	fr->error = error_none;
	return fr;
}

bvnr_frame_reader_t* bvnr_frame_reader_create(
	const bvnr_doc_stream_opts_t* opts)
{
	bvnr_frame_reader_t* fr = frame_reader_new(
		opts ? opts->max_document_size : 0);
	if (!fr) return NULL;
	if (opts) {
		fr->opts = *opts;
		if (opts->flags) {
			fr->flags     = *opts->flags;
			fr->has_flags = true;
		}
	}
	fr->reader = bvnr_reader_create();
	if (!fr->reader) { free(fr); return NULL; }
	return fr;
}

bvnr_frame_reader_t* bvnr_frame_reader_create_pull(uint64_t max_document_size)
{
	bvnr_frame_reader_t* fr = frame_reader_new(max_document_size);
	if (fr) fr->pull = true;
	return fr;
}

void bvnr_frame_reader_destroy(bvnr_frame_reader_t* fr)
{
	if (!fr) return;
	bvnr_reader_destroy(fr->reader);
	free(fr->buf);
	free(fr);
}

static bool frame_fail(bvnr_frame_reader_t* fr, error_code_t err)
{
	fr->error  = err;
	fr->failed = true;
	return false;
}

/* Take header bytes from *p; true once the header is whole and accepted. */
static bool frame_header(bvnr_frame_reader_t* fr, const uint8_t** p,
	uint64_t* left)
{
	uint32_t take = BVNR_FRAME_HEADER_SIZE - fr->hdr_have;
	if ((uint64_t)take > *left) take = (uint32_t)*left;
	memcpy(fr->hdr + fr->hdr_have, *p, take);
	fr->hdr_have += take;
	*p    += take;
	*left -= take;
	if (fr->hdr_have < BVNR_FRAME_HEADER_SIZE)
		return false;
	fr->hdr_have = 0;
	if (fr->hdr[0] != BVNR_FRAME_MAGIC0 || fr->hdr[1] != BVNR_FRAME_MAGIC1 ||
	    fr->hdr[2] != BVNR_FRAME_MAGIC2 || fr->hdr[3] != BVNR_FRAME_MAGIC3)
		return frame_fail(fr, error_unexpected_input_byte);
	fr->len = bvn_get_u64le(fr->hdr + 4);
	/* As in bvnr_doc_stream_read: the cap is always finite unless the caller
	 * passed a huge one, and on a 32-bit host a pull-mode document must also
	 * fit size_t to be gathered. */
	if (fr->len > fr->cap || (fr->pull && fr->len > (uint64_t)SIZE_MAX))
		return frame_fail(fr, error_value_out_of_range);
	fr->have       = 0;
	fr->in_payload = true;
	return true;
}

/* Parse mode: one document is over. Reports it, then applies the sequence
 * policy of bvnr_doc_stream_read. */
static bool frame_document_done(bvnr_frame_reader_t* fr)
{
	bool ok = bvnr_read_finish(fr->reader);
	if (fr->failed)		/* a callback called back in */
		return false;
	error_code_t err = bvnr_reader_get_error(fr->reader);
	uint64_t index = fr->count++;
	fr->in_payload = false;
	if (fr->opts.on_document &&
	    !fr->opts.on_document(fr->opts.userdata, index, ok, err))
		return frame_fail(fr, error_scanner_callback_failed);
	/* As in bvnr_doc_stream_read_ex: a failed document never ends the
	 * stream looking clean. */
	if (!ok && !fr->opts.continue_past_failed)
		return frame_fail(fr, err != error_none
			? err : error_got_incomplete_bvnr_stream);
	return true;
}

static bool frame_feed_parse(bvnr_frame_reader_t* fr, const uint8_t* p,
	uint64_t len);

/* A feed or finish from inside a callback of a running feed. */
static bool frame_reentered(bvnr_frame_reader_t* fr)
{
	bvn_lex_pause(fr->reader);
	return fr->failed ? false : frame_fail(fr, error_invalid_argument);
}

bool bvnr_frame_reader_feed(bvnr_frame_reader_t* fr, const void* buf,
	uint64_t len)
{
	if (!fr || fr->failed || fr->finished || (!buf && len))
		return false;
	const uint8_t* p = (const uint8_t*)buf;
	if (fr->pull) {
		if (fr->in_left)
			return false;
		fr->in      = p;
		fr->in_left = len;
		return true;
	}
	/* From inside on_document or one of the reader's callbacks: the feed
	 * running fails too, its reader right after the event that called. */
	if (fr->busy)
		return frame_reentered(fr);
	fr->busy = true;
	bool ok = frame_feed_parse(fr, p, len);
	fr->busy = false;
	return ok && !fr->failed;
}

/* Parse mode's feed: the frames of `len` bytes at `p`. */
static bool frame_feed_parse(bvnr_frame_reader_t* fr, const uint8_t* p,
	uint64_t len)
{
	while (len && !fr->failed) {
		if (!fr->in_payload) {
			if (!frame_header(fr, &p, &len)) {
				if (fr->failed) return false;
				continue;
			}
			if (!bvnr_open_read_feed(fr->reader, NULL,
					fr->has_flags ? &fr->flags : NULL))
				return frame_fail(fr, error_invalid_argument);
			/* A zero-length frame is an empty document, complete as
			 * soon as its header is. */
			if (!fr->len && !frame_document_done(fr))
				return false;
			continue;
		}
		uint64_t take = fr->len - fr->have;
		if (take > len) take = len;
		/* A document that has already failed reads to the end of its frame
		 * regardless: the frame says where the next one starts. */
		(void)bvnr_read_feed(fr->reader, p, take);
		if (fr->failed)		/* a callback called back in */
			return false;
		fr->have += take;
		p   += take;
		len -= take;
		if (fr->have == fr->len && !frame_document_done(fr))
			return false;
	}
	return true;
}

int bvnr_frame_reader_next(bvnr_frame_reader_t* fr, const uint8_t** doc,
	uint64_t* len)
{
	if (!fr || !doc || !len || !fr->pull)
		return BVNR_NEXT_ERROR;
	for (;;) {
		if (fr->failed)
			return BVNR_NEXT_ERROR;
		if (!fr->in_payload) {
			if (!fr->in_left)
				return BVNR_NEXT_NONE;
			if (!frame_header(fr, &fr->in, &fr->in_left))
				continue;
		}
		uint64_t want = fr->len - fr->have;
		if (fr->have == 0 && fr->in_left >= want) {
			/* Whole in the fed buffer: handed out where it lies. */
			*doc = fr->len ? fr->in : (const uint8_t*)"";
			*len = fr->len;
			fr->in      += want;
			fr->in_left -= want;
			fr->in_payload = false;
			fr->count++;
			return BVNR_NEXT_DOCUMENT;
		}
		if (!fr->in_left)
			return BVNR_NEXT_NONE;
		uint64_t take = fr->in_left < want ? fr->in_left : want;
		if (fr->have + take > fr->buf_cap) {
			/* Grow with the bytes that actually arrived, never to the
			 * declared length up front: the length is the sender's word. */
			uint64_t nc = fr->buf_cap ? fr->buf_cap : 4096u;
			while (nc < fr->have + take)
				nc = nc > fr->len / 2u ? fr->len : nc * 2u;
			uint8_t* nb = (uint8_t*)realloc(fr->buf, (size_t)nc);
			if (!nb) {
				frame_fail(fr, error_invalid_argument);
				return BVNR_NEXT_ERROR;
			}
			fr->buf     = nb;
			fr->buf_cap = nc;
		}
		memcpy(fr->buf + fr->have, fr->in, (size_t)take);
		fr->have    += take;
		fr->in      += take;
		fr->in_left -= take;
		if (fr->have == fr->len) {
			*doc = fr->buf;
			*len = fr->len;
			fr->in_payload = false;
			fr->count++;
			return BVNR_NEXT_DOCUMENT;
		}
	}
}

bool bvnr_frame_reader_finish(bvnr_frame_reader_t* fr)
{
	if (!fr || fr->finished)
		return false;
	if (fr->busy)
		return frame_reentered(fr);
	if (fr->pull && !fr->failed && fr->in_left)
		return false;
	fr->finished = true;
	if (fr->failed)
		return false;
	/* A clean end is one on a frame boundary, before any header byte. */
	if (fr->hdr_have || fr->in_payload)
		return frame_fail(fr, error_got_incomplete_bvnr_stream);
	return true;
}

error_code_t bvnr_frame_reader_error(const bvnr_frame_reader_t* fr)
{
	return fr ? fr->error : error_invalid_argument;
}

uint64_t bvnr_frame_reader_count(const bvnr_frame_reader_t* fr)
{
	return fr ? fr->count : 0;
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
