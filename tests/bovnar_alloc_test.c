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
 * The hot paths of the streaming layer allocate nothing per message once
 * warm: bvnr_mux_send (whose 64 KiB chunk used to be malloc'd and freed on
 * every call), alone and followed by bvnr_write_flush into a callback sink,
 * the pull-style drains of a fed demux and frame reader for messages and
 * documents that lie whole in the fed buffer, and every path that
 * re-opens a reader for the next document -- the parsing frame reader,
 * bvnr_doc_stream_read and a fed demux -- which used to allocate the reader's
 * frame stack and scratch buffers again (about 120 KB) for each one.
 *
 * Counted by interposing malloc/calloc/realloc on glibc, where the library's
 * calls bind to the executable's definitions. Elsewhere, and under
 * AddressSanitizer (which owns malloc itself), the test reports itself skipped
 * (exit 77).
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bovnar.h"
#include "bovnar_stream.h"

#if defined(__has_feature)
#  if __has_feature(address_sanitizer)
#    define BVN_TEST_ASAN 1
#  endif
#endif
#if defined(__SANITIZE_ADDRESS__)
#  define BVN_TEST_ASAN 1
#endif

#if defined(__GLIBC__) && !defined(BVN_TEST_ASAN)

extern void* __libc_malloc(size_t n);
extern void* __libc_calloc(size_t n, size_t m);
extern void* __libc_realloc(void* p, size_t n);
extern void  __libc_free(void* p);

static bool          g_counting;
static unsigned long g_allocs;

void* malloc(size_t n)
{
	if (g_counting) g_allocs++;
	return __libc_malloc(n);
}
void* calloc(size_t n, size_t m)
{
	if (g_counting) g_allocs++;
	return __libc_calloc(n, m);
}
void* realloc(void* p, size_t n)
{
	if (g_counting) g_allocs++;
	return __libc_realloc(p, n);
}
void free(void* p)
{
	__libc_free(p);
}

static int g_failures = 0;

static void expect_none(const char* what, unsigned long allocs)
{
	printf("  %-58s %lu allocations\n", what, allocs);
	if (allocs) g_failures++;
}

/* A transport that takes every byte: what a socket's send does, minus the
 * system call. */
static uint64_t g_pushed;
static bool take_push(bvnr_sink_t* s, const void* p, uint32_t n)
{
	(void)s; (void)p;
	g_pushed += n;
	return true;
}
static bool take_flush(bvnr_sink_t* s) { (void)s; return true; }

static void count_begin(void) { g_allocs = 0; g_counting = true; }
static unsigned long count_end(void) { g_counting = false; return g_allocs; }

int main(void)
{
	/* The test's own buffers are static: a malloc here would go through the
	 * wrapper above, which confuses the static analyser, and is not what is
	 * being measured anyway. */
	static uint8_t payload[400000];
	static uint8_t out[64u << 20];
	for (size_t i = 0; i < sizeof(payload); i++)
		payload[i] = (uint8_t)(i * 7u);
	size_t cap = sizeof(out);

	/* The counter must see what the library allocates, or every "0" below
	 * proves nothing. */
	count_begin();
	bvnr_reader_t* probe = bvnr_reader_create();
	unsigned long seen = count_end();
	bvnr_reader_destroy(probe);
	printf("  %-58s %lu allocations\n", "bvnr_reader_create (must count)",
	       seen);
	if (!seen) g_failures++;

	printf("allocations per call, once warm\n");
	bvnr_writer_t* w = bvnr_writer_create();
	bvnr_open_write_mem(w, out, cap, false, NULL);
	bvnr_mux_begin(w, "mux");
	bvnr_mux_send(w, 1, payload, 150);       /* warm: the chunk buffer */
	static const uint64_t sizes[] = { 150, 30000, 400000 };
	for (size_t s = 0; s < 3; s++) {
		count_begin();
		for (int i = 0; i < 40; i++)
			bvnr_mux_send(w, (uint64_t)i, payload, sizes[s]);
		char what[64];
		snprintf(what, sizeof(what), "40 x bvnr_mux_send of %llu bytes",
			 (unsigned long long)sizes[s]);
		expect_none(what, count_end());
	}
	bvnr_mux_end(w);
	bvnr_write_finish(w);
	uint64_t doc_len = bvnr_writer_bytes_written(w);

	/* Re-opening keeps the chunk buffer. */
	bvnr_open_write_mem(w, out + doc_len, cap - doc_len, false, NULL);
	bvnr_mux_begin(w, "mux");
	count_begin();
	bvnr_mux_send(w, 1, payload, 150);
	expect_none("bvnr_mux_send after re-opening the writer", count_end());
	bvnr_writer_destroy(w);

	/* Live: each message sent and flushed into a callback sink. */
	w = bvnr_writer_create();
	bvnr_sink_t live;
	bvnr_sink_to_callback(&live, take_push, take_flush, NULL);
	bvnr_open_write_sink(w, &live, false, NULL);
	bvnr_mux_begin(w, "mux");
	bvnr_mux_send(w, 1, payload, 150);       /* warm: the chunk buffer */
	bvnr_write_flush(w);
	for (size_t s = 0; s < 3; s++) {
		count_begin();
		for (int i = 0; i < 40; i++) {
			bvnr_mux_send(w, (uint64_t)i, payload, sizes[s]);
			bvnr_write_flush(w);
		}
		char what[64];
		snprintf(what, sizeof(what), "40 x bvnr_mux_send + bvnr_write_flush "
			 "of %llu B", (unsigned long long)sizes[s]);
		expect_none(what, count_end());
	}
	bvnr_mux_end(w);
	if (!bvnr_write_finish(w) || g_pushed != bvnr_writer_bytes_written(w)) {
		printf("  the live writer did not push its output\n");
		g_failures++;
	}
	bvnr_writer_destroy(w);

	/* A stream of small messages, drained from one fed buffer. */
	uint8_t* small = out + (32u << 20);
	w = bvnr_writer_create();
	bvnr_open_write_mem(w, small, 16u << 20, false, NULL);
	bvnr_mux_begin(w, "mux");
	for (int i = 0; i < 20000; i++)
		bvnr_mux_send(w, (uint64_t)(i % 5), payload + i % 100, 150);
	bvnr_mux_end(w);
	bvnr_write_finish(w);
	uint64_t small_len = bvnr_writer_bytes_written(w);
	bvnr_writer_destroy(w);

	bvnr_demux_t* dm = bvnr_demux_create(NULL, NULL, 0);
	bvnr_demux_item_t it;
	for (int round = 0; round < 2; round++) {
		/* The first round is the warm-up: the reader's buffers, the
		 * channels. */
		bvnr_demux_open_feed(dm, NULL, false);
		if (round) count_begin();
		bvnr_demux_feed(dm, small, small_len);
		unsigned long msgs = 0;
		while (bvnr_demux_next(dm, &it) == BVNR_NEXT_MESSAGE)
			msgs++;
		if (round) {
			unsigned long a = count_end();
			char what[64];
			snprintf(what, sizeof(what),
				 "bvnr_demux_next draining %lu messages", msgs);
			expect_none(what, a);
		}
		bvnr_demux_finish(dm);
	}
	bvnr_demux_destroy(dm);

	/* Frames handed out from one fed buffer. */
	uint8_t* frames = out + (48u << 20);
	bvnr_sink_t sink;
	bvnr_sink_to_mem(&sink, frames, 8u << 20);
	for (int i = 0; i < 10000; i++)
		bvnr_frame_write(&sink, payload, 150);
	uint64_t frames_len = bvnr_sink_bytes_written(&sink);
	bvnr_frame_reader_t* fr = bvnr_frame_reader_create_pull(0);
	count_begin();
	bvnr_frame_reader_feed(fr, frames, frames_len);
	const uint8_t* doc;
	uint64_t dl;
	unsigned long docs = 0;
	while (bvnr_frame_reader_next(fr, &doc, &dl) == BVNR_NEXT_DOCUMENT)
		docs++;
	char what[64];
	snprintf(what, sizeof(what), "bvnr_frame_reader_next handing out %lu "
		 "documents", docs);
	expect_none(what, count_end());
	bvnr_frame_reader_destroy(fr);

	/* Parsing frame reader: one document after another, each re-opening the
	 * reader. Documents with nested arrays, an octet stream, a version
	 * directive, a failure, under continue_past_failed. */
	static const char* const fdocs[] = {
		".id = 1; .name = \"doc\"; .t = 21.5;",
		".m = [[1,2]/[3,4]]; .s = { .a = [1, [2, 3]]; };",
		"#!bovnar 1.1\n.t = 2026-06-15;",
		".bad = @;",
		"",
	};
	uint8_t blob[64] = { 0 };
	static uint8_t octet_doc[256];
	bvnr_writer_t* ow = bvnr_writer_create();
	bvnr_open_write_mem(ow, octet_doc, sizeof(octet_doc), false, NULL);
	bvnr_embed_document(ow, "blob", blob, sizeof(blob));
	bvnr_write_finish(ow);
	uint64_t octet_len = bvnr_writer_bytes_written(ow);
	bvnr_writer_destroy(ow);
	/* Every sixth document is the octet one; the first six are the warm-up,
	 * so they include the largest frame. */
	bvnr_sink_to_mem(&sink, frames, 8u << 20);
	uint64_t warm_len = 0;
	for (int i = 0; i < 1200; i++) {
		const void* d = i % 6 == 5 ? (const void*)octet_doc : fdocs[i % 6];
		uint64_t n = i % 6 == 5 ? octet_len : strlen(fdocs[i % 6]);
		bvnr_frame_write(&sink, d, n);
		if (i == 5) warm_len = bvnr_sink_bytes_written(&sink);
	}
	frames_len = bvnr_sink_bytes_written(&sink);
	bvnr_doc_stream_opts_t fo;
	memset(&fo, 0, sizeof(fo));
	fo.continue_past_failed = true;
	fr = bvnr_frame_reader_create(&fo);
	bvnr_frame_reader_feed(fr, frames, warm_len);     /* warm: the reader */
	count_begin();
	bvnr_frame_reader_feed(fr, frames + warm_len, frames_len - warm_len);
	unsigned long a = count_end();
	snprintf(what, sizeof(what), "parsing frame reader, %llu documents",
		 (unsigned long long)(bvnr_frame_reader_count(fr) - 6u));
	expect_none(what, a);
	if (!bvnr_frame_reader_finish(fr) ||
	    bvnr_frame_reader_count(fr) != 1200u) {
		printf("  parsing frame reader did not read the stream\n");
		g_failures++;
	}
	bvnr_frame_reader_destroy(fr);

	/* bvnr_doc_stream_read: its reader and payload buffer are per call, so
	 * the count must not depend on how many documents the stream holds. */
	unsigned long per_call[2];
	for (int k = 0; k < 2; k++) {
		bvnr_source_t src;
		bvnr_source_from_mem(&src, frames, k ? frames_len : warm_len);
		uint64_t n = 0;
		count_begin();
		bvnr_doc_stream_read(&src, &fo, &n);
		per_call[k] = count_end();
		if (n != (k ? 1200u : 6u)) {
			printf("  bvnr_doc_stream_read did not read the stream\n");
			g_failures++;
		}
	}
	printf("  %-58s %lu, %lu allocations\n",
	       "bvnr_doc_stream_read of 6, of 1200 documents",
	       per_call[0], per_call[1]);
	if (per_call[0] != per_call[1]) g_failures++;

	/* A fed demux re-opened for the next document. */
	dm = bvnr_demux_create(NULL, NULL, 0);
	for (int round = 0; round < 3; round++) {
		if (round == 2) count_begin();
		bvnr_demux_open_feed(dm, NULL, false);
		bvnr_demux_feed(dm, small, small_len);
		while (bvnr_demux_next(dm, &it) == BVNR_NEXT_MESSAGE)
			;
		bvnr_demux_finish(dm);
	}
	expect_none("bvnr_demux_open_feed and a document, re-opened", count_end());
	bvnr_demux_destroy(dm);

	/* The fed document reader itself, across feeds. */
	bvnr_reader_t* r = bvnr_reader_create();
	bvnr_open_read_feed(r, NULL, NULL);
	count_begin();
	for (int i = 0; i < 1000; i++)
		bvnr_read_feed(r, ".k = 12345; .s = \"abc\";\n", 25);
	expect_none("1000 x bvnr_read_feed", count_end());
	bvnr_read_finish(r);
	bvnr_reader_destroy(r);

	printf("%s\n", g_failures ? "FAILED" : "ok");
	return g_failures ? 1 : 0;
}

#else

int main(void)
{
	printf("skipped: counting allocations needs glibc without ASan\n");
	return 77;
}

#endif
