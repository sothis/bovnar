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
 * ===========================================================================
 * The cross-vocabulary conformance suite
 * ===========================================================================
 *
 * Every other profile test asks "does this vocabulary translate correctly?".
 * This one asks the question that actually matters once there are five of them:
 * DO THEY AGREE?
 *
 * The claim under test is that a unit profile is a SPELLING and not a second
 * unit model -- so when UCUM says "kg", UNECE says "KGM", QUDT says "KiloGM",
 * UDUNITS says "kg" and QUDT's quantity kind says "Mass", all five must land on
 * one identical value_unit_t. If any two disagree, the format's central promise
 * fails: a document written by a UCUM-speaking producer and read by a
 * UNECE-speaking consumer would carry a unit that compares unequal to itself.
 *
 * WHY THIS IS ORGANISED AS A CONCEPT TABLE
 *   Each row below is one PHYSICAL CONCEPT and the way each vocabulary spells
 *   it. The suite then checks every spelling against every other spelling in
 *   its row -- pairwise, not against a designated "reference" spelling. A
 *   reference-based check would pass if two non-reference spellings agreed with
 *   the reference for different reasons; the pairwise form cannot.
 *
 * WHAT EACH ROW IS CHECKED FOR
 *   1. every spelling parses at all;
 *   2. pairwise bvn_unit_equal -- the same components, prefixes and exponents;
 *   3. pairwise agreement of the coherent-SI factor, which catches a prefix
 *      folded in the wrong direction that equality alone would not (a mapping
 *      that is off by a decade produces a DIFFERENT unit, so equality catches
 *      it -- but a mapping that is off by a decade in BOTH directions at once
 *      would not, and the factor pins the absolute scale);
 *   4. pairwise dimensional compatibility;
 *   5. round-trip: the canonical spelling re-parses to the same unit.
 *
 * THE NEGATIVE HALF IS NOT OPTIONAL
 *   A suite that only checks agreement is satisfied by a translator that maps
 *   everything onto the metre. test_must_differ() pins the pairs that look
 *   interchangeable and are not -- across vocabularies, where the temptation to
 *   "helpfully" unify them is strongest.
 *
 * WHAT THIS SUITE CANNOT TELL YOU
 *   It proves the five vocabularies agree WITH EACH OTHER. It cannot prove they
 *   agree with their publishers: nothing in this repository checks that "KGM" is
 *   UNECE's kilogram rather than something the table asserts. Five tables that
 *   are wrong in the same way agree perfectly. See the provenance notes at the
 *   top of each src/gendata/*.bvnr.
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

/* At most this many spellings per concept: native, ucum, unece, qudt, udunits,
 * qudt-qk, and room for a second spelling in a vocabulary that has one. */
#define MAX_SPELLINGS 8

typedef struct {
	const char* concept;
	const char* spelling[MAX_SPELLINGS];   /* NULL-terminated */
} cv_concept_t;

/*
 * The concept table.
 *
 * A NULL slot ends the row; a vocabulary that has no spelling for a concept is
 * simply left out rather than given a placeholder, because "UNECE has no code
 * for the katal" is a fact about UNECE and not a hole in this table.
 *
 * qudt-qk entries appear only where the concept IS the coherent unit of that
 * kind. "qudt-qk:Mass" belongs on the kilogram row and would be wrong on the
 * gram row -- which is exactly the sort of error this suite exists to catch.
 */
static const cv_concept_t g_concepts[] = {
	/* ── SI base units ───────────────────────────────────────────────────── */
	{ "metre", { "m", "ucum:m", "unece:MTR", "qudt:M", "udunits:m",
	             "udunits:meter", "qudt-qk:Length", NULL } },
	{ "kilogram", { "k~g", "kg", "ucum:kg", "unece:KGM", "qudt:KiloGM",
	                "udunits:kg", "qudt-qk:Mass", NULL } },
	{ "second", { "s", "ucum:s", "unece:SEC", "qudt:SEC", "udunits:s",
	              "udunits:second", "qudt-qk:Time", NULL } },
	{ "ampere", { "A", "ucum:A", "unece:AMP", "qudt:A", "udunits:ampere",
	              "qudt-qk:ElectricCurrent", NULL } },
	{ "kelvin", { "K", "ucum:K", "unece:KEL", "qudt:K", "udunits:kelvin",
	              "qudt-qk:ThermodynamicTemperature", NULL } },
	{ "mole", { "mol", "ucum:mol", "unece:MOL", "qudt:MOL", "udunits:mole",
	            "qudt-qk:AmountOfSubstance", NULL } },
	{ "candela", { "cd", "ucum:cd", "unece:CDL", "qudt:CD", "udunits:candela",
	               "qudt-qk:LuminousIntensity", NULL } },

	/* ── named SI-derived ────────────────────────────────────────────────── */
	{ "hertz", { "Hz", "ucum:Hz", "unece:HTZ", "qudt:HZ", "udunits:hertz",
	             "qudt-qk:Frequency", NULL } },
	{ "newton", { "N", "ucum:N", "unece:NEW", "qudt:N", "udunits:newton",
	              "qudt-qk:Force", NULL } },
	{ "pascal", { "Pa", "ucum:Pa", "unece:PAL", "qudt:PA", "udunits:pascal",
	              "qudt-qk:Pressure", NULL } },
	{ "joule", { "J", "ucum:J", "unece:JOU", "qudt:J", "udunits:joule",
	             "qudt-qk:Energy", NULL } },
	{ "watt", { "W", "ucum:W", "unece:WTT", "qudt:W", "udunits:watt",
	            "qudt-qk:Power", NULL } },
	{ "volt", { "V", "ucum:V", "unece:VLT", "qudt:V", "udunits:volt",
	            "qudt-qk:Voltage", NULL } },
	{ "ohm", { "Ω", "ucum:Ohm", "unece:OHM", "qudt:OHM", "udunits:ohm",
	           "qudt-qk:Resistance", NULL } },
	{ "farad", { "F", "ucum:F", "unece:FAR", "qudt:FARAD", "udunits:farad",
	             "qudt-qk:Capacitance", NULL } },
	{ "coulomb", { "C", "ucum:C", "unece:COU", "qudt:C", "udunits:coulomb",
	               "qudt-qk:ElectricCharge", NULL } },
	{ "siemens", { "S", "ucum:S", "unece:SIE", "qudt:S", "udunits:siemens",
	               "qudt-qk:Conductance", NULL } },
	{ "henry", { "H", "ucum:H", "unece:HEN", "qudt:H", "udunits:henry",
	             "qudt-qk:Inductance", NULL } },
	{ "weber", { "Wb", "ucum:Wb", "unece:WEB", "qudt:WB", "udunits:weber",
	             "qudt-qk:MagneticFlux", NULL } },
	{ "tesla", { "T", "ucum:T", "qudt:T", "udunits:tesla",
	             "qudt-qk:MagneticFluxDensity", NULL } },
	{ "lumen", { "lm", "ucum:lm", "unece:LUM", "qudt:LM", "udunits:lumen",
	             "qudt-qk:LuminousFlux", NULL } },
	{ "lux", { "lx", "ucum:lx", "unece:LUX", "qudt:LUX", "udunits:lux",
	           "qudt-qk:Illuminance", NULL } },
	{ "becquerel", { "Bq", "ucum:Bq", "unece:BQL", "qudt:BQ",
	                 "udunits:becquerel", "qudt-qk:Activity", NULL } },
	{ "gray", { "Gy", "ucum:Gy", "unece:GRY", "qudt:GRAY", "udunits:gray",
	            "qudt-qk:AbsorbedDose", NULL } },
	{ "sievert", { "Sv", "ucum:Sv", "qudt:SV", "udunits:sievert",
	               "qudt-qk:DoseEquivalent", NULL } },
	{ "katal", { "kat", "ucum:kat", "qudt:KAT", "udunits:katal",
	             "qudt-qk:CatalyticActivity", NULL } },
	{ "radian", { "rad", "ucum:rad", "qudt:RAD", "udunits:radian",
	              "qudt-qk:PlaneAngle", NULL } },
	{ "steradian", { "sr", "ucum:sr", "qudt:SR", "udunits:steradian",
	                 "qudt-qk:SolidAngle", NULL } },

	/* ── compounds ───────────────────────────────────────────────────────── */
	{ "metre per second", { "m/s", "ucum:m/s", "unece:MTS", "qudt:M-PER-SEC",
	                        "udunits:m*s-1", "udunits:m/s",
	                        "qudt-qk:Velocity", NULL } },
	{ "metre per second squared", { "m/s^2", "ucum:m/s2", "qudt:M-PER-SEC2",
	                                "udunits:m*s-2",
	                                "qudt-qk:Acceleration", NULL } },
	{ "square metre", { "m^2", "ucum:m2", "unece:MTK", "qudt:M2",
	                    "udunits:m2", "udunits:m^2", "qudt-qk:Area", NULL } },
	{ "cubic metre", { "m^3", "ucum:m3", "unece:MTQ", "qudt:M3",
	                   "udunits:m3", "qudt-qk:Volume", NULL } },
	{ "kilometre per hour", { "k~m/h", "unece:KMH", "qudt:KiloM-PER-HR",
	                          "udunits:km/h", "udunits:km*h-1", NULL } },
	{ "newton metre", { "N·m", "ucum:N.m", "qudt:N-M", "udunits:N*m",
	                    "qudt-qk:Torque", NULL } },
	{ "kilogram per cubic metre", { "k~g/m^3", "ucum:kg/m3",
	                                "qudt:KiloGM-PER-M3", "udunits:kg*m-3",
	                                "qudt-qk:Density", NULL } },

	/* ── prefixed and non-SI ─────────────────────────────────────────────── */
	{ "gram", { "g", "ucum:g", "unece:GRM", "qudt:GM", "udunits:g",
	            "udunits:gram", NULL } },
	{ "milligram", { "m~g", "mg", "ucum:mg", "unece:MGM", "qudt:MilliGM",
	                 "udunits:mg", "udunits:milligram", NULL } },
	{ "kilometre", { "k~m", "km", "ucum:km", "unece:KMT", "qudt:KiloM",
	                 "udunits:km", "udunits:kilometer", NULL } },
	{ "centimetre", { "c~m", "cm", "ucum:cm", "unece:CMT", "qudt:CentiM",
	                  "udunits:cm", NULL } },
	{ "litre", { "L", "ucum:L", "ucum:l", "unece:LTR", "qudt:L",
	             "udunits:L", "udunits:litre", NULL } },
	{ "millilitre", { "m~L", "mL", "ucum:mL", "unece:MLT", "qudt:MilliL",
	                  "udunits:mL", NULL } },
	{ "tonne", { "t", "ucum:t", "unece:TNE", "qudt:TON_Metric",
	             "udunits:tonne", NULL } },
	/* UCUM's "ar" is the ARE, a hundredth of this; the hectare is "har". */
	{ "hectare", { "ha", "ucum:har", "unece:HAR", "qudt:HA",
	               "udunits:hectare", NULL } },
	{ "bar", { "bar", "ucum:bar", "unece:BAR", "qudt:BAR", "udunits:bar",
	           NULL } },
	{ "hectopascal", { "h~Pa", "hPa", "ucum:hPa", "qudt:HectoPA",
	                   "udunits:hPa", NULL } },
	{ "kilowatt hour", { "k~W·h", "unece:KWH", "qudt:KiloW-HR",
	                     "udunits:kW*h", NULL } },
	{ "degree Celsius", { "°C", "ucum:Cel", "unece:CEL", "qudt:DEG_C",
	                      "udunits:degC", "udunits:celsius", NULL } },
	{ "degree of angle", { "°", "ucum:deg", "unece:DD", "qudt:DEG",
	                       "udunits:degree", NULL } },
	{ "percent", { "%", "ucum:%", "unece:P1", "qudt:PERCENT",
	               "udunits:percent", NULL } },
	{ "inch", { "in", "ucum:[in_i]", "unece:INH", "qudt:IN",
	            "udunits:inch", NULL } },
	{ "foot", { "ft", "ucum:[ft_i]", "unece:FOT", "qudt:FT",
	            "udunits:foot", NULL } },
	{ "pound", { "lb", "ucum:[lb_av]", "unece:LBR", "qudt:LB",
	             "udunits:pound", NULL } },
	{ "knot", { "kn", "unece:KNT", "qudt:KN", "udunits:knot", NULL } },
};

#define N_CONCEPTS (sizeof(g_concepts) / sizeof(g_concepts[0]))

/* ── the pairwise engine ────────────────────────────────────────────────── */

static bool si_factor(value_unit_t u, double* out)
{
	bool aff = false, ok = true;
	double off = 0.0;
	double f = bvn_unit_to_si_factor(u, &aff, &off, &ok);
	if (!ok)
		return false;
	*out = f;
	return true;
}

static void test_agreement(void)
{
	printf("  every vocabulary agrees, pairwise, on every concept...\n");

	for (size_t c = 0; c < N_CONCEPTS; c++) {
		const cv_concept_t* con = &g_concepts[c];
		value_unit_t u[MAX_SPELLINGS];
		bool         have[MAX_SPELLINGS];
		size_t       n = 0;

		/* 1. every spelling parses. */
		for (; n < MAX_SPELLINGS && con->spelling[n]; n++) {
			bool ok = true;
			u[n] = bvn_parse_unit((const uint8_t*)con->spelling[n], &ok);
			have[n] = ok;
			tests++;
			if (!ok) {
				fprintf(stderr, "FAIL [%s]: %s did not parse\n",
				        con->concept, con->spelling[n]);
				failures++;
			}
		}
		if (n < 2) {
			fprintf(stderr, "FAIL [%s]: a concept needs at least two "
			                "spellings to be worth checking\n", con->concept);
			tests++;
			failures++;
			continue;
		}

		/* 2-4. pairwise equality, factor and compatibility. */
		for (size_t i = 0; i < n; i++) {
			if (!have[i])
				continue;
			for (size_t j = i + 1; j < n; j++) {
				if (!have[j])
					continue;

				tests++;
				if (!bvn_unit_equal(u[i], u[j])) {
					char a[BVNR_UNIT_STRING_MAX], b[BVNR_UNIT_STRING_MAX];
					if (bvn_unit_to_string(u[i], a, sizeof(a)) < 0)
						strcpy(a, "(unwritable)");
					if (bvn_unit_to_string(u[j], b, sizeof(b)) < 0)
						strcpy(b, "(unwritable)");
					fprintf(stderr,
					        "FAIL [%s]: %s -> %s but %s -> %s\n",
					        con->concept, con->spelling[i], a,
					        con->spelling[j], b);
					failures++;
					continue;   /* the factor check would just repeat this */
				}

				double fa = 0.0, fb = 0.0;
				bool ha = si_factor(u[i], &fa), hb = si_factor(u[j], &fb);
				tests++;
				if (ha != hb) {
					fprintf(stderr,
					        "FAIL [%s]: %s and %s disagree on whether they "
					        "have an SI factor at all\n",
					        con->concept, con->spelling[i], con->spelling[j]);
					failures++;
				} else if (ha && fabs(fa - fb) > fabs(fa) * 1e-12) {
					fprintf(stderr,
					        "FAIL [%s]: %s factor %.17g, %s factor %.17g\n",
					        con->concept, con->spelling[i], fa,
					        con->spelling[j], fb);
					failures++;
				}

				tests++;
				/* Compatibility is a statement about DIMENSION. Two spellings
				 * that compare equal must also be mutually convertible, unless
				 * neither has a dimension at all -- which no concept in this
				 * table does, since an opaque unit has only one spelling by
				 * construction. */
				if (!bvn_units_compatible(u[i], u[j])) {
					fprintf(stderr,
					        "FAIL [%s]: %s and %s compare equal but are not "
					        "compatible\n",
					        con->concept, con->spelling[i], con->spelling[j]);
					failures++;
				}
			}
		}

		/* 5. round trip, once per spelling. */
		for (size_t i = 0; i < n; i++) {
			if (!have[i])
				continue;
			char buf[BVNR_UNIT_STRING_MAX];
			tests++;
			if (bvn_unit_to_string(u[i], buf, sizeof(buf)) < 0) {
				fprintf(stderr, "FAIL [%s]: %s has no spelling at all\n",
				        con->concept, con->spelling[i]);
				failures++;
				continue;
			}
			bool ok = true;
			value_unit_t back = bvn_parse_unit((const uint8_t*)buf, &ok);
			if (!ok || !bvn_unit_equal(u[i], back)) {
				fprintf(stderr,
				        "FAIL [%s]: %s -> %s did not re-parse to itself\n",
				        con->concept, con->spelling[i], buf);
				failures++;
			}
		}
	}
}

/* ── the negative half ──────────────────────────────────────────────────── */

typedef struct {
	const char* a;
	const char* b;
	const char* why;
} cv_differ_t;

/*
 * Pairs that look interchangeable across vocabularies and are not. A suite that
 * only checked agreement would be satisfied by a translator that mapped
 * everything onto the metre; these are what stop that.
 */
static const cv_differ_t g_differ[] = {
	{ "ucum:kg", "qudt:GM",
	  "the kilogram is not the gram — the one SI base unit with a prefix in "
	  "its name is where a 'tidied' table goes wrong by 10^3" },
	{ "qudt-qk:Mass", "qudt:GM",
	  "the COHERENT unit of mass is the kilogram, not the gram" },
	{ "unece:KGM", "unece:GRM",
	  "a flat vocabulary spells each decade separately and must keep them "
	  "apart" },
	{ "ucum:st", "unece:STI",
	  "UCUM's 'st' is the STERE, a cubic metre; UNECE's STI is the stone. The "
	  "sharpest collision in the two namespaces" },
	{ "qudt:MI", "qudt:MilliM",
	  "QUDT's MI is the mile — a flat local name never decomposes into a "
	  "prefix" },
	{ "udunits:ms-1", "udunits:m*s-1",
	  "reciprocal milliseconds is not metres per second; this is what a space "
	  "in a unit slot silently becomes" },
	{ "ucum:B", "qudt:BYTE",
	  "UCUM's 'B' is the BEL; UCUM writes the byte 'By'" },
	{ "qudt:KiloBYTE", "qudt:KibiBYTE",
	  "10^3 bytes is not 2^10 bytes, and QUDT keeps the families apart" },
	{ "unece:CEL", "unece:KEL",
	  "an affine scale is not its thermodynamic counterpart" },
	{ "qudt-qk:Length", "qudt-qk:Area",
	  "two kinds with different dimensions" },
	{ "unece:MTS", "unece:MTK",
	  "metre per second against square metre — two Rec 20 codes one letter "
	  "apart" },
	{ "ucum:mo", "ucum:a",
	  "the month is not the year" },
};

#define N_DIFFER (sizeof(g_differ) / sizeof(g_differ[0]))

static void test_must_differ(void)
{
	printf("  pairs that look interchangeable and are not...\n");

	for (size_t i = 0; i < N_DIFFER; i++) {
		bool oa = true, ob = true;
		value_unit_t a = bvn_parse_unit((const uint8_t*)g_differ[i].a, &oa);
		value_unit_t b = bvn_parse_unit((const uint8_t*)g_differ[i].b, &ob);
		tests++;
		if (!oa || !ob) {
			fprintf(stderr, "FAIL: %s or %s did not parse\n",
			        g_differ[i].a, g_differ[i].b);
			failures++;
			continue;
		}
		if (bvn_unit_equal(a, b)) {
			fprintf(stderr, "FAIL: %s and %s compare EQUAL — %s\n",
			        g_differ[i].a, g_differ[i].b, g_differ[i].why);
			failures++;
		}
	}
}

/* ── opaque units belong to exactly one vocabulary ──────────────────────── */

static void test_opaque_isolation(void)
{
	printf("  an opaque unit is incommensurable with everything...\n");

	static const char* opaque[] = {
		"ucum:[IU]", "ucum:[PFU]", "unece:XBX", "unece:XPX", "unece:C62",
	};
	const size_t n = sizeof(opaque) / sizeof(opaque[0]);

	for (size_t i = 0; i < n; i++) {
		bool ok = true;
		value_unit_t u = bvn_parse_unit((const uint8_t*)opaque[i], &ok);
		ASSERT_TRUE(ok && bvn_unit_is_profile_only(u),
		            "an opaque unit parses and has no native spelling");

		/* Against every other opaque unit, and against a plain SI unit. */
		for (size_t j = i + 1; j < n; j++) {
			bool ok2 = true;
			value_unit_t v = bvn_parse_unit((const uint8_t*)opaque[j], &ok2);
			tests++;
			if (!ok2 || bvn_unit_equal(u, v) || bvn_units_compatible(u, v)) {
				fprintf(stderr, "FAIL: %s and %s are not distinct\n",
				        opaque[i], opaque[j]);
				failures++;
			}
		}
		bool okm = true;
		value_unit_t m = bvn_parse_unit((const uint8_t*)"m", &okm);
		tests++;
		if (bvn_units_compatible(u, m)) {
			fprintf(stderr, "FAIL: %s is compatible with the metre\n",
			        opaque[i]);
			failures++;
		}
	}
}

/* ── the enforcement point, end to end ──────────────────────────────────── */

static void test_enforcement(void)
{
	printf("  a cross-vocabulary mismatch is a parse error...\n");

	/*
	 * The whole argument for the format, exercised across vocabularies: an
	 * annotation in one and an inline unit in another are checked against each
	 * other by the parser, with no checker to run and no library to remember to
	 * call. bvn_unit_equal is what the validator uses for that comparison, so
	 * this is the same decision the document path makes.
	 */
	bool o1 = true, o2 = true;
	value_unit_t ann = bvn_parse_unit((const uint8_t*)"ucum:kg", &o1);
	value_unit_t inl = bvn_parse_unit((const uint8_t*)"unece:KGM", &o2);
	ASSERT_TRUE(o1 && o2 && bvn_unit_equal(ann, inl),
	            "ucum:kg and unece:KGM agree, so the document parses");

	value_unit_t bad = bvn_parse_unit((const uint8_t*)"unece:GRM", &o2);
	ASSERT_TRUE(o2 && !bvn_unit_equal(ann, bad),
	            "ucum:kg against unece:GRM is a mismatch, so it does not");
}

int main(void)
{
	printf("cross-vocabulary conformance (%zu concepts, %zu vocabularies)\n",
	       N_CONCEPTS, (size_t)5);

	test_agreement();
	test_must_differ();
	test_opaque_isolation();
	test_enforcement();

	printf("\n%d tests, %d failures\n", tests, failures);
	if (failures == 0)
		printf("ALL TESTS PASSED\n");
	return failures == 0 ? 0 : 1;
}
