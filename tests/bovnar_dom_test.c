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

int main(void)
{
	printf("Running bovnar_dom_test regression suite...\n");

	test_parse_basic_dom();
	test_null_values_and_arrays();
	test_bom_and_parse_errors();
	test_lookup_edge_cases();
	test_getter_semantics();

	if (failures == 0) {
		printf("PASSED %d tests\n", tests);
		return 0;
	}

	fprintf(stderr, "FAILED %d of %d tests\n", failures, tests);
	return 1;
}
