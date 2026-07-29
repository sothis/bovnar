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
 * The CF standard-name profile (spec 1.2). Specified in
 * doc/11_bovnar_unit_profiles.md.
 *
 * A standard name is not a unit -- it names what is being measured -- so this
 * namespace has the shape qudt-qk has, with one difference that this file is
 * mostly about: it is READ-ONLY. bvn_unit_to_profile("cf", u) returns -1 for
 * every unit, because sixty-nine standard names state the kelvin and picking
 * one to write a kelvin back as would assert a quantity the unit does not know.
 *
 * The other thing pinned here is that a row's unit comes from CF's own
 * canonical_units field and not from a reading of the name: cf:rainfall_flux is
 * kg m-2 s-1 because the table says so.
 */

#include <math.h>
#include <stdio.h>
#include <string.h>
#include "bovnar.h"
#include "bovnar_si_units.h"

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

/* ── the canonical units CF states ──────────────────────────────────────── */

static void test_canonical_units(void)
{
	printf("  cf: a standard name carries its canonical_units...\n");

	chk_str("cf:air_temperature",             "K");
	chk_str("cf:sea_water_temperature",       "K");
	chk_str("cf:surface_air_pressure",        "Pa");
	chk_str("cf:northward_wind",              "m/s");
	chk_str("cf:eastward_wind",               "m/s");
	chk_str("cf:geopotential_height",         "m");
	chk_str("cf:latitude",                    "°");
	chk_str("cf:toa_incoming_shortwave_flux", "W/m²");

	/* The compound one, written the way CF writes it: kg m-2 s-1. */
	chk_str("cf:rainfall_flux", "k~g/m²·s");
	chk_factor("cf:rainfall_flux", 1.0);
	chk_factor("cf:air_temperature", 1.0);
	chk_factor("cf:latitude", 0.017453292519943295);

	/*
	 * Two names that state the same canonical_units translate to the SAME unit
	 * -- there is no per-name unit here, only per-canonical_units -- which is
	 * what keeps 4450 rows from drifting apart.
	 */
	ASSERT_TRUE(bvn_unit_equal(U("cf:air_temperature"),
	                           U("cf:sea_water_temperature")),
	            "two names with the same canonical units are the same unit");
}

/* ── it is the same unit the other vocabularies mean ────────────────────── */

static void test_cross_vocabulary(void)
{
	printf("  cf: agrees with the vocabularies already here...\n");

	ASSERT_TRUE(bvn_unit_equal(U("cf:air_temperature"), U("K")),
	            "cf:air_temperature is the kelvin");
	ASSERT_TRUE(bvn_unit_equal(U("cf:air_temperature"), U("udunits:K")),
	            "cf:air_temperature == udunits:K");
	ASSERT_TRUE(bvn_unit_equal(U("cf:air_temperature"), U("qudt-qk:ThermodynamicTemperature")),
	            "cf:air_temperature == the coherent unit of that kind");
	ASSERT_TRUE(bvn_unit_equal(U("cf:northward_wind"), U("udunits:m/s")),
	            "cf:northward_wind == udunits:m/s");
	ASSERT_TRUE(bvn_unit_equal(U("cf:surface_air_pressure"), U("ucum:Pa")),
	            "cf:surface_air_pressure == ucum:Pa");

	/* And it converts like any other unit once translated. */
	{
		bool ok = true, aff = false;
		double f = bvn_unit_convert_factor(U("cf:geopotential_height"),
		                                   U("k~m"), &ok, &aff);
		tests++;
		if (!ok || fabs(f - 1e-3) > 1e-18) {
			fprintf(stderr, "FAIL: cf:geopotential_height -> km factor %.17g\n", f);
			failures++;
		}
	}
}

/* ── read-only ──────────────────────────────────────────────────────────── */

static void test_read_only(void)
{
	printf("  cf: nothing is ever written back as a standard name...\n");

	static const char* natives[] = { "K", "m", "Pa", "m/s", "W/m^2", "k~g" };
	for (size_t i = 0; i < sizeof(natives) / sizeof(natives[0]); i++) {
		char buf[BVNR_UNIT_STRING_MAX];
		tests++;
		if (bvn_unit_to_profile("cf", U(natives[i]), buf, sizeof(buf)) >= 0) {
			fprintf(stderr, "FAIL: %s was written back as cf:%s — a standard "
			                "name asserts a quantity a unit does not know\n",
			        natives[i], buf);
			failures++;
		}
	}

	/* The refusal is this namespace's, not a general one: the same units DO
	 * have codes in the vocabularies that spell units. */
	{
		char buf[BVNR_UNIT_STRING_MAX];
		ASSERT_TRUE(bvn_unit_to_profile("qudt", U("K"), buf, sizeof(buf)) > 0,
		            "the kelvin still has a QUDT code");
		ASSERT_TRUE(bvn_unit_to_profile("om", U("K"), buf, sizeof(buf)) > 0,
		            "the kelvin still has an OM code");
	}

	/* A cf: unit is not "profile only": it translates to an ordinary native
	 * unit, so it has a native spelling and writes as one. */
	ASSERT_TRUE(!bvn_unit_is_profile_only(U("cf:air_temperature")),
	            "cf:air_temperature is an ordinary kelvin once translated");
}

/* ── refusals, by cause ─────────────────────────────────────────────────── */

static void test_refusals(void)
{
	printf("  cf: what is refused, and why...\n");

	/* Dimension one: CF states "1", bovnar spells that by leaving the unit
	 * slot out, and translating it to any dimensionless native unit would put a
	 * cloud fraction and a percentage in one equivalence class. */
	chk_error("cf:cloud_area_fraction",   error_unit_profile_unsupported);
	chk_error("cf:relative_humidity",     error_unit_profile_unsupported);

	/* A bare scale factor. CF gives the salinities "1e-3", a magnitude with no
	 * unit at all. */
	chk_error("cf:sea_water_salinity",    error_unit_profile_unsupported);

	/* String-valued standard names state no units because they measure
	 * nothing. */
	chk_error("cf:region",                error_unit_profile_unsupported);
	chk_error("cf:area_type",             error_unit_profile_unsupported);

	/* CF's year is UDUNITS', the TROPICAL year of 31556925.9747 s; bovnar's yr
	 * is the Julian 31557600 s, and doc/11 §13.3 already refuses the tropical
	 * one rather than rounding it into the Julian. */
	chk_error("cf:age_of_sea_ice", error_unit_profile_unsupported);
	chk_error("cf:tendency_of_global_average_sea_level_change",
	          error_unit_profile_unsupported);

	/* A DEPRECATED ALIAS is not a spelling this profile blesses: CF keeps it in
	 * the table pointing at its replacement, and a document written today
	 * should carry the replacement. */
	chk_error("cf:chlorophyll_concentration_in_sea_water", error_unit_illegal);
	chk_str("cf:mass_concentration_of_chlorophyll_in_sea_water", "k~g/m³");

	/* Not a standard name at all. */
	chk_error("cf:no_such_standard_name", error_unit_illegal);
	chk_error("cf:", error_unit_illegal);
}

/* ── flatness ───────────────────────────────────────────────────────────── */

static void test_flatness(void)
{
	printf("  cf: a standard name is one whole token...\n");

	/* No operators, no exponents, no prefixes. A standard name that looks like
	 * an expression is still one token, and one this table does not have. */
	chk_error("cf:air_temperature/s",   error_unit_illegal);
	chk_error("cf:air_temperature2",    error_unit_illegal);
	chk_error("cf:kilo_air_temperature", error_unit_illegal);

	/* CF's names are lowercase with underscores, and case is significant. */
	chk_error("cf:Air_temperature",     error_unit_illegal);
	chk_error("cf:AIR_TEMPERATURE",     error_unit_illegal);

	/* The longest names in the table are 166 bytes and must still fit. */
	chk_str("cf:tendency_of_atmosphere_mass_content_of_sulfur_dioxide_due_to_"
	        "emission_from_residential_and_commercial_combustion",
	        "k~g/m²·s");
}

int main(void)
{
	printf("CF standard-name profile\n");

	test_canonical_units();
	test_cross_vocabulary();
	test_read_only();
	test_refusals();
	test_flatness();

	printf("\n%d tests, %d failures\n", tests, failures);
	if (failures == 0)
		printf("ALL TESTS PASSED\n");
	return failures == 0 ? 0 : 1;
}
