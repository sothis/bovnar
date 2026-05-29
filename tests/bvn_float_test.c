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

#include <float.h>
#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bvn_float.h"
#include "bvn_float_impl.h"

static int g_fails = 0;
static int g_pass  = 0;

#define CHECK(cond, msg)                                                \
	do {                                                                \
		if (!(cond)) {                                                  \
			fprintf(stderr, "FAIL  line %d: %s\n", __LINE__, (msg));   \
			g_fails++;                                                  \
		} else {                                                        \
			g_pass++;                                                   \
		}                                                               \
	} while (0)

static uint32_t f32_bits(float f)   { uint32_t u; memcpy(&u, &f, 4); return u; }
static uint64_t f64_bits(double d)  { uint64_t u; memcpy(&u, &d, 8); return u; }

static bool f32_eq_strtof(const char *s)
{
	float ref = strtof(s, NULL);
	float got = bvn_float_parse_f32(s);
	if (isnan(ref) && isnan(got)) return true;
	return f32_bits(ref) == f32_bits(got);
}

static bool f64_eq_strtod(const char *s)
{
	double ref = strtod(s, NULL);
	double got = bvn_float_parse_f64(s);
	if (isnan(ref) && isnan(got)) return true;
	return f64_bits(ref) == f64_bits(got);
}

static void test_bigint_internals(void)
{
	puts("── BigInt internals ──────────────────────────────────────────");

	BVN_INT_LOCAL(a);
	BVN_INT_LOCAL(b);
	BVN_INT_LOCAL(q);
	BVN_INT_LOCAL(r);
	bvn_float_ctx_t ctx; bvn_float_ctx_init(&ctx);

	bvn_int_zero(&a);
	CHECK(bvn_int_is_zero(&a), "bvn_int_zero → is_zero");

	bvn_int_from_uint64(&a, 0);
	CHECK(bvn_int_is_zero(&a), "bvn_int_from_uint64(0) → is_zero");

	bvn_int_from_uint64(&a, 1);
	CHECK(!bvn_int_is_zero(&a), "bvn_int_from_uint64(1) → not zero");

	bvn_int_from_uint64(&a, UINT64_MAX);
	CHECK(a.nused == 2, "UINT64_MAX needs 2 words");
	CHECK(a.limbs[0] == 0xFFFFFFFFu && a.limbs[1] == 0xFFFFFFFFu, "UINT64_MAX words");

	bvn_int_from_uint64(&b, 0xDEADBEEF12345678ull);
	bvn_int_copy(&a, &b);
	CHECK(bvn_int_cmp(&a, &b) == 0, "bvn_int_copy produces identical BigInt");

	bvn_int_from_uint64(&a, 0xFFFFFFFFu);
	bvn_int_add_u32(&a, 1);
	CHECK(a.nused == 2 && a.limbs[0] == 0 && a.limbs[1] == 1, "add_u32 carry into word[1]");

	bvn_int_from_uint64(&a, 42);
	bvn_int_add_u32(&a, 0);
	{ BVN_INT_LOCAL(fortytwo); bvn_int_from_uint64(&fortytwo, 42);
	  CHECK(bvn_int_cmp(&a, &fortytwo) == 0, "add_u32(0) is identity"); }

	bvn_int_from_uint64(&a, 999999);
	bvn_int_mul_u32(&a, 0);
	CHECK(bvn_int_is_zero(&a), "mul_u32(0) yields zero");

	bvn_int_from_uint64(&a, 7);
	bvn_int_mul_u32(&a, 1);
	{ BVN_INT_LOCAL(seven); bvn_int_from_uint64(&seven, 7);
	  CHECK(bvn_int_cmp(&a, &seven) == 0, "mul_u32(1) is identity"); }

	bvn_int_from_uint64(&a, 0xFFFFFFFFu);
	bvn_int_mul_u32(&a, 2);
	{ BVN_INT_LOCAL(expected); bvn_int_from_uint64(&expected, (uint64_t)0xFFFFFFFFu * 2);
	  CHECK(bvn_int_cmp(&a, &expected) == 0, "mul_u32(2) carry"); }

	bvn_int_from_uint64(&a, 100);
	uint32_t rem = bvn_int_div_u32(&a, 7);
	{ BVN_INT_LOCAL(fourteen); bvn_int_from_uint64(&fourteen, 14);
	  CHECK(bvn_int_cmp(&a, &fourteen) == 0, "div_u32 quotient 100/7==14");
	  CHECK(rem == 2, "div_u32 remainder 100%7==2"); }

	bvn_int_from_uint64(&a, 0x12345678u);
	bvn_int_shl(&a, 16);
	bvn_int_shr(&a, 16);
	{ BVN_INT_LOCAL(orig); bvn_int_from_uint64(&orig, 0x12345678u);
	  CHECK(bvn_int_cmp(&a, &orig) == 0, "shl(16)/shr(16) round-trip"); }

	bvn_int_from_uint64(&b, 1);
	bvn_int_shr(&b, 1);
	CHECK(bvn_int_is_zero(&b), "shr 1>>1 → 0");

	bvn_int_from_uint64(&b, 0xFFFFFFFFu);
	bvn_int_shr(&b, 200);
	CHECK(bvn_int_is_zero(&b), "shr by >> n → 0");

	bvn_int_from_uint64(&a, 0);  CHECK(bvn_int_bitlen(&a) == 0, "bitlen(0)==0");
	bvn_int_from_uint64(&a, 1);  CHECK(bvn_int_bitlen(&a) == 1, "bitlen(1)==1");
	bvn_int_from_uint64(&a, 2);  CHECK(bvn_int_bitlen(&a) == 2, "bitlen(2)==2");
	bvn_int_from_uint64(&a, 3);  CHECK(bvn_int_bitlen(&a) == 2, "bitlen(3)==2");
	bvn_int_from_uint64(&a, 4);  CHECK(bvn_int_bitlen(&a) == 3, "bitlen(4)==3");
	bvn_int_from_uint64(&a, 255);CHECK(bvn_int_bitlen(&a) == 8, "bitlen(255)==8");
	bvn_int_from_uint64(&a, 256);CHECK(bvn_int_bitlen(&a) == 9, "bitlen(256)==9");

	bvn_int_from_uint64(&a, 10); bvn_int_from_uint64(&b, 20);
	CHECK(bvn_int_cmp(&a, &b) < 0,  "cmp 10 < 20");
	CHECK(bvn_int_cmp(&b, &a) > 0,  "cmp 20 > 10");
	CHECK(bvn_int_cmp(&a, &a) == 0, "cmp 10 == 10");
	bvn_int_from_uint64(&b, 10);
	CHECK(bvn_int_cmp(&a, &b) == 0, "cmp equal values across instances");

	bvn_int_zero(&a);
	bvn_int_setbit(&a, 0);  CHECK(bvn_int_getbit(&a, 0) == 1, "setbit/getbit 0");
	bvn_int_setbit(&a, 31); CHECK(bvn_int_getbit(&a, 31) == 1, "setbit/getbit 31");
	bvn_int_setbit(&a, 32); CHECK(bvn_int_getbit(&a, 32) == 1, "setbit/getbit 32 (cross word)");
	CHECK(bvn_int_getbit(&a, 1) == 0, "getbit 1 unset after setbit 0,31,32");

	bvn_int_from_uint64(&a, 100); bvn_int_from_uint64(&b, 37);
	bvn_int_sub_inplace(&a, &b);
	{ BVN_INT_LOCAL(sixtythree); bvn_int_from_uint64(&sixtythree, 63);
	  CHECK(bvn_int_cmp(&a, &sixtythree) == 0, "sub_inplace 100-37==63"); }

	bvn_int_from_uint64(&a, 17); bvn_int_from_uint64(&b, 5);
	bvn_int_divrem(&q, &r, &a, &b);
	{ BVN_INT_LOCAL(three); bvn_int_from_uint64(&three, 3);
	  BVN_INT_LOCAL(two);   bvn_int_from_uint64(&two,   2);
	  CHECK(bvn_int_cmp(&q, &three) == 0, "divrem 17/5 quotient==3");
	  CHECK(bvn_int_cmp(&r, &two)   == 0, "divrem 17/5 remainder==2"); }

	bvn_int_from_uint64(&a, 3); bvn_int_from_uint64(&b, 7);
	bvn_int_divrem(&q, &r, &a, &b);
	{ BVN_INT_LOCAL(three); bvn_int_from_uint64(&three, 3);
	  CHECK(bvn_int_is_zero(&q),              "divrem 3/7 quotient==0");
	  CHECK(bvn_int_cmp(&r, &three) == 0,    "divrem 3/7 remainder==3"); }

	bvn_int_from_uint64(&a, 1);
	bvn_int_mul_pow10(&a, 9);
	{ BVN_INT_LOCAL(billion); bvn_int_from_uint64(&billion, 1000000000u);
	  CHECK(bvn_int_cmp(&a, &billion) == 0, "mul_pow10(9) == 1e9"); }

	bvn_int_from_uint64(&a, 1);
	bvn_int_mul_pow10(&a, 18);
	{ BVN_INT_LOCAL(quad); bvn_int_from_uint64(&quad, 1000000000000000000ull);
	  CHECK(bvn_int_cmp(&a, &quad) == 0, "mul_pow10(18) == 1e18"); }

	bvn_float_clear_overflow(&ctx);
	bvn_int_from_uint64(&a, 1);
	bvni_shl(&ctx, &a, (int)(BVN_FLOAT_INT_WORDS * 32u - 1u));
	CHECK(!bvn_float_has_overflow(&ctx), "no overflow yet after placing MSB");
	bvni_shl(&ctx, &a, 1);
	CHECK(bvn_float_has_overflow(&ctx), "overflow after shift past BVN_FLOAT_INT_WORDS*32 bits");
	bvn_float_clear_overflow(&ctx);
	CHECK(!bvn_float_has_overflow(&ctx), "overflow cleared");

	bvn_float_clear_overflow(&ctx);
	bvn_int_zero(&a);
	a.nused = BVN_FLOAT_INT_WORDS;

	for (int i = 0; i < (int)BVN_FLOAT_INT_WORDS; i++) a.limbs[i] = 0xFFFFFFFFu;
	bvni_add_u32(&ctx, &a, 1);
	CHECK(bvn_float_has_overflow(&ctx), "overflow after add into fully-packed BigInt");
	bvn_float_clear_overflow(&ctx);
}

static void test_parse_basics(void)
{
	puts("── parser basics ─────────────────────────────────────────────");

	PNum p; bvn_float_pnum_init(&p);
	bvn_float_ctx_t ctx; bvn_float_ctx_init(&ctx);

	CHECK(!bvn_float_parse(&ctx, "",     &p), "empty string → false");
	CHECK(!bvn_float_parse(&ctx, "-",    &p), "bare minus → false");
	CHECK(!bvn_float_parse(&ctx, "+",    &p), "bare plus → false");
	CHECK(!bvn_float_parse(&ctx, "1e",   &p), "trailing 'e' with no digits → false");
	CHECK(!bvn_float_parse(&ctx, "e5",   &p), "leading 'e5' no mantissa → false");
	CHECK(!bvn_float_parse(&ctx, ".e5",  &p), "only dot before exponent → false");
	CHECK(!bvn_float_parse(&ctx, ".",    &p), "bare dot → false");

	CHECK(bvn_float_parse(&ctx, " 1",  &p) && !p.neg && !p.nan && !p.inf, "leading space");
	CHECK(bvn_float_parse(&ctx, "\t1", &p) && !p.neg && !p.nan && !p.inf, "leading tab");
	CHECK(bvn_float_parse(&ctx, "\r1", &p) && !p.neg && !p.nan && !p.inf, "leading CR");
	CHECK(bvn_float_parse(&ctx, "\n1", &p) && !p.neg && !p.nan && !p.inf, "leading LF");

	CHECK(bvn_float_parse(&ctx, "0",   &p) && bvn_int_is_zero(&p.coeff) && !p.neg, "0");
	CHECK(bvn_float_parse(&ctx, "0.0", &p) && bvn_int_is_zero(&p.coeff) && !p.neg, "0.0");
	CHECK(bvn_float_parse(&ctx, ".0",  &p) && bvn_int_is_zero(&p.coeff) && !p.neg, ".0");
	CHECK(bvn_float_parse(&ctx, "0e5", &p) && bvn_int_is_zero(&p.coeff) && !p.neg, "0e5");
	CHECK(bvn_float_parse(&ctx, "+0",  &p) && bvn_int_is_zero(&p.coeff) && !p.neg, "+0");
	CHECK(bvn_float_parse(&ctx, "00",  &p) && bvn_int_is_zero(&p.coeff),            "00");
	CHECK(bvn_float_parse(&ctx, "0.000000000000", &p) && bvn_int_is_zero(&p.coeff), "0.000...");

	CHECK(bvn_float_parse(&ctx, "-0",   &p) && bvn_int_is_zero(&p.coeff) && p.neg, "-0");
	CHECK(bvn_float_parse(&ctx, "-0.0", &p) && bvn_int_is_zero(&p.coeff) && p.neg, "-0.0");
	CHECK(bvn_float_parse(&ctx, "-0e9", &p) && bvn_int_is_zero(&p.coeff) && p.neg, "-0e9");

	CHECK(bvn_float_parse(&ctx, "inf",  &p) && p.inf && !p.neg, "inf");
	CHECK(bvn_float_parse(&ctx, "INF",  &p) && p.inf && !p.neg, "INF");
	CHECK(bvn_float_parse(&ctx, "Inf",  &p) && p.inf && !p.neg, "Inf");
	CHECK(bvn_float_parse(&ctx, "+inf", &p) && p.inf && !p.neg, "+inf");
	CHECK(bvn_float_parse(&ctx, "-inf", &p) && p.inf &&  p.neg, "-inf");
	CHECK(bvn_float_parse(&ctx, "-INF", &p) && p.inf &&  p.neg, "-INF");
	CHECK(bvn_float_parse(&ctx, "-Inf", &p) && p.inf &&  p.neg, "-Inf");

	CHECK(bvn_float_parse(&ctx, "nan",  &p) && p.nan && !p.neg, "nan");
	CHECK(bvn_float_parse(&ctx, "NaN",  &p) && p.nan && !p.neg, "NaN");
	CHECK(bvn_float_parse(&ctx, "NAN",  &p) && p.nan && !p.neg, "NAN");
	CHECK(bvn_float_parse(&ctx, "-nan", &p) && p.nan &&  p.neg, "-nan");
	CHECK(bvn_float_parse(&ctx, "+nan", &p) && p.nan && !p.neg, "+nan");
	CHECK(bvn_float_parse(&ctx, "-NAN", &p) && p.nan &&  p.neg, "-NAN");

	CHECK(bvn_float_parse(&ctx, "inf", &p) && bvn_int_is_zero(&p.coeff) && p.dex == 0,
		  "inf: coeff=0, dex=0");
	CHECK(bvn_float_parse(&ctx, "nan", &p) && bvn_int_is_zero(&p.coeff) && p.dex == 0,
		  "nan: coeff=0, dex=0");

	CHECK(bvn_float_parse(&ctx, "1",  &p) && !p.neg && !p.nan && !p.inf, "1 positive");
	CHECK(bvn_float_parse(&ctx, "-1", &p) &&  p.neg && !p.nan && !p.inf, "-1 negative");
	CHECK(bvn_float_parse(&ctx, "+1", &p) && !p.neg,                     "+1 non-negative");
	CHECK(bvn_float_parse(&ctx, "42", &p) && !p.neg && !p.nan && !p.inf, "42");

	CHECK(bvn_float_parse(&ctx, "0.5",  &p) && !p.nan && !p.inf, "0.5");
	CHECK(bvn_float_parse(&ctx, "-0.5", &p) && !p.nan && !p.inf && p.neg, "-0.5");
	CHECK(bvn_float_parse(&ctx, ".5",   &p) && !p.nan && !p.inf, ".5 (no leading zero)");
	CHECK(bvn_float_parse(&ctx, ".123456789012345678", &p) && !p.nan, "long fractional");

	CHECK(bvn_float_parse(&ctx, "1e0",   &p) && p.dex == 0,  "1e0");
	CHECK(bvn_float_parse(&ctx, "1e+0",  &p) && p.dex == 0,  "1e+0");
	CHECK(bvn_float_parse(&ctx, "1e-0",  &p) && p.dex == 0,  "1e-0");
	CHECK(bvn_float_parse(&ctx, "1e10",  &p) && p.dex == 10, "1e10");
	CHECK(bvn_float_parse(&ctx, "1e+10", &p) && p.dex == 10, "1e+10");
	CHECK(bvn_float_parse(&ctx, "1e-3",  &p) && p.dex == -3, "1e-3");
	CHECK(bvn_float_parse(&ctx, "1E5",   &p) && p.dex == 5,  "1E5 (uppercase E)");
	CHECK(bvn_float_parse(&ctx, "1E-5",  &p) && p.dex == -5, "1E-5");
	CHECK(bvn_float_parse(&ctx, "1e007", &p) && p.dex == 7,  "1e007 (leading zeros in exp)");
	CHECK(bvn_float_parse(&ctx, "1.5e2", &p) && !p.nan && !p.inf, "1.5e2");

	CHECK(bvn_float_parse(&ctx, "1e999999",  &p) && !p.nan, "1e999999 accepted");
	CHECK(bvn_float_parse(&ctx, "1e1000000", &p) && !p.nan, "1e1000000 accepted");

	CHECK(bvn_float_parse(&ctx, "0e9999", &p) && bvn_int_is_zero(&p.coeff), "0e9999 → zero coeff");

	CHECK(bvn_float_parse(&ctx, "1234567890", &p) && !p.nan && !p.inf, "10 integer digits");
	CHECK(bvn_float_parse(&ctx, "12345678901234567890", &p) && !p.nan, "20 integer digits");

	CHECK(bvn_float_parse(&ctx, "123456789.987654321e5", &p) && !p.nan, "long decimal with exp");
}

static void test_parse_normalization(void)
{
	puts("── coefficient normalisation ─────────────────────────────────");

	PNum p; bvn_float_pnum_init(&p);
	bvn_float_ctx_t ctx; bvn_float_ctx_init(&ctx);

	CHECK(bvn_float_parse(&ctx, "10", &p), "parse 10");
	{ BVN_INT_LOCAL(one); bvn_int_from_uint64(&one, 1);
	  CHECK(bvn_int_cmp(&p.coeff, &one) == 0, "10: coeff==1");
	  CHECK(p.dex == 1, "10: dex==1"); }

	CHECK(bvn_float_parse(&ctx, "100", &p), "parse 100");
	{ BVN_INT_LOCAL(one); bvn_int_from_uint64(&one, 1);
	  CHECK(bvn_int_cmp(&p.coeff, &one) == 0, "100: coeff==1");
	  CHECK(p.dex == 2, "100: dex==2"); }

	CHECK(bvn_float_parse(&ctx, "1000.000", &p), "parse 1000.000");
	{ BVN_INT_LOCAL(one); bvn_int_from_uint64(&one, 1);
	  CHECK(bvn_int_cmp(&p.coeff, &one) == 0, "1000.000: coeff==1");
	  CHECK(p.dex == 3, "1000.000: dex==3"); }

	CHECK(bvn_float_parse(&ctx, "1.50", &p), "parse 1.50");
	{ BVN_INT_LOCAL(fifteen); bvn_int_from_uint64(&fifteen, 15);
	  CHECK(bvn_int_cmp(&p.coeff, &fifteen) == 0, "1.50: coeff==15");
	  CHECK(p.dex == -1, "1.50: dex==-1"); }

	CHECK(bvn_float_parse(&ctx, "1.23", &p), "parse 1.23");
	{ BVN_INT_LOCAL(c); bvn_int_from_uint64(&c, 123);
	  CHECK(bvn_int_cmp(&p.coeff, &c) == 0, "1.23: coeff==123");
	  CHECK(p.dex == -2, "1.23: dex==-2"); }

	CHECK(bvn_float_parse(&ctx, "-10",  &p) && p.neg,  "-10 is negative after normalise");
	CHECK(bvn_float_parse(&ctx, "+100", &p) && !p.neg, "+100 is non-negative after normalise");

	CHECK(bvn_float_parse(&ctx, "1.0e3", &p), "parse 1.0e3");
	{
	  BVN_INT_LOCAL(one); bvn_int_from_uint64(&one, 1);
	  CHECK(bvn_int_cmp(&p.coeff, &one) == 0, "1.0e3: coeff==1");
	  CHECK(p.dex == 3, "1.0e3: dex==3"); }

	CHECK(bvn_float_parse(&ctx, "5e2", &p), "parse 5e2");
	{ BVN_INT_LOCAL(five); bvn_int_from_uint64(&five, 5);
	  CHECK(bvn_int_cmp(&p.coeff, &five) == 0, "5e2: coeff==5");
	  CHECK(p.dex == 2, "5e2: dex==2"); }
}

static void test_f16(void)
{
	puts("── binary16 (float16) conversion ─────────────────────────────");

	CHECK(bvn_float_parse_bin16("0")   == 0x0000u, "f16 +0");
	CHECK(bvn_float_parse_bin16("-0")  == 0x8000u, "f16 -0");
	CHECK(bvn_float_parse_bin16("0.0") == 0x0000u, "f16 0.0");

	CHECK(bvn_float_parse_bin16("1")     == 0x3C00u, "f16 +1.0");
	CHECK(bvn_float_parse_bin16("-1")    == 0xBC00u, "f16 -1.0");
	CHECK(bvn_float_parse_bin16("2")     == 0x4000u, "f16 +2.0");
	CHECK(bvn_float_parse_bin16("-2")    == 0xC000u, "f16 -2.0");
	CHECK(bvn_float_parse_bin16("4")     == 0x4400u, "f16 +4.0");
	CHECK(bvn_float_parse_bin16("0.5")   == 0x3800u, "f16 +0.5");
	CHECK(bvn_float_parse_bin16("-0.5")  == 0xB800u, "f16 -0.5");
	CHECK(bvn_float_parse_bin16("0.25")  == 0x3400u, "f16 +0.25");
	CHECK(bvn_float_parse_bin16("0.125") == 0x3000u, "f16 +0.125");

	CHECK(bvn_float_parse_bin16("1.5") == 0x3E00u, "f16 1.5");

	CHECK(bvn_float_parse_bin16("3")   == 0x4200u, "f16 3.0");

	CHECK(bvn_float_parse_bin16("inf")  == 0x7C00u, "f16 +inf");
	CHECK(bvn_float_parse_bin16("+inf") == 0x7C00u, "f16 +inf explicit");
	CHECK(bvn_float_parse_bin16("INF")  == 0x7C00u, "f16 INF");
	CHECK(bvn_float_parse_bin16("-inf") == 0xFC00u, "f16 -inf");

	{ uint16_t nb = bvn_float_parse_bin16("nan");
	  CHECK((nb & 0x7C00u) == 0x7C00u, "f16 nan: exp bits set");
	  CHECK((nb & 0x03FFu) != 0u,      "f16 nan: mantissa nonzero"); }
	{ uint16_t nb = bvn_float_parse_bin16("-nan");
	  CHECK((nb >> 15) == 1u,          "f16 -nan: sign bit");
	  CHECK((nb & 0x7C00u) == 0x7C00u, "f16 -nan: exp bits set"); }

	CHECK(bvn_float_parse_bin16("65504")  == 0x7BFFu, "f16 max finite 65504");
	CHECK(bvn_float_parse_bin16("-65504") == 0xFBFFu, "f16 -65504");

	CHECK(bvn_float_parse_bin16("65536")  == 0x7C00u, "f16 65536 → +inf");
	CHECK(bvn_float_parse_bin16("-65536") == 0xFC00u, "f16 -65536 → -inf");
	CHECK(bvn_float_parse_bin16("1e6")    == 0x7C00u, "f16 1e6 → +inf");

	CHECK(bvn_float_parse_bin16("6.103515625e-5") == 0x0400u,
		  "f16 smallest normal (2^-14)");

	CHECK(bvn_float_parse_bin16("6.0975551605224609375e-5") == 0x03FFu,
		  "f16 largest subnormal (0x03FF)");

	CHECK(bvn_float_parse_bin16("5.9604644775390625e-8") == 0x0001u,
		  "f16 smallest subnormal (2^-24)");

	{ uint16_t r = bvn_float_parse_bin16("0.333");
	  CHECK((r & 0x7C00u) == 0x3400u, "f16 ~0.333: exponent field == 13"); }
}

static void test_f32(void)
{
	puts("── float32 conversion ────────────────────────────────────────");

	CHECK(bvn_float_parse_bin32("0")   == 0x00000000u, "f32 +0");
	CHECK(bvn_float_parse_bin32("-0")  == 0x80000000u, "f32 -0");
	CHECK(bvn_float_parse_bin32("0.0") == 0x00000000u, "f32 0.0");

	CHECK(bvn_float_parse_bin32("1")    == 0x3F800000u, "f32 +1.0");
	CHECK(bvn_float_parse_bin32("-1")   == 0xBF800000u, "f32 -1.0");
	CHECK(bvn_float_parse_bin32("2")    == 0x40000000u, "f32 +2.0");
	CHECK(bvn_float_parse_bin32("0.5")  == 0x3F000000u, "f32 +0.5");
	CHECK(bvn_float_parse_bin32("0.25") == 0x3E800000u, "f32 +0.25");
	CHECK(bvn_float_parse_bin32("4")    == 0x40800000u, "f32 +4.0");
	CHECK(bvn_float_parse_bin32("8")    == 0x41000000u, "f32 +8.0");
	CHECK(bvn_float_parse_bin32("16")   == 0x41800000u, "f32 +16.0");

	CHECK(bvn_float_parse_bin32("inf")  == 0x7F800000u, "f32 +inf");
	CHECK(bvn_float_parse_bin32("+Inf") == 0x7F800000u, "f32 +Inf");
	CHECK(bvn_float_parse_bin32("INF")  == 0x7F800000u, "f32 INF");
	CHECK(bvn_float_parse_bin32("-inf") == 0xFF800000u, "f32 -inf");

	{ uint32_t nb = bvn_float_parse_bin32("nan");
	  CHECK((nb & 0x7F800000u) == 0x7F800000u, "f32 nan: exp bits");
	  CHECK((nb & 0x007FFFFFu) != 0u,           "f32 nan: mantissa nonzero"); }
	{ uint32_t nb = bvn_float_parse_bin32("-nan");
	  CHECK((nb >> 31) == 1u,                   "f32 -nan: sign bit");
	  CHECK((nb & 0x7F800000u) == 0x7F800000u,  "f32 -nan: exp bits"); }

	CHECK(f32_eq_strtof("0.1"),             "f32 0.1   vs strtof");
	CHECK(f32_eq_strtof("0.2"),             "f32 0.2   vs strtof");
	CHECK(f32_eq_strtof("0.3"),             "f32 0.3   vs strtof");
	CHECK(f32_eq_strtof("1.5"),             "f32 1.5   vs strtof");
	CHECK(f32_eq_strtof("-0.1"),            "f32 -0.1  vs strtof");
	CHECK(f32_eq_strtof("3.14159265"),      "f32 π     vs strtof");
	CHECK(f32_eq_strtof("2.71828182"),      "f32 e     vs strtof");
	CHECK(f32_eq_strtof("255"),             "f32 255   vs strtof");
	CHECK(f32_eq_strtof("1e10"),            "f32 1e10  vs strtof");
	CHECK(f32_eq_strtof("1e-10"),           "f32 1e-10 vs strtof");
	CHECK(f32_eq_strtof("1.5e2"),           "f32 1.5e2 vs strtof");
	CHECK(f32_eq_strtof("1.175494350822287507969e-38"), "f32 FLT_MIN vs strtof");
	CHECK(f32_eq_strtof("3.40282347e+38"),  "f32 FLT_MAX vs strtof");

	CHECK(bvn_float_parse_bin32("1e39")    == 0x7F800000u, "f32 1e39  → +inf");
	CHECK(bvn_float_parse_bin32("-1e39")   == 0xFF800000u, "f32 -1e39 → -inf");
	CHECK(bvn_float_parse_bin32("1e1000")  == 0x7F800000u, "f32 1e1000 → +inf");

	CHECK(f32_eq_strtof("1e-45"),   "f32 1e-45  (min subnormal) vs strtof");
	CHECK(f32_eq_strtof("1.4e-45"), "f32 1.4e-45 (near min sub) vs strtof");
	CHECK(f32_eq_strtof("1e-38"),   "f32 1e-38  (subnormal range) vs strtof");

	CHECK(f32_eq_strtof("1.00000001"),   "f32 1+ulp rounding vs strtof");
	CHECK(f32_eq_strtof("1.99999999"),   "f32 near 2 rounding vs strtof");
	CHECK(f32_eq_strtof("0.123456789"),  "f32 0.123456789 vs strtof");
	CHECK(f32_eq_strtof("12345678.9"),   "f32 large with fraction vs strtof");
	CHECK(f32_eq_strtof("1.23456789e15"),"f32 scientific rounding vs strtof");
}

static void test_f64(void)
{
	puts("── float64 conversion ────────────────────────────────────────");

	CHECK(bvn_float_parse_bin64("0")  == 0x0000000000000000ull, "f64 +0");
	CHECK(bvn_float_parse_bin64("-0") == 0x8000000000000000ull, "f64 -0");

	CHECK(bvn_float_parse_bin64("1")    == 0x3FF0000000000000ull, "f64 +1.0");
	CHECK(bvn_float_parse_bin64("-1")   == 0xBFF0000000000000ull, "f64 -1.0");
	CHECK(bvn_float_parse_bin64("2")    == 0x4000000000000000ull, "f64 +2.0");
	CHECK(bvn_float_parse_bin64("0.5")  == 0x3FE0000000000000ull, "f64 +0.5");
	CHECK(bvn_float_parse_bin64("0.25") == 0x3FD0000000000000ull, "f64 +0.25");
	CHECK(bvn_float_parse_bin64("0.125")== 0x3FC0000000000000ull, "f64 +0.125");
	CHECK(bvn_float_parse_bin64("4")    == 0x4010000000000000ull, "f64 +4.0");

	CHECK(bvn_float_parse_bin64("inf")  == 0x7FF0000000000000ull, "f64 +inf");
	CHECK(bvn_float_parse_bin64("-inf") == 0xFFF0000000000000ull, "f64 -inf");

	{ uint64_t nb = bvn_float_parse_bin64("nan");
	  CHECK((nb & 0x7FF0000000000000ull) == 0x7FF0000000000000ull,
			"f64 nan: exp bits set");
	  CHECK((nb & 0x000FFFFFFFFFFFFFull) != 0ull,
			"f64 nan: mantissa nonzero"); }

	CHECK(f64_eq_strtod("0.1"),   "f64 0.1   vs strtod");
	CHECK(f64_eq_strtod("0.2"),   "f64 0.2   vs strtod");
	CHECK(f64_eq_strtod("0.3"),   "f64 0.3   vs strtod");
	CHECK(f64_eq_strtod("1.5"),   "f64 1.5   vs strtod");
	CHECK(f64_eq_strtod("-0.1"),  "f64 -0.1  vs strtod");
	CHECK(f64_eq_strtod("3.141592653589793"),   "f64 π        vs strtod");
	CHECK(f64_eq_strtod("2.718281828459045"),   "f64 e        vs strtod");
	CHECK(f64_eq_strtod("1e100"),              "f64 1e100    vs strtod");
	CHECK(f64_eq_strtod("1e-100"),             "f64 1e-100   vs strtod");
	CHECK(f64_eq_strtod("1e308"),              "f64 1e308    vs strtod");
	CHECK(f64_eq_strtod("1.7976931348623157e+308"), "f64 DBL_MAX vs strtod");
	CHECK(f64_eq_strtod("2.2250738585072014e-308"), "f64 DBL_MIN vs strtod");
	CHECK(f64_eq_strtod("5e-324"),             "f64 min subnormal vs strtod");
	CHECK(f64_eq_strtod("1e-323"),             "f64 2×min-sub  vs strtod");
	CHECK(f64_eq_strtod("1.23456789012345678"), "f64 high precision vs strtod");

	CHECK(bvn_float_parse_bin64("1e309")  == 0x7FF0000000000000ull,
		  "f64 1e309 → +inf");
	CHECK(bvn_float_parse_bin64("-1e309") == 0xFFF0000000000000ull,
		  "f64 -1e309 → -inf");

	CHECK(f64_eq_strtod("1.000000000000000111"), "f64 rte near 1.0");
	CHECK(f64_eq_strtod("0.49999999999999994"),  "f64 rte near 0.5");
	CHECK(f64_eq_strtod("4503599627370497.0"),   "f64 2^52+1 exact int");
}

static void test_f128(void)
{
	puts("── float128 (binary128) conversion ──────────────────────────");

	uint32_t b[4];

	bvn_float_parse_bin128("0", b);
	CHECK(b[3] == 0 && b[2] == 0 && b[1] == 0 && b[0] == 0, "f128 +0");

	bvn_float_parse_bin128("-0", b);
	CHECK(b[3] == 0x80000000u && b[2] == 0 && b[1] == 0 && b[0] == 0, "f128 -0");

	bvn_float_parse_bin128("1", b);
	CHECK(b[3] == 0x3FFF0000u && b[2] == 0 && b[1] == 0 && b[0] == 0, "f128 +1.0");

	bvn_float_parse_bin128("-1", b);
	CHECK(b[3] == 0xBFFF0000u && b[2] == 0 && b[1] == 0 && b[0] == 0, "f128 -1.0");

	bvn_float_parse_bin128("2", b);
	CHECK(b[3] == 0x40000000u, "f128 +2.0");

	bvn_float_parse_bin128("0.5", b);
	CHECK(b[3] == 0x3FFE0000u && b[2] == 0 && b[1] == 0 && b[0] == 0, "f128 +0.5");

	bvn_float_parse_bin128("inf", b);
	CHECK(b[3] == 0x7FFF0000u && b[2] == 0 && b[1] == 0 && b[0] == 0, "f128 +inf");

	bvn_float_parse_bin128("-inf", b);
	CHECK(b[3] == 0xFFFF0000u, "f128 -inf");

	bvn_float_parse_bin128("nan", b);
	CHECK((b[3] & 0x7FFF0000u) == 0x7FFF0000u, "f128 nan: exp bits");
	CHECK((b[3] & 0x00008000u) != 0u,           "f128 nan: mantissa MSB");

	bvn_float_parse_bin128("-nan", b);
	CHECK((b[3] >> 31) == 1u,                   "f128 -nan: sign bit");
	CHECK((b[3] & 0x7FFF0000u) == 0x7FFF0000u,  "f128 -nan: exp bits");

	bvn_float_parse_bin128("1e1000000", b);
	CHECK(b[3] == 0x7FFF0000u, "f128 1e1000000 → +inf");
}

static void test_fixed_point(void)
{
	puts("── fixed-point conversion ────────────────────────────────────");

	CHECK(bvn_float_parse_fix16("0",    8) == 0,     "fix16 0 Q8");
	CHECK(bvn_float_parse_fix16("1",    8) == 256,   "fix16 1.0 Q8");
	CHECK(bvn_float_parse_fix16("0.5",  8) == 128,   "fix16 0.5 Q8");
	CHECK(bvn_float_parse_fix16("2",    8) == 512,   "fix16 2.0 Q8");
	CHECK(bvn_float_parse_fix16("-1",   8) == -256,  "fix16 -1.0 Q8");
	CHECK(bvn_float_parse_fix16("-0.5", 8) == -128,  "fix16 -0.5 Q8");
	CHECK(bvn_float_parse_fix16("-2",   8) == -512,  "fix16 -2.0 Q8");

	CHECK(bvn_float_parse_fix16("0.5",  15) == (int16_t)0x4000, "fix16 0.5 Q15");
	CHECK(bvn_float_parse_fix16("-0.5", 15) == (int16_t)0xC000, "fix16 -0.5 Q15");
	CHECK(bvn_float_parse_fix16("0",    15) == 0,                "fix16 0 Q15");

	CHECK(bvn_float_parse_fix16("5",   0) == 5,   "fix16 5 Q0 (integer)");
	CHECK(bvn_float_parse_fix16("-5",  0) == -5,  "fix16 -5 Q0");
	CHECK(bvn_float_parse_fix16("127", 0) == 127, "fix16 127 Q0");

	CHECK(bvn_float_parse_fix16("0.25", 0) == 0, "fix16 0.25 Q0 → 0");
	CHECK(bvn_float_parse_fix16("0.5",  0) == 0, "fix16 0.5  Q0 ties-to-even → 0");
	CHECK(bvn_float_parse_fix16("0.75", 0) == 1, "fix16 0.75 Q0 → 1");
	CHECK(bvn_float_parse_fix16("1.5",  0) == 2, "fix16 1.5  Q0 ties-to-even → 2");
	CHECK(bvn_float_parse_fix16("2.5",  0) == 2, "fix16 2.5  Q0 ties-to-even → 2");
	CHECK(bvn_float_parse_fix16("3.5",  0) == 4, "fix16 3.5  Q0 ties-to-even → 4");

	CHECK(bvn_float_parse_fix32("0",    16) == 0,        "fix32 0 Q16");
	CHECK(bvn_float_parse_fix32("1",    16) == 65536,    "fix32 1.0 Q16");
	CHECK(bvn_float_parse_fix32("-1",   16) == -65536,   "fix32 -1.0 Q16");
	CHECK(bvn_float_parse_fix32("0.5",  16) == 32768,    "fix32 0.5 Q16");
	CHECK(bvn_float_parse_fix32("2",    16) == 131072,   "fix32 2.0 Q16");
	CHECK(bvn_float_parse_fix32("-2",   16) == -131072,  "fix32 -2.0 Q16");
	CHECK(bvn_float_parse_fix32("0.25", 16) == 16384,    "fix32 0.25 Q16");

	CHECK(bvn_float_parse_fix32("0.5",  31) == (int32_t)0x40000000, "fix32 0.5 Q31");
	CHECK(bvn_float_parse_fix32("-0.5", 31) == (int32_t)0xC0000000, "fix32 -0.5 Q31");

	CHECK(bvn_float_parse_fix64("0",    32) == 0LL,              "fix64 0 Q32");
	CHECK(bvn_float_parse_fix64("1",    32) == 0x100000000LL,    "fix64 1.0 Q32");
	CHECK(bvn_float_parse_fix64("-1",   32) == -0x100000000LL,   "fix64 -1.0 Q32");
	CHECK(bvn_float_parse_fix64("0.5",  32) == 0x80000000LL,     "fix64 0.5 Q32");
	CHECK(bvn_float_parse_fix64("0.25", 32) == 0x40000000LL,     "fix64 0.25 Q32");
	CHECK(bvn_float_parse_fix64("2",    32) == 0x200000000LL,    "fix64 2.0 Q32");

	CHECK(bvn_float_parse_fix32("nan", 16) == 0, "fix32 nan → 0");
	CHECK(bvn_float_parse_fix32("inf", 16) == 0, "fix32 inf → 0");
	CHECK(bvn_float_parse_fix64("nan", 32) == 0LL, "fix64 nan → 0");
	CHECK(bvn_float_parse_fix64("inf", 32) == 0LL, "fix64 inf → 0");
}

static void test_decimal32(void)
{
	puts("── decimal32 conversion ──────────────────────────────────────");

	CHECK(bvn_float_parse_dec32("0")  == 0x00000000u, "dec32 +0");
	CHECK(bvn_float_parse_dec32("-0") == 0x80000000u, "dec32 -0");

	CHECK(bvn_float_parse_dec32("inf")  == 0x7F800000u, "dec32 +inf");
	CHECK(bvn_float_parse_dec32("-inf") == 0xFF800000u, "dec32 -inf");
	CHECK(bvn_float_parse_dec32("INF")  == 0x7F800000u, "dec32 INF");

	{ uint32_t nb = bvn_float_parse_dec32("nan");
	  CHECK((nb & 0x7F800000u) == 0x7F800000u, "dec32 nan: exp bits");
	  CHECK((nb & 0x00400000u) != 0u,           "dec32 nan: coeff MSB"); }
	{ uint32_t nb = bvn_float_parse_dec32("-nan");
	  CHECK((nb >> 31) == 1u,                   "dec32 -nan: sign bit");
	  CHECK((nb & 0x7F800000u) == 0x7F800000u,  "dec32 -nan: exp bits"); }

	CHECK(bvn_float_parse_dec32("1")  == 0x32800001u, "dec32 1");
	CHECK(bvn_float_parse_dec32("-1") == 0xB2800001u, "dec32 -1");

	CHECK(bvn_float_parse_dec32("10") == 0x33000001u, "dec32 10");

	CHECK(bvn_float_parse_dec32("100") == 0x33800001u, "dec32 100");

	CHECK(bvn_float_parse_dec32("9") == 0x32800009u, "dec32 9");

	{ uint32_t pos = bvn_float_parse_dec32("42");
	  uint32_t neg = bvn_float_parse_dec32("-42");
	  CHECK((neg ^ pos) == 0x80000000u, "dec32 ±42 differ only in sign bit"); }

	{ uint32_t pos = bvn_float_parse_dec32("1234567");
	  uint32_t neg = bvn_float_parse_dec32("-1234567");
	  CHECK((neg ^ pos) == 0x80000000u, "dec32 ±1234567 differ only in sign"); }
}

static void test_decimal64(void)
{
	puts("── decimal64 conversion ──────────────────────────────────────");

	const uint64_t EXP_MASK64 = 0x7FE0000000000000ull;
	const uint64_t COEF_MASK64 = 0x001FFFFFFFFFFFFFull;

	CHECK(bvn_float_parse_dec64("0")  == 0x0000000000000000ull, "dec64 +0");
	CHECK(bvn_float_parse_dec64("-0") == 0x8000000000000000ull, "dec64 -0");

	{ uint64_t inf = bvn_float_parse_dec64("inf");
	  CHECK((inf & EXP_MASK64) == EXP_MASK64,  "dec64 +inf: exp bits all set");
	  CHECK((inf & COEF_MASK64) == 0ull,        "dec64 +inf: coeff is zero"); }

	{ uint64_t inf = bvn_float_parse_dec64("-inf");
	  CHECK((inf >> 63) == 1ull,                "dec64 -inf: sign bit"); }

	{ uint64_t nb = bvn_float_parse_dec64("nan");
	  CHECK((nb & EXP_MASK64) == EXP_MASK64,   "dec64 nan: exp bits");
	  CHECK((nb & (1ull << 52)) != 0ull,        "dec64 nan: coeff MSB (bit52)"); }

	{ uint64_t nb = bvn_float_parse_dec64("-nan");
	  CHECK((nb >> 63) == 1ull,                 "dec64 -nan: sign bit");
	  CHECK((nb & EXP_MASK64) == EXP_MASK64,   "dec64 -nan: exp bits"); }

	{ uint64_t expected = ((uint64_t)398 << 53) | 1ull;
	  CHECK(bvn_float_parse_dec64("1") == expected, "dec64 1"); }

	{ uint64_t expected = 0x8000000000000000ull | (((uint64_t)398 << 53) | 1ull);
	  CHECK(bvn_float_parse_dec64("-1") == expected, "dec64 -1"); }

	{ uint64_t expected = ((uint64_t)399 << 53) | 1ull;
	  CHECK(bvn_float_parse_dec64("10") == expected, "dec64 10"); }

	{ uint64_t pos = bvn_float_parse_dec64("42");
	  uint64_t neg = bvn_float_parse_dec64("-42");
	  CHECK((neg ^ pos) == 0x8000000000000000ull, "dec64 ±42 sign-bit flip"); }
}

static void test_overflow_to_inf(void)
{
	puts("── overflow → infinity ───────────────────────────────────────");

	PNum p; bvn_float_pnum_init(&p);
	bvn_float_ctx_t ctx; bvn_float_ctx_init(&ctx);

	{

		char *giant = malloc(3002);
		if (!giant) { fputs("SKIP: malloc failed\n", stderr); return; }
		memset(giant, '9', 3000);
		giant[3000] = '\0';

		CHECK(bvn_float_parse(&ctx, giant, &p) && p.inf && !p.neg,
			  "3000-digit: parse returns true with +inf (overflow inside digit loop)");
		free(giant);
	}

	{
		char *giant = malloc(3003);
		if (!giant) { fputs("SKIP: malloc failed\n", stderr); return; }
		giant[0] = '-';
		memset(giant + 1, '9', 3000);
		giant[3001] = '\0';

		CHECK(bvn_float_parse(&ctx, giant, &p) && p.inf && p.neg,
			  "negative 3000-digit: parse returns true with -inf");
		free(giant);
	}

	CHECK(bvn_float_parse_bin32("1e1000000")  == 0x7F800000u,
		  "f32 1e1000000 → +inf");
	CHECK(bvn_float_parse_bin64("1e1000000")  == 0x7FF0000000000000ull,
		  "f64 1e1000000 → +inf");

	{ uint32_t bits[4];
	  bvn_float_parse_bin128("1e1000000", bits);
	  CHECK(bits[3] == 0x7FFF0000u, "f128 1e1000000 → +inf"); }

	CHECK(bvn_float_parse_bin32("-1e1000000") == 0xFF800000u,
		  "f32 -1e1000000 → -inf");
	CHECK(bvn_float_parse_bin64("-1e1000000") == 0xFFF0000000000000ull,
		  "f64 -1e1000000 → -inf");
}

/* ══════════════════════════════════════════════════════════════════════
 * New public bvn_float_t API tests
 * ══════════════════════════════════════════════════════════════════════ */

static void test_bvnf_lifecycle(void)
{
	/* alloc / free */
	bvn_float_t *f = bvn_float_alloc(64u);
	CHECK(f != NULL, "bvn_float_alloc(64) non-NULL");
	CHECK(bvn_float_is_zero(f), "newly allocated → +0");
	CHECK(!bvn_float_is_neg(f), "newly allocated not negative");
	bvn_float_free(f);

	/* stack init */
	bvn_limb_t buf[BVN_FLOAT_NLIMBS(53u)];
	bvn_float_t g;
	bvn_float_init_buf(&g, 53u, buf, BVN_FLOAT_NLIMBS(53u));
	CHECK(bvn_float_is_zero(&g), "init_buf → +0");

	/* special setters */
	bvn_float_set_nan(&g);
	CHECK(bvn_float_is_nan(&g), "set_nan → NaN");
	bvn_float_set_inf(&g, false);
	CHECK(bvn_float_is_inf(&g),   "set_inf → Inf");
	CHECK(!bvn_float_is_neg(&g),  "set_inf(false) → positive");
	bvn_float_set_inf(&g, true);
	CHECK(bvn_float_is_neg(&g),   "set_inf(true) → negative");
	bvn_float_set_zero(&g, false);
	CHECK(bvn_float_is_zero(&g),  "set_zero → zero");
}

static void test_bvnf_from_to_double(void)
{
	bvn_limb_t buf[BVN_FLOAT_NLIMBS(64u)];
	bvn_float_t f;
	double out;

	bvn_float_init_buf(&f, 64u, buf, BVN_FLOAT_NLIMBS(64u));

	CHECK(bvn_float_from_double(&f, 1.0), "from_double(1.0) ok");
	CHECK(!bvn_float_is_zero(&f) && bvn_float_is_regular(&f), "1.0 → regular");
	CHECK(bvn_float_to_double(&f, &out) && out == 1.0, "round-trip 1.0");

	CHECK(bvn_float_from_double(&f, -3.14), "from_double(-3.14) ok");
	CHECK(bvn_float_to_double(&f, &out) && out == -3.14, "round-trip -3.14");

	CHECK(bvn_float_from_double(&f, 0.0), "from_double(0.0) ok");
	CHECK(bvn_float_is_zero(&f), "0.0 → zero");

	double inf = 1.0 / 0.0;
	CHECK(bvn_float_from_double(&f, inf), "from_double(+inf) ok");
	CHECK(bvn_float_is_inf(&f) && !bvn_float_is_neg(&f), "+Inf detected");

	double nan = 0.0 / 0.0;
	CHECK(bvn_float_from_double(&f, nan), "from_double(nan) ok");
	CHECK(bvn_float_is_nan(&f), "NaN detected");
}

static void test_bvnf_from_str_base10(void)
{
	bvn_limb_t buf[BVN_FLOAT_NLIMBS(64u)];
	bvn_float_t f;
	double out;

	bvn_float_init_buf(&f, 64u, buf, BVN_FLOAT_NLIMBS(64u));

	CHECK(bvn_float_from_str(&f, "1.0", 10), "parse '1.0'");
	CHECK(bvn_float_to_double(&f, &out) && out == 1.0, "parsed 1.0 correct");

	CHECK(bvn_float_from_str(&f, "-2.5", 10), "parse '-2.5'");
	CHECK(bvn_float_to_double(&f, &out) && out == -2.5, "parsed -2.5 correct");

	CHECK(bvn_float_from_str(&f, "1e10", 10), "parse '1e10'");
	CHECK(bvn_float_to_double(&f, &out) && out == 1e10, "parsed 1e10 correct");

	CHECK(bvn_float_from_str(&f, "nan", 10), "parse 'nan'");
	CHECK(bvn_float_is_nan(&f), "nan parsed");

	CHECK(bvn_float_from_str(&f, "inf", 10), "parse 'inf'");
	CHECK(bvn_float_is_inf(&f), "inf parsed");

	CHECK(!bvn_float_from_str(&f, "", 10), "empty string → false");
	CHECK(!bvn_float_from_str(&f, ".", 10), "bare dot → false");
}

static void test_bvnf_from_str_base16(void)
{
	bvn_limb_t buf[BVN_FLOAT_NLIMBS(64u)];
	bvn_float_t f;
	double out;

	bvn_float_init_buf(&f, 64u, buf, BVN_FLOAT_NLIMBS(64u));

	/* 0x1p0 = 1.0 */
	CHECK(bvn_float_from_str(&f, "1p0", 16), "parse hex '1p0'");
	CHECK(bvn_float_to_double(&f, &out) && out == 1.0,
		  "hex 1p0 == 1.0");

	/* 0x1.8p1 = 3.0 */
	CHECK(bvn_float_from_str(&f, "1.8p1", 16), "parse hex '1.8p1'");
	CHECK(bvn_float_to_double(&f, &out) && out == 3.0,
		  "hex 1.8p1 == 3.0");

	/* 0x1p-1 = 0.5 */
	CHECK(bvn_float_from_str(&f, "1p-1", 16), "parse hex '1p-1'");
	CHECK(bvn_float_to_double(&f, &out) && out == 0.5,
		  "hex 1p-1 == 0.5");
}

static void test_bvnf_to_str(void)
{
	bvn_limb_t buf[BVN_FLOAT_NLIMBS(64u)];
	bvn_float_t f;
	char strbuf[256];

	bvn_float_init_buf(&f, 64u, buf, BVN_FLOAT_NLIMBS(64u));

	CHECK(bvn_float_from_double(&f, 1.0), "setup 1.0");
	int32_t n = bvn_float_to_str(&f, strbuf, sizeof(strbuf), 10);
	CHECK(n > 0, "to_str(1.0, base10) returns positive length");

	/* Parse back and verify round-trip. */
	bvn_float_t g;
	bvn_limb_t gbuf[BVN_FLOAT_NLIMBS(64u)];
	bvn_float_init_buf(&g, 64u, gbuf, BVN_FLOAT_NLIMBS(64u));
	CHECK(bvn_float_from_str(&g, strbuf, 10), "re-parse to_str output");
	double out;
	CHECK(bvn_float_to_double(&g, &out) && out == 1.0, "round-trip via string: 1.0");

	CHECK(bvn_float_from_double(&f, -3.14), "setup -3.14");
	n = bvn_float_to_str(&f, strbuf, sizeof(strbuf), 10);
	CHECK(n > 0 && strbuf[0] == '-', "to_str(-3.14) starts with '-'");
	CHECK(bvn_float_from_str(&g, strbuf, 10) &&
		  bvn_float_to_double(&g, &out) && out == -3.14,
		  "round-trip via string: -3.14");

	/* Special values. */
	bvn_float_set_nan(&f);
	n = bvn_float_to_str(&f, strbuf, sizeof(strbuf), 10);
	CHECK(n > 0 && (strbuf[0]=='n'||strbuf[0]=='N'), "to_str(NaN) starts with 'n'");

	bvn_float_set_inf(&f, false);
	n = bvn_float_to_str(&f, strbuf, sizeof(strbuf), 10);
	CHECK(n > 0 && (strbuf[0]=='i'||strbuf[0]=='I'), "to_str(+Inf) starts with 'i'");

	bvn_float_set_inf(&f, true);
	n = bvn_float_to_str(&f, strbuf, sizeof(strbuf), 10);
	CHECK(n > 0 && strbuf[0] == '-', "to_str(-Inf) starts with '-'");
}

static void test_bvnf_ieee_roundtrip(void)
{
	bvn_limb_t buf[BVN_FLOAT_NLIMBS(64u)];
	bvn_float_t f;
	bvn_float_init_buf(&f, 64u, buf, BVN_FLOAT_NLIMBS(64u));

	/* f32 round-trip for a few representative values. */
	const float f32vals[] = { 1.0f, -1.0f, 3.14159f, 0.0f, 1e30f, -1e-30f };
	for (size_t i = 0; i < sizeof(f32vals)/sizeof(f32vals[0]); i++) {
		union { float f; uint32_t u; } cv;
		cv.f = f32vals[i];
		bvn_float_from_bin32(&f, cv.u);
		uint32_t back;
		bvn_float_to_bin32(&f, &back);
		CHECK(back == cv.u, "f32 IEEE round-trip");
	}

	/* f64 round-trip. */
	const double f64vals[] = { 1.0, -1.0, 3.141592653589793, 2.2250738585072014e-308 };
	for (size_t i = 0; i < sizeof(f64vals)/sizeof(f64vals[0]); i++) {
		union { double f; uint64_t u; } cv;
		cv.f = f64vals[i];
		bvn_float_from_bin64(&f, cv.u);
		uint64_t back;
		bvn_float_to_bin64(&f, &back);
		CHECK(back == cv.u, "f64 IEEE round-trip");
	}
}

int main(void)
{
#define RUN(fn) do { fprintf(stderr,">>> " #fn "\n"); fflush(stderr); fn(); } while(0)
	RUN(test_bigint_internals);
	RUN(test_parse_basics);
	RUN(test_parse_normalization);
	RUN(test_overflow_to_inf);
	RUN(test_f16);
	RUN(test_f32);
	RUN(test_f64);
	RUN(test_f128);
	RUN(test_fixed_point);
	RUN(test_decimal32);
	RUN(test_decimal64);
	RUN(test_bvnf_lifecycle);
	RUN(test_bvnf_from_to_double);
	RUN(test_bvnf_from_str_base10);
	RUN(test_bvnf_from_str_base16);
	RUN(test_bvnf_to_str);
	RUN(test_bvnf_ieee_roundtrip);

	printf("\n%s  %d passed, %d failed\n",
		   g_fails ? "FAIL" : "PASS", g_pass, g_fails);
	return g_fails ? 1 : 0;
}
