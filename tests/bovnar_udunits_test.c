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

/*
 * The UDUNITS-2 unit profile (spec 1.2). Specified in
 * doc/11_bovnar_unit_profiles.md.
 *
 * The second EXPR profile, so what this file mostly defends is that the shared
 * parser really is shared: that UDUNITS' extra operators work, that its division
 * means what UCUM's division means, and that neither vocabulary's syntax leaked
 * into the other. Plus the two refusals that carry the most weight -- reference
 * time, and the space-separated spelling.
 *
 * What this file does NOT check, and cannot: that "hPa" is really UDUNITS'
 * hectopascal. See the provenance note at the top of src/gendata/udunits.bvnr.
 */

#include <math.h>
#include <stdio.h>
#include <string.h>
#include "bovnar.h"
#include "bovnar_si_units.h"
#include "bvn_internal_dims.h"

static int failures = 0;
static int tests    = 0;

#define ASSERT_TRUE(cond, msg) do { \
	tests++; \
	if (!(cond)) { \
		fprintf(stderr, "FAIL line %d: %s\n", __LINE__, (msg)); \
		failures++; \
	} \
} while (0)

static value_unit_t U(const char* s)
{
	bool ok = true;
	return bvn_parse_unit((const uint8_t*)s, &ok);
}

static void chk_str(const char* src, const char* want)
{
	bool ok = true;
	value_unit_t u = bvn_parse_unit((const uint8_t*)src, &ok);
	tests++;
	if (!ok) {
		fprintf(stderr, "FAIL: %s did not parse (expected %s)\n", src, want);
		failures++;
		return;
	}
	char buf[BVNR_UNIT_STRING_MAX];
	int32_t n = bvn_unit_to_string(u, buf, sizeof(buf));
	if (n < 0 || strcmp(buf, want) != 0) {
		fprintf(stderr, "FAIL: %s -> %s, expected %s\n",
		        src, n < 0 ? "(unwritable)" : buf, want);
		failures++;
	}
}

static void chk_factor(const char* src, double want)
{
	bool ok = true;
	value_unit_t u = bvn_parse_unit((const uint8_t*)src, &ok);
	tests++;
	if (!ok) {
		fprintf(stderr, "FAIL: %s did not parse\n", src);
		failures++;
		return;
	}
	bool aff = false, fok = true;
	double off = 0.0;
	double f = bvn_unit_to_si_factor(u, &aff, &off, &fok);
	if (!fok || fabs(f - want) > fabs(want) * 1e-9) {
		fprintf(stderr, "FAIL: %s factor %.17g, expected %.17g\n",
		        src, f, want);
		failures++;
	}
}

static void chk_error(const char* src, error_code_t want)
{
	bool ok = true;
	(void)bvn_parse_unit((const uint8_t*)src, &ok);
	tests++;
	if (ok) {
		fprintf(stderr, "FAIL: %s parsed, expected a refusal\n", src);
		failures++;
		return;
	}
	error_code_t got = bvn_unit_error_code((const uint8_t*)src,
	                                       (uint32_t)strlen(src));
	if (got != want) {
		fprintf(stderr, "FAIL: %s -> %s, expected %s\n",
		        src, bvn_error_to_string(got), bvn_error_to_string(want));
		failures++;
	}
}

/* ── spellings ──────────────────────────────────────────────────────────── */

static void test_spellings(void)
{
	printf("  symbols, names, and prefixed names...\n");

	/* UDUNITS accepts symbols and spelled-out names for units AND prefixes, and
	 * all four combinations have to land on the same unit. */
	chk_str("udunits:m",          "m");
	chk_str("udunits:meter",      "m");
	chk_str("udunits:metre",      "m");
	chk_str("udunits:km",         "k~m");
	chk_str("udunits:kilometer",  "k~m");
	chk_str("udunits:millimeter", "m~m");
	chk_str("udunits:hPa",        "h~Pa");
	chk_str("udunits:hectopascal", "h~Pa");
	chk_str("udunits:degC",       "°C");
	chk_str("udunits:celsius",    "°C");

	ASSERT_TRUE(bvn_unit_equal(U("udunits:km"), U("udunits:kilometer")),
	            "the symbol and the name are one unit");
	ASSERT_TRUE(bvn_unit_equal(U("udunits:m"), U("udunits:metre")),
	            "meter and metre are one unit");

	/* A whole atom beats a prefix, the same rule every other profile applies:
	 * "min" is the minute, not milli-inch, and "at" is the technical
	 * atmosphere, not an atto-tonne. */
	chk_str("udunits:min", "min");
	chk_str("udunits:at",  "at");
	chk_str("udunits:pc",  "pc");
}

/* ── the traps: codes that look mappable and are not ────────────────────── */

/* Each of these is dimensionally identical to a native unit of the same name
 * and differs only in factor, which is the one error shape nothing downstream
 * can catch. They are refused rather than mapped. */
static void test_near_miss_refusals(void)
{
	/* UDUNITS' unqualified year is the TROPICAL year (3.15569259747e7 s),
	 * not the Julian year native yr is. 674 s apart. */
	chk_error("udunits:year", error_unit_profile_unsupported);
	chk_error("udunits:yr", error_unit_profile_unsupported);
	chk_error("udunits:month", error_unit_profile_unsupported);
	chk_str("udunits:Julian_year", "yr");

	/* UDUNITS' unqualified calorie is the IT calorie (4.1868 J); native cal
	 * is the thermochemical 4.184 J. 0.067 % apart. */
	chk_error("udunits:calorie", error_unit_profile_unsupported);
	chk_error("udunits:cal", error_unit_profile_unsupported);
	chk_error("udunits:IT_calorie", error_unit_profile_unsupported);

	/* UDUNITS builds these on the US SURVEY foot, 2 ppm longer than the
	 * international foot the native atoms of the same name use. */
	chk_error("udunits:chain", error_unit_profile_unsupported);
	chk_error("udunits:rod", error_unit_profile_unsupported);
	chk_error("udunits:furlong", error_unit_profile_unsupported);
	chk_error("udunits:fathom", error_unit_profile_unsupported);
	chk_error("udunits:acre", error_unit_profile_unsupported);

	/* The BTU goes the other way: UDUNITS' is the IT BTU, which is exactly
	 * what native Btu is, so it maps where UCUM's unqualified one cannot. */
	chk_str("udunits:Btu", "Btu");

	/* "oz" is a symbol of the US FLUID ounce in UDUNITS -- a volume. Reading
	 * it as the avoirdupois ounce was a dimension error. */
	chk_str("udunits:oz", "fl_oz");
	chk_str("udunits:avoirdupois_ounce", "oz");
	ASSERT_TRUE(!bvn_units_compatible(U("udunits:oz"),
	                                  U("udunits:avoirdupois_ounce")),
	            "the UDUNITS ounce is a volume and does not compare to a mass");

	/* "b" is the barn, not the bit -- the collision doc/11 §6.2 records. */
	chk_str("udunits:b", "barn");
}

/* ── operators ──────────────────────────────────────────────────────────── */

static void test_operators(void)
{
	printf("  operators: '.', '*', '/' and '^'...\n");

	chk_str("udunits:kg.m-2",  "k~g/m²");
	chk_str("udunits:kg*m-2",  "k~g/m²");
	chk_str("udunits:m^2",     "m²");
	chk_str("udunits:m2",      "m²");
	chk_str("udunits:m^-1",    "m⁻¹");
	chk_str("udunits:m-1",     "m⁻¹");

	/* '.' and '*' are the same operator, so the two spellings are one unit. */
	ASSERT_TRUE(bvn_unit_equal(U("udunits:kg.m-2"), U("udunits:kg*m-2")),
	            "'.' and '*' both multiply");
	ASSERT_TRUE(bvn_unit_equal(U("udunits:m^2"), U("udunits:m2")),
	            "'^2' and bare '2' are one exponent");

	/*
	 * Division inverts the term that FOLLOWS it and nothing else -- ordinary
	 * left-to-right arithmetic, and the same rule UCUM uses. "kg/m*s" is
	 * (kg/m)*s, so the second is positive. Getting this backwards is the bug
	 * that turns a viscosity into its reciprocal and still formats cleanly.
	 */
	ASSERT_TRUE(bvn_unit_equal(U("udunits:kg/m*s"), U("ucum:kg/m.s")),
	            "udunits and ucum divide identically");
	/* The coherent SI unit of mass IS the kilogram, so kg carries factor 1. */
	chk_factor("udunits:kg/m*s", 1.0);

	/* And a second '/' inverts its own term too: kg/m/s is kg·m⁻¹·s⁻¹. */
	ASSERT_TRUE(bvn_unit_equal(U("udunits:kg/m/s"), U("ucum:kg/m/s")),
	            "repeated division agrees across the two profiles");

	chk_str("udunits:km/h", "k~m/h");
	chk_error("udunits:m..s",  error_unit_illegal);
	chk_error("udunits:m/",    error_unit_illegal);
	chk_error("udunits:/",     error_unit_illegal);
}

static void test_no_syntax_leak(void)
{
	printf("  neither vocabulary's syntax leaks into the other...\n");

	/* '*' multiplies in UDUNITS and does not in UCUM, where "10*3" is a factor.
	 * '^' is an exponent in UDUNITS; UCUM spells exponents bare. */
	chk_error("ucum:kg*m",  error_unit_illegal);
	chk_error("ucum:m^2",   error_unit_illegal);

	/* UCUM's bracketed atoms and brace annotations are not UDUNITS syntax. */
	chk_error("udunits:[IU]",     error_unit_illegal);
	chk_error("udunits:m{depth}", error_unit_illegal);

	/* And a UDUNITS name is not a UCUM atom, nor the reverse. */
	chk_error("ucum:millimeter", error_unit_illegal);
	chk_error("udunits:Cel",     error_unit_illegal);
}

/* ── the two refusals that carry weight ─────────────────────────────────── */

static void test_reference_time(void)
{
	printf("  reference time is refused, and refused as UNSUPPORTED...\n");

	/*
	 * The distinction is the whole point. A producer who writes
	 * "days since 1970-01-01" has written valid UDUNITS and must be told bovnar
	 * cannot carry it -- not that they made a typo. A bovnar timestamp is
	 * <datetime:width,epoch>: the epoch lives in the type spec's base slot,
	 * which a unit-slot expression cannot reach, and the carrier is defined as
	 * a count of seconds, which "days since" is not.
	 */
	chk_error("udunits:days since 1970-01-01", error_unit_profile_unsupported);
	chk_error("udunits:seconds since 1970-01-01T00:00:00Z",
	          error_unit_profile_unsupported);
	chk_error("udunits:hours since 2000-01-01", error_unit_profile_unsupported);
	chk_error("udunits:since", error_unit_profile_unsupported);

	/*
	 * And it holds after the lexer has eaten the whitespace, which is the form
	 * that actually reaches the parser from a document. This is why the refusal
	 * is a substring test rather than an atom lookup: there are no atoms left to
	 * look up by then.
	 */
	chk_error("udunits:dayssince1970-01-01", error_unit_profile_unsupported);
}

static void test_space_is_not_an_operator(void)
{
	printf("  space does not multiply...\n");

	/*
	 * UDUNITS multiplies with a space and CF's commonest spelling is
	 * "kg m-2 s-1", but a space cannot survive into a unit slot: the lexer
	 * deletes whitespace inside a type annotation, so "m s-1" arrives as
	 * "ms-1". That string is itself valid UDUNITS -- RECIPROCAL MILLISECONDS --
	 * so the parser is right to accept it and right to read it that way.
	 *
	 * This test pins the reading, so that anyone tempted to add ' ' to the
	 * profile's multiplication set sees what it would actually mean.
	 */
	chk_str("udunits:ms-1", "m~s⁻¹");
	ASSERT_TRUE(!bvn_unit_equal(U("udunits:ms-1"), U("udunits:m/s")),
	            "ms-1 is reciprocal milliseconds, NOT metres per second");
	ASSERT_TRUE(bvn_units_compatible(U("udunits:ms-1"), U("Hz")),
	            "reciprocal milliseconds is a frequency");

	/* Written with a real operator, it means what CF meant. */
	chk_str("udunits:m*s-1", "m/s");
	ASSERT_TRUE(bvn_unit_equal(U("udunits:m*s-1"), U("m/s")),
	            "m*s-1 is metres per second");
}

/* ── agreement with the other vocabularies ──────────────────────────────── */

static void test_cross_vocabulary(void)
{
	printf("  agreement with the other vocabularies...\n");

	ASSERT_TRUE(bvn_unit_equal(U("udunits:km"), U("ucum:km")),
	            "udunits:km == ucum:km");
	ASSERT_TRUE(bvn_unit_equal(U("udunits:kg"), U("unece:KGM")),
	            "udunits:kg == unece:KGM");
	ASSERT_TRUE(bvn_unit_equal(U("udunits:kg"), U("qudt:KiloGM")),
	            "udunits:kg == qudt:KiloGM");
	ASSERT_TRUE(bvn_unit_equal(U("udunits:kg"), U("qudt-qk:Mass")),
	            "udunits:kg == the coherent unit of Mass");
	ASSERT_TRUE(bvn_unit_equal(U("udunits:degC"), U("ucum:Cel")),
	            "udunits:degC == ucum:Cel");
	ASSERT_TRUE(bvn_unit_equal(U("udunits:m*s-1"), U("qudt:M-PER-SEC")),
	            "udunits:m*s-1 == qudt:M-PER-SEC");

	chk_factor("udunits:kg",   1.0);
	chk_factor("udunits:g",    1e-3);
	chk_factor("udunits:km",   1e3);
	chk_factor("udunits:hPa",  100.0);
	chk_factor("udunits:bar",  100000.0);
}

/* ── writing back out ───────────────────────────────────────────────────── */

static void test_to_udunits(void)
{
	printf("  emitting a UDUNITS code from a native unit...\n");

	char buf[BVNR_UNIT_STRING_MAX];

	/* An expr profile emits a prefix, so one row per base reaches every decade. */
	ASSERT_TRUE(bvn_unit_to_profile("udunits", U("k~m"), buf, sizeof(buf)) > 0 &&
	            strcmp(buf, "km") == 0,
	            "k~m should emit as km");
	ASSERT_TRUE(bvn_unit_to_profile("udunits", U("m/s"), buf, sizeof(buf)) > 0,
	            "an expr profile can spell a compound");

	/* Unlike a flat profile: UNECE cannot spell m/s at all. */
	ASSERT_TRUE(bvn_unit_to_profile("unece", U("m/s"), buf, sizeof(buf)) < 0,
	            "a flat profile cannot spell a compound");

	/* UDUNITS owns no opaque range, so it cannot spell another profile's. */
	ASSERT_TRUE(bvn_unit_to_profile("udunits", U("ucum:[IU]"), buf,
	                                sizeof(buf)) < 0,
	            "udunits must not spell a UCUM arbitrary atom");
	ASSERT_TRUE(BVN_PROFILE_UDUNITS_OPAQUE_FIRST >
	            BVN_PROFILE_UDUNITS_OPAQUE_LAST,
	            "the udunits profile owns no opaque range");
}

/* ── round trip ─────────────────────────────────────────────────────────── */

static void test_round_trip(void)
{
	printf("  round trip...\n");

	static const char* codes[] = {
		"udunits:m", "udunits:km", "udunits:cm", "udunits:mm", "udunits:um",
		"udunits:s", "udunits:min", "udunits:hour", "udunits:day",
		"udunits:kg", "udunits:g", "udunits:mg", "udunits:tonne",
		"udunits:K", "udunits:degC", "udunits:degF",
		"udunits:Pa", "udunits:hPa", "udunits:kPa", "udunits:bar",
		"udunits:J", "udunits:kJ", "udunits:W", "udunits:kW",
		"udunits:V", "udunits:A", "udunits:ohm", "udunits:F", "udunits:H",
		"udunits:Hz", "udunits:kHz", "udunits:N", "udunits:mol",
		"udunits:cd", "udunits:lm", "udunits:lux", "udunits:Bq",
		"udunits:Gy", "udunits:Sv", "udunits:rad", "udunits:sr",
		"udunits:L", "udunits:mL", "udunits:degree", "udunits:percent",
		"udunits:m2", "udunits:m^3", "udunits:m*s-1", "udunits:kg*m-2*s-1",
		"udunits:km/h", "udunits:inch", "udunits:foot", "udunits:mile",
		"udunits:pound", "udunits:knot", "udunits:psi", "udunits:Btu",
		"udunits:horsepower", "udunits:troy_ounce", "udunits:fluid_ounce",
		"udunits:maxwell", "udunits:roentgen", "udunits:tex",
		"udunits:Julian_year", "udunits:fortnight", "udunits:light_year",
	};
	for (size_t i = 0; i < sizeof(codes) / sizeof(codes[0]); i++) {
		bool ok = true;
		value_unit_t u = bvn_parse_unit((const uint8_t*)codes[i], &ok);
		tests++;
		if (!ok) {
			fprintf(stderr, "FAIL: %s did not parse\n", codes[i]);
			failures++;
			continue;
		}
		char buf[BVNR_UNIT_STRING_MAX];
		int32_t n = bvn_unit_to_string(u, buf, sizeof(buf));
		if (n < 0) {
			fprintf(stderr, "FAIL: %s has no spelling at all\n", codes[i]);
			failures++;
			continue;
		}
		bool ok2 = true;
		value_unit_t v = bvn_parse_unit((const uint8_t*)buf, &ok2);
		if (!ok2 || !bvn_unit_equal(u, v)) {
			fprintf(stderr, "FAIL: %s -> %s did not re-parse to itself\n",
			        codes[i], buf);
			failures++;
		}
	}
}

/* ── other refusals ─────────────────────────────────────────────────────── */

static void test_refusals(void)
{
	printf("  the rest of the refusals...\n");

	/* CF's coordinate pseudo-units: not units at all. */
	chk_error("udunits:degrees_north", error_unit_profile_unsupported);
	chk_error("udunits:degrees_east",  error_unit_profile_unsupported);

	/* CF's dimensionless "1" resolves to UNITY -- a value with no unit -- in the
	 * shared expr parser, before any table is consulted, exactly as "ucum:1"
	 * does. It is not an error and must not become one. */
	ASSERT_TRUE(bvn_unit_valid(U("udunits:1")) &&
	            U("udunits:1").num_components == 0,
	            "udunits:1 is unity, not an error");
	chk_error("udunits:count", error_unit_profile_unsupported);

	/* Logarithms with a reference level, which dB cannot carry. */
	chk_error("udunits:dBm", error_unit_profile_unsupported);

	/* Years that are not the Julian year. */
	chk_error("udunits:sidereal_year", error_unit_profile_unsupported);

	/* Not UDUNITS at all. */
	chk_error("udunits:metres_per_second", error_unit_illegal);
	chk_error("udunits:", error_unit_illegal);
}

int main(void)
{
	printf("UDUNITS-2 unit profile\n");

	test_spellings();
	test_near_miss_refusals();
	test_operators();
	test_no_syntax_leak();
	test_reference_time();
	test_space_is_not_an_operator();
	test_cross_vocabulary();
	test_to_udunits();
	test_round_trip();
	test_refusals();

	printf("\n%d tests, %d failures\n", tests, failures);
	if (failures == 0)
		printf("ALL TESTS PASSED\n");
	return failures == 0 ? 0 : 1;
}
