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
/* The _ex twins, at their defaults: the sweep reaches their allocation paths
 * without needing a policy that outlives a loop iteration. */
static char *call_validate_ex(const char *buf, int len)
{
	return bvnr_wasm_validate_ex(buf, len, 0, 0);
}
static char *call_events_ex(const char *buf, int len)
{
	return bvnr_wasm_events_ex(buf, len, 0, 0);
}
static char *call_unit(const char *buf, int len)
{
	(void)buf; (void)len;
	return bvnr_wasm_unit("k~m/s");
}
static char *call_profiles(const char *buf, int len)
{
	(void)buf; (void)len;
	return bvnr_wasm_unit_profiles();
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
		{ "validate_ex", call_validate_ex }, { "events_ex", call_events_ex },
		{ "unit", call_unit },              { "unit_profiles", call_profiles },
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

/*
 * The reader options and the unit policy -- bvnr_read_flags_t and
 * bvnr_reader_set_unit_policy, as data -- plus the shapes the event, doc and
 * helper additions produce. Everything here is the C API's own behaviour
 * surfaced through the shim; the JS tests check the wrapper on top.
 */
static void test_read_options_and_policy(void)
{
	char *j;
#define LEN(s) ((int)(sizeof(s) - 1))

	/* A future version directive is accepted by default and refused under
	 * strict_version, exactly as bvnr_read_flags_t documents. */
	static const char fut[] = "#!bovnar 9.9\n.a = 1;";
	j = bvnr_wasm_validate(fut, LEN(fut));
	CHECK(has(j, "\"ok\":true") && has(j, "\"declared_version\":\"9.9\""),
	      "future version accepted by default: %s", j);
	free(j);
	bvnr_wasm_opts_t o;
	memset(&o, 0, sizeof o);
	o.version = BVNR_WASM_OPTS_VERSION;
	o.flags = BVNR_WASM_OPT_STRICT_VERSION;
	j = bvnr_wasm_validate_ex(fut, LEN(fut), (uintptr_t)&o, 0);
	CHECK(has(j, "\"error_name\":\"unsupported_spec_version\""),
	      "strict_version refuses it: %s", j);
	free(j);

	/* text_only refuses the one part of the format a text pipeline destroys. */
	static const char oct[] = ".a = \x00\x01\x02\x00hi\x00;";
	o.flags = BVNR_WASM_OPT_TEXT_ONLY;
	j = bvnr_wasm_validate_ex(oct, LEN(oct), (uintptr_t)&o, 0);
	CHECK(has(j, "\"error_name\":\"octet_stream_forbidden\""),
	      "text_only refuses an octet stream: %s", j);
	free(j);

	/* A block from a layout this build does not know is refused whole. */
	o.flags = 0;
	o.version = BVNR_WASM_OPTS_VERSION + 1u;
	j = bvnr_wasm_validate_ex(oct, LEN(oct), (uintptr_t)&o, 0);
	CHECK(has(j, "\"error_name\":\"invalid_argument\"") &&
	      has(j, "unrecognized reader options"),
	      "an unknown options layout is refused: %s", j);
	free(j);
	o.version = BVNR_WASM_OPTS_VERSION;

	/* The limits reach the reader: this identifier is longer than 4. */
	static const char ident[] = ".averylongkey = 1;";
	o.max_identifier_length = 4;
	j = bvnr_wasm_validate_ex(ident, LEN(ident), (uintptr_t)&o, 0);
	CHECK(has(j, "\"error_name\":\"identifier_too_long\""),
	      "max_identifier_length reaches the reader: %s", j);
	free(j);

	/* require_unit: a bare number fails; the document with a unit passes. */
	void *p = bvnr_wasm_policy_create();
	CHECK(p != NULL, "policy_create");
	CHECK(bvnr_wasm_policy_require_unit(p, 1), "policy_require_unit");
	static const char bare[] = ".a = 1;\n";
	j = bvnr_wasm_validate_ex(bare, LEN(bare), 0, (uintptr_t)p);
	CHECK(has(j, "\"error_name\":\"unit_mismatch\""),
	      "require_unit refuses a bare number: %s", j);
	free(j);
	j = bvnr_wasm_errors_ex(bare, LEN(bare), 0, (uintptr_t)p);
	CHECK(has(j, "\"error_name\":\"unit_mismatch\""),
	      "require_unit reaches errors(): %s", j);
	free(j);
	bvnr_wasm_policy_destroy(p);

	/* The add is checked by the library's own parser: a unit it cannot parse,
	 * a rule path that could never match, and a base it cannot write are all
	 * refused at the call, leaving the handle usable. */
	void *q = bvnr_wasm_policy_create();
	CHECK(!bvnr_wasm_policy_target(q, "not a unit", 0),
	      "policy refuses an unparseable target unit");
	CHECK(!bvnr_wasm_policy_require_dimension(q, "zzz:xx"),
	      "policy refuses an unknown profile");
	CHECK(!bvnr_wasm_policy_rule(q, ".a b", "m", 0, 0),
	      "policy refuses a rule path a key cannot have");
	CHECK(!bvnr_wasm_policy_rule(q, ".a", "m", 1, 0),
	      "policy refuses a base bvnr cannot write");
	CHECK(!bvnr_wasm_policy_rule(q, ".a", "m", 0, 9),
	      "policy refuses an unknown rule mode");
	CHECK(bvnr_wasm_policy_rule(q, ".speed", "m/s", 0, 0),
	      "policy accepts a good rule after refused ones");
	bvnr_wasm_policy_destroy(q);

	/* A target converts on the reader path and on the DOM path (the DOM stores
	 * the converted value, so toJSON sees 1500 and doc's spelling is gone). */
	void *t = bvnr_wasm_policy_create();
	CHECK(bvnr_wasm_policy_target(t, "m", 0), "policy target m");
	static const char km[] = ".v = <float:64,km> 1.5;";
	j = bvnr_wasm_events_ex(km, LEN(km), 0, (uintptr_t)t);
	CHECK(has(j, "\"converted\":\"1500\"") && has(j, "\"converted_unit\":\"m\""),
	      "a policy target converts the event: %s", j);
	free(j);
	j = bvnr_wasm_to_json_ex(km, LEN(km), (uintptr_t)t);
	CHECK(has(j, "\"v\":1500"), "a policy target converts the DOM: %s", j);
	free(j);
	j = bvnr_wasm_doc_ex(km, LEN(km), (uintptr_t)t);
	CHECK(has(j, "\"unit\":\"m\"") && has(j, "\"unitSource\":null"),
	      "a converted DOM node loses the document spelling: %s", j);
	free(j);
	bvnr_wasm_policy_destroy(t);
#undef LEN
}

/* The event and document fields the additions expose: a float_fix's q, the
 * first error's position and what resync cost, the exact rational behind a
 * non-terminating conversion, the written unit spelling, and a datetime's
 * sub-second digits. */
static void test_new_event_and_doc_fields(void)
{
	char *j;
#define LEN(s) ((int)(sizeof(s) - 1))
	static const char ff[] = ".v = <float_fix:16,q8> 1.5;";
	j = bvnr_wasm_events(ff, LEN(ff));
	CHECK(has(j, "\"tok\":\"type_q\",\"q\":8}"),
	      "a q parameter carries q, not a base: %s", j);
	CHECK(!has(j, "\"tok\":\"type_q\",\"family\""),
	      "a q parameter is not stamped with the resolved type: %s", j);
	free(j);

	static const char bad[] = ".a = @;\n.b = 1;\n";
	j = bvnr_wasm_events(bad, LEN(bad));
	CHECK(has(j, "\"error_name\":\"unexpected_input_byte\"") &&
	      has(j, "\"line\":1,\"column\":6,\"byte\":64,\"offset\":5") &&
	      has(j, "\"recovery_count\":1"),
	      "events carries the first position and the recovery count: %s", j);
	free(j);
	j = bvnr_wasm_parse(bad, LEN(bad));
	CHECK(has(j, "\"line\":1,\"column\":6,\"byte\":64,\"offset\":5"),
	      "parse carries it too: %s", j);
	free(j);
	j = bvnr_wasm_errors(bad, LEN(bad));
	CHECK(has(j, "\"ok\":false,\"error\":15,"
	            "\"error_name\":\"unexpected_input_byte\"") &&
	      has(j, "\"recovery_count\":1"),
	      "errors names the first error at the top level: %s", j);
	free(j);

	/* A non-terminating conversion: the rational is the value, so it must be
	 * reachable. */
	static const char slow[] = ".v = <float:64,k~m/h> 100.0;";
	j = bvnr_wasm_events_convert(slow, LEN(slow), "m/s", 10, 1);
	CHECK(has(j, "\"converted\":null") &&
	      has(j, "\"converted_num\":\"250\"") &&
	      has(j, "\"converted_den\":\"9\""),
	      "the exact rational is delivered with a null text: %s", j);
	free(j);
	/* ...and only then: a terminating conversion needs no rational. */
	static const char exact[] = ".v = <float:64,km> 1.5;";
	j = bvnr_wasm_events_convert(exact, LEN(exact), "m", 10, 0);
	CHECK(has(j, "\"converted\":\"1500\"") && !has(j, "converted_num"),
	      "a terminating conversion omits the rational: %s", j);
	free(j);

	/* An unusable base is refused before the document is read, so an empty
	 * document cannot pass one either. */
	j = bvnr_wasm_events_convert(".v = 1;", 7, "", 63, 0);
	CHECK(has(j, "\"error_name\":\"invalid_argument\"") && has(j, "\"events\":[]"),
	      "base 63 is refused up front: %s", j);
	free(j);

	/* doc(): the written spelling and the sub-second digits. */
	static const char prof[] =
		"#!bovnar 1.2\n.a = <float:64,ucum:mL{total}> 1.0;\n.b = 2 km;";
	j = bvnr_wasm_doc(prof, LEN(prof));
	CHECK(has(j, "\"unitSource\":\"ucum:mL{total}\""),
	      "doc keeps the profile spelling: %s", j);
	CHECK(has(j, "\"unitSource\":\"km\""),
	      "doc keeps a non-canonical spelling: %s", j);
	free(j);
	static const char frac[] =
		"#!bovnar 1.1\n.t = <datetime:64> 2026-06-15T12:00:00.5Z;";
	j = bvnr_wasm_doc(frac, LEN(frac));
	CHECK(has(j, "\"frac\":\"5\""), "doc keeps the sub-second digits: %s", j);
	free(j);
#undef LEN
}

/* The stateless helpers: versions, error names, unit introspection. */
static void test_helpers(void)
{
	char *j;
	CHECK(strcmp(bvnr_wasm_error_name(0), "none") == 0, "error_name(0)");
	CHECK(strcmp(bvnr_wasm_error_name(38), "unit_mismatch") == 0, "error_name(38)");
	CHECK(bvnr_wasm_version_number() == (uint32_t)BVNR_VERSION,
	      "version_number is BVNR_VERSION");
	uint16_t smaj = 0, smin = 0;
	bvnr_spec_version(&smaj, &smin);
	CHECK(bvnr_wasm_spec_version() == (((uint32_t)smaj << 16) | smin),
	      "spec_version matches bvnr_spec_version");
	static const char dir[] = "#!bovnar 1.2\n.a = 1;";
	CHECK(bvnr_wasm_peek_version(dir, (int)(sizeof dir - 1)) ==
	      (((uint32_t)1 << 16) | 2u), "peek_version reads the directive");
	CHECK(bvnr_wasm_peek_version(".a = 1;", 7) == 0xffffffffu,
	      "peek_version says there is none");

	j = bvnr_wasm_unit("k~m/s");
	CHECK(has(j, "\"ok\":true") && has(j, "\"unit\":\"k~m/s\"") &&
	      has(j, "\"profile_only\":false"), "unit parses: %s", j);
	free(j);
	j = bvnr_wasm_unit("zzz:x");
	CHECK(has(j, "\"error_name\":\"unit_profile_unknown\"") &&
	      has(j, "\"unit\":null"), "unit names an unknown profile: %s", j);
	free(j);
	CHECK(bvnr_wasm_units_equal("m/s", "m·s⁻¹") == 1,
	      "units_equal sees the canonical spelling");
	CHECK(bvnr_wasm_units_equal("m", "s") == 0, "units_equal: different");
	CHECK(bvnr_wasm_units_equal("m", "nope") == -1, "units_equal: not a unit");
	CHECK(bvnr_wasm_units_convertible("m", "ft") == 1,
	      "units_convertible: a length to a length");
	CHECK(bvnr_wasm_units_convertible("m", "s") == 0,
	      "units_convertible: a length to a time");
	CHECK(bvnr_wasm_units_convertible("m", "nope") == -1,
	      "units_convertible: not a unit");
	j = bvnr_wasm_convert_unit(0.0, "°C", "K");
	CHECK(has(j, "\"ok\":true") && has(j, "\"value\":273.1"),
	      "convert_unit: an affine conversion: %s", j);
	free(j);
	j = bvnr_wasm_convert_unit(1.0, "m", "s");
	CHECK(has(j, "\"error_name\":\"unit_mismatch\"") &&
	      has(j, "\"value\":null"), "convert_unit: incompatible units: %s", j);
	free(j);
	j = bvnr_wasm_unit_profiles();
	CHECK(has(j, "\"ok\":true") && has(j, "\"ucum\"") &&
	      has(j, "\"udunits\""), "profiles list: %.80s", j);
	free(j);
}

int main(void)
{
	test_result_shapes();
	test_read_options_and_policy();
	test_new_event_and_doc_fields();
	test_helpers();
	test_out_of_memory_shapes();
	if (failures) {
		fprintf(stderr, "FAILED %d of %d checks\n", failures, checks);
		return 1;
	}
	printf("PASSED %d checks\n", checks);
	return 0;
}
