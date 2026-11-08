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
 * Per-message cost of the streaming layer's hot paths, on one core:
 *
 *   mux_send      bvnr_mux_send into a memory sink;
 *   demux read    bvnr_read with bvnr_demux_on_event (the batch path);
 *   demux feed    the push-fed demux, callback mode (on_message) and pull mode
 *                 (bvnr_demux_next), fed the whole document at once and in
 *                 1460-byte and 64-byte pieces;
 *   frames        per framed document: bvnr_doc_stream_read and the parsing
 *                 frame reader, fed the whole stream at once and in 1460-byte
 *                 pieces.
 *
 * Messages of 150 B, 30 KB and 400 KB (the last spans seven chunks); framed
 * documents of about 50 B and 30 KB. Not a test: it is built with the others
 * and run by hand,
 *
 *     ./build/tests/bvnr_stream_bench [seconds-per-cell]
 *
 * Compiled with -DBVNR_BENCH_BATCH_ONLY it uses only the API that predates the
 * fed demux and the frame reader, so the same source measures a library from
 * before them (for the before/after figures of bvnr_mux_send).
 */

#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#  define _POSIX_C_SOURCE 200809L
#endif

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "bovnar.h"
#include "bovnar_stream.h"

static double now_sec(void)
{
#if defined(CLOCK_MONOTONIC)
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
#else
	return (double)clock() / (double)CLOCKS_PER_SEC;
#endif
}

static uint64_t g_sink;      /* keeps the work observable */

static void* xmalloc(size_t n)
{
	void* p = malloc(n);
	if (!p) { fprintf(stderr, "out of memory\n"); exit(2); }
	return p;
}

static bool count_msg(void* ud, uint64_t ch, const uint8_t* p, uint64_t n)
{
	(void)ud;
	g_sink += ch + n + (n ? p[n - 1] : 0);
	return true;
}

typedef struct {
	uint8_t* doc;
	uint64_t doc_len;
	uint32_t msgs;
	uint32_t size;
} cell_t;

static uint8_t* g_payload;

static uint64_t write_stream(uint8_t* out, uint64_t cap, uint32_t msgs,
	uint32_t size, bvnr_writer_t* w)
{
	bvnr_open_write_mem(w, out, cap, false, NULL);
	bvnr_mux_begin(w, "mux");
	for (uint32_t i = 0; i < msgs; i++)
		bvnr_mux_send(w, i % 4u, g_payload + (i % 64u), size);
	bvnr_mux_end(w);
	return bvnr_write_finish(w) ? bvnr_writer_bytes_written(w) : 0;
}

static void report(const char* what, const cell_t* c, double secs,
	uint64_t rounds)
{
	double per = secs / ((double)rounds * (double)c->msgs);
	double mbs = (double)c->doc_len * (double)rounds / secs / 1e6;
	printf("  %-34s %8u B  %10.1f ns/msg  %9.1f MB/s\n", what, c->size,
	       per * 1e9, mbs);
}

/* Run `fn` until `budget` seconds have passed, at least twice. */
#define BENCH(budget, rounds_out, secs_out, body) do {                       \
	uint64_t _r = 0;                                                          \
	double _t0 = now_sec(), _t;                                               \
	do { body; _r++; _t = now_sec() - _t0; } while (_t < (budget) || _r < 2); \
	*(rounds_out) = _r; *(secs_out) = _t;                                     \
} while (0)

static void bench_send(cell_t* c, double budget)
{
	bvnr_writer_t* w = bvnr_writer_create();
	uint64_t rounds; double secs;
	BENCH(budget, &rounds, &secs,
	      write_stream(c->doc, c->doc_len + 4096, c->msgs, c->size, w));
	report("bvnr_mux_send", c, secs, rounds);
	bvnr_writer_destroy(w);
}

static void bench_read(cell_t* c, double budget)
{
	bvnr_demux_t* dm = bvnr_demux_create(count_msg, NULL, 0);
	bvnr_reader_t* r = bvnr_reader_create();
	bvnr_read_flags_t f;
	memset(&f, 0, sizeof(f));
	f.userdata = dm;
	f.on_verified = bvnr_demux_on_event;
	uint64_t rounds; double secs;
	BENCH(budget, &rounds, &secs,
	      bvnr_open_read_mem(r, c->doc, c->doc_len, NULL, 0, &f);
	      bvnr_read(r));
	report("bvnr_read + bvnr_demux_on_event", c, secs, rounds);
	bvnr_reader_destroy(r);
	bvnr_demux_destroy(dm);
}

#if !defined(BVNR_BENCH_BATCH_ONLY)
static void feed_all(bvnr_demux_t* dm, const cell_t* c, uint64_t piece,
	bool pull)
{
	bvnr_demux_item_t it;
	bvnr_demux_open_feed(dm, NULL, false);
	for (uint64_t at = 0; at < c->doc_len; at += piece) {
		uint64_t n = c->doc_len - at < piece ? c->doc_len - at : piece;
		bvnr_demux_feed(dm, c->doc + at, n);
		if (pull)
			while (bvnr_demux_next(dm, &it) == BVNR_NEXT_MESSAGE)
				g_sink += it.channel + it.length +
					  (it.length ? it.data[it.length - 1] : 0);
	}
	bvnr_demux_finish(dm);
}

static void bench_feed(cell_t* c, double budget, uint64_t piece, bool pull)
{
	bvnr_demux_t* dm = bvnr_demux_create(pull ? NULL : count_msg, NULL, 0);
	uint64_t rounds; double secs;
	uint64_t p = piece ? piece : c->doc_len;
	BENCH(budget, &rounds, &secs, feed_all(dm, c, p, pull));
	char what[64];
	snprintf(what, sizeof(what), "feed %s, %s", pull ? "pull" : "callback",
		 piece ? (piece == 1460 ? "1460 B pieces" : "64 B pieces")
		       : "whole buffer");
	report(what, c, secs, rounds);
	bvnr_demux_destroy(dm);
}
#endif

/* ---- framed documents ---------------------------------------------------*/

typedef struct {
	uint8_t* stream;
	uint64_t stream_len;
	uint32_t docs;
	uint64_t doc_len;
} frames_t;

static bool count_value(void* ud, bvnr_event_t ev, bvnr_data_t* d)
{
	(void)ud;
	g_sink += (uint64_t)ev + (d ? d->length : 0);
	return true;
}

/* `docs` copies of one document, each in its own frame. */
static void frames_build(frames_t* f, const char* doc, uint64_t doc_len,
	uint32_t docs)
{
	uint64_t cap = (uint64_t)docs * (doc_len + BVNR_FRAME_HEADER_SIZE);
	f->stream  = xmalloc((size_t)cap);
	f->docs    = docs;
	f->doc_len = doc_len;
	bvnr_sink_t sink;
	bvnr_sink_to_mem(&sink, f->stream, cap);
	for (uint32_t i = 0; i < docs; i++)
		bvnr_frame_write(&sink, doc, doc_len);
	f->stream_len = bvnr_sink_bytes_written(&sink);
}

static void frames_report(const char* what, const frames_t* f, double secs,
	uint64_t rounds)
{
	double per = secs / ((double)rounds * (double)f->docs);
	double mbs = (double)f->stream_len * (double)rounds / secs / 1e6;
	printf("  %-34s %8llu B  %10.1f ns/doc  %9.1f MB/s\n", what,
	       (unsigned long long)f->doc_len, per * 1e9, mbs);
}

static void frames_opts(bvnr_doc_stream_opts_t* o, bvnr_read_flags_t* fl)
{
	memset(fl, 0, sizeof(*fl));
	fl->on_verified = count_value;
	memset(o, 0, sizeof(*o));
	o->flags = fl;
}

static void bench_doc_stream(const frames_t* f, double budget)
{
	bvnr_read_flags_t fl;
	bvnr_doc_stream_opts_t o;
	frames_opts(&o, &fl);
	uint64_t rounds; double secs;
	bvnr_source_t src;
	BENCH(budget, &rounds, &secs,
	      bvnr_source_from_mem(&src, f->stream, f->stream_len);
	      bvnr_doc_stream_read(&src, &o, NULL));
	frames_report("bvnr_doc_stream_read", f, secs, rounds);
}

#if !defined(BVNR_BENCH_BATCH_ONLY)
static void frames_feed(const frames_t* f, const bvnr_doc_stream_opts_t* o,
	uint64_t piece)
{
	bvnr_frame_reader_t* fr = bvnr_frame_reader_create(o);
	for (uint64_t at = 0; at < f->stream_len; at += piece) {
		uint64_t n = f->stream_len - at < piece ? f->stream_len - at : piece;
		bvnr_frame_reader_feed(fr, f->stream + at, n);
	}
	bvnr_frame_reader_finish(fr);
	g_sink += bvnr_frame_reader_count(fr);
	bvnr_frame_reader_destroy(fr);
}

static void bench_frame_reader(const frames_t* f, double budget,
	uint64_t piece)
{
	bvnr_read_flags_t fl;
	bvnr_doc_stream_opts_t o;
	frames_opts(&o, &fl);
	uint64_t rounds; double secs;
	BENCH(budget, &rounds, &secs,
	      frames_feed(f, &o, piece ? piece : f->stream_len));
	frames_report(piece ? "frame reader, 1460 B pieces"
			    : "frame reader, whole buffer", f, secs, rounds);
}
#endif

/* About 30 KB of assignments, numbers and strings alternating. */
static uint64_t large_doc(char* out, uint64_t cap)
{
	uint64_t n = 0;
	for (uint32_t i = 0; n + 64 < cap && n < 30000; i++)
		n += (uint64_t)snprintf(out + n, (size_t)(cap - n),
			i & 1 ? ".k%u = \"value %u\";\n" : ".k%u = %u;\n",
			i, i * 7919u);
	return n;
}

static void bench_frames(double budget)
{
	static const char small[] = ".id = 12345; .name = \"sensor-7\"; .t = 21.5;";
	static char large[32768];
	const char* docs[2] = { small, large };
	uint64_t lens[2] = { sizeof(small) - 1, large_doc(large, sizeof(large)) };
	static const uint32_t counts[2] = { 20000, 200 };
	for (int s = 0; s < 2; s++) {
		frames_t f;
		frames_build(&f, docs[s], lens[s], counts[s]);
		printf("%u framed documents of %llu bytes\n", f.docs,
		       (unsigned long long)f.doc_len);
		bench_doc_stream(&f, budget);
#if !defined(BVNR_BENCH_BATCH_ONLY)
		bench_frame_reader(&f, budget, 0);
		bench_frame_reader(&f, budget, 1460);
#endif
		free(f.stream);
	}
}

int main(int argc, char** argv)
{
	double budget = argc > 1 ? atof(argv[1]) : 0.5;
	static const uint32_t sizes[] = { 150, 30000, 400000 };
	static const uint32_t counts[] = { 20000, 400, 30 };
	g_payload = xmalloc(400000 + 64);
	for (uint32_t i = 0; i < 400000 + 64; i++)
		g_payload[i] = (uint8_t)(i * 2654435761u >> 11);

	printf("bovnar %s streaming bench, %.2f s per cell\n",
	       BVNR_VERSION_STRING, budget);
	for (int s = 0; s < 3; s++) {
		cell_t c = { NULL, 0, counts[s], sizes[s] };
		uint64_t cap = (uint64_t)c.msgs * (c.size + 64u) + 65536u;
		c.doc = xmalloc((size_t)cap);
		bvnr_writer_t* w = bvnr_writer_create();
		c.doc_len = write_stream(c.doc, cap, c.msgs, c.size, w);
		bvnr_writer_destroy(w);
		printf("%u messages of %u bytes, %llu-byte document\n", c.msgs,
		       c.size, (unsigned long long)c.doc_len);
		bench_send(&c, budget);
		bench_read(&c, budget);
#if !defined(BVNR_BENCH_BATCH_ONLY)
		bench_feed(&c, budget, 0, false);
		bench_feed(&c, budget, 0, true);
		bench_feed(&c, budget, 1460, false);
		bench_feed(&c, budget, 1460, true);
		bench_feed(&c, budget, 64, false);
		bench_feed(&c, budget, 64, true);
#endif
		free(c.doc);
	}
	bench_frames(budget);
	free(g_payload);
	return g_sink == 42 ? 1 : 0;
}
