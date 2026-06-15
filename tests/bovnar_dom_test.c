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
		char ubuf[64] = {0};
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
int main(void)
{
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
	test_reference_indexing();

	if (failures == 0) {
		printf("PASSED %d tests\n", tests);
		return 0;
	}

	fprintf(stderr, "FAILED %d of %d tests\n", failures, tests);
	return 1;
}
