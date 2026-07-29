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
#include "bvn_profile_impl.h"
#include <limits.h>
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
/*
 * These two were exhaustive switches over the ±1..±9 enumerators. With the
 * range now [BVN_EXPONENT_MIN, BVN_EXPONENT_MAX] a switch cannot enumerate it,
 * so both are range checks — and both still map everything outside the domain
 * onto exp_invalid/0 rather than asserting. A library must not abort its host
 * over a struct a caller filled in wrongly, and the shipped Release build keeps
 * asserts live, so `u.components[0].exponent = 4000` has to be survivable.
 */
int32_t bvn_exponent_to_int(unit_exponent_t e)
{
	int32_t n = (int32_t)e;
	if (n < BVN_EXPONENT_MIN || n > BVN_EXPONENT_MAX)
		return 0;
	return n;
}
unit_exponent_t bvn_int_to_exponent(int32_t n)
{
	if (n < BVN_EXPONENT_MIN || n > BVN_EXPONENT_MAX)
		return exp_invalid;
	return (unit_exponent_t)n;
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
 * The master conversion table, one row per defined base unit and indexed by
 * bvni_unit_slot() — NOT by the base unit id, which is blocked and sparse (see
 * the id-space note in bovnar.h). The designated initialisers spell their index
 * as BVN_SLOT_*(bu_...) so a row and its enumerator cannot drift apart, and
 * bvn_verify_conv_table re-checks the mapping in debug builds.
 *
 * to_si_factor scales the unit to coherent SI (e.g. inch -> 0.0254 m, hour ->
 * 3600 s); dims is its dimension exponent vector; is_affine/affine_offset
 * handle the temperature scales. Currencies have no row at all — they are the
 * business of bovnar_currency.c, not of SI conversion.
 */
static const bvn_si_conv_entry_t si_conv_table[BVN_UNIT_SLOT_COUNT] = {
	[0]                     = { bu_none,               1.0,        {0, 0, 0, 0, 0, 0, 0}, false, 0.0,    "1", "1", "0", "1", true },
	/*
	 * The native units — generated from src/gendata/units.bvnr by gen_units.py.
	 */
#include "bovnar_si_conv_table.gen.inc"
	/*
	 * The profiles' opaque units — generated from the profile data files by
	 * gen_profiles.py. These rows exist so the table stays well-formed
	 * (bvn_verify_conv_table asserts every slot's .base maps back to it); their
	 * VALUES are never used, because bvn_unit_to_si_factor and
	 * bvn_unit_dimension_vector refuse an opaque component outright. That
	 * refusal is what makes them incommensurable — without it the 1.0 factor and
	 * zero dimension vector here would say [IU] is a dimensionless quantity
	 * worth one, i.e. a plain count.
	 */
#include "bovnar_profiles_conv.gen.inc"
};
#define SI_CONV_TABLE_SIZE \
	((uint32_t)(sizeof(si_conv_table) / sizeof(si_conv_table[0])))
/*
 * Structural validity of a unit, WITHOUT requiring an SI conversion row.
 *
 * This is the shared precondition of the identity short-circuits below. They
 * must accept a unit that has no SI mapping — a currency — and one whose factor
 * is irrational, because neither property means anything for the identity map.
 * What they must NOT accept is a malformed unit: an exp_invalid component, a
 * prefix the base does not allow, a base index off the end of the table. Every
 * other entry point in this file rejects those, and skipping the check here
 * would make the identity path the one place in the library that silently
 * accepts a unit nothing else will.
 */
static bool bvn_unit_structurally_valid(value_unit_t u)
{
	if (u.num_components > BVNR_MAX_UNIT_COMPONENTS)
		return false;
	for (uint32_t i = 0; i < u.num_components; i++) {
		const value_unit_component_t *c = &u.components[i];
		if (!bvni_base_defined(c->base))              return false;
		if (c->exponent == exp_invalid)               return false;
		if (!bvn_prefix_unit_valid(c->prefix, c->base)) return false;
	}
	return true;
}
/*
 * Every slot's row names the unit that slot belongs to. A hole would be a row
 * of zeros — factor 0.0, no dimensions, .base == bu_none — which reads back as
 * a perfectly plausible unit rather than as missing data, so this is checked
 * rather than assumed. Slot 0 is bu_none's own row and maps to itself.
 */
static void bvn_verify_conv_table(void)
{
#ifndef NDEBUG
	for (uint32_t i = 0; i < SI_CONV_TABLE_SIZE; i++) {
		assert(bvni_unit_slot(si_conv_table[i].base) == (int32_t)i &&
		       "si_conv_table: row does not map back to its own slot");
	}
#endif
}
static const bvn_si_conv_entry_t *bvn_find_si_conv(value_base_unit_t bu)
{
	int32_t slot = bvni_unit_slot(bu);
	/* A currency lands here as slot -1: it has a catalogue row, not a
	 * conversion row, and asking for its SI factor is a category error. */
	if (slot < 0 || (uint32_t)slot >= SI_CONV_TABLE_SIZE)
		return NULL;
	return &si_conv_table[slot];
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
		/*
		 * spec 1.2 — a UCUM arbitrary unit is assay-defined and commensurable
		 * with nothing, so it has no SI factor to report. Refusing here is what
		 * enforces that: bvn_units_compatible and bvn_unit_convert_factor are
		 * both built on this function and on bvn_unit_dimension_vector, so one
		 * refusal in each place makes [IU] incompatible with mol, with a plain
		 * count, and with [PFU] — while bvn_unit_convert_factor's prefix-only
		 * fallback still relates [IU]/L to [IU]/mL, exactly as it does for a
		 * currency, which has no SI row for the same reason.
		 */
		if (bvni_is_opaque(c->base)) {
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
			/* An affine scale has an SI meaning only ALONE and at exponent 1.
			 * The offset is a number of kelvin; there is nowhere to add it in a
			 * product whose SI unit is K/s or K*m, and the code that tried
			 * added it unscaled — so 20 °C/h converted to K/h as 983360, and
			 * 20 °C*m to K*m as 293.15 whatever the metres did. Both are
			 * arithmetic on a quantity that does not exist. Refusing is what
			 * the reader turns into error_unit_mismatch, and it agrees with
			 * pint, which forbids an offset unit in a product outright.
			 * `°C/h` still PARSES and is still a valid annotation — a consumer
			 * that means a temperature difference can read the components
			 * itself; what it no longer gets is a wrong SI value. */
			if (uexp != 1 || u.num_components != 1) {
				*ok = false;
				return f;
			}
			*is_affine     = true;
			*affine_offset = conv->affine_offset;
		}
	}
	/* Extreme prefix/exponent combinations (e.g. Q~m^9, or several huge
	 * components multiplied together) can overflow the running product to ±inf,
	 * or produce nan via 0*inf. Surfacing that as a failure — rather than handing
	 * back a non-finite factor with *ok still true — mirrors how bvn_unit_reduce
	 * flags isinf, and stops callers like bvn_dom_get_value_in_base_units from
	 * silently emitting inf/nan values.
	 *
	 * Zero is the same failure at the other end and was not caught, because 0.0
	 * is perfectly finite. q~m^100 is 10^-3000, which underflows to exactly
	 * 0.0, and the function reported success with a factor of zero — so every
	 * value in that unit converted to 0 with nothing to say it had not. No real
	 * unit has an SI factor of zero (every to_si_factor and every prefix is
	 * positive and non-zero), so a zero here can only be underflow. */
	if (!isfinite(f) || f == 0.0) {
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
		/* An arbitrary unit has no dimension — see bvn_unit_to_si_factor. */
		if (bvni_is_opaque(c->base))
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
/*
 * Quantity kinds: units that carry NO SI dimension yet are not interchangeable
 * with a pure number. The SI dimension vector cannot separate them — they are
 * all [0,0,0,0,0,0,0] — so each kind is tracked by net exponent, exactly as a
 * dimension would be, and two units are only compatible if their kind vectors
 * agree as well.
 *
 *   information  — bit and byte are separate kinds: a data rate is not a
 *                  frequency, and this format does not silently trade 8 bits
 *                  for a byte either.
 *   angle        — one SHARED kind, because degree -> radian is a conversion
 *                  people legitimately want; only the factor is irrational.
 *                  Giving each angle unit its own kind would break that. What
 *                  the kind does stop is an angle drifting into a plain count:
 *                  rev/min is an angular rate and rpm is a cycle rate, and with
 *                  a revolution defined as 2*pi radians they differ by exactly
 *                  that factor -- so the two used to "convert" into each other
 *                  2*pi wrong. Same for rad/s against Hz, which are angular
 *                  frequency and frequency.
 *                  Steradian carries weight 2 because a steradian IS rad^2, so
 *                  sr <-> rad^2 keeps working. The photometric units built ON
 *                  the steradian carry that same weight: a lumen IS cd*sr and a
 *                  lux IS lm/m^2, so without it the table contradicted itself —
 *                  it refused lm <-> cd*sr on the kind rule while happily
 *                  converting lm <-> cd at factor 1, which is the same claim
 *                  with the sr silently dropped. With the weight, luminous flux
 *                  (lm, weight 2) stops converting into luminous intensity (cd,
 *                  weight 0) and illuminance (lx, ph) stops converting into
 *                  luminance (sb, cd/m^2) — the distinction the SI dimension
 *                  vector cannot carry, since every photometric unit reduces to
 *                  candela in base dimensions.
 *   logarithmic  — Np, dB and pH are separate kinds. They are logarithms of a
 *                  ratio, not linear quantities (20 dB is a ratio of 100, not
 *                  twice 10 dB), so no factor can relate them; dB is ambiguous
 *                  on its own besides, being 8.686 or 4.343 per neper depending
 *                  on whether the quantity is a power or a field. pH is the
 *                  same argument in a different field: it is -log10(activity),
 *                  so pH 7 is not "7 of" anything and a pH one unit lower is a
 *                  tenfold concentration. Without its own kind it would carry
 *                  the empty dimension and a factor of 1, and therefore convert
 *                  freely into a plain count or a percentage — the exact
 *                  wrong-by-orders-of-magnitude answer this table exists to
 *                  refuse.
 *
 *   turbidity    — NTU, FNU, FTU, FAU and JTU are five separate kinds, one per
 *                  measurement method. The formazin-calibrated ones are
 *                  numerically equal ON a formazin standard, which is exactly
 *                  the trap: NTU is white-light (EPA 180.1), FNU near-infrared
 *                  (ISO 7027), FTU the same family with the geometry left
 *                  unstated, and on real water those optics disagree by an
 *                  amount that depends on particle size and colour. FAU is not
 *                  even the same optical quantity — it measures attenuation in
 *                  the transmitted beam rather than sideways scatter. JTU is
 *                  the visual candle method, whose relation to formazin is
 *                  nonlinear and sample-dependent. No factor relates any pair
 *                  of them, which is why every standard demands the method be
 *                  reported rather than the number alone.
 *   salinity     — practical salinity (PSS-78) is a conductivity RATIO, so it
 *                  is dimensionless by construction and "PSU" is a label, not a
 *                  unit. It is not a mass fraction: S_P 35 is about 35.165 g/kg
 *                  absolute salinity, so letting it convert to per-mille or
 *                  g/kg would be wrong by half a percent — write the mass
 *                  fraction directly if that is what you mean.
 *
 * Percent, per-mille, ppm and friends are deliberately NOT here: those are pure
 * ratios and converting 1 % to 0.01 is exactly right. Nor is the hydroponic
 * conductivity factor (CF): that one really is a conductivity, just rescaled,
 * so it carries siemens-per-metre dimensions and converts to µS/cm as it should.
 */
typedef enum {
	BVNI_KIND_INFO_BIT = 0,
	BVNI_KIND_INFO_BYTE,
	BVNI_KIND_ANGLE,
	BVNI_KIND_LOG_NEPER,
	BVNI_KIND_LOG_DECIBEL,
	BVNI_KIND_LOG_PH,
	BVNI_KIND_TURBIDITY_NTU,
	BVNI_KIND_TURBIDITY_FNU,
	BVNI_KIND_TURBIDITY_FTU,
	BVNI_KIND_TURBIDITY_FAU,
	BVNI_KIND_TURBIDITY_JTU,
	BVNI_KIND_SALINITY_PSU,
	BVNI_KIND_COUNT
} bvni_quantity_kind_t;
typedef struct {
	value_base_unit_t    base;
	bvni_quantity_kind_t kind;
	int32_t              weight;   /* how many of the kind one unit is worth */
} bvni_kind_entry_t;
static const bvni_kind_entry_t bvni_kind_table[] = {
	{ bu_bit,        BVNI_KIND_INFO_BIT,    1 },
	{ bu_byte,       BVNI_KIND_INFO_BYTE,   1 },
	{ bu_radian,     BVNI_KIND_ANGLE,       1 },
	{ bu_degree,     BVNI_KIND_ANGLE,       1 },
	{ bu_arcminute,  BVNI_KIND_ANGLE,       1 },
	{ bu_arcsecond,  BVNI_KIND_ANGLE,       1 },
	{ bu_grad,       BVNI_KIND_ANGLE,       1 },
	{ bu_revolution, BVNI_KIND_ANGLE,       1 },
	{ bu_steradian,  BVNI_KIND_ANGLE,       2 },
	/* lm = cd*sr, lx = lm/m^2, ph = lm/cm^2 — each carries one steradian.
	 * bu_candela and bu_stilb (cd/cm^2) deliberately do NOT: they are luminous
	 * intensity and luminance, which is exactly what this separates them from. */
	{ bu_lumen,      BVNI_KIND_ANGLE,       2 },
	{ bu_lux,        BVNI_KIND_ANGLE,       2 },
	{ bu_phot,       BVNI_KIND_ANGLE,       2 },
	{ bu_neper,      BVNI_KIND_LOG_NEPER,   1 },
	{ bu_decibel,    BVNI_KIND_LOG_DECIBEL, 1 },
	{ bu_ph_scale,   BVNI_KIND_LOG_PH,      1 },
	{ bu_turbidity_ntu,      BVNI_KIND_TURBIDITY_NTU, 1 },
	{ bu_turbidity_fnu,      BVNI_KIND_TURBIDITY_FNU, 1 },
	{ bu_turbidity_ftu,      BVNI_KIND_TURBIDITY_FTU, 1 },
	{ bu_turbidity_fau,      BVNI_KIND_TURBIDITY_FAU, 1 },
	{ bu_turbidity_jtu,      BVNI_KIND_TURBIDITY_JTU, 1 },
	{ bu_practical_salinity, BVNI_KIND_SALINITY_PSU,  1 },
};
#define BVNI_KIND_TABLE_COUNT \
	((uint32_t)(sizeof(bvni_kind_table) / sizeof(bvni_kind_table[0])))
/* Net exponent of every quantity kind in `u`. False if a component's exponent
 * is unusable, which makes the unit incomparable rather than equal-to-anything. */
static bool bvni_kind_exponents(value_unit_t u, int32_t out[BVNI_KIND_COUNT])
{
	for (uint32_t k = 0; k < BVNI_KIND_COUNT; k++)
		out[k] = 0;
	for (uint32_t i = 0; i < u.num_components && i < BVNR_MAX_UNIT_COMPONENTS; i++) {
		int32_t e = bvn_exponent_to_int(u.components[i].exponent);
		if (e == 0)
			return false;
		for (uint32_t t = 0; t < BVNI_KIND_TABLE_COUNT; t++) {
			if (bvni_kind_table[t].base == u.components[i].base) {
				out[bvni_kind_table[t].kind] +=
					bvni_kind_table[t].weight * e;
				break;
			}
		}
	}
	return true;
}
/* True when both units carry the same amount of every quantity kind. Exposed
 * within the library so the formatter's named-SI collapse can apply the same
 * test — collapsing "B/s" onto "Hz" passes the dimension check but produces a
 * unit that is not compatible with what it replaced. */
bool bvni_kinds_match(value_unit_t a, value_unit_t b)
{
	int32_t ka[BVNI_KIND_COUNT], kb[BVNI_KIND_COUNT];
	if (!bvni_kind_exponents(a, ka) || !bvni_kind_exponents(b, kb))
		return false;
	for (uint32_t k = 0; k < BVNI_KIND_COUNT; k++)
		if (ka[k] != kb[k])
			return false;
	return true;
}
/*
 * Two units are inter-convertible iff they have the same SI dimension vector AND
 * the same net exponent for every quantity kind above. bvn_unit_convert_factor
 * builds on this to return the actual multiplier a->b, refusing affine
 * conversions unless the offsets match.
 */
bool bvn_units_compatible(value_unit_t a, value_unit_t b)
{
	if (!bvni_kinds_match(a, b))
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
/*
 * Two units that are the same apart from their PREFIXES, and the exact scale
 * between them.
 *
 * This exists for units with no SI conversion row — currencies. They carry no
 * dimension by design, so bvn_unit_dimension_vector fails for them and
 * bvn_units_compatible calls every currency incompatible even with itself. The
 * identity short-circuit rescues "$USD -> $USD", but "k~$USD -> $USD" is not an
 * identity: the two differ by exactly one prefix, and nothing between them but a
 * factor of 1000. Every conversion entry point refused it, so a prefixed
 * currency could neither be read through want_unit nor written under
 * BVN_UNIT_REDUCE.
 *
 * The match is a multiset over (base, exponent) — unit multiplication commutes,
 * so "$USD·oz_t⁻¹" and "oz_t⁻¹·$USD" are the same unit. The scale comes out as
 * separate powers of ten and two, because SI prefixes are decimal and IEC ones
 * binary; keeping them apart is what lets the rational path stay exact.
 *
 * Only ever consulted AFTER bvn_units_compatible has said no, so a unit that
 * does have an SI row keeps taking the normal, fully general path.
 */
static bool bvn_unit_prefix_only_delta(value_unit_t a, value_unit_t b,
				       int32_t *si_delta, int32_t *iec_delta)
{
	if (a.num_components != b.num_components) return false;
	if (!bvn_unit_structurally_valid(a) || !bvn_unit_structurally_valid(b))
		return false;
	uint32_t n = a.num_components < BVNR_MAX_UNIT_COMPONENTS
		   ? a.num_components : BVNR_MAX_UNIT_COMPONENTS;
	bool used[BVNR_MAX_UNIT_COMPONENTS] = { false };
	for (uint32_t i = 0; i < n; i++) {
		bool found = false;
		for (uint32_t j = 0; j < n; j++) {
			if (used[j]) continue;
			if (a.components[i].base     != b.components[j].base)     continue;
			if (a.components[i].exponent != b.components[j].exponent) continue;
			used[j] = true;
			found   = true;
			break;
		}
		if (!found) return false;
	}
	int32_t si = 0, iec = 0;
	for (uint32_t i = 0; i < n; i++) {
		if (a.components[i].prefix.system == prefix_iec)
			iec += bvni_prefix_exp_int(a.components[i]);
		else
			si  += bvni_prefix_exp_int(a.components[i]);
		if (b.components[i].prefix.system == prefix_iec)
			iec -= bvni_prefix_exp_int(b.components[i]);
		else
			si  -= bvni_prefix_exp_int(b.components[i]);
	}
	*si_delta  = si;
	*iec_delta = iec;
	return true;
}
/*
 * The predicate a conversion SELECTOR wants, as opposed to the one a dimensional
 * analysis wants.
 *
 * bvn_units_compatible answers "do these two carry the same physical dimension",
 * and a currency deliberately carries none — so it reports false for
 * "k~$USD -> $USD" and even for "$USD -> $USD", both of which the conversion
 * entry points perform correctly. Anything picking a target by asking
 * bvn_units_compatible first therefore silently declines work the library can
 * do; every hand-written want_unit hook that screens its targets has this hole.
 *
 * bvn_unit_convert_factor is not the answer either: it reports ok=false for
 * "°F -> °C", because an affine conversion has no single multiplicative factor.
 * Screening on it drops every temperature in the format.
 *
 * So: dimensionally compatible, OR the same unit apart from its prefixes. That
 * is exactly the set bvn_unit_convert_value and bvn_unit_convert_rational
 * accept, which is what makes this the right question to ask before calling
 * them.
 */
bool bvn_units_convertible(value_unit_t a, value_unit_t b)
{
	if (bvn_units_compatible(a, b))
		return true;
	int32_t si_delta = 0, iec_delta = 0;
	return bvn_unit_prefix_only_delta(a, b, &si_delta, &iec_delta);
}
/*
 * The coherent SI form of `u` — the product of SI base units carrying the same
 * dimension, with every prefix folded out except the kilogram's (the SI base
 * unit for mass IS the kilogram, so mass comes back as k~g, not g).
 *
 * Returns false, leaving *out untouched, when `u` has no coherent SI form to
 * name: a currency (no dimension vector at all), or a unit whose dimension
 * vector is entirely zero. That second case covers more than it first appears —
 * %, ppm, dB, pH, rad, ° and the turbidity scales are all dimensionless — and
 * refusing them is deliberate rather than a limitation. Normalising a ratio
 * would silently turn "35 %" into a bare 0.35, and normalising an angle would
 * ask for the irrational factor between ° and rad. Neither is something a
 * caller who asked for "SI" is asking for.
 *
 * Also false when the dimension vector cannot be spelled as a unit: an exponent
 * outside the -9..9 the notation carries.
 */
bool bvn_unit_si_normal_form(value_unit_t u, value_unit_t *out)
{
	static const struct {
		bvn_si_dim_idx_t  dim;
		value_base_unit_t base;
		si_prefix_id_t    prefix;
	} si_base[] = {
		{ bvn_si_dim_meter,    bu_meter,   si_none },
		{ bvn_si_dim_kilogram, bu_gram,    si_kilo },
		{ bvn_si_dim_second,   bu_second,  si_none },
		{ bvn_si_dim_ampere,   bu_ampere,  si_none },
		{ bvn_si_dim_kelvin,   bu_kelvin,  si_none },
		{ bvn_si_dim_mol,      bu_mol,     si_none },
		{ bvn_si_dim_candela,  bu_candela, si_none },
	};
	if (!out)
		return false;
	/* A unit with no usable SI factor has no SI form either, and the dimension
	 * vector alone will not say so: `s/°C` has perfectly good dimensions, but an
	 * affine scale means nothing at an exponent other than 1, so the conversion
	 * entry points refuse it. Asking the factor path is how that is detected
	 * without restating its rules here. */
	bool is_affine = false, factor_ok = false;
	double offset = 0.0;
	(void)bvn_unit_to_si_factor(u, &is_affine, &offset, &factor_ok);
	if (!factor_ok)
		return false;
	int32_t dims[bvn_si_dim_count];
	if (!bvn_unit_dimension_vector(u, dims))
		return false;
	value_unit_t r;
	memset(&r, 0, sizeof(r));
	for (uint32_t i = 0; i < sizeof(si_base) / sizeof(si_base[0]); i++) {
		int32_t e = dims[si_base[i].dim];
		if (e == 0)
			continue;
		/* A dimension exponent unit_exponent_t cannot carry has no SI form to
		 * build. The bound is the type's own — it was ±9 here too, left behind
		 * when the exponent range grew to ±100, which meant km^10 (whose SI
		 * form is simply m^10) reported no SI form at all and a normalising
		 * policy silently left it alone. */
		if (e < BVN_EXPONENT_MIN || e > BVN_EXPONENT_MAX)
			return false;
		if (r.num_components >= BVNR_MAX_UNIT_COMPONENTS)
			return false;
		value_unit_component_t *c = &r.components[r.num_components++];
		c->base            = si_base[i].base;
		c->exponent        = bvn_int_to_exponent(e);
		c->prefix.system   = prefix_si;
		c->prefix.id.si    = si_base[i].prefix;
	}
	if (r.num_components == 0u)
		return false;     /* dimensionless — see the comment above */
	/*
	 * The dimension vector is not the whole of what a unit means, so the form
	 * built from it has to be checked against the unit it came from rather than
	 * assumed equivalent to it. The photometric units are the case that proves
	 * it: lm, lx and ph each carry the steradian's quantity kind, which no SI
	 * dimension vector can express (the steradian is dimensionless), so
	 * reconstructing them from dims alone yields cd and cd/m² — luminous
	 * INTENSITY where the value was luminous FLUX, and luminance where it was
	 * illuminance. bvn_unit_convert_rational refuses those pairs, exactly as it
	 * should; without this check a caller normalising a document would have the
	 * parse aborted by a unit the library was right to refuse.
	 *
	 * Screening here rather than in the caller makes the guarantee structural:
	 * a form this function returns is one the conversion entry points accept.
	 */
	if (!bvn_units_convertible(u, r))
		return false;
	*out = r;
	return true;
}
double bvn_unit_convert_factor(value_unit_t a, value_unit_t b,
			       bool *ok, bool *requires_affine)
{
	*ok             = true;
	*requires_affine = false;
	/* An identity conversion has factor 1 whatever the unit is — no offset, no SI
	 * mapping needed. But this is only a FALLBACK for the pairs the normal path
	 * refuses (a currency, which deliberately has no SI row): every pair it
	 * already handles keeps its previous result, including the requires_affine
	 * signal callers read for °C -> °C. bvn_unit_convert_value and
	 * bvn_unit_convert_rational short-circuit earlier because they must also
	 * rescue an irrational factor, which has no bearing on an identity. */
	if (!bvn_units_compatible(a, b)) {
		/* Same unit apart from prefixes — a currency, which has no SI row and so
		 * cannot be judged by dimension. Identity is just the delta-zero case of
		 * this. */
		int32_t sid = 0, iecd = 0;
		if (bvn_unit_prefix_only_delta(a, b, &sid, &iecd))
			return bvni_pow10(sid) * bvni_ipow(2.0, iecd);
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
	 * unit count as equal — but still structurally checked, so a malformed unit
	 * is rejected here exactly as it is everywhere else. */
	if (bvn_unit_equal(from, to))
		return bvn_unit_structurally_valid(from) ? (*out = value, true) : false;
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
		/* affine offset: only a lone linear temperature carries one — the unit
		 * must BE that temperature, not contain it (see bvn_unit_to_si_factor). */
		if (conv->is_affine) {
			if (uexp != 1 || have_affine || u.num_components != 1) {
				ok = false; break; }
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
		if (!bvn_unit_structurally_valid(from)) return false;
		*exact = true;
		return bvn_int_copy(out_num, vnum) && bvn_int_copy(out_den, vden) &&
		       rat_reduce(out_num, out_den);
	}
	if (!bvn_units_compatible(from, to)) {
		/* Same unit apart from prefixes: the scale is a power of ten times a
		 * power of two, both exact, so this stays lossless without ever touching
		 * si_conv_table — which is the point, since a currency has no row there. */
		int32_t sid = 0, iecd = 0;
		if (!bvn_unit_prefix_only_delta(from, to, &sid, &iecd))
			return false;                       /* genuine dim mismatch */
		*exact = true;
		if (!bvn_int_copy(out_num, vnum) || !bvn_int_copy(out_den, vden))
			return false;
		if (sid  > 0 && !bvn_int_mul_pow10(out_num,  sid))  return false;
		if (sid  < 0 && !bvn_int_mul_pow10(out_den, -sid))  return false;
		if (iecd > 0 && !bvn_int_shl(out_num,  iecd))       return false;
		if (iecd < 0 && !bvn_int_shl(out_den, -iecd))       return false;
		return rat_reduce(out_num, out_den);
	}
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
	 * +3 covers the sign, the radix point and the NUL. Saturating rather than
	 * wrapping: bvn_int_str_bufsize itself saturates at SIZE_MAX for a width no
	 * buffer could hold, and adding to that must not turn a refusal into a tiny
	 * allocation. */
	size_t ipart = bvn_int_str_bufsize((uint32_t)(nb > 0 ? nb : 1), base);
	size_t fpart = (size_t)(db > 0 ? db : 1) + 3u;
	return (ipart > SIZE_MAX - fpart) ? SIZE_MAX : ipart + fpart;
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
	 * 'A', Ascii85 '!'), and it is what trailing fraction zeros are trimmed
	 * against. Get it from the canonical renderer. */
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
	/*
	 * Decide whether the result can fit BEFORE generating the digits. k is the
	 * exact fractional digit count and trailing-zero trimming only shortens the
	 * result, so this is a sound upper bound — and refusing here is what keeps
	 * `bufsize` a usable work limit, since everything below is quadratic in k.
	 */
	{
		size_t upper = (size_t)(neg ? 1 : 0) + (size_t)ilen +
			       (bvn_int_is_zero(r) ? 0u : (size_t)(1 + k)) + 1u;
		if (upper > bufsize) { ret = BVN_RATIONAL_TOO_LONG; goto done; }
	}
	int32_t fend = 0;
	if (!bvn_int_is_zero(r)) {
		/*
		 * Emit the k fractional digits by long division: digit_i = (r*base)/d,
		 * r = (r*base) mod d, k times.
		 *
		 * The obvious formulation -- build base^k, divide by d, multiply by r and
		 * render the product -- materialises base^k, which overflows the big-int
		 * ceiling long before the ANSWER does. That made a value whose expansion
		 * terminates fail in a large base while succeeding in a small one (1e-6000
		 * rendered in base 40 but not in base 50), reported as "unusable output
		 * base". Dividing step by step keeps every intermediate the size of d, so
		 * only the result's own length bounds what can be rendered -- and the
		 * leading zeros fall out of the loop, so nothing has to count them.
		 */
		fbuf = malloc((size_t)k + 1u);
		if (!fbuf) goto done;
		if (!bvn_int_copy(m, r)) goto done;
		for (int32_t e = 0; e < k; e++) {
			uint64_t dig = 0;
			char one[4];
			if (!bvn_int_mul(m, m, bb)) goto done;
			if (!bvn_int_divrem(fi, rr, m, d)) goto done;
			if (!bvn_int_to_uint64(fi, &dig) || dig >= (uint64_t)base) goto done;
			if (!bvn_int_from_uint64(bk, dig)) goto done;
			if (bvn_int_to_str(bk, one, sizeof one, base) != 1) goto done;
			fbuf[e] = one[0];
			if (!bvn_int_copy(m, rr)) goto done;
		}
		fend = k;
		while (fend > 0 && fbuf[fend - 1] == zdig[0]) fend--;   /* trim */
	}
	/* Assemble: [-]<int>[.<frac, trailing zeros trimmed>]. The exact
	 * length is known now, so a buffer that cannot hold it is refused outright —
	 * truncating here would hand back a WRONG number under an exact-value
	 * contract. Size with bvn_rational_str_bufsize. */
	{
		size_t need = (size_t)(neg ? 1 : 0) + (size_t)ilen +
			      (bvn_int_is_zero(r) ? 0u : (size_t)(1 + fend)) + 1u;
		if (need > bufsize) goto done;
		size_t pos = 0;
		if (neg) buf[pos++] = '-';
		for (int32_t e = 0; e < ilen; e++)
			buf[pos++] = ibuf[e];
		if (!bvn_int_is_zero(r)) {
			buf[pos++] = '.';
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
 * magnitude, then base) for a canonical form. This underlies the
 * BVN_UNIT_REDUCE formatting option and the named-SI collapse.
 *
 * A summed exponent outside [BVN_EXPONENT_MIN, BVN_EXPONENT_MAX] cannot be
 * carried by unit_exponent_t at all. There is nothing honest to return for it,
 * so the component's SI factor is folded into *scale and the component is
 * dropped — which LOSES ITS DIMENSION, and is why *overflow exists. A caller
 * that ignores *overflow gets a unit that is not the one it asked to reduce;
 * bvn_unit_to_string_ex refuses to format one.
 *
 * That threshold used to be 9, left behind when the exponent range grew from
 * ±9 to ±100. It made m^10 reduce to the DIMENSIONLESS unit with scale 1.0 —
 * metre's SI factor being 1.0, the fold left no trace of it — for every
 * exponent the type had just gained.
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
		int32_t uexp = bvn_exponent_to_int(c->exponent);
		if (uexp == 0)                 /* exp_invalid, or outside the enum */
			continue;
		if (!bvn_prefix_unit_valid(c->prefix, c->base))
			continue;
		int32_t pexp = bvni_prefix_exp_int(*c);
		/* bu_none contributes no dimension, so it must not survive into the
		 * reduced unit — but it CAN carry a prefix, and dropping the whole
		 * component dropped that prefix's scale too. Fold the scale in, then
		 * drop it. (bvn_unit_to_si_rational had the same defect; this was the
		 * last function that still disagreed about a dimensionless kilo.) */
		if (c->base == bu_none) {
			if (pexp != 0) {
				*scale *= (c->prefix.system == prefix_iec)
					? bvni_ipow(2.0, pexp) : bvni_pow10(pexp);
				if (isinf(*scale) && overflow)
					*overflow = true;
			}
			continue;
		}
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
		if (abs_sum > BVN_EXPONENT_MAX) {
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
 * src/gendata/units.bvnr — which units take which prefixes is now data, not
 * code. Indexed by bvni_unit_slot(), like si_conv_table. Slot 0 (bu_none) keeps
 * the array's zero default, BVN_PFX_DEFAULT; currencies have no slot and are
 * handled by the currency rule before this table is consulted.
 */
typedef enum {
	BVN_PFX_DEFAULT = 0, BVN_PFX_INFO, BVN_PFX_GERMAN, BVN_PFX_RATIO, BVN_PFX_NONE
} bvn_prefix_policy_t;
static const uint8_t bu_prefix_policy[BVN_UNIT_SLOT_COUNT] = {
#include "bovnar_prefix_policy.gen.inc"
	/* UCUM arbitrary units, from UCUM's own metric flag: [IU] takes prefixes,
	 * [arb'U] does not. Without these the slots keep the array's zero default,
	 * which is BVN_PFX_DEFAULT — "any SI prefix" — and k[arb'U] would translate
	 * although UCUM forbids it. */
#include "bovnar_profiles_policy.gen.inc"
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
	if (!bvni_base_defined(base))
		return false;
	/*
	 * The prefix ID has to be in range before anything else looks at it. Only
	 * the information-unit branch below used to range-check it, so an ID past
	 * the end of the table stayed "valid" everywhere else — and since this is
	 * the one gatekeeper the parser, the writer and the conversion helpers all
	 * share, that leaked straight through the writer: si_prefix_str() has no
	 * symbol for such an ID and returns "", so bvn_write_unit_component emitted
	 * the '~' separator with nothing in front of it ("~m"). The writer reported
	 * success, and the document it produced came back error_unit_illegal on the
	 * next read. bvn_unit_prefix_factor() meanwhile scored the same unit as 1,
	 * silently dropping the scale. Rejecting the ID here makes all three agree.
	 */
	if (prefix.system == prefix_iec) {
		if ((uint32_t)prefix.id.iec >= BVN_IEC_PREFIX_COUNT)
			return false;
	} else if ((uint32_t)prefix.id.si >= BVN_SI_PREFIX_COUNT) {
		return false;
	}
	if (bvn_unit_is_currency((int)base))
		return bvn_currency_prefix_valid((int)base, (int)prefix.system);
	/* Not a currency and bvni_base_defined() said yes, so the slot is real. */
	bvn_prefix_policy_t pol =
		(bvn_prefix_policy_t)bu_prefix_policy[bvni_unit_slot(base)];
	bool is_info = (pol == BVN_PFX_INFO);
	if (pol == BVN_PFX_GERMAN || pol == BVN_PFX_RATIO || pol == BVN_PFX_NONE) {
		if (prefix.system == prefix_iec)
			return prefix.id.iec == iec_none;
		return prefix.id.si == si_none;
	}
	if (prefix.system == prefix_iec)
		return (prefix.id.iec == iec_none) || is_info;
	if (is_info && prefix.system == prefix_si) {
		if (prefix.id.si == si_none)
			return true;
		/* "kilo and up" is a statement about MAGNITUDE, so test the exponent.
		 * The old `prefix.id.si >= si_kilo` tested the enum id instead, which
		 * only agreed because prefixes.bvnr happens to list them in ascending
		 * order — while that same file requires ids to be append-only, so the
		 * first sub-kilo prefix ever added would have been handed a high id and
		 * silently become legal on bit and byte. gen_prefixes.py now also
		 * refuses a non-monotonic list, but this no longer depends on it.
		 * The ID is already known to be in range — the check at the top of the
		 * function covers every branch now, not just this one. */
		return bvni_si_pfx_table[prefix.id.si].exp >= 3;
	}
	return true;
}
