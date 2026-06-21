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
	return e == exp_neg_linear  || e == exp_neg_square  ||
	       e == exp_neg_cubic   || e == exp_neg_quartic ||
	       e == exp_neg_quintic || e == exp_neg_sextic  ||
	       e == exp_neg_septic  || e == exp_neg_octic   ||
	       e == exp_neg_nonic;
}
static inline int32_t bvni_exp_abs(unit_exponent_t e)
{
	assert(e != exp_invalid);
	int32_t v = bvn_exponent_to_int(e);
	assert(v != 0);
	return v < 0 ? -v : v;
}
static inline int32_t bvni_prefix_exp_int(value_unit_component_t c)
{
	assert(c.exponent != exp_invalid);
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
