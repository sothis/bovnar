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
 * Exercises the four streaming/framing capabilities layered on the lexer/
 * validator event API (include/bovnar_stream.h):
 *   1. endless mode      — BVNR_FILESIZE_UNLIMITED (0, the default) uncaps the byte counter
 *   2. multi-document    — length-prefixed record framing round-trip
 *   3. octet multiplex   — out-of-band channel convention round-trip
 *   4. doc-in-document   — embed + recursive parse round-trip
 */

/* Must precede every system header (glibc latches feature-test macros on the
 * first include): selects the POSIX 2008 surface the fd-framing test needs
 * (fork/pipe/waitpid/nanosleep). Ignored by MSVC, where that test is excluded. */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#  define _POSIX_C_SOURCE 200809L
#endif

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bovnar.h"
#include "bovnar_stream.h"

/* The framing-over-a-real-fd test drives bvnr_doc_stream_read across forced
 * short reads, which needs fork(2)/pipe(2)/usleep(3) — POSIX only. On Windows it
 * is compiled out (the in-memory framing tests still cover the codec there). */
#if !defined(_WIN32)
#  include <time.h>
#  include <unistd.h>
#  include <sys/wait.h>
#endif

static int g_failures = 0;
static int g_tests    = 0;

#define ASSERT_TRUE(cond, msg) do {                                          \
	g_tests++;                                                               \
	if (!(cond)) {                                                           \
		fprintf(stderr, "  FAIL line %d: %s\n", __LINE__, (msg));           \
		g_failures++;                                                        \
	}                                                                        \
} while (0)

#define ASSERT_EQ_UINT(a, b, msg) do {                                       \
	g_tests++;                                                               \
	uint64_t _a = (uint64_t)(a), _b = (uint64_t)(b);                        \
	if (_a != _b) {                                                          \
		fprintf(stderr, "  FAIL line %d: %s\n  got %" PRIu64                \
						", expected %" PRIu64 "\n",                          \
				__LINE__, (msg), _a, _b);                                    \
		g_failures++;                                                        \
	}                                                                        \
} while (0)

/* ---------------------------------------------------------------------------
 * Tiny document builder: writes ".id = <id>; .name = \"doc<id>\";" into buf.
 * ------------------------------------------------------------------------- */
static uint64_t build_doc(uint8_t *buf, size_t cap, uint32_t id)
{
	bvnr_writer_t *w = bvnr_writer_create();
	if (!w) return 0;
	if (!bvnr_open_write_mem(w, buf, cap, false, NULL)) {
		bvnr_writer_destroy(w);
		return 0;
	}
	bvnr_data_t hdr = {0};
	char name[32];
	snprintf(name, sizeof(name), "doc%u", id);
	bool ok = bvnr_write_event(w, ev_stream_start, &hdr)
		&& bvnr_write_uint(w, "id", 32, id)
		&& bvnr_write_string(w, "name", name)
		&& bvnr_write_finish(w);
	uint64_t n = ok ? bvnr_writer_bytes_written(w) : 0;
	bvnr_writer_destroy(w);
	return n;
}

/* Capture context shared by several tests: records id + name seen. */
typedef struct {
	uint32_t id;
	bool     have_id;
	char     name[64];
	bool     have_name;
	char     pending_key[64];
	bool     error;
} doc_capture_t;

static bool doc_capture_cb(void *ud, bvnr_event_t ev, bvnr_data_t *d)
{
	doc_capture_t *c = ud;
	switch (ev) {
	case ev_assignment_start:
		memset(c->pending_key, 0, sizeof(c->pending_key));
		if (d->length && d->length < sizeof(c->pending_key)) {
			memcpy(c->pending_key, d->data, d->length);
			c->pending_key[d->length] = '\0';
		}
		break;
	case ev_data:
		if (strcmp(c->pending_key, "id") == 0 && d->data && d->length) {
			char tmp[32];
			uint32_t n = d->length < 31u ? d->length : 31u;
			memcpy(tmp, d->data, n); tmp[n] = '\0';
			c->id = (uint32_t)strtoul(tmp, NULL, 10);
			c->have_id = true;
		} else if (strcmp(c->pending_key, "name") == 0 && d->data) {
			uint32_t n = d->length < sizeof(c->name) - 1u
				   ? d->length : (uint32_t)sizeof(c->name) - 1u;
			memcpy(c->name, d->data, n); c->name[n] = '\0';
			c->have_name = true;
		}
		break;
	default:
		break;
	}
	return true;
}

static void on_err(void *ud, error_code_t err, uint64_t line, uint64_t col,
				   uint32_t byte, uint64_t off)
{
	(void)line; (void)col; (void)byte; (void)off;
	doc_capture_t *c = ud;
	if (c) c->error = true;
	fprintf(stderr, "  [reader error] %s\n", bvn_error_to_string(err));
}

/* ===========================================================================
 * 1. Endless mode
 * =========================================================================== */
static void test_endless_knob(void)
{
	printf("  test_endless_knob...\n");
	uint8_t doc[256];
	uint64_t n = build_doc(doc, sizeof(doc), 7);
	ASSERT_TRUE(n > 16, "doc built");

	/* A tiny cap must reject the stream as too long. */
	{
		doc_capture_t c = {0};
		bvnr_read_flags_t fl = {
			.on_verified = doc_capture_cb, .on_error = on_err,
			.userdata = &c, .max_file_size = 10,
		};
		bvnr_reader_t *r = bvnr_reader_create();
		bool ok = bvnr_open_read_mem(r, doc, n, NULL, 0, &fl) && bvnr_read(r);
		ASSERT_TRUE(!ok, "tiny max_file_size rejects the stream");
		ASSERT_EQ_UINT(bvnr_reader_get_error(r), error_file_too_long,
					   "rejection is error_file_too_long");
		bvnr_reader_destroy(r);
	}

	/* BVNR_FILESIZE_UNLIMITED is 0 (the zero-initialised default): no byte-count
	 * cap, so the same input parses. This locks the 0 == endless convention. */
	ASSERT_EQ_UINT(BVNR_FILESIZE_UNLIMITED, 0u, "unlimited sentinel is 0");
	{
		doc_capture_t c = {0};
		bvnr_read_flags_t fl = {
			.on_verified = doc_capture_cb, .on_error = on_err,
			.userdata = &c, .max_file_size = BVNR_FILESIZE_UNLIMITED,
		};
		bvnr_reader_t *r = bvnr_reader_create();
		bool ok = bvnr_open_read_mem(r, doc, n, NULL, 0, &fl) && bvnr_read(r);
		ASSERT_TRUE(ok, "BVNR_FILESIZE_UNLIMITED parses the stream");
		ASSERT_TRUE(c.have_id && c.id == 7u, "id round-trips under endless mode");
		bvnr_reader_destroy(r);
	}
}

/* ===========================================================================
 * 2. Multi-document framing
 * =========================================================================== */
typedef struct {
	uint64_t count;
	uint32_t ids[8];
	char     names[8][64];
	bool     all_ok;
} multidoc_ctx_t;

static multidoc_ctx_t *g_md;           /* on_document has no per-doc userdata slot */
static doc_capture_t   g_md_doc;        /* reused per document */

static bool md_on_document(void *ud, uint64_t index, bool ok, error_code_t err)
{
	(void)ud; (void)err;
	if (index < 8 && ok) {
		g_md->ids[index] = g_md_doc.id;
		snprintf(g_md->names[index], 64, "%s", g_md_doc.name);
	}
	if (!ok) g_md->all_ok = false;
	memset(&g_md_doc, 0, sizeof(g_md_doc));   /* reset for next document */
	return true;
}

static void test_multidoc(void)
{
	printf("  test_multidoc...\n");
	uint8_t big[4096];
	bvnr_sink_t sink;
	bvnr_sink_to_mem(&sink, big, sizeof(big));

	const uint32_t ids[3] = { 100, 200, 300 };
	bool framed = true;
	for (int i = 0; i < 3; i++) {
		uint8_t doc[256];
		uint64_t n = build_doc(doc, sizeof(doc), ids[i]);
		framed = framed && n > 0 && bvnr_frame_write(&sink, doc, n);
	}
	ASSERT_TRUE(framed, "three documents framed into one buffer");
	uint64_t total = bvnr_sink_bytes_written(&sink);

	multidoc_ctx_t mc = {0};
	mc.all_ok = true;
	g_md = &mc;
	memset(&g_md_doc, 0, sizeof(g_md_doc));

	bvnr_read_flags_t fl = {
		.on_verified = doc_capture_cb, .on_error = on_err,
		.userdata = &g_md_doc,
	};
	bvnr_doc_stream_opts_t opts = {
		.flags = &fl, .on_document = md_on_document,
	};
	bvnr_source_t src;
	bvnr_source_from_mem(&src, big, total);
	uint64_t count = 0;
	bool ok = bvnr_doc_stream_read(&src, &opts, &count);

	ASSERT_TRUE(ok, "doc stream read succeeds");
	ASSERT_TRUE(mc.all_ok, "every framed document parsed");
	ASSERT_EQ_UINT(count, 3, "three documents demuxed from the frame stream");
	ASSERT_EQ_UINT(mc.ids[0], 100, "doc 0 id");
	ASSERT_EQ_UINT(mc.ids[1], 200, "doc 1 id");
	ASSERT_EQ_UINT(mc.ids[2], 300, "doc 2 id");
	ASSERT_TRUE(strcmp(mc.names[2], "doc300") == 0, "doc 2 name");
}

/* ===========================================================================
 * 3. Octet multiplexing
 * =========================================================================== */
typedef struct {
	int      n;
	uint64_t channels[16];
	char     payloads[16][64];
	uint64_t lens[16];
} mux_capture_t;

static bool mux_on_msg(void *ud, uint64_t channel, const uint8_t *data, uint64_t len)
{
	mux_capture_t *m = ud;
	if (m->n < 16) {
		m->channels[m->n] = channel;
		uint64_t k = len < 63u ? len : 63u;
		memcpy(m->payloads[m->n], data, (size_t)k);
		m->payloads[m->n][k] = '\0';
		m->lens[m->n] = len;
		m->n++;
	}
	return true;
}

static void test_octet_mux(void)
{
	printf("  test_octet_mux...\n");
	uint8_t buf[1024];

	/* Producer: three messages interleaved across channels 1, 42, 1. */
	bvnr_writer_t *w = bvnr_writer_create();
	bvnr_data_t hdr = {0};
	bool wok = bvnr_open_write_mem(w, buf, sizeof(buf), false, NULL)
		&& bvnr_write_event(w, ev_stream_start, &hdr)
		&& bvnr_mux_begin(w, "mux")
		&& bvnr_mux_send(w, 1,    "hello", 5)
		&& bvnr_mux_send(w, 42,   "world!!", 7)
		&& bvnr_mux_send(w, 1,    "bye", 3)
		&& bvnr_mux_end(w)
		&& bvnr_write_finish(w);
	ASSERT_TRUE(wok, "mux producer writes cleanly");
	uint64_t total = bvnr_writer_bytes_written(w);
	bvnr_writer_destroy(w);

	/* Consumer: demux by channel. */
	mux_capture_t mc = {0};
	bvnr_demux_t *demux = bvnr_demux_create(mux_on_msg, &mc, 0);
	bvnr_read_flags_t fl = {
		.on_verified = bvnr_demux_on_event, .on_error = on_err,
		.userdata = demux,
	};
	bvnr_reader_t *r = bvnr_reader_create();
	bool rok = bvnr_open_read_mem(r, buf, total, NULL, 0, &fl) && bvnr_read(r);
	ASSERT_TRUE(rok, "mux consumer parses cleanly");
	ASSERT_EQ_UINT(bvnr_demux_error(demux), error_none, "no demux desync");
	bvnr_reader_destroy(r);
	bvnr_demux_destroy(demux);

	ASSERT_EQ_UINT(mc.n, 3, "three multiplexed messages recovered");
	if (mc.n == 3) {
		ASSERT_EQ_UINT(mc.channels[0], 1,  "msg0 channel");
		ASSERT_TRUE(strcmp(mc.payloads[0], "hello") == 0, "msg0 payload");
		ASSERT_EQ_UINT(mc.channels[1], 42, "msg1 channel (multi-byte varint)");
		ASSERT_EQ_UINT(mc.lens[1], 7, "msg1 length");
		ASSERT_TRUE(strcmp(mc.payloads[1], "world!!") == 0, "msg1 payload");
		ASSERT_EQ_UINT(mc.channels[2], 1,  "msg2 channel");
		ASSERT_TRUE(strcmp(mc.payloads[2], "bye") == 0, "msg2 payload");
	}
}

/* Regression (deep-review pass 4): a message that is truncated when its octet
 * stream ends must NOT bleed into the next octet stream on a reused demux.
 * ev_octet_stream_end now resets every channel's reassembly progress. */
static uint32_t test_varint_encode(uint64_t v, uint8_t *o)
{
	uint32_t i = 0;
	do { uint8_t b = v & 0x7fu; v >>= 7; if (v) b |= 0x80u; o[i++] = b; } while (v);
	return i;
}
static void test_demux_truncated_stream_reset(void)
{
	printf("  test_demux_truncated_stream_reset...\n");
	mux_capture_t mc = {0};
	bvnr_demux_t *dm = bvnr_demux_create(mux_on_msg, &mc, 0);
	bvnr_data_t empty = {0};
	uint8_t frag[64];
	uint32_t off;

	/* Stream 1: a truncated message on channel 0 (declares 100 bytes, sends 10). */
	bvnr_demux_on_event(dm, ev_octet_stream_start, &empty);
	off  = test_varint_encode(0u,   frag);        /* channel 0 */
	off += test_varint_encode(100u, frag + off);  /* declared length 100 */
	memset(frag + off, 0xAB, 10u); off += 10u;    /* only 10 bytes of payload */
	bvnr_data_t d = { .type = token_is_octet_stream, .data = frag, .length = off };
	bvnr_demux_on_event(dm, ev_data, &d);
	bvnr_demux_on_event(dm, ev_octet_stream_end, &empty);   /* truncated → must reset */

	/* Stream 2: a fresh complete 3-byte message on the same channel 0. */
	bvnr_demux_on_event(dm, ev_octet_stream_start, &empty);
	off  = test_varint_encode(0u, frag);          /* channel 0 */
	off += test_varint_encode(3u, frag + off);    /* declared length 3 */
	frag[off++] = 'X'; frag[off++] = 'Y'; frag[off++] = 'Z';
	d.data = frag; d.length = off;
	bvnr_demux_on_event(dm, ev_data, &d);
	bvnr_demux_on_event(dm, ev_octet_stream_end, &empty);

	ASSERT_EQ_UINT(mc.n, 1, "only the fresh stream-2 message is delivered");
	if (mc.n >= 1) {
		ASSERT_EQ_UINT(mc.channels[0], 0, "fresh message channel");
		ASSERT_EQ_UINT(mc.lens[0], 3, "fresh message length (no bleed)");
		ASSERT_TRUE(strcmp(mc.payloads[0], "XYZ") == 0, "fresh message payload");
	}
	/* The truncated stream is reported -- silently dropping it made data loss
	 * indistinguishable from an empty stream -- but with its own code, and
	 * without aborting: the fresh stream-2 message above still arrived. */
	ASSERT_EQ_UINT(bvnr_demux_error(dm), error_octet_stream_truncated,
		       "truncated stream is reported, not silently dropped");
	bvnr_demux_destroy(dm);
}

/* An overlong channel varint — 10 bytes whose 10th byte carries a value bit
 * beyond bit 63 — does not fit a uint64 and must be rejected, not silently
 * truncated. The 10th byte (0x02) has its continuation bit clear, so a decoder
 * that did not guard the final byte would accept the run and wrap the channel
 * to 0; the guard makes it a demux desync instead. */
static void test_demux_overlong_varint_rejected(void)
{
	printf("  test_demux_overlong_varint_rejected...\n");
	mux_capture_t mc = {0};
	bvnr_demux_t *dm = bvnr_demux_create(mux_on_msg, &mc, 0);
	bvnr_data_t empty = {0};
	uint8_t frag[10];

	bvnr_demux_on_event(dm, ev_octet_stream_start, &empty);
	for (int i = 0; i < 9; i++) frag[i] = 0x80u;   /* continuation, value 0 */
	frag[9] = 0x02u;                                /* 10th byte: value bit > 63 */
	bvnr_data_t d = { .type = token_is_octet_stream, .data = frag, .length = 10u };
	bool fed = bvnr_demux_on_event(dm, ev_data, &d);

	ASSERT_TRUE(!fed, "overlong channel varint aborts the feed");
	ASSERT_EQ_UINT(bvnr_demux_error(dm), error_octet_stream_out_of_sync,
		"overlong varint is reported as a desync, not silently truncated");
	ASSERT_EQ_UINT(mc.n, 0, "no message delivered from an overlong varint");
	bvnr_demux_destroy(dm);
}

/* A message-length varint split across two octet chunks must be reassembled,
 * not rejected: the demux accumulates the partial varint per channel across
 * chunks. Hand-build channel 5 with length varint 0xC8 0x01 (= 200) where the
 * first varint byte rides chunk 1 and the second rides chunk 2 with the data. */
static void test_demux_split_length_varint(void)
{
	printf("  test_demux_split_length_varint...\n");
	mux_capture_t mc = {0};
	bvnr_demux_t *dm = bvnr_demux_create(mux_on_msg, &mc, 0);
	bvnr_data_t empty = {0};
	uint8_t frag[300];
	uint32_t off;

	bvnr_demux_on_event(dm, ev_octet_stream_start, &empty);

	/* Chunk 1: channel 5, then only the FIRST byte of the length varint. */
	off = test_varint_encode(5u, frag);       /* channel 5 */
	frag[off++] = 0xC8u;                       /* len varint byte 1 (continuation) */
	bvnr_data_t d1 = { .type = token_is_octet_stream, .data = frag, .length = off };
	bool fed1 = bvnr_demux_on_event(dm, ev_data, &d1);
	ASSERT_TRUE(fed1, "chunk with a partial length varint feeds cleanly");
	ASSERT_EQ_UINT(mc.n, 0, "no message yet (length varint incomplete)");

	/* Chunk 2: channel 5, the SECOND varint byte, then 200 data bytes. */
	uint8_t frag2[256];
	off = test_varint_encode(5u, frag2);      /* channel 5 again */
	frag2[off++] = 0x01u;                      /* len varint byte 2 -> len = 200 */
	for (int i = 0; i < 200; i++) frag2[off++] = (uint8_t)(i + 1);
	bvnr_data_t d2 = { .type = token_is_octet_stream, .data = frag2, .length = off };
	bool fed2 = bvnr_demux_on_event(dm, ev_data, &d2);
	bvnr_demux_on_event(dm, ev_octet_stream_end, &empty);

	ASSERT_TRUE(fed2, "chunk completing the split varint feeds cleanly");
	ASSERT_EQ_UINT(bvnr_demux_error(dm), error_none, "no desync from a split length varint");
	ASSERT_EQ_UINT(mc.n, 1, "the cross-chunk-varint message is delivered");
	if (mc.n == 1) {
		ASSERT_EQ_UINT(mc.channels[0], 5, "split-varint message channel");
		ASSERT_EQ_UINT(mc.lens[0], 200, "split-varint message length (= 200)");
	}
	bvnr_demux_destroy(dm);
}

/* Key scoping: a document may carry one multiplexed octet stream alongside an
 * ordinary binary octet payload. With bvnr_demux_set_key the demux ignores octet
 * streams under other keys, so the plain payload — whose raw bytes would
 * otherwise be misread as channel/length framing — does not perturb the demux. */
static void test_demux_key_scope(void)
{
	printf("  test_demux_key_scope...\n");
	uint8_t buf[512];

	/* Producer: a plain octet payload under ".bin" (bytes 0x00 0x00, which an
	 * unscoped demux would misread as an empty message on channel 0), then a
	 * real mux stream under ".mux". */
	bvnr_writer_t *w = bvnr_writer_create();
	bvnr_data_t hdr = {0};
	bvnr_data_t binkey = { .data = "bin", .length = 3 };
	bvnr_data_t empty  = {0};
	uint8_t rawbytes[2] = { 0x00, 0x00 };
	bvnr_data_t rawchunk = { .type = token_is_octet_stream, .data = rawbytes, .length = 2 };
	bool wok = bvnr_open_write_mem(w, buf, sizeof(buf), false, NULL)
		&& bvnr_write_event(w, ev_stream_start, &hdr)
		&& bvnr_write_event(w, ev_assignment_start, &binkey)
		&& bvnr_write_event(w, ev_octet_stream_start, &empty)
		&& bvnr_write_event(w, ev_data, &rawchunk)
		&& bvnr_write_event(w, ev_octet_stream_end, &empty)
		&& bvnr_mux_begin(w, "mux")
		&& bvnr_mux_send(w, 1, "hello", 5)
		&& bvnr_mux_end(w)
		&& bvnr_write_finish(w);
	ASSERT_TRUE(wok, "mixed plain+mux producer writes cleanly");
	uint64_t total = bvnr_writer_bytes_written(w);
	bvnr_writer_destroy(w);

	/* Consumer scoped to ".mux": the ".bin" stream is ignored. */
	mux_capture_t mc = {0};
	bvnr_demux_t *demux = bvnr_demux_create(mux_on_msg, &mc, 0);
	ASSERT_TRUE(bvnr_demux_set_key(demux, "mux"), "set_key succeeds");
	bvnr_read_flags_t fl = {
		.on_verified = bvnr_demux_on_event, .on_error = on_err, .userdata = demux,
	};
	bvnr_reader_t *r = bvnr_reader_create();
	bool rok = bvnr_open_read_mem(r, buf, total, NULL, 0, &fl) && bvnr_read(r);
	ASSERT_TRUE(rok, "scoped consumer parses the mixed document cleanly");
	ASSERT_EQ_UINT(bvnr_demux_error(demux), error_none, "no desync from the ignored plain stream");
	bvnr_reader_destroy(r);
	bvnr_demux_destroy(demux);

	ASSERT_EQ_UINT(mc.n, 1, "only the in-scope mux message is delivered");
	if (mc.n == 1) {
		ASSERT_EQ_UINT(mc.channels[0], 1, "scoped message channel");
		ASSERT_TRUE(strcmp(mc.payloads[0], "hello") == 0, "scoped message payload");
	}

	/* Control: an unscoped demux over the same bytes also sees the plain
	 * stream, proving the scoping above is what suppressed it. */
	mux_capture_t mc2 = {0};
	bvnr_demux_t *demux2 = bvnr_demux_create(mux_on_msg, &mc2, 0);
	bvnr_read_flags_t fl2 = {
		.on_verified = bvnr_demux_on_event, .on_error = on_err, .userdata = demux2,
	};
	bvnr_reader_t *r2 = bvnr_reader_create();
	bool rok2 = bvnr_open_read_mem(r2, buf, total, NULL, 0, &fl2) && bvnr_read(r2);
	ASSERT_TRUE(rok2, "unscoped consumer parses too");
	bvnr_reader_destroy(r2);
	bvnr_demux_destroy(demux2);
	ASSERT_EQ_UINT(mc2.n, 2, "unscoped demux also picks up the plain stream (control)");
}

/* Large messages that span many octet chunks, interleaved across channels,
 * must reassemble byte-exact. */
typedef struct {
	int      n;
	uint64_t channels[8];
	uint64_t lens[8];
	uint32_t checksums[8];   /* additive checksum to verify byte content */
} mux_big_capture_t;

static uint32_t add_checksum(const uint8_t *p, uint64_t len)
{
	uint32_t s = 0;
	for (uint64_t i = 0; i < len; i++) s += p[i];
	return s;
}

static bool mux_big_on_msg(void *ud, uint64_t channel, const uint8_t *data, uint64_t len)
{
	mux_big_capture_t *m = ud;
	if (m->n < 8) {
		m->channels[m->n]  = channel;
		m->lens[m->n]      = len;
		m->checksums[m->n] = add_checksum(data, len);
		m->n++;
	}
	return true;
}

static void test_octet_mux_large(void)
{
	printf("  test_octet_mux_large...\n");

	/* Two messages far larger than one 64 KiB chunk, on two channels. */
	const uint64_t big_a = 200000;   /* ~3 chunks */
	const uint64_t big_b = 150000;   /* ~3 chunks */
	uint8_t *a = malloc(big_a), *b = malloc(big_b);
	ASSERT_TRUE(a && b, "alloc big payloads");
	if (!a || !b) { free(a); free(b); return; }
	for (uint64_t i = 0; i < big_a; i++) a[i] = (uint8_t)(i * 7u + 1u);
	for (uint64_t i = 0; i < big_b; i++) b[i] = (uint8_t)(i * 13u + 5u);
	uint32_t csum_a = add_checksum(a, big_a);
	uint32_t csum_b = add_checksum(b, big_b);

	uint8_t *out = malloc(big_a + big_b + 4096);
	ASSERT_TRUE(out != NULL, "alloc output");
	if (!out) { free(a); free(b); return; }

	bvnr_writer_t *w = bvnr_writer_create();
	bvnr_data_t hdr = {0};
	bool wok = bvnr_open_write_mem(w, out, big_a + big_b + 4096, false, NULL)
		&& bvnr_write_event(w, ev_stream_start, &hdr)
		&& bvnr_mux_begin(w, "mux")
		&& bvnr_mux_send(w, 7, a, big_a)
		&& bvnr_mux_send(w, 9, b, big_b)
		&& bvnr_mux_end(w)
		&& bvnr_write_finish(w);
	ASSERT_TRUE(wok, "large mux producer writes cleanly");
	uint64_t total = bvnr_writer_bytes_written(w);
	bvnr_writer_destroy(w);

	mux_big_capture_t mc = {0};
	bvnr_demux_t *demux = bvnr_demux_create(mux_big_on_msg, &mc, 0);
	bvnr_read_flags_t fl = {
		.on_verified = bvnr_demux_on_event, .on_error = on_err,
		.userdata = demux,
		.max_file_size = BVNR_FILESIZE_UNLIMITED,
	};
	bvnr_reader_t *r = bvnr_reader_create();
	bool rok = bvnr_open_read_mem(r, out, total, NULL, 0, &fl) && bvnr_read(r);
	ASSERT_TRUE(rok, "large mux consumer parses cleanly");
	ASSERT_EQ_UINT(bvnr_demux_error(demux), error_none, "no demux desync (large)");
	bvnr_reader_destroy(r);
	bvnr_demux_destroy(demux);

	ASSERT_EQ_UINT(mc.n, 2, "two large messages reassembled");
	if (mc.n == 2) {
		ASSERT_EQ_UINT(mc.channels[0], 7, "big msg0 channel");
		ASSERT_EQ_UINT(mc.lens[0], big_a, "big msg0 length");
		ASSERT_EQ_UINT(mc.checksums[0], csum_a, "big msg0 content checksum");
		ASSERT_EQ_UINT(mc.channels[1], 9, "big msg1 channel");
		ASSERT_EQ_UINT(mc.lens[1], big_b, "big msg1 length");
		ASSERT_EQ_UINT(mc.checksums[1], csum_b, "big msg1 content checksum");
	}

	free(a); free(b); free(out);
}

/* Message sizes straddling the 65536-byte octet-chunk limit and the varint
 * widths are the off-by-one-prone region of the split/reassembly code. Verify a
 * spread of boundary sizes, on interleaved channels, round-trips byte-exact. */
typedef struct {
	int      n;
	uint64_t lens[16];
	uint32_t sums[16];
} mux_bound_capture_t;

static bool mux_bound_on_msg(void *ud, uint64_t channel, const uint8_t *data, uint64_t len)
{
	(void)channel;
	mux_bound_capture_t *m = ud;
	if (m->n < 16) {
		m->lens[m->n] = len;
		m->sums[m->n] = add_checksum(data, len);
		m->n++;
	}
	return true;
}

static void test_octet_mux_boundaries(void)
{
	printf("  test_octet_mux_boundaries...\n");
	const uint64_t sizes[] = {
		0, 1, 9, 10, 11, 65525, 65526, 65527, 65535, 65536, 65537, 131072
	};
	const int N = (int)(sizeof sizes / sizeof sizes[0]);

	uint8_t *bufs[12] = {0};
	uint32_t sums[12] = {0};
	uint64_t total = 0;
	bool alloc_ok = true;
	for (int i = 0; i < N; i++) {
		bufs[i] = malloc(sizes[i] ? sizes[i] : 1);
		if (!bufs[i]) { alloc_ok = false; break; }
		for (uint64_t k = 0; k < sizes[i]; k++)
			bufs[i][k] = (uint8_t)((k * 31u + (uint64_t)i * 7u) & 0xffu);
		sums[i] = add_checksum(bufs[i], sizes[i]);
		total += sizes[i];
	}
	ASSERT_TRUE(alloc_ok, "alloc boundary payloads");
	if (!alloc_ok) { for (int i = 0; i < N; i++) free(bufs[i]); return; }

	size_t cap = (size_t)total + (size_t)N * 64u + 4096u;
	uint8_t *out = malloc(cap);
	ASSERT_TRUE(out != NULL, "alloc boundary output");
	if (!out) { for (int i = 0; i < N; i++) free(bufs[i]); return; }

	bvnr_writer_t *w = bvnr_writer_create();
	bvnr_data_t hdr = {0};
	bool wok = out
		&& bvnr_open_write_mem(w, out, cap, false, NULL)
		&& bvnr_write_event(w, ev_stream_start, &hdr)
		&& bvnr_mux_begin(w, "mux");
	for (int i = 0; i < N && wok; i++)
		wok = bvnr_mux_send(w, (uint64_t)(i % 3), bufs[i], sizes[i]);
	wok = wok && bvnr_mux_end(w) && bvnr_write_finish(w);
	ASSERT_TRUE(wok, "boundary producer writes cleanly");
	uint64_t n = bvnr_writer_bytes_written(w);
	bvnr_writer_destroy(w);

	mux_bound_capture_t mc = {0};
	bvnr_demux_t *dm = bvnr_demux_create(mux_bound_on_msg, &mc, 0);
	bvnr_read_flags_t fl = {
		.on_verified = bvnr_demux_on_event, .on_error = on_err,
		.userdata = dm, .max_file_size = BVNR_FILESIZE_UNLIMITED,
	};
	bvnr_reader_t *r = bvnr_reader_create();
	bool rok = bvnr_open_read_mem(r, out, n, NULL, 0, &fl) && bvnr_read(r);
	ASSERT_TRUE(rok, "boundary consumer parses cleanly");
	ASSERT_EQ_UINT(bvnr_demux_error(dm), error_none, "no boundary desync");
	bvnr_reader_destroy(r);
	bvnr_demux_destroy(dm);

	ASSERT_EQ_UINT(mc.n, N, "all boundary messages reassembled");
	if (mc.n == N) {
		for (int i = 0; i < N; i++) {
			char msg[64];
			snprintf(msg, sizeof msg, "boundary size[%d]=%llu len", i,
				 (unsigned long long)sizes[i]);
			ASSERT_EQ_UINT(mc.lens[i], sizes[i], msg);
			snprintf(msg, sizeof msg, "boundary size[%d] content", i);
			ASSERT_EQ_UINT(mc.sums[i], sums[i], msg);
		}
	}
	for (int i = 0; i < N; i++) free(bufs[i]);
	free(out);
}

/* Empty message (len 0): must reassemble with len 0 and a non-NULL pointer
 * (matching the library's "callbacks always get non-NULL data" convention). */
typedef struct { int n; uint64_t lens[4]; bool null_ptr; } mux_empty_capture_t;

static bool mux_empty_on_msg(void *ud, uint64_t channel, const uint8_t *data, uint64_t len)
{
	(void)channel;
	mux_empty_capture_t *m = ud;
	if (m->n < 4) { m->lens[m->n] = len; if (!data) m->null_ptr = true; m->n++; }
	return true;
}

static void test_octet_mux_empty(void)
{
	printf("  test_octet_mux_empty...\n");
	uint8_t buf[256];
	bvnr_writer_t *w = bvnr_writer_create();
	bvnr_data_t hdr = {0};
	bool wok = bvnr_open_write_mem(w, buf, sizeof(buf), false, NULL)
		&& bvnr_write_event(w, ev_stream_start, &hdr)
		&& bvnr_mux_begin(w, "mux")
		&& bvnr_mux_send(w, 5, NULL, 0)      /* empty message */
		&& bvnr_mux_send(w, 5, "x", 1)
		&& bvnr_mux_end(w)
		&& bvnr_write_finish(w);
	ASSERT_TRUE(wok, "empty-message producer writes cleanly");
	uint64_t total = bvnr_writer_bytes_written(w);
	bvnr_writer_destroy(w);

	mux_empty_capture_t mc = {0};
	bvnr_demux_t *demux = bvnr_demux_create(mux_empty_on_msg, &mc, 0);
	bvnr_read_flags_t fl = {
		.on_verified = bvnr_demux_on_event, .on_error = on_err, .userdata = demux,
	};
	bvnr_reader_t *r = bvnr_reader_create();
	bool rok = bvnr_open_read_mem(r, buf, total, NULL, 0, &fl) && bvnr_read(r);
	ASSERT_TRUE(rok, "empty-message consumer parses cleanly");
	bvnr_reader_destroy(r);
	bvnr_demux_destroy(demux);

	ASSERT_EQ_UINT(mc.n, 2, "two messages (empty + 1-byte)");
	ASSERT_EQ_UINT(mc.lens[0], 0, "first message is empty");
	ASSERT_EQ_UINT(mc.lens[1], 1, "second message is one byte");
	ASSERT_TRUE(!mc.null_ptr, "callback never receives a NULL data pointer");
}

static bool count_on_msg(void *ud, uint64_t channel, const uint8_t *data, uint64_t len)
{
	(void)channel; (void)data; (void)len;
	(*(int *)ud)++;
	return true;
}

/* A chunk may declare a large message length but carry far less data. The demux
 * must not pre-allocate the declared size (that would be a memory-amplification
 * vector) and must simply not deliver the still-incomplete message. The octet
 * framing itself is valid, so the parse succeeds and it is not a desync. */
static void test_octet_mux_amplification_guard(void)
{
	printf("  test_octet_mux_amplification_guard...\n");
	/* .m = <octet-stream> ;  one chunk whose 7-byte payload is:
	 *   0x00              channel varint = 0
	 *   0xC0 0x84 0x3D    length varint  = 1000000
	 *   0x41 0x42 0x43    only 3 of the declared 1000000 data bytes      */
	static const uint8_t doc[] = {
		'.', 'm', ' ', '=', ' ',
		0x00,                         /* octet-stream start            */
		0x01, 0x07, 0x00,             /* chunk header: 7 payload bytes  */
		0x00,                         /* channel = 0                    */
		0xC0, 0x84, 0x3D,             /* declared length = 1000000      */
		0x41, 0x42, 0x43,            /* 3 data bytes (stays incomplete)*/
		0x00,                         /* octet-stream end               */
		';'
	};
	int n = 0;
	bvnr_demux_t *dm = bvnr_demux_create(count_on_msg, &n, 0);
	bvnr_read_flags_t fl = {
		.on_verified = bvnr_demux_on_event, .on_error = on_err,
		.userdata = dm, .max_file_size = BVNR_FILESIZE_UNLIMITED,
	};
	bvnr_reader_t *r = bvnr_reader_create();
	bool rok = bvnr_open_read_mem(r, doc, sizeof doc, NULL, 0, &fl) && bvnr_read(r);
	ASSERT_TRUE(rok, "valid framing with oversized-declared message parses");
	/* Not a FRAMING desync -- the octet framing is valid and the read succeeds --
	 * but the message layer is short, and that must be reportable rather than
	 * silent. Hence its own code, latched without aborting the read. */
	ASSERT_EQ_UINT(bvnr_demux_error(dm), error_octet_stream_truncated,
		       "over-declared message is reported as truncated, not as a desync");
	ASSERT_EQ_UINT(n, 0, "incomplete message is not delivered");
	bvnr_reader_destroy(r);
	bvnr_demux_destroy(dm);
}

/* Randomized differential round-trip (deterministic seed for reproducibility):
 * many runs of random channels/sizes/content, each verified to come back in
 * order, byte-exact. Catches reassembly/splitting logic regressions that the
 * fixed-vector tests would miss. */
typedef struct {
	int      n, cap, fail;
	uint64_t *ch;
	uint64_t *len;
	uint32_t *sum;
} rt_ctx_t;

static bool rt_on_msg(void *ud, uint64_t channel, const uint8_t *data, uint64_t len)
{
	rt_ctx_t *c = ud;
	if (c->n >= c->cap) { c->fail = 1; return true; }
	if (channel != c->ch[c->n] || len != c->len[c->n] ||
	    add_checksum(data, len) != c->sum[c->n])
		c->fail = 1;
	c->n++;
	return true;
}

static void test_octet_mux_random_roundtrip(void)
{
	printf("  test_octet_mux_random_roundtrip...\n");
	uint32_t st = 2654435761u;           /* fixed seed */
	#define RT_NEXT() (st ^= st << 13, st ^= st >> 17, st ^= st << 5, st)
	const int iters = 300;
	int total_msgs = 0, any_fail = 0;

	for (int it = 0; it < iters && !any_fail; it++) {
		int K = (int)(RT_NEXT() % 8u);
		uint64_t chs[8], lens[8]; uint32_t sums[8];
		uint8_t *data[8] = {0};
		uint64_t totbytes = 0;
		bool aok = true;
		for (int i = 0; i < K; i++) {
			uint64_t L;
			switch (RT_NEXT() % 5u) {
			case 0:  L = 0; break;                       /* empty */
			case 1:  L = RT_NEXT() % 16u; break;         /* tiny */
			case 2:  L = 65500u + RT_NEXT() % 80u; break;/* around 64 KiB chunk edge */
			default: L = RT_NEXT() % 4000u; break;       /* small/medium */
			}
			data[i] = malloc(L ? (size_t)L : 1u);
			if (!data[i]) { aok = false; break; }
			for (uint64_t k = 0; k < L; k++) data[i][k] = (uint8_t)RT_NEXT();
			chs[i] = RT_NEXT() % 20u; lens[i] = L; sums[i] = add_checksum(data[i], L);
			totbytes += L;
		}
		/* Allocation failure is an environment issue, not a product failure:
		 * clean up and stop without asserting (keeps the assertion count
		 * meaningful while still satisfying the analyzer's null checks). */
		if (!aok) { for (int i = 0; i < K; i++) free(data[i]); break; }

		size_t cap = (size_t)totbytes + (size_t)K * 32u + 4096u;
		uint8_t *out = malloc(cap);
		if (!out) { for (int i = 0; i < K; i++) free(data[i]); break; }

		bvnr_writer_t *w = bvnr_writer_create();
		bvnr_data_t hdr = {0};
		bool wok = bvnr_open_write_mem(w, out, cap, false, NULL)
			&& bvnr_write_event(w, ev_stream_start, &hdr)
			&& bvnr_mux_begin(w, "m");
		for (int i = 0; i < K && wok; i++) wok = bvnr_mux_send(w, chs[i], data[i], lens[i]);
		wok = wok && bvnr_mux_end(w) && bvnr_write_finish(w);
		uint64_t n = bvnr_writer_bytes_written(w);
		bvnr_writer_destroy(w);
		if (!wok) { any_fail = 1; }

		rt_ctx_t c = { 0, K, 0, chs, lens, sums };
		bvnr_demux_t *dm = bvnr_demux_create(rt_on_msg, &c, 0);
		bvnr_read_flags_t fl = {
			.on_verified = bvnr_demux_on_event, .userdata = dm,
			.max_file_size = BVNR_FILESIZE_UNLIMITED,
		};
		bvnr_reader_t *r = bvnr_reader_create();
		bool rok = wok && bvnr_open_read_mem(r, out, n, NULL, 0, &fl) && bvnr_read(r);
		if (!rok || bvnr_demux_error(dm) != error_none || c.n != K || c.fail)
			any_fail = 1;
		total_msgs += c.n;
		bvnr_reader_destroy(r);
		bvnr_demux_destroy(dm);
		for (int i = 0; i < K; i++) free(data[i]);
		free(out);
	}
	#undef RT_NEXT
	ASSERT_TRUE(!any_fail, "randomized mux round-trip: all runs recover byte-exact");
	ASSERT_TRUE(total_msgs > 0, "randomized round-trip exercised messages");
}

/* ===========================================================================
 * Multi-document: truncation + continue-on-error
 * =========================================================================== */
static void test_frames_truncated(void)
{
	printf("  test_frames_truncated...\n");
	uint8_t big[2048];
	bvnr_sink_t sink;
	bvnr_sink_to_mem(&sink, big, sizeof(big));
	uint8_t doc[256];
	uint64_t n = build_doc(doc, sizeof(doc), 1);
	ASSERT_TRUE(bvnr_frame_write(&sink, doc, n), "frame written");
	uint64_t total = bvnr_sink_bytes_written(&sink);

	/* Lop off the last few payload bytes: a mid-record EOF must be an error. */
	bvnr_source_t src;
	bvnr_source_from_mem(&src, big, total - 4);
	uint64_t count = 0;
	bool ok = bvnr_doc_stream_read(&src, NULL, &count);
	ASSERT_TRUE(!ok, "truncated frame stream is rejected");
}

/* A frame header may declare a length far larger than the bytes that follow.
 * The reader must reject it as truncation without pre-allocating the declared
 * size (a memory-amplification vector — the payload buffer grows with the data
 * that actually arrives). */
static void test_frames_oversized_declared(void)
{
	printf("  test_frames_oversized_declared...\n");
	static const uint8_t stream[] = {
		'B', 'V', 'F', '1',
		0x00, 0xE1, 0xF5, 0x05, 0x00, 0x00, 0x00, 0x00, /* len = 100000000 LE */
		'.', 'x', ' ', '=', ' '                          /* only 5 of them present */
	};
	bvnr_read_flags_t fl = {0};
	fl.max_file_size = BVNR_FILESIZE_UNLIMITED;
	bvnr_doc_stream_opts_t opts = { .flags = &fl };
	bvnr_source_t src;
	bvnr_source_from_mem(&src, stream, sizeof stream);
	uint64_t count = 0;
	bool ok = bvnr_doc_stream_read(&src, &opts, &count);
	ASSERT_TRUE(!ok, "frame declaring more bytes than present is rejected");
	ASSERT_EQ_UINT(count, 0, "no document completed");
}

/* The per-frame guard `max_document_size` is independent of max_file_size:
 * unlike max_file_size (0 == unlimited), 0 here selects the finite
 * BVNR_DOC_DEFAULT_MAX, and the guard is lifted with an explicit huge cap
 * (NOT BVNR_FILESIZE_UNLIMITED, which is now 0 and merely selects the default).
 * Locks the framing-guard decision behind the 0-means-unlimited file-size flip. */
static void test_frames_max_document_size(void)
{
	printf("  test_frames_max_document_size...\n");
	uint8_t big[2048];
	bvnr_sink_t sink;
	bvnr_sink_to_mem(&sink, big, sizeof(big));
	uint8_t doc[256];
	uint64_t n = build_doc(doc, sizeof(doc), 1);
	ASSERT_TRUE(bvnr_frame_write(&sink, doc, n), "frame written");
	uint64_t total = bvnr_sink_bytes_written(&sink);

	/* An explicit cap below the payload length rejects the frame as too large. */
	{
		bvnr_doc_stream_opts_t opts = { .max_document_size = n - 1u };
		bvnr_source_t src;
		bvnr_source_from_mem(&src, big, total);
		uint64_t count = 0;
		bool ok = bvnr_doc_stream_read(&src, &opts, &count);
		ASSERT_TRUE(!ok, "frame exceeding max_document_size is rejected");
		ASSERT_EQ_UINT(count, 0, "no document completed under tiny cap");
	}

	/* An explicit huge cap lifts the guard (the path that replaced the old
	 * BVNR_FILESIZE_UNLIMITED==UINT64_MAX sentinel); the same frame now parses. */
	{
		bvnr_doc_stream_opts_t opts = { .max_document_size = UINT64_MAX };
		bvnr_source_t src;
		bvnr_source_from_mem(&src, big, total);
		uint64_t count = 0;
		bool ok = bvnr_doc_stream_read(&src, &opts, &count);
		ASSERT_TRUE(ok, "explicit huge max_document_size lifts the guard");
		ASSERT_EQ_UINT(count, 1, "one document parsed when guard lifted");
	}
}

typedef struct { int n; bool ok_flags[4]; } md_err_ctx_t;
static md_err_ctx_t g_mderr;
static bool md_err_on_document(void *ud, uint64_t index, bool ok, error_code_t err)
{
	(void)ud; (void)err;
	if (index < 4) g_mderr.ok_flags[index] = ok;
	g_mderr.n++;
	return true;
}

/* Build a 3-frame stream [good, garbage, good] into `big`; returns total len. */
static uint64_t build_mixed_frames(uint8_t *big, size_t cap, const uint8_t *good, uint64_t gn)
{
	bvnr_sink_t sink;
	bvnr_sink_to_mem(&sink, big, cap);
	const char *garbage = ".broken = @@@;";   /* lexically invalid document */
	(void)(bvnr_frame_write(&sink, good, gn)
		&& bvnr_frame_write(&sink, garbage, strlen(garbage))
		&& bvnr_frame_write(&sink, good, gn));
	return bvnr_sink_bytes_written(&sink);
}

static void test_frames_abort_on_error(void)
{
	printf("  test_frames_abort_on_error...\n");
	uint8_t big[2048], good[256];
	uint64_t gn = build_doc(good, sizeof(good), 1);
	uint64_t total = build_mixed_frames(big, sizeof(big), good, gn);

	/* Strict reader (no resync): the bad frame aborts the sequence. */
	memset(&g_mderr, 0, sizeof(g_mderr));
	bvnr_doc_stream_opts_t opts = { .on_document = md_err_on_document };
	bvnr_source_t src;
	bvnr_source_from_mem(&src, big, total);
	uint64_t count = 0;
	bool ok = bvnr_doc_stream_read(&src, &opts, &count);

	ASSERT_TRUE(!ok, "strict mode: stream aborts on the bad document");
	ASSERT_EQ_UINT(count, 2, "stops after visiting the bad document (index 1)");
	ASSERT_TRUE(g_mderr.ok_flags[0], "frame 0 parsed OK");
	ASSERT_TRUE(!g_mderr.ok_flags[1], "frame 1 reported as failed");
}

static void test_frames_continue_on_error(void)
{
	printf("  test_frames_continue_on_error...\n");
	uint8_t big[2048], good[256];
	uint64_t gn = build_doc(good, sizeof(good), 1);
	uint64_t total = build_mixed_frames(big, sizeof(big), good, gn);

	/* Resync reader: the bad frame is recovered and the sequence completes. */
	memset(&g_mderr, 0, sizeof(g_mderr));
	bvnr_read_flags_t fl = {0};
	fl.continue_on_error = true;
	bvnr_doc_stream_opts_t opts = { .flags = &fl, .on_document = md_err_on_document };
	bvnr_source_t src;
	bvnr_source_from_mem(&src, big, total);
	uint64_t count = 0;
	bool ok = bvnr_doc_stream_read(&src, &opts, &count);

	ASSERT_TRUE(ok, "continue_on_error: stream read completes");
	ASSERT_EQ_UINT(count, 3, "all three frames visited (bad one recovered)");
	ASSERT_TRUE(g_mderr.ok_flags[0] && g_mderr.ok_flags[2], "good frames parsed OK");
}

/* Inter-document resilience decoupled from intra-document resync: parse each
 * document strictly (no resync) yet still process every frame, with the bad one
 * reported as failed. This is the case the old single-flag design could not
 * express (strict mode aborted the whole stream on the first bad document). */
static void test_frames_continue_past_failed(void)
{
	printf("  test_frames_continue_past_failed...\n");
	uint8_t big[2048], good[256];
	uint64_t gn = build_doc(good, sizeof(good), 1);
	uint64_t total = build_mixed_frames(big, sizeof(big), good, gn);

	memset(&g_mderr, 0, sizeof(g_mderr));
	bvnr_read_flags_t fl = {0};
	fl.continue_on_error = false;   /* strict inner parsing: the bad doc truly fails */
	bvnr_doc_stream_opts_t opts = {
		.flags = &fl,
		.on_document = md_err_on_document,
		.continue_past_failed = true,   /* but keep processing frames */
	};
	bvnr_source_t src;
	bvnr_source_from_mem(&src, big, total);
	uint64_t count = 0;
	bool ok = bvnr_doc_stream_read(&src, &opts, &count);

	ASSERT_TRUE(ok, "continue_past_failed: framing fully processed");
	ASSERT_EQ_UINT(count, 3, "all three frames visited despite a hard doc failure");
	ASSERT_TRUE(g_mderr.ok_flags[0], "frame 0 parsed OK");
	ASSERT_TRUE(!g_mderr.ok_flags[1], "frame 1 reported as failed (no resync)");
	ASSERT_TRUE(g_mderr.ok_flags[2], "frame 2 parsed OK after the failure");
}

/* ===========================================================================
 * 4. Document-in-document
 * =========================================================================== */
typedef struct {
	char     pending_key[64];
	bool     in_octet;
	uint8_t  inner[512];
	uint32_t inner_len;
	bool     got_inner;
	doc_capture_t inner_doc;   /* result of recursively parsing the payload */
} embed_ctx_t;

static bool embed_outer_cb(void *ud, bvnr_event_t ev, bvnr_data_t *d)
{
	embed_ctx_t *e = ud;
	switch (ev) {
	case ev_assignment_start:
		memset(e->pending_key, 0, sizeof(e->pending_key));
		if (d->length && d->length < sizeof(e->pending_key)) {
			memcpy(e->pending_key, d->data, d->length);
			e->pending_key[d->length] = '\0';
		}
		break;
	case ev_octet_stream_start:
		if (strcmp(e->pending_key, "payload") == 0) {
			e->in_octet = true;
			e->inner_len = 0;
		}
		break;
	case ev_data:
		if (e->in_octet && d->type == token_is_octet_stream && d->data) {
			uint32_t room = (uint32_t)sizeof(e->inner) - e->inner_len;
			uint32_t k = d->length < room ? d->length : room;
			memcpy(e->inner + e->inner_len, d->data, k);
			e->inner_len += k;
		}
		break;
	case ev_octet_stream_end:
		if (e->in_octet) {
			e->in_octet = false;
			/* Recurse: parse the embedded document. */
			bvnr_read_flags_t inner_fl = {
				.on_verified = doc_capture_cb, .on_error = on_err,
				.userdata = &e->inner_doc,
			};
			e->got_inner = bvnr_parse_embedded(
				e->inner, e->inner_len, &inner_fl);
		}
		break;
	default:
		break;
	}
	return true;
}

static void test_doc_in_doc(void)
{
	printf("  test_doc_in_doc...\n");

	/* Inner document. */
	uint8_t inner[256];
	uint64_t inner_n = build_doc(inner, sizeof(inner), 999);
	ASSERT_TRUE(inner_n > 0, "inner doc built");

	/* Outer document embeds the inner one as an octet payload. */
	uint8_t outer[1024];
	bvnr_writer_t *w = bvnr_writer_create();
	bvnr_data_t hdr = {0};
	bool wok = bvnr_open_write_mem(w, outer, sizeof(outer), false, NULL)
		&& bvnr_write_event(w, ev_stream_start, &hdr)
		&& bvnr_write_uint(w, "outer", 32, 1u)
		&& bvnr_embed_document(w, "payload", inner, inner_n)
		&& bvnr_write_finish(w);
	ASSERT_TRUE(wok, "outer doc with embedded payload writes cleanly");
	uint64_t outer_n = bvnr_writer_bytes_written(w);
	bvnr_writer_destroy(w);

	/* Parse the outer doc; the callback recursively parses the payload. */
	embed_ctx_t e = {0};
	bvnr_read_flags_t fl = {
		.on_verified = embed_outer_cb, .on_error = on_err, .userdata = &e,
	};
	bvnr_reader_t *r = bvnr_reader_create();
	bool rok = bvnr_open_read_mem(r, outer, outer_n, NULL, 0, &fl) && bvnr_read(r);
	ASSERT_TRUE(rok, "outer doc parses cleanly");
	bvnr_reader_destroy(r);

	ASSERT_TRUE(e.got_inner, "embedded document re-parsed");
	ASSERT_EQ_UINT(e.inner_len, inner_n, "embedded payload length preserved");
	ASSERT_TRUE(e.inner_doc.have_id && e.inner_doc.id == 999u,
				"inner document id recovered");
	ASSERT_TRUE(strcmp(e.inner_doc.name, "doc999") == 0,
				"inner document name recovered");
}

/* A single octet chunk may carry more than one complete message (a future
 * "packed" producer); bvnr_mux_send never packs, so this path is otherwise
 * unexercised. demux_feed must loop within the fragment, delivering each
 * message in order. Hand-build one fragment holding two messages on one channel. */
static void test_demux_packed_messages(void)
{
	printf("  test_demux_packed_messages...\n");
	mux_capture_t mc = {0};
	bvnr_demux_t *dm = bvnr_demux_create(mux_on_msg, &mc, 0);
	bvnr_data_t empty = {0};
	uint8_t frag[64];
	uint32_t off = 0;

	/* channel 3, then msg "AB" (len 2), then msg "CDE" (len 3) — all packed. */
	off  = test_varint_encode(3u, frag);
	off += test_varint_encode(2u, frag + off); frag[off++] = 'A'; frag[off++] = 'B';
	off += test_varint_encode(3u, frag + off);
	frag[off++] = 'C'; frag[off++] = 'D'; frag[off++] = 'E';

	bvnr_demux_on_event(dm, ev_octet_stream_start, &empty);
	bvnr_data_t d = { .type = token_is_octet_stream, .data = frag, .length = off };
	bool fed = bvnr_demux_on_event(dm, ev_data, &d);
	bvnr_demux_on_event(dm, ev_octet_stream_end, &empty);

	ASSERT_TRUE(fed, "packed fragment feeds cleanly");
	ASSERT_EQ_UINT(bvnr_demux_error(dm), error_none, "no desync from packed messages");
	ASSERT_EQ_UINT(mc.n, 2, "both packed messages delivered");
	if (mc.n == 2) {
		ASSERT_EQ_UINT(mc.channels[0], 3, "packed msg0 channel");
		ASSERT_TRUE(strcmp(mc.payloads[0], "AB") == 0, "packed msg0 payload");
		ASSERT_EQ_UINT(mc.channels[1], 3, "packed msg1 channel");
		ASSERT_TRUE(strcmp(mc.payloads[1], "CDE") == 0, "packed msg1 payload");
	}
	bvnr_demux_destroy(dm);
}

/* Regression: a document the on_document callback vetoes (returns false) must
 * still be counted in *out_count — the same as a parse-failure abort, which
 * counts the document it stops on. Pins the counting consistency between the two
 * abort paths. */
static bool veto_first_on_document(void *ud, uint64_t index, bool ok, error_code_t err)
{
	(void)ud; (void)ok; (void)err;
	return index != 0;   /* veto the very first document */
}
static void test_frames_on_document_veto_counts(void)
{
	printf("  test_frames_on_document_veto_counts...\n");
	uint8_t big[2048], doc[256];
	uint64_t n = build_doc(doc, sizeof(doc), 1);
	bvnr_sink_t sink;
	bvnr_sink_to_mem(&sink, big, sizeof(big));
	(void)(bvnr_frame_write(&sink, doc, n) && bvnr_frame_write(&sink, doc, n));
	uint64_t total = bvnr_sink_bytes_written(&sink);

	bvnr_doc_stream_opts_t opts = { .on_document = veto_first_on_document };
	bvnr_source_t src;
	bvnr_source_from_mem(&src, big, total);
	uint64_t count = 0;
	bool ok = bvnr_doc_stream_read(&src, &opts, &count);

	ASSERT_TRUE(!ok, "on_document veto aborts the sequence");
	ASSERT_EQ_UINT(count, 1, "the vetoed document is counted (parity with parse-fail abort)");
}

#if !defined(_WIN32)
/* Drive the multi-document framing over a real pipe fd with forced short reads:
 * a child dribbles the framed stream 3 bytes at a time, so bvn_src_read_exact
 * and the geometric grow-buffer must loop over partial pulls across both the
 * frame header and the payload. The in-memory tests cannot exercise this because
 * the memory source returns every requested byte in one pull. */
typedef struct { int n; uint32_t ids[8]; char pend[32]; } fd_ctx_t;
static bool fd_outer_cb(void *ud, bvnr_event_t ev, bvnr_data_t *d)
{
	fd_ctx_t *c = ud;
	if (ev == ev_assignment_start) {
		memset(c->pend, 0, sizeof(c->pend));
		if (d->length && d->length < sizeof(c->pend)) {
			memcpy(c->pend, d->data, d->length);
			c->pend[d->length] = '\0';
		}
	} else if (ev == ev_data && strcmp(c->pend, "id") == 0 && d->data && d->length) {
		char tmp[16];
		uint32_t k = d->length < 15u ? d->length : 15u;
		memcpy(tmp, d->data, k); tmp[k] = '\0';
		if (c->n < 8) c->ids[c->n] = (uint32_t)strtoul(tmp, NULL, 10);
	}
	return true;
}
static bool fd_on_document(void *ud, uint64_t index, bool ok, error_code_t err)
{
	(void)index; (void)err;
	fd_ctx_t *c = ud;
	if (ok && c->n < 8) c->n++;
	return true;
}
static void test_frames_over_fd_short_reads(void)
{
	printf("  test_frames_over_fd_short_reads...\n");
	const uint32_t ids[4] = { 11, 22, 33, 44 };

	uint8_t stream[4096];
	bvnr_sink_t sink;
	bvnr_sink_to_mem(&sink, stream, sizeof(stream));
	bool built = true;
	for (int i = 0; i < 4; i++) {
		uint8_t doc[256];
		uint64_t dn = build_doc(doc, sizeof(doc), ids[i]);
		built = built && dn > 0 && bvnr_frame_write(&sink, doc, dn);
	}
	ASSERT_TRUE(built, "four documents framed for the fd test");
	uint64_t total = bvnr_sink_bytes_written(&sink);

	/* ASSERT_TRUE records a failure and CONTINUES, so these two need an early
	 * return of their own: after a failed pipe() the descriptors are
	 * uninitialised and close(p[0]) would close whatever integer happened to be
	 * on the stack -- possibly a live fd of this process -- and after a failed
	 * fork() the parent would wait on a pipe nobody writes to and hang until the
	 * CTest timeout. Both are only reachable under fd/process exhaustion, but a
	 * test that hangs or closes a random fd there reports the wrong thing. */
	int p[2];
	if (pipe(p) != 0) {
		ASSERT_TRUE(false, "pipe created");
		return;
	}
	pid_t pid = fork();
	if (pid < 0) {
		ASSERT_TRUE(false, "fork succeeds");
		close(p[0]);
		close(p[1]);
		return;
	}
	if (pid == 0) {                       /* child: dribble 3 bytes at a time */
		close(p[0]);
		for (uint64_t off = 0; off < total; ) {
			uint64_t step = total - off < 3u ? total - off : 3u;
			ssize_t wn = write(p[1], stream + off, (size_t)step);
			if (wn <= 0) _exit(1);
			off += (uint64_t)wn;
			/* Widen the short-read window so the parent's read() loop sees
			 * partial frames. nanosleep (not usleep, which POSIX 2008 dropped). */
			nanosleep(&(struct timespec){ .tv_nsec = 200000 }, NULL);
		}
		close(p[1]);
		_exit(0);
	}
	close(p[1]);

	fd_ctx_t c = {0};
	bvnr_read_flags_t fl = { .on_verified = fd_outer_cb, .on_error = on_err, .userdata = &c };
	bvnr_doc_stream_opts_t opts = { .flags = &fl, .on_document = fd_on_document, .userdata = &c };
	bvnr_source_t src;
	bvnr_source_from_fd(&src, p[0]);
	uint64_t count = 0;
	bool ok = bvnr_doc_stream_read(&src, &opts, &count);
	close(p[0]);
	int st = 0;
	waitpid(pid, &st, 0);

	ASSERT_TRUE(ok, "framing reassembles across forced short reads");
	ASSERT_EQ_UINT(count, 4, "all four frames read over the dribbled pipe");
	ASSERT_EQ_UINT(c.n, 4, "all four documents parsed over the pipe");
	for (int i = 0; i < 4 && i < c.n; i++) {
		char msg[40];
		snprintf(msg, sizeof(msg), "fd doc %d id == %u", i, ids[i]);
		ASSERT_EQ_UINT(c.ids[i], ids[i], msg);
	}
}
#endif /* !_WIN32 */

int main(void)
{
	printf("Running bovnar_stream_test suite...\n");
	test_endless_knob();
	test_multidoc();
	test_octet_mux();
	test_demux_truncated_stream_reset();
	test_demux_overlong_varint_rejected();
	test_demux_split_length_varint();
	test_demux_key_scope();
	test_demux_packed_messages();
	test_octet_mux_large();
	test_octet_mux_boundaries();
	test_octet_mux_empty();
	test_octet_mux_amplification_guard();
	test_octet_mux_random_roundtrip();
	test_frames_truncated();
	test_frames_oversized_declared();
	test_frames_max_document_size();
	test_frames_abort_on_error();
	test_frames_continue_on_error();
	test_frames_continue_past_failed();
	test_frames_on_document_veto_counts();
#if !defined(_WIN32)
	test_frames_over_fd_short_reads();
#endif
	test_doc_in_doc();

	if (g_failures == 0) {
		printf("PASSED %d tests\n", g_tests);
		return 0;
	}
	fprintf(stderr, "FAILED %d of %d tests\n", g_failures, g_tests);
	return 1;
}
