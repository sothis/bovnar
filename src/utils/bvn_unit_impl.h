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

#ifndef BVN_UNIT_IMPL_H_
#define BVN_UNIT_IMPL_H_
#include <assert.h>
#include <math.h>       /* frexp/ldexp/isfinite for bvni_scaled_t */
#include <stdbool.h>
#include <stdint.h>
#include "bovnar.h"
#include "bvn_internal_dims.h"
#include "bovnar_si_units.h"
typedef struct {
	double  factor;
	int32_t exp;
} bvni_pfx_t;
static const bvni_pfx_t bvni_si_pfx_table[] = {
	[si_none]   = {1e0,   0},
#include "bovnar_si_pfx_table.gen.inc"
};
static const bvni_pfx_t bvni_iec_pfx_table[] = {
	[iec_none]  = {1.0,                                            0},
#include "bovnar_iec_pfx_table.gen.inc"
};
static inline double bvni_prefix_factor(value_unit_component_t c)
{
	if (c.prefix.system == prefix_iec) {
		if (c.prefix.id.iec < BVN_IEC_PREFIX_COUNT)
			return bvni_iec_pfx_table[c.prefix.id.iec].factor;
		return 1.0;
	}
	if (c.prefix.id.si < BVN_SI_PREFIX_COUNT)
		return bvni_si_pfx_table[c.prefix.id.si].factor;
	return 1.0;
}
/*
 * Integer power of a double, base^exp, by exponentiation-by-squaring. Replaces
 * pow() (and the libm dependency it dragged in) for the only way this library
 * ever raised to a power: an integer exponent. For base 2.0 every intermediate
 * is an exact power of two, so the result is bit-exact (matching what ldexp/pow
 * produced); for other bases it is a product of exp multiplications, within a
 * ulp of a correctly-rounded pow over the small exponents used here.
 */
static inline double bvni_ipow(double base, int32_t exp)
{
	if (exp == 0)
		return 1.0;
	uint32_t n = (exp < 0) ? (uint32_t)(-(int64_t)exp) : (uint32_t)exp;
	double result = 1.0;
	double b = base;
	while (n > 0u) {
		if (n & 1u)
			result *= b;
		n >>= 1;
		if (n)
			b *= b;
	}
	return (exp < 0) ? 1.0 / result : result;
}
/*
 * A double with its binary exponent carried separately: value = m * 2^e2, with
 * m normalised into [0.5, 1) (or exactly 0).
 *
 * WHY THE UNIT ENGINE NEEDS ONE. A compound unit's SI factor is a product of
 * per-component factors, and a component's own factor can sit far outside what a
 * double spans even when the PRODUCT does not: `r~barn⁴` is 10⁻²²⁰ and
 * `R~ha⁴·R~tn_sh³` is 10²²⁰-ish, and the running product between them lands in
 * the subnormal range where a double has fewer than 53 bits of mantissa. It
 * comes back out with the bits it lost, so the answer was quietly wrong rather
 * than obviously so: a randomised sweep found `n~m·r~Da⁴·r~tn_l⁴` converting
 * 5.3e-5 off the exact value, and `z~fl_oz_uk⁴·y~var·r~barn⁴/µ~qt_uk³` — whose
 * true factor is a perfectly ordinary 10⁻¹⁹⁸ — underflowing to zero and being
 * REFUSED, while bvn_unit_convert_rational performed it exactly.
 *
 * Renormalising after every multiply keeps the mantissa at full precision no
 * matter what the intermediates do, and moves the only overflow/underflow to the
 * end, where it is a property of the answer instead of an accident of the order
 * the components happened to be written in. Powers of two are exact in binary
 * floating point, so the split itself introduces no error at all.
 */
typedef struct {
	double  m;      /* mantissa in [0.5, 1), or 0.0 */
	int32_t e2;     /* binary exponent */
} bvni_scaled_t;

static inline bvni_scaled_t bvni_scaled_norm(double v, int32_t e2)
{
	bvni_scaled_t s;
	if (v == 0.0 || !isfinite(v)) {
		/* 0 stays 0; a non-finite has escaped the representation and is
		 * reported as such by the caller's final isfinite() check. */
		s.m = v;
		s.e2 = 0;
		return s;
	}
	int exp = 0;
	s.m = frexp(v, &exp);
	/* Saturate rather than wrap: an exponent this large is already a failure,
	 * and a wrapped one would turn it into a plausible small number. */
	int64_t sum = (int64_t)e2 + exp;
	if (sum > INT32_MAX) sum = INT32_MAX;
	if (sum < INT32_MIN) sum = INT32_MIN;
	s.e2 = (int32_t)sum;
	return s;
}

static inline bvni_scaled_t bvni_scaled_one(void)
{
	bvni_scaled_t s = { 0.5, 1 };   /* 0.5 * 2^1 == 1 */
	return s;
}

static inline bvni_scaled_t bvni_scaled_mul(bvni_scaled_t a, bvni_scaled_t b)
{
	return bvni_scaled_norm(a.m * b.m, a.e2 + b.e2);
}

static inline bvni_scaled_t bvni_scaled_inv(bvni_scaled_t a)
{
	if (a.m == 0.0)
		return bvni_scaled_norm(1.0 / a.m, 0);   /* inf, caught downstream */
	return bvni_scaled_norm(1.0 / a.m, -a.e2);
}

static inline bvni_scaled_t bvni_scaled_div(bvni_scaled_t a, bvni_scaled_t b)
{
	if (b.m == 0.0)
		return bvni_scaled_norm(a.m / b.m, 0);
	return bvni_scaled_norm(a.m / b.m, a.e2 - b.e2);
}

/* base^exp in the scaled representation, by the same exponentiation-by-squaring
 * as bvni_ipow -- but renormalising each step, so `(1e-30)^100` is 10⁻³⁰⁰⁰ as a
 * mantissa and an exponent rather than the 0.0 a double squares its way down to
 * after three rounds. */
static inline bvni_scaled_t bvni_scaled_ipow(double base, int32_t exp)
{
	bvni_scaled_t result = bvni_scaled_one();
	if (exp == 0)
		return result;
	uint32_t n = (exp < 0) ? (uint32_t)(-(int64_t)exp) : (uint32_t)exp;
	bvni_scaled_t b = bvni_scaled_norm(base, 0);
	while (n > 0u) {
		if (n & 1u)
			result = bvni_scaled_mul(result, b);
		n >>= 1;
		if (n)
			b = bvni_scaled_mul(b, b);
	}
	return (exp < 0) ? bvni_scaled_inv(result) : result;
}

/* Collapse back to a plain double. Returns false — without writing *out — when
 * the value does not fit one, which is the ONLY place overflow and underflow are
 * decided now. Zero is a failure for the same reason it is in
 * bvn_unit_to_si_factor: no real unit has an SI factor of zero, so a zero here
 * can only be underflow. */
static inline bool bvni_scaled_to_double(bvni_scaled_t s, double *out)
{
	if (s.m == 0.0 || !isfinite(s.m))
		return false;
	double v = ldexp(s.m, s.e2);
	if (!isfinite(v) || v == 0.0)
		return false;
	*out = v;
	return true;
}

/*
 * 10^n as a double. Powers of ten up to 10^22 are exactly representable, so the
 * table (and the reciprocal of an exact value for n<0) reproduces pow(10.0,n)
 * bit-for-bit over that range; beyond it both this and pow round, and the
 * squaring fallback is used.
 */
static inline double bvni_pow10(int32_t n)
{
	static const double tab[] = {
		1e0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,
		1e8,  1e9,  1e10, 1e11, 1e12, 1e13, 1e14, 1e15,
		1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22,
	};
	const int32_t max = (int32_t)(sizeof(tab) / sizeof(tab[0])) - 1;
	if (n >= 0)
		return (n <= max) ? tab[n] : bvni_ipow(10.0, n);
	return (n >= -max) ? 1.0 / tab[-n] : bvni_ipow(10.0, n);
}
static inline bool bvni_is_neg_exp(unit_exponent_t e)
{
	return bvn_exponent_to_int(e) < 0;
}
/*
 * |exponent|, or 0 for one that is not a member of unit_exponent_t.
 *
 * These used to assert. A library must not abort its host process over a struct
 * a caller filled in wrongly, and the shipped Release build keeps asserts live,
 * so `u.components[0].exponent = 10` reaching bvn_unit_prefix_factor took the
 * whole program down. Returning 0 lets every caller use the check it already
 * makes for exp_invalid — bvn_exponent_to_int returns 0 for that too.
 */
/* bovnar_si_units.c — true when two units carry the same amount of every
 * dimensionless quantity kind (information, angle, logarithmic ratio). */
bool bvni_kinds_match(value_unit_t a, value_unit_t b);
static inline int32_t bvni_exp_abs(unit_exponent_t e)
{
	int32_t v = bvn_exponent_to_int(e);
	return v < 0 ? -v : v;
}
static inline int32_t bvni_prefix_exp_int(value_unit_component_t c)
{
	if (bvn_exponent_to_int(c.exponent) == 0)
		return 0;
	int32_t base_exp;
	if (c.prefix.system == prefix_iec) {
		base_exp = (c.prefix.id.iec < BVN_IEC_PREFIX_COUNT)
		         ? bvni_iec_pfx_table[c.prefix.id.iec].exp : 0;
	} else {
		base_exp = (c.prefix.id.si < BVN_SI_PREFIX_COUNT)
		         ? bvni_si_pfx_table[c.prefix.id.si].exp : 0;
	}
	int32_t abs_exp = bvni_exp_abs(c.exponent);
	int32_t result  = base_exp * abs_exp;
	if (bvni_is_neg_exp(c.exponent))
		result = -result;
	return result;
}
#endif
