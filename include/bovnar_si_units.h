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

#ifndef BOVNAR_SI_UNITS_H_
#define BOVNAR_SI_UNITS_H_
#include "bovnar.h"
#include "bvn_int.h"
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef enum bvn_si_dim_idx_e {
	bvn_si_dim_meter    = 0,
	bvn_si_dim_kilogram = 1,
	bvn_si_dim_second   = 2,
	bvn_si_dim_ampere   = 3,
	bvn_si_dim_kelvin   = 4,
	bvn_si_dim_mol      = 5,
	bvn_si_dim_candela  = 6,
	bvn_si_dim_count    = 7
} bvn_si_dim_idx_t;
BVN_API int32_t bvn_exponent_to_int(unit_exponent_t e);
BVN_API unit_exponent_t bvn_int_to_exponent(int32_t n);
BVN_API double bvn_unit_to_si_factor(value_unit_t u,
                              bool *is_affine,
                              double *affine_offset,
                              bool *ok);
BVN_API value_unit_t bvn_unit_reduce(value_unit_t u, double *scale, bool *overflow);
BVN_API bool bvn_unit_dimension_vector(value_unit_t u,
                                int32_t dims[bvn_si_dim_count]);
BVN_API bool bvn_units_compatible(value_unit_t a, value_unit_t b);
BVN_API double bvn_unit_convert_factor(value_unit_t a, value_unit_t b,
                                bool *ok, bool *requires_affine);
/* Convert `value` from unit `from` into unit `to`, writing the result to *out.
 * Handles multiplicative and affine (°C/°F/K) conversions. Returns false and
 * leaves *out untouched when the units are dimensionally incompatible or have no
 * SI mapping — the "validly convert only" guard used by the reader's want_unit. */
BVN_API bool bvn_unit_convert_value(double value, value_unit_t from,
                                value_unit_t to, double *out);
/* Lossless (exact arbitrary-precision) unit conversion. Converts the exact
 * rational value vnum/vden from unit `from` into unit `to`, writing the exact
 * result to out_num/out_den (caller-allocated, reduced to lowest terms, den>0).
 * Returns false when the units are dimensionally incompatible or the unit is
 * structurally invalid. *exact is set false when the true conversion factor is
 * irrational (π-based angles): the result is then only an approximation and a
 * lossless consumer must reject it. This is the engine behind the reader's
 * want_unit hook; it is exact for any value width and any base. */
BVN_API bool bvn_unit_convert_rational(const bvn_int_t *vnum, const bvn_int_t *vden,
                                value_unit_t from, value_unit_t to,
                                bvn_int_t *out_num, bvn_int_t *out_den, bool *exact);
/* Returned by bvn_rational_to_str when num/den has no terminating positional
 * expansion in the requested base (1/3 in base 10, 1/1000 in base 2). The
 * rational itself is still exact — only its digit string is infinite — so this
 * is kept distinct from the -1 hard failures. */
#define BVN_RATIONAL_NONTERMINATING (-2)
/* True for a base bvnr can write and bvn_rational_to_str can render: 2..62 plus
 * the two byte-encoding bases 64 (Base64) and 85 (Ascii85). Mirrors the range
 * bvn_int_from_str/bvn_int_to_str accept. */
static inline bool bvn_rational_base_valid(uint32_t base)
{
	return (base >= 2u && base <= 62u) || base == 64u || base == 85u;
}
/* Upper bound (including sign, radix point and NUL) on the buffer
 * bvn_rational_to_str needs for num/den in `base`. The integer part takes at
 * most bitlen(num) bits' worth of digits; the fraction terminates after at most
 * bitlen(den) digits, since each digit strips a factor >= 2 from the
 * denominator. */
BVN_API size_t bvn_rational_str_bufsize(const bvn_int_t *num, const bvn_int_t *den,
                                uint32_t base);
/* Render an exact rational num/den as a positional string in `base` — 2..62,
 * 64, or 85 (bvn_rational_base_valid). Writes the full exact expansion and sets
 * *exact=true when the fraction terminates in `base`, returning the string
 * length (excluding NUL).
 *
 * Two distinct failures, both leaving *exact=false and `buf` unwritten:
 *   BVN_RATIONAL_NONTERMINATING (-2) — the expansion is infinite in this base.
 *       Not a defect in the value: num/den remain exact and a caller that can
 *       consume a rational should use them.
 *   -1 — bad arguments, an unsupported base, a negative value in the sign-less
 *       bases 64/85, allocation failure, or a `bufsize` too small to hold the
 *       result. The buffer is NEVER truncated: a short buffer is refused, since
 *       a truncated digit string is a WRONG number under an exact contract.
 *       Size it with bvn_rational_str_bufsize. */
BVN_API int32_t bvn_rational_to_str(const bvn_int_t *num, const bvn_int_t *den,
                                uint32_t base, char *buf, size_t bufsize, bool *exact);
BVN_API bool bvn_prefix_unit_valid(value_unit_prefix_t prefix, value_base_unit_t base);
#ifdef __cplusplus
}
#endif
#endif
