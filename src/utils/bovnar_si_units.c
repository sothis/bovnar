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
	[bu_none]               = { bu_none,               1.0,        {0, 0, 0, 0, 0, 0, 0}, false, 0.0    },
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
