/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2026 Janos Sonntag
 *
 * Native test of wasm/bvnr_wasm.c: the JSON the WebAssembly exports return, on
 * the paths a browser cannot be made to take.
 *
 * bvnr_wasm.c compiles without emscripten (WASM_EXPORT is empty then), so it is
 * built here against the ordinary library with its allocator replaced by one
 * that can be told to fail. That is the only way to see what the module says
 * when it runs out of memory: in a real WebAssembly heap the failure cannot be
 * provoked at a chosen call, and the shapes below were wrong for exactly as
 * long as nobody could look at them.
 *
 *   - exhaustion returned {"ok":false,"error":"out_of_memory"}: `error` a
 *     string where every other result has a number, and no error_name;
 *   - exhaustion WHILE COLLECTING EVENTS pasted that object into the events
 *     array of a result that still said ok:true.
 *
 * Every payload produced here -- at every buffer growth the module makes, failed
 * in turn -- must be either complete, or an object with ok:false, a numeric
 * `error` and a string `error_name`; and never an "ok":true around an
 * "ok":false.
 *
 * The same harness asserts the result shapes the node tests also see, so a
 * build without emscripten still checks them.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bovnar.h"
#include "bovnar_dom.h"

/* Fail the (fail_at)-th GROWTH made by bvnr_wasm.c itself, and every one after
 * it; 0 never fails. Only that file's calls are counted -- the library's own
 * allocations go to the real allocator -- and only realloc, which is how every
 * buffer in it grows: the shape of a real exhaustion is a large buffer that can
 * no longer be extended while a hundred bytes for the error object can still be
 * had. (When not even those can, the export returns NULL and the JS wrapper
 * throws; there is no payload to have a shape.) */
static long alloc_calls = 0, fail_at = 0;
static void *test_realloc(void *p, size_t n)
{
	if (fail_at && ++alloc_calls >= fail_at) return NULL;
	return realloc(p, n);
}
static void *test_malloc(void *unused, size_t n)
{
	(void)unused;
	return malloc(n);
}
#define realloc(p, n) test_realloc((p), (n))
#define malloc(n)     test_malloc(NULL, (n))
#include "../bvnr_wasm.c"
#undef realloc
#undef malloc

static int failures = 0, checks = 0;
#define CHECK(cond, ...) do {                                   \
	checks++;                                               \
	if (!(cond)) {                                          \
		failures++;                                     \
		fprintf(stderr, "FAIL line %d: ", __LINE__);    \
		fprintf(stderr, __VA_ARGS__);                   \
		fputc('\n', stderr);                            \
	}                                                       \
} while (0)

static bool has(const char *json, const char *needle)
{
	return json && strstr(json, needle) != NULL;
}

/* A result that reports failure reports it in the common shape. */
static void check_shape(const char *what, long at, const char *json)
{
	if (!json) return;      /* nothing could be allocated at all: JS throws */
	if (has(json, "\"ok\":false")) {
		CHECK(has(json, "\"error\":") && !has(json, "\"error\":\""),
		      "%s (alloc %ld): `error` is not a number: %.120s", what, at, json);
		CHECK(has(json, "\"error_name\":\""),
		      "%s (alloc %ld): no error_name: %.120s", what, at, json);
	}
	CHECK(!(has(json, "\"ok\":true") && has(json, "\"ok\":false")),
	      "%s (alloc %ld): ok:true around ok:false: %.160s", what, at, json);
	size_t n = strlen(json);
	CHECK(n >= 2 && json[0] == '{' && json[n - 1] == '}',
	      "%s (alloc %ld): not one JSON object: %.80s", what, at, json);
}

typedef char *(*entry_fn)(const char *, int);
static char *call_convert(const char *buf, int len)
{
	return bvnr_wasm_events_convert(buf, len, "m", 10, 0);
}

static void test_out_of_memory_shapes(void)
{
	/* long enough that every growable buffer reallocates several times */
	static char doc[6000];
	size_t n = 0;
	for (int i = 0; i < 60 && n + 80 < sizeof doc; i++)
		n += (size_t)snprintf(doc + n, sizeof doc - n,
				      ".key%d = <float:64,k~m> %d.5;\n", i, i);
	static const struct { const char *name; entry_fn fn; } entries[] = {
		{ "validate", bvnr_wasm_validate }, { "errors", bvnr_wasm_errors },
		{ "toJSON", bvnr_wasm_to_json },    { "events", bvnr_wasm_events },
		{ "parse", bvnr_wasm_parse },       { "doc", bvnr_wasm_doc },
		{ "eventsConvert", call_convert },
	};
	for (size_t e = 0; e < sizeof entries / sizeof entries[0]; e++) {
		long total;
		fail_at = 0; alloc_calls = 0;
		char *full = entries[e].fn(doc, (int)n);
		CHECK(full && has(full, "\"ok\":true"), "%s: baseline is ok", entries[e].name);
		free(full);
		/* count this entry point's allocations, then fail each in turn */
		fail_at = 1000000000L; alloc_calls = 0;
		free(entries[e].fn(doc, (int)n));
		total = alloc_calls;
		CHECK(total > 0, "%s allocates", entries[e].name);
		bool saw_oom = false;
		for (long at = 1; at <= total; at++) {
			fail_at = at; alloc_calls = 0;
			char *json = entries[e].fn(doc, (int)n);
			fail_at = 0;
			check_shape(entries[e].name, at, json);
			if (has(json, "\"detail\":\"out of memory\"")) saw_oom = true;
			free(json);
		}
		CHECK(saw_oom, "%s: exhaustion is reported as such at least once",
		      entries[e].name);
	}
	fail_at = 0;
}

static void test_result_shapes(void)
{
	char *j;
	/* both tiers in validate() and errors() */
	static const char dup[] = ".a = 1;\n.a = 2;\n";
	j = bvnr_wasm_validate(dup, (int)(sizeof dup - 1));
	CHECK(has(j, "\"ok\":false") && has(j, "\"error_name\":\"duplicate_struct_key\"") &&
	      has(j, "\"tier\":\"document\"") && has(j, "\"line\":0"),
	      "validate reports a duplicate key, positionless: %s", j);
	free(j);
	j = bvnr_wasm_errors(dup, (int)(sizeof dup - 1));
	CHECK(has(j, "\"ok\":false") && has(j, "\"error_name\":\"duplicate_struct_key\"") &&
	      has(j, "\"tier\":\"document\""), "errors reports it too: %s", j);
	free(j);
	static const char het[] = ".a = [1, \"x\"];";
	j = bvnr_wasm_validate(het, (int)(sizeof het - 1));
	CHECK(has(j, "\"error_name\":\"array_element_type_mismatch\""),
	      "validate reports a heterogeneous array: %s", j);
	free(j);
	static const char good[] = ".a = [1, 2];\n.b = \"x\";";
	j = bvnr_wasm_validate(good, (int)(sizeof good - 1));
	CHECK(has(j, "\"ok\":true") && has(j, "\"tier\":null"), "a valid document: %s", j);
	free(j);
	j = bvnr_wasm_errors(good, (int)(sizeof good - 1));
	CHECK(has(j, "\"ok\":true") && has(j, "\"errors\":[]"), "no errors: %s", j);
	free(j);
	static const char bad[] = ".a = @;";
	j = bvnr_wasm_validate(bad, (int)(sizeof bad - 1));
	CHECK(has(j, "\"tier\":\"stream\"") && has(j, "\"line\":1"),
	      "a stream error keeps its position: %s", j);
	free(j);

	/* the FIRST error, also when the last one is fatal */
	static const char two[] = ".a = \"\xff\xfe\";";      /* bad bytes, then EOF */
	static const char twob[] = ".a = \"\xff";
	j = bvnr_wasm_events(twob, (int)(sizeof twob - 1));
	CHECK(has(j, "\"error_name\":\"invalid_utf8_byte\""),
	      "events names the first error: %s", j);
	free(j);
	j = bvnr_wasm_parse(twob, (int)(sizeof twob - 1));
	CHECK(has(j, "\"ok\":false,\"error\":19,\"error_name\":\"invalid_utf8_byte\""),
	      "parse names the first error: %s", j);
	free(j);
	(void)two;

	/* doc() says why */
	j = bvnr_wasm_doc(dup, (int)(sizeof dup - 1));
	CHECK(has(j, "\"ok\":false,\"error\":41,\"error_name\":\"duplicate_struct_key\""),
	      "doc carries the error: %s", j);
	free(j);

	/* toJSON refuses to truncate a datetime */
	static const char frac[] =
		"#!bovnar 1.1\n.when = <datetime:64> 2026-06-15T12:00:00.5Z;";
	j = bvnr_wasm_to_json(frac, (int)(sizeof frac - 1));
	CHECK(has(j, "\"ok\":false") && has(j, "\"error_name\":\"value_out_of_range\"") &&
	      has(j, "sub-second") && has(j, "'when'") && has(j, "\"value\":null"),
	      "toJSON refuses a fractional datetime: %s", j);
	free(j);
	static const char whole[] =
		"#!bovnar 1.1\n.when = <datetime:64> 2026-06-15T12:00:00Z;";
	j = bvnr_wasm_to_json(whole, (int)(sizeof whole - 1));
	CHECK(has(j, "\"ok\":true") && has(j, "\"when\":1781524800"),
	      "a whole-second datetime is its epoch count: %s", j);
	free(j);
}

int main(void)
{
	test_result_shapes();
	test_out_of_memory_shapes();
	if (failures) {
		fprintf(stderr, "FAILED %d of %d checks\n", failures, checks);
		return 1;
	}
	printf("PASSED %d checks\n", checks);
	return 0;
}
