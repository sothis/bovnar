#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bovnar.h"
#include "bvn_internal_dims.h"
#include "bovnar_dom.h"

static void assert_valid_error(error_code_t e)
{
	if ((unsigned)e >= (unsigned)BVN_ERROR_COUNT)
		__builtin_trap();
}

static void assert_valid_dom_type(bvn_dom_type_t t)
{

	if ((unsigned)t > (unsigned)BVN_DOM_OCTET_STREAM)
		__builtin_trap();
}

static void assert_valid_type_family(value_type_family_t f)
{
	if ((unsigned)f >= (unsigned)vt_illegal + 1u)
		__builtin_trap();
}

#define MAX_WALK_DEPTH 64

static void walk_node(const bvn_dom_node_t *node, unsigned depth);

static void exercise_node_getters(const bvn_dom_node_t *node)
{
	if (!node) return;

	bvn_dom_type_t  dtype = bvn_dom_node_type(node);
	assert_valid_dom_type(dtype);

	bool is_null = bvn_dom_is_null(node);
	(void)is_null;

	value_type_spec_t vts = bvn_dom_get_value_type(node);
	assert_valid_type_family(vts.family);

	value_unit_t vu = bvn_dom_get_unit(node);

	if (vu.num_components > BVNR_MAX_UNIT_COMPONENTS + 1u)
		__builtin_trap();

	char ubuf[256];
	(void)bvn_dom_get_unit_string(node, ubuf, sizeof(ubuf));

	double base_val = bvn_dom_get_value_in_base_units(node);
	(void)base_val;

	{
		int64_t  iv = 0;
		bool ok = bvn_dom_get_i64(node, &iv);
		(void)ok; (void)iv;
	}
	{
		uint64_t uv = 0;
		bool ok = bvn_dom_get_u64(node, &uv);
		(void)ok; (void)uv;
	}
	{
		double fv = 0.0;
		bool ok = bvn_dom_get_float(node, &fv);
		(void)ok; (void)fv;
	}
	{
		const char *s = NULL; uint32_t slen = 0;
		bool ok = bvn_dom_get_string(node, &s, &slen);
		if (ok && s) {  (void)s[0]; if (slen) (void)s[slen-1]; }
		(void)ok;
	}
	{
		const char *s = NULL; uint32_t slen = 0;
		bool ok = bvn_dom_get_symbol(node, &s, &slen);
		if (ok && s) { (void)s[0]; if (slen) (void)s[slen-1]; }
		(void)ok;
	}
	{
		const char *s = NULL; uint32_t slen = 0;
		bool ok = bvn_dom_get_reference(node, &s, &slen);
		if (ok && s) { (void)s[0]; if (slen) (void)s[slen-1]; }
		(void)ok;
	}
	{
		const uint8_t *b = NULL; uint32_t blen = 0;
		bool ok = bvn_dom_get_octets(node, &b, &blen);
		if (ok && b && blen) { (void)b[0]; (void)b[blen-1]; }
		(void)ok;
	}

	uint32_t acnt  = bvn_dom_array_count(node);
	uint32_t adims = bvn_dom_array_dims(node);
	(void)adims;

	if (acnt > 0) {
		(void)bvn_dom_array_at(node, 0);
		(void)bvn_dom_array_at(node, acnt - 1);
	}

	(void)bvn_dom_array_at(node, acnt);
	(void)bvn_dom_array_at(node, UINT32_MAX);

	uint32_t scnt = bvn_dom_struct_count(node);
	(void)scnt;
	(void)bvn_dom_struct_entries(node);
}

static void walk_node(const bvn_dom_node_t *node, unsigned depth)
{
	if (!node || depth >= MAX_WALK_DEPTH)
		return;

	exercise_node_getters(node);

	bvn_dom_type_t dtype = bvn_dom_node_type(node);

	if (dtype == BVN_DOM_STRUCT) {
		uint32_t cnt = bvn_dom_struct_count(node);
		const bvn_dom_entry_t *entries = bvn_dom_struct_entries(node);
		for (uint32_t i = 0; i < cnt; i++) {

			const char *k = entries[i].key;
			if (k) (void)strlen(k);
			walk_node(entries[i].value, depth + 1);

			if (k)
				(void)bvn_dom_struct_get(node, k);
		}

		(void)bvn_dom_struct_get(node, "");
		(void)bvn_dom_struct_get(node, "\x00");
		(void)bvn_dom_struct_get(node, "___no_such_key___");

	} else if (dtype == BVN_DOM_ARRAY) {
		uint32_t cnt = bvn_dom_array_count(node);
		uint32_t limit = cnt < 64u ? cnt : 64u;
		for (uint32_t i = 0; i < limit; i++) {
			bvn_dom_node_t *elem = bvn_dom_array_at(node, i);
			walk_node(elem, depth + 1);
		}
	}
}

static void exercise_lookups(const bvn_dom_doc_t *doc)
{
	uint32_t cnt = bvn_dom_doc_count(doc);
	const bvn_dom_entry_t *entries = bvn_dom_doc_entries(doc);

	for (uint32_t i = 0; i < cnt && i < 32u; i++) {
		const char *k = entries[i].key;
		if (!k) continue;

		char path[512];
		int r = snprintf(path, sizeof(path), ".%s", k);
		if (r < 0 || (size_t)r >= sizeof(path)) continue;

		bvn_dom_node_t *found = bvn_dom_lookup(doc, path);

		if (found) {
			assert_valid_dom_type(bvn_dom_node_type(found));
		}

		(void)bvn_dom_lookup(doc, "");
		(void)bvn_dom_lookup(doc, ".");
		(void)bvn_dom_lookup(doc, "..");
		(void)bvn_dom_lookup(doc, "no_dot");
	}
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	if (size == 0)
		return 0;

	bvn_dom_doc_t *doc = bvn_dom_parse(data, (uint32_t)size);
	if (!doc)
		return 0;

	assert_valid_error(bvn_dom_doc_get_parse_error(doc));

	uint32_t cnt = bvn_dom_doc_count(doc);
	const bvn_dom_entry_t *entries = bvn_dom_doc_entries(doc);

	for (uint32_t i = 0; i < cnt; i++) {
		if (entries[i].key) (void)strlen(entries[i].key);
		walk_node(entries[i].value, 0);
	}

	exercise_lookups(doc);

	bvn_dom_doc_destroy(doc);
	return 0;
}

#ifdef __AFL_FUZZ_TESTCASE_LEN

__AFL_FUZZ_INIT();

int main(void)
{
	__AFL_INIT();
	unsigned char *buf = __AFL_FUZZ_TESTCASE_BUF;
	while (__AFL_LOOP(100000)) {
		size_t len = __AFL_FUZZ_TESTCASE_LEN;
		LLVMFuzzerTestOneInput(buf, len);
	}
	return 0;
}

#elif defined(FUZZ_STANDALONE)

static int run_file(const char *path)
{
	FILE *f = fopen(path, "rb");
	if (!f) { perror(path); return 1; }

	fseek(f, 0, SEEK_END);
	long sz = ftell(f);
	rewind(f);
	if (sz <= 0) { fclose(f); return 0; }

	uint8_t *buf = malloc((size_t)sz);
	if (!buf) { fclose(f); return 1; }

	if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
		free(buf); fclose(f); return 1;
	}
	fclose(f);
	LLVMFuzzerTestOneInput(buf, (size_t)sz);
	free(buf);
	return 0;
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "Usage: %s <corpus_file> [...]\n", argv[0]);
		return 1;
	}
	int ret = 0;
	for (int i = 1; i < argc; i++)
		ret |= run_file(argv[i]);
	return ret;
}

#endif
