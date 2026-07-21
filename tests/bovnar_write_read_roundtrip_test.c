/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2026 Janos Sonntag
 *
 * The library's central promise: what the writer emits, the reader reads back
 * unchanged. Existing tests check that per-family on hand-picked values; this
 * sweeps the dimensions systematically -- every integer width at its boundary
 * values, every numeric base for arbitrary-precision integers, and the exact set
 * of control bytes a string may carry.
 *
 * The last of those is a SYMMETRY check rather than a round-trip: the reader
 * accepts a fixed set of control characters via \xHH escapes, and the writer
 * accepts a fixed set inside a string. If those sets ever diverge, the library
 * can read a document whose value it cannot write back -- the same class of
 * defect as the writer-vs-reader disagreements found in bvn_validate_reference
 * and bvn_validate_digits_for_base, in the opposite direction.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bovnar.h"
#include "bovnar_dom.h"
#include "bvn_int.h"

static int g_pass, g_fail;

#define CHECK(cond, msg) do {                                                 \
	if (cond) { g_pass++; }                                                   \
	else { g_fail++; fprintf(stderr, "  FAIL line %d: %s\n", __LINE__, msg); } \
} while (0)

/* Emit one assignment, parse it back, hand the node to the caller. */
static bvn_dom_doc_t *emit_and_parse(uint8_t *buf, size_t cap,
                                     bool (*emit)(bvnr_writer_t *, void *),
                                     void *ud, bool *wrote)
{
	bvnr_sink_t sink;
	bvnr_sink_to_mem(&sink, buf, cap);
	bvnr_writer_t *w = bvnr_writer_create();
	if (!w) { *wrote = false; return NULL; }
	bvnr_write_flags_t wf;
	memset(&wf, 0, sizeof wf);
	wf.emit_version = true;
	bvnr_open_write_sink(w, &sink, true, &wf);
	*wrote = emit(w, ud) && bvnr_write_finish(w);
	uint64_t n = bvnr_writer_bytes_written(w);
	bvnr_writer_destroy(w);
	if (!*wrote || n == 0 || n >= cap) return NULL;
	buf[n] = 0;
	bvn_dom_doc_t *d = bvn_dom_parse(buf, (uint32_t)n);
	if (d && bvn_dom_doc_get_parse_error(d) != error_none) {
		bvn_dom_doc_destroy(d);
		return NULL;
	}
	return d;
}

struct uintjob { uint32_t width; uint64_t value; };
static bool emit_uint(bvnr_writer_t *w, void *ud)
{
	struct uintjob *j = ud;
	return bvnr_write_uint(w, "v", j->width, j->value);
}
struct sintjob { uint32_t width; int64_t value; };
static bool emit_sint(bvnr_writer_t *w, void *ud)
{
	struct sintjob *j = ud;
	return bvnr_write_sint(w, "v", j->width, j->value);
}

static void test_integer_widths(void)
{
	printf("  test_integer_widths...\n");
	static const uint32_t UW[] = { 1, 2, 7, 8, 15, 16, 31, 32, 63, 64 };
	uint8_t buf[1024];

	for (size_t i = 0; i < sizeof UW / sizeof UW[0]; i++) {
		uint32_t w = UW[i];
		uint64_t max = (w >= 64) ? ~(uint64_t)0 : (((uint64_t)1 << w) - 1u);
		uint64_t vals[] = { 0, 1, max / 2, max - (max ? 1 : 0), max };
		for (size_t k = 0; k < sizeof vals / sizeof vals[0]; k++) {
			struct uintjob j = { w, vals[k] };
			bool wrote = false;
			bvn_dom_doc_t *d = emit_and_parse(buf, sizeof buf, emit_uint,
							  &j, &wrote);
			CHECK(wrote, "writer accepts a value that fits its width");
			CHECK(d != NULL, "the emitted uint document re-reads");
			if (!d) continue;
			bvn_dom_node_t *n = bvn_dom_lookup(d, "v");
			uint64_t got = 0;
			CHECK(n && bvn_dom_get_u64(n, &got) && got == vals[k],
				"uint value survives the round-trip");
			bvn_dom_doc_destroy(d);
		}
	}

	static const uint32_t SW[] = { 2, 8, 16, 32, 64 };
	for (size_t i = 0; i < sizeof SW / sizeof SW[0]; i++) {
		uint32_t w = SW[i];
		int64_t hi = (w >= 64) ? INT64_MAX : (((int64_t)1 << (w - 1)) - 1);
		int64_t lo = (w >= 64) ? INT64_MIN : -((int64_t)1 << (w - 1));
		int64_t vals[] = { 0, 1, -1, hi, lo, hi - 1, lo + 1 };
		for (size_t k = 0; k < sizeof vals / sizeof vals[0]; k++) {
			struct sintjob j = { w, vals[k] };
			bool wrote = false;
			bvn_dom_doc_t *d = emit_and_parse(buf, sizeof buf, emit_sint,
							  &j, &wrote);
			CHECK(wrote, "writer accepts a signed value that fits");
			CHECK(d != NULL, "the emitted sint document re-reads");
			if (!d) continue;
			bvn_dom_node_t *n = bvn_dom_lookup(d, "v");
			int64_t got = 0;
			CHECK(n && bvn_dom_get_i64(n, &got) && got == vals[k],
				"sint value survives the round-trip");
			bvn_dom_doc_destroy(d);
		}
	}
}

struct bvnijob { bvn_int_t *n; uint32_t width, base; };
static bool emit_bvni(bvnr_writer_t *w, void *ud)
{
	struct bvnijob *j = ud;
	return bvnr_write_bvni(w, "v", j->n, j->width, j->base);
}

static void test_every_numeric_base(void)
{
	printf("  test_every_numeric_base...\n");
	static const char *V[] = {
		"0", "1", "255", "65535", "4294967295", "18446744073709551615",
		"340282366920938463463374607431768211455",
		"123456789012345678901234567890",
	};
	static const uint32_t B[] = { 2, 8, 10, 16, 36, 62, 64, 85 };
	static const uint32_t W[] = { 64, 128, 256 };
	uint8_t buf[4096];

	for (size_t v = 0; v < sizeof V / sizeof V[0]; v++)
	for (size_t b = 0; b < sizeof B / sizeof B[0]; b++)
	for (size_t w = 0; w < sizeof W / sizeof W[0]; w++) {
		bvn_int_t *n = bvn_int_alloc();
		if (!n || !bvn_int_from_str(n, V[v], 10)) { bvn_int_free(n); continue; }
		/* A width too small for the value is refused by design, not a defect. */
		if ((uint32_t)bvn_int_bitlen(n) > W[w]) { bvn_int_free(n); continue; }
		struct bvnijob j = { n, W[w], B[b] };
		bool wrote = false;
		bvn_dom_doc_t *d = emit_and_parse(buf, sizeof buf, emit_bvni, &j, &wrote);
		CHECK(wrote, "writer accepts the bignum at this width and base");
		CHECK(d != NULL, "the emitted bignum document re-reads");
		if (d) {
			bvn_dom_node_t *nd = bvn_dom_lookup(d, "v");
			char *back = nd ? bvn_dom_int_to_str(nd, 10u) : NULL;
			CHECK(back && strcmp(back, V[v]) == 0,
				"every digit survives the round-trip in every base");
			free(back);
			bvn_dom_doc_destroy(d);
		}
		bvn_int_free(n);
	}
}

static void test_control_byte_symmetry(void)
{
	printf("  test_control_byte_symmetry...\n");
	int writable = 0, readable = 0, asymmetric = 0;

	for (int c = 1; c < 0x21; c++) {
		char z[4] = { 'a', (char)c, 'b', 0 };

		uint8_t buf[256];
		bvnr_sink_t sink;
		bvnr_sink_to_mem(&sink, buf, sizeof buf);
		bvnr_writer_t *w = bvnr_writer_create();
		bvnr_write_flags_t wf;
		memset(&wf, 0, sizeof wf);
		bvnr_open_write_sink(w, &sink, true, &wf);
		bool wrote = bvnr_write_string(w, "v", z) && bvnr_write_finish(w);
		uint64_t n = bvnr_writer_bytes_written(w);
		bvnr_writer_destroy(w);

		char doc[64];
		snprintf(doc, sizeof doc,
			 "#!bovnar 1.1\n.v = \"a\\x%02X" "b\";\n", c);
		bvn_dom_doc_t *rd = bvn_dom_parse(doc, (uint32_t)strlen(doc));
		bool reads = rd && bvn_dom_doc_get_parse_error(rd) == error_none;
		if (rd) bvn_dom_doc_destroy(rd);

		writable += wrote;
		readable += reads;
		if (reads != wrote) {
			asymmetric++;
			fprintf(stderr, "  byte 0x%02x: writer=%d reader=%d\n",
				c, wrote, reads);
		}
		if (wrote && n && n < sizeof buf) {
			buf[n] = 0;
			bvn_dom_doc_t *d2 = bvn_dom_parse(buf, (uint32_t)n);
			const char *s = NULL;
			uint32_t sl = 0;
			bvn_dom_node_t *nd = d2 ? bvn_dom_lookup(d2, "v") : NULL;
			CHECK(nd && bvn_dom_get_string(nd, &s, &sl)
			      && sl == 3 && memcmp(s, z, 3) == 0,
				"a writable control byte survives the round-trip");
			if (d2) bvn_dom_doc_destroy(d2);
		}
	}
	CHECK(asymmetric == 0,
		"the reader and writer agree on which control bytes a string may hold");
	CHECK(writable == readable,
		"...and on how many");
	CHECK(writable > 0, "at least some control bytes are permitted");
}

int main(void)
{
	printf("Running bovnar_write_read_roundtrip_test suite...\n");
	test_integer_widths();
	test_every_numeric_base();
	test_control_byte_symmetry();
	if (g_fail) {
		printf("\nFAILED %d of %d checks\n", g_fail, g_pass + g_fail);
		return 1;
	}
	printf("\nPASSED %d checks\n", g_pass);
	return 0;
}
