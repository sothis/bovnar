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

/*
 * Dedicated suite for the reader's lossless read-time unit/base conversion hook
 * (bvnr_read_flags_t.want_unit). Split out of bovnar_reader_test.c: the feature
 * has enough surface — factor exactness, output bases, refusal modes, reader
 * reuse — to warrant its own binary rather than doubling the size of the general
 * reader regression suite.
 */

/* ── read-time unit conversion (bvnr_read_flags_t.want_unit) ─────────────── */

typedef struct {
	value_unit_t want;       /* unit to request */
	uint32_t     want_base;  /* output base (0 = keep native) */
	bool         request;    /* whether to request conversion at all */
	bool         identity;   /* target the value's OWN unit (pure base conv) */
	uint32_t     max_conv_len;   /* 0 = the library default */
	bool         allow_nonterminating;   /* deliver rational-only results */
	bool         saw_value;
	bool         converted;
	bool         had_text;   /* conv.text non-NULL (false = rational only) */
	bool         unv_seen;       /* on_unverified observed an ev_data value */
	bool         unv_converted;  /* ...and what it saw in data->converted */
	char         unv_text[512];  /* ...and the conversion text it could read */
	char         text[512];  /* copy of the exact converted string */
	uint32_t     base;
} want_ctx_t;

static bool want_unit_cb(void *userdata, const bvnr_data_t *data,
						 value_unit_t *want, uint32_t *want_base)
{
	want_ctx_t *c = userdata;
	if (!c->request)
		return false;        /* decline: deliver untouched */
	*want      = c->identity ? data->value_unit : c->want;
	*want_base = c->want_base;
	return true;
}

/*
 * want_unit runs ahead of BOTH value callbacks, so an on_unverified consumer
 * sees the same populated conv as on_verified does. That is a documented
 * property (include/bovnar.h, read/write API §1.10 and §1.4) rather than an
 * accident of emission order, so it is pinned here.
 */
static bool want_on_unverified(void *userdata, bvnr_event_t ev, bvnr_data_t *data)
{
	want_ctx_t *c = userdata;
	if (ev == ev_data && data &&
	    (data->type == token_is_number || data->type == token_is_string)) {
		c->unv_seen      = true;
		c->unv_converted = data->converted;
		if (data->converted && data->conv.text) {
			size_t n = data->conv.length < sizeof c->unv_text - 1
				 ? data->conv.length : sizeof c->unv_text - 1;
			memcpy(c->unv_text, data->conv.text, n);
			c->unv_text[n] = '\0';
		}
	}
	return true;
}

static bool want_on_verified(void *userdata, bvnr_event_t ev, bvnr_data_t *data)
{
	want_ctx_t *c = userdata;
	if (ev == ev_data && data &&
	    (data->type == token_is_number || data->type == token_is_string)) {
		c->saw_value = true;
		c->converted = data->converted;
		c->had_text  = data->converted && data->conv.text != NULL;
		if (data->converted)
			c->base = data->conv.base;
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
	flags.on_verified   = want_on_verified;
	flags.on_unverified = want_on_unverified;
	flags.want_unit     = want_unit_cb;
	flags.want_unit_allow_nonterminating = ctx->allow_nonterminating;
	flags.max_conversion_length          = ctx->max_conv_len;

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

static void test_want_unit_nonterminating_opt_in(void)
{
	/* Same 1 m → mile as above, but the caller can consume a rational: the value
	 * is delivered with conv.num/den exact and conv.text NULL, instead of killing
	 * the parse. km/h → m/s and °F → °C fall in this same very common class. */
	want_ctx_t ctx = {0};
	ctx.want                 = BVN_UNIT_NO_PREFIX(bu_mile);
	ctx.want_base            = 10;
	ctx.request              = true;
	ctx.allow_nonterminating = true;
	run_want(".d = 1 m;", &ctx, true, error_none);
	ASSERT_TRUE(ctx.converted, "want: non-terminating result still delivered");
	ASSERT_TRUE(!ctx.had_text, "want: non-terminating result has NULL text");
	ASSERT_EQ_INT(ctx.base, 10, "want: output base still reported");
}

static void test_want_unit_irrational_ignores_opt_in(void)
{
	/* The opt-in relaxes only the RENDERING limit. A π-based factor has no exact
	 * rational at all, so there is nothing to hand over and it must still abort. */
	want_ctx_t ctx = {0};
	ctx.want                 = BVN_UNIT_NO_PREFIX(bu_radian);
	ctx.request              = true;
	ctx.allow_nonterminating = true;
	run_want(".a = 90 °;", &ctx, false, error_unit_inexact);
}

static void test_want_unit_unrepresentable_value_is_error(void)
{
	/* A finite literal too extreme to build an exact rational from must not be
	 * quietly passed through in its ORIGINAL unit — the caller would read
	 * kilometres believing they were metres, the exact misread want_unit exists
	 * to prevent. */
	want_ctx_t ctx = {0};
	ctx.want    = BVN_UNIT_NO_PREFIX(bu_meter);
	ctx.request = true;
	run_want(".d = <float:64> 1e1000000 k~m;", &ctx, false,
			 error_value_out_of_range);
}

static void test_want_unit_special_floats_pass_through(void)
{
	/* nan/inf carry no finite value: nothing to convert, and no error either. */
	/* Annotated form: a bare `nan` keyword takes no inline unit suffix. */
	const char *docs[] = { ".d = <float:64,k~m> nan;",
			       ".d = <float:64,k~m> inf;",
			       ".d = <float:64,k~m> ninf;" };
	for (size_t i = 0; i < sizeof docs / sizeof docs[0]; i++) {
		want_ctx_t ctx = {0};
		ctx.want    = BVN_UNIT_NO_PREFIX(bu_meter);
		ctx.request = true;
		run_want(docs[i], &ctx, true, error_none);
		ASSERT_TRUE(ctx.saw_value, "want: special float still delivered");
		ASSERT_TRUE(!ctx.converted, "want: special float not converted");
	}
}

static void test_want_unit_high_bases(void)
{
	/* Every base bvnr can write is a valid output base, not just 2..36:
	 * 255 = 4·62+7 = "47"₆₂, = 3·64+63 = "D/"₆₄, = 3·85+0 = "$!"₈₅. */
	static const struct { uint32_t base; const char *text; } cases[] = {
		{ 36, "73" }, { 62, "47" }, { 64, "D/" }, { 85, "$!" },
	};
	for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
		want_ctx_t ctx = {0};
		ctx.want      = BVN_UNIT_NONE;
		ctx.want_base = cases[i].base;
		ctx.request   = true;
		run_want(".n = 255;", &ctx, true, error_none);
		ASSERT_TRUE(ctx.converted, "want: high-base conversion applied");
		ASSERT_TRUE(strcmp(ctx.text, cases[i].text) == 0,
					"want: high-base digits exact");
		ASSERT_EQ_INT(ctx.base, cases[i].base, "want: high base recorded");
	}
}

static void test_want_unit_native_high_base_kept(void)
{
	/* want_base 0 means "keep the value's own" — including a base above 36,
	 * which used to be silently rewritten to 10. */
	want_ctx_t ctx = {0};
	ctx.want      = BVN_UNIT_NO_PREFIX(bu_meter);
	ctx.want_base = 0;
	ctx.request   = true;
	run_want(".x = <uint:64,_62> \"zz\" m;", &ctx, true, error_none);
	ASSERT_TRUE(ctx.converted, "want: base-62 value converted");
	ASSERT_EQ_INT(ctx.base, 62, "want: native base 62 kept, not rewritten to 10");
	ASSERT_TRUE(strcmp(ctx.text, "zz") == 0, "want: base-62 digits round-trip");
}

static void test_want_unit_bad_base_is_error(void)
{
	/* 63 is not a base bvnr writes, and 99 is not a base at all. Substituting a
	 * default would hand back digits in a base the caller never asked for. */
	static const uint32_t bad[] = { 1u, 63u, 86u, 99u };
	for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
		want_ctx_t ctx = {0};
		ctx.want      = BVN_UNIT_NONE;
		ctx.want_base = bad[i];
		ctx.request   = true;
		run_want(".n = 255;", &ctx, false, error_invalid_argument);
	}
}

static void test_want_unit_signless_base_rejects_negative(void)
{
	/* Bases 64/85 spend '+'/'-' on digits and cannot spell a negative value. */
	want_ctx_t ctx = {0};
	ctx.want      = BVN_UNIT_NONE;
	ctx.want_base = 85;
	ctx.request   = true;
	run_want(".n = <sint:64> -255;", &ctx, false, error_invalid_argument);
}

static void test_want_unit_reader_reuse(void)
{
	/* bvnr_open_read_source advertises reader reuse, and re-arming the validator
	 * must release the previous document's conversion scratch. Run several
	 * documents through one reader and check each still converts correctly (a
	 * leak here is caught by the ASAN/valgrind runs of this same binary). */
	want_ctx_t ctx = {0};
	ctx.want    = BVN_UNIT_NO_PREFIX(bu_meter);
	ctx.request = true;

	bvnr_read_flags_t flags;
	memset(&flags, 0, sizeof(flags));
	flags.userdata    = &ctx;
	flags.on_verified = want_on_verified;
	flags.want_unit   = want_unit_cb;

	bvnr_reader_t *reader = bvnr_reader_create();
	ASSERT_TRUE(reader != NULL, "want reuse: reader_create");
	if (!reader) return;
	for (int i = 0; i < 4; i++) {
		const char *doc = ".d = 5 k~m;";
		ctx.converted = false;
		ctx.text[0]   = '\0';
		ASSERT_TRUE(bvnr_open_read_mem(reader, doc, (uint32_t)strlen(doc),
									   NULL, 0, &flags),
					"want reuse: reopen same reader");
		ASSERT_TRUE(bvnr_read(reader), "want reuse: read");
		ASSERT_TRUE(ctx.converted && strcmp(ctx.text, "5000") == 0,
					"want reuse: conversion still exact after reopen");
	}
	bvnr_reader_destroy(reader);
}

static void test_want_unit_corrected_factors(void)
{
	/* Units whose true SI factor is a NON-terminating rational used to ship a
	 * 17-digit double repr as their "exact" value, so they silently produced
	 * wrong answers flagged exact — while °F, hand-overridden to 5/9, correctly
	 * refused. Both halves of that contradiction are checked here. */

	/* 760 Torr is exactly 101325 Pa (1 Torr = 101325/760), not 101324.9999…. */
	want_ctx_t ctx = {0};
	ctx.want    = BVN_UNIT_NO_PREFIX(bu_pascal);
	ctx.request = true;
	run_want(".p = 760 Torr;", &ctx, true, error_none);
	ASSERT_TRUE(ctx.converted && strcmp(ctx.text, "101325") == 0,
				"want: 760 Torr → exactly 101325 Pa");

	/* 1 rpm is 1/60 Hz and 1 kn is 463/900 m/s: no terminating decimal, so both
	 * must refuse rather than round — exactly as °F → K does. */
	static const char *nonterminating[] = { ".f = 1 rpm;", ".f = 1 °F;" };
	value_unit_t targets[] = { BVN_UNIT_NO_PREFIX(bu_hertz),
				   BVN_UNIT_NO_PREFIX(bu_kelvin) };
	for (size_t i = 0; i < sizeof nonterminating / sizeof nonterminating[0]; i++) {
		want_ctx_t c = {0};
		c.want    = targets[i];
		c.request = true;
		run_want(nonterminating[i], &c, false, error_unit_inexact);
	}
}


static void test_want_unit_identity_is_always_exact(void)
{
	/* The documented pure-base-conversion request names the value's OWN unit
	 * precisely because it wants no unit change. That is the identity map, so it
	 * must succeed whatever the unit is — including one with no SI row (a
	 * currency) or an irrational factor (a π-based angle), which are refused for
	 * reasons that simply do not apply when from == to. Without an identity
	 * short-circuit the natural generic hook ("every number in base 16, keep its
	 * unit") could not read a document containing one angle or one price. */
	static const struct { const char *doc; uint32_t base; const char *text; } cases[] = {
		{ ".a = 90 °;",                        16, "5a"     },  /* irrational factor */
		{ ".a = 90 °;",                         0, "90"     },
		{ ".x = 1 arcmin;",                    16, "1"      },
		{ ".r = 2 rev;",                       16, "2"      },
		{ ".d = 3 pc;",                        16, "3"      },
		{ ".m = 5 Oe;",                        16, "5"      },
		{ ".p = <uint:64,$BTC> 100;",          16, "64"     },  /* no SI row at all */
		{ ".p = <float_dec:64,$USD> 100.25;",   0, "100.25" },
	};
	for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
		want_ctx_t ctx = {0};
		ctx.want      = BVN_UNIT_NONE;     /* replaced by the value's own below */
		ctx.want_base = cases[i].base;
		ctx.request   = true;
		ctx.identity  = true;
		run_want(cases[i].doc, &ctx, true, error_none);
		ASSERT_TRUE(ctx.converted, "want: identity conversion applied");
		ASSERT_TRUE(strcmp(ctx.text, cases[i].text) == 0,
					"want: identity conversion is the value itself");
	}
}

static void test_want_unit_underflow_is_not_zero(void)
{
	/* A literal whose exact rational will not fit the big-int budget is NOT
	 * zero. It used to be delivered as exactly "0" with converted == true —
	 * wrong by every digit, silently, from the path that advertises exactness.
	 * The symmetric overflow side always refused; so must this one. */
	static const char *unrepresentable[] = {
		".d = <float:64> 1.5e-20000 m;",
		".d = <float:64> -7e-30000 m;",
		".d = <float:1056> 1.5e-9865 m;",
	};
	for (size_t i = 0; i < sizeof unrepresentable / sizeof unrepresentable[0]; i++) {
		want_ctx_t ctx = {0};
		ctx.request  = true;
		ctx.identity = true;
		run_want(unrepresentable[i], &ctx, false, error_value_out_of_range);
	}
	/* Still small but representable, and within the work limit: converts, and is
	 * emphatically not "0". */
	{
		want_ctx_t ctx = {0};
		ctx.request  = true;
		ctx.identity = true;
		run_want(".d = <float:1056> 1.5e-100 m;", &ctx, true, error_none);
		ASSERT_TRUE(ctx.converted, "want: representable tiny value converts");
		ASSERT_TRUE(strcmp(ctx.text, "0") != 0,
					"want: representable tiny value is not rendered as 0");
		ASSERT_TRUE(strlen(ctx.text) > 100,
					"want: and it carries its full exact expansion");
	}
	/* Beyond the work limit is a DIFFERENT refusal from underflow: the value is
	 * perfectly representable, it is the cost of writing out its thousands of
	 * exact digits that the reader declines. Rendering is quadratic in the digit
	 * count and the count follows the exponent rather than the literal's length,
	 * so seven characters otherwise buy a tenth of a second of CPU. */
	{
		want_ctx_t ctx = {0};
		ctx.request  = true;
		ctx.identity = true;
		run_want(".d = <float:64> 1e-9800 m;", &ctx, false, error_value_out_of_range);
	}
	/* ...and raising the limit lets it through again. */
	{
		want_ctx_t ctx = {0};
		ctx.request    = true;
		ctx.identity   = true;
		ctx.max_conv_len = 20000;
		run_want(".d = <float:64> 1e-2000 m;", &ctx, true, error_none);
		ASSERT_TRUE(ctx.converted, "want: a raised limit admits a long expansion");
	}
	/* A literal that really is zero still converts to zero. */
	{
		want_ctx_t ctx = {0};
		ctx.request  = true;
		ctx.identity = true;
		run_want(".d = <float:64> 0.0 m;", &ctx, true, error_none);
		ASSERT_TRUE(ctx.converted && strcmp(ctx.text, "0") == 0,
					"want: a genuine zero still converts to 0");
	}
}


static void test_want_unit_unverified_sees_the_conversion(void)
{
	/* Documented: the hook runs after validation but before EITHER callback, so
	 * the two views of one value cannot disagree. An on_unverified consumer
	 * therefore observes the conversion too — everything else about that event
	 * is still the pre-validation view. */
	want_ctx_t ctx = {0};
	ctx.want    = BVN_UNIT_NO_PREFIX(bu_meter);
	ctx.request = true;
	run_want(".d = 5 k~m;", &ctx, true, error_none);
	ASSERT_TRUE(ctx.unv_seen, "want: on_unverified saw the value");
	ASSERT_TRUE(ctx.unv_converted,
				"want: on_unverified sees converted set, like on_verified");
	ASSERT_TRUE(strcmp(ctx.unv_text, "5000") == 0,
				"want: on_unverified reads the same exact text");
	ASSERT_TRUE(strcmp(ctx.unv_text, ctx.text) == 0,
				"want: both callbacks agree on the converted value");

	/* And with no hook installed, neither callback sees a conversion. */
	want_ctx_t plain = {0};
	plain.request = false;
	run_want(".d = 5 k~m;", &plain, true, error_none);
	ASSERT_TRUE(plain.unv_seen, "want: on_unverified saw the declined value");
	ASSERT_TRUE(!plain.unv_converted && !plain.converted,
				"want: no conversion means converted clear in both callbacks");
}

int main(void)
{
	printf("Running bovnar_want_unit_test regression suite...\n");
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
	test_want_unit_nonterminating_opt_in();
	test_want_unit_irrational_ignores_opt_in();
	test_want_unit_unrepresentable_value_is_error();
	test_want_unit_special_floats_pass_through();
	test_want_unit_high_bases();
	test_want_unit_native_high_base_kept();
	test_want_unit_bad_base_is_error();
	test_want_unit_signless_base_rejects_negative();
	test_want_unit_reader_reuse();
	test_want_unit_corrected_factors();
	test_want_unit_identity_is_always_exact();
	test_want_unit_underflow_is_not_zero();
	test_want_unit_unverified_sees_the_conversion();

	if (failures == 0) {
		printf("PASSED %d tests\n", tests);
		return 0;
	}
	fprintf(stderr, "FAILED %d of %d tests\n", failures, tests);
	return 1;
}
