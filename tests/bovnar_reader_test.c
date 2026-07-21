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
#include "bovnar.h"

static int failures = 0;
static int tests = 0;

#define ASSERT_TRUE(cond, msg) do {                                   \
	tests++;                                                         \
	if (!(cond)) {                                                   \
		fprintf(stderr, "FAIL line %d: %s\n", __LINE__, (msg));  \
		failures++;                                                  \
	}                                                                \
} while (0)

#define ASSERT_EQ_INT(a, b, msg) do {                                 \
	tests++;                                                         \
	int64_t _a = (int64_t)(a);                                       \
	int64_t _b = (int64_t)(b);                                       \
	if (_a != _b) {                                                  \
		fprintf(stderr, "FAIL line %d: %s\n  got %lld, expected %lld\n", \
				__LINE__, (msg), (long long)_a, (long long)_b);      \
		failures++;                                                  \
	}                                                                \
} while (0)

#define ASSERT_EQ_UINT(a, b, msg) do {                                \
	tests++;                                                         \
	uint64_t _a = (uint64_t)(a);                                     \
	uint64_t _b = (uint64_t)(b);                                     \
	if (_a != _b) {                                                  \
		fprintf(stderr, "FAIL line %d: %s\n  got %llu, expected %llu\n", \
				__LINE__, (msg), (unsigned long long)_a, (unsigned long long)_b); \
		failures++;                                                  \
	}                                                                \
} while (0)

typedef struct {
	uint32_t verified_events;
	uint32_t unverified_events;
	uint32_t error_callback_count;
	bool     saw_second_assignment;
	error_code_t last_error;
} reader_ctx_t;

static bool on_unverified(void *userdata, bvnr_event_t ev, bvnr_data_t *data)
{
	reader_ctx_t *ctx = userdata;
	ctx->unverified_events++;
	(void)ev;
	(void)data;
	return true;
}

static bool on_verified(void *userdata, bvnr_event_t ev, bvnr_data_t *data)
{
	reader_ctx_t *ctx = userdata;
	ctx->verified_events++;

	if (ev == ev_assignment_start && data && data->data && data->length > 0) {
		if (data->length == 6 && memcmp(data->data, "second", 6) == 0)
			ctx->saw_second_assignment = true;
	}

	return true;
}

static void on_error(void *userdata,
					 error_code_t err,
					 uint64_t line,
					 uint64_t column,
					 uint32_t byte_val,
					 uint64_t offset)
{
	reader_ctx_t *ctx = userdata;
	ctx->error_callback_count++;
	ctx->last_error = err;
	(void)line; (void)column; (void)byte_val; (void)offset;
}

static void run_reader(const char *payload,
					   bool continue_on_error,
					   error_code_t expected_error,
					   bool expect_recovery,
					   bool expect_second_assignment)
{
	reader_ctx_t ctx = {0};
	bvnr_read_flags_t flags;
	memset(&flags, 0, sizeof(flags));
	flags.userdata = &ctx;
	flags.on_unverified = on_unverified;
	flags.on_verified = on_verified;
	flags.on_error = on_error;
	flags.continue_on_error = continue_on_error;

	bvnr_reader_t *reader = bvnr_reader_create();
	ASSERT_TRUE(reader != NULL, "bvnr_reader_create must succeed");
	if (!reader) return;

	bool opened = bvnr_open_read_mem(reader, payload,
									 (uint32_t)strlen(payload),
									 NULL, 0, &flags);
	ASSERT_TRUE(opened, "bvnr_open_read_mem must open the payload");

	bool ok = false;
	if (opened)
		ok = bvnr_read(reader);

	error_code_t err = bvnr_reader_get_error(reader);
	uint64_t recovery = bvnr_reader_get_recovery_count(reader);

	if (expected_error == error_none) {
		ASSERT_TRUE(ok, "bvnr_read must succeed when no parse error is expected");
		ASSERT_EQ_INT(err, error_none, "reader must report error_none");
	} else if (continue_on_error) {
		ASSERT_TRUE(ok, "bvnr_read must succeed in continue_on_error mode even if errors are recovered");
		ASSERT_EQ_INT(err, error_none, "reader must report error_none after recovery in continue_on_error mode");
		ASSERT_TRUE(ctx.error_callback_count > 0, "error callback must fire for recovered parse errors");
		ASSERT_EQ_INT(ctx.last_error, expected_error, "error callback must report the expected error code");
	} else {
		ASSERT_TRUE(!ok, "bvnr_read must fail when parse error is expected without resync");
		ASSERT_EQ_INT(err, expected_error, "reader must report the expected error code");
	}

	if (expect_recovery)
		ASSERT_TRUE(recovery > 0, "recovery_count must be greater than zero when resync is expected");
	else
		ASSERT_EQ_UINT(recovery, 0, "recovery_count must be zero when no resync is expected");

	if (expect_second_assignment)
		ASSERT_TRUE(ctx.saw_second_assignment, "second assignment must be reached after resync");

	bvnr_reader_destroy(reader);
}

static void test_resync_on_error(void)
{
	const char *payload = ".first = 1; .broken = <float:64,_2> 1.0; .second = 2;";
	run_reader(payload,
			   true,
			   error_illegal_value_type,
			   true,
			   true);
}

/*
 * Regression: a semantic error (value out of range) is detected only once the
 * value's terminating ';' has been consumed, so the stream is already at a
 * clean assignment boundary. Recovery must resume at the *next* assignment
 * rather than skipping forward to the following ';' (which would swallow the
 * valid ".second"). See §13.2.
 */
static void test_resync_after_value_error_keeps_next(void)
{
	const char *payload = ".first = 1; .broken = <uint:8> 999; .second = 2;";
	run_reader(payload,
			   true,
			   error_value_out_of_range,
			   true,
			   true);   /* .second must survive recovery */
}

static void test_incomplete_stream(void)
{
	const char *payload = ".a = \"value";
	run_reader(payload,
			   false,
			   error_got_incomplete_bvnr_stream,
			   false,
			   false);
}

static void test_comment_inside_array(void)
{
	const char *payload = ".arr = [1, # comment\n 2, 3];";
	run_reader(payload,
			   false,
			   error_none,
			   false,
			   false);
}

static void test_bom_error_after_comment(void)
{
	const char *payload = "# comment\n\xEF\xBB\xBF.a = 1;";
	reader_ctx_t ctx = {0};
	bvnr_read_flags_t flags;
	memset(&flags, 0, sizeof(flags));
	flags.userdata = &ctx;
	flags.on_unverified = on_unverified;
	flags.on_verified = on_verified;
	flags.on_error = on_error;
	flags.continue_on_error = false;

	bvnr_reader_t *reader = bvnr_reader_create();
	ASSERT_TRUE(reader != NULL, "bvnr_reader_create must succeed");
	if (!reader) return;

	bool opened = bvnr_open_read_mem(reader, payload,
									 (uint32_t)strlen(payload),
									 NULL, 0, &flags);
	ASSERT_TRUE(opened, "bvnr_open_read_mem must open the BOM payload");

	bool ok = false;
	if (opened)
		ok = bvnr_read(reader);

	error_code_t err = bvnr_reader_get_error(reader);
	ASSERT_TRUE(!ok, "bvnr_read must fail for invalid BOM after comment");
	ASSERT_TRUE(err != error_none, "invalid BOM after comment must produce a parse error");

	bvnr_reader_destroy(reader);
}

/* ── read-time unit conversion (bvnr_read_flags_t.want_unit) ─────────────── */

typedef struct {
	value_unit_t want;       /* unit to request */
	uint32_t     want_base;  /* output base (0 = keep native) */
	bool         request;    /* whether to request conversion at all */
	bool         saw_value;
	bool         converted;
	char         text[512];  /* copy of the exact converted string */
	uint32_t     base;
} want_ctx_t;

static bool want_unit_cb(void *userdata, const bvnr_data_t *data,
						 value_unit_t *want, uint32_t *want_base)
{
	want_ctx_t *c = userdata;
	(void)data;
	if (!c->request)
		return false;        /* decline: deliver untouched */
	*want      = c->want;
	*want_base = c->want_base;
	return true;
}

static bool want_on_verified(void *userdata, bvnr_event_t ev, bvnr_data_t *data)
{
	want_ctx_t *c = userdata;
	if (ev == ev_data && data &&
	    (data->type == token_is_number || data->type == token_is_string)) {
		c->saw_value = true;
		c->converted = data->converted;
		if (data->converted && data->conv.text) {
			size_t n = data->conv.length < sizeof c->text - 1
				 ? data->conv.length : sizeof c->text - 1;
			memcpy(c->text, data->conv.text, n);
			c->text[n] = '\0';
			c->base = data->conv.base;
		}
	}
	return true;
}

static void run_want(const char *payload, want_ctx_t *ctx,
					 bool expect_ok, error_code_t expect_err)
{
	bvnr_read_flags_t flags;
	memset(&flags, 0, sizeof(flags));
	flags.userdata    = ctx;
	flags.on_verified = want_on_verified;
	flags.want_unit   = want_unit_cb;

	bvnr_reader_t *reader = bvnr_reader_create();
	ASSERT_TRUE(reader != NULL, "want: reader_create");
	if (!reader) return;

	bool opened = bvnr_open_read_mem(reader, payload,
									 (uint32_t)strlen(payload), NULL, 0, &flags);
	ASSERT_TRUE(opened, "want: open_read_mem");
	bool ok = opened && bvnr_read(reader);
	error_code_t err = bvnr_reader_get_error(reader);

	ASSERT_EQ_INT(ok, expect_ok, "want: read result matches expectation");
	ASSERT_EQ_INT(err, expect_err, "want: error code matches expectation");

	bvnr_reader_destroy(reader);
}

static void test_want_unit_multiplicative(void)
{
	/* 5 km delivered as metres → exactly "5000" */
	want_ctx_t ctx = {0};
	ctx.want    = BVN_UNIT_NO_PREFIX(bu_meter);
	ctx.request = true;
	run_want(".d = 5 k~m;", &ctx, true, error_none);
	ASSERT_TRUE(ctx.converted, "want: converted flag set");
	ASSERT_TRUE(strcmp(ctx.text, "5000") == 0, "want: 5 km → 5000 m exact");
}

static void test_want_unit_affine(void)
{
	/* 25 °C → K → exactly "298.15" */
	want_ctx_t ctx = {0};
	ctx.want    = BVN_UNIT_NO_PREFIX(bu_kelvin);
	ctx.request = true;
	run_want(".t = 25 °C;", &ctx, true, error_none);
	ASSERT_TRUE(ctx.converted, "want: affine converted");
	ASSERT_TRUE(strcmp(ctx.text, "298.15") == 0, "want: 25 °C → 298.15 K exact");
}

static void test_want_unit_incompatible_is_mismatch(void)
{
	want_ctx_t ctx = {0};
	ctx.want    = BVN_UNIT_NO_PREFIX(bu_second);
	ctx.request = true;
	run_want(".d = 5 k~m;", &ctx, false, error_unit_mismatch);
}

static void test_want_unit_irrational_is_inexact(void)
{
	/* degree → radian is π-based: no lossless conversion exists */
	want_ctx_t ctx = {0};
	ctx.want    = BVN_UNIT_NO_PREFIX(bu_radian);
	ctx.request = true;
	run_want(".a = 90 °;", &ctx, false, error_unit_inexact);
}

static void test_want_unit_nonterminating_is_inexact(void)
{
	/* 1 m → mile = 125/201168, no terminating base-10 expansion */
	want_ctx_t ctx = {0};
	ctx.want    = BVN_UNIT_NO_PREFIX(bu_mile);
	ctx.want_base = 10;
	ctx.request = true;
	run_want(".d = 1 m;", &ctx, false, error_unit_inexact);
}

static void test_want_unit_decline_passthrough(void)
{
	want_ctx_t ctx = {0};
	ctx.request = false;
	run_want(".d = 5 k~m;", &ctx, true, error_none);
	ASSERT_TRUE(ctx.saw_value, "want: value delivered when declined");
	ASSERT_TRUE(!ctx.converted, "want: converted flag clear when declined");
}

static void test_want_unit_pure_base_conversion(void)
{
	/* same unit, new base: 255 → base 16 "ff" (unit-less) */
	want_ctx_t ctx = {0};
	ctx.want      = BVN_UNIT_NONE;   /* no unit change */
	ctx.want_base = 16;
	ctx.request   = true;
	run_want(".n = 255;", &ctx, true, error_none);
	ASSERT_TRUE(ctx.converted, "want: unit-less value base-converted");
	ASSERT_TRUE(strcmp(ctx.text, "ff") == 0, "want: 255 → base16 ff");
	ASSERT_EQ_INT(ctx.base, 16, "want: output base recorded");
}

static void test_want_unit_string_carried_base(void)
{
	/* hex string carrier <uint:32,_16> "FF" == 255; 255 k~m → 255000 m exact.
	 * Ask for base-10 output (want_base 0 would keep the native base 16). */
	want_ctx_t ctx = {0};
	ctx.want      = BVN_UNIT_NO_PREFIX(bu_meter);
	ctx.want_base = 10;
	ctx.request   = true;
	run_want(".x = <uint:32,_16> \"FF\" k~m;", &ctx, true, error_none);
	ASSERT_TRUE(ctx.converted, "want: hex string-carried value converted");
	ASSERT_TRUE(strcmp(ctx.text, "255000") == 0, "want: 0xFF k~m → 255000 m");
}

static void test_want_unit_real_string_skipped(void)
{
	want_ctx_t ctx = {0};
	ctx.want    = BVN_UNIT_NO_PREFIX(bu_meter);
	ctx.request = true;
	run_want(".s = \"hello\";", &ctx, true, error_none);
	ASSERT_TRUE(ctx.saw_value, "want: string value still delivered");
	ASSERT_TRUE(!ctx.converted, "want: utf8 string not converted");
}

static void test_want_unit_multiprecision_integer(void)
{
	/* A >64-bit integer converts LOSSLESSLY: uint128 max × 1000, exact digits. */
	want_ctx_t ctx = {0};
	ctx.want    = BVN_UNIT_NO_PREFIX(bu_meter);
	ctx.request = true;
	run_want(".x = <uint:128> "
			 "340282366920938463463374607431768211455 k~m;",
			 &ctx, true, error_none);
	ASSERT_TRUE(ctx.converted, "want: multiprecision integer converted");
	ASSERT_TRUE(strcmp(ctx.text,
			   "340282366920938463463374607431768211455000") == 0,
				"want: uint128 max k~m → exact ×1000");
}

static void test_want_unit_wide_float(void)
{
	/* A 1056-bit binary float converts LOSSLESSLY: 70 significant digits
	 * survive k~m → m intact, far beyond what a double could carry. */
	want_ctx_t ctx = {0};
	ctx.want      = BVN_UNIT_NO_PREFIX(bu_meter);
	ctx.want_base = 10;
	ctx.request   = true;
	run_want(".d = <float:1056> "
			 "1.234567890123456789012345678901234567890"
			 "123456789012345678901234567891 k~m;",
			 &ctx, true, error_none);
	ASSERT_TRUE(ctx.converted, "want: wide float converted");
	ASSERT_TRUE(strcmp(ctx.text,
			   "1234.567890123456789012345678901234567890"
			   "123456789012345678901234567891") == 0,
				"want: 1056-bit float k~m → m exact ×1000");
}

int main(void)
{
	printf("Running bovnar_reader_test regression suite...\n");
	test_resync_on_error();
	test_resync_after_value_error_keeps_next();
	test_incomplete_stream();
	test_comment_inside_array();
	test_bom_error_after_comment();
	test_want_unit_multiplicative();
	test_want_unit_affine();
	test_want_unit_incompatible_is_mismatch();
	test_want_unit_irrational_is_inexact();
	test_want_unit_nonterminating_is_inexact();
	test_want_unit_decline_passthrough();
	test_want_unit_pure_base_conversion();
	test_want_unit_string_carried_base();
	test_want_unit_real_string_skipped();
	test_want_unit_multiprecision_integer();
	test_want_unit_wide_float();

	if (failures == 0) {
		printf("PASSED %d tests\n", tests);
		return 0;
	}

	fprintf(stderr, "FAILED %d of %d tests\n", failures, tests);
	return 1;
}
