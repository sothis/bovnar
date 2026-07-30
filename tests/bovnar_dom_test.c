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

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "bovnar_dom.h"
#include "bovnar.h"
#include "bvn_int.h"

static int failures = 0;
static int tests = 0;

#define ASSERT_TRUE(cond, msg) do {                                   \
	tests++;                                                          \
	if (!(cond)) {                                                    \
		fprintf(stderr, "FAIL line %d: %s\n", __LINE__, (msg));   \
		failures++;                                                   \
	}                                                                 \
} while (0)

#define ASSERT_EQ_INT(a, b, msg) do {                                  \
	tests++;                                                          \
	int64_t _a = (int64_t)(a);                                        \
	int64_t _b = (int64_t)(b);                                        \
	if (_a != _b) {                                                   \
		fprintf(stderr, "FAIL line %d: %s\n  got %lld, expected %lld\n", \
				__LINE__, (msg), (long long)_a, (long long)_b);       \
		failures++;                                                   \
	}                                                                 \
} while (0)

#define ASSERT_EQ_UINT(a, b, msg) do {                                 \
	tests++;                                                          \
	uint64_t _a = (uint64_t)(a);                                      \
	uint64_t _b = (uint64_t)(b);                                      \
	if (_a != _b) {                                                   \
		fprintf(stderr, "FAIL line %d: %s\n  got %llu, expected %llu\n", \
				__LINE__, (msg), (unsigned long long)_a, (unsigned long long)_b); \
		failures++;                                                   \
	}                                                                 \
} while (0)

#define ASSERT_EQ_STR(a, b, msg) do {                                   \
	tests++;                                                          \
	const char *_a = (a);                                              \
	const char *_b = (b);                                              \
	if (!_a || !_b || strcmp(_a, _b) != 0) {                          \
		fprintf(stderr, "FAIL line %d: %s\n  got '%s', expected '%s'\n", \
				__LINE__, (msg), _a ? _a : "(null)", _b ? _b : "(null)"); \
		failures++;                                                   \
	}                                                                 \
} while (0)

#define ASSERT_NULL(ptr, msg) do {                                      \
	tests++;                                                          \
	if ((ptr) != NULL) {                                               \
		fprintf(stderr, "FAIL line %d: %s\n  got non-null pointer\n", \
				__LINE__, (msg));                                     \
		failures++;                                                   \
	}                                                                 \
} while (0)

#define ASSERT_NOT_NULL(ptr, msg) do {                                  \
	tests++;                                                          \
	if ((ptr) == NULL) {                                               \
		fprintf(stderr, "FAIL line %d: %s\n  got null pointer\n",   \
				__LINE__, (msg));                                     \
		failures++;                                                   \
	}                                                                 \
} while (0)

static bvn_dom_doc_t *parse_doc(const char *bvn)
{
	bvn_dom_doc_t *doc = bvn_dom_parse(bvn, (uint32_t)strlen(bvn));
	ASSERT_NOT_NULL(doc, "bvn_dom_parse must return a document");
	return doc;
}

static void test_parse_basic_dom(void)
{
	const char *bvn =
		".app_name = \"Bovnar Demo\";\n"
		".version = 1;\n"
		".debug = false;\n"
		".config = {\n"
		"    .host = \"localhost\";\n"
		"    .port = 8080;\n"
		"    .timeout = <uint:32,s> 30;\n"
		"};\n"
		".accel = <float:64,m/s²> 9.81;\n"
		".storage = <uint:64,Ki~B> 1024;\n";

	bvn_dom_doc_t *doc = parse_doc(bvn);
	if (!doc) return;

	ASSERT_EQ_UINT(bvn_dom_doc_count(doc), 6, "document contains six entries");
	ASSERT_EQ_INT(bvn_dom_doc_get_parse_error(doc), error_none, "parse error must be none");

	bvn_dom_node_t *app = bvn_dom_lookup(doc, ".app_name");
	ASSERT_NOT_NULL(app, "lookup .app_name must succeed");
	if (app) {
		const char *s; uint32_t slen = 0;
		ASSERT_TRUE(bvn_dom_get_string(app, &s, &slen), "get_string must succeed for app_name");
		ASSERT_EQ_UINT(slen, 11, "app_name string length");
		ASSERT_EQ_STR(s, "Bovnar Demo", "app_name string contents");
	}

	bvn_dom_node_t *ver = bvn_dom_lookup(doc, "version");
	ASSERT_NOT_NULL(ver, "lookup version without leading dot must succeed");
	if (ver) {
		int64_t v = 0;
		ASSERT_TRUE(bvn_dom_get_i64(ver, &v), "get_int must succeed for version");
		ASSERT_EQ_INT(v, 1, "version value must be 1");
		double vf = 0.0;
		ASSERT_TRUE(bvn_dom_get_float(ver, &vf), "get_float must convert integer version");
		ASSERT_EQ_INT((int64_t)vf, 1, "version as float must equal 1.0");
	}

	bvn_dom_node_t *timeout = bvn_dom_lookup(doc, ".config.timeout");
	ASSERT_NOT_NULL(timeout, "lookup .config.timeout must succeed");
	if (timeout) {
		uint64_t v = 0;
		ASSERT_TRUE(bvn_dom_get_u64(timeout, &v), "get_uint must succeed for timeout");
		ASSERT_EQ_UINT(v, 30, "timeout value must be 30");
		char ubuf[BVNR_UNIT_STRING_MAX] = {0};
		ASSERT_EQ_INT(bvn_dom_get_unit_string(timeout, ubuf, sizeof(ubuf)), 1, "unit string length for seconds");
		ASSERT_EQ_STR(ubuf, "s", "timeout unit string");
	}

	bvn_dom_node_t *accel = bvn_dom_lookup(doc, ".accel");
	ASSERT_NOT_NULL(accel, "lookup .accel must succeed");
	if (accel) {
		double base = bvn_dom_get_value_in_base_units(accel);
		ASSERT_EQ_INT((int64_t)(base * 1000.0), 9810, "acceleration in base units must be 9.81 m/s²");
	}

	bvn_dom_node_t *storage = bvn_dom_lookup(doc, ".storage");
	ASSERT_NOT_NULL(storage, "lookup .storage must succeed");
	if (storage) {
		double base = bvn_dom_get_value_in_base_units(storage);
		ASSERT_EQ_INT((int64_t)base, 1048576, "storage in bytes must equal 1048576 for Ki~B");
	}

	bvn_dom_node_t *cfg = bvn_dom_lookup(doc, ".config");
	ASSERT_NOT_NULL(cfg, "lookup .config must succeed");
	if (cfg) {
		ASSERT_EQ_UINT(bvn_dom_struct_count(cfg), 3, "config struct must contain three members");
		const bvn_dom_entry_t *entries = bvn_dom_struct_entries(cfg);
		ASSERT_NOT_NULL(entries, "config entries pointer must not be null");
		ASSERT_EQ_STR(entries[0].key, "host", "first config entry key");
	}

	bvn_dom_doc_destroy(doc);
}

static void test_null_values_and_arrays(void)
{
	const char *bvn =
		".values = [<sint:16> 1, <sint:16> , <sint:16> 3];\n"
		".matrix = [1,2]/[3,4];\n"
		".missing = ;\n"
		".typed_null = <uint:8> ;\n";

	bvn_dom_doc_t *doc = parse_doc(bvn);
	if (!doc) return;

	bvn_dom_node_t *values = bvn_dom_lookup(doc, ".values");
	ASSERT_NOT_NULL(values, "lookup .values must succeed");
	if (values) {
		ASSERT_EQ_UINT(bvn_dom_node_type(values), BVN_DOM_ARRAY, "values must be array");
		ASSERT_EQ_UINT(bvn_dom_array_count(values), 3, "values array must contain three elements");
		ASSERT_EQ_UINT(bvn_dom_array_dims(values), 1, "values array dimensions must be 1");

		bvn_dom_node_t *first = bvn_dom_array_at(values, 0);
		bvn_dom_node_t *second = bvn_dom_array_at(values, 1);
		bvn_dom_node_t *third = bvn_dom_array_at(values, 2);
		ASSERT_NOT_NULL(first, "first array element must not be null");
		ASSERT_NOT_NULL(second, "second array element must not be out-of-bounds");
		ASSERT_NOT_NULL(third, "third array element must not be null");
		ASSERT_TRUE(!bvn_dom_is_null(first), "first element must be an integer");
		ASSERT_TRUE(bvn_dom_is_null(second), "second element must be a null placeholder");
		ASSERT_TRUE(!bvn_dom_is_null(third), "third element must be an integer");
	}

	bvn_dom_node_t *matrix = bvn_dom_lookup(doc, ".matrix");
	ASSERT_NOT_NULL(matrix, "lookup .matrix must succeed");
	if (matrix) {
		ASSERT_EQ_UINT(bvn_dom_node_type(matrix), BVN_DOM_ARRAY, "matrix must be array");
		ASSERT_EQ_UINT(bvn_dom_array_count(matrix), 4, "matrix flat element count must be four");
		ASSERT_EQ_UINT(bvn_dom_array_dims(matrix), 2, "matrix must have two dimensions");
		ASSERT_NULL(bvn_dom_array_at(matrix, 4), "matrix index 4 must return NULL");
	}

	bvn_dom_node_t *missing = bvn_dom_lookup(doc, ".missing");
	ASSERT_NOT_NULL(missing, "lookup .missing must succeed even for null values");
	if (missing)
		ASSERT_TRUE(bvn_dom_is_null(missing), "missing assignment must produce a null node");

	bvn_dom_node_t *typed_null = bvn_dom_lookup(doc, ".typed_null");
	ASSERT_NOT_NULL(typed_null, "lookup .typed_null must succeed");
	if (typed_null) {
		ASSERT_TRUE(bvn_dom_is_null(typed_null), "typed_null must be a null node");
		value_type_spec_t vt = bvn_dom_get_value_type(typed_null);
		ASSERT_EQ_INT(vt.family, vt_uint, "typed_null family must be vt_uint");
		ASSERT_EQ_INT(vt.width, 8, "typed_null width must be 8");
	}

	bvn_dom_doc_destroy(doc);
}

static void test_bom_and_parse_errors(void)
{
	const char valid_bom[] = "\xEF\xBB\xBF.app_name = \"bom\";";
	bvn_dom_doc_t *doc = bvn_dom_parse(valid_bom, (uint32_t)(sizeof(valid_bom) - 1));
	ASSERT_NOT_NULL(doc, "parse must return a document for BOM input");
	if (doc) {
		ASSERT_EQ_INT(bvn_dom_doc_get_parse_error(doc), error_none, "UTF-8 BOM at start must be accepted");
		bvn_dom_doc_destroy(doc);
	}

	const char invalid_bom[] = "# comment\n\xEF\xBB\xBF.app_name = \"bad\";";
	doc = bvn_dom_parse(invalid_bom, (uint32_t)(sizeof(invalid_bom) - 1));
	ASSERT_NOT_NULL(doc, "parse must return a document for invalid BOM input");
	if (doc) {
		ASSERT_TRUE(bvn_dom_doc_get_parse_error(doc) != error_none, "BOM after comment must be rejected");
		bvn_dom_doc_destroy(doc);
	}
}

static void test_lookup_edge_cases(void)
{
	const char *bvn =
		".outer = {\n"
		"    .inner = 42;\n"
		"};\n";

	bvn_dom_doc_t *doc = parse_doc(bvn);
	if (!doc) return;

	ASSERT_NOT_NULL(bvn_dom_lookup(doc, ".outer"), "lookup .outer must succeed");
	ASSERT_NOT_NULL(bvn_dom_lookup(doc, "outer.inner"), "lookup outer.inner must succeed without leading dot");
	ASSERT_NULL(bvn_dom_lookup(doc, ".outer.unknown"), "lookup of missing nested key must return NULL");
	ASSERT_NULL(bvn_dom_lookup(doc, ""), "empty lookup path must return NULL");
	ASSERT_NULL(bvn_dom_lookup(doc, "."), "single dot lookup path must return NULL");
	ASSERT_NULL(bvn_dom_lookup(doc, ".."), "double dot lookup path must return NULL");

	bvn_dom_doc_destroy(doc);
}

static void test_getter_semantics(void)
{
	const char *bvn =
		".negative = <sint:16> -7;\n"
		".seconds = <uint:32,s> 60;\n"
		".text = \"hello\";\n";

	bvn_dom_doc_t *doc = parse_doc(bvn);
	if (!doc) return;

	bvn_dom_node_t *negative = bvn_dom_lookup(doc, ".negative");
	ASSERT_NOT_NULL(negative, "lookup .negative must succeed");
	if (negative) {
		uint64_t u = 0;
		ASSERT_TRUE(!bvn_dom_get_u64(negative, &u), "get_uint must fail for negative signed value");
		double f = 0.0;
		ASSERT_TRUE(bvn_dom_get_float(negative, &f), "get_float must succeed for signed integer");
		ASSERT_EQ_INT((int64_t)f, -7, "negative integer as float must equal -7");
	}

	bvn_dom_node_t *seconds = bvn_dom_lookup(doc, ".seconds");
	ASSERT_NOT_NULL(seconds, "lookup .seconds must succeed");
	if (seconds) {
		char buf[16] = {0};
		ASSERT_EQ_INT(bvn_dom_get_unit_string(seconds, buf, sizeof(buf)), 1, "unit string length for seconds");
		ASSERT_EQ_STR(buf, "s", "seconds unit string");
	}

	bvn_dom_node_t *text = bvn_dom_lookup(doc, ".text");
	ASSERT_NOT_NULL(text, "lookup .text must succeed");
	if (text) {
		const char *s = NULL;
		uint32_t slen = 0;
		ASSERT_TRUE(bvn_dom_get_string(text, &s, &slen), "get_string must succeed for text");
		ASSERT_EQ_UINT(slen, 5, "text length must be five");
		ASSERT_EQ_STR(s, "hello", "text contents must equal hello");
	}

	bvn_dom_doc_destroy(doc);
}

/*
 * Regression: bvn_dom_int_to_str must render an unsigned 64-bit value with the
 * high bit set (>= 2^63, stored as a negative int64 bit pattern) through the
 * UNSIGNED formatter. A prior bug gated the signed path on
 * `family == vt_sint || int_val < 0`, so any uint64 in the upper half came out
 * sign-flipped (18446744073709551615 -> "-1"). The signed path must depend on
 * the family alone.
 */
static void test_int_to_str_uint64_highbit(void)
{
	const char *bvn =
		".big = <uint:64> 18446744073709551615;\n"  /* UINT64_MAX */
		".mid = <uint:64> 9223372036854775809;\n"   /* 2^63 + 1   */
		".neg = <sint:64> -5;\n"
		".pos = <sint:64> 42;\n";

	bvn_dom_doc_t *doc = parse_doc(bvn);
	if (!doc) return;

	struct { const char *path; uint32_t base; const char *want; } cases[] = {
		{ ".big", 10u, "18446744073709551615" },
		{ ".big", 16u, "ffffffffffffffff"     },
		{ ".mid", 10u, "9223372036854775809"  },
		{ ".neg", 10u, "-5"                    },
		{ ".pos", 10u, "42"                    },
	};
	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		bvn_dom_node_t *n = bvn_dom_lookup(doc, cases[i].path);
		ASSERT_NOT_NULL(n, "lookup must succeed");
		if (!n) continue;
		char *s = bvn_dom_int_to_str(n, cases[i].base);
		ASSERT_NOT_NULL(s, "int_to_str must return a string");
		if (s) {
			ASSERT_EQ_STR(s, cases[i].want, "int_to_str rendering");
			bvn_dom_free_string(s);
		}
	}
	bvn_dom_doc_destroy(doc);
}

/*
 * Regression: an array whose elements are themselves arrays (the
 * "[[1,2],[3,4]]" bracket-nested form) produces back-to-back array_row_end
 * events. The builder's deferred array pop must close the inner array at the
 * parent's row_end; otherwise the outer array stays open and silently absorbs
 * every following sibling assignment (the keys after it simply vanished). This
 * guards the fix in on_verified() / ev_array_row_end.
 */
static void test_nested_array_elements(void)
{
	const char *bvn =
		".a = [[1, 2], [3, 4]];\n"
		".b = 99;\n"
		".c = [[[1, 2], [3, 4]], [[5, 6], [7, 8]]];\n"
		".d = ok;\n";
	bvn_dom_doc_t *doc = parse_doc(bvn);
	if (!doc) return;

	ASSERT_EQ_UINT(bvn_dom_doc_get_parse_error(doc), error_none,
				   "nested-array document must parse without error");
	/* The bug dropped every key after the first nested array. */
	ASSERT_EQ_UINT(bvn_dom_doc_count(doc), 4,
				   "all four top-level keys must survive");

	bvn_dom_node_t *a = bvn_dom_lookup(doc, ".a");
	ASSERT_EQ_UINT(bvn_dom_node_type(a), BVN_DOM_ARRAY, ".a must be an array");
	ASSERT_EQ_UINT(bvn_dom_array_count(a), 2,
				   ".a must have exactly two (array) elements, not absorb .b");
	ASSERT_EQ_UINT(bvn_dom_node_type(bvn_dom_array_at(a, 0)), BVN_DOM_ARRAY,
				   ".a[0] must itself be an array");

	/* .b must still exist as its own key with its own value. */
	bvn_dom_node_t *bnode = bvn_dom_lookup(doc, ".b");
	ASSERT_NOT_NULL(bnode, ".b must not be absorbed into .a");
	uint64_t bv = 0;
	ASSERT_TRUE(bvn_dom_get_u64(bnode, &bv) && bv == 99u,
				".b must equal 99");

	/* Deeper nesting must also keep its sibling. */
	bvn_dom_node_t *c = bvn_dom_lookup(doc, ".c");
	ASSERT_EQ_UINT(bvn_dom_node_type(c), BVN_DOM_ARRAY, ".c must be an array");
	ASSERT_EQ_UINT(bvn_dom_array_count(c), 2, ".c must have two elements");
	ASSERT_NOT_NULL(bvn_dom_lookup(doc, ".d"),
					".d after deep nesting must survive");

	bvn_dom_doc_destroy(doc);
}
/*
 * Array row-size consistency is per-array and scoped to '/'-dimension rows only
 * (per-array rectangular, not globally rectangular). The DOM rides on the same
 * reader as the streaming API, so it must surface error_array_row_size_mismatch
 * when a single array's '/'-rows disagree. Since spec 1.0, array elements are
 * also homogeneous (above the lexer, in the materialised DOM): ragged
 * comma-separated sub-arrays now disagree in length and are rejected. The
 * sibling-branch case (same-shaped blocks) still guards the cross-branch
 * row-width leak fixed in bvn_action_array_intro.
 */
static void test_array_row_size_model(void)
{
	/* Ragged comma-separated sub-arrays: heterogeneous, rejected (spec 1.0). */
	bvn_dom_doc_t *ragged = parse_doc(".a = [[1, 2], [3, 4, 5]];\n");
	if (ragged) {
		ASSERT_EQ_UINT(bvn_dom_doc_get_parse_error(ragged),
					   error_array_row_size_mismatch,
					   "ragged comma-separated sub-arrays must be rejected");
		bvn_dom_doc_destroy(ragged);
	}

	/* Two same-shaped 2-D /-blocks side by side: valid (no leak, homogeneous). */
	bvn_dom_doc_t *sib =
		parse_doc(".a = [[1, 2]/[3, 4]]/[[5, 6]/[7, 8]];\n");
	if (sib) {
		ASSERT_EQ_UINT(bvn_dom_doc_get_parse_error(sib), error_none,
					   "a /-row width must not leak across a sibling branch");
		bvn_dom_doc_destroy(sib);
	}

	/* A single array's /-rows disagree: must be rejected. */
	bvn_dom_doc_t *bad = parse_doc(".a = [1, 2, 3]/[4, 5];\n");
	if (bad) {
		ASSERT_EQ_UINT(bvn_dom_doc_get_parse_error(bad),
					   error_array_row_size_mismatch,
					   "mismatched /-rows of one array must error in the DOM");
		bvn_dom_doc_destroy(bad);
	}

	/* A nested /-array whose own rows disagree: must be rejected. */
	bvn_dom_doc_t *bad2 = parse_doc(".a = [[1, 2]/[3, 4, 5]];\n");
	if (bad2) {
		ASSERT_EQ_UINT(bvn_dom_doc_get_parse_error(bad2),
					   error_array_row_size_mismatch,
					   "mismatched rows of a nested /-array must error in the DOM");
		bvn_dom_doc_destroy(bad2);
	}
}
/*
 * Array element homogeneity (spec 1.0), "shape uniform, fields free": every
 * non-null array element shares the same kind; bare scalar arrays and matrices
 * also share dimension and are rectangular; struct elements share keys and field
 * kinds, but a scalar field may carry a different unit/length in each record.
 */
static void expect_parse(const char *src, error_code_t want, const char *msg)
{
	bvn_dom_doc_t *d = parse_doc(src);
	if (d) {
		ASSERT_EQ_UINT(bvn_dom_doc_get_parse_error(d), (unsigned)want, msg);
		bvn_dom_doc_destroy(d);
	}
}
static void test_array_homogeneity(void)
{
	/* Valid: uniform kind/dimension, mixed numeric encodings, sparse holes. */
	expect_parse(".a = [1, 2, 3];\n", error_none, "uniform ints");
	expect_parse(".a = [1, 2.5, 3];\n", error_none, "int+float, same (no) dimension");
	expect_parse(".a = [1, , 3];\n", error_none, "null hole keeps array valid");
	expect_parse(".a = <float:64,m> [1.0, 2.0];\n", error_none, "uniform unit");
	expect_parse(".a = [[1, 2], [3, 4]];\n", error_none, "rectangular matrix");
	expect_parse(".a = <float_dec:64,$USD> [1.0, 2.0];\n", error_none,
				 "uniform currency array");

	/* Invalid: kind mismatch, dimension mismatch, ragged, struct keys. */
	expect_parse(".a = [1, \"two\"];\n", error_array_element_type_mismatch,
				 "number vs string is heterogeneous");
	expect_parse(".a = [1, {.x = 1;}];\n", error_array_element_type_mismatch,
				 "scalar vs struct is heterogeneous");
	expect_parse(".a = [<float:64,k~g> 1.0, <float:64,m> 2.0];\n",
				 error_array_element_type_mismatch, "mass vs length is heterogeneous");
	expect_parse(".a = [[1, 2], [3, 4, 5]];\n", error_array_row_size_mismatch,
				 "ragged sub-arrays rejected");
	expect_parse(".a = [{.x = 1;}, {.y = 1;}];\n", error_struct_shape_mismatch,
				 "differing struct keys rejected");
	expect_parse(".a = [{.x = 1;}, {.x = \"s\";}];\n",
				 error_array_element_type_mismatch, "struct field kind must match");

	/* Fields free: per-record units and list lengths are allowed. */
	expect_parse(".a = [{.bal = <float_dec:64,$USD> 1.0;},"
				 "{.bal = <float_dec:64,$EUR> 2.0;}];\n", error_none,
				 "multi-currency record array is valid (fields free)");
	expect_parse(".a = [{.args = [\"a\", \"b\"];}, {.args = [\"c\"];}];\n",
				 error_none, "per-record list lengths may differ (fields free)");
	expect_parse(".a = [{.x = 1;}, {.x = 2;}, {.x = 3;}];\n", error_none,
				 "same-key struct array is valid");
}
/*
 * Empty arrays: "[]" is a genuinely empty row (0 elements), distinct from
 * "[null]" (1 null) and "[,]" (2 nulls). Width 0 is a real width, so empty
 * "/"-rows and empty sibling sub-arrays follow the same rectangular rule.
 */
static void test_empty_array_semantics(void)
{
	bvn_dom_doc_t *d;
	bvn_dom_node_t *a;

	d = parse_doc(".a = [];\n");
	if (d) {
		ASSERT_EQ_UINT(bvn_dom_doc_get_parse_error(d), error_none, "[] parses");
		a = bvn_dom_lookup(d, ".a");
		ASSERT_EQ_UINT(bvn_dom_array_count(a), 0, "[] has 0 elements");
		bvn_dom_doc_destroy(d);
	}
	d = parse_doc(".a = [null];\n");
	if (d) {
		a = bvn_dom_lookup(d, ".a");
		ASSERT_EQ_UINT(bvn_dom_array_count(a), 1, "[null] has 1 element");
		ASSERT_EQ_UINT(bvn_dom_node_type(bvn_dom_array_at(a, 0)),
					   BVN_DOM_NULL, "[null][0] is a null");
		bvn_dom_doc_destroy(d);
	}
	d = parse_doc(".a = [,];\n");
	if (d) {
		a = bvn_dom_lookup(d, ".a");
		ASSERT_EQ_UINT(bvn_dom_array_count(a), 2, "[,] has 2 null elements");
		bvn_dom_doc_destroy(d);
	}
	d = parse_doc(".a = <uint:32> [];\n");
	if (d) {
		a = bvn_dom_lookup(d, ".a");
		ASSERT_EQ_UINT(bvn_dom_array_count(a), 0, "typed [] has 0 elements");
		bvn_dom_doc_destroy(d);
	}
	/* Empty /-rows: all-empty valid; empty mixed with non-empty is ragged. */
	expect_parse(".a = []/[];\n",     error_none,                  "[]/[] valid");
	expect_parse(".a = []/[]/[];\n",  error_none,                  "[]/[]/[] valid");
	expect_parse(".a = []/[1];\n",    error_array_row_size_mismatch, "[]/[1] ragged");
	expect_parse(".a = [1]/[];\n",    error_array_row_size_mismatch, "[1]/[] ragged");
	/* Empty vs non-empty sibling sub-arrays differ in length: rejected. */
	expect_parse(".a = [[], [1]];\n",     error_array_row_size_mismatch,
				 "[] vs [1] siblings");
	expect_parse(".a = [[], []];\n",      error_none, "[] vs [] siblings valid");
	expect_parse(".a = [[], [], [1]];\n", error_array_row_size_mismatch,
				 "empty,empty,len1 siblings");
}
/*
 * Keys must be unique within one scope (a struct, or the top-level document),
 * so lookup, references and iteration always agree. The same key in *different*
 * scopes is fine. Also guards that an empty array "[]" leaves no phantom
 * top-level entry behind.
 */
static void test_struct_unique_keys(void)
{
	expect_parse(".a = {.x = 1; .x = 2;};\n", error_duplicate_struct_key,
				 "struct duplicate key");
	expect_parse(".a = {.x = 1; .y = 2; .x = 3;};\n",
				 error_duplicate_struct_key, "non-adjacent duplicate key");
	expect_parse(".x = 1;\n.x = 2;\n", error_duplicate_struct_key,
				 "top-level duplicate key");
	expect_parse(".a = {.p = {.x = 1; .x = 2;};};\n",
				 error_duplicate_struct_key, "duplicate in nested struct");
	expect_parse(".a = [{.x = 1; .x = 2;}];\n", error_duplicate_struct_key,
				 "duplicate in array struct element");

	expect_parse(".a = {.x = 1;};\n.b = {.x = 2;};\n", error_none,
				 "same key in sibling structs is fine");
	expect_parse(".a = {.x = 1; .p = {.x = 2;};};\n", error_none,
				 "same key nested vs outer is fine");
	expect_parse(".a = {.x = 1; .y = 2;};\n", error_none,
				 "distinct struct keys are fine");

	/* Empty array must not leave a phantom top-level NULL entry. */
	bvn_dom_doc_t *d = parse_doc(".a = [];\n.b = 2;\n");
	if (d) {
		ASSERT_EQ_UINT(bvn_dom_doc_get_parse_error(d), error_none,
					   "[] then .b parses");
		ASSERT_EQ_UINT(bvn_dom_doc_count(d), 2,
					   "[] leaves no phantom top-level entry");
		bvn_dom_doc_destroy(d);
	}
}
/*
 * References are stored UNRESOLVED — the path string only. The library never
 * dereferences them, so dangling, forward and cyclic references all parse, and
 * lookup navigates literal structure (it does not follow a reference). The path
 * grammar requires "." + id-start segments.
 */
static void test_references(void)
{
	bvn_dom_doc_t *d;
	const char *path; uint32_t pl;

	d = parse_doc(".x = 42;\n.r = &.x;\n");
	if (d) {
		ASSERT_EQ_UINT(bvn_dom_doc_get_parse_error(d), error_none,
					   "reference parses");
		bvn_dom_node_t *r = bvn_dom_lookup(d, ".r");
		ASSERT_EQ_UINT(bvn_dom_node_type(r), BVN_DOM_REFERENCE,
					   ".r is a reference node");
		ASSERT_TRUE(bvn_dom_get_reference(r, &path, &pl) &&
					pl == 2u && memcmp(path, ".x", 2) == 0,
					"reference stores the path '.x', unresolved");
		bvn_dom_doc_destroy(d);
	}
	/* Dangling, cyclic and forward references are all accepted (app-resolved). */
	expect_parse(".r = &.nonexistent;\n", error_none, "dangling reference accepted");
	expect_parse(".a = &.b;\n.b = &.a;\n", error_none, "reference cycle accepted");
	expect_parse(".r = &.x;\n.x = 1;\n",   error_none, "forward reference accepted");

	/* lookup does not follow references (structural navigation only). */
	d = parse_doc(".b = {.c = 1;};\n.a = &.b;\n");
	if (d) {
		ASSERT_NULL(bvn_dom_lookup(d, ".a.c"),
					"lookup does not dereference a reference mid-path");
		bvn_dom_doc_destroy(d);
	}
	/* Path grammar. */
	expect_parse(".r = &;\n",    error_unexpected_input_byte, "'&' alone rejected");
	expect_parse(".r = &.;\n",   error_unexpected_input_byte, "'&.' rejected");
	expect_parse(".r = &..x;\n", error_unexpected_input_byte, "empty segment rejected");
	expect_parse(".a = {.b = {.c = 1;};}; .r = &.a.b.c;\n", error_none,
				 "nested reference path accepted");
	/* References are homogeneous by kind regardless of resolved target type. */
	expect_parse(".i = 1; .s = \"x\"; .arr = [&.i, &.s];\n", error_none,
				 "array of references is homogeneous by kind");
}
static void test_reference_indexing(void)
{
	bvn_dom_doc_t *d;
	int64_t v;

	/* The in-document &.matrix[0][1] literal lexes (1.1) and is stored verbatim. */
	d = parse_doc("#!bovnar 1.1\n.matrix = [10, 20, 30]/[40, 50, 60];\n"
				  ".row0c1 = &.matrix[0][1];\n");
	if (d) {
		ASSERT_EQ_UINT(bvn_dom_doc_get_parse_error(d), error_none,
					   "indexed reference parses in a 1.1 document");
		const char *path; uint32_t pl;
		bvn_dom_node_t *r = bvn_dom_lookup(d, ".row0c1");
		ASSERT_TRUE(bvn_dom_get_reference(r, &path, &pl) &&
					pl == 13u && memcmp(path, ".matrix[0][1]", 13) == 0,
					"indexed reference stores '.matrix[0][1]' unresolved");
		/* resolve the index path at the DOM layer */
		ASSERT_TRUE(bvn_dom_get_i64(bvn_dom_lookup(d, ".matrix[0][1]"), &v) &&
					v == 20, ".matrix[0][1] resolves to 20");
		ASSERT_TRUE(bvn_dom_get_i64(bvn_dom_lookup(d, ".matrix[1][0]"), &v) &&
					v == 40, ".matrix[1][0] resolves to 40");
		ASSERT_NULL(bvn_dom_lookup(d, ".matrix[0]"),
					"partial index of a flat matrix returns NULL");
		ASSERT_NULL(bvn_dom_lookup(d, ".matrix[0][9]"),
					"out-of-range column returns NULL");
		ASSERT_NULL(bvn_dom_lookup(d, ".matrix[2][0]"),
					"out-of-range row returns NULL");
		bvn_dom_doc_destroy(d);
	}
	/* 1-D array: a single index; nested arrays descend one index per level. */
	d = parse_doc("#!bovnar 1.1\n.a = [1, 2, 3];\n.n = [[1, 2], [3, 4]];\n");
	if (d) {
		ASSERT_TRUE(bvn_dom_get_i64(bvn_dom_lookup(d, ".a[2]"), &v) && v == 3,
					".a[2] resolves to 3");
		ASSERT_TRUE(bvn_dom_get_i64(bvn_dom_lookup(d, ".n[0][1]"), &v) && v == 2,
					".n[0][1] resolves to 2 (nested)");
		ASSERT_NULL(bvn_dom_lookup(d, ".a[3]"), "1-D out-of-range returns NULL");
		bvn_dom_doc_destroy(d);
	}
	/* index then struct field: .rows[1].name */
	d = parse_doc("#!bovnar 1.1\n.rows = [{.name = \"a\";}, {.name = \"b\";}];\n");
	if (d) {
		bvn_dom_node_t *nm = bvn_dom_lookup(d, ".rows[1].name");
		const char *s; uint32_t sl;
		ASSERT_TRUE(bvn_dom_get_string(nm, &s, &sl) &&
					sl == 1u && s[0] == 'b',
					".rows[1].name resolves to \"b\"");
		bvn_dom_doc_destroy(d);
	}
	/* indexing a non-array, and a malformed index, both fail to resolve. */
	d = parse_doc("#!bovnar 1.1\n.x = 7;\n");
	if (d) {
		ASSERT_NULL(bvn_dom_lookup(d, ".x[0]"), "indexing a scalar returns NULL");
		ASSERT_NULL(bvn_dom_lookup(d, ".x[a]"), "non-numeric index returns NULL");
		bvn_dom_doc_destroy(d);
	}
}
/*
 * datetime values must decode through the DOM as plain decimal signed integers
 * regardless of epoch — a regression guard for the bug where the epoch index
 * stored in value_type.base was misread as a numeric base (so <datetime:gps>
 * 1010 decoded as base-2 "1010" = 10).
 */
static void test_datetime_dom_decode(void)
{
	static const struct { const char *epoch; int idx; int32_t mjd; } eps[] = {
		{"unix",0,40587}, {"tai",1,36204}, {"gps",2,44244}, {"mjd",3,0},
		{"ntp",4,15020}, {"galileo",5,51412}, {"glonass",6,50083},
		{"y2000",7,51544}, {"beidou",8,53736},
	};
	for (size_t i = 0; i < sizeof(eps)/sizeof(eps[0]); i++) {
		char src[96];
		snprintf(src, sizeof(src),
			"#!bovnar 1.1\n.t = <datetime:%s> 1750000000;\n", eps[i].epoch);
		bvn_dom_doc_t *d = parse_doc(src);
		if (!d) continue;
		int64_t v = 0;
		ASSERT_TRUE(bvn_dom_get_i64(bvn_dom_lookup(d, ".t"), &v) &&
					v == 1750000000,
					"datetime epoch-seconds decode as decimal regardless of epoch");
		value_type_spec_t vt = bvn_dom_get_value_type(bvn_dom_lookup(d, ".t"));
		ASSERT_TRUE(strcmp(bvnr_datetime_epoch_name(vt), eps[i].epoch) == 0,
					"epoch name round-trips through the spec");
		ASSERT_EQ_INT(bvnr_datetime_epoch_mjd(vt), eps[i].mjd,
					"epoch MJD matches bvn_datetime.h");
		bvn_dom_doc_destroy(d);
	}
	/* datetime is its own kind for array homogeneity, and epoch is a dimension */
	{
		bvn_dom_doc_t *ok2 = parse_doc(
			"#!bovnar 1.1\n.a = [<datetime:64,tai> 1, <datetime:64,tai> 2];\n");
		if (ok2) {
			ASSERT_EQ_UINT(bvn_dom_doc_get_parse_error(ok2), error_none,
				"homogeneous same-epoch datetime array is accepted");
			bvn_dom_doc_destroy(ok2);
		}
		bvn_dom_doc_t *mix = parse_doc(
			"#!bovnar 1.1\n.a = [<datetime:64,unix> 1, <sint:64> 2];\n");
		if (mix) {
			ASSERT_EQ_UINT(bvn_dom_doc_get_parse_error(mix),
				error_array_element_type_mismatch,
				"datetime does not mix with a plain number");
			bvn_dom_doc_destroy(mix);
		}
		bvn_dom_doc_t *ep = parse_doc(
			"#!bovnar 1.1\n.a = [<datetime:64,unix> 1, <datetime:64,tai> 2];\n");
		if (ep) {
			ASSERT_EQ_UINT(bvn_dom_doc_get_parse_error(ep),
				error_array_element_type_mismatch,
				"datetimes on different epochs do not mix");
			bvn_dom_doc_destroy(ep);
		}
	}
	/* negative (pre-epoch) instant decodes as a signed value */
	bvn_dom_doc_t *neg = parse_doc("#!bovnar 1.1\n.t = <datetime> -100;\n");
	if (neg) {
		int64_t v = 0;
		ASSERT_TRUE(bvn_dom_get_i64(bvn_dom_lookup(neg, ".t"), &v) && v == -100,
					"negative datetime decodes as -100");
		bvn_dom_doc_destroy(neg);
	}
	/* spec 1.1 — an ISO literal's fractional seconds are visible to DOM
	 * consumers as a verbatim string, while the carrier stays whole seconds. */
	bvn_dom_doc_t *fr = parse_doc(
		"#!bovnar 1.1\n.t = 2026-06-15T12:00:00.000000123Z;\n");
	if (fr) {
		bvn_dom_node_t *n = bvn_dom_lookup(fr, ".t");
		int64_t v = 0;
		ASSERT_TRUE(bvn_dom_get_i64(n, &v) && v == 1781524800,
					"fractional literal carrier is the whole second");
		uint32_t flen = 0;
		const char *f = bvn_dom_get_datetime_fraction(n, &flen);
		ASSERT_NOT_NULL(f, "DOM exposes the datetime fraction string");
		if (f) {
			ASSERT_EQ_STR(f, "000000123", "fraction digits stored verbatim");
			ASSERT_EQ_UINT(flen, 9u, "fraction length is the digit count");
		}
		bvn_dom_doc_destroy(fr);
	}
	/* a datetime given as an integer carrier (or no fraction) exposes none */
	bvn_dom_doc_t *nofr = parse_doc("#!bovnar 1.1\n.t = <datetime:64> 42;\n");
	if (nofr) {
		uint32_t flen = 7;
		const char *f = bvn_dom_get_datetime_fraction(
			bvn_dom_lookup(nofr, ".t"), &flen);
		ASSERT_TRUE(f == NULL && flen == 0u,
					"integer-carrier datetime has no fraction");
		bvn_dom_doc_destroy(nofr);
	}
}
/*
 * Broad coverage of index resolution across every array feature: deep /-row
 * matrices, slash-rows whose cells are sub-arrays or structs, arrays nested in
 * structs and vice versa, arbitrary nesting depth, null elements, and chained
 * index/field navigation. Mirrors the manually-verified matrix of cases.
 */
static void test_reference_indexing_coverage(void)
{
	bvn_dom_doc_t *d;
	int64_t v;

	/* 3-row /-matrix: addressed [row][col] regardless of row count. */
	d = parse_doc("#!bovnar 1.1\n.m = [10,20]/[30,40]/[50,60];\n");
	if (d) {
		ASSERT_TRUE(bvn_dom_get_i64(bvn_dom_lookup(d, ".m[2][1]"), &v) && v == 60,
					"3-row matrix .m[2][1] resolves to 60");
		ASSERT_TRUE(bvn_dom_get_i64(bvn_dom_lookup(d, ".m[0][0]"), &v) && v == 10,
					"3-row matrix .m[0][0] resolves to 10");
		ASSERT_NULL(bvn_dom_lookup(d, ".m[3][0]"), "row past the last is NULL");
		bvn_dom_doc_destroy(d);
	}
	/* /-rows whose cells are themselves arrays: descend [row][col] then [k]. */
	d = parse_doc("#!bovnar 1.1\n.m = [[1,2],[3,4]]/[[5,6],[7,8]];\n");
	if (d) {
		ASSERT_TRUE(bvn_dom_get_i64(bvn_dom_lookup(d, ".m[1][0][1]"), &v) && v == 6,
					".m[1][0][1] resolves to 6");
		ASSERT_TRUE(bvn_dom_get_i64(bvn_dom_lookup(d, ".m[0][1][0]"), &v) && v == 3,
					".m[0][1][0] resolves to 3");
		bvn_dom_doc_destroy(d);
	}
	/* struct in a flat array; /-matrix of structs; array inside a struct. */
	d = parse_doc("#!bovnar 1.1\n.r = [{.a=1;},{.a=2;}];\n"
				  ".g = [{.a=1;},{.a=2;}]/[{.a=3;},{.a=4;}];\n"
				  ".s = {.arr=[7,8,9];};\n");
	if (d) {
		ASSERT_TRUE(bvn_dom_get_i64(bvn_dom_lookup(d, ".r[1].a"), &v) && v == 2,
					"struct-in-array .r[1].a resolves to 2");
		ASSERT_TRUE(bvn_dom_get_i64(bvn_dom_lookup(d, ".g[1][0].a"), &v) && v == 3,
					"/-matrix of structs .g[1][0].a resolves to 3");
		ASSERT_TRUE(bvn_dom_get_i64(bvn_dom_lookup(d, ".s.arr[2]"), &v) && v == 9,
					"array-in-struct .s.arr[2] resolves to 9");
		bvn_dom_doc_destroy(d);
	}
	/* array of arrays of structs; index -> field -> index chain; 3-deep nest. */
	d = parse_doc("#!bovnar 1.1\n.z = [[{.x=1;}]];\n"
				  ".g = [{.rows=[7,8];}];\n.d = [[[1,2]]];\n");
	if (d) {
		ASSERT_TRUE(bvn_dom_get_i64(bvn_dom_lookup(d, ".z[0][0].x"), &v) && v == 1,
					"array-of-arrays-of-structs .z[0][0].x resolves to 1");
		ASSERT_TRUE(bvn_dom_get_i64(bvn_dom_lookup(d, ".g[0].rows[1]"), &v) && v == 8,
					"index->field->index .g[0].rows[1] resolves to 8");
		ASSERT_TRUE(bvn_dom_get_i64(bvn_dom_lookup(d, ".d[0][0][1]"), &v) && v == 2,
					"3-deep nested .d[0][0][1] resolves to 2");
		bvn_dom_doc_destroy(d);
	}
	/* null array element resolves to a null node (not a missing path). */
	d = parse_doc("#!bovnar 1.1\n.n = [1,,3];\n");
	if (d) {
		bvn_dom_node_t *e = bvn_dom_lookup(d, ".n[1]");
		ASSERT_NOT_NULL(e, ".n[1] (a null element) is a resolvable node");
		ASSERT_TRUE(bvn_dom_is_null(e), ".n[1] is the null placeholder");
		ASSERT_TRUE(bvn_dom_get_i64(bvn_dom_lookup(d, ".n[2]"), &v) && v == 3,
					".n[2] resolves to 3");
		bvn_dom_doc_destroy(d);
	}
	/* a comma-nested row IS addressable as a sub-array; a /-matrix row is not. */
	d = parse_doc("#!bovnar 1.1\n.c = [[1,2],[3,4]];\n.m = [1,2]/[3,4];\n");
	if (d) {
		bvn_dom_node_t *row = bvn_dom_lookup(d, ".c[0]");
		ASSERT_NOT_NULL(row, "comma-nested row .c[0] resolves");
		ASSERT_EQ_UINT(bvn_dom_node_type(row), BVN_DOM_ARRAY,
					   ".c[0] is itself an array");
		ASSERT_EQ_UINT(bvn_dom_array_count(row), 2,
					   ".c[0] has two elements");
		ASSERT_NULL(bvn_dom_lookup(d, ".m[0]"),
					"a /-matrix row is not addressable as a sub-array");
		bvn_dom_doc_destroy(d);
	}
}
/*
 * Regression for two wrong-acceptance bugs:
 *  - A '/'-row matrix is stored flat with its geometry in num_dims +
 *    rows_per_dim[], so two sibling matrices with the SAME total cell count but
 *    a DIFFERENT shape (2x3 vs 3x2, flat-4 vs 2x2) must still be rejected;
 *    bvn_dom_shape_equal once compared only the flattened cell count.
 *  - A datetime carrier takes no unit in ANY form. The ISO-literal form was
 *    rejected, but the plain integer carrier (<datetime:64> 100 m) was accepted
 *    and the unit then silently dropped on emit.
 */
static void test_shape_count_and_datetime_unit_regression(void)
{
	/* equal total cell count, different shape -> reject */
	expect_parse(".a = [[1,2,3]/[4,5,6], [7,8]/[9,10]/[11,12]];\n",
				 error_array_row_size_mismatch, "2x3 vs 3x2 siblings (6 cells each)");
	expect_parse(".a = [[1,2,3,4]/[5,6,7,8], [1,2]/[3,4]/[5,6]/[7,8]];\n",
				 error_array_row_size_mismatch, "2x4 vs 4x2 siblings (8 cells each)");
	expect_parse(".a = [[1,2,3,4], [1,2]/[3,4]];\n",
				 error_array_row_size_mismatch, "flat-4 vs 2x2 (4 cells each)");
	/* genuinely same shape -> valid */
	expect_parse(".a = [[1,2]/[3,4], [5,6]/[7,8]];\n", error_none,
				 "2x2 vs 2x2 siblings valid");
	expect_parse(".a = [[1,2,3]/[4,5,6], [7,8,9]/[10,11,12]];\n", error_none,
				 "2x3 vs 2x3 siblings valid");

	/* a datetime carrier takes no unit, in either carrier form */
	expect_parse("#!bovnar 1.1\n.t = <datetime:64> 100 m;\n",
				 error_unit_illegal, "datetime integer carrier + inline unit");
	expect_parse("#!bovnar 1.1\n.t = <datetime:64,unix> 100 k~m;\n",
				 error_unit_illegal, "datetime integer carrier + compound unit");
	expect_parse("#!bovnar 1.1\n.t = <datetime:64> 100;\n", error_none,
				 "datetime integer carrier without unit valid");
}

static void test_dom_accessors_fail_rather_than_lie(void)
{
	/* bovnar_dom.h promises these "return false (leaving *out UNCHANGED -- no
	 * clamping or truncation)" when a value does not fit. They used to hand back
	 * a reinterpreted bit pattern with a true return instead. */
	const char *doc =
		".u64max = <uint:64> 18446744073709551615;\n"
		".u64mid = <uint:64> 9223372036854775808;\n"
		".sneg   = <sint:64> -315619200;\n"
		".wide   = <uint:128,k~m> 100000000000000000000;\n";
	bvn_dom_doc_t *d = bvn_dom_parse(doc, (uint32_t)strlen(doc));
	ASSERT_TRUE(d && bvn_dom_doc_get_parse_error(d) == error_none, "doc parses");
	if (!d) return;

	int64_t i64 = 7; int8_t i8 = 7; uint64_t u64 = 7; double f = 0;
	const bvn_dom_node_t *n = bvn_dom_lookup(d, ".u64max");
	ASSERT_TRUE(!bvn_dom_get_i64(n, &i64),
		    "a uint64 above INT64_MAX does not fit an int64");
	ASSERT_EQ_INT(i64, 7, "...and *out is left untouched");
	ASSERT_TRUE(!bvn_dom_get_i8(n, &i8), "nor an int8");
	ASSERT_TRUE(bvn_dom_get_u64(n, &u64) && u64 == UINT64_MAX,
		    "but it reads correctly as unsigned");
	ASSERT_TRUE(bvn_dom_get_float(n, &f) && f > 1.8e19,
		    "and as a positive double, not a negative one");

	n = bvn_dom_lookup(d, ".u64mid");
	ASSERT_TRUE(!bvn_dom_get_i64(n, &i64), "INT64_MAX+1 does not fit an int64");

	/* A negative signed carrier must not read as a huge unsigned one. */
	n = bvn_dom_lookup(d, ".sneg");
	ASSERT_TRUE(!bvn_dom_get_u64(n, &u64), "a negative value is not unsigned");
	ASSERT_TRUE(bvn_dom_get_i64(n, &i64) && i64 == -315619200,
		    "...but reads correctly as signed");

	/* A bignum past int64 is still a fine double, and base units must not
	 * collapse it to 0.0 -- the same value that means "no SI mapping". */
	n = bvn_dom_lookup(d, ".wide");
	ASSERT_TRUE(bvn_dom_get_float(n, &f) && f > 9.9e19 && f < 1.01e20,
		    "a uint128 beyond int64 reads as a double");
	double si = bvn_dom_get_value_in_base_units(n);
	ASSERT_TRUE(si > 9.9e22 && si < 1.01e23,
		    "1e20 km is 1e23 m, not 0");
	bvn_dom_doc_destroy(d);
}

static void test_dom_string_carried_numbers_are_numbers(void)
{
	/* Spec 6.1: uint/sint/float accept "Number OR string". The quoted form is
	 * the only way to write a non-decimal base whose digits are letters, and the
	 * canonical way to write a wide integer -- examples/integers.bvnr ships
	 * twelve. Dispatching on the token alone made them all BVN_DOM_STRING
	 * carrying a numeric value_type, so the value was unreachable. */
	const char *doc =
		".hex  = <uint:32,_16> \"CAFEBABE\";\n"
		".wide = <uint:256> \"115792089237316195423570985008687907853269984665640564039457584007913129639935\";\n"
		".text = <utf8> \"CAFEBABE\";\n";
	bvn_dom_doc_t *d = bvn_dom_parse(doc, (uint32_t)strlen(doc));
	ASSERT_TRUE(d && bvn_dom_doc_get_parse_error(d) == error_none, "doc parses");
	if (!d) return;

	const bvn_dom_node_t *n = bvn_dom_lookup(d, ".hex");
	uint64_t u = 0;
	ASSERT_EQ_INT((int)bvn_dom_node_type(n), (int)BVN_DOM_INT,
		      "a hex string carrier is an INT node");
	ASSERT_TRUE(bvn_dom_get_u64(n, &u) && u == 3405691582ull,
		    "...and its value is reachable");

	n = bvn_dom_lookup(d, ".wide");
	ASSERT_EQ_INT((int)bvn_dom_node_type(n), (int)BVN_DOM_INT,
		      "a wide string carrier is an INT node");
	ASSERT_TRUE(bvn_dom_get_bigint(n) != NULL, "...with a reachable bigint");
	char *txt = bvn_dom_int_to_str(n, 10);
	ASSERT_TRUE(txt && strcmp(txt,
		"115792089237316195423570985008687907853269984665640564039457584007913129639935") == 0,
		"...that round-trips to its digits");
	if (txt) bvn_dom_free_string(txt);

	/* A genuine string is still a string. */
	n = bvn_dom_lookup(d, ".text");
	ASSERT_EQ_INT((int)bvn_dom_node_type(n), (int)BVN_DOM_STRING,
		      "a utf8 value is still a STRING node");
	bvn_dom_doc_destroy(d);
}

static void test_dom_dimension_match_is_order_insensitive(void)
{
	/* Unit multiplication commutes, so "$USD*$EUR" and "$EUR*$USD" are the same
	 * unit. The currency fallback compared components positionally and rejected
	 * such an array as heterogeneous -- disagreeing with bvn_unit_equal, which
	 * the rest of the library uses. */
	static const struct { const char *doc; bool want_ok; } cases[] = {
		{ "#!bovnar 1.1\n.p = [<float:64,$USD*$EUR> 1.0, <float:64,$EUR*$USD> 2.0];\n", true },
		{ "#!bovnar 1.1\n.p = [<float:64,$USD/m/s> 1.0, <float:64,$USD/s/m> 2.0];\n", true },
		{ "#!bovnar 1.1\n.p = [<float:64,m*s> 1.0, <float:64,s*m> 2.0];\n", true },
		/* genuinely different currencies must still be rejected */
		{ "#!bovnar 1.1\n.p = [<float:64,$USD> 1.0, <float:64,$EUR> 2.0];\n", false },
	};
	for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
		bvn_dom_doc_t *d = bvn_dom_parse(cases[i].doc,
						 (uint32_t)strlen(cases[i].doc));
		bool ok = d && bvn_dom_doc_get_parse_error(d) == error_none;
		ASSERT_EQ_INT((int)ok, (int)cases[i].want_ok,
			      "unit order does not decide array homogeneity");
		if (d) bvn_dom_doc_destroy(d);
	}
}

/*
 * bvn_dom_node_from_bigint: a unit-carrying constructor with an ASYMMETRIC
 * ownership contract and, until this, no caller anywhere in the tree.
 *
 * The header states it plainly -- on success the node takes the bvn_int_t, on
 * failure the caller still owns it -- which is exactly the shape that leaks or
 * double-frees when nobody checks. It is also the only DOM entry point that
 * takes a value_unit_t, so a unit lost here is a unit lost on the whole
 * arbitrary-precision path.
 *
 * Both halves are pinned: the unit survives construction, and every refusal
 * leaves the integer for the caller to free (which it does below, so a leak
 * checker has something to say if that ever stops being true).
 */
static void test_dom_bigint_node_carries_its_unit(void)
{
	printf("  test_dom_bigint_node_carries_its_unit...\n");

	bool uok = true;
	value_unit_t usd = bvn_parse_unit((const uint8_t *)"$USD", &uok);
	ASSERT_TRUE(uok, "the test's own unit must parse");

	/* Success: wider than any machine integer, which is the point. */
	{
		bvn_int_t *big = bvn_int_alloc();
		ASSERT_TRUE(big != NULL, "bvn_int_alloc");
		if (!big) return;
		ASSERT_TRUE(bvn_int_from_str(
				big, "170141183460469231731687303715884105727", 10),
			    "a 127-bit value parses");
		value_type_spec_t vt = { .family = vt_uint, .width = 128, .base = 10 };
		bvn_dom_node_t *n = bvn_dom_node_from_bigint(big, vt, usd);
		ASSERT_TRUE(n != NULL, "a 128-bit integer node is built");
		if (n) {
			ASSERT_TRUE(bvn_unit_equal(bvn_dom_get_unit(n), usd),
				    "the node carries the unit it was given");
			char ub[64];
			int32_t k = bvn_dom_get_unit_string(n, ub, sizeof ub);
			ASSERT_TRUE(k == 4 && strcmp(ub, "$USD") == 0,
				    "and spells it back with the sigil");
			/* Takes ownership: this must free the bigint too. */
			bvn_dom_node_destroy(n);
		} else {
			bvn_int_free(big);
		}
	}

	/* Every refusal, with ONE integer that the caller frees at the end. If a
	 * refusal ever started consuming it, this is a double free rather than a
	 * silent change of contract. */
	{
		bvn_int_t *big = bvn_int_alloc();
		ASSERT_TRUE(big != NULL, "bvn_int_alloc");
		if (!big) return;
		ASSERT_TRUE(bvn_int_from_str(big, "5", 10), "a small value parses");

		value_type_spec_t narrow = { .family = vt_uint,  .width = 64,  .base = 10 };
		value_type_spec_t notint = { .family = vt_float, .width = 128, .base = 10 };
		ASSERT_TRUE(bvn_dom_node_from_bigint(big, narrow, usd) == NULL,
			    "a width a machine integer can hold is refused");
		ASSERT_TRUE(bvn_dom_node_from_bigint(big, notint, usd) == NULL,
			    "a non-integer family is refused");
		ASSERT_TRUE(bvn_dom_node_from_bigint(NULL, notint, usd) == NULL,
			    "a NULL integer is refused");
		bvn_int_free(big);
	}
}

int main(void)
{
	test_dom_accessors_fail_rather_than_lie();
	test_dom_string_carried_numbers_are_numbers();
	test_dom_dimension_match_is_order_insensitive();
	test_dom_bigint_node_carries_its_unit();
	printf("Running bovnar_dom_test regression suite...\n");

	test_parse_basic_dom();
	test_null_values_and_arrays();
	test_bom_and_parse_errors();
	test_lookup_edge_cases();
	test_getter_semantics();
	test_int_to_str_uint64_highbit();
	test_nested_array_elements();
	test_array_row_size_model();
	test_array_homogeneity();
	test_empty_array_semantics();
	test_struct_unique_keys();
	test_references();
	test_datetime_dom_decode();
	test_reference_indexing();
	test_reference_indexing_coverage();
	test_shape_count_and_datetime_unit_regression();

	if (failures == 0) {
		printf("PASSED %d tests\n", tests);
		return 0;
	}

	fprintf(stderr, "FAILED %d of %d tests\n", failures, tests);
	return 1;
}
