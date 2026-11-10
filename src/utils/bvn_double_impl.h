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

#ifndef BVN_DOUBLE_IMPL_H_
#define BVN_DOUBLE_IMPL_H_

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/*
 * frexp() and ldexp() are libm functions, and libm is a SEPARATE library on
 * FreeBSD and OpenBSD -- unlike glibc >= 2.34, musl, macOS's libSystem and the
 * Windows CRT, which all fold it into libc. FreeBSD's <math.h> is worse still:
 * it spells the classification macros isfinite/isinf/isnan/signbit as calls to
 * the libm entry points __isfinite/__isinf/__isnan/__signbit even when the
 * compiler could use a builtin, so a single isfinite() in library code makes
 * every executable link libm.
 *
 * This library is built with no dependency beyond the C standard library
 * (CONTRIBUTING.md), so the services it needs are implemented here from the
 * IEEE-754 binary64 bit pattern instead. That is the same representation
 * assumption bvn_float.c and bovnar_utils.c already make when they memcpy a
 * double into a uint64_t; every target this project builds for uses binary64.
 *
 * None of these helpers calls libm. None performs floating-point arithmetic
 * except bvni_frexp's exact 2^54 rescale of a subnormal, so their results do
 * not depend on the current rounding mode. bvni_frexp/bvni_ldexp implement the
 * C99 round-to-nearest, ties-to-even behaviour of the functions they replace;
 * the range-error cases (which C99 leaves loosely specified) are fixed here:
 * overflow yields +/-infinity and underflow +/-0.
 */

#define BVNI_DBL_SIGN 0x8000000000000000ULL
#define BVNI_DBL_EXP  0x7FF0000000000000ULL
#define BVNI_DBL_FRAC 0x000FFFFFFFFFFFFFULL

/* v == m * 2^(*exp), with 0.5 <= |m| < 1. Zero, infinity and NaN are returned
 * unchanged with *exp = 0 (C99 leaves their exponent unspecified). */
static inline double bvni_frexp(double v, int *exp)
{
	uint64_t bits, sign, mag;
	int e;

	memcpy(&bits, &v, sizeof bits);
	sign = bits & BVNI_DBL_SIGN;
	mag  = bits & ~BVNI_DBL_SIGN;

	if (mag == 0) {                 /* +0 / -0 */
		*exp = 0;
		return v;
	}
	e = (int)(mag >> 52);
	if (e == 0x7FF) {               /* infinity or NaN */
		*exp = 0;
		return v;
	}
	if (e == 0) {
		/* Subnormal. Multiplying by 2^54 makes it normal and leaves the
		 * fraction bits where they are; the true exponent is 54 lower. */
		v *= 18014398509481984.0;       /* 2^54 */
		memcpy(&bits, &v, sizeof bits);
		sign = bits & BVNI_DBL_SIGN;
		mag  = bits & ~BVNI_DBL_SIGN;
		e = (int)(mag >> 52) - 54;
	}
	*exp = e - 1022;
	/* 0.5 <= m < 1: keep the fraction, pin the exponent to 2^-1. */
	bits = sign | (1022ULL << 52) | (mag & BVNI_DBL_FRAC);
	memcpy(&v, &bits, sizeof v);
	return v;
}

/* x * 2^e, correctly rounded; overflow is +/-infinity, underflow +/-0. */
static inline double bvni_ldexp(double x, int32_t e)
{
	uint64_t bits, sign, frac;
	int64_t ee;
	int ex;
	double m;

	/* Zero, infinity and NaN pass through unchanged. */
	memcpy(&bits, &x, sizeof bits);
	if ((bits & ~BVNI_DBL_SIGN) == 0 ||
	    (bits & BVNI_DBL_EXP) == BVNI_DBL_EXP)
		return x;

	m = bvni_frexp(x, &ex);         /* x == m * 2^ex, m in [0.5, 1) */
	memcpy(&bits, &m, sizeof bits);
	sign = bits & BVNI_DBL_SIGN;
	frac = bits & BVNI_DBL_FRAC;
	ee   = (int64_t)ex + (int64_t)e;

	if (ee > 1024) {                /* beyond 1.frac * 2^1023 */
		bits = sign | BVNI_DBL_EXP;
	} else if (ee <= -1075) {       /* below half the smallest subnormal */
		bits = sign;
	} else if (ee >= -1021) {       /* normal: 1.frac * 2^(ee - 1) */
		bits = sign | ((uint64_t)(ee + 1022) << 52) | frac;
	} else {
		/* Subnormal. The value is (2^52 + frac) * 2^(ee - 53), i.e.
		 * k * 2^-1074 with k = (2^52 + frac) >> -(ee + 1021); round
		 * that quotient to nearest, ties to even. */
		unsigned sh = (unsigned)(-(ee + 1021));   /* 1 ... 53 */
		uint64_t n = (1ULL << 52) | frac;
		uint64_t q = n >> sh;
		uint64_t rem = n & ((1ULL << sh) - 1ULL);
		uint64_t half = 1ULL << (sh - 1);

		if (rem > half || (rem == half && (q & 1ULL)))
			q++;
		if (q == (1ULL << 52))
			bits = sign | (1ULL << 52);   /* smallest normal */
		else
			bits = sign | q;              /* exponent field 0 */
	}
	memcpy(&x, &bits, sizeof x);
	return x;
}

/* fabs without libm. A NaN keeps its payload; only the sign bit is cleared. */
static inline double bvni_fabs(double x)
{
	uint64_t bits;

	memcpy(&bits, &x, sizeof bits);
	bits &= ~BVNI_DBL_SIGN;
	memcpy(&x, &bits, sizeof x);
	return x;
}

/* The classification predicates, for the same reason (FreeBSD's math.h macros
 * call libm). Each reads the IEEE-754 fields directly. */
static inline bool bvni_isnan(double x)
{
	uint64_t bits;

	memcpy(&bits, &x, sizeof bits);
	return (bits & ~BVNI_DBL_SIGN) > BVNI_DBL_EXP;
}

static inline bool bvni_isinf(double x)
{
	uint64_t bits;

	memcpy(&bits, &x, sizeof bits);
	return (bits & ~BVNI_DBL_SIGN) == BVNI_DBL_EXP;
}

static inline bool bvni_isfinite(double x)
{
	return !bvni_isnan(x) && !bvni_isinf(x);
}

static inline bool bvni_signbit(double x)
{
	uint64_t bits;

	memcpy(&bits, &x, sizeof bits);
	return (bits & BVNI_DBL_SIGN) != 0;
}

#endif /* BVN_DOUBLE_IMPL_H_ */
