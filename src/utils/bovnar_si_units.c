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

#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "bovnar.h"
#include "bvn_internal_dims.h"
#include "bovnar_si_units.h"
#include "bovnar_currency.h"
#include "bvn_unit_impl.h"
#include "bvn_int.h"
#include "bvn_float.h"
/*
 * ===========================================================================
 * SI conversion and dimensional analysis
 * ===========================================================================
 *
 * This file gives units physical meaning. Every base unit maps (in
 * si_conv_table) to: a factor converting it to the coherent SI unit of its
 * kind, an integer exponent vector over the seven SI base dimensions
 * [length, mass, time, current, temperature, amount, luminosity], and — for
 * temperatures — an affine offset (°C/°F/etc. need value*factor + offset, not a
 * pure scaling).
 *
 * From those two pieces of data the library can: convert a value to SI base
 * units (bvn_unit_to_si_factor), compute a unit's dimension signature
 * (bvn_unit_dimension_vector), decide whether two units measure the same
 * quantity and may be inter-converted (bvn_units_compatible /
 * bvn_unit_convert_factor), and algebraically simplify a compound unit
 * (bvn_unit_reduce). The exponent enum <-> int helpers at the top bridge the
 * compact unit_exponent_t representation and ordinary arithmetic.
 */
int32_t bvn_exponent_to_int(unit_exponent_t e)
{
	switch (e) {
	case exp_invalid:     return 0;
	case exp_linear:      return 1;
	case exp_square:      return 2;
	case exp_cubic:       return 3;
	case exp_quartic:     return 4;
	case exp_quintic:     return 5;
	case exp_sextic:      return 6;
	case exp_septic:      return 7;
	case exp_octic:       return 8;
	case exp_nonic:       return 9;
	case exp_neg_square:  return -2;
	case exp_neg_cubic:   return -3;
	case exp_neg_quartic: return -4;
	case exp_neg_quintic: return -5;
	case exp_neg_sextic:  return -6;
	case exp_neg_septic:  return -7;
	case exp_neg_octic:   return -8;
	case exp_neg_nonic:   return -9;
	case exp_neg_linear:  return -1;
	default:              return 0;
	}
}
unit_exponent_t bvn_int_to_exponent(int32_t n)
{
	switch (n) {
	case  0: return exp_invalid;
	case  1: return exp_linear;
	case  2: return exp_square;
	case  3: return exp_cubic;
	case  4: return exp_quartic;
	case  5: return exp_quintic;
	case  6: return exp_sextic;
	case  7: return exp_septic;
	case  8: return exp_octic;
	case  9: return exp_nonic;
	case -1: return exp_neg_linear;
	case -2: return exp_neg_square;
	case -3: return exp_neg_cubic;
	case -4: return exp_neg_quartic;
	case -5: return exp_neg_quintic;
	case -6: return exp_neg_sextic;
	case -7: return exp_neg_septic;
	case -8: return exp_neg_octic;
	case -9: return exp_neg_nonic;
	default: return exp_invalid;
	}
}
typedef struct {
	value_base_unit_t base;
	double            to_si_factor;
	int32_t           dims[bvn_si_dim_count];
	bool              is_affine;
	double            affine_offset;
	/* Exact rational form of the factor and affine offset, as decimal strings
	 * (parsed to bignums on demand). These drive lossless conversion; the double
	 * fields above remain the fast path for approximate/legacy callers. `exact`
	 * is false for a unit whose true factor is irrational (π-based angles) — such
	 * a unit can never be converted losslessly and yields error_unit_inexact. */
	const char       *factor_num;
	const char       *factor_den;
	const char       *offset_num;
	const char       *offset_den;
	bool              exact;
} bvn_si_conv_entry_t;
/*
 * The master conversion table, indexed directly by value_base_unit_t (the
 * designated initialisers make the index == .base, which bvn_verify_conv_table
 * asserts in debug builds). to_si_factor scales the unit to coherent SI (e.g.
 * inch -> 0.0254 m, hour -> 3600 s); dims is its dimension exponent vector;
 * is_affine/affine_offset handle the temperature scales. Currency slots are
 * skipped here — they are handled by bovnar_currency.c, not by SI conversion.
 */
static const bvn_si_conv_entry_t si_conv_table[BVN_VALUE_BASE_UNIT_COUNT] = {
	[bu_none]               = { bu_none,               1.0,        {0, 0, 0, 0, 0, 0, 0}, false, 0.0,    "1", "1", "0", "1", true },
	/*
	 * Physical-unit conversion entries — generated from src/gendata/units.bvnr
	 * by gen_units.py. Indexed by enum value; the [bu_none] row above
	 * and the currency slots keep the table's zero defaults.
	 */
#include "bovnar_si_conv_table.gen.inc"
};
#define SI_CONV_TABLE_SIZE \
	((uint32_t)(sizeof(si_conv_table) / sizeof(si_conv_table[0])))
static void bvn_verify_conv_table(void)
{
#ifndef NDEBUG
	for (uint32_t i = 0; i < SI_CONV_TABLE_SIZE; i++) {
		if (bvn_unit_is_currency((int)i))
			continue;
		assert((uint32_t)si_conv_table[i].base == i &&
		       "si_conv_table: .base mismatch at index i");
	}
#endif
}
static const bvn_si_conv_entry_t *bvn_find_si_conv(value_base_unit_t bu)
{
	if ((uint32_t)bu >= SI_CONV_TABLE_SIZE)
		return NULL;
	if (bvn_unit_is_currency((int)bu))
		return NULL;
	return &si_conv_table[bu];
}
/*
 * Compute the scalar factor that converts a value in unit `u` to SI base units,
 * multiplying each component's (prefix·base-factor)^exponent. Affine units are
 * special: an offset only makes sense for a lone linear temperature (e.g. plain
 * °C), so an affine unit with exponent != 1, or two affine components, sets
 * *ok=false. The reported *affine_offset lets the caller apply the +offset step
 * itself (see bvn_dom_get_value_in_base_units).
 */
double bvn_unit_to_si_factor(value_unit_t u,
			     bool *is_affine,
			     double *affine_offset,
			     bool *ok)
{
	bvn_verify_conv_table();
	double f = 1.0;
	*is_affine     = false;
	*affine_offset = 0.0;
	*ok            = true;
	for (uint32_t i = 0; i < u.num_components && i < BVNR_MAX_UNIT_COMPONENTS; i++) {
		const value_unit_component_t *c = &u.components[i];
		if (c->exponent == exp_invalid) {
			*ok = false;
			return f;
		}
		if (!bvn_prefix_unit_valid(c->prefix, c->base)) {
			*ok = false;
			return f;
		}
		int32_t uexp = bvn_exponent_to_int(c->exponent);
		if (uexp == 0) {
			*ok = false;
			return f;
		}
		int32_t abs_exp = bvni_exp_abs(c->exponent);
		double prefix_contrib = bvni_ipow(bvni_prefix_factor(*c), abs_exp);
		const bvn_si_conv_entry_t *conv = bvn_find_si_conv(c->base);
		if (!conv) {
			*ok = false;
			return f;
		}
		double bu_contrib = bvni_ipow(conv->to_si_factor, abs_exp);
		double comp_total = prefix_contrib * bu_contrib;
		if (uexp < 0)
			comp_total = 1.0 / comp_total;
		f *= comp_total;
		if (conv->is_affine) {
			if (uexp == 1) {
				if (*is_affine) {
					*ok = false;
					return f;
				}
				*is_affine     = true;
				*affine_offset = conv->affine_offset;
			} else {
				*ok = false;
				return f;
			}
		}
	}
	/* Extreme prefix/exponent combinations (e.g. Q~m^9, or several huge
	 * components multiplied together) can overflow the running product to ±inf,
	 * or produce nan via 0*inf. Surfacing that as a failure — rather than handing
	 * back a non-finite factor with *ok still true — mirrors how bvn_unit_reduce
	 * flags isinf, and stops callers like bvn_dom_get_value_in_base_units from
	 * silently emitting inf/nan values. */
	if (!isfinite(f)) {
		*ok = false;
	}
	return f;
}
/*
 * Sum each component's dimension vector (scaled by its exponent) to get the
 * unit's overall physical signature, e.g. m/s² -> [1,0,-2,0,0,0,0]. Two units
 * with equal signatures measure the same kind of quantity. This is the basis
 * of compatibility checking and of the named-SI collapse in the formatter.
 */
bool bvn_unit_dimension_vector(value_unit_t u, int32_t dims[bvn_si_dim_count])
{
	bvn_verify_conv_table();
	memset(dims, 0, sizeof(int32_t) * (size_t)bvn_si_dim_count);
	for (uint32_t i = 0; i < u.num_components && i < BVNR_MAX_UNIT_COMPONENTS; i++) {
		const value_unit_component_t *c = &u.components[i];
		if (c->exponent == exp_invalid)
			return false;
		if (!bvn_prefix_unit_valid(c->prefix, c->base))
			return false;
		int32_t uexp = bvn_exponent_to_int(c->exponent);
		if (uexp == 0)
			continue;
		const bvn_si_conv_entry_t *conv = bvn_find_si_conv(c->base);
		if (!conv)
			return false;
		for (int d = 0; d < bvn_si_dim_count; d++) {
			if (conv->dims[d] != 0)
				dims[d] += conv->dims[d] * uexp;
		}
	}
	return true;
}
static bool info_net_exponent(value_unit_t u, value_base_unit_t info_base,
                              int32_t *out)
{
	int32_t sum = 0;
	for (uint32_t i = 0; i < u.num_components && i < BVNR_MAX_UNIT_COMPONENTS; i++) {
		if (u.components[i].base == info_base) {
			if (u.components[i].exponent == exp_invalid)
				return false;
			sum += bvn_exponent_to_int(u.components[i].exponent);
		}
	}
	*out = sum;
	return true;
}
/*
 * Two units are inter-convertible iff they have the same SI dimension vector
 * AND the same net information-unit exponents. Bits/bytes are dimensionless in
 * the SI sense but are not freely interchangeable with pure numbers, so they
 * are tracked separately (info_net_exponent) — e.g. "B/s" is not compatible
 * with "1/s". bvn_unit_convert_factor builds on this to return the actual
 * multiplier a->b, refusing affine conversions unless the offsets match.
 */
bool bvn_units_compatible(value_unit_t a, value_unit_t b)
{
	int32_t a_bit, b_bit, a_byte, b_byte;
	if (!info_net_exponent(a, bu_bit,  &a_bit)  ||
	    !info_net_exponent(b, bu_bit,  &b_bit)  ||
	    !info_net_exponent(a, bu_byte, &a_byte) ||
	    !info_net_exponent(b, bu_byte, &b_byte))
		return false;
	if (a_bit != b_bit || a_byte != b_byte)
		return false;
	int32_t da[bvn_si_dim_count], db[bvn_si_dim_count];
	bool ok_a = bvn_unit_dimension_vector(a, da);
	bool ok_b = bvn_unit_dimension_vector(b, db);
	if (!ok_a || !ok_b)
		return false;
	for (int i = 0; i < bvn_si_dim_count; i++) {
		if (da[i] != db[i])
			return false;
	}
	return true;
}
double bvn_unit_convert_factor(value_unit_t a, value_unit_t b,
			       bool *ok, bool *requires_affine)
{
	*ok             = true;
	*requires_affine = false;
	if (!bvn_units_compatible(a, b)) {
		*ok = false;
		return 0.0;
	}
	bool aff_a = false, aff_b = false;
	double off_a = 0.0, off_b = 0.0;
	bool ok_a = true, ok_b = true;
	double fa = bvn_unit_to_si_factor(a, &aff_a, &off_a, &ok_a);
	double fb = bvn_unit_to_si_factor(b, &aff_b, &off_b, &ok_b);
	if (!ok_a || !ok_b) {
		*ok = false;
		return 0.0;
	}
	if (aff_a || aff_b) {
		*requires_affine = true;
		if (aff_a && aff_b &&
		    fabs(off_a - off_b) <=
		        DBL_EPSILON * fabs(off_a + off_b) + DBL_EPSILON)
			return fa / fb;
		*ok = false;
		return 0.0;
	}
	return fa / fb;
}
/*
 * Convert one numeric quantity from `from` into `to`, handling both the simple
 * multiplicative case and the affine case (e.g. °C ↔ K ↔ °F, where a bare factor
 * is not enough). Returns false — writing nothing to *out — when the two units
 * are dimensionally incompatible or lack an SI mapping; this is the "validly"
 * guard a reader turns into error_unit_mismatch. On success *out receives the
 * value expressed in `to`.
 *
 * The multiplicative path is a single factor from bvn_unit_convert_factor. The
 * affine path routes through SI base units in two steps — value·f_from +
 * off_from lands in SI, then (si − off_to)/f_to lands in `to` — mirroring the
 * Python convert_value() reference.
 */
bool bvn_unit_convert_value(double value, value_unit_t from, value_unit_t to,
			    double *out)
{
	if (!out)
		return false;
	/* Converting a unit to itself is the identity map — no factor, no offset, and
	 * nothing that needs an SI mapping. Short-circuiting it here is what lets a
	 * unit with no SI row (a currency) or an irrational factor (a π-based angle)
	 * be "converted" to itself, which is otherwise refused for reasons that do
	 * not apply to the identity. Order-insensitive, so two spellings of the same
	 * unit count as equal. */
	if (bvn_unit_equal(from, to)) {
		*out = value;
		return true;
	}
	bool conv_ok = true, requires_affine = false;
	double factor = bvn_unit_convert_factor(from, to, &conv_ok,
						&requires_affine);
	if (conv_ok) {
		*out = value * factor;
		return true;
	}
	if (!requires_affine)
		return false;               /* dimensionally incompatible */
	bool aff_f = false, aff_t = false;
	double off_f = 0.0, off_t = 0.0;
	bool ok_f = true, ok_t = true;
	double f_from = bvn_unit_to_si_factor(from, &aff_f, &off_f, &ok_f);
	double f_to   = bvn_unit_to_si_factor(to,   &aff_t, &off_t, &ok_t);
	if (!ok_f || !ok_t || f_to == 0.0)
		return false;
	double si_value = value * f_from + off_f;
	*out = (si_value - off_t) / f_to;
	return true;
}
/* ── exact-rational unit conversion ───────────────────────────────────────
 *
 * Everything below performs unit conversion in EXACT arbitrary-precision
 * rational arithmetic (num/den of bignums), so a value of any width and any
 * base — a 1056-bit float, a 512-bit integer — converts with no loss beyond the
 * library's own declared factor. A rational whose factor is genuinely irrational
 * (π-based angles) is reported via *exact = false; the reader turns that into
 * error_unit_inexact. These are the lossless counterparts to the double-based
 * bvn_unit_to_si_factor / bvn_unit_convert_value above.
 */
static bool rat_reduce(bvn_int_t *n, bvn_int_t *d)
{
	if (bvn_int_is_zero(d)) return false;
	if (d->negative) {                          /* keep denominator positive */
		d->negative = false;
		if (!bvn_int_is_zero(n)) n->negative = !n->negative;
	}
	if (bvn_int_is_zero(n)) return bvn_int_from_uint64(d, 1u);
	bvn_int_t *g = bvn_int_alloc(), *q = bvn_int_alloc(), *r = bvn_int_alloc();
	bool ok = g && q && r && bvn_int_gcd(g, n, d);
	if (ok) ok = bvn_int_divrem(q, r, n, g) && bvn_int_copy(n, q);
	if (ok) ok = bvn_int_divrem(q, r, d, g) && bvn_int_copy(d, q);
	bvn_int_free(g); bvn_int_free(q); bvn_int_free(r);
	return ok;
}
/* (no/do) = (an/ad) * (bn/bd), reduced. Outputs must differ from inputs. */
static bool rat_mul(bvn_int_t *no, bvn_int_t *dou,
		    const bvn_int_t *an, const bvn_int_t *ad,
		    const bvn_int_t *bn, const bvn_int_t *bd)
{
	return bvn_int_mul(no, an, bn) && bvn_int_mul(dou, ad, bd) &&
	       rat_reduce(no, dou);
}
/* (no/do) = (an/ad) + (bn/bd), reduced. Outputs must differ from inputs. */
static bool rat_add(bvn_int_t *no, bvn_int_t *dou,
		    const bvn_int_t *an, const bvn_int_t *ad,
		    const bvn_int_t *bn, const bvn_int_t *bd)
{
	bvn_int_t *t1 = bvn_int_alloc(), *t2 = bvn_int_alloc();
	bool ok = t1 && t2;
	if (ok) ok = bvn_int_mul(t1, an, bd);     /* an*bd */
	if (ok) ok = bvn_int_mul(t2, bn, ad);     /* bn*ad */
	if (ok) ok = bvn_int_add(no, t1, t2);     /* numerator */
	if (ok) ok = bvn_int_mul(dou, ad, bd);    /* denominator */
	if (ok) ok = rat_reduce(no, dou);
	bvn_int_free(t1); bvn_int_free(t2);
	return ok;
}
static bool rat_set_decstr(bvn_int_t *n, bvn_int_t *d,
			   const char *num_s, const char *den_s)
{
	return bvn_int_from_str(n, num_s ? num_s : "0", 10) &&
	       bvn_int_from_str(d, den_s ? den_s : "1", 10);
}
/*
 * Exact rational SI factor + affine offset for a unit: value_SI =
 * value*(fnum/fden) + (onum/oden). Mirrors bvn_unit_to_si_factor's structure
 * (per-component base-factor^exp × prefix^exp, a lone affine temperature adds an
 * offset) but in exact rationals. Returns false on a structurally invalid unit
 * (bad prefix/exponent, or an affine unit with exponent != 1 / two affine
 * components); sets *exact = false when any component's factor is irrational.
 */
static bool bvn_unit_to_si_rational(value_unit_t u,
				    bvn_int_t *fnum, bvn_int_t *fden,
				    bvn_int_t *onum, bvn_int_t *oden,
				    bool *exact)
{
	bvn_verify_conv_table();
	*exact = true;
	bool ok = bvn_int_from_uint64(fnum, 1u) && bvn_int_from_uint64(fden, 1u) &&
		  bvn_int_from_uint64(oden, 1u);
	bvn_int_zero(onum);
	bool have_affine = false;
	bvn_int_t *cfn = bvn_int_alloc(), *cfd = bvn_int_alloc();
	bvn_int_t *tn  = bvn_int_alloc(), *td  = bvn_int_alloc();
	bvn_int_t *bfn = bvn_int_alloc(), *bfd = bvn_int_alloc();
	if (!cfn || !cfd || !tn || !td || !bfn || !bfd) { ok = false; goto done; }
	for (uint32_t i = 0; ok && i < u.num_components &&
	     i < BVNR_MAX_UNIT_COMPONENTS; i++) {
		const value_unit_component_t *c = &u.components[i];
		/* No bu_none short-circuit here: bu_none is a real table row (factor
		 * 1/1) and it can still carry a PREFIX — a dimensionless kilo, which
		 * bvn_prefix_unit_valid, bvn_unit_dimension_vector, bvn_units_compatible
		 * and bvn_unit_to_si_factor all accept as a scale of 1000. Skipping the
		 * component here dropped that factor and made this the one function in
		 * the library that disagreed, silently returning the value unscaled. */
		if (c->exponent == exp_invalid) { ok = false; break; }
		if (!bvn_prefix_unit_valid(c->prefix, c->base)) { ok = false; break; }
		int32_t uexp = bvn_exponent_to_int(c->exponent);
		if (uexp == 0) { ok = false; break; }
		int32_t abs_exp = bvni_exp_abs(c->exponent);
		const bvn_si_conv_entry_t *conv = bvn_find_si_conv(c->base);
		if (!conv) { ok = false; break; }
		if (!conv->exact) *exact = false;
		/* base factor ^ abs_exp */
		if (!rat_set_decstr(bfn, bfd, conv->factor_num, conv->factor_den)) {
			ok = false; break; }
		if (!bvn_int_copy(cfn, bfn) || !bvn_int_copy(cfd, bfd)) { ok = false; break; }
		for (int32_t e = 1; e < abs_exp; e++)
			if (!bvn_int_mul(cfn, cfn, bfn) || !bvn_int_mul(cfd, cfd, bfd)) {
				ok = false; break; }
		if (!ok) break;
		if (uexp < 0) {                         /* invert this component */
			bvn_int_t *sw = cfn; cfn = cfd; cfd = sw;
		}
		/* Prefix, base 10 for SI and base 2 for IEC. bvni_prefix_exp_int
		 * already folds in |exp| AND the sign of the component's exponent, so it
		 * is the FINAL signed power: it must not be multiplied by abs_exp again,
		 * and it must be applied after the inversion above rather than through
		 * it. Doing either turned k~m² into 10¹² instead of 10⁶ and inverted the
		 * prefix outright on any negative exponent — 1 m/k~m came out 10⁶ times
		 * too large. */
		int32_t totpexp = bvni_prefix_exp_int(*c);
		if (c->prefix.system == prefix_iec) {
			if (totpexp > 0) ok = bvn_int_shl(cfn, totpexp);
			else if (totpexp < 0) ok = bvn_int_shl(cfd, -totpexp);
		} else {
			if (totpexp > 0) ok = bvn_int_mul_pow10(cfn, totpexp);
			else if (totpexp < 0) ok = bvn_int_mul_pow10(cfd, -totpexp);
		}
		if (!ok) break;
		/* accumulate into the running factor */
		if (!rat_mul(tn, td, fnum, fden, cfn, cfd)) { ok = false; break; }
		if (!bvn_int_copy(fnum, tn) || !bvn_int_copy(fden, td)) { ok = false; break; }
		/* affine offset: only a lone linear temperature carries one */
		if (conv->is_affine) {
			if (uexp != 1 || have_affine) { ok = false; break; }
			have_affine = true;
			if (!rat_set_decstr(onum, oden, conv->offset_num, conv->offset_den)) {
				ok = false; break; }
			if (!conv->exact) *exact = false;
		}
	}
	if (ok) ok = rat_reduce(fnum, fden) && rat_reduce(onum, oden);
done:
	bvn_int_free(cfn); bvn_int_free(cfd); bvn_int_free(tn); bvn_int_free(td);
	bvn_int_free(bfn); bvn_int_free(bfd);
	return ok;
}
bool bvn_unit_convert_rational(const bvn_int_t *vnum, const bvn_int_t *vden,
			       value_unit_t from, value_unit_t to,
			       bvn_int_t *out_num, bvn_int_t *out_den, bool *exact)
{
	if (!vnum || !vden || !out_num || !out_den || !exact) return false;
	/* Identity conversion: the value passes through unchanged and EXACTLY,
	 * whatever the unit is. Without this short-circuit the from- and to-unit's
	 * own properties are consulted for what is arithmetically a no-op, so
	 * `90° -> 90°` was rejected as inexact (the π factor never enters) and
	 * `$USD -> $USD` as dimensionally incompatible (currencies have no SI row).
	 * That broke the documented pure-base-conversion request, where the caller
	 * names the value's own unit precisely because it wants no unit change. */
	if (bvn_unit_equal(from, to)) {
		*exact = true;
		return bvn_int_copy(out_num, vnum) && bvn_int_copy(out_den, vden) &&
		       rat_reduce(out_num, out_den);
	}
	if (!bvn_units_compatible(from, to)) return false;   /* dim mismatch */
	bvn_int_t *ffn = bvn_int_alloc(), *ffd = bvn_int_alloc();
	bvn_int_t *fon = bvn_int_alloc(), *fod = bvn_int_alloc();
	bvn_int_t *tfn = bvn_int_alloc(), *tfd = bvn_int_alloc();
	bvn_int_t *ton = bvn_int_alloc(), *tod = bvn_int_alloc();
	bvn_int_t *sn  = bvn_int_alloc(), *sd  = bvn_int_alloc();
	bvn_int_t *un  = bvn_int_alloc(), *ud  = bvn_int_alloc();
	bool exf = true, ext = true;
	bool ok = ffn && ffd && fon && fod && tfn && tfd && ton && tod &&
		  sn && sd && un && ud;
	if (ok) ok = bvn_unit_to_si_rational(from, ffn, ffd, fon, fod, &exf);
	if (ok) ok = bvn_unit_to_si_rational(to,   tfn, tfd, ton, tod, &ext);
	if (ok) {
		*exact = exf && ext;
		/* si = value*(from factor) + (from offset) */
		ok = rat_mul(sn, sd, vnum, vden, ffn, ffd) &&
		     rat_add(sn, sd, sn, sd, fon, fod);
		/* out = (si - to offset) / (to factor) = (si - off) * tfd/tfn */
		if (ok) {
			bvn_int_t *nton = bvn_int_alloc();
			ok = nton && bvn_int_copy(nton, ton);
			if (ok && !bvn_int_is_zero(nton)) nton->negative = !nton->negative;
			if (ok) ok = rat_add(un, ud, sn, sd, nton, tod);   /* si - to off */
			bvn_int_free(nton);
		}
		if (ok) ok = rat_mul(out_num, out_den, un, ud, tfd, tfn);
	}
	bvn_int_free(ffn); bvn_int_free(ffd); bvn_int_free(fon); bvn_int_free(fod);
	bvn_int_free(tfn); bvn_int_free(tfd); bvn_int_free(ton); bvn_int_free(tod);
	bvn_int_free(sn); bvn_int_free(sd); bvn_int_free(un); bvn_int_free(ud);
	return ok;
}
/*
 * Render an exact rational num/den as a positional string in `base` (2..62, 64,
 * 85). When the fraction terminates in that base the full exact expansion is
 * written and *exact is set true. When it does not (e.g. 1/3 in base 10) nothing
 * is written, *exact is set false, and BVN_RATIONAL_NONTERMINATING is returned —
 * the rational stays exact, so the reader can still hand num/den to a caller
 * that asked for them. -1 is reserved for genuine failures, including a buffer
 * too small: this function never truncates, because half of an exact expansion
 * is simply a different (wrong) number.
 */
static bool bvn_int_is_one(const bvn_int_t *n)
{
	return n && !n->negative && n->nused == 1u && n->limbs && n->limbs[0] == 1u;
}
size_t bvn_rational_str_bufsize(const bvn_int_t *num, const bvn_int_t *den,
				uint32_t base)
{
	if (!num || !den || !bvn_rational_base_valid(base))
		return 0u;
	int nb = bvn_int_bitlen(num), db = bvn_int_bitlen(den);
	/* Integer digits are bounded by the numerator's width; fraction digits by
	 * the denominator's, since every emitted digit divides out a factor >= 2.
	 * +3 covers the sign, the radix point and the NUL. */
	return bvn_int_str_bufsize((uint32_t)(nb > 0 ? nb : 1), base) +
	       (size_t)(db > 0 ? db : 1) + 3u;
}
int32_t bvn_rational_to_str(const bvn_int_t *num, const bvn_int_t *den,
			    uint32_t base, char *buf, size_t bufsize, bool *exact)
{
	if (!num || !den || !buf || bufsize < 2 || !bvn_rational_base_valid(base))
		return -1;
	if (exact) *exact = false;
	bvn_int_t *n = bvn_int_alloc(), *d = bvn_int_alloc();
	bvn_int_t *q = bvn_int_alloc(), *r = bvn_int_alloc();
	bvn_int_t *bb = bvn_int_alloc(), *g = bvn_int_alloc();
	bvn_int_t *dd = bvn_int_alloc(), *qq = bvn_int_alloc(), *rr = bvn_int_alloc();
	bvn_int_t *bk = bvn_int_alloc(), *m = bvn_int_alloc(), *fi = bvn_int_alloc();
	char *ibuf = NULL, *fbuf = NULL;
	char zdig[4];
	int32_t ret = -1;
	bool neg = false, terminates = true;
	int32_t k = 0;
	if (!n || !d || !q || !r || !bb || !g || !dd || !qq || !rr ||
	    !bk || !m || !fi) goto done;
	if (!bvn_int_copy(n, num) || !bvn_int_copy(d, den) || !rat_reduce(n, d))
		goto done;
	neg = n->negative;
	n->negative = false;
	/* Bases 64 and 85 spend '+'/'-' on digits, so they have no sign character
	 * and cannot represent a negative value at all (same rule bvn_int_to_str
	 * enforces). Refuse rather than emit a '-' that would read back as a digit. */
	if (neg && (base == 64u || base == 85u)) goto done;
	/* The zero digit is not '0' outside the alphanumeric bases (Base64 spells it
	 * 'A', Ascii85 '!'), and it is needed both to pad leading fraction zeros and
	 * to trim trailing ones. Get it from the canonical renderer. */
	bvn_int_zero(q);
	if (bvn_int_to_str(q, zdig, sizeof zdig, base) != 1) goto done;
	if (!bvn_int_from_uint64(bb, base)) goto done;
	if (!bvn_int_divrem(q, r, n, d)) goto done;             /* integer part */
	/* Strip primes shared with `base` from the (reduced) denominator; if what
	 * remains is 1 the fraction terminates in `base`, and k counts the digits. */
	if (!bvn_int_copy(dd, d)) goto done;
	while (!bvn_int_is_zero(r) && !bvn_int_is_one(dd)) {
		if (!bvn_int_gcd(g, dd, bb)) goto done;
		if (bvn_int_is_one(g)) { terminates = false; break; }
		if (!bvn_int_divrem(qq, rr, dd, g) || !bvn_int_copy(dd, qq)) goto done;
		if (++k > 1000000) goto done;                       /* safety bound */
	}
	if (!terminates) {
		/* Not an error in itself: the rational IS exact, only its positional
		 * expansion in this base is infinite. Distinguished from a hard failure
		 * so a caller can still take num/den. */
		ret = BVN_RATIONAL_NONTERMINATING;
		goto done;
	}
	/* Size and render the integer part and (if any) the fractional integer. */
	int qbits = bvn_int_bitlen(q);
	size_t ibsz = bvn_int_str_bufsize((uint32_t)(qbits > 0 ? qbits : 1), base);
	ibuf = malloc(ibsz);
	if (!ibuf) goto done;
	int32_t ilen = bvn_int_to_str(q, ibuf, ibsz, base);
	if (ilen < 0) goto done;
	int32_t flen = 0, fend = 0, pad = 0;
	if (!bvn_int_is_zero(r)) {
		/* fractional integer = r * (base^k / d), exactly k base-digits */
		if (!bvn_int_from_uint64(bk, 1u)) goto done;
		for (int32_t e = 0; e < k; e++)
			if (!bvn_int_mul(bk, bk, bb)) goto done;
		if (!bvn_int_divrem(m, rr, bk, d) || !bvn_int_mul(fi, r, m)) goto done;
		int fbits = bvn_int_bitlen(fi);
		size_t fbsz = bvn_int_str_bufsize((uint32_t)(fbits > 0 ? fbits : 1), base);
		fbuf = malloc(fbsz);
		if (!fbuf) goto done;
		flen = bvn_int_to_str(fi, fbuf, fbsz, base);
		if (flen < 0) goto done;
		fend = flen;
		while (fend > 0 && fbuf[fend - 1] == zdig[0]) fend--;   /* trim */
		pad  = k - flen;                                       /* leading zeros */
	}
	/* Assemble: [-]<int>[.<zero-pad><frac, trailing zeros trimmed>]. The exact
	 * length is known now, so a buffer that cannot hold it is refused outright —
	 * truncating here would hand back a WRONG number under an exact-value
	 * contract. Size with bvn_rational_str_bufsize. */
	{
		size_t need = (size_t)(neg ? 1 : 0) + (size_t)ilen +
			      (bvn_int_is_zero(r) ? 0u : (size_t)(1 + pad + fend)) + 1u;
		if (need > bufsize) goto done;
		size_t pos = 0;
		if (neg) buf[pos++] = '-';
		for (int32_t e = 0; e < ilen; e++)
			buf[pos++] = ibuf[e];
		if (!bvn_int_is_zero(r)) {
			buf[pos++] = '.';
			for (int32_t e = 0; e < pad; e++)  buf[pos++] = zdig[0];
			for (int32_t e = 0; e < fend; e++) buf[pos++] = fbuf[e];
		}
		buf[pos] = '\0';
		if (exact) *exact = true;
		ret = (int32_t)pos;
	}
done:
	free(ibuf); free(fbuf);
	bvn_int_free(n); bvn_int_free(d); bvn_int_free(q); bvn_int_free(r);
	bvn_int_free(bb); bvn_int_free(g); bvn_int_free(dd); bvn_int_free(qq);
	bvn_int_free(rr); bvn_int_free(bk); bvn_int_free(m); bvn_int_free(fi);
	return ret;
}
/*
 * Algebraically simplify a compound unit: combine repeated bases by summing
 * their exponents (m·m -> m²), fold all the prefix powers into a single scalar
 * *scale (so km -> m with scale 1000), drop components whose exponent cancels to
 * zero, and sort the survivors (numerator before denominator, then by exponent
 * magnitude, then base) for a canonical form. Exponents whose magnitude exceeds
 * the representable range (>9) are folded into *scale instead and flagged via
 * *overflow. This underlies the BVN_UNIT_REDUCE formatting option and the
 * named-SI collapse.
 */
value_unit_t bvn_unit_reduce(value_unit_t u, double *scale, bool *overflow)
{
	bvn_verify_conv_table();
	*scale = 1.0;
	if (overflow)
		*overflow = false;
	typedef struct {
		value_base_unit_t base;
		int32_t           exp_sum;
		int32_t           si_pexp_sum;
		int32_t           iec_pexp_sum;
	} accum_t;
	accum_t acc[BVNR_MAX_UNIT_COMPONENTS];
	uint32_t acc_count = 0;
	memset(acc, 0, sizeof(acc));
	for (uint32_t i = 0; i < u.num_components && i < BVNR_MAX_UNIT_COMPONENTS; i++) {
		const value_unit_component_t *c = &u.components[i];
		if (c->base == bu_none)
			continue;
		if (c->exponent == exp_invalid)
			continue;
		if (!bvn_prefix_unit_valid(c->prefix, c->base))
			continue;
		int32_t uexp = bvn_exponent_to_int(c->exponent);
		int32_t pexp = bvni_prefix_exp_int(*c);
		accum_t *a = NULL;
		for (uint32_t j = 0; j < acc_count; j++) {
			if (acc[j].base == c->base) {
				a = &acc[j];
				break;
			}
		}
		if (!a) {
			if (acc_count >= BVNR_MAX_UNIT_COMPONENTS)
				continue;
			a = &acc[acc_count++];
			a->base = c->base;
		}
		a->exp_sum += uexp;
		if (c->prefix.system == prefix_iec)
			a->iec_pexp_sum += pexp;
		else
			a->si_pexp_sum  += pexp;
	}
	value_unit_t result = { .num_components = 0 };
	for (uint32_t ai = 0; ai < acc_count; ai++) {
		accum_t *a = &acc[ai];
		if (a->si_pexp_sum != 0) {
			*scale *= bvni_pow10(a->si_pexp_sum);
			if (isinf(*scale) && overflow)
				*overflow = true;
		}
		if (a->iec_pexp_sum != 0) {
			*scale *= bvni_ipow(2.0, a->iec_pexp_sum);
			if (isinf(*scale) && overflow)
				*overflow = true;
		}
		if (a->exp_sum == 0)
			continue;
		if (result.num_components >= BVNR_MAX_UNIT_COMPONENTS) {
			if (overflow)
				*overflow = true;
			continue;
		}
		int32_t abs_sum = (a->exp_sum < 0) ? -a->exp_sum : a->exp_sum;
		if (abs_sum > 9) {
			if (overflow)
				*overflow = true;
			const bvn_si_conv_entry_t *conv =
			        bvn_find_si_conv(a->base);
			if (conv) {
				double contrib = bvni_ipow(conv->to_si_factor, abs_sum);
				if (a->exp_sum < 0)
					*scale /= contrib;
				else
					*scale *= contrib;
			}
			continue;
		}
		value_unit_component_t *rc = &result.components[result.num_components];
		rc->base          = a->base;
		rc->exponent      = bvn_int_to_exponent(a->exp_sum);
		rc->prefix.system = prefix_si;
		rc->prefix.id.si  = si_none;
		result.num_components++;
	}
	for (uint32_t i = 1; i < result.num_components; i++) {
		value_unit_component_t tmp = result.components[i];
		int32_t ei = bvn_exponent_to_int(tmp.exponent);
		uint32_t j = i;
		while (j > 0) {
			int32_t ej = bvn_exponent_to_int(result.components[j - 1].exponent);
			bool should_swap = false;
			if (ei >= 0 && ej < 0) {
				should_swap = true;
			} else if (ej >= 0 && ei < 0) {
				should_swap = false;
			} else {
				int32_t ai_val = (ei < 0) ? -ei : ei;
				int32_t aj_val = (ej < 0) ? -ej : ej;
				if (ai_val != aj_val) {
					should_swap = (ai_val > aj_val);
				} else {
					should_swap = (tmp.base < result.components[j - 1].base);
				}
			}
			if (!should_swap)
				break;
			result.components[j] = result.components[j - 1];
			j--;
		}
		result.components[j] = tmp;
	}
	return result;
}
/*
 * Per-unit prefix policy, generated from the .prefix field in
 * src/gendata/units.bvnr — which units take which prefixes is now data, not code.
 * Unlisted slots (currencies, bu_none) default to BVN_PFX_DEFAULT; currencies
 * are handled by the currency rule before this table is consulted.
 */
typedef enum {
	BVN_PFX_DEFAULT = 0, BVN_PFX_INFO, BVN_PFX_GERMAN, BVN_PFX_RATIO, BVN_PFX_NONE
} bvn_prefix_policy_t;
static const uint8_t bu_prefix_policy[BVN_VALUE_BASE_UNIT_COUNT] = {
#include "bovnar_prefix_policy.gen.inc"
};
/*
 * Enforce which prefixes may legally attach to which base unit — the rule set
 * that makes "kg" and "Kib" valid but "Ks" (binary prefix on seconds) or
 * "kPfund" (prefix on a historical German unit) invalid. Currencies defer to
 * the currency rule; information units (bit/byte) accept IEC binary prefixes and
 * only SI prefixes >= kilo; German historical and ratio units (%/ppm/...) accept
 * no prefix at all; everything else accepts any SI prefix. The per-unit class is
 * read from bu_prefix_policy (generated); the rule logic below is fixed.
 */
bool bvn_prefix_unit_valid(value_unit_prefix_t prefix, value_base_unit_t base)
{
	if ((uint32_t)prefix.system >= BVN_PREFIX_SYSTEM_COUNT)
		return false;
	if ((uint32_t)base >= BVN_VALUE_BASE_UNIT_COUNT)
		return false;
	if (bvn_unit_is_currency((int)base))
		return bvn_currency_prefix_valid((int)base, (int)prefix.system);
	bvn_prefix_policy_t pol = (bvn_prefix_policy_t)bu_prefix_policy[base];
	bool is_info = (pol == BVN_PFX_INFO);
	if (pol == BVN_PFX_GERMAN || pol == BVN_PFX_RATIO || pol == BVN_PFX_NONE) {
		if (prefix.system == prefix_iec)
			return prefix.id.iec == iec_none;
		return prefix.id.si == si_none;
	}
	if (prefix.system == prefix_iec)
		return (prefix.id.iec == iec_none) || is_info;
	if (is_info && prefix.system == prefix_si)
		return prefix.id.si == si_none || prefix.id.si >= si_kilo;
	return true;
}
