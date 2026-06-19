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
#include <stdlib.h>
#include <string.h>
#include "bovnar.h"
#include "bvn_float.h"
#include "bvn_int.h"

static int failures = 0;
static int tests    = 0;

#define ASSERT_TRUE(cond, msg) do {                                     \
	tests++;                                                            \
	if (!(cond)) {                                                      \
		fprintf(stderr, "FAIL line %d: %s\n", __LINE__, (msg));        \
		failures++;                                                     \
	}                                                                   \
} while (0)

#define ASSERT_EQ_INT(a, b, msg) do {                                   \
	tests++;                                                            \
	int64_t _a = (int64_t)(a);                                         \
	int64_t _b = (int64_t)(b);                                         \
	if (_a != _b) {                                                     \
		fprintf(stderr, "FAIL line %d: %s\n  got %lld, expected %lld\n",\
				__LINE__, (msg), (long long)_a, (long long)_b);        \
		failures++;                                                     \
	}                                                                   \
} while (0)

#define ASSERT_EQ_UINT(a, b, msg) do {                                  \
	tests++;                                                            \
	uint64_t _a = (uint64_t)(a);                                       \
	uint64_t _b = (uint64_t)(b);                                       \
	if (_a != _b) {                                                     \
		fprintf(stderr, "FAIL line %d: %s\n  got %llu, expected %llu\n",\
				__LINE__, (msg),                                        \
				(unsigned long long)_a, (unsigned long long)_b);       \
		failures++;                                                     \
	}                                                                   \
} while (0)

#define ASSERT_NOT_NULL(ptr, msg) do {                                  \
	tests++;                                                            \
	if ((ptr) == NULL) {                                                \
		fprintf(stderr, "FAIL line %d: %s\n  got null pointer\n",      \
				__LINE__, (msg));                                       \
		failures++;                                                     \
	}                                                                   \
} while (0)

#define ASSERT_FALSE(cond, msg) ASSERT_TRUE(!(cond), (msg))

static bvnr_writer_t *make_writer(uint8_t *buf, uint32_t cap, bvnr_sink_t *sink)
{
	bvnr_sink_to_mem(sink, buf, cap);
	bvnr_writer_t *w = bvnr_writer_create();
	if (!w) return NULL;
	if (!bvnr_open_write_sink(w, sink, true, NULL)) {
		bvnr_writer_destroy(w);
		return NULL;
	}
	bvnr_data_t hdr = {0};
	if (!bvnr_write_event(w, ev_stream_start, &hdr)) {
		bvnr_writer_destroy(w);
		return NULL;
	}
	return w;
}

typedef struct {
	char    key[256];
	char    value[256];
	uint32_t value_len;
	value_type_spec_t vt;
	token_type_t value_tok;
} last_event_t;

static bool on_rt(void *ud, bvnr_event_t ev, bvnr_data_t *d)
{
	last_event_t *le = ud;
	if (ev == ev_assignment_start && d->length < sizeof(le->key)) {
		memcpy(le->key, d->data, d->length);
		le->key[d->length] = '\0';
	}

	if (ev == ev_type_annotation_type_family_parameter)
		le->vt = d->value_type;
	if (ev == ev_data && d->length < sizeof(le->value)) {
		memcpy(le->value, d->data, d->length);
		le->value_len = d->length;
		le->value[d->length] = '\0';
		le->value_tok = d->type;

		if (le->vt.family == vt_plain)
			le->vt = d->value_type;
	}
	return true;
}

static bool roundtrip(const uint8_t *buf, uint64_t len, last_event_t *le)
{
	bvnr_reader_t *r = bvnr_reader_create();
	if (!r) return false;
	bvnr_read_flags_t opts = { .on_verified = on_rt, .userdata = le };
	bool ok = bvnr_open_read_mem(r, buf, len, NULL, 0, &opts)
		   && bvnr_read(r);
	bvnr_reader_destroy(r);
	return ok;
}

static void test_write_simple_strings(void)
{
	printf("  test_write_simple_strings...\n");

	uint8_t output[4096];
	bvnr_sink_t sink;
	bvnr_writer_t *w = make_writer(output, sizeof(output), &sink);
	ASSERT_NOT_NULL(w, "make_writer must succeed");
	if (!w) return;

	ASSERT_TRUE(bvnr_write_string(w, "name", "Alice"),
				"write name must succeed");
	ASSERT_TRUE(bvnr_write_string(w, "city", "Berlin"),
				"write city must succeed");
	ASSERT_TRUE(bvnr_write_finish(w), "finish must succeed");
	ASSERT_TRUE(bvnr_writer_bytes_written(w) > 0, "bytes_written > 0");
	ASSERT_EQ_INT(bvnr_writer_get_error(w), error_none, "no error");

	last_event_t le = {0};
	ASSERT_TRUE(roundtrip(output, bvnr_writer_bytes_written(w), &le),
				"round-trip must succeed");
	ASSERT_TRUE(strcmp(le.key, "city") == 0, "last key == city");
	ASSERT_TRUE(strcmp(le.value, "Berlin") == 0, "last value == Berlin");

	bvnr_writer_destroy(w);
}

static void test_write_typed_integers(void)
{
	printf("  test_write_typed_integers...\n");

	uint8_t output[4096];
	bvnr_sink_t sink;
	bvnr_writer_t *w = make_writer(output, sizeof(output), &sink);
	ASSERT_NOT_NULL(w, "make_writer must succeed");
	if (!w) return;

	ASSERT_TRUE(bvnr_write_uint(w, "age",         8,  42),
				"write uint:8 age must succeed");
	ASSERT_TRUE(bvnr_write_sint(w, "temperature", 16, -15),
				"write sint:16 temperature must succeed");
	ASSERT_TRUE(bvnr_write_uint(w, "flags",       32, 0xDEADBEEFUL),
				"write uint:32 flags must succeed");
	ASSERT_TRUE(bvnr_write_finish(w), "finish must succeed");
	ASSERT_EQ_INT(bvnr_writer_get_error(w), error_none, "no error");

	last_event_t le = {0};
	ASSERT_TRUE(roundtrip(output, bvnr_writer_bytes_written(w), &le),
				"round-trip must succeed");
	ASSERT_TRUE(le.vt.family == vt_uint, "last type is uint");
	ASSERT_EQ_UINT(le.vt.width, 32, "last width == 32");

	bvnr_writer_destroy(w);
}

static void test_write_float_values(void)
{
	printf("  test_write_float_values...\n");

	uint8_t output[4096];
	bvnr_sink_t sink;
	bvnr_writer_t *w = make_writer(output, sizeof(output), &sink);
	ASSERT_NOT_NULL(w, "make_writer must succeed");
	if (!w) return;

	ASSERT_TRUE(bvnr_write_float(w, "pi",      64, 3.14159265358979),
				"write float:64 pi must succeed");
	ASSERT_TRUE(bvnr_write_float(w, "epsilon", 32, 1.19209e-7f),
				"write float:32 epsilon must succeed");
	ASSERT_TRUE(bvnr_write_finish(w), "finish must succeed");
	ASSERT_EQ_INT(bvnr_writer_get_error(w), error_none, "no error");

	last_event_t le = {0};
	ASSERT_TRUE(roundtrip(output, bvnr_writer_bytes_written(w), &le),
				"round-trip must succeed");
	ASSERT_TRUE(le.vt.family == vt_float, "last type is float");

	bvnr_writer_destroy(w);
}

static void test_write_float_width_over_64_rejected(void)
{
	printf("  test_write_float_width_over_64_rejected...\n");

	uint8_t output[512];
	bvnr_sink_t sink;
	bvnr_writer_t *w;

	/* Valid widths 0, 16, 32, 64, 128, 256 must all succeed */
	static const uint32_t valid_widths[] = { 0, 16, 32, 64, 128, 256 };
	for (size_t i = 0; i < sizeof(valid_widths)/sizeof(valid_widths[0]); i++) {
		uint32_t wd = valid_widths[i];
		w = make_writer(output, sizeof(output), &sink);
		ASSERT_NOT_NULL(w, "make_writer must succeed for valid float width");
		if (!w) return;
		ASSERT_TRUE(bvnr_write_float(w, "x", wd, 3.14),
					"bvnr_write_float must succeed for valid width");
		ASSERT_EQ_INT(bvnr_writer_get_error(w), error_none,
					  "no error for valid float width");
		bvnr_writer_destroy(w);

		w = make_writer(output, sizeof(output), &sink);
		ASSERT_NOT_NULL(w, "make_writer must succeed for valid float_fix width");
		if (!w) return;
		ASSERT_TRUE(bvnr_write_float_fix(w, "x", wd, 10, 1.5),
					"bvnr_write_float_fix must succeed for valid width");
		ASSERT_EQ_INT(bvnr_writer_get_error(w), error_none,
					  "no error for valid float_fix width");
		bvnr_writer_destroy(w);

		w = make_writer(output, sizeof(output), &sink);
		ASSERT_NOT_NULL(w, "make_writer must succeed for valid float_dec width");
		if (!w) return;
		ASSERT_TRUE(bvnr_write_float_dec(w, "x", wd, 2.5),
					"bvnr_write_float_dec must succeed for valid width");
		ASSERT_EQ_INT(bvnr_writer_get_error(w), error_none,
					  "no error for valid float_dec width");
		bvnr_writer_destroy(w);
	}

	/* Invalid width (not in {0,16,32,64,128,256}) must be rejected */
	w = make_writer(output, sizeof(output), &sink);
	ASSERT_NOT_NULL(w, "make_writer must succeed");
	if (!w) return;
	ASSERT_FALSE(bvnr_write_float(w, "x", 100, 3.14),
				 "bvnr_write_float with width=100 must fail");
	ASSERT_EQ_INT(bvnr_writer_get_error(w), error_invalid_argument,
				  "error must be error_invalid_argument for width=100");
	bvnr_writer_destroy(w);

	w = make_writer(output, sizeof(output), &sink);
	ASSERT_NOT_NULL(w, "make_writer (fix) must succeed");
	if (!w) return;
	ASSERT_FALSE(bvnr_write_float_fix(w, "x", 100, 10, 1.5),
				 "bvnr_write_float_fix with width=100 must fail");
	ASSERT_EQ_INT(bvnr_writer_get_error(w), error_invalid_argument,
				  "float_fix width=100 error must be error_invalid_argument");
	bvnr_writer_destroy(w);

	w = make_writer(output, sizeof(output), &sink);
	ASSERT_NOT_NULL(w, "make_writer (dec) must succeed");
	if (!w) return;
	ASSERT_FALSE(bvnr_write_float_dec(w, "x", 100, 2.5),
				 "bvnr_write_float_dec with width=100 must fail");
	ASSERT_EQ_INT(bvnr_writer_get_error(w), error_invalid_argument,
				  "float_dec width=100 error must be error_invalid_argument");
	bvnr_writer_destroy(w);
}

static void test_write_bool_and_null(void)
{
	printf("  test_write_bool_and_null...\n");

	uint8_t output[4096];
	bvnr_sink_t sink;
	bvnr_writer_t *w = make_writer(output, sizeof(output), &sink);
	ASSERT_NOT_NULL(w, "make_writer must succeed");
	if (!w) return;

	ASSERT_TRUE(bvnr_write_bool(w, "active",   true),  "write true must succeed");
	ASSERT_TRUE(bvnr_write_bool(w, "disabled", false), "write false must succeed");
	ASSERT_TRUE(bvnr_write_null(w, "optional"),        "write null must succeed");
	ASSERT_TRUE(bvnr_write_finish(w), "finish must succeed");
	ASSERT_EQ_INT(bvnr_writer_get_error(w), error_none, "no error");

	bvnr_writer_destroy(w);
}

static void test_write_with_units(void)
{
	printf("  test_write_with_units...\n");

	uint8_t output[4096];
	bvnr_sink_t sink;
	bvnr_writer_t *w = make_writer(output, sizeof(output), &sink);
	ASSERT_NOT_NULL(w, "make_writer must succeed");
	if (!w) return;

	value_unit_t km = BVN_UNIT_SI(bu_meter, si_kilo);
	ASSERT_TRUE(bvnr_write_uint_unit(w, "distance", 32, 42, km),
				"write uint with km unit must succeed");

	value_unit_t ms = BVN_UNIT_COMPOUND2(
		bu_meter,  si_none, exp_linear,
		bu_second, si_none, exp_neg_linear);
	ASSERT_TRUE(bvnr_write_float_unit(w, "speed", 64, 9.81, ms),
				"write float with m/s unit must succeed");

	value_unit_t kg = BVN_UNIT_SI(bu_gram, si_kilo);
	ASSERT_TRUE(bvnr_write_float_unit(w, "mass", 32, 70.5, kg),
				"write float with kg unit must succeed");

	ASSERT_TRUE(bvnr_write_finish(w), "finish must succeed");
	ASSERT_EQ_INT(bvnr_writer_get_error(w), error_none, "no error");

	bvnr_writer_destroy(w);
}

static void test_write_struct(void)
{
	printf("  test_write_struct...\n");

	uint8_t output[4096];
	bvnr_sink_t sink;
	bvnr_writer_t *w = make_writer(output, sizeof(output), &sink);
	ASSERT_NOT_NULL(w, "make_writer must succeed");
	if (!w) return;

	ASSERT_TRUE(bvnr_write_struct_start(w, "server"),       "struct begin");
	ASSERT_TRUE(bvnr_write_string      (w, "host", "127.0.0.1"), "write host");
	ASSERT_TRUE(bvnr_write_uint        (w, "port", 16, 8080),    "write port");
	ASSERT_TRUE(bvnr_write_bool        (w, "tls",  false),       "write tls");
	ASSERT_TRUE(bvnr_write_struct_end  (w),                 "struct end");

	ASSERT_TRUE(bvnr_write_finish(w), "finish must succeed");
	ASSERT_EQ_INT(bvnr_writer_get_error(w), error_none, "no error");

	last_event_t le = {0};
	ASSERT_TRUE(roundtrip(output, bvnr_writer_bytes_written(w), &le),
				"round-trip must succeed");

	ASSERT_TRUE(strcmp(le.key, "tls") == 0, "last key == tls");

	bvnr_writer_destroy(w);
}

static void test_write_nested_struct(void)
{
	printf("  test_write_nested_struct...\n");

	uint8_t output[4096];
	bvnr_sink_t sink;
	bvnr_writer_t *w = make_writer(output, sizeof(output), &sink);
	ASSERT_NOT_NULL(w, "make_writer must succeed");
	if (!w) return;

	ASSERT_TRUE(bvnr_write_struct_start(w, "outer"),    "outer struct begin");
	ASSERT_TRUE(bvnr_write_struct_start(w, "inner"),    "inner struct begin");
	ASSERT_TRUE(bvnr_write_uint        (w, "x", 32, 1), "write x");
	ASSERT_TRUE(bvnr_write_uint        (w, "y", 32, 2), "write y");
	ASSERT_TRUE(bvnr_write_struct_end  (w),             "inner struct end");
	ASSERT_TRUE(bvnr_write_struct_end  (w),             "outer struct end");

	ASSERT_TRUE(bvnr_write_finish(w), "finish must succeed");
	ASSERT_EQ_INT(bvnr_writer_get_error(w), error_none, "no error");

	bvnr_writer_destroy(w);
}

static void test_write_type_annotation_direct(void)
{
	printf("  test_write_type_annotation_direct...\n");

	uint8_t output[4096];
	bvnr_sink_t sink;
	bvnr_writer_t *w = make_writer(output, sizeof(output), &sink);
	ASSERT_NOT_NULL(w, "make_writer must succeed");
	if (!w) return;

	const char *key = "count";
	bvnr_data_t d = {
		.type   = token_is_identifier,
		.data   = (const void *)key,
		.length = (uint32_t)strlen(key),
	};
	ASSERT_TRUE(bvnr_write_event(w, ev_assignment_start, &d),
				"assignment_start must succeed");

	value_type_spec_t vt = BVN_TYPE_UINT(32);
	ASSERT_TRUE(bvnr_write_type_annotation(w, vt, BVN_UNIT_NONE),
				"type_annotation must succeed");

	d = (bvnr_data_t){
		.type       = token_is_number,
		.value_type = vt,
		.data       = "99",
		.length     = 2,
	};
	ASSERT_TRUE(bvnr_write_event(w, ev_data, &d), "ev_data must succeed");

	ASSERT_TRUE(bvnr_write_finish(w), "finish must succeed");
	ASSERT_EQ_INT(bvnr_writer_get_error(w), error_none, "no error");

	last_event_t le = {0};
	ASSERT_TRUE(roundtrip(output, bvnr_writer_bytes_written(w), &le),
				"round-trip must succeed");
	ASSERT_TRUE(strcmp(le.key,   "count") == 0, "key == count");
	ASSERT_TRUE(strcmp(le.value, "99")    == 0, "value == 99");
	ASSERT_TRUE(le.vt.family == vt_uint,        "family == vt_uint");
	ASSERT_EQ_UINT(le.vt.width, 32,             "width == 32");

	bvnr_writer_destroy(w);
}

/*
 * Regression: a symbol (bare plain value) whose text equals a reserved keyword
 * (null/true/false/on/off/nan/inf/ninf) is emitted bare and would be
 * reclassified by the reader into a null/bool/float value, silently losing the
 * symbol type. Symbols have no quoted form, so the writer must reject them.
 */
static void test_write_keyword_symbol_rejected(void)
{
	printf("  test_write_keyword_symbol_rejected...\n");

	const char *keywords[] = { "null", "true", "false", "on",
				   "off", "nan", "inf", "ninf" };
	const char *valid[]    = { "hello", "null_", "true1", "Inf", "onoff" };

	for (size_t i = 0; i < sizeof(keywords) / sizeof(keywords[0]); i++) {
		uint8_t output[256];
		bvnr_sink_t sink;
		bvnr_writer_t *w = make_writer(output, sizeof(output), &sink);
		ASSERT_NOT_NULL(w, "make_writer must succeed");
		if (!w) continue;
		ASSERT_FALSE(bvnr_write_plain(w, "k", keywords[i]),
					 "keyword-valued symbol must be rejected");
		bvnr_writer_destroy(w);
	}
	for (size_t i = 0; i < sizeof(valid) / sizeof(valid[0]); i++) {
		uint8_t output[256];
		bvnr_sink_t sink;
		bvnr_writer_t *w = make_writer(output, sizeof(output), &sink);
		ASSERT_NOT_NULL(w, "make_writer must succeed");
		if (!w) continue;
		ASSERT_TRUE(bvnr_write_plain(w, "k", valid[i]),
					"non-keyword symbol must be accepted");
		bvnr_writer_destroy(w);
	}
}

/*
 * Regression: utf8 is a parameterless family (spec: any parameter ->
 * error_illegal_value_type). The writer must reject a width on utf8, or it
 * would emit <utf8:N> that its own reader rejects. A bare <utf8> is fine.
 */
static void test_write_utf8_width_rejected(void)
{
	printf("  test_write_utf8_width_rejected...\n");

	uint8_t output[256];
	bvnr_sink_t sink;
	bvnr_writer_t *w = make_writer(output, sizeof(output), &sink);
	ASSERT_NOT_NULL(w, "make_writer must succeed");
	if (!w) return;

	const char *key = "k";
	bvnr_data_t d = {
		.type   = token_is_identifier,
		.data   = (const void *)key,
		.length = 1u,
	};
	ASSERT_TRUE(bvnr_write_event(w, ev_assignment_start, &d),
				"assignment_start must succeed");

	value_type_spec_t utf8_w = { .family = vt_utf8, .width = 8u, .base = 0u };
	ASSERT_FALSE(bvnr_write_type_annotation(w, utf8_w, BVN_UNIT_NONE),
				 "utf8 with width must be rejected");
	bvnr_writer_destroy(w);

	/* A bare <utf8> annotation must still be accepted. */
	w = make_writer(output, sizeof(output), &sink);
	ASSERT_NOT_NULL(w, "make_writer must succeed");
	if (!w) return;
	ASSERT_TRUE(bvnr_write_event(w, ev_assignment_start, &d),
				"assignment_start must succeed");
	ASSERT_TRUE(bvnr_write_type_annotation(w, BVN_TYPE_UTF8, BVN_UNIT_NONE),
				"bare utf8 annotation must be accepted");
	bvnr_writer_destroy(w);
}

static void test_write_buffer_exhaustion(void)
{
	printf("  test_write_buffer_exhaustion...\n");

	uint8_t output[16];
	bvnr_sink_t sink;
	bvnr_sink_to_mem(&sink, output, sizeof(output));

	bvnr_writer_t *w = bvnr_writer_create();
	ASSERT_NOT_NULL(w, "bvnr_writer_create must succeed");
	if (!w) return;

	ASSERT_TRUE(bvnr_open_write_sink(w, &sink, true, NULL),
				"open_write_sink must succeed");

	bvnr_data_t hdr = {0};
	ASSERT_TRUE(bvnr_write_event(w, ev_stream_start, &hdr),
				"stream_begin must succeed");

	bool result = bvnr_write_string(w,
		"this_is_a_very_long_key_that_will_not_fit", "value");

	if (result) {
		ASSERT_TRUE(!bvnr_write_finish(w),
					"finish must fail on exhaustion");
		ASSERT_EQ_INT(bvnr_writer_get_error(w), error_sink_buffer_exhausted,
					  "error must be buffer_exhausted");
	} else {
		ASSERT_EQ_INT(bvnr_writer_get_error(w), error_sink_buffer_exhausted,
					  "error must be buffer_exhausted");
	}

	bvnr_writer_destroy(w);
}

static void test_write_multiple_assignments(void)
{
	printf("  test_write_multiple_assignments...\n");

	uint8_t output[8192];
	bvnr_sink_t sink;
	bvnr_writer_t *w = make_writer(output, sizeof(output), &sink);
	ASSERT_NOT_NULL(w, "make_writer must succeed");
	if (!w) return;

	for (int i = 0; i < 20; i++) {
		char key[16];
		snprintf(key, sizeof(key), "item%d", i);
		ASSERT_TRUE(bvnr_write_string(w, key, "value"),
					"write assignment must succeed");
	}

	ASSERT_TRUE(bvnr_write_finish(w), "finish must succeed");
	ASSERT_TRUE(bvnr_writer_bytes_written(w) > 0, "bytes_written > 0");

	bvnr_writer_destroy(w);
}

static void test_write_sint_negative(void)
{
	printf("  test_write_sint_negative...\n");

	uint8_t output[4096];
	bvnr_sink_t sink;
	bvnr_writer_t *w = make_writer(output, sizeof(output), &sink);
	ASSERT_NOT_NULL(w, "make_writer must succeed");
	if (!w) return;

	ASSERT_TRUE(bvnr_write_sint(w, "delta",  16, -1),
				"write sint:16 -1 must succeed");
	ASSERT_TRUE(bvnr_write_sint(w, "offset", 32, INT32_MIN),
				"write sint:32 INT32_MIN must succeed");
	ASSERT_TRUE(bvnr_write_sint(w, "big",    64, INT64_MIN),
				"write sint:64 INT64_MIN must succeed");

	ASSERT_TRUE(bvnr_write_finish(w), "finish must succeed");
	ASSERT_EQ_INT(bvnr_writer_get_error(w), error_none, "no error");

	bvnr_writer_destroy(w);
}

static void test_write_bvnf_base10(void)
{
	printf("  test_write_bvnf_base10...\n");

	uint8_t output[32768];
	bvnr_sink_t sink;
	bvnr_writer_t *w = make_writer(output, sizeof(output), &sink);
	ASSERT_NOT_NULL(w, "make_writer must succeed");
	if (!w) return;

	bvn_float_t *f = bvn_float_alloc(512u);
	ASSERT_NOT_NULL(f, "bvn_float_alloc(512) must succeed");
	if (!f) { bvnr_writer_destroy(w); return; }

	ASSERT_TRUE(bvn_float_from_double(f, 3.14159265358979323846),
				"bvn_float_from_double must succeed");

	ASSERT_TRUE(bvnr_write_bvnf_base(w, "pi_512", f, 512u, 10u),
				"bvnr_write_bvnf_base base10 width512 must succeed");

	ASSERT_TRUE(bvnr_write_bvnf_base(w, "pi_1024", f, 1024u, 10u),
				"bvnr_write_bvnf_base base10 width1024 must succeed");

	ASSERT_TRUE(bvnr_write_finish(w), "finish must succeed");
	ASSERT_EQ_INT(bvnr_writer_get_error(w), error_none, "no error");

	uint64_t bw = bvnr_writer_bytes_written(w);
	ASSERT_TRUE(bw > 0, "bytes_written > 0");

	last_event_t le = {0};
	ASSERT_TRUE(roundtrip(output, bw, &le),
				"bvnf base10 pi document must round-trip");
	ASSERT_TRUE(strstr(le.value, "3.14") != NULL,
				"pi_1024 value must begin with 3.14 (value preserved through write/read)");
	ASSERT_EQ_INT(le.value_tok, token_is_number,
				  "base10 bvnf token must be token_is_number");

	bvn_float_free(f);
	bvnr_writer_destroy(w);
}

static void test_write_bvnf_base16(void)
{
	printf("  test_write_bvnf_base16...\n");

	uint8_t output[32768];
	bvnr_sink_t sink;
	bvnr_writer_t *w = make_writer(output, sizeof(output), &sink);
	ASSERT_NOT_NULL(w, "make_writer must succeed");
	if (!w) return;

	bvn_float_t *f = bvn_float_alloc(256u);
	ASSERT_NOT_NULL(f, "bvn_float_alloc(256) must succeed");
	if (!f) { bvnr_writer_destroy(w); return; }

	ASSERT_TRUE(bvn_float_from_double(f, 1.5),
				"bvn_float_from_double(1.5) must succeed");

	ASSERT_TRUE(bvnr_write_bvnf_base(w, "hex_256", f, 256u, 16u),
				"bvnr_write_bvnf_base base16 width256 must succeed");

	bvn_float_set_nan(f);
	ASSERT_TRUE(bvnr_write_bvnf_base(w, "nan_256", f, 256u, 16u),
				"bvnr_write_bvnf_base base16 NaN must succeed");

	bvn_float_set_inf(f, false);
	ASSERT_TRUE(bvnr_write_bvnf_base(w, "pos_inf", f, 256u, 16u),
				"bvnr_write_bvnf_base base16 +Inf must succeed");

	bvn_float_set_inf(f, true);
	ASSERT_TRUE(bvnr_write_bvnf_base(w, "neg_inf", f, 256u, 16u),
				"bvnr_write_bvnf_base base16 -Inf must succeed");

	ASSERT_TRUE(bvnr_write_finish(w), "finish must succeed");
	ASSERT_EQ_INT(bvnr_writer_get_error(w), error_none, "no error");

	uint64_t n = bvnr_writer_bytes_written(w);
	ASSERT_TRUE(n > 0u, "bytes_written > 0");

	last_event_t le = {0};
	ASSERT_TRUE(roundtrip(output, n, &le),
				"base16 bvnf document with NaN and Inf must round-trip");

	bvn_float_free(f);
	bvnr_writer_destroy(w);

	{
		uint8_t out2[1024];
		bvnr_sink_t sink2;
		bvnr_writer_t *w2 = make_writer(out2, sizeof(out2), &sink2);
		ASSERT_NOT_NULL(w2, "value-check writer must succeed");
		if (!w2) return;

		bvn_float_t *f2 = bvn_float_alloc(256u);
		ASSERT_NOT_NULL(f2, "value-check bvn_float_alloc must succeed");
		if (!f2) { bvnr_writer_destroy(w2); return; }

		ASSERT_TRUE(bvn_float_from_double(f2, 1.5),
					"bvn_float_from_double(1.5) value-check");
		ASSERT_TRUE(bvnr_write_bvnf_base(w2, "v", f2, 256u, 16u),
					"write 1.5 base16 value-check");
		ASSERT_TRUE(bvnr_write_finish(w2), "finish value-check writer");

		last_event_t le2 = {0};
		ASSERT_TRUE(roundtrip(out2, bvnr_writer_bytes_written(w2), &le2),
					"1.5 base16 bvnf must round-trip");
		ASSERT_TRUE(strstr(le2.value, "1.8") != NULL,
					"base16 repr of 1.5 must contain '1.8' (value preserved)");
		ASSERT_EQ_INT(le2.value_tok, token_is_string,
					  "base16 non-special bvnf token must be token_is_string");

		bvn_float_free(f2);
		bvnr_writer_destroy(w2);
	}
}

static void test_write_bvni_decimal(void)
{
	printf("  test_write_bvni_decimal...\n");

	uint8_t output[32768];
	bvnr_sink_t sink;
	bvnr_writer_t *w = make_writer(output, sizeof(output), &sink);
	ASSERT_NOT_NULL(w, "make_writer must succeed");
	if (!w) return;

	bvn_int_t *n = bvn_int_alloc();
	ASSERT_NOT_NULL(n, "bvn_int_alloc must succeed");
	if (!n) { bvnr_writer_destroy(w); return; }

	ASSERT_TRUE(bvn_int_from_uint64(n, 18446744073709551615ULL),
				"bvn_int_from_uint64 UINT64_MAX must succeed");
	ASSERT_TRUE(bvnr_write_bvni(w, "u64max", n, 64u, 10u),
				"bvnr_write_bvni uint:64 base10 must succeed");

	ASSERT_TRUE(bvn_int_from_str(n, "340282366920938463463374607431768211455", 10),
				"bvn_int_from_str 128-bit max must succeed");
	ASSERT_TRUE(bvnr_write_bvni(w, "u128max", n, 128u, 10u),
				"bvnr_write_bvni uint:128 base10 must succeed");

	ASSERT_TRUE(bvn_int_from_int64(n, -1LL),
				"bvn_int_from_int64(-1) must succeed");
	ASSERT_TRUE(bvnr_write_bvni(w, "neg_one", n, 64u, 10u),
				"bvnr_write_bvni sint:64 -1 must succeed");

	ASSERT_TRUE(bvnr_write_finish(w), "finish must succeed");
	ASSERT_EQ_INT(bvnr_writer_get_error(w), error_none, "no error");

	last_event_t le = {0};
	ASSERT_TRUE(roundtrip(output, bvnr_writer_bytes_written(w), &le),
				"bvni decimal roundtrip must succeed");
	ASSERT_TRUE(le.vt.family == vt_sint,
				"last entry (neg_one) must be sint");
	ASSERT_EQ_UINT(le.vt.width, 64,
				   "neg_one width must be 64");
	ASSERT_TRUE(strcmp(le.value, "-1") == 0,
				"neg_one value must round-trip as \"-1\"");
	ASSERT_EQ_INT(le.value_tok, token_is_number,
				  "base10 bvni token must be token_is_number");

	bvn_int_free(n);
	bvnr_writer_destroy(w);
}

static void test_write_bvni_hex(void)
{
	printf("  test_write_bvni_hex...\n");

	uint8_t output[32768];
	bvnr_sink_t sink;
	bvnr_writer_t *w = make_writer(output, sizeof(output), &sink);
	ASSERT_NOT_NULL(w, "make_writer must succeed");
	if (!w) return;

	bvn_int_t *n = bvn_int_alloc();
	ASSERT_NOT_NULL(n, "bvn_int_alloc must succeed");
	if (!n) { bvnr_writer_destroy(w); return; }

	ASSERT_TRUE(bvn_int_from_str(n, "deadbeef", 16),
				"bvn_int_from_str hex must succeed");
	ASSERT_TRUE(bvnr_write_bvni(w, "hex_val", n, 32u, 16u),
				"bvnr_write_bvni uint:32 base16 must succeed");

	ASSERT_TRUE(bvn_int_from_str(n, "-7fffffff", 16),
				"bvn_int_from_str negative hex must succeed");
	ASSERT_TRUE(bvnr_write_bvni(w, "neg_hex", n, 32u, 16u),
				"bvnr_write_bvni sint:32 base16 must succeed");

	ASSERT_TRUE(bvnr_write_finish(w), "finish must succeed");
	ASSERT_EQ_INT(bvnr_writer_get_error(w), error_none, "no error");

	last_event_t le = {0};
	ASSERT_TRUE(roundtrip(output, bvnr_writer_bytes_written(w), &le),
				"bvni hex roundtrip must succeed");
	ASSERT_TRUE(le.vt.family == vt_sint,
				"last entry (neg_hex) must be sint");
	ASSERT_EQ_UINT(le.vt.width, 32,
				   "neg_hex width must be 32");
	ASSERT_EQ_UINT(le.vt.base, 16,
				   "neg_hex base must be 16");
	ASSERT_TRUE(strcmp(le.value, "-7fffffff") == 0,
				"neg_hex value must round-trip as \"-7fffffff\"");
	ASSERT_EQ_INT(le.value_tok, token_is_string,
				  "base16 bvni token must be token_is_string");

	bvn_int_free(n);
	bvnr_writer_destroy(w);
}

/*
 * Bases 64 and 85 use '+'/'-' as digit characters (Base64 '+'=62; Ascii85
 * '+'=10, '-'=12). They carry no sign and are unsigned-only. Verify the full
 * digit alphabet round-trips through writer->reader (a value whose digits
 * include '+'/'-' is written and read back unchanged), and that a signed
 * integer in these bases is rejected as an illegal type.
 */
static void test_write_bvni_base64_signchar(void)
{
	printf("  test_write_bvni_base64_signchar...\n");

	bvn_int_t *n = bvn_int_alloc();
	ASSERT_NOT_NULL(n, "bvn_int_alloc must succeed");
	if (!n) return;

	/* 3326 -> "z+" (digit 62 = '+'): writer emits it and the reader accepts. */
	{
		uint8_t output[4096];
		bvnr_sink_t sink;
		bvnr_writer_t *w = make_writer(output, sizeof(output), &sink);
		ASSERT_NOT_NULL(w, "make_writer (b64) must succeed");
		if (w) {
			ASSERT_TRUE(bvn_int_from_str(n, "3326", 10), "parse 3326");
			ASSERT_TRUE(bvnr_write_bvni(w, "v", n, 128u, 64u),
						"uint base-64 with a '+' digit must write");
			ASSERT_TRUE(bvnr_write_finish(w), "finish must succeed");
			last_event_t le = {0};
			ASSERT_TRUE(roundtrip(output, bvnr_writer_bytes_written(w), &le),
						"writer output must read back (no writer/reader skew)");
			ASSERT_TRUE(strcmp(le.value, "z+") == 0,
						"3326 must round-trip as base-64 \"z+\"");
			bvnr_writer_destroy(w);
		}
	}

	/* 12 -> "-" in Ascii85 (digit 12 = '-'): full alphabet round-trips. */
	{
		uint8_t output[4096];
		bvnr_sink_t sink;
		bvnr_writer_t *w = make_writer(output, sizeof(output), &sink);
		ASSERT_NOT_NULL(w, "make_writer (b85) must succeed");
		if (w) {
			ASSERT_TRUE(bvn_int_from_str(n, "12", 10), "parse 12");
			ASSERT_TRUE(bvnr_write_bvni(w, "v", n, 128u, 85u),
						"uint base-85 with a '-' digit must write");
			ASSERT_TRUE(bvnr_write_finish(w), "finish must succeed");
			last_event_t le = {0};
			ASSERT_TRUE(roundtrip(output, bvnr_writer_bytes_written(w), &le),
						"writer output must read back (no writer/reader skew)");
			ASSERT_TRUE(strcmp(le.value, "-") == 0,
						"12 must round-trip as base-85 \"-\"");
			bvnr_writer_destroy(w);
		}
	}

	/* A negative value forces family sint; sint is illegal for base 64/85. */
	{
		uint8_t output[4096];
		bvnr_sink_t sink;
		bvnr_writer_t *w = make_writer(output, sizeof(output), &sink);
		ASSERT_NOT_NULL(w, "make_writer (sint) must succeed");
		if (w) {
			ASSERT_TRUE(bvn_int_from_str(n, "-5", 10), "parse -5");
			ASSERT_TRUE(!bvnr_write_bvni(w, "bad", n, 128u, 64u),
						"signed integer in base 64 must be rejected");
			ASSERT_EQ_INT(bvnr_writer_get_error(w), error_illegal_value_type,
						  "rejection must report illegal_value_type");
			bvnr_writer_destroy(w);
		}
	}

	bvn_int_free(n);
}

static void test_write_streaming_flush(void)
{
	printf("  test_write_streaming_flush...\n");

	uint32_t cap = 2u * 1024u * 1024u;
	uint8_t *output = malloc(cap);
	ASSERT_NOT_NULL(output, "malloc 2MiB must succeed");
	if (!output) return;

	bvnr_sink_t sink;
	bvnr_sink_to_mem(&sink, output, cap);

	bvnr_writer_t *w = bvnr_writer_create();
	ASSERT_NOT_NULL(w, "bvnr_writer_create must succeed");
	if (!w) { free(output); return; }

	ASSERT_TRUE(bvnr_open_write_sink(w, &sink, false, NULL),
				"open_write_sink must succeed");

	bvnr_data_t hdr = {0};
	ASSERT_TRUE(bvnr_write_event(w, ev_stream_start, &hdr),
				"stream_start must succeed");

	const uint32_t n_writes = 4000u;
	for (uint32_t i = 0; i < n_writes; i++) {
		char key[32];
		snprintf(key, sizeof(key), "k%u", i);
		ASSERT_TRUE(bvnr_write_uint(w, key, 64u, (uint64_t)i),
					"bvnr_write_uint must succeed in streaming loop");
	}

	uint64_t mid_count = bvnr_writer_bytes_written(w);
	ASSERT_TRUE(mid_count > 65536u,
				"bytes_written must exceed 64KiB (buffer flushed at least once)");

	ASSERT_TRUE(bvnr_write_finish(w), "finish must succeed");
	ASSERT_EQ_INT(bvnr_writer_get_error(w), error_none, "no error");

	uint64_t final_count = bvnr_writer_bytes_written(w);
	ASSERT_TRUE(final_count >= mid_count,
				"bytes_written after finish >= bytes_written before finish");

	bvnr_writer_destroy(w);
	free(output);
}

static void test_write_float_fix_roundtrip(void)
{
	printf("  test_write_float_fix_roundtrip...\n");

	uint8_t output[256];
	bvnr_sink_t sink;
	bvnr_writer_t *w = make_writer(output, sizeof(output), &sink);
	ASSERT_NOT_NULL(w, "make_writer must succeed");
	if (!w) return;

	ASSERT_TRUE(bvnr_write_float_fix(w, "ff", 32, 16, 1.5),
				"write float_fix:32,q16 must succeed");
	ASSERT_TRUE(bvnr_write_float_fix(w, "neg", 64, 8, -0.25),
				"write float_fix:64,q8 negative must succeed");
	ASSERT_TRUE(bvnr_write_finish(w), "finish must succeed");
	ASSERT_EQ_INT(bvnr_writer_get_error(w), error_none, "no error");

	last_event_t le = {0};
	ASSERT_TRUE(roundtrip(output, bvnr_writer_bytes_written(w), &le),
				"float_fix roundtrip must parse cleanly");
	ASSERT_TRUE(le.vt.family == vt_float_fix, "last type family = float_fix");
	ASSERT_EQ_UINT(le.vt.width, 64, "last width = 64");
	ASSERT_EQ_UINT(le.vt.base, 8, "last Q = 8 (stored in base)");
	ASSERT_TRUE(le.value_tok == token_is_number,
				"float_fix data token is token_is_number");

	bvnr_writer_destroy(w);
}

/*
 * Regression: float_fix stores Q in the .base field, but Q is NOT a numeral
 * base. Q values outside the {2-62,64,85} numeral-base set (e.g. 1, 63, 100)
 * must still be writable as long as Q < width — the reader accepts them, so the
 * writer must round-trip them. Q == width (or greater) must still be rejected.
 */
static void test_write_float_fix_q_edges(void)
{
	printf("  test_write_float_fix_q_edges...\n");

	const struct { uint32_t w, q; double v; } good[] = {
		{ 32, 1,   0.5    },
		{ 64, 63,  0.125  },
		{ 128, 100, 0.0625 },
		{ 256, 200, 0.03125 },
	};
	for (size_t i = 0; i < sizeof(good) / sizeof(good[0]); i++) {
		uint8_t output[256];
		bvnr_sink_t sink;
		bvnr_writer_t *w = make_writer(output, sizeof(output), &sink);
		ASSERT_NOT_NULL(w, "make_writer must succeed");
		if (!w) continue;
		ASSERT_TRUE(bvnr_write_float_fix(w, "x", good[i].w, good[i].q, good[i].v),
					"write float_fix with valid Q outside numeral-base set");
		ASSERT_TRUE(bvnr_write_finish(w), "finish must succeed");
		ASSERT_EQ_INT(bvnr_writer_get_error(w), error_none, "no write error");
		last_event_t le = {0};
		ASSERT_TRUE(roundtrip(output, bvnr_writer_bytes_written(w), &le),
					"float_fix q-edge roundtrip must parse cleanly");
		ASSERT_EQ_UINT(le.vt.base, good[i].q, "Q preserved through roundtrip");
		bvnr_writer_destroy(w);
	}

	/* Q == width must be rejected. */
	uint8_t output[64];
	bvnr_sink_t sink;
	bvnr_writer_t *w = make_writer(output, sizeof(output), &sink);
	ASSERT_NOT_NULL(w, "make_writer must succeed");
	if (w) {
		ASSERT_FALSE(bvnr_write_float_fix(w, "x", 32, 32, 1.0),
					 "float_fix with Q == width must fail");
		bvnr_writer_destroy(w);
	}

	/* Value outside the Q-format's representable range must be rejected,
	 * not silently saturated/wrapped on a later decode. */
	const struct { uint32_t w, q; double v; } oor[] = {
		{ 16, 8, 1000000.0 },   /* range [-128, 127.996] */
		{ 16, 8, 128.0 },
		{ 16, 8, -200.0 },
		{ 32, 16, 1e9 },
		{ 16, 0, 70000.0 },     /* q0: signed 16-bit int range */
	};
	for (size_t i = 0; i < sizeof(oor) / sizeof(oor[0]); i++) {
		uint8_t obuf[64];
		bvnr_sink_t sk;
		bvnr_writer_t *ww = make_writer(obuf, sizeof(obuf), &sk);
		ASSERT_NOT_NULL(ww, "make_writer must succeed");
		if (!ww) continue;
		ASSERT_FALSE(bvnr_write_float_fix(ww, "x", oor[i].w, oor[i].q, oor[i].v),
					 "out-of-range float_fix value must be rejected");
		ASSERT_EQ_INT(bvnr_writer_get_error(ww), error_value_out_of_range,
					  "rejection must be error_value_out_of_range");
		bvnr_writer_destroy(ww);
	}
}

static void test_write_float_dec_roundtrip(void)
{
	printf("  test_write_float_dec_roundtrip...\n");

	uint8_t output[256];
	bvnr_sink_t sink;
	bvnr_writer_t *w = make_writer(output, sizeof(output), &sink);
	ASSERT_NOT_NULL(w, "make_writer must succeed");
	if (!w) return;

	ASSERT_TRUE(bvnr_write_float_dec(w, "fd", 64, 3.14),
				"write float_dec:64 must succeed");
	ASSERT_TRUE(bvnr_write_float_dec(w, "small", 32, -1.5e-3f),
				"write float_dec:32 negative must succeed");
	ASSERT_TRUE(bvnr_write_finish(w), "finish must succeed");
	ASSERT_EQ_INT(bvnr_writer_get_error(w), error_none, "no error");

	last_event_t le = {0};
	ASSERT_TRUE(roundtrip(output, bvnr_writer_bytes_written(w), &le),
				"float_dec roundtrip must parse cleanly");
	ASSERT_TRUE(le.vt.family == vt_float_dec, "last type family = float_dec");
	ASSERT_EQ_UINT(le.vt.width, 32, "last width = 32");
	ASSERT_TRUE(le.value_tok == token_is_number,
				"float_dec data token is token_is_number");

	bvnr_writer_destroy(w);
}

static void test_validate_reference(void)
{
	printf("  test_validate_reference...\n");

	ASSERT_TRUE(bvn_validate_reference(".host"),
				"'.host' is a valid reference");
	ASSERT_TRUE(bvn_validate_reference(".config.host"),
				"'.config.host' is a valid multi-segment reference");
	ASSERT_TRUE(bvn_validate_reference(".a.b.c"),
				"'.a.b.c' is a valid three-segment reference");
	ASSERT_TRUE(bvn_validate_reference("._under_score"),
				"'._under_score' is valid");

	ASSERT_FALSE(bvn_validate_reference("host"),
				 "'host' without leading dot must be rejected");
	ASSERT_FALSE(bvn_validate_reference(""),
				 "empty string must be rejected");
	ASSERT_FALSE(bvn_validate_reference(NULL),
				 "NULL must be rejected");
	ASSERT_FALSE(bvn_validate_reference("."),
				 "'.' (dot only, no id-start) must be rejected");
	ASSERT_FALSE(bvn_validate_reference("..host"),
				 "'..host' (double dot at start) must be rejected");
	ASSERT_FALSE(bvn_validate_reference(".host."),
				 "'.host.' (trailing dot, no id-start) must be rejected");
}

static void test_write_reference_roundtrip(void)
{
	printf("  test_write_reference_roundtrip...\n");

	uint8_t output[4096];
	bvnr_sink_t sink;
	bvnr_writer_t *w = make_writer(output, sizeof(output), &sink);
	ASSERT_NOT_NULL(w, "make_writer must succeed");
	if (!w) return;

	bvnr_data_t ka = {
		.type   = token_is_identifier,
		.data   = "x",
		.length = 1,
	};
	ASSERT_TRUE(bvnr_write_event(w, ev_assignment_start, &ka),
				"assignment_start must succeed");

	bvnr_data_t kd = {
		.type   = token_is_reference,
		.data   = ".host",
		.length = 5,
	};
	ASSERT_TRUE(bvnr_write_event(w, ev_data, &kd),
				"writing valid reference '.host' must succeed");

	ASSERT_TRUE(bvnr_write_finish(w), "finish must succeed");

	uint64_t n = bvnr_writer_bytes_written(w);
	ASSERT_TRUE(n > 0, "bytes_written > 0");

	last_event_t le = {0};
	ASSERT_TRUE(roundtrip(output, n, &le),
				"reference roundtrip must parse cleanly");
	ASSERT_TRUE(strcmp(le.key, "x") == 0, "last key == x");
	ASSERT_TRUE(strcmp(le.value, ".host") == 0,
				"last value == .host (with leading dot)");
	ASSERT_EQ_INT(le.value_tok, token_is_reference,
				  "token type must be token_is_reference");

	bvnr_writer_destroy(w);
}

static void test_write_reference_without_dot_rejected(void)
{
	printf("  test_write_reference_without_dot_rejected...\n");

	uint8_t output[256];
	bvnr_sink_t sink;
	bvnr_writer_t *w = make_writer(output, sizeof(output), &sink);
	ASSERT_NOT_NULL(w, "make_writer must succeed");
	if (!w) return;

	bvnr_data_t ka = {
		.type   = token_is_identifier,
		.data   = "x",
		.length = 1,
	};
	bvnr_write_event(w, ev_assignment_start, &ka);

	bvnr_data_t kd = {
		.type   = token_is_reference,
		.data   = "host",
		.length = 4,
	};
	ASSERT_FALSE(bvnr_write_event(w, ev_data, &kd),
				 "writing reference without leading dot must be rejected");
	ASSERT_EQ_INT(bvnr_writer_get_error(w), error_type_value_mismatch,
				  "error must be type_value_mismatch");

	bvnr_writer_destroy(w);
}

static void test_write_reference_dot_only_rejected(void)
{
	printf("  test_write_reference_dot_only_rejected...\n");

	uint8_t output[256];
	bvnr_sink_t sink;
	bvnr_writer_t *w = make_writer(output, sizeof(output), &sink);
	ASSERT_NOT_NULL(w, "make_writer must succeed");
	if (!w) return;

	bvnr_data_t ka = {
		.type   = token_is_identifier,
		.data   = "x",
		.length = 1,
	};
	bvnr_write_event(w, ev_assignment_start, &ka);

	bvnr_data_t kd = {
		.type   = token_is_reference,
		.data   = ".",
		.length = 1,
	};
	ASSERT_FALSE(bvnr_write_event(w, ev_data, &kd),
				 "writing reference '.' (dot only) must be rejected");
	ASSERT_EQ_INT(bvnr_writer_get_error(w), error_type_value_mismatch,
				  "error must be type_value_mismatch");

	bvnr_writer_destroy(w);
}

static void test_write_string_vf_escapes(void)
{
	printf("  test_write_string_vf_escapes...\n");

	uint8_t output[4096];
	bvnr_sink_t sink;
	bvnr_writer_t *w = make_writer(output, sizeof(output), &sink);
	ASSERT_NOT_NULL(w, "make_writer must succeed");
	if (!w) return;

	char vf_str[] = {'v', 't', '\x0b', 'f', 'f', '\x0c', '\0'};
	ASSERT_TRUE(bvnr_write_string(w, "s", vf_str),
				"write string with \\v and \\f must succeed");
	ASSERT_TRUE(bvnr_write_finish(w), "finish must succeed");

	uint64_t n = bvnr_writer_bytes_written(w);
	ASSERT_TRUE(n > 0, "bytes_written > 0");

	last_event_t le = {0};
	ASSERT_TRUE(roundtrip(output, n, &le),
				"string with \\v and \\f must roundtrip cleanly");
	ASSERT_EQ_INT(le.value_len, (int64_t)strlen(vf_str),
				  "roundtripped string length matches");
	ASSERT_TRUE(memcmp(le.value, vf_str, le.value_len) == 0,
				"roundtripped string content matches (\\v and \\f preserved)");

	bvnr_writer_destroy(w);
}

static void test_write_version_directive(void)
{
	printf("  test_write_version_directive...\n");

	/* explicit bvnr_write_version before any value, round-tripped through the reader */
	uint8_t out[4096];
	bvnr_sink_t sink;
	bvnr_sink_to_mem(&sink, out, sizeof(out));
	bvnr_writer_t *w = bvnr_writer_create();
	ASSERT_NOT_NULL(w, "writer create");
	if (!w) return;
	ASSERT_TRUE(bvnr_open_write_sink(w, &sink, true, NULL), "open");
	ASSERT_TRUE(bvnr_write_version(w, 1, 1), "write_version before any value");
	ASSERT_TRUE(bvnr_write_uint(w, "x", 8, 7), "write a value after the directive");
	ASSERT_TRUE(bvnr_write_finish(w), "finish");
	uint64_t n = bvnr_writer_bytes_written(w);
	ASSERT_TRUE(n > 13 && memcmp(out, "#!bovnar 1.1\n", 13) == 0,
		"output begins with the version directive");
	uint16_t maj = 0, min = 0;
	ASSERT_TRUE(bvnr_peek_version(out, n, &maj, &min) && maj == 1 && min == 1,
		"emitted directive is re-readable");
	bvnr_writer_destroy(w);

	/* calling bvnr_write_version once output has begun is rejected */
	bvnr_sink_to_mem(&sink, out, sizeof(out));
	w = bvnr_writer_create();
	ASSERT_NOT_NULL(w, "writer create 2");
	if (!w) return;
	ASSERT_TRUE(bvnr_open_write_sink(w, &sink, true, NULL), "open 2");
	ASSERT_TRUE(bvnr_write_uint(w, "x", 8, 1), "write a value first");
	ASSERT_FALSE(bvnr_write_version(w, 1, 1), "write_version after output is rejected");
	ASSERT_EQ_INT(bvnr_writer_get_error(w), error_invalid_argument,
		"late write_version sets error_invalid_argument");
	bvnr_writer_destroy(w);

	/* emit_version write flag stamps the current spec version on open */
	bvnr_sink_to_mem(&sink, out, sizeof(out));
	w = bvnr_writer_create();
	ASSERT_NOT_NULL(w, "writer create 3");
	if (!w) return;
	bvnr_write_flags_t wf = {0};
	wf.emit_version = true;
	ASSERT_TRUE(bvnr_open_write_sink(w, &sink, true, &wf), "open with emit_version");
	ASSERT_TRUE(bvnr_write_uint(w, "x", 8, 1), "write a value");
	ASSERT_TRUE(bvnr_write_finish(w), "finish 3");
	maj = min = 0;
	ASSERT_TRUE(bvnr_peek_version(out, bvnr_writer_bytes_written(w), &maj, &min) &&
		maj == BVNR_SPEC_VERSION_MAJOR && min == BVNR_SPEC_VERSION_MINOR,
		"emit_version stamps the current spec version");
	bvnr_writer_destroy(w);
}

static void test_write_datetime(void)
{
	printf("  test_write_datetime...\n");
	uint8_t out[512];
	bvnr_sink_t sink;
	bvnr_sink_to_mem(&sink, out, sizeof(out));
	bvnr_writer_t *w = bvnr_writer_create();
	ASSERT_NOT_NULL(w, "writer create");
	if (!w) return;
	ASSERT_TRUE(bvnr_open_write_sink(w, &sink, false, NULL), "open");
	ASSERT_TRUE(bvnr_write_version(w, 1, 1), "version directive");
	/* the epoch must survive the direct write API (regression: it was dropped) */
	ASSERT_TRUE(bvnr_write_datetime(w, "a", 64, "gps", 1750000000),
		"write datetime gps");
	ASSERT_TRUE(bvnr_write_datetime(w, "b", 0, NULL, -100),
		"write datetime unix default, negative");
	ASSERT_TRUE(bvnr_write_finish(w), "finish");
	uint64_t n = bvnr_writer_bytes_written(w);
	out[n < sizeof(out) ? n : sizeof(out) - 1] = '\0';
	ASSERT_TRUE(strstr((char *)out, "<datetime:64,gps>") != NULL,
		"epoch 'gps' is emitted in the annotation");
	ASSERT_TRUE(strstr((char *)out, "<datetime>") != NULL,
		"unix default epoch stays implicit");
	/* the produced document re-parses (annotation + directive are consistent) */
	last_event_t le = {0};
	ASSERT_TRUE(roundtrip(out, n, &le), "datetime document round-trips");
	bvnr_writer_destroy(w);

	/* an unknown epoch name is rejected */
	bvnr_sink_to_mem(&sink, out, sizeof(out));
	w = bvnr_writer_create();
	if (w) {
		bvnr_open_write_sink(w, &sink, false, NULL);
		ASSERT_FALSE(bvnr_write_datetime(w, "x", 64, "nope", 1),
			"unknown epoch name is rejected");
		bvnr_writer_destroy(w);
	}
	ASSERT_EQ_INT(bvnr_datetime_epoch_index("tai"), 1, "epoch index tai == 1");
	ASSERT_EQ_INT(bvnr_datetime_epoch_index(NULL), 0, "NULL epoch == unix (0)");
	ASSERT_EQ_INT(bvnr_datetime_epoch_index("bogus"), -1, "unknown epoch == -1");

	/* the writer must reject a datetime the reader would reject (no
	 * writer/reader asymmetry): out-of-range value and over-wide carrier. */
	{
		uint8_t b2[128]; bvnr_sink_t s2;
		struct { uint32_t w; const char *ep; int64_t v; const char *why; } bad[] = {
			{ 8,  NULL,   300,   "datetime:8 300 over signed range" },
			{ 8,  "unix", -200,  "datetime:8 -200 under signed range" },
			{ 16, NULL,   32768, "datetime:16 32768 over signed range" },
			{ 33000, NULL, 1,    "datetime:33000 width over BVN_MAX_INT_WIDTH" },
		};
		for (size_t i = 0; i < sizeof(bad)/sizeof(bad[0]); i++) {
			bvnr_sink_to_mem(&s2, b2, sizeof(b2));
			bvnr_writer_t *w2 = bvnr_writer_create();
			if (!w2) continue;
			bvnr_open_write_sink(w2, &s2, false, NULL);
			ASSERT_FALSE(bvnr_write_datetime(w2, "x", bad[i].w, bad[i].ep, bad[i].v),
				bad[i].why);
			bvnr_writer_destroy(w2);
		}
		/* in-range still accepted */
		bvnr_sink_to_mem(&s2, b2, sizeof(b2));
		bvnr_writer_t *w3 = bvnr_writer_create();
		if (w3) {
			bvnr_open_write_sink(w3, &s2, false, NULL);
			ASSERT_TRUE(bvnr_write_datetime(w3, "x", 8, NULL, 127),
				"datetime:8 127 is in range");
			bvnr_writer_destroy(w3);
		}
	}
}

/*
 * spec 1.1 — the writer re-emits a datetime carrying captured ISO sub-second
 * digits (bvnr_data_t.frac_data) as an ISO literal so it round-trips. These
 * cases drive the raw event API directly to cover the hardening guards that the
 * reader path can never reach: a fraction with non-digit bytes, an atomic GNSS
 * epoch (which rejects literals), and a near-INT64_MIN tai carrier whose civil
 * conversion overflows (the converter leaves *dt untouched). All three must
 * fall back to the plain integer carrier — never a corrupt literal or a crash.
 */
static void emit_dt_value(uint8_t *out, uint32_t cap, uint32_t *outlen,
	uint32_t epoch_base, const char *carrier,
	const char *frac, uint32_t frac_len)
{
	bvnr_sink_t sink;
	bvnr_writer_t *w = make_writer(out, cap, &sink);
	if (!w) { *outlen = 0; return; }
	const char *key = "t";
	bvnr_data_t d = { .type = token_is_identifier,
	                  .data = key, .length = 1 };
	bvnr_write_event(w, ev_assignment_start, &d);
	value_type_spec_t vt = { .family = vt_datetime, .width = 64,
	                         .base = epoch_base };
	d = (bvnr_data_t){ .type = token_is_number, .value_type = vt,
	                   .data = carrier, .length = (uint32_t)strlen(carrier),
	                   .frac_data = frac, .frac_length = frac_len };
	bvnr_write_event(w, ev_data, &d);
	bvnr_write_finish(w);
	*outlen = (uint32_t)bvnr_writer_bytes_written(w);
	if (*outlen < cap) out[*outlen] = '\0';
	bvnr_writer_destroy(w);
}
static void test_write_datetime_fraction_guards(void)
{
	printf("  test_write_datetime_fraction_guards...\n");
	uint8_t out[4096];
	uint32_t n;

	/* 1. valid unix fractional datetime -> re-emitted as an ISO literal */
	emit_dt_value(out, sizeof out, &n, 0, "1781524800", "5", 1);
	ASSERT_TRUE(strstr((char *)out, "2026-06-15T12:00:00.5Z") != NULL,
		"valid fraction re-emits as an ISO literal");

	/* 2. a fraction with non-digit bytes must NOT produce a corrupt literal;
	 *    fall back to the integer carrier. */
	emit_dt_value(out, sizeof out, &n, 0, "1781524800", "5Z", 2);
	ASSERT_TRUE(strstr((char *)out, "1781524800") != NULL &&
	            strstr((char *)out, "2026-") == NULL,
		"non-digit fraction falls back to the integer carrier");

	/* 3. an atomic GNSS epoch (gps=2) rejects literals on read, so a fraction
	 *    must fall back to the integer carrier rather than emit one. */
	emit_dt_value(out, sizeof out, &n, 2, "1781524800", "5", 1);
	ASSERT_TRUE(strstr((char *)out, "1781524800") != NULL &&
	            strstr((char *)out, "2026-") == NULL,
		"GNSS-epoch fraction falls back to the integer carrier");

	/* 4. a near-INT64_MIN tai carrier whose civil conversion overflows must not
	 *    read uninitialised civil fields; it falls back to the integer carrier. */
	emit_dt_value(out, sizeof out, &n, 1, "-9223372036854775808", "5", 1);
	ASSERT_TRUE(strstr((char *)out, "-9223372036854775808") != NULL,
		"tai overflow falls back to the integer carrier (no UB)");
}
/*
 * The canon-observer path drives the serializer with no validation (unlike the
 * bvnr_write_* API, which validates first). An unbalanced ev_struct_end — no
 * matching ev_struct_start, so struct_depth is 0 — must be self-rejected: without
 * the guard in bvn_ser_serialize_event, indent/struct_depth would underflow and
 * the next bvn_ser_indent would emit ~4 billion tab bytes. This is the only path
 * that reaches that guard, so it is exercised here directly. Pretty mode is on so
 * indentation is actually emitted: without the guard the run would flood the
 * sink, so bytes_written==0 (not the on_event return alone) is what pins it.
 */
static void test_canon_observer_unbalanced_struct_end_rejected(void)
{
	uint8_t buf[256];
	bvnr_sink_t sink;
	bvnr_sink_to_mem(&sink, buf, sizeof buf);

	bvnr_canon_observer_t *obs = bvnr_canon_observer_create(&sink, true);
	ASSERT_NOT_NULL(obs, "canon observer create");
	if (!obs) return;

	bvnr_data_t data;
	memset(&data, 0, sizeof data);
	ASSERT_FALSE(bvnr_canon_observer_on_event(obs, ev_struct_end, &data),
		"unbalanced struct_end must be rejected, not underflow");
	ASSERT_TRUE(bvnr_sink_bytes_written(&sink) == 0,
		"rejected struct_end must not emit any bytes");

	bvnr_canon_observer_destroy(obs);
}
/*
 * A canonicalising observer must be able to re-emit a document's version
 * directive, or a spec-1.1 document (datetime, the new escapes, …)
 * canonicalises to output a reader then rejects as 1.0. The realistic timing
 * (mirrored here) is that the driver learns the declared version only AFTER the
 * leading ev_stream_start event — a reader resolves it while parsing the
 * directive line — but still before any value bytes; the observer emits the
 * directive lazily ahead of the first event that follows.
 */
static void test_canon_observer_emits_version(void)
{
	printf("  test_canon_observer_emits_version...\n");
	uint8_t out[256];
	memset(out, 0, sizeof out);
	bvnr_sink_t sink;
	bvnr_sink_to_mem(&sink, out, sizeof out);
	bvnr_canon_observer_t *obs = bvnr_canon_observer_create(&sink, false);
	ASSERT_NOT_NULL(obs, "observer create");
	if (!obs) return;

	bvnr_data_t data;
	memset(&data, 0, sizeof data);

	/* The observer copies the sink, so its byte counter lives on its private
	 * copy; read the shared output buffer directly instead. ev_stream_start
	 * emits no value bytes, so the buffer holds only the directive (ASCII, no
	 * embedded NUL), which makes strlen a valid length here. */

	/* stream-start arrives before the version is known: nothing is emitted */
	ASSERT_TRUE(bvnr_canon_observer_on_event(obs, ev_stream_start, &data),
		"stream-start accepted");
	ASSERT_TRUE(out[0] == 0, "no output before the version is known");

	/* the driver now learns the declared version (still ahead of any value) */
	bvnr_canon_observer_set_version(obs, 1, 1);
	ASSERT_TRUE(bvnr_canon_observer_on_event(obs, ev_stream_start, &data),
		"next event accepted");
	ASSERT_TRUE(memcmp(out, "#!bovnar 1.1\n", 13) == 0,
		"observer prepends the version directive ahead of the first event");
	size_t n = strlen((const char *)out);
	ASSERT_TRUE(n == 13, "directive is exactly the one line");
	uint16_t maj = 0, min = 0;
	ASSERT_TRUE(bvnr_peek_version(out, (uint64_t)n, &maj, &min) && maj == 1 && min == 1,
		"emitted directive re-reads as 1.1");

	/* the directive is emitted exactly once even across further events */
	ASSERT_TRUE(bvnr_canon_observer_on_event(obs, ev_stream_start, &data),
		"further event accepted");
	ASSERT_TRUE(strlen((const char *)out) == 13, "directive emitted exactly once");

	/* a late set_version (after the directive went out) is a harmless no-op */
	bvnr_canon_observer_set_version(obs, 2, 0);
	ASSERT_TRUE(strlen((const char *)out) == 13,
		"set_version after emission does not re-emit");

	bvnr_canon_observer_destroy(obs);
}
int main(void)
{
	printf("Running bovnar_writer_test regression suite...\n");

	test_write_version_directive();
	test_write_datetime();
	test_write_datetime_fraction_guards();

	test_write_simple_strings();
	test_write_typed_integers();
	test_write_float_values();
	test_write_float_width_over_64_rejected();
	test_write_float_fix_roundtrip();
	test_write_float_fix_q_edges();
	test_write_float_dec_roundtrip();
	test_write_bool_and_null();
	test_write_with_units();
	test_write_struct();
	test_write_nested_struct();
	test_write_type_annotation_direct();
	test_write_keyword_symbol_rejected();
	test_write_utf8_width_rejected();
	test_write_buffer_exhaustion();
	test_write_multiple_assignments();
	test_write_sint_negative();
	test_write_bvnf_base10();
	test_write_bvnf_base16();
	test_write_bvni_decimal();
	test_write_bvni_hex();
	test_write_bvni_base64_signchar();
	test_write_streaming_flush();
	test_validate_reference();
	test_write_reference_roundtrip();
	test_write_reference_without_dot_rejected();
	test_write_reference_dot_only_rejected();
	test_write_string_vf_escapes();
	test_canon_observer_unbalanced_struct_end_rejected();
	test_canon_observer_emits_version();

	if (failures == 0) {
		printf("PASSED %d tests\n", tests);
		return 0;
	}

	fprintf(stderr, "FAILED %d of %d tests\n", failures, tests);
	return 1;
}
