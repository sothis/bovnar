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

#include <math.h>
#include <stdio.h>
#include <string.h>
#include "bovnar.h"
#include "bvn_internal_dims.h"
#include "bovnar_si_units.h"

/*
 * Internal (non-public) guard exported by bovnar_utils.c: recomputes the
 * bu_first_for_len / bu_max_len length index from bu_table and returns whether
 * it matches the precomputed static-const literals the parser actually uses.
 * The literals exist so unit lookup needs no lazy init and stays thread-safe;
 * this check fails the build's test step if a table edit makes them stale.
 */
extern bool bvn_bu_index_selfcheck(void);

static int failures = 0;
static int tests    = 0;

#define ASSERT_TRUE(cond, msg) do { \
	tests++; \
	if (!(cond)) { \
		fprintf(stderr, "FAIL line %d: %s\n", __LINE__, (msg)); \
		failures++; \
	} \
} while (0)

#define ASSERT_FALSE(cond, msg)  ASSERT_TRUE(!(cond), msg)
#define ASSERT_EQ_INT(a, b, msg) ASSERT_TRUE((int64_t)(a) == (int64_t)(b), msg)
#define ASSERT_EQ_DBL(a, b, tol, msg) do { \
	tests++; \
	double _a = (a), _b = (b); \
	if (fabs(_a - _b) > (tol)) { \
		fprintf(stderr, "FAIL line %d: %s\n  got %.15g, expected %.15g\n", \
				__LINE__, (msg), _a, _b); \
		failures++; \
	} \
} while (0)

static void test_bu_index_selfcheck(void)
{
	printf("  bu_first_for_len precomputed-index self-check...\n");
	ASSERT_TRUE(bvn_bu_index_selfcheck(),
		"precomputed bu_first_for_len/bu_max_len must match the table; "
		"regenerate the literals in bovnar_utils.c after editing bu_table");
}

static void test_new_units_in_enum(void)
{
	printf("  new enum values...\n");
	ASSERT_TRUE(bu_radian    > bu_celsius,                 "bu_radian after bu_celsius");
	ASSERT_TRUE(bu_steradian > bu_radian,                  "bu_steradian after bu_radian");
	ASSERT_TRUE(bu_steradian < BVN_VALUE_BASE_UNIT_COUNT,  "bu_steradian before sentinel");
}

static void test_radian_steradian_si_factor(void)
{
	printf("  radian/steradian SI factor...\n");
	bool aff; double off; bool ok = true;

	double f_rad = bvn_unit_to_si_factor(BVN_UNIT_NO_PREFIX(bu_radian),
										  &aff, &off, &ok);
	ASSERT_EQ_DBL(f_rad, 1.0, 1e-15, "rad → 1.0");
	ASSERT_FALSE(aff, "rad not affine");
	ASSERT_TRUE(ok,   "rad ok");

	double f_sr  = bvn_unit_to_si_factor(BVN_UNIT_NO_PREFIX(bu_steradian),
										  &aff, &off, &ok);
	ASSERT_EQ_DBL(f_sr, 1.0, 1e-15, "sr → 1.0");
	ASSERT_FALSE(aff, "sr not affine");
	ASSERT_TRUE(ok,   "sr ok");

	double f_mrad = bvn_unit_to_si_factor(BVN_UNIT_SI(bu_radian, si_milli),
										   &aff, &off, &ok);
	ASSERT_EQ_DBL(f_mrad, 1e-3, 1e-15, "m~rad → 1e-3");

	double f_ksr  = bvn_unit_to_si_factor(BVN_UNIT_SI(bu_steradian, si_kilo),
										   &aff, &off, &ok);
	ASSERT_EQ_DBL(f_ksr, 1e3, 1e-10, "k~sr → 1e3");
}

static void test_radian_steradian_dim_vector(void)
{
	printf("  radian/steradian dimension vector...\n");
	int32_t dims[7];
	ASSERT_TRUE(bvn_unit_dimension_vector(BVN_UNIT_NO_PREFIX(bu_radian), dims),
				"rad dimvec ok");
	for (int i = 0; i < 7; i++)
		ASSERT_EQ_INT(dims[i], 0, "rad dim[i] == 0");

	ASSERT_TRUE(bvn_unit_dimension_vector(BVN_UNIT_NO_PREFIX(bu_steradian), dims),
				"sr dimvec ok");
	for (int i = 0; i < 7; i++)
		ASSERT_EQ_INT(dims[i], 0, "sr dim[i] == 0");
}

static void test_parse_new_symbols(void)
{
	printf("  parse new unit symbols rad/sr...\n");
	bool ok;
	value_unit_t u;

	u = bvn_parse_unit((const uint8_t *)"rad", &ok);
	ASSERT_TRUE(ok,  "parse 'rad' ok");
	ASSERT_EQ_INT(u.num_components, 1, "rad 1 component");
	ASSERT_EQ_INT(u.components[0].base, bu_radian, "rad → bu_radian");

	u = bvn_parse_unit((const uint8_t *)"sr", &ok);
	ASSERT_TRUE(ok,  "parse 'sr' ok");
	ASSERT_EQ_INT(u.num_components, 1, "sr 1 component");
	ASSERT_EQ_INT(u.components[0].base, bu_steradian, "sr → bu_steradian");

	u = bvn_parse_unit((const uint8_t *)"m~rad", &ok);
	ASSERT_TRUE(ok,  "parse 'm~rad' ok");
	ASSERT_EQ_INT(u.components[0].base,        bu_radian, "m~rad base");
	ASSERT_EQ_INT(u.components[0].prefix.id.si, si_milli, "m~rad prefix");

	u = bvn_parse_unit((const uint8_t *)"rad/s", &ok);
	ASSERT_TRUE(ok, "parse 'rad/s' ok");
	ASSERT_EQ_INT(u.num_components, 2, "rad/s has 2 components");
}

static void test_parse_long_name_aliases(void)
{
	printf("  parse long-name aliases...\n");
	bool ok;
	value_unit_t u;

	struct { const char *s; value_base_unit_t exp; } cases[] = {
		{ "sec",        bu_second    },
		{ "second",     bu_second    },
		{ "seconds",    bu_second    },
		{ "meter",      bu_meter     },
		{ "metre",      bu_meter     },
		{ "meters",     bu_meter     },
		{ "metres",     bu_meter     },
		{ "gram",       bu_gram      },
		{ "grams",      bu_gram      },
		{ "ampere",     bu_ampere    },
		{ "amperes",    bu_ampere    },
		{ "amp",        bu_ampere    },
		{ "amps",       bu_ampere    },
		{ "kelvin",     bu_kelvin    },
		{ "kelvins",    bu_kelvin    },
		{ "mole",       bu_mol       },
		{ "moles",      bu_mol       },
		{ "candela",    bu_candela   },
		{ "candelas",   bu_candela   },
		{ "hertz",      bu_hertz     },
		{ "newton",     bu_newton    },
		{ "newtons",    bu_newton    },
		{ "pascal",     bu_pascal    },
		{ "pascals",    bu_pascal    },
		{ "joule",      bu_joule     },
		{ "joules",     bu_joule     },
		{ "watt",       bu_watt      },
		{ "watts",      bu_watt      },
		{ "volt",       bu_volt      },
		{ "volts",      bu_volt      },
		{ "ohm",        bu_ohm       },
		{ "ohms",       bu_ohm       },
		{ "farad",      bu_farad     },
		{ "farads",     bu_farad     },
		{ "coulomb",    bu_coulomb   },
		{ "coulombs",   bu_coulomb   },
		{ "siemens",    bu_siemens   },
		{ "weber",      bu_weber     },
		{ "webers",     bu_weber     },
		{ "tesla",      bu_tesla     },
		{ "teslas",     bu_tesla     },
		{ "henry",      bu_henry     },
		{ "henrys",     bu_henry     },
		{ "henries",    bu_henry     },
		{ "lumen",      bu_lumen     },
		{ "lumens",     bu_lumen     },
		{ "lux",        bu_lux       },
		{ "becquerel",  bu_becquerel },
		{ "becquerels", bu_becquerel },
		{ "gray",       bu_gray      },
		{ "grays",      bu_gray      },
		{ "sievert",    bu_sievert   },
		{ "sieverts",   bu_sievert   },
		{ "katal",      bu_katal     },
		{ "katals",     bu_katal     },
		{ "liter",      bu_liter     },
		{ "litre",      bu_liter     },
		{ "liters",     bu_liter     },
		{ "litres",     bu_liter     },
		{ "l",          bu_liter     },
		{ "celsius",    bu_celsius   },
		{ "degree",     bu_degree    },
		{ "degrees",    bu_degree    },
		{ "degr",       bu_degree    },
		{ "deg",        bu_degree    },
		{ "degC",       bu_celsius   },
		{ "degrC",      bu_celsius   },
		{ "hour",       bu_hour      },
		{ "hours",      bu_hour      },
		{ "minute",     bu_minute    },
		{ "minutes",    bu_minute    },
		{ "day",        bu_day       },
		{ "days",       bu_day       },
		{ "bit",        bu_bit       },
		{ "bits",       bu_bit       },
		{ "byte",       bu_byte      },
		{ "bytes",      bu_byte      },
		{ "Byte",       bu_byte      },
		{ "Bytes",      bu_byte      },
		{ "radian",     bu_radian    },
		{ "radians",    bu_radian    },
		{ "steradian",  bu_steradian },
		{ "steradians", bu_steradian },
		/* Regression: documented aliases that must parse.
		 * "fluid_ounce_uk" once carried a wrong length (13 vs 14) and
		 * was unparseable; "mils" (plural of mil/thou) was missing. */
		{ "thou",            bu_thou           },
		{ "mil",             bu_thou           },
		{ "mils",            bu_thou           },
		{ "fl_oz_uk",        bu_fluid_ounce_uk },
		{ "fluid_ounce_uk",  bu_fluid_ounce_uk },
		{ "fluid_ounces_uk", bu_fluid_ounce_uk },
	};
	size_t n = sizeof(cases) / sizeof(cases[0]);
	for (size_t i = 0; i < n; i++) {
		u = bvn_parse_unit((const uint8_t *)cases[i].s, &ok);
		char msg[128];
		snprintf(msg, sizeof(msg), "parse '%s' ok", cases[i].s);
		ASSERT_TRUE(ok, msg);
		snprintf(msg, sizeof(msg), "'%s' base unit", cases[i].s);
		ASSERT_EQ_INT(u.components[0].base, (int64_t)cases[i].exp, msg);
	}
}

static void test_prefix_unit_valid_function(void)
{
	printf("  bvn_prefix_unit_valid...\n");

#define PFX_SI(p)  ((value_unit_prefix_t){prefix_si,  .id.si  = (p)})
#define PFX_IEC(p) ((value_unit_prefix_t){prefix_iec, .id.iec = (p)})

	ASSERT_TRUE(bvn_prefix_unit_valid(PFX_SI(si_none),  bu_meter),
				"si_none on meter OK");
	ASSERT_TRUE(bvn_prefix_unit_valid(PFX_SI(si_kilo),  bu_meter),
				"si_kilo on meter OK");
	ASSERT_TRUE(bvn_prefix_unit_valid(PFX_SI(si_mega),  bu_hertz),
				"si_mega on hertz OK");
	ASSERT_TRUE(bvn_prefix_unit_valid(PFX_SI(si_none),  bu_bit),
				"si_none on bit OK (no-prefix sentinel)");
	ASSERT_TRUE(bvn_prefix_unit_valid(PFX_SI(si_none),  bu_byte),
				"si_none on byte OK");

	ASSERT_TRUE(bvn_prefix_unit_valid(PFX_SI(si_kilo), bu_bit),
				"si_kilo on bit OK");
	ASSERT_TRUE(bvn_prefix_unit_valid(PFX_SI(si_mega), bu_byte),
				"si_mega on byte OK");
	ASSERT_TRUE(bvn_prefix_unit_valid(PFX_SI(si_giga), bu_byte),
				"si_giga on byte OK");

	ASSERT_TRUE(bvn_prefix_unit_valid(PFX_IEC(iec_none), bu_bit),
				"iec_none on bit OK");
	ASSERT_TRUE(bvn_prefix_unit_valid(PFX_IEC(iec_kibi), bu_bit),
				"iec_kibi on bit OK");
	ASSERT_TRUE(bvn_prefix_unit_valid(PFX_IEC(iec_gibi), bu_byte),
				"iec_gibi on byte OK");
	ASSERT_TRUE(bvn_prefix_unit_valid(PFX_IEC(iec_none), bu_meter),
				"iec_none on meter OK (no-prefix sentinel)");

	ASSERT_FALSE(bvn_prefix_unit_valid(PFX_IEC(iec_kibi), bu_meter),
				 "iec_kibi on meter INVALID");
	ASSERT_FALSE(bvn_prefix_unit_valid(PFX_IEC(iec_mebi), bu_second),
				 "iec_mebi on second INVALID");
	ASSERT_FALSE(bvn_prefix_unit_valid(PFX_IEC(iec_gibi), bu_hertz),
				 "iec_gibi on hertz INVALID");

	ASSERT_FALSE(bvn_prefix_unit_valid(PFX_SI(si_milli), bu_bit),
				 "si_milli on bit INVALID");
	ASSERT_FALSE(bvn_prefix_unit_valid(PFX_SI(si_milli), bu_byte),
				 "si_milli on byte INVALID");
	ASSERT_FALSE(bvn_prefix_unit_valid(PFX_SI(si_micro), bu_bit),
				 "si_micro on bit INVALID");
	ASSERT_FALSE(bvn_prefix_unit_valid(PFX_SI(si_micro), bu_byte),
				 "si_micro on byte INVALID");
	ASSERT_FALSE(bvn_prefix_unit_valid(PFX_SI(si_nano),  bu_byte),
				 "si_nano on byte INVALID");
	ASSERT_FALSE(bvn_prefix_unit_valid(PFX_SI(si_hecto), bu_bit),
				 "si_hecto on bit INVALID (positive but below kilo)");
	ASSERT_FALSE(bvn_prefix_unit_valid(PFX_SI(si_deca),  bu_byte),
				 "si_deca on byte INVALID (positive but below kilo)");

#undef PFX_SI
#undef PFX_IEC
}

static void test_prefix_enforcement_via_parse(void)
{
	printf("  prefix enforcement via bvn_parse_unit...\n");
	bool ok;

	bvn_parse_unit((const uint8_t *)"Ki~B", &ok);
	ASSERT_TRUE(ok, "Ki~B (iec on byte) accepted");

	bvn_parse_unit((const uint8_t *)"Mi~b", &ok);
	ASSERT_TRUE(ok, "Mi~b (iec on bit) accepted");

	bvn_parse_unit((const uint8_t *)"Gi~B", &ok);
	ASSERT_TRUE(ok, "Gi~B (iec on byte) accepted");

	bvn_parse_unit((const uint8_t *)"k~m", &ok);
	ASSERT_TRUE(ok, "k~m (si on meter) accepted");

	bvn_parse_unit((const uint8_t *)"M~Hz", &ok);
	ASSERT_TRUE(ok, "M~Hz (si on hertz) accepted");

	bvn_parse_unit((const uint8_t *)"k~B", &ok);
	ASSERT_TRUE(ok, "k~B (si kilo on byte) accepted");

	bvn_parse_unit((const uint8_t *)"M~b", &ok);
	ASSERT_TRUE(ok, "M~b (si mega on bit) accepted");

	bvn_parse_unit((const uint8_t *)"G~B", &ok);
	ASSERT_TRUE(ok, "G~B (si giga on byte) accepted");

	bvn_parse_unit((const uint8_t *)"k~bit", &ok);
	ASSERT_TRUE(ok, "k~bit (si kilo on bit longform) accepted");

	bvn_parse_unit((const uint8_t *)"M~Byte", &ok);
	ASSERT_TRUE(ok, "M~Byte (si mega on Byte longform) accepted");

	bvn_parse_unit((const uint8_t *)"G~Bytes", &ok);
	ASSERT_TRUE(ok, "G~Bytes (si giga on Bytes longform) accepted");

	bvn_parse_unit((const uint8_t *)"Ki~m", &ok);
	ASSERT_FALSE(ok, "Ki~m (iec on meter) rejected");

	bvn_parse_unit((const uint8_t *)"Mi~s", &ok);
	ASSERT_FALSE(ok, "Mi~s (iec on second) rejected");

	bvn_parse_unit((const uint8_t *)"Gi~J", &ok);
	ASSERT_FALSE(ok, "Gi~J (iec on joule) rejected");

	bvn_parse_unit((const uint8_t *)"Ki~kg", &ok);
	ASSERT_FALSE(ok, "Ki~kg does not parse (no 'kg' symbol) → rejected");

	bvn_parse_unit((const uint8_t *)"B", &ok);
	ASSERT_TRUE(ok, "bare B (no prefix) accepted");

	bvn_parse_unit((const uint8_t *)"b", &ok);
	ASSERT_TRUE(ok, "bare b (no prefix) accepted");

	bvn_parse_unit((const uint8_t *)"m~B", &ok);
	ASSERT_FALSE(ok, "m~B (si milli on byte) rejected");

	bvn_parse_unit((const uint8_t *)"m~b", &ok);
	ASSERT_FALSE(ok, "m~b (si milli on bit) rejected");

	bvn_parse_unit((const uint8_t *)"n~B", &ok);
	ASSERT_FALSE(ok, "n~B (si nano on byte) rejected");

	bvn_parse_unit((const uint8_t *)"\xc2\xb5~b", &ok);
	ASSERT_FALSE(ok, "\xc2\xb5~b (si micro on bit) rejected");

	bvn_parse_unit((const uint8_t *)"m~bit", &ok);
	ASSERT_FALSE(ok, "m~bit (si milli on bit longform) rejected");

	bvn_parse_unit((const uint8_t *)"m~byte", &ok);
	ASSERT_FALSE(ok, "m~byte (si milli on byte longform) rejected");

	bvn_parse_unit((const uint8_t *)"h~B", &ok);
	ASSERT_FALSE(ok, "h~B (si hecto on byte) rejected (positive but below kilo)");

	bvn_parse_unit((const uint8_t *)"da~b", &ok);
	ASSERT_FALSE(ok, "da~b (si deca on bit) rejected (positive but below kilo)");
}

static void test_alias_with_prefix(void)
{
	printf("  aliases work with SI prefixes...\n");
	bool ok;
	value_unit_t u;

	u = bvn_parse_unit((const uint8_t *)"k~seconds", &ok);
	ASSERT_TRUE(ok, "k~seconds ok");
	ASSERT_EQ_INT(u.components[0].base,         bu_second, "k~seconds base");
	ASSERT_EQ_INT(u.components[0].prefix.id.si, si_kilo,   "k~seconds prefix");

	u = bvn_parse_unit((const uint8_t *)"m~meters", &ok);
	ASSERT_TRUE(ok, "m~meters ok");
	ASSERT_EQ_INT(u.components[0].base,         bu_meter, "m~meters base");
	ASSERT_EQ_INT(u.components[0].prefix.id.si, si_milli, "m~meters prefix");

	u = bvn_parse_unit((const uint8_t *)"k~joules", &ok);
	ASSERT_TRUE(ok, "k~joules ok");
	ASSERT_EQ_INT(u.components[0].base,         bu_joule, "k~joules base");

	u = bvn_parse_unit((const uint8_t *)"m~radians", &ok);
	ASSERT_TRUE(ok, "m~radians ok");
	ASSERT_EQ_INT(u.components[0].base,         bu_radian, "m~radians base");
	ASSERT_EQ_INT(u.components[0].prefix.id.si, si_milli,  "m~radians prefix");

	u = bvn_parse_unit((const uint8_t *)"Ki~bytes", &ok);
	ASSERT_TRUE(ok, "Ki~bytes ok");
	ASSERT_EQ_INT(u.components[0].base,          bu_byte,   "Ki~bytes base");
	ASSERT_EQ_INT(u.components[0].prefix.id.iec, iec_kibi,  "Ki~bytes prefix");

	u = bvn_parse_unit((const uint8_t *)"k~bytes", &ok);
	ASSERT_TRUE(ok, "k~bytes (si kilo on byte alias) accepted");
	ASSERT_EQ_INT(u.components[0].base,         bu_byte,  "k~bytes base");
	ASSERT_EQ_INT(u.components[0].prefix.id.si, si_kilo,  "k~bytes prefix");

	u = bvn_parse_unit((const uint8_t *)"k~Bytes", &ok);
	ASSERT_TRUE(ok, "k~Bytes (si kilo on Bytes alias) accepted");
	ASSERT_EQ_INT(u.components[0].base,         bu_byte,  "k~Bytes base");
	ASSERT_EQ_INT(u.components[0].prefix.id.si, si_kilo,  "k~Bytes prefix");

	u = bvn_parse_unit((const uint8_t *)"M~Byte", &ok);
	ASSERT_TRUE(ok, "M~Byte (si mega on Byte alias) accepted");
	ASSERT_EQ_INT(u.components[0].base,         bu_byte,  "M~Byte base");
	ASSERT_EQ_INT(u.components[0].prefix.id.si, si_mega,  "M~Byte prefix");

	u = bvn_parse_unit((const uint8_t *)"k~bit", &ok);
	ASSERT_TRUE(ok, "k~bit (si kilo on bit alias) accepted");
	ASSERT_EQ_INT(u.components[0].base,         bu_bit,   "k~bit base");
	ASSERT_EQ_INT(u.components[0].prefix.id.si, si_kilo,  "k~bit prefix");

	u = bvn_parse_unit((const uint8_t *)"G~bits", &ok);
	ASSERT_TRUE(ok, "G~bits (si giga on bits alias) accepted");
	ASSERT_EQ_INT(u.components[0].base,         bu_bit,   "G~bits base");
	ASSERT_EQ_INT(u.components[0].prefix.id.si, si_giga,  "G~bits prefix");

	u = bvn_parse_unit((const uint8_t *)"Ki~seconds", &ok);
	ASSERT_FALSE(ok, "Ki~seconds (iec on second alias) rejected");
}

/*
 * Compact (tilde-less) prefixed spellings: "kg" for "k~g".
 *
 * The three sections below cover the three things that make the form safe. It
 * resolves to exactly the same value_unit_t as the separated spelling; a bare
 * unit alias always wins over a prefixed reading, so nothing that parsed before
 * can parse differently now; and a prefix still cannot be stacked, smuggled in
 * past a stray '~', or attached to a unit whose policy forbids it.
 */
static void assert_compact_matches(const char *compact, const char *separated)
{
	bool okc = false, oks = false;
	value_unit_t c = bvn_parse_unit((const uint8_t *)compact,   &okc);
	value_unit_t s = bvn_parse_unit((const uint8_t *)separated, &oks);
	char msg[128];
	snprintf(msg, sizeof(msg), "%s parses", compact);
	ASSERT_TRUE(okc, msg);
	snprintf(msg, sizeof(msg), "%s parses", separated);
	ASSERT_TRUE(oks, msg);
	if (!okc || !oks)
		return;
	snprintf(msg, sizeof(msg), "%s == %s (component count)", compact, separated);
	ASSERT_EQ_INT(c.num_components, s.num_components, msg);
	for (uint32_t i = 0; i < c.num_components && i < s.num_components; i++) {
		snprintf(msg, sizeof(msg), "%s == %s (base %u)", compact, separated, i);
		ASSERT_EQ_INT(c.components[i].base, s.components[i].base, msg);
		snprintf(msg, sizeof(msg), "%s == %s (prefix system %u)", compact, separated, i);
		ASSERT_EQ_INT(c.components[i].prefix.system, s.components[i].prefix.system, msg);
		snprintf(msg, sizeof(msg), "%s == %s (prefix id %u)", compact, separated, i);
		ASSERT_EQ_INT(c.components[i].prefix.id.si, s.components[i].prefix.id.si, msg);
		snprintf(msg, sizeof(msg), "%s == %s (exponent %u)", compact, separated, i);
		ASSERT_EQ_INT(c.components[i].exponent, s.components[i].exponent, msg);
	}
	snprintf(msg, sizeof(msg), "%s prefix factor == %s", compact, separated);
	ASSERT_EQ_DBL(bvn_unit_prefix_factor(c), bvn_unit_prefix_factor(s), 0.0, msg);
}

static void assert_unit_rejected(const char *spelling)
{
	bool ok = true;
	char msg[128];
	(void)bvn_parse_unit((const uint8_t *)spelling, &ok);
	snprintf(msg, sizeof(msg), "%s rejected", spelling);
	ASSERT_FALSE(ok, msg);
}

static void assert_parses_bare(const char *spelling, value_base_unit_t base)
{
	bool ok = false;
	char msg[128];
	value_unit_t u = bvn_parse_unit((const uint8_t *)spelling, &ok);
	snprintf(msg, sizeof(msg), "%s parses", spelling);
	ASSERT_TRUE(ok, msg);
	if (!ok)
		return;
	snprintf(msg, sizeof(msg), "%s is the bare unit, not a prefixed reading", spelling);
	ASSERT_EQ_INT(u.components[0].base, base, msg);
	snprintf(msg, sizeof(msg), "%s carries no prefix", spelling);
	ASSERT_EQ_INT(u.components[0].prefix.id.si, si_none, msg);
}

static void test_compact_prefix_equivalence(void)
{
	printf("  compact prefix form parses as the separated form...\n");

	/* SI prefixes on SI, metric and non-SI bases, single- and multi-byte. */
	assert_compact_matches("kg",     "k~g");
	assert_compact_matches("km",     "k~m");
	assert_compact_matches("cm",     "c~m");
	assert_compact_matches("ms",     "m~s");
	assert_compact_matches("us",     "u~s");
	assert_compact_matches("\xc2\xb5s", "\xc2\xb5~s");   /* µs */
	assert_compact_matches("MHz",    "M~Hz");
	assert_compact_matches("hPa",    "h~Pa");
	assert_compact_matches("mmol",   "m~mol");
	assert_compact_matches("mL",     "m~L");
	assert_compact_matches("MeV",    "M~eV");
	assert_compact_matches("kDa",    "k~Da");
	assert_compact_matches("Mpc",    "M~pc");
	assert_compact_matches("kcal",   "k~cal");
	assert_compact_matches("mrem",   "m~rem");
	assert_compact_matches("cSt",    "c~St");
	assert_compact_matches("cP",     "c~P");
	assert_compact_matches("mGal",   "m~Gal");
	assert_compact_matches("Mt",     "M~t");
	assert_compact_matches("mseconds", "m~seconds");     /* long-name alias */

	/* IEC prefixes, which only the information units accept. */
	assert_compact_matches("KiB",    "Ki~B");
	assert_compact_matches("MiB",    "Mi~B");
	assert_compact_matches("GiB",    "Gi~B");
	assert_compact_matches("Kib",    "Ki~b");
	assert_compact_matches("MB",     "M~B");
	assert_compact_matches("Gbit",   "G~bit");

	/* Currencies take the compact prefix too. The '$' sigil separates prefix
	 * from code on its own, so there is nothing here for the '~' to resolve. */
	assert_compact_matches("k$USD",  "k~$USD");
	assert_compact_matches("M$EUR",  "M~$EUR");
	assert_compact_matches("G$ETH",  "G~$ETH");
	assert_compact_matches("m$USD",  "m~$USD");
	assert_compact_matches("da$USD", "da~$USD");
	assert_compact_matches("u$USD",  "u~$USD");
	assert_compact_matches("k$USD/h", "k~$USD/h");

	/* Compound expressions, exponents and groups compose unchanged. */
	assert_compact_matches("kg\xc2\xb7m/s^2", "k~g\xc2\xb7m/s^2");
	assert_compact_matches("km/h",   "k~m/h");
	assert_compact_matches("MB/s",   "M~B/s");
	assert_compact_matches("mg/L",   "m~g/L");
	assert_compact_matches("km^2",   "k~m^2");
	assert_compact_matches("kg/(m\xc2\xb7s^2)", "k~g/(m\xc2\xb7s^2)");

	/* The canonical output form is unchanged: a compact spelling is an input
	 * alias, so a document that round-trips through the writer comes back out
	 * separated and stays readable to a reader that predates this. */
	{
		bool ok = false;
		char buf[64];
		value_unit_t u = bvn_parse_unit((const uint8_t *)"kg", &ok);
		ASSERT_TRUE(ok, "kg parses");
		ASSERT_TRUE(bvn_unit_to_string(u, buf, sizeof(buf)) > 0, "kg to_string");
		ASSERT_TRUE(strcmp(buf, "k~g") == 0, "kg serialises back as k~g");

		u = bvn_parse_unit((const uint8_t *)"MiB", &ok);
		ASSERT_TRUE(ok, "MiB parses");
		ASSERT_TRUE(bvn_unit_to_string(u, buf, sizeof(buf)) > 0, "MiB to_string");
		ASSERT_TRUE(strcmp(buf, "Mi~B") == 0, "MiB serialises back as Mi~B");

		u = bvn_parse_unit((const uint8_t *)"k$USD", &ok);
		ASSERT_TRUE(ok, "k$USD parses");
		ASSERT_TRUE(bvn_unit_to_string(u, buf, sizeof(buf)) > 0, "k$USD to_string");
		ASSERT_TRUE(strcmp(buf, "k~$USD") == 0, "k$USD serialises back as k~$USD");
	}
}

static void test_compact_currency_form(void)
{
	printf("  compact prefix form on currencies...\n");

	/* The sigil is still mandatory — it is what dispatches a component to the
	 * currency table — and it must introduce a known code. */
	assert_unit_rejected("kUSD");        /* no sigil: a bare code is not money */
	assert_unit_rejected("k$XYZ");       /* sigil, unknown code               */
	assert_unit_rejected("k$");          /* sigil, no code                    */
	assert_unit_rejected("$");
	assert_unit_rejected("~$EUR");       /* empty prefix                      */
	assert_unit_rejected("k~~$EUR");     /* malformed separated form          */
	assert_unit_rejected("kg$USD");      /* "kg" is not a prefix              */

	/* IEC prefixes are forbidden on money in either spelling. */
	assert_unit_rejected("Ki$USD");
	assert_unit_rejected("Ki~$USD");

	/* An unprefixed currency is unchanged. */
	{
		bool ok = false;
		value_unit_t u = bvn_parse_unit((const uint8_t *)"$BTC", &ok);
		ASSERT_TRUE(ok, "$BTC parses");
		ASSERT_EQ_INT(u.components[0].prefix.id.si, si_none, "$BTC has no prefix");
	}
}

static void test_compact_prefix_precedence(void)
{
	printf("  compact prefix form never outranks a bare unit alias...\n");

	/* Every bare alias that a prefix+base reading could also spell. The bare
	 * unit wins because the base symbol is matched as the LONGEST alias suffix,
	 * which is what keeps existing documents decoding identically. */
	assert_parses_bare("min", bu_minute);                /* not milli-inch    */
	assert_parses_bare("cd",  bu_candela);               /* not centi-day     */
	assert_parses_bare("ft",  bu_foot);                  /* not femto-tonne   */
	assert_parses_bare("pt",  bu_pint);                  /* not pico-tonne    */
	assert_parses_bare("qt",  bu_quart);                 /* not quecto-tonne  */
	assert_parses_bare("ct",  bu_carat);                 /* not centi-tonne   */
	assert_parses_bare("yd",  bu_yard);                  /* not yocto-day     */
	assert_parses_bare("rd",  bu_rod);                   /* not ronto-day     */
	assert_parses_bare("ch",  bu_chain);                 /* not centi-hour    */
	assert_parses_bare("ph",  bu_phot);                  /* not pico-hour     */
	assert_parses_bare("at",  bu_atmosphere_technical);  /* not atto-tonne    */
	assert_parses_bare("au",  bu_astronomical_unit);     /* not atto-dalton   */
	assert_parses_bare("kat", bu_katal);                 /* not kilo-at       */
	assert_parses_bare("nmi", bu_nautical_mile);         /* not nano-mile     */
	assert_parses_bare("PS",  bu_metric_horsepower);     /* not peta-siemens  */
	assert_parses_bare("dB",  bu_decibel);               /* not deci-byte     */

	/* Where two prefixed readings compete, the longest base suffix decides, so
	 * the result is deterministic rather than ambiguous: "at"/"au" beat "t"/"u". */
	assert_compact_matches("dat", "d~at");
	assert_compact_matches("dau", "d~au");

	/* A prefix cannot be stacked, in either spelling or in combination. */
	assert_unit_rejected("kkg");
	assert_unit_rejected("k~kg");
	assert_unit_rejected("kmm");
	assert_unit_rejected("k~mm");
	assert_unit_rejected("kKiB");
	assert_unit_rejected("k~KiB");

	/* A remainder that still contains a '~' is a malformed separated form, not
	 * a compact one. */
	assert_unit_rejected("k~~g");
	assert_unit_rejected("~g");
	assert_unit_rejected("k~");
	assert_unit_rejected("kg~");

	/* Unknown prefixes stay unknown — the compact form widens the accepted
	 * spellings, not the tables. */
	assert_unit_rejected("mcg");            /* micrograms, spelled the US way */
	assert_unit_rejected("kWh");            /* needs a separator: kW\xc2\xb7h */
	assert_unit_rejected("cc");
	assert_unit_rejected("zzm");

	/* Per-unit prefix policy is enforced identically in both spellings. */
	assert_unit_rejected("Kim");            /* IEC prefix on a length unit    */
	assert_unit_rejected("Ki~m");
	assert_unit_rejected("mB");             /* SI prefix < kilo on a byte     */
	assert_unit_rejected("m~B");
	assert_unit_rejected("mb");             /* ... and on a bit               */
	assert_unit_rejected("kPfd");           /* prefix on a German unit        */
	assert_unit_rejected("k~Pfd");
	assert_unit_rejected("kppm");           /* prefix on a ratio unit         */
	assert_unit_rejected("k~ppm");
}

static void test_compact_prefix_exceptions(void)
{
	printf("  compact spellings that are refused by name...\n");

	/* Tokens that are well-known annotations for something else. Accepting them
	 * would turn a parse error into a quietly wrong unit, so they stay errors:
	 * "usb" is not the microstilb, and "kt" abbreviates BOTH the kilotonne and
	 * the knot — two units bovnar models — so only the author can resolve it. */
	assert_unit_rejected("usb");
	assert_unit_rejected("kt");

	/* The exponent suffix is stripped before the check, so a decorated form is
	 * refused too. */
	assert_unit_rejected("usb^2");
	assert_unit_rejected("kt^-1");

	/* pH, mph and kph were on that list until the units they name were added.
	 * A bare alias outranks any prefixed reading, so they now need no entry —
	 * and the prefixed readings they used to be mistaken for are still there
	 * under their own spellings. */
	{
		bool ok = false;
		value_unit_t u = bvn_parse_unit((const uint8_t *)"pH", &ok);
		ASSERT_TRUE(ok, "pH parses");
		ASSERT_EQ_INT(u.components[0].base, bu_ph_scale, "pH is the acidity scale");
		u = bvn_parse_unit((const uint8_t *)"mph", &ok);
		ASSERT_TRUE(ok, "mph parses");
		ASSERT_EQ_INT(u.components[0].base, bu_mile_per_hour, "mph is a speed");
		u = bvn_parse_unit((const uint8_t *)"kph", &ok);
		ASSERT_TRUE(ok, "kph parses");
		ASSERT_EQ_INT(u.components[0].base, bu_kilometer_per_hour, "kph is a speed");
		u = bvn_parse_unit((const uint8_t *)"ph", &ok);
		ASSERT_TRUE(ok, "ph parses");
		ASSERT_EQ_INT(u.components[0].base, bu_phot, "ph is still the phot");
	}

	/* Only the compact spelling is refused; the separated one is spec-1.0
	 * syntax and keeps its meaning. */
	{
		bool ok = false;
		value_unit_t u = bvn_parse_unit((const uint8_t *)"p~H", &ok);
		ASSERT_TRUE(ok, "p~H (picohenry) still parses");
		ASSERT_EQ_INT(u.components[0].base,         bu_henry, "p~H base");
		ASSERT_EQ_INT(u.components[0].prefix.id.si, si_pico,  "p~H prefix");

		u = bvn_parse_unit((const uint8_t *)"m~ph", &ok);
		ASSERT_TRUE(ok, "m~ph (milliphot) still parses");
		ASSERT_EQ_INT(u.components[0].base,         bu_phot,  "m~ph base");
		ASSERT_EQ_INT(u.components[0].prefix.id.si, si_milli, "m~ph prefix");

	/* The units added for pH/mph/kph take no prefix at all: a milli-pH is not a
	 * quantity, and a kilo-kph is a prefix on a prefix. */
	assert_unit_rejected("m~pH");
	assert_unit_rejected("k~pH");
	assert_unit_rejected("m~mph");
	assert_unit_rejected("k~kph");

		u = bvn_parse_unit((const uint8_t *)"u~sb", &ok);
		ASSERT_TRUE(ok, "u~sb (microstilb) still parses");
		ASSERT_EQ_INT(u.components[0].base,         bu_stilb, "u~sb base");
		ASSERT_EQ_INT(u.components[0].prefix.id.si, si_micro, "u~sb prefix");

		u = bvn_parse_unit((const uint8_t *)"k~t", &ok);
		ASSERT_TRUE(ok, "k~t (kilotonne) still parses");
		ASSERT_EQ_INT(u.components[0].base,         bu_tonne, "k~t base");
		ASSERT_EQ_INT(u.components[0].prefix.id.si, si_kilo,  "k~t prefix");

		u = bvn_parse_unit((const uint8_t *)"kn", &ok);
		ASSERT_TRUE(ok, "kn (knot) still parses");
		ASSERT_EQ_INT(u.components[0].base, bu_knot, "kn base");
	}

	/* A refusal is per-spelling, not per-unit: other compact prefixes on the
	 * same bases keep working. */
	assert_compact_matches("nH",  "n~H");
	assert_compact_matches("mH",  "m~H");
	assert_compact_matches("nph", "n~ph");
	assert_compact_matches("ksb", "k~sb");
	assert_compact_matches("Mt",  "M~t");
}

/*
 * Water hardness: six scales for one quantity — the concentration of dissolved
 * alkaline-earth ions. Each is defined as a mass of a different reference
 * compound per litre (CaO, CaCO3, Ca), so amount concentration is their only
 * correct common ground; the numbers below are the published conversion table,
 * which is what a reader will check them against.
 */
static void test_water_hardness_units(void)
{
	printf("  water hardness scales...\n");
	bool ok = false;
	double out = 0.0;
	value_unit_t dH  = bvn_parse_unit((const uint8_t *)"\xc2\xb0" "dH", &ok);
	ASSERT_TRUE(ok, "°dH parses");
	value_unit_t mmol_l = bvn_parse_unit((const uint8_t *)"m~mol/L", &ok);
	ASSERT_TRUE(ok, "m~mol/L parses");

	ASSERT_EQ_INT(dH.components[0].base, bu_german_hardness, "°dH base");
	ASSERT_TRUE(bvn_units_compatible(dH, mmol_l),
		    "a hardness degree is an amount concentration");

	/* 1 mmol/L = 5.6077 °dH = 7.0217 °e = 10.0086 °fH = 40.078 °rH
	 *          = 100.086 °aH = 2.000 mval/L  (source table, rounded to 3-4 sf) */
	struct { const char *sym; double per_mmol_l; } scale[] = {
		{ "\xc2\xb0" "dH",  5.6077   },
		{ "\xc2\xb0" "e",   7.0217   },
		{ "\xc2\xb0" "fH", 10.0086   },
		{ "\xc2\xb0" "rH", 40.0780   },
		{ "\xc2\xb0" "aH", 100.0860  },
		{ "m~val/L",       2.0      },
		{ "m~eq/L",        2.0      },   /* the English spelling of mval/L */
		{ "gpg",           5.8468   },
	};
	for (unsigned i = 0; i < sizeof(scale) / sizeof(scale[0]); i++) {
		bool sok = false;
		value_unit_t u = bvn_parse_unit((const uint8_t *)scale[i].sym, &sok);
		char msg[96];
		snprintf(msg, sizeof(msg), "%s parses", scale[i].sym);
		ASSERT_TRUE(sok, msg);
		if (!sok) continue;
		snprintf(msg, sizeof(msg), "1 m~mol/L in %s", scale[i].sym);
		ASSERT_TRUE(bvn_unit_convert_value(1.0, mmol_l, u, &out), msg);
		ASSERT_EQ_DBL(out, scale[i].per_mmol_l, scale[i].per_mmol_l * 1e-4, msg);
	}

	/* The Clark degree is the English degree, not a scale of its own. */
	{
		value_unit_t e = bvn_parse_unit((const uint8_t *)"\xc2\xb0" "e", &ok);
		value_unit_t clark = bvn_parse_unit((const uint8_t *)"\xc2\xb0" "Clark", &ok);
		ASSERT_TRUE(ok, "°Clark parses");
		ASSERT_EQ_INT(clark.components[0].base, e.components[0].base,
			      "°Clark and °e are one unit");
	}

	/* The degree sign is load-bearing: "dH" was, and stays, the decihenry. */
	{
		value_unit_t dh_lower = bvn_parse_unit((const uint8_t *)"dH", &ok);
		ASSERT_TRUE(ok, "dH still parses");
		ASSERT_EQ_INT(dh_lower.components[0].base, bu_henry, "dH is the henry");
		ASSERT_EQ_INT(dh_lower.components[0].prefix.id.si, si_deci, "...deci-");
	}

	/* Hardness degrees take no prefix; val does (m~val is the useful one). */
	assert_unit_rejected("m~\xc2\xb0" "dH");
	assert_unit_rejected("k~\xc2\xb0" "fH");
	assert_compact_matches("mval", "m~val");

	/* Electrical conductivity needs no unit of its own: siemens per length says
	 * it exactly, in every spelling the field uses. "mho" is the pre-SI name for
	 * the siemens and still turns up in older US water and soil data. */
	{
		bool eok = false;
		value_unit_t ec = bvn_parse_unit((const uint8_t *)"\xc2\xb5S/cm", &eok);
		ASSERT_TRUE(eok, "µS/cm parses");
		ASSERT_EQ_INT(ec.num_components, 2, "µS/cm has two components");
		ASSERT_EQ_INT(ec.components[0].base, bu_siemens, "…siemens");
		ASSERT_EQ_INT(ec.components[1].base, bu_meter,   "…per metre");
		ASSERT_TRUE(bvn_units_compatible(ec, bvn_parse_unit((const uint8_t *)"S/m", &eok)),
			    "µS/cm and S/m are the same quantity");
		ASSERT_TRUE(bvn_unit_convert_value(1.0, bvn_parse_unit((const uint8_t *)"m~mho/c~m", &eok),
						   bvn_parse_unit((const uint8_t *)"m~S/c~m", &eok), &out) &&
			    out == 1.0, "a mho is a siemens");
	}

	/* gpg is an amount concentration and converts with the other hardness
	 * scales; gr/gal is a MASS concentration and deliberately does not. */
	{
		bool gok = false;
		ASSERT_TRUE(!bvn_units_compatible(bvn_parse_unit((const uint8_t *)"gpg", &gok),
						  bvn_parse_unit((const uint8_t *)"gr/gal", &gok)),
			    "gpg is not the compound gr/gal");
	}

	/* Water chemistry says "ppm" for °aH, but bovnar's ppm is the
	 * dimensionless 10^-6 — the two must not be interchangeable. */
	{
		value_unit_t aH = bvn_parse_unit((const uint8_t *)"\xc2\xb0" "aH", &ok);
		ASSERT_TRUE(!bvn_units_compatible(aH, BVN_UNIT_NO_PREFIX(bu_ppm)),
			    "the hardness ppm is not the dimensionless ppm");
	}
}

/*
 * Turbidity, salinity and the hydroponic conductivity factor. The first three
 * are dimensionless instrument scales, each in its own quantity kind, so the
 * interesting assertions are the conversions that must NOT happen. CF is the
 * opposite case: a rescaled conductivity, so it must convert.
 */
static void test_water_quality_scales(void)
{
	printf("  turbidity, salinity and the conductivity factor...\n");
	bool ok = false;
	double out = 0.0;
	value_unit_t ntu = bvn_parse_unit((const uint8_t *)"NTU", &ok);
	ASSERT_TRUE(ok, "NTU parses");
	value_unit_t fnu = bvn_parse_unit((const uint8_t *)"FNU", &ok);
	ASSERT_TRUE(ok, "FNU parses");
	value_unit_t psu = bvn_parse_unit((const uint8_t *)"PSU", &ok);
	ASSERT_TRUE(ok, "PSU parses");
	value_unit_t none = BVN_UNIT_NO_PREFIX(bu_none);

	/* NTU is white light, FNU near-infrared. They agree on a formazin standard
	 * and not on real water, so no factor relates them — reporting the method
	 * is the whole point, and silently trading one for the other would undo it. */
	ASSERT_TRUE(!bvn_units_compatible(ntu, fnu), "NTU and FNU are separate kinds");

	/* Five turbidity scales, one per method, and no pair of them relates: the
	 * formazin ones (NTU/FNU/FTU) agree only on a formazin standard, FAU
	 * measures attenuation rather than scatter, and JTU is the visual candle
	 * method whose relation to formazin is nonlinear and sample-dependent. */
	{
		static const value_base_unit_t turb[] = {
			bu_turbidity_ntu, bu_turbidity_fnu, bu_turbidity_ftu,
			bu_turbidity_fau, bu_turbidity_jtu
		};
		for (unsigned i = 0; i < sizeof(turb) / sizeof(turb[0]); i++) {
			for (unsigned j = 0; j < sizeof(turb) / sizeof(turb[0]); j++) {
				value_unit_t a = BVN_UNIT_NO_PREFIX(turb[i]);
				value_unit_t b = BVN_UNIT_NO_PREFIX(turb[j]);
				bool want = (i == j);
				char msg[80];
				snprintf(msg, sizeof(msg),
					 "turbidity kinds %u/%u compatible == %d", i, j, want);
				ASSERT_TRUE(bvn_units_compatible(a, b) == want, msg);
			}
		}
	}
	ASSERT_TRUE(!bvn_unit_convert_value(1.0, ntu, fnu, &out),
		    "1 NTU -> FNU is refused rather than answered with 1");
	ASSERT_TRUE(!bvn_units_compatible(ntu, none), "turbidity is not a plain number");
	ASSERT_TRUE(!bvn_units_compatible(ntu, BVN_UNIT_NO_PREFIX(bu_percent)),
		    "nor a percentage");
	ASSERT_TRUE(bvn_units_compatible(ntu, ntu) &&
		    bvn_unit_convert_value(4.0, ntu, ntu, &out) && out == 4.0,
		    "NTU -> NTU is the identity");
	/* Ultrapure-water work writes m~NTU, so prefixes stay inside the kind. */
	ASSERT_TRUE(bvn_unit_convert_value(1.0, BVN_UNIT_SI(bu_turbidity_ntu, si_milli),
					   ntu, &out) && out == 0.001,
		    "m~NTU scales within its own kind");

	/* Practical salinity is a conductivity ratio, not a mass fraction: S_P 35
	 * is ~35.165 g/kg, so a conversion to per-mille would be wrong by ~0.5%. */
	ASSERT_TRUE(!bvn_units_compatible(psu, BVN_UNIT_NO_PREFIX(bu_per_mille)),
		    "PSU is not per-mille");
	ASSERT_TRUE(!bvn_units_compatible(psu, none), "PSU is not a plain number");
	ASSERT_TRUE(!bvn_units_compatible(psu, ntu), "PSU is not a turbidity");
	assert_unit_rejected("m~PSU");     /* the scale is bounded by construction */
	/* JTU likewise: the candle method cannot resolve below roughly 25 JTU, so a
	 * milli-JTU is not a measurement. FTU and FAU are ordinary instrument
	 * scales and do take prefixes. */
	assert_unit_rejected("m~JTU");
	assert_compact_matches("mFTU", "m~FTU");
	assert_compact_matches("mFAU", "m~FAU");
	/* "fau" lowercase is the femto-astronomical-unit and must stay that way. */
	{
		bool fok = false;
		value_unit_t fau_lower = bvn_parse_unit((const uint8_t *)"fau", &fok);
		ASSERT_TRUE(fok, "fau parses");
		ASSERT_EQ_INT(fau_lower.components[0].base, bu_astronomical_unit,
			      "fau is the astronomical unit");
		ASSERT_EQ_INT(fau_lower.components[0].prefix.id.si, si_femto, "…femto-");
	}

	/* CF is a conductivity, just rescaled: 1 CF = 0.1 mS/cm = 100 µS/cm. */
	{
		value_unit_t cf = bvn_parse_unit((const uint8_t *)"CF", &ok);
		value_unit_t mS_cm = bvn_parse_unit((const uint8_t *)"m~S/c~m", &ok);
		value_unit_t uS_cm = bvn_parse_unit((const uint8_t *)"\xc2\xb5S/cm", &ok);
		ASSERT_TRUE(ok, "CF and the EC compounds parse");
		ASSERT_TRUE(bvn_units_compatible(cf, mS_cm), "CF is a conductivity");
		ASSERT_TRUE(bvn_unit_convert_value(1.0, cf, mS_cm, &out) &&
			    fabs(out - 0.1) < 1e-12, "1 CF = 0.1 mS/cm");
		ASSERT_TRUE(bvn_unit_convert_value(1.0, cf, uS_cm, &out) &&
			    fabs(out - 100.0) < 1e-9, "1 CF = 100 µS/cm");
		ASSERT_TRUE(bvn_unit_convert_value(1.0, mS_cm, cf, &out) &&
			    fabs(out - 10.0) < 1e-12, "1 mS/cm = 10 CF");
		/* "cF" is the centifarad and must stay that way. */
		value_unit_t cfarad = bvn_parse_unit((const uint8_t *)"cF", &ok);
		ASSERT_TRUE(ok, "cF parses");
		ASSERT_EQ_INT(cfarad.components[0].base, bu_farad, "cF is the farad");
		ASSERT_EQ_INT(cfarad.components[0].prefix.id.si, si_centi, "…centi-");
	}
}

static void test_unit_to_string_new_units(void)
{
	printf("  bvn_unit_to_string for rad/sr...\n");
	char buf[64];
	int32_t r;
	bool ok;
	value_unit_t round;

	r = bvn_unit_to_string(BVN_UNIT_NO_PREFIX(bu_radian), buf, sizeof(buf));
	ASSERT_TRUE(r > 0, "rad to_string returns > 0");
	round = bvn_parse_unit((const uint8_t *)buf, &ok);
	ASSERT_TRUE(ok, "rad serialized string re-parses ok");
	ASSERT_EQ_INT(round.components[0].base, bu_radian, "rad round-trips to bu_radian");

	r = bvn_unit_to_string(BVN_UNIT_NO_PREFIX(bu_steradian), buf, sizeof(buf));
	ASSERT_TRUE(r > 0, "sr to_string returns > 0");
	round = bvn_parse_unit((const uint8_t *)buf, &ok);
	ASSERT_TRUE(ok, "sr serialized string re-parses ok");
	ASSERT_EQ_INT(round.components[0].base, bu_steradian, "sr round-trips to bu_steradian");
}

static void test_nonsi_enum_order(void)
{
	printf("  non-SI enum ordering and sentinel...\n");
	ASSERT_TRUE((int)bu_inch          == 45,  "bu_inch == 45");
	ASSERT_TRUE((int)bu_foot          == 46,  "bu_foot == 46");
	ASSERT_TRUE((int)bu_yard          == 47,  "bu_yard == 47");
	ASSERT_TRUE((int)bu_mile          == 48,  "bu_mile == 48");
	ASSERT_TRUE((int)bu_nautical_mile == 49,  "bu_nautical_mile == 49");
	ASSERT_TRUE((int)bu_angstrom      == 50,  "bu_angstrom == 50");
	ASSERT_TRUE((int)bu_light_year    == 51,  "bu_light_year == 51");
	ASSERT_TRUE((int)bu_parsec        == 52,  "bu_parsec == 52");
	ASSERT_TRUE((int)bu_furlong       == 53,  "bu_furlong == 53");
	ASSERT_TRUE((int)bu_fathom        == 54,  "bu_fathom == 54");
	ASSERT_TRUE((int)bu_pound         == 55,  "bu_pound == 55");
	ASSERT_TRUE((int)bu_ounce         == 56,  "bu_ounce == 56");
	ASSERT_TRUE((int)bu_grain         == 57,  "bu_grain == 57");
	ASSERT_TRUE((int)bu_stone         == 58,  "bu_stone == 58");
	ASSERT_TRUE((int)bu_short_ton     == 59,  "bu_short_ton == 59");
	ASSERT_TRUE((int)bu_long_ton      == 60,  "bu_long_ton == 60");
	ASSERT_TRUE((int)bu_troy_ounce    == 61,  "bu_troy_ounce == 61");
	ASSERT_TRUE((int)bu_carat         == 62,  "bu_carat == 62");
	ASSERT_TRUE((int)bu_fahrenheit    == 63,  "bu_fahrenheit == 63");
	ASSERT_TRUE((int)bu_atmosphere    == 64,  "bu_atmosphere == 64");
	ASSERT_TRUE((int)bu_mmhg          == 65,  "bu_mmhg == 65");
	ASSERT_TRUE((int)bu_torr          == 66,  "bu_torr == 66");
	ASSERT_TRUE((int)bu_psi           == 67,  "bu_psi == 67");
	ASSERT_TRUE((int)bu_calorie       == 68,  "bu_calorie == 68");
	ASSERT_TRUE((int)bu_btu           == 69,  "bu_btu == 69");
	ASSERT_TRUE((int)bu_erg           == 70,  "bu_erg == 70");
	ASSERT_TRUE((int)bu_therm         == 71,  "bu_therm == 71");
	ASSERT_TRUE((int)bu_horsepower    == 72,  "bu_horsepower == 72");
	ASSERT_TRUE((int)bu_pound_force   == 73,  "bu_pound_force == 73");
	ASSERT_TRUE((int)bu_dyne          == 74,  "bu_dyne == 74");
	ASSERT_TRUE((int)bu_kip           == 75,  "bu_kip == 75");
	ASSERT_TRUE((int)bu_knot          == 76,  "bu_knot == 76");
	ASSERT_TRUE((int)bu_gallon        == 77,  "bu_gallon == 77");
	ASSERT_TRUE((int)bu_gallon_uk     == 78,  "bu_gallon_uk == 78");
	ASSERT_TRUE((int)bu_quart         == 79,  "bu_quart == 79");
	ASSERT_TRUE((int)bu_pint          == 80,  "bu_pint == 80");
	ASSERT_TRUE((int)bu_cup           == 81,  "bu_cup == 81");
	ASSERT_TRUE((int)bu_fluid_ounce   == 82,  "bu_fluid_ounce == 82");
	ASSERT_TRUE((int)bu_tablespoon    == 83,  "bu_tablespoon == 83");
	ASSERT_TRUE((int)bu_teaspoon      == 84,  "bu_teaspoon == 84");
	ASSERT_TRUE((int)bu_barrel        == 85,  "bu_barrel == 85");
	ASSERT_TRUE((int)bu_acre          == 86,  "bu_acre == 86");
	ASSERT_TRUE((int)bu_barn          == 87,  "bu_barn == 87");
	ASSERT_TRUE((int)bu_arcminute     == 88,  "bu_arcminute == 88");
	ASSERT_TRUE((int)bu_arcsecond     == 89,  "bu_arcsecond == 89");
	ASSERT_TRUE((int)bu_grad          == 90,  "bu_grad == 90");
	ASSERT_TRUE((int)bu_poise         == 91,  "bu_poise == 91");
	ASSERT_TRUE((int)bu_stokes        == 92,  "bu_stokes == 92");
	ASSERT_TRUE((int)bu_gauss         == 93,  "bu_gauss == 93");
	ASSERT_TRUE((int)bu_maxwell       == 94,  "bu_maxwell == 94");
	ASSERT_TRUE((int)bu_oersted       == 95,  "bu_oersted == 95");
	ASSERT_TRUE((int)bu_stilb         == 96,  "bu_stilb == 96");
	ASSERT_TRUE((int)bu_phot          == 97,  "bu_phot == 97");
	ASSERT_TRUE((int)bu_galileo       == 98,  "bu_galileo == 98");
	ASSERT_TRUE((int)bu_curie         == 99,  "bu_curie == 99");
	ASSERT_TRUE((int)bu_roentgen      == 100, "bu_roentgen == 100");
	ASSERT_TRUE((int)bu_rem           == 101, "bu_rem == 101");
	ASSERT_TRUE((int)bu_neper         == 102, "bu_neper == 102");
	ASSERT_TRUE((int)bu_decibel       == 103, "bu_decibel == 103");
	ASSERT_TRUE((int)bu_rankine       == 104, "bu_rankine == 104");
	ASSERT_TRUE((int)bu_slug          == 105, "bu_slug == 105");
	ASSERT_TRUE((int)bu_thou          == 106, "bu_thou == 106");
	ASSERT_TRUE((int)bu_pint_uk       == 107, "bu_pint_uk == 107");
	ASSERT_TRUE((int)bu_fluid_ounce_uk == 108, "bu_fluid_ounce_uk == 108");
	ASSERT_TRUE((int)bu_quart_uk      == 109, "bu_quart_uk == 109");
	ASSERT_TRUE((int)bu_var           == 110, "bu_var == 110");
	ASSERT_TRUE((int)bu_volt_ampere   == 111, "bu_volt_ampere == 111");
	ASSERT_TRUE((int)bu_kilogram_force == 112, "bu_kilogram_force == 112");
	ASSERT_TRUE((int)bu_inch_hg       == 113, "bu_inch_hg == 113");
	ASSERT_TRUE((int)bu_rpm           == 114, "bu_rpm == 114");
	ASSERT_TRUE((int)bu_foot_pound    == 115, "bu_foot_pound == 115");
	ASSERT_TRUE((int)bu_dram         == 116, "bu_dram == 116");
	ASSERT_TRUE((int)bu_pennyweight   == 117, "bu_pennyweight == 117");
	ASSERT_TRUE((int)bu_chain        == 118, "bu_chain == 118");
	ASSERT_TRUE((int)bu_rod          == 119, "bu_rod == 119");
	ASSERT_TRUE((int)bu_gill         == 120, "bu_gill == 120");
	ASSERT_TRUE((int)bu_gill_uk      == 121, "bu_gill_uk == 121");
	ASSERT_TRUE((int)bu_standard_gravity    == 122, "bu_standard_gravity == 122");
	ASSERT_TRUE((int)bu_metric_horsepower   == 123, "bu_metric_horsepower == 123");
	ASSERT_TRUE((int)bu_revolution          == 124, "bu_revolution == 124");
	ASSERT_TRUE((int)bu_month               == 125, "bu_month == 125");
	ASSERT_TRUE((int)bu_fortnight           == 126, "bu_fortnight == 126");
	ASSERT_TRUE((int)bu_atmosphere_technical == 127, "bu_atmosphere_technical == 127");
	ASSERT_TRUE((int)bu_tex                 == 128, "bu_tex == 128");
	ASSERT_TRUE((int)bu_denier              == 129, "bu_denier == 129");
	ASSERT_TRUE((int)bu_fluid_dram          == 130, "bu_fluid_dram == 130");
	ASSERT_TRUE((int)bu_minim               == 131, "bu_minim == 131");
	ASSERT_TRUE((int)bu_peck                == 132, "bu_peck == 132");
	ASSERT_TRUE((int)bu_bushel              == 133, "bu_bushel == 133");
	ASSERT_TRUE((int)bu_pfund              == 348, "bu_pfund == 348");
	ASSERT_TRUE((int)bu_zentner            == 349, "bu_zentner == 349");
	ASSERT_TRUE((int)bu_doppelzentner      == 350, "bu_doppelzentner == 350");
	ASSERT_TRUE((int)bu_lot               == 351, "bu_lot == 351");
	ASSERT_TRUE((int)bu_prussian_line      == 352, "bu_prussian_line == 352");
	ASSERT_TRUE((int)bu_prussian_zoll      == 353, "bu_prussian_zoll == 353");
	ASSERT_TRUE((int)bu_prussian_fuss      == 354, "bu_prussian_fuss == 354");
	ASSERT_TRUE((int)bu_prussian_elle      == 355, "bu_prussian_elle == 355");
	ASSERT_TRUE((int)bu_prussian_rute      == 356, "bu_prussian_rute == 356");
	ASSERT_TRUE((int)bu_klafter            == 357, "bu_klafter == 357");
	ASSERT_TRUE((int)bu_german_mile        == 358, "bu_german_mile == 358");
	ASSERT_TRUE((int)bu_morgen             == 359, "bu_morgen == 359");
	ASSERT_TRUE((int)bu_scheffel           == 360, "bu_scheffel == 360");
	ASSERT_TRUE((int)bu_survey_foot        == 361, "bu_survey_foot == 361");
	ASSERT_TRUE((int)bu_league             == 362, "bu_league == 362");
	ASSERT_TRUE((int)bu_cable              == 363, "bu_cable == 363");
	ASSERT_TRUE((int)bu_hand               == 364, "bu_hand == 364");
	ASSERT_TRUE((int)bu_quintal            == 365, "bu_quintal == 365");
	ASSERT_TRUE((int)bu_scruple            == 366, "bu_scruple == 366");
	ASSERT_TRUE((int)bu_baud               == 367, "bu_baud == 367");
	ASSERT_TRUE((int)bu_delisle            == 368, "bu_delisle == 368");
	ASSERT_TRUE((int)bu_newton_temp        == 369, "bu_newton_temp == 369");
	ASSERT_TRUE((int)bu_reaumur            == 370, "bu_reaumur == 370");
	ASSERT_TRUE((int)bu_romer              == 371, "bu_romer == 371");
	ASSERT_TRUE((int)bu_percent            == 372, "bu_percent == 372");
	ASSERT_TRUE((int)bu_per_mille          == 373, "bu_per_mille == 373");
	ASSERT_TRUE((int)bu_per_myriad         == 374, "bu_per_myriad == 374");
	ASSERT_TRUE((int)bu_per_cent_mille     == 375, "bu_per_cent_mille == 375");
	ASSERT_TRUE((int)bu_ppm                == 376, "bu_ppm == 376");
	ASSERT_TRUE((int)bu_ppb                == 377, "bu_ppb == 377");
	/* 378..379 are the appended currencies; physical units resume at 380. */
	ASSERT_TRUE((int)bu_ph_scale           == 380, "bu_ph_scale == 380");
	ASSERT_TRUE((int)bu_mile_per_hour      == 381, "bu_mile_per_hour == 381");
	ASSERT_TRUE((int)bu_kilometer_per_hour == 382, "bu_kilometer_per_hour == 382");
	ASSERT_TRUE((int)bu_german_hardness    == 383, "bu_german_hardness == 383");
	ASSERT_TRUE((int)bu_english_hardness   == 384, "bu_english_hardness == 384");
	ASSERT_TRUE((int)bu_french_hardness    == 385, "bu_french_hardness == 385");
	ASSERT_TRUE((int)bu_russian_hardness   == 386, "bu_russian_hardness == 386");
	ASSERT_TRUE((int)bu_american_hardness  == 387, "bu_american_hardness == 387");
	ASSERT_TRUE((int)bu_val                == 388, "bu_val == 388");
	ASSERT_TRUE((int)bu_grains_per_gallon  == 389, "bu_grains_per_gallon == 389");
	ASSERT_TRUE((int)bu_turbidity_ntu      == 390, "bu_turbidity_ntu == 390");
	ASSERT_TRUE((int)bu_turbidity_fnu      == 391, "bu_turbidity_fnu == 391");
	ASSERT_TRUE((int)bu_practical_salinity == 392, "bu_practical_salinity == 392");
	ASSERT_TRUE((int)bu_conductivity_factor == 393, "bu_conductivity_factor == 393");
	ASSERT_TRUE((int)bu_turbidity_ftu       == 394, "bu_turbidity_ftu == 394");
	ASSERT_TRUE((int)bu_turbidity_fau       == 395, "bu_turbidity_fau == 395");
	ASSERT_TRUE((int)bu_turbidity_jtu       == 396, "bu_turbidity_jtu == 396");
	ASSERT_EQ_INT(BVN_VALUE_BASE_UNIT_COUNT, 397, "sentinel == 397");
}

static void test_nonsi_si_factors(void)
{
	printf("  non-SI SI conversion factors...\n");
	bool aff; double off; bool ok;

#define CHK(unit, expected, tol) do { \
	double f = bvn_unit_to_si_factor(BVN_UNIT_NO_PREFIX(unit), &aff, &off, &ok); \
	ASSERT_TRUE(ok,  #unit " factor ok"); \
	ASSERT_FALSE(aff, #unit " not affine"); \
	ASSERT_EQ_DBL(f, (expected), (tol), #unit " SI factor"); \
} while (0)

	CHK(bu_inch,          0.0254,                       1e-18);
	CHK(bu_foot,          0.3048,                       1e-18);
	CHK(bu_yard,          0.9144,                       1e-18);
	CHK(bu_mile,          1609.344,                     1e-12);
	CHK(bu_nautical_mile, 1852.0,                       1e-12);
	CHK(bu_angstrom,      1e-10,                        1e-25);
	CHK(bu_light_year,    9.4607304725808e15,           1e2);
	CHK(bu_parsec,        3.085677581491367e16,         1e2);
	CHK(bu_furlong,       201.168,                      1e-12);
	CHK(bu_fathom,        1.8288,                       1e-15);

	CHK(bu_pound,         0.45359237,                   1e-15);
	CHK(bu_ounce,         0.028349523125,               1e-18);
	CHK(bu_grain,         6.479891e-5,                  1e-20);
	CHK(bu_stone,         6.35029318,                   1e-12);
	CHK(bu_short_ton,     907.18474,                    1e-10);
	CHK(bu_long_ton,      1016.0469088,                 1e-10);
	CHK(bu_troy_ounce,    0.0311034768,                 1e-15);
	CHK(bu_carat,         2e-4,                         1e-19);

	CHK(bu_atmosphere,    101325.0,                     1e-10);
	CHK(bu_mmhg,          133.322387415,                1e-12);
	CHK(bu_torr,          101325.0/760.0,               1e-12);
	CHK(bu_psi,           6894.757293168361,            1e-9);

	CHK(bu_calorie,       4.184,                        1e-15);
	CHK(bu_btu,           1055.05585262,                1e-8);
	CHK(bu_erg,           1e-7,                         1e-22);
	CHK(bu_therm,         1.05480400e8,                 1.0);

	CHK(bu_horsepower,    745.69987158227,              1e-8);

	CHK(bu_pound_force,   4.4482216152605,              1e-13);
	CHK(bu_dyne,          1e-5,                         1e-20);
	CHK(bu_kip,           4448.2216152605,              1e-10);

	CHK(bu_knot,          1852.0/3600.0,                1e-15);

	CHK(bu_gallon,        3.785411784e-3,               1e-18);
	CHK(bu_gallon_uk,     4.54609e-3,                   1e-18);
	CHK(bu_quart,         9.46352946e-4,                1e-18);
	CHK(bu_pint,          4.73176473e-4,                1e-18);
	CHK(bu_cup,           2.365882365e-4,               1e-18);
	CHK(bu_fluid_ounce,   2.95735295625e-5,             1e-20);
	CHK(bu_tablespoon,    1.478676478125e-5,             1e-20);
	CHK(bu_teaspoon,      4.92892159375e-6,              1e-21);
	CHK(bu_barrel,        0.158987294928,               1e-15);

	CHK(bu_acre,          4046.8564224,                 1e-10);
	CHK(bu_barn,          1e-28,                        1e-43);

#define M_PI_LOCAL 3.14159265358979323846
	CHK(bu_arcminute,     M_PI_LOCAL/10800.0,           1e-18);
	CHK(bu_arcsecond,     M_PI_LOCAL/648000.0,          1e-22);
	CHK(bu_grad,          M_PI_LOCAL/200.0,             1e-18);

	CHK(bu_poise,         0.1,                          1e-18);
	CHK(bu_stokes,        1e-4,                         1e-19);
	CHK(bu_gauss,         1e-4,                         1e-19);
	CHK(bu_maxwell,       1e-8,                         1e-23);
	CHK(bu_oersted,       1000.0/(4.0*M_PI_LOCAL),      1e-12);
	CHK(bu_stilb,         1e4,                          1e-11);
	CHK(bu_phot,          1e4,                          1e-11);
	CHK(bu_galileo,       1e-2,                         1e-17);

	CHK(bu_curie,         3.7e10,                       1.0);
	CHK(bu_roentgen,      2.58e-4,                      1e-19);
	CHK(bu_rem,           1e-2,                         1e-17);
	CHK(bu_neper,         1.0,                          1e-15);
	CHK(bu_kilogram_force, 9.80665,                     1e-12);
	/* Each of these is an exact multiple of a datum, so it is written as that
	 * arithmetic rather than as a restated constant: the four below were pinned
	 * to the ROUNDED value the table used to carry, which is how a Klafter came
	 * to be 6.000006 Fuss with a test agreeing. */
	CHK(bu_inch_hg,       25.4*133.322387415,           1e-9);
	CHK(bu_rpm,           1.0/60.0,                     1e-18);
	CHK(bu_foot_pound,    0.3048*0.45359237*9.80665,    1e-15);
	CHK(bu_dram,          1.7718451953125e-3,            1e-18);
	CHK(bu_pennyweight,   1.55517384e-3,                1e-14);
	CHK(bu_chain,         20.1168,                      1e-12);
	CHK(bu_rod,           5.0292,                       1e-13);
	CHK(bu_gill,          1.18294118250e-4,             1e-19);
	CHK(bu_gill_uk,       1.420653125e-4,               1e-18);
	CHK(bu_standard_gravity,   9.80665,                 1e-10);
	CHK(bu_metric_horsepower,  735.49875,               1e-5);
	CHK(bu_revolution,    6.283185307179586,             1e-15);
	CHK(bu_month,         2629800.0,                    1e-5);
	CHK(bu_fortnight,     1209600.0,                    1e-5);
	CHK(bu_atmosphere_technical, 98066.5,               1e-1);
	CHK(bu_tex,           1e-6,                         1e-21);
	CHK(bu_denier,        1.0/9000000.0,                1e-22);
	CHK(bu_fluid_dram,    3.6966911953125e-6,           1e-21);
	CHK(bu_minim,         6.16115199218750e-8,          1e-23);
	CHK(bu_peck,          8.80976754172e-3,             1e-18);
	CHK(bu_bushel,        3.523907016688e-2,            1e-17);
	CHK(bu_pfund,         0.5,                          1e-16);
	CHK(bu_zentner,       50.0,                         1e-14);
	CHK(bu_doppelzentner, 100.0,                        1e-13);
	CHK(bu_lot,           15.625e-3,                    1e-18);
	CHK(bu_prussian_line, 3.13853e-1/144.0,              1e-19);
	CHK(bu_prussian_zoll, 3.13853e-1/12.0,              1e-18);
	CHK(bu_prussian_fuss, 3.13853e-1,                   1e-16);
	CHK(bu_prussian_elle, 6.669376250e-1,               1e-16);   /* 25.5 Zoll */
	CHK(bu_prussian_rute, 12.0*3.13853e-1,              1e-15);
	CHK(bu_klafter,       6.0*3.13853e-1,               1e-15);
	CHK(bu_german_mile,   7420.44,                      1e-12);
	CHK(bu_morgen,        180.0*(12.0*3.13853e-1)*(12.0*3.13853e-1), 1e-11);
	CHK(bu_scheffel,      54.961e-3,                    1e-18);
	CHK(bu_survey_foot,   1200.0/3937.0,               1e-16);
	CHK(bu_league,        4828.032,                    1e-12);
	CHK(bu_cable,         185.2,                       1e-12);
	CHK(bu_hand,          0.1016,                      1e-18);
	CHK(bu_quintal,       100.0,                       1e-13);
	CHK(bu_scruple,       1.2959782e-3,                1e-18);
	CHK(bu_baud,          1.0,                         1e-15);
	CHK(bu_percent,       1e-2,                        1e-17);
	CHK(bu_per_mille,     1e-3,                        1e-18);
	CHK(bu_per_myriad,    1e-4,                        1e-19);
	CHK(bu_per_cent_mille, 1e-5,                       1e-20);
	CHK(bu_ppm,           1e-6,                         1e-21);
	CHK(bu_ppb,           1e-9,                         1e-24);
#undef CHK
#undef M_PI_LOCAL

	double f = bvn_unit_to_si_factor(BVN_UNIT_NO_PREFIX(bu_fahrenheit), &aff, &off, &ok);
	ASSERT_TRUE(ok,   "fahrenheit factor ok");
	ASSERT_TRUE(aff,  "fahrenheit is affine");
	ASSERT_EQ_DBL(f,   5.0/9.0,              1e-15, "fahrenheit factor = 5/9");
	ASSERT_EQ_DBL(off, 459.67*(5.0/9.0),     1e-12, "fahrenheit offset");

	f = bvn_unit_to_si_factor(BVN_UNIT_NO_PREFIX(bu_delisle), &aff, &off, &ok);
	ASSERT_TRUE(ok,   "delisle factor ok");
	ASSERT_TRUE(aff,  "delisle is affine");
	ASSERT_EQ_DBL(f,   -2.0/3.0,             1e-15, "delisle factor = -2/3");
	ASSERT_EQ_DBL(off, 373.15,               1e-12, "delisle offset = 373.15");

	f = bvn_unit_to_si_factor(BVN_UNIT_NO_PREFIX(bu_newton_temp), &aff, &off, &ok);
	ASSERT_TRUE(ok,   "newton_temp factor ok");
	ASSERT_TRUE(aff,  "newton_temp is affine");
	ASSERT_EQ_DBL(f,   100.0/33.0,           1e-13, "newton_temp factor = 100/33");
	ASSERT_EQ_DBL(off, 273.15,               1e-12, "newton_temp offset = 273.15");

	f = bvn_unit_to_si_factor(BVN_UNIT_NO_PREFIX(bu_reaumur), &aff, &off, &ok);
	ASSERT_TRUE(ok,   "reaumur factor ok");
	ASSERT_TRUE(aff,  "reaumur is affine");
	ASSERT_EQ_DBL(f,   5.0/4.0,              1e-15, "reaumur factor = 5/4");
	ASSERT_EQ_DBL(off, 273.15,               1e-12, "reaumur offset = 273.15");

	f = bvn_unit_to_si_factor(BVN_UNIT_NO_PREFIX(bu_romer), &aff, &off, &ok);
	ASSERT_TRUE(ok,   "romer factor ok");
	ASSERT_TRUE(aff,  "romer is affine");
	ASSERT_EQ_DBL(f,   40.0/21.0,            1e-14, "romer factor = 40/21");
	ASSERT_EQ_DBL(off, 273.15 - 100.0/7.0,  1e-12, "romer offset = 273.15 - 100/7");
}

static void test_nonsi_dim_vectors(void)
{
	printf("  non-SI dimension vectors...\n");
	int32_t d[7];

#define DIM_OK(unit) \
	ASSERT_TRUE(bvn_unit_dimension_vector(BVN_UNIT_NO_PREFIX(unit), d), #unit " dimvec ok")

	DIM_OK(bu_inch);
	ASSERT_EQ_INT(d[0], 1, "inch m=1"); ASSERT_EQ_INT(d[1], 0, "inch kg=0");
	ASSERT_EQ_INT(d[2], 0, "inch s=0");

	DIM_OK(bu_pound);
	ASSERT_EQ_INT(d[0], 0, "pound m=0"); ASSERT_EQ_INT(d[1], 1, "pound kg=1");
	ASSERT_EQ_INT(d[2], 0, "pound s=0");

	DIM_OK(bu_fahrenheit);
	ASSERT_EQ_INT(d[0], 0, "degF m=0"); ASSERT_EQ_INT(d[4], 1, "degF K=1");

	DIM_OK(bu_atmosphere);
	ASSERT_EQ_INT(d[0], -1, "atm m=-1"); ASSERT_EQ_INT(d[1], 1, "atm kg=1");
	ASSERT_EQ_INT(d[2], -2, "atm s=-2");

	DIM_OK(bu_calorie);
	ASSERT_EQ_INT(d[0], 2, "cal m=2"); ASSERT_EQ_INT(d[1], 1, "cal kg=1");
	ASSERT_EQ_INT(d[2], -2, "cal s=-2");

	DIM_OK(bu_horsepower);
	ASSERT_EQ_INT(d[0], 2, "hp m=2"); ASSERT_EQ_INT(d[1], 1, "hp kg=1");
	ASSERT_EQ_INT(d[2], -3, "hp s=-3");

	DIM_OK(bu_pound_force);
	ASSERT_EQ_INT(d[0], 1, "lbf m=1"); ASSERT_EQ_INT(d[1], 1, "lbf kg=1");
	ASSERT_EQ_INT(d[2], -2, "lbf s=-2");

	DIM_OK(bu_knot);
	ASSERT_EQ_INT(d[0], 1, "kn m=1"); ASSERT_EQ_INT(d[2], -1, "kn s=-1");

	DIM_OK(bu_gallon);
	ASSERT_EQ_INT(d[0], 3, "gal m=3");
	for (int i = 1; i < 7; i++) ASSERT_EQ_INT(d[i], 0, "gal dim[i]=0");

	DIM_OK(bu_acre);
	ASSERT_EQ_INT(d[0], 2, "acre m=2");
	for (int i = 1; i < 7; i++) ASSERT_EQ_INT(d[i], 0, "acre dim[i]=0");

	DIM_OK(bu_arcminute);
	for (int i = 0; i < 7; i++) ASSERT_EQ_INT(d[i], 0, "arcmin dim[i]=0");

	DIM_OK(bu_kilogram_force);
	ASSERT_EQ_INT(d[0], 1, "kgf m=1"); ASSERT_EQ_INT(d[1], 1, "kgf kg=1");
	ASSERT_EQ_INT(d[2], -2, "kgf s=-2");

	DIM_OK(bu_inch_hg);
	ASSERT_EQ_INT(d[0], -1, "inHg m=-1"); ASSERT_EQ_INT(d[1], 1, "inHg kg=1");
	ASSERT_EQ_INT(d[2], -2, "inHg s=-2");

	DIM_OK(bu_rpm);
	ASSERT_EQ_INT(d[2], -1, "rpm s=-1");
	ASSERT_EQ_INT(d[0], 0, "rpm m=0"); ASSERT_EQ_INT(d[1], 0, "rpm kg=0");

	DIM_OK(bu_foot_pound);
	ASSERT_EQ_INT(d[0], 2, "ft_lb m=2"); ASSERT_EQ_INT(d[1], 1, "ft_lb kg=1");
	ASSERT_EQ_INT(d[2], -2, "ft_lb s=-2");

	DIM_OK(bu_dram);
	ASSERT_EQ_INT(d[1], 1, "dr kg=1");
	ASSERT_EQ_INT(d[0], 0, "dr m=0"); ASSERT_EQ_INT(d[2], 0, "dr s=0");

	DIM_OK(bu_pennyweight);
	ASSERT_EQ_INT(d[1], 1, "dwt kg=1");
	ASSERT_EQ_INT(d[0], 0, "dwt m=0"); ASSERT_EQ_INT(d[2], 0, "dwt s=0");

	DIM_OK(bu_chain);
	ASSERT_EQ_INT(d[0], 1, "ch m=1");
	for (int i = 1; i < 7; i++) ASSERT_EQ_INT(d[i], 0, "ch dim[i]=0");

	DIM_OK(bu_rod);
	ASSERT_EQ_INT(d[0], 1, "rd m=1");
	for (int i = 1; i < 7; i++) ASSERT_EQ_INT(d[i], 0, "rd dim[i]=0");

	DIM_OK(bu_gill);
	ASSERT_EQ_INT(d[0], 3, "gi m=3");
	for (int i = 1; i < 7; i++) ASSERT_EQ_INT(d[i], 0, "gi dim[i]=0");

	DIM_OK(bu_gill_uk);
	ASSERT_EQ_INT(d[0], 3, "gi_uk m=3");
	for (int i = 1; i < 7; i++) ASSERT_EQ_INT(d[i], 0, "gi_uk dim[i]=0");

	DIM_OK(bu_grad);
	for (int i = 0; i < 7; i++) ASSERT_EQ_INT(d[i], 0, "grad dim[i]=0");

	DIM_OK(bu_poise);
	ASSERT_EQ_INT(d[0], -1, "P m=-1"); ASSERT_EQ_INT(d[1], 1, "P kg=1");
	ASSERT_EQ_INT(d[2], -1, "P s=-1");

	DIM_OK(bu_stokes);
	ASSERT_EQ_INT(d[0], 2, "St m=2"); ASSERT_EQ_INT(d[2], -1, "St s=-1");

	DIM_OK(bu_gauss);
	ASSERT_EQ_INT(d[0], 0, "G m=0"); ASSERT_EQ_INT(d[1], 1, "G kg=1");
	ASSERT_EQ_INT(d[2], -2, "G s=-2"); ASSERT_EQ_INT(d[3], -1, "G A=-1");

	DIM_OK(bu_maxwell);
	ASSERT_EQ_INT(d[0], 2, "Mx m=2"); ASSERT_EQ_INT(d[1], 1, "Mx kg=1");
	ASSERT_EQ_INT(d[2], -2, "Mx s=-2"); ASSERT_EQ_INT(d[3], -1, "Mx A=-1");

	DIM_OK(bu_oersted);
	ASSERT_EQ_INT(d[0], -1, "Oe m=-1"); ASSERT_EQ_INT(d[3], 1, "Oe A=1");

	DIM_OK(bu_galileo);
	ASSERT_EQ_INT(d[0], 1, "Gal m=1"); ASSERT_EQ_INT(d[2], -2, "Gal s=-2");

	DIM_OK(bu_curie);
	ASSERT_EQ_INT(d[2], -1, "Ci s=-1");

	DIM_OK(bu_roentgen);
	ASSERT_EQ_INT(d[1], -1, "R kg=-1"); ASSERT_EQ_INT(d[2], 1, "R s=1");
	ASSERT_EQ_INT(d[3], 1,  "R A=1");

	DIM_OK(bu_rem);
	ASSERT_EQ_INT(d[0], 2, "rem m=2"); ASSERT_EQ_INT(d[2], -2, "rem s=-2");

	DIM_OK(bu_neper);
	for (int i = 0; i < 7; i++) ASSERT_EQ_INT(d[i], 0, "Np dim[i]=0");

	DIM_OK(bu_standard_gravity);
	ASSERT_EQ_INT(d[0], 1, "gn m=1"); ASSERT_EQ_INT(d[2], -2, "gn s=-2");
	ASSERT_EQ_INT(d[1], 0, "gn kg=0");

	DIM_OK(bu_metric_horsepower);
	ASSERT_EQ_INT(d[0], 2, "PS m=2"); ASSERT_EQ_INT(d[1], 1, "PS kg=1");
	ASSERT_EQ_INT(d[2], -3, "PS s=-3");

	DIM_OK(bu_revolution);
	for (int i = 0; i < 7; i++) ASSERT_EQ_INT(d[i], 0, "rev dim[i]=0");

	DIM_OK(bu_month);
	ASSERT_EQ_INT(d[2], 1, "mo s=1");
	ASSERT_EQ_INT(d[0], 0, "mo m=0"); ASSERT_EQ_INT(d[1], 0, "mo kg=0");

	DIM_OK(bu_fortnight);
	ASSERT_EQ_INT(d[2], 1, "fn s=1");
	ASSERT_EQ_INT(d[0], 0, "fn m=0"); ASSERT_EQ_INT(d[1], 0, "fn kg=0");

	DIM_OK(bu_atmosphere_technical);
	ASSERT_EQ_INT(d[0], -1, "at m=-1"); ASSERT_EQ_INT(d[1], 1, "at kg=1");
	ASSERT_EQ_INT(d[2], -2, "at s=-2");

	DIM_OK(bu_tex);
	ASSERT_EQ_INT(d[0], -1, "tex m=-1"); ASSERT_EQ_INT(d[1], 1, "tex kg=1");
	ASSERT_EQ_INT(d[2], 0, "tex s=0");

	DIM_OK(bu_denier);
	ASSERT_EQ_INT(d[0], -1, "den m=-1"); ASSERT_EQ_INT(d[1], 1, "den kg=1");
	ASSERT_EQ_INT(d[2], 0, "den s=0");

	DIM_OK(bu_fluid_dram);
	ASSERT_EQ_INT(d[0], 3, "fl_dr m=3");
	for (int i = 1; i < 7; i++) ASSERT_EQ_INT(d[i], 0, "fl_dr dim[i]=0");

	DIM_OK(bu_minim);
	ASSERT_EQ_INT(d[0], 3, "minim m=3");
	for (int i = 1; i < 7; i++) ASSERT_EQ_INT(d[i], 0, "minim dim[i]=0");

	DIM_OK(bu_peck);
	ASSERT_EQ_INT(d[0], 3, "pk m=3");
	for (int i = 1; i < 7; i++) ASSERT_EQ_INT(d[i], 0, "pk dim[i]=0");

	DIM_OK(bu_bushel);
	ASSERT_EQ_INT(d[0], 3, "bsh m=3");
	for (int i = 1; i < 7; i++) ASSERT_EQ_INT(d[i], 0, "bsh dim[i]=0");

	DIM_OK(bu_pfund);
	ASSERT_EQ_INT(d[1], 1, "Pfd kg=1");
	ASSERT_EQ_INT(d[0], 0, "Pfd m=0"); ASSERT_EQ_INT(d[2], 0, "Pfd s=0");

	DIM_OK(bu_zentner);
	ASSERT_EQ_INT(d[1], 1, "Ztr kg=1");
	ASSERT_EQ_INT(d[0], 0, "Ztr m=0"); ASSERT_EQ_INT(d[2], 0, "Ztr s=0");

	DIM_OK(bu_doppelzentner);
	ASSERT_EQ_INT(d[1], 1, "dz kg=1");
	ASSERT_EQ_INT(d[0], 0, "dz m=0"); ASSERT_EQ_INT(d[2], 0, "dz s=0");

	DIM_OK(bu_lot);
	ASSERT_EQ_INT(d[1], 1, "lot kg=1");
	ASSERT_EQ_INT(d[0], 0, "lot m=0"); ASSERT_EQ_INT(d[2], 0, "lot s=0");

	DIM_OK(bu_prussian_line);
	ASSERT_EQ_INT(d[0], 1, "prln m=1");
	for (int i = 1; i < 7; i++) ASSERT_EQ_INT(d[i], 0, "prln dim[i]=0");

	DIM_OK(bu_prussian_zoll);
	ASSERT_EQ_INT(d[0], 1, "prz m=1");
	for (int i = 1; i < 7; i++) ASSERT_EQ_INT(d[i], 0, "prz dim[i]=0");

	DIM_OK(bu_prussian_fuss);
	ASSERT_EQ_INT(d[0], 1, "prf m=1");
	for (int i = 1; i < 7; i++) ASSERT_EQ_INT(d[i], 0, "prf dim[i]=0");

	DIM_OK(bu_prussian_elle);
	ASSERT_EQ_INT(d[0], 1, "elle m=1");
	for (int i = 1; i < 7; i++) ASSERT_EQ_INT(d[i], 0, "elle dim[i]=0");

	DIM_OK(bu_prussian_rute);
	ASSERT_EQ_INT(d[0], 1, "rute m=1");
	for (int i = 1; i < 7; i++) ASSERT_EQ_INT(d[i], 0, "rute dim[i]=0");

	DIM_OK(bu_klafter);
	ASSERT_EQ_INT(d[0], 1, "klafter m=1");
	for (int i = 1; i < 7; i++) ASSERT_EQ_INT(d[i], 0, "klafter dim[i]=0");

	DIM_OK(bu_german_mile);
	ASSERT_EQ_INT(d[0], 1, "dt_mi m=1");
	for (int i = 1; i < 7; i++) ASSERT_EQ_INT(d[i], 0, "dt_mi dim[i]=0");

	DIM_OK(bu_morgen);
	ASSERT_EQ_INT(d[0], 2, "morgen m=2");
	for (int i = 1; i < 7; i++) ASSERT_EQ_INT(d[i], 0, "morgen dim[i]=0");

	DIM_OK(bu_scheffel);
	ASSERT_EQ_INT(d[0], 3, "schffl m=3");
	for (int i = 1; i < 7; i++) ASSERT_EQ_INT(d[i], 0, "schffl dim[i]=0");

	DIM_OK(bu_delisle);
	ASSERT_EQ_INT(d[4], 1, "De K=1");
	ASSERT_EQ_INT(d[0], 0, "De m=0"); ASSERT_EQ_INT(d[1], 0, "De kg=0");

	DIM_OK(bu_newton_temp);
	ASSERT_EQ_INT(d[4], 1, "degN K=1");
	ASSERT_EQ_INT(d[0], 0, "degN m=0"); ASSERT_EQ_INT(d[1], 0, "degN kg=0");

	DIM_OK(bu_reaumur);
	ASSERT_EQ_INT(d[4], 1, "Re K=1");
	ASSERT_EQ_INT(d[0], 0, "Re m=0"); ASSERT_EQ_INT(d[1], 0, "Re kg=0");

	DIM_OK(bu_romer);
	ASSERT_EQ_INT(d[4], 1, "Ro K=1");
	ASSERT_EQ_INT(d[0], 0, "Ro m=0"); ASSERT_EQ_INT(d[1], 0, "Ro kg=0");

	DIM_OK(bu_percent);
	for (int i = 0; i < 7; i++) ASSERT_EQ_INT(d[i], 0, "% dim[i]=0");
	DIM_OK(bu_per_mille);
	for (int i = 0; i < 7; i++) ASSERT_EQ_INT(d[i], 0, "permille dim[i]=0");
	DIM_OK(bu_per_myriad);
	for (int i = 0; i < 7; i++) ASSERT_EQ_INT(d[i], 0, "permyriad dim[i]=0");
	DIM_OK(bu_per_cent_mille);
	for (int i = 0; i < 7; i++) ASSERT_EQ_INT(d[i], 0, "pcm dim[i]=0");
	DIM_OK(bu_ppm);
	for (int i = 0; i < 7; i++) ASSERT_EQ_INT(d[i], 0, "ppm dim[i]=0");
	DIM_OK(bu_ppb);
	for (int i = 0; i < 7; i++) ASSERT_EQ_INT(d[i], 0, "ppb dim[i]=0");

#undef DIM_OK
}

static void test_nonsi_parse_canonical(void)
{
	printf("  parse canonical non-SI symbols...\n");
	bool ok;
	value_unit_t u;

	struct { const char *sym; value_base_unit_t exp; } cases[] = {
		{ "in",          bu_inch          },
		{ "ft",          bu_foot          },
		{ "yd",          bu_yard          },
		{ "mi",          bu_mile          },
		{ "nmi",         bu_nautical_mile },
		{ "\xe2\x84\xab", bu_angstrom    },
		{ "ly",          bu_light_year    },
		{ "pc",          bu_parsec        },
		{ "fur",         bu_furlong       },
		{ "fath",        bu_fathom        },
		{ "lb",          bu_pound         },
		{ "oz",          bu_ounce         },
		{ "gr",          bu_grain         },
		{ "st",          bu_stone         },
		{ "tn_sh",       bu_short_ton     },
		{ "tn_l",        bu_long_ton      },
		{ "oz_t",        bu_troy_ounce    },
		{ "ct",          bu_carat         },
		{ "\xc2\xb0""F", bu_fahrenheit   },
		{ "atm",         bu_atmosphere    },
		{ "mmHg",        bu_mmhg          },
		{ "Torr",        bu_torr          },
		{ "psi",         bu_psi           },
		{ "cal",         bu_calorie       },
		{ "Btu",         bu_btu           },
		{ "erg",         bu_erg           },
		{ "thm",         bu_therm         },
		{ "hp",          bu_horsepower    },
		{ "lbf",         bu_pound_force   },
		{ "dyn",         bu_dyne          },
		{ "kip",         bu_kip           },
		{ "kn",          bu_knot          },
		{ "gal",         bu_gallon        },
		{ "gal_uk",      bu_gallon_uk     },
		{ "qt",          bu_quart         },
		{ "pt",          bu_pint          },
		{ "cup",         bu_cup           },
		{ "fl_oz",       bu_fluid_ounce   },
		{ "tbsp",        bu_tablespoon    },
		{ "tsp",         bu_teaspoon      },
		{ "bbl",         bu_barrel        },
		{ "ac",          bu_acre          },
		{ "barn",        bu_barn          },
		{ "arcmin",      bu_arcminute     },
		{ "arcsec",      bu_arcsecond     },
		{ "grad",        bu_grad          },
		{ "P",           bu_poise         },
		{ "St",          bu_stokes        },
		{ "G",           bu_gauss         },
		{ "Mx",          bu_maxwell       },
		{ "Oe",          bu_oersted       },
		{ "sb",          bu_stilb         },
		{ "ph",          bu_phot          },
		{ "Gal",         bu_galileo       },
		{ "Ci",          bu_curie         },
		{ "R",           bu_roentgen      },
		{ "rem",         bu_rem           },
		{ "Np",          bu_neper         },
		{ "kgf",         bu_kilogram_force },
		{ "inHg",        bu_inch_hg       },
		{ "rpm",         bu_rpm           },
		{ "ft_lb",       bu_foot_pound    },
		{ "dr",          bu_dram          },
		{ "dwt",         bu_pennyweight   },
		{ "ch",          bu_chain         },
		{ "rd",          bu_rod           },
		{ "gi",          bu_gill          },
		{ "gi_uk",       bu_gill_uk       },
		{ "gn",          bu_standard_gravity      },
		{ "PS",          bu_metric_horsepower     },
		{ "rev",         bu_revolution            },
		{ "mo",          bu_month                 },
		{ "fn",          bu_fortnight             },
		{ "at",          bu_atmosphere_technical  },
		{ "tex",         bu_tex                   },
		{ "den",         bu_denier                },
		{ "fl_dr",       bu_fluid_dram            },
		{ "minim",       bu_minim                 },
		{ "pk",          bu_peck                  },
		{ "bsh",         bu_bushel                },
		{ "Pfd",         bu_pfund                 },
		{ "Ztr",         bu_zentner               },
		{ "dz",          bu_doppelzentner         },
		{ "lot",         bu_lot                   },
		{ "prln",        bu_prussian_line         },
		{ "prz",         bu_prussian_zoll         },
		{ "prf",         bu_prussian_fuss         },
		{ "elle",        bu_prussian_elle         },
		{ "rute",        bu_prussian_rute         },
		{ "klafter",     bu_klafter               },
		{ "dt_mi",       bu_german_mile           },
		{ "morgen",      bu_morgen                },
		{ "schffl",      bu_scheffel              },
		{ "\xc2\xb0""De", bu_delisle             },
		{ "\xc2\xb0""N",  bu_newton_temp         },
		{ "\xc2\xb0""Re", bu_reaumur             },
		{ "\xc2\xb0""Ro", bu_romer               },
		{ "%",           bu_percent              },
		{ "\xe2\x80\xb0", bu_per_mille           },
		{ "\xe2\x80\xb1", bu_per_myriad          },
		{ "pcm",         bu_per_cent_mille       },
		{ "ppm",         bu_ppm                  },
		{ "ppb",         bu_ppb                  },
	};
	size_t n = sizeof(cases) / sizeof(cases[0]);
	for (size_t i = 0; i < n; i++) {
		u = bvn_parse_unit((const uint8_t *)cases[i].sym, &ok);
		char msg[128];
		snprintf(msg, sizeof(msg), "parse '%s' ok", cases[i].sym);
		ASSERT_TRUE(ok, msg);
		snprintf(msg, sizeof(msg), "'%s' → correct base", cases[i].sym);
		ASSERT_EQ_INT(u.components[0].base, (int64_t)cases[i].exp, msg);
	}
}

static void test_nonsi_parse_aliases(void)
{
	printf("  parse non-SI long-name aliases...\n");
	bool ok;
	value_unit_t u;

	struct { const char *s; value_base_unit_t exp; } cases[] = {
		{ "inch",          bu_inch          },
		{ "inches",        bu_inch          },
		{ "foot",          bu_foot          },
		{ "feet",          bu_foot          },
		{ "yard",          bu_yard          },
		{ "yards",         bu_yard          },
		{ "mile",          bu_mile          },
		{ "miles",         bu_mile          },
		{ "nautical_mile", bu_nautical_mile },
		{ "nautical_miles",bu_nautical_mile },
		{ "angstrom",      bu_angstrom      },
		{ "angstroms",     bu_angstrom      },
		{ "\xc3\x85",      bu_angstrom      },
		{ "light_year",    bu_light_year    },
		{ "light_years",   bu_light_year    },
		{ "parsec",        bu_parsec        },
		{ "parsecs",       bu_parsec        },
		{ "furlong",       bu_furlong       },
		{ "furlongs",      bu_furlong       },
		{ "fathom",        bu_fathom        },
		{ "fathoms",       bu_fathom        },
		{ "lbs",           bu_pound         },
		{ "pound",         bu_pound         },
		{ "pounds",        bu_pound         },
		{ "ounce",         bu_ounce         },
		{ "ounces",        bu_ounce         },
		{ "grain",         bu_grain         },
		{ "grains",        bu_grain         },
		{ "stone",         bu_stone         },
		{ "stones",        bu_stone         },
		{ "short_ton",     bu_short_ton     },
		{ "short_tons",    bu_short_ton     },
		{ "long_ton",      bu_long_ton      },
		{ "long_tons",     bu_long_ton      },
		{ "troy_ounce",    bu_troy_ounce    },
		{ "troy_ounces",   bu_troy_ounce    },
		{ "carat",         bu_carat         },
		{ "carats",        bu_carat         },
		{ "degF",          bu_fahrenheit    },
		{ "degrF",         bu_fahrenheit    },
		{ "fahrenheit",    bu_fahrenheit    },
		{ "atmosphere",    bu_atmosphere    },
		{ "atmospheres",   bu_atmosphere    },
		{ "torr",          bu_torr          },
		{ "calorie",       bu_calorie       },
		{ "calories",      bu_calorie       },
		{ "BTU",           bu_btu           },
		{ "btu",           bu_btu           },
		{ "ergs",          bu_erg           },
		{ "therm",         bu_therm         },
		{ "therms",        bu_therm         },
		{ "horsepower",    bu_horsepower    },
		{ "pound_force",   bu_pound_force   },
		{ "dyne",          bu_dyne          },
		{ "dynes",         bu_dyne          },
		{ "kips",          bu_kip           },
		{ "knot",          bu_knot          },
		{ "knots",         bu_knot          },
		{ "gallon",        bu_gallon        },
		{ "gallons",       bu_gallon        },
		{ "gallon_uk",     bu_gallon_uk     },
		{ "gallons_uk",    bu_gallon_uk     },
		{ "quart",         bu_quart         },
		{ "quarts",        bu_quart         },
		{ "pint",          bu_pint          },
		{ "pints",         bu_pint          },
		{ "cups",          bu_cup           },
		{ "fluid_ounce",   bu_fluid_ounce   },
		{ "fluid_ounces",  bu_fluid_ounce   },
		{ "tablespoon",    bu_tablespoon    },
		{ "tablespoons",   bu_tablespoon    },
		{ "teaspoon",      bu_teaspoon      },
		{ "teaspoons",     bu_teaspoon      },
		{ "barrel",        bu_barrel        },
		{ "barrels",       bu_barrel        },
		{ "acre",          bu_acre          },
		{ "acres",         bu_acre          },
		{ "barns",         bu_barn          },
		{ "arcminute",     bu_arcminute     },
		{ "arcminutes",    bu_arcminute     },
		{ "arcsecond",     bu_arcsecond     },
		{ "arcseconds",    bu_arcsecond     },
		{ "gradian",       bu_grad          },
		{ "gradians",      bu_grad          },
		{ "gon",           bu_grad          },
		{ "poise",         bu_poise         },
		{ "poises",        bu_poise         },
		{ "stokes",        bu_stokes        },
		{ "stoke",         bu_stokes        },
		{ "gauss",         bu_gauss         },
		{ "maxwell",       bu_maxwell       },
		{ "maxwells",      bu_maxwell       },
		{ "oersted",       bu_oersted       },
		{ "oersteds",      bu_oersted       },
		{ "stilb",         bu_stilb         },
		{ "stilbs",        bu_stilb         },
		{ "phot",          bu_phot          },
		{ "phots",         bu_phot          },
		{ "galileo",       bu_galileo       },
		{ "galileos",      bu_galileo       },
		{ "curie",         bu_curie         },
		{ "curies",        bu_curie         },
		{ "roentgen",      bu_roentgen      },
		{ "roentgens",     bu_roentgen      },
		{ "rems",          bu_rem           },
		{ "neper",         bu_neper         },
		{ "nepers",        bu_neper         },
		{ "kilogram_force", bu_kilogram_force },
		{ "inch_hg",       bu_inch_hg       },
		{ "inch_mercury",  bu_inch_hg       },
		{ "foot_pound",    bu_foot_pound    },
		{ "foot_pounds",   bu_foot_pound    },
		{ "dram",          bu_dram          },
		{ "drams",         bu_dram          },
		{ "pennyweight",   bu_pennyweight   },
		{ "pennyweights",  bu_pennyweight   },
		{ "chain",         bu_chain         },
		{ "chains",        bu_chain         },
		{ "rod",           bu_rod           },
		{ "rods",          bu_rod           },
		{ "gill",          bu_gill          },
		{ "gills",         bu_gill          },
		{ "gill_uk",       bu_gill_uk       },
		{ "gills_uk",      bu_gill_uk       },
		{ "standard_gravity",      bu_standard_gravity     },
		{ "CV",                    bu_metric_horsepower    },
		{ "metric_horsepower",     bu_metric_horsepower    },
		{ "revolution",            bu_revolution           },
		{ "revolutions",           bu_revolution           },
		{ "turn",                  bu_revolution           },
		{ "turns",                 bu_revolution           },
		{ "month",                 bu_month                },
		{ "months",                bu_month                },
		{ "fortnight",             bu_fortnight            },
		{ "fortnights",            bu_fortnight            },
		{ "atmosphere_technical",  bu_atmosphere_technical },
		{ "denier",                bu_denier               },
		{ "deniers",               bu_denier               },
		{ "fluid_dram",            bu_fluid_dram           },
		{ "fluid_drams",           bu_fluid_dram           },
		{ "fl_drams",              bu_fluid_dram           },
		{ "minims",                bu_minim                },
		{ "peck",                  bu_peck                 },
		{ "pecks",                 bu_peck                 },
		{ "bushel",                bu_bushel               },
		{ "bushels",               bu_bushel               },
		{ "pfund",                 bu_pfund                },
		{ "pfunds",                bu_pfund                },
		{ "zentner",               bu_zentner              },
		{ "doppelzentner",         bu_doppelzentner        },
		{ "lots",                  bu_lot                  },
		{ "prussian_line",         bu_prussian_line        },
		{ "linie",                 bu_prussian_line        },
		{ "prussian_zoll",         bu_prussian_zoll        },
		{ "zoll",                  bu_prussian_zoll        },
		{ "prussian_fuss",         bu_prussian_fuss        },
		{ "preussischer_fuss",     bu_prussian_fuss        },
		{ "prussian_elle",         bu_prussian_elle        },
		{ "preussische_elle",      bu_prussian_elle        },
		{ "prussian_rute",         bu_prussian_rute        },
		{ "preussische_rute",      bu_prussian_rute        },
		{ "prussian_klafter",      bu_klafter              },
		{ "deutsche_meile",        bu_german_mile          },
		{ "german_mile",           bu_german_mile          },
		{ "prussian_morgen",       bu_morgen               },
		{ "scheffel",              bu_scheffel             },
		{ "prussian_scheffel",     bu_scheffel             },
		{ "delisle",               bu_delisle              },
		{ "degDe",                 bu_delisle              },
		{ "degreeDe",              bu_delisle              },
		{ "newton_temperature",    bu_newton_temp          },
		{ "degN",                  bu_newton_temp          },
		{ "degreeN",               bu_newton_temp          },
		{ "reaumur",               bu_reaumur              },
		{ "degRe",                 bu_reaumur              },
		{ "degreeRe",              bu_reaumur              },
		{ "romer",                 bu_romer                },
		{ "degRo",                 bu_romer                },
		{ "degreeRo",              bu_romer                },
		{ "percent",               bu_percent              },
		{ "per_mille",             bu_per_mille            },
		{ "per_myriad",            bu_per_myriad           },
		{ "per_cent_mille",        bu_per_cent_mille       },
	};
	size_t n = sizeof(cases) / sizeof(cases[0]);
	for (size_t i = 0; i < n; i++) {
		u = bvn_parse_unit((const uint8_t *)cases[i].s, &ok);
		char msg[128];
		snprintf(msg, sizeof(msg), "parse alias '%s' ok", cases[i].s);
		ASSERT_TRUE(ok, msg);
		snprintf(msg, sizeof(msg), "alias '%s' → correct base", cases[i].s);
		ASSERT_EQ_INT(u.components[0].base, (int64_t)cases[i].exp, msg);
	}
}

static void test_nonsi_roundtrip(void)
{
	printf("  non-SI serialize → parse round-trips...\n");
	char buf[64];
	bool ok;
	value_unit_t rt;

	value_base_unit_t units[] = {
		bu_inch, bu_foot, bu_yard, bu_mile, bu_nautical_mile,
		bu_angstrom, bu_light_year, bu_parsec, bu_furlong, bu_fathom,
		bu_pound, bu_ounce, bu_grain, bu_stone, bu_short_ton,
		bu_long_ton, bu_troy_ounce, bu_carat,
		bu_fahrenheit,
		bu_atmosphere, bu_mmhg, bu_torr, bu_psi,
		bu_calorie, bu_btu, bu_erg, bu_therm,
		bu_horsepower,
		bu_pound_force, bu_dyne, bu_kip,
		bu_knot,
		bu_gallon, bu_gallon_uk, bu_quart, bu_pint, bu_cup,
		bu_fluid_ounce, bu_tablespoon, bu_teaspoon, bu_barrel,
		bu_acre, bu_barn,
		bu_arcminute, bu_arcsecond, bu_grad,
		bu_poise, bu_stokes, bu_gauss, bu_maxwell, bu_oersted,
		bu_stilb, bu_phot, bu_galileo,
		bu_curie, bu_roentgen, bu_rem, bu_neper,
		bu_kilogram_force, bu_inch_hg, bu_rpm, bu_foot_pound,
		bu_dram, bu_pennyweight, bu_chain, bu_rod, bu_gill, bu_gill_uk,
		bu_standard_gravity, bu_metric_horsepower, bu_revolution,
		bu_month, bu_fortnight, bu_atmosphere_technical,
		bu_tex, bu_denier, bu_fluid_dram, bu_minim, bu_peck, bu_bushel,
		bu_pfund, bu_zentner, bu_doppelzentner, bu_lot,
		bu_prussian_line, bu_prussian_zoll, bu_prussian_fuss, bu_prussian_elle,
		bu_prussian_rute, bu_klafter, bu_german_mile, bu_morgen, bu_scheffel,
		bu_rankine, bu_delisle, bu_newton_temp, bu_reaumur, bu_romer,
		bu_percent, bu_per_mille, bu_per_myriad,
		bu_per_cent_mille, bu_ppm, bu_ppb,
	};
	size_t n = sizeof(units) / sizeof(units[0]);
	for (size_t i = 0; i < n; i++) {
		int32_t r = bvn_unit_to_string(BVN_UNIT_NO_PREFIX(units[i]), buf, sizeof(buf));
		char msg[128];
		snprintf(msg, sizeof(msg), "unit %d to_string > 0", (int)units[i]);
		ASSERT_TRUE(r > 0, msg);
		rt = bvn_parse_unit((const uint8_t *)buf, &ok);
		snprintf(msg, sizeof(msg), "unit %d re-parses ok (sym='%s')", (int)units[i], buf);
		ASSERT_TRUE(ok, msg);
		snprintf(msg, sizeof(msg), "unit %d round-trips correctly", (int)units[i]);
		ASSERT_EQ_INT(rt.components[0].base, (int64_t)units[i], msg);
	}
}

static void test_nonsi_prefix_tilde_disambiguation(void)
{
	printf("  prefix~tilde disambiguation for ambiguous single-char symbols...\n");
	bool ok;
	value_unit_t u;

	u = bvn_parse_unit((const uint8_t *)"G", &ok);
	ASSERT_TRUE(ok, "bare G → gauss ok");
	ASSERT_EQ_INT(u.components[0].base, bu_gauss, "bare G → bu_gauss");

	u = bvn_parse_unit((const uint8_t *)"G~m", &ok);
	ASSERT_TRUE(ok, "G~m → giga-meter ok");
	ASSERT_EQ_INT(u.components[0].base, bu_meter, "G~m base → bu_meter");

	u = bvn_parse_unit((const uint8_t *)"P", &ok);
	ASSERT_TRUE(ok, "bare P → poise ok");
	ASSERT_EQ_INT(u.components[0].base, bu_poise, "bare P → bu_poise");

	u = bvn_parse_unit((const uint8_t *)"P~m", &ok);
	ASSERT_TRUE(ok, "P~m → peta-meter ok");
	ASSERT_EQ_INT(u.components[0].base, bu_meter, "P~m base → bu_meter");

	u = bvn_parse_unit((const uint8_t *)"R", &ok);
	ASSERT_TRUE(ok, "bare R → roentgen ok");
	ASSERT_EQ_INT(u.components[0].base, bu_roentgen, "bare R → bu_roentgen");

	u = bvn_parse_unit((const uint8_t *)"R~m", &ok);
	ASSERT_TRUE(ok, "R~m → ronna-meter ok");
	ASSERT_EQ_INT(u.components[0].base, bu_meter, "R~m base → bu_meter");

	u = bvn_parse_unit((const uint8_t *)"Gal", &ok);
	ASSERT_TRUE(ok, "Gal → galileo ok");
	ASSERT_EQ_INT(u.components[0].base, bu_galileo, "Gal → bu_galileo");

	u = bvn_parse_unit((const uint8_t *)"St", &ok);
	ASSERT_TRUE(ok, "bare St → stokes ok");
	ASSERT_EQ_INT(u.components[0].base, bu_stokes, "bare St → bu_stokes");
}

static void test_nonsi_compound_units(void)
{
	printf("  non-SI units in compound expressions...\n");
	bool ok;
	value_unit_t u;

	u = bvn_parse_unit((const uint8_t *)"lb/in²", &ok);
	ASSERT_TRUE(ok, "lb/in² ok");
	ASSERT_EQ_INT(u.num_components, 2, "lb/in² has 2 components");
	ASSERT_EQ_INT(u.components[0].base, bu_pound, "lb/in² num → pound");

	u = bvn_parse_unit((const uint8_t *)"ft/s", &ok);
	ASSERT_TRUE(ok, "ft/s ok");
	ASSERT_EQ_INT(u.num_components, 2, "ft/s 2 components");
	ASSERT_EQ_INT(u.components[0].base, bu_foot, "ft/s → foot");

	u = bvn_parse_unit((const uint8_t *)"Btu/h", &ok);
	ASSERT_TRUE(ok, "Btu/h ok");
	ASSERT_EQ_INT(u.num_components, 2, "Btu/h 2 components");
	ASSERT_EQ_INT(u.components[0].base, bu_btu, "Btu/h num → btu");

	u = bvn_parse_unit((const uint8_t *)"hp·h", &ok);
	ASSERT_TRUE(ok, "hp·h ok");
	ASSERT_EQ_INT(u.num_components, 2, "hp·h 2 components");
	ASSERT_EQ_INT(u.components[0].base, bu_horsepower, "hp·h[0] → horsepower");
	ASSERT_EQ_INT(u.components[1].base, bu_hour,       "hp·h[1] → hour");
}

static void test_german_unit_prefix_restriction(void)
{
	printf("  German unit prefix restrictions...\n");
#define PFX_SI_G(p)  ((value_unit_prefix_t){prefix_si,  .id.si  = (p)})
#define PFX_IEC_G(p) ((value_unit_prefix_t){prefix_iec, .id.iec = (p)})
	value_base_unit_t german_units[] = {
		bu_pfund, bu_zentner, bu_doppelzentner, bu_lot,
		bu_prussian_line, bu_prussian_zoll, bu_prussian_fuss, bu_prussian_elle,
		bu_prussian_rute, bu_klafter, bu_german_mile, bu_morgen, bu_scheffel,
	};
	size_t n = sizeof(german_units) / sizeof(german_units[0]);
	for (size_t i = 0; i < n; i++) {
		value_base_unit_t bu = german_units[i];
		char msg[128];
		snprintf(msg, sizeof(msg), "german unit %d accepts si_none", (int)bu);
		ASSERT_TRUE(bvn_prefix_unit_valid(PFX_SI_G(si_none), bu), msg);
		snprintf(msg, sizeof(msg), "german unit %d accepts iec_none", (int)bu);
		ASSERT_TRUE(bvn_prefix_unit_valid(PFX_IEC_G(iec_none), bu), msg);
		snprintf(msg, sizeof(msg), "german unit %d rejects si_kilo", (int)bu);
		ASSERT_FALSE(bvn_prefix_unit_valid(PFX_SI_G(si_kilo), bu), msg);
		snprintf(msg, sizeof(msg), "german unit %d rejects iec_mebi", (int)bu);
		ASSERT_FALSE(bvn_prefix_unit_valid(PFX_IEC_G(iec_mebi), bu), msg);
	}
#undef PFX_SI_G
#undef PFX_IEC_G
}

static void test_longest_unit_fits_the_declared_bound(void)
{
	printf("  the longest emitted unit fits BVNR_UNIT_STRING_MAX...\n");
	/* bvn_unit_to_string returns -1 when the text does not fit AND writes no
	 * NUL, so a caller that ignores the return and hands the buffer to "%s"
	 * reads past it -- which is exactly what `bovnar events -d` did with a
	 * 128-byte buffer, a stack-buffer-overflow reachable from a legal document.
	 * Every unit buffer is now sized from BVNR_UNIT_STRING_MAX, and gen_units.py
	 * fails the build if the table outgrows it. This pins the worst case the
	 * generator computes: eight components, the longest prefixable symbol, a
	 * two-byte prefix and an all-negative exponent set (which is written as a
	 * flat product, the one shape that renders "\u207b\u2079" at full width). */
	value_unit_t u;
	memset(&u, 0, sizeof u);
	u.num_components = BVNR_MAX_UNIT_COMPONENTS;
	for (uint32_t i = 0; i < BVNR_MAX_UNIT_COMPONENTS; i++) {
		u.components[i].base            = bu_fluid_ounce_uk;   /* "fl_oz_uk" */
		u.components[i].exponent        = exp_neg_nonic;
		u.components[i].prefix.system   = prefix_si;
		u.components[i].prefix.id.si    = si_micro;            /* two-byte "\u00b5" */
	}
	char buf[BVNR_UNIT_STRING_MAX];
	int32_t n = bvn_unit_to_string(u, buf, sizeof buf);
	ASSERT_TRUE(n > 0, "the worst-case unit fits BVNR_UNIT_STRING_MAX");
	ASSERT_TRUE((size_t)n + 1u <= (size_t)BVNR_UNIT_STRING_MAX,
		    "...with the NUL inside the bound");
	ASSERT_EQ_INT(n, 150, "and it is the 150 bytes gen_units.py computes");
	/* One byte short must REFUSE, not truncate -- and must not be mistaken for
	 * success by a caller that only checks for a non-empty buffer. */
	char tight[150];
	ASSERT_TRUE(bvn_unit_to_string(u, tight, sizeof tight) < 0,
		    "a buffer one byte short is refused");
	/* and it round-trips */
	bool pok = false;
	value_unit_t back = bvn_parse_unit((const uint8_t *)buf, &pok);
	ASSERT_TRUE(pok && bvn_unit_equal(u, back),
		    "the worst-case unit parses back to itself");
}

int main(void)
{
	printf("══════════════════════════════════════\n");
	printf("  Bovnar Unit Extension Test Suite\n");
	printf("══════════════════════════════════════\n\n");

	test_bu_index_selfcheck();
	test_new_units_in_enum();
	test_radian_steradian_si_factor();
	test_radian_steradian_dim_vector();
	test_parse_new_symbols();
	test_parse_long_name_aliases();
	test_prefix_unit_valid_function();
	test_prefix_enforcement_via_parse();
	test_alias_with_prefix();
	test_compact_prefix_equivalence();
	test_compact_prefix_precedence();
	test_compact_prefix_exceptions();
	test_compact_currency_form();
	test_water_hardness_units();
	test_water_quality_scales();
	test_unit_to_string_new_units();

	printf("\n--- non-SI unit tests ---\n");
	test_nonsi_enum_order();
	test_nonsi_si_factors();
	test_nonsi_dim_vectors();
	test_nonsi_parse_canonical();
	test_nonsi_parse_aliases();
	test_nonsi_roundtrip();
	test_nonsi_prefix_tilde_disambiguation();
	test_nonsi_compound_units();
	test_german_unit_prefix_restriction();
	test_longest_unit_fits_the_declared_bound();

	printf("\n──────────────────────────────────────\n");
	printf("  Results: %d tests, %d failures\n", tests, failures);
	printf("──────────────────────────────────────\n");
	return failures ? 1 : 0;
}
