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

	uint8_t output[256];
	bvnr_sink_t sink;
	bvnr_writer_t *w = make_writer(output, sizeof(output), &sink);
	ASSERT_NOT_NULL(w, "make_writer must succeed");
	if (!w) return;

	ASSERT_FALSE(bvnr_write_float(w, "x", 128, 3.14),
				 "bvnr_write_float with width=128 must fail");
	ASSERT_EQ_INT(bvnr_writer_get_error(w), error_illegal_value_type,
				  "error must be error_illegal_value_type for width=128");
	bvnr_writer_destroy(w);

	w = make_writer(output, sizeof(output), &sink);
	ASSERT_NOT_NULL(w, "make_writer (fix) must succeed");
	if (!w) return;
	ASSERT_FALSE(bvnr_write_float_fix(w, "x", 128, 10, 1.5),
				 "bvnr_write_float_fix with width=128 must fail");
	ASSERT_EQ_INT(bvnr_writer_get_error(w), error_illegal_value_type,
				  "float_fix width=128 error must be error_illegal_value_type");
	bvnr_writer_destroy(w);

	w = make_writer(output, sizeof(output), &sink);
	ASSERT_NOT_NULL(w, "make_writer (dec) must succeed");
	if (!w) return;
	ASSERT_FALSE(bvnr_write_float_dec(w, "x", 128, 2.5),
				 "bvnr_write_float_dec with width=128 must fail");
	ASSERT_EQ_INT(bvnr_writer_get_error(w), error_illegal_value_type,
				  "float_dec width=128 error must be error_illegal_value_type");
	bvnr_writer_destroy(w);

	w = make_writer(output, sizeof(output), &sink);
	ASSERT_NOT_NULL(w, "make_writer (w64) must succeed");
	if (!w) return;
	ASSERT_TRUE(bvnr_write_float(w, "x", 64, 3.14),
				"bvnr_write_float with width=64 must still succeed");
	ASSERT_EQ_INT(bvnr_writer_get_error(w), error_none,
				  "no error for width=64");
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

int main(void)
{
	printf("Running bovnar_writer_test regression suite...\n");

	test_write_simple_strings();
	test_write_typed_integers();
	test_write_float_values();
	test_write_float_width_over_64_rejected();
	test_write_float_fix_roundtrip();
	test_write_float_dec_roundtrip();
	test_write_bool_and_null();
	test_write_with_units();
	test_write_struct();
	test_write_nested_struct();
	test_write_type_annotation_direct();
	test_write_buffer_exhaustion();
	test_write_multiple_assignments();
	test_write_sint_negative();
	test_write_bvnf_base10();
	test_write_bvnf_base16();
	test_write_bvni_decimal();
	test_write_bvni_hex();
	test_write_streaming_flush();
	test_validate_reference();
	test_write_reference_roundtrip();
	test_write_reference_without_dot_rejected();
	test_write_reference_dot_only_rejected();
	test_write_string_vf_escapes();

	if (failures == 0) {
		printf("PASSED %d tests\n", tests);
		return 0;
	}

	fprintf(stderr, "FAILED %d of %d tests\n", failures, tests);
	return 1;
}
