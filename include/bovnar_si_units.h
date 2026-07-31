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
/*
 * OUT-PARAMETERS ARE OPTIONAL, THROUGHOUT THIS HEADER AND THE UNIT FUNCTIONS IN
 * bovnar.h. Passing NULL for one means "do not report this"; the function
 * behaves identically otherwise and its return value is unchanged.
 *
 * This was half true and half a crash. bvn_unit_reduce guarded `overflow` and
 * dereferenced `scale`; bvn_unit_convert_value guarded `out` and
 * bvn_unit_convert_factor dereferenced both of its; bvn_parse_unit checked its
 * INPUT pointer and not its `ok`. A caller with no use for `requires_affine`, or
 * one reading a factor it will validate itself, had to declare a variable to
 * throw away — and had no way to know which functions let it skip that except by
 * trying. The rule is now the same everywhere.
 *
 * It does not extend to arguments a function's answer is made OF: a NULL
 * bvn_int_t in bvn_unit_convert_rational, or a NULL buffer to a formatter, is a
 * refused call (false / -1), not a silent success.
 */
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
/* The predicate to SCREEN a conversion target with: dimensionally compatible,
 * OR the same unit apart from its prefixes.
 *
 * It is not bvn_units_compatible, which reports false for "k~$USD -> $USD" (and
 * for "$USD -> $USD") because a currency carries no dimension by design,
 * although both convert exactly. It is not bvn_unit_convert_factor either — that
 * reports failure for "°F -> °C", since an affine conversion has no single
 * multiplicative factor, so screening on it drops every temperature.
 *
 * It is a screen, NOT a guarantee. `s/°C` is dimensionally compatible with `s/K`
 * and passes here, and the conversion entry points still refuse it, because an
 * affine scale means nothing at an exponent other than 1. Code that screens with
 * this must therefore still handle a conversion that declines — treating that as
 * "this target does not apply to this value" rather than as an error is what the
 * reader's unit policy does. */
BVN_API bool bvn_units_convertible(value_unit_t a, value_unit_t b);
/* The coherent SI form of `u`: the product of SI base units carrying the same
 * dimension, prefixes folded out (mass comes back as k~g, the SI base unit for
 * mass being the kilogram).
 *
 * Returns false and leaves *out untouched when there is no such form to name:
 *   - a currency, which has no dimension vector at all;
 *   - any DIMENSIONLESS unit (%, ppm, dB, pH, rad, °, the turbidity scales).
 *     Deliberate: normalising a ratio would silently turn "35 %" into a bare
 *     0.35, and normalising an angle would need the irrational factor between °
 *     and rad;
 *   - a unit whose SI form the conversion engine would then refuse. The
 *     dimension vector does not determine a unit: lm, lx and ph carry the
 *     steradian's quantity kind, which no dimension vector can express, so
 *     rebuilding them from dimensions yields cd and cd/m² — a different
 *     quantity. `s/°C` has the dimensions of `s/K` and is unconvertible for the
 *     affine reason above. The form returned here is checked against `u` before
 *     it is returned, so a form this function gives back is one the conversion
 *     entry points accept. */
BVN_API bool bvn_unit_si_normal_form(value_unit_t u, value_unit_t* out);
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
 * Returns false in three cases, which a caller who reports a diagnosis must
 * tell apart -- ask bvn_units_convertible to separate the first two from the
 * third:
 *   - the units are dimensionally incompatible;
 *   - the unit is structurally invalid;
 *   - CAPACITY: the units agree and the conversion is defined, but the exact
 *     factor needs more than BVN_INT_MAX_BITS. Reachable only from deliberately
 *     extreme units -- `Q~m¹⁰⁰·Q~g¹⁰⁰` to `q~m¹⁰⁰·q~g¹⁰⁰` needs 10^12000 --
 *     and NOT a statement that the units disagree. Nothing is truncated: the
 *     arbitrary-precision helpers report their own overflow and this refuses on
 *     it rather than computing with the low bits.
 * *exact is set false when the true conversion factor is irrational (π-based
 * angles): the result is then only an approximation and a lossless consumer
 * must reject it. On any false return *exact is unspecified. This is the engine
 * behind the reader's want_unit hook; it is exact for any value width and any
 * base. */
BVN_API bool bvn_unit_convert_rational(const bvn_int_t *vnum, const bvn_int_t *vden,
                                value_unit_t from, value_unit_t to,
                                bvn_int_t *out_num, bvn_int_t *out_den, bool *exact);
/* Returned by bvn_rational_to_str when num/den has no terminating positional
 * expansion in the requested base (1/3 in base 10, 1/1000 in base 2). The
 * rational itself is still exact — only its digit string is infinite — so this
 * is kept distinct from the -1 hard failures. */
#define BVN_RATIONAL_NONTERMINATING (-2)
/* Returned when the exact expansion simply does not fit `bufsize`. Distinct from
 * -1 so a caller can tell "your buffer is too small / this is more work than I
 * allowed" from a genuine failure. Detected BEFORE the digits are generated, so
 * `bufsize` doubles as a work limit: rendering is quadratic in the digit count,
 * and the count is driven by the magnitude of the value's exponent rather than
 * by its length, so a seven-character literal like 1e-9800 otherwise costs a
 * tenth of a second. */
#define BVN_RATIONAL_TOO_LONG (-3)
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
 *   BVN_RATIONAL_TOO_LONG (-3) — the exact expansion does not fit `bufsize`.
 *       The buffer is NEVER truncated: a truncated digit string is a WRONG
 *       number under an exact contract. Size it with bvn_rational_str_bufsize,
 *       or pass a smaller buffer deliberately to bound the work.
 *   -1 — bad arguments, an unsupported base, a negative value in the sign-less
 *       bases 64/85, or allocation failure. */
BVN_API int32_t bvn_rational_to_str(const bvn_int_t *num, const bvn_int_t *den,
                                uint32_t base, char *buf, size_t bufsize, bool *exact);
BVN_API bool bvn_prefix_unit_valid(value_unit_prefix_t prefix, value_base_unit_t base);
#ifdef __cplusplus
}
#endif
#endif
