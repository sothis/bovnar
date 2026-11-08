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
 *                 1460-byte and 64-byte pieces.
 *
 * Messages of 150 B, 30 KB and 400 KB (the last spans seven chunks). Not a
 * test: it is built with the others and run by hand,
 *
 *     ./build/tests/bvnr_stream_bench [seconds-per-cell]
 *
 * Compiled with -DBVNR_BENCH_BATCH_ONLY it uses only the API that predates the
 * fed demux, so the same source measures a library from before it (for the
 * before/after figures of bvnr_mux_send).
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
	free(g_payload);
	return g_sink == 42 ? 1 : 0;
}
