/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2026 Janos Sonntag
 *
 * Distinct reader / writer / DOM objects on distinct threads must be safe to use
 * concurrently.
 *
 * The FAQ says the reader and writer objects "are not thread-safe by design",
 * which is true of SHARING one object -- but the more useful half of the
 * contract was undocumented and untested: the library keeps no mutable shared
 * state, so independent objects on independent threads do not interfere. That is
 * not an accident. bovnar_lexer.c builds its run LUTs from a pre-main
 * constructor precisely so the lazy-init flag can never be raced on, and the
 * comment there says the in-function guard "must not be the sole initialiser
 * under threads".
 *
 * A guarantee that deliberate deserves a test: someone adding a lazily
 * initialised cache later would break it silently, since the failure mode is a
 * rare wrong parse rather than a crash. Run this under ThreadSanitizer to check
 * the stronger property (no data race at all, not merely no observed failure):
 *
 *     cmake -B b -DCMAKE_C_FLAGS=-fsanitize=thread \
 *               -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=thread .
 */
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bovnar.h"
#include "bovnar_dom.h"

#define NTHREADS 8
#define NITER    500

/* Exercises the lexer run LUTs (strings, identifiers, numbers, units, symbols,
 * references, type bodies) plus the DOM builder and the datetime path. */
static const char *DOC =
	"#!bovnar 1.1\n"
	".speed  = <float:64,k~m/h> 88.5;\n"
	".hex    = <uint:32,_16> \"deadbeef\";\n"
	".when   = <datetime:64,tai> 2016-12-31T23:59:60.5Z;\n"
	".grid   = [1, 2, 3]/[4, 5, 6];\n"
	".rec    = {.name = \"caf\\u{e9}\"; .flag = on;};\n"
	".ref    = &.speed;\n"
	".money  = <float_dec:64,$USD> 19.99;\n";

static int g_failures[NTHREADS];

static void *worker(void *arg)
{
	long id = (long)arg;
	uint32_t len = (uint32_t)strlen(DOC);

	for (int i = 0; i < NITER; i++) {
		/* streaming reader */
		bvnr_reader_t *r = bvnr_reader_create();
		if (!r) { g_failures[id]++; continue; }
		bvnr_read_flags_t fl;
		memset(&fl, 0, sizeof fl);
		fl.max_file_size = BVNR_FILESIZE_UNLIMITED;
		if (!bvnr_open_read_mem(r, (const uint8_t *)DOC, len, NULL, 0, &fl)
		    || !bvnr_read(r))
			g_failures[id]++;
		bvnr_reader_destroy(r);

		/* DOM tier, and a value read back out of it */
		bvn_dom_doc_t *d = bvn_dom_parse(DOC, len);
		if (!d || bvn_dom_doc_get_parse_error(d) != error_none) {
			g_failures[id]++;
		} else {
			bvn_dom_node_t *n = bvn_dom_lookup(d, "hex");
			uint64_t v = 0;
			if (!n || !bvn_dom_get_u64(n, &v) || v != 0xdeadbeefu)
				g_failures[id]++;
		}
		bvn_dom_doc_destroy(d);

		/* writer */
		uint8_t out[512];
		bvnr_sink_t sink;
		bvnr_sink_to_mem(&sink, out, sizeof out);
		bvnr_writer_t *w = bvnr_writer_create();
		if (!w) { g_failures[id]++; continue; }
		bvnr_write_flags_t wf;
		memset(&wf, 0, sizeof wf);
		wf.emit_version = true;
		if (!bvnr_open_write_sink(w, &sink, true, &wf)
		    || !bvnr_write_uint(w, "port", 16, 443)
		    || !bvnr_write_finish(w))
			g_failures[id]++;
		bvnr_writer_destroy(w);
	}
	return NULL;
}

int main(void)
{
	pthread_t threads[NTHREADS];
	memset(g_failures, 0, sizeof g_failures);

	for (long i = 0; i < NTHREADS; i++) {
		if (pthread_create(&threads[i], NULL, worker, (void *)i) != 0) {
			printf("FAILED: could not create thread %ld\n", i);
			return 1;
		}
	}
	for (int i = 0; i < NTHREADS; i++)
		pthread_join(threads[i], NULL);

	int total = 0;
	for (int i = 0; i < NTHREADS; i++)
		total += g_failures[i];

	if (total) {
		printf("FAILED %d of %d operations across %d threads\n",
		       total, NTHREADS * NITER * 3, NTHREADS);
		return 1;
	}
	printf("PASSED %d concurrent operations across %d threads\n",
	       NTHREADS * NITER * 3, NTHREADS);
	return 0;
}
