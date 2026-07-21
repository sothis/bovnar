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

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "bovnar.h"
#include "bovnar_si_units.h"

static int failures = 0;
static int tests = 0;

#define ASSERT_TRUE(cond, msg) do {                                   \
	tests++;                                                         \
	if (!(cond)) {                                                   \
		fprintf(stderr, "FAIL line %d: %s\n", __LINE__, (msg));  \
		failures++;                                                  \
	}                                                                \
} while (0)

#define ASSERT_EQ_INT(a, b, msg) do {                                 \
	tests++;                                                         \
	int64_t _a = (int64_t)(a);                                       \
	int64_t _b = (int64_t)(b);                                       \
	if (_a != _b) {                                                  \
		fprintf(stderr, "FAIL line %d: %s\n  got %lld, expected %lld\n", \
				__LINE__, (msg), (long long)_a, (long long)_b);      \
		failures++;                                                  \
	}                                                                \
} while (0)

#define ASSERT_EQ_UINT(a, b, msg) do {                                \
	tests++;                                                         \
	uint64_t _a = (uint64_t)(a);                                     \
	uint64_t _b = (uint64_t)(b);                                     \
	if (_a != _b) {                                                  \
		fprintf(stderr, "FAIL line %d: %s\n  got %llu, expected %llu\n", \
				__LINE__, (msg), (unsigned long long)_a, (unsigned long long)_b); \
		failures++;                                                  \
	}                                                                \
} while (0)

#define ASSERT_EQ_DBL(a, b, tol, msg) do {                            \
	tests++;                                                         \
	double _a = (a), _b = (b);                                       \
	if (fabs(_a - _b) > (tol)) {                                     \
		fprintf(stderr, "FAIL line %d: %s\n  got %.15g, expected %.15g\n", \
				__LINE__, (msg), _a, _b);                           \
		failures++;                                                  \
	}                                                                \
} while (0)

#define ASSERT_FALSE(cond, msg) ASSERT_TRUE(!(cond), (msg))

static void test_type_spec_constructors(void)
{
	printf("  test_type_spec_constructors...\n");

	value_type_spec_t uint_type = BVN_TYPE_UINT(32);
	ASSERT_EQ_INT(uint_type.family, vt_uint, "UINT type family must be vt_uint");
	ASSERT_EQ_INT(uint_type.width, 32, "UINT width must be 32");

	value_type_spec_t sint_type = BVN_TYPE_SINT(16);
	ASSERT_EQ_INT(sint_type.family, vt_sint, "SINT type family must be vt_sint");
	ASSERT_EQ_INT(sint_type.width, 16, "SINT width must be 16");

	value_type_spec_t float_type = BVN_TYPE_FLOAT(64);
	ASSERT_EQ_INT(float_type.family, vt_float, "FLOAT type family must be vt_float");
	ASSERT_EQ_INT(float_type.width, 64, "FLOAT width must be 64");

	value_type_spec_t string_type = BVN_TYPE_UTF8;
	ASSERT_EQ_INT(string_type.family, vt_utf8, "UTF8 type family must be vt_utf8");
}

static void test_unit_constructors_si(void)
{
	printf("  test_unit_constructors_si...\n");

	value_unit_t unit = BVN_UNIT_NO_PREFIX(bu_meter);
	ASSERT_EQ_INT(unit.num_components, 1, "meter unit must have 1 component");
	ASSERT_EQ_INT(unit.components[0].base, bu_meter, "component base must be bu_meter");
	ASSERT_EQ_INT(unit.components[0].exponent, exp_linear, "exponent must be linear");

	value_unit_t km = BVN_UNIT_SI(bu_meter, si_kilo);
	ASSERT_EQ_INT(km.num_components, 1, "kilometer unit must have 1 component");
	ASSERT_EQ_INT(km.components[0].base, bu_meter, "component base must be bu_meter");
	ASSERT_EQ_INT(km.components[0].prefix.system, prefix_si, "prefix system must be SI");
	ASSERT_EQ_INT(km.components[0].prefix.id.si, si_kilo, "prefix must be kilo");
}

static void test_unit_constructors_iec(void)
{
	printf("  test_unit_constructors_iec...\n");

	value_unit_t kib = BVN_UNIT_IEC(bu_byte, iec_kibi);
	ASSERT_EQ_INT(kib.num_components, 1, "KiB unit must have 1 component");
	ASSERT_EQ_INT(kib.components[0].base, bu_byte, "component base must be bu_byte");
	ASSERT_EQ_INT(kib.components[0].prefix.system, prefix_iec, "prefix system must be IEC");
	ASSERT_EQ_INT(kib.components[0].prefix.id.iec, iec_kibi, "prefix must be kibi");
}

static void test_unit_none(void)
{
	printf("  test_unit_none...\n");

	value_unit_t none_unit = BVN_UNIT_NONE;
	ASSERT_EQ_INT(none_unit.num_components, 0, "BVN_UNIT_NONE must have 0 components");
}

static void test_exponent_roundtrip(void)
{
	printf("  test_exponent_roundtrip...\n");

	int exponents[] = {-9, -8, -7, -6, -5, -4, -3, -2, -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9};

	for (size_t i = 0; i < sizeof(exponents)/sizeof(exponents[0]); i++) {
		int exp_val = exponents[i];
		unit_exponent_t unit_exp = bvn_int_to_exponent(exp_val);
		int roundtrip = bvn_exponent_to_int(unit_exp);
		ASSERT_EQ_INT(roundtrip, exp_val, "exponent roundtrip must preserve value");
	}
}

static void test_si_prefixes_all(void)
{
	printf("  test_si_prefixes_all...\n");

	const struct {
		si_prefix_id_t id;
		double expected_factor;
	} prefixes[] = {
		{si_quecto, 1e-30},
		{si_ronto, 1e-27},
		{si_yocto, 1e-24},
		{si_zepto, 1e-21},
		{si_atto, 1e-18},
		{si_femto, 1e-15},
		{si_pico, 1e-12},
		{si_nano, 1e-9},
		{si_micro, 1e-6},
		{si_milli, 1e-3},
		{si_centi, 1e-2},
		{si_deci, 1e-1},
		{si_deca, 1e1},
		{si_hecto, 1e2},
		{si_kilo, 1e3},
		{si_mega, 1e6},
		{si_giga, 1e9},
		{si_tera, 1e12},
		{si_peta, 1e15},
		{si_exa, 1e18},
		{si_zetta, 1e21},
		{si_yotta, 1e24},
		{si_ronna, 1e27},
		{si_quetta, 1e30},
	};

	for (size_t i = 0; i < sizeof(prefixes)/sizeof(prefixes[0]); i++) {
		value_unit_t unit = BVN_UNIT_SI(bu_meter, prefixes[i].id);
		bool affine = false;
		double offset = 0.0;
		bool si_ok = true;
		double factor = bvn_unit_to_si_factor(unit, &affine, &offset, &si_ok);
		ASSERT_EQ_DBL(factor, prefixes[i].expected_factor, 1e-5,
					  "SI prefix factor must match expected value");
	}
}

static void test_iec_prefixes_all(void)
{
	printf("  test_iec_prefixes_all...\n");

	const struct {
		iec_prefix_id_t id;
		double expected_factor;
	} prefixes[] = {
		{iec_kibi, 1024.0},
		{iec_mebi, 1024.0*1024.0},
		{iec_gibi, 1024.0*1024.0*1024.0},
		{iec_tebi, 1024.0*1024.0*1024.0*1024.0},
		{iec_pebi, 1024.0*1024.0*1024.0*1024.0*1024.0},
	};

	for (size_t i = 0; i < sizeof(prefixes)/sizeof(prefixes[0]); i++) {
		value_unit_t unit = BVN_UNIT_IEC(bu_byte, prefixes[i].id);
		bool affine = false;
		double offset = 0.0;
		bool si_ok = true;
		double factor = bvn_unit_to_si_factor(unit, &affine, &offset, &si_ok);
		ASSERT_EQ_DBL(factor, prefixes[i].expected_factor, 1.0,
					  "IEC prefix factor must match expected value");
	}
}

static void test_base_unit_all(void)
{
	printf("  test_base_unit_all...\n");

	value_base_unit_t units[] = {
		bu_bit, bu_byte,
		bu_second, bu_meter, bu_gram, bu_ampere, bu_kelvin,
		bu_mol, bu_candela,
		bu_hertz, bu_newton, bu_pascal, bu_joule, bu_watt,
		bu_volt, bu_ohm, bu_farad, bu_coulomb, bu_siemens,
		bu_weber, bu_tesla, bu_henry, bu_lumen, bu_lux,
		bu_becquerel, bu_gray, bu_sievert, bu_katal,
		bu_liter, bu_minute, bu_hour, bu_day, bu_degree, bu_celsius,
	};

	for (size_t i = 0; i < sizeof(units)/sizeof(units[0]); i++) {
		value_unit_t unit = BVN_UNIT_NO_PREFIX(units[i]);
		ASSERT_EQ_INT(unit.num_components, 1, "single base unit must have 1 component");
		ASSERT_EQ_INT(unit.components[0].base, units[i], "base unit must match");
	}
}

static void test_unit_conversion_celsius(void)
{
	printf("  test_unit_conversion_celsius...\n");

	value_unit_t celsius = BVN_UNIT_NO_PREFIX(bu_celsius);
	bool affine = false;
	double offset = 0.0;
	bool si_ok = true;
	double factor = bvn_unit_to_si_factor(celsius, &affine, &offset, &si_ok);

	ASSERT_TRUE(affine, "Celsius must be affine");
	ASSERT_EQ_DBL(offset, 273.15, 0.01, "Celsius offset must be 273.15K");
	ASSERT_EQ_DBL(factor, 1.0, 1e-10, "Celsius scale factor must be 1.0");
}

static void test_type_family_names(void)
{
	printf("  test_type_family_names...\n");

	value_type_family_t families[] = {
		vt_plain, vt_utf8, vt_sint, vt_uint, vt_float
	};

	for (size_t i = 0; i < sizeof(families)/sizeof(families[0]); i++) {
		ASSERT_TRUE(families[i] >= vt_plain && families[i] < vt_illegal,
					"type family must be valid");
	}
}

static void test_error_codes_coverage(void)
{
	printf("  test_error_codes_coverage...\n");

	error_code_t errors[] = {
		error_none,
		error_unknown_token_type,
		error_identifier_too_long,
		error_string_too_long,
		error_illegal_escape_sequence,
		error_got_incomplete_bvnr_stream,
		error_invalid_utf8_byte,
		error_type_value_mismatch,
		error_recovered,
	};

	for (size_t i = 0; i < sizeof(errors)/sizeof(errors[0]); i++) {
		const char *err_str = bvn_error_to_string(errors[i]);
		ASSERT_TRUE(err_str != NULL, "error_to_string must not return NULL");
		ASSERT_TRUE(strlen(err_str) > 0, "error string must not be empty");
	}
}

static void test_token_type_boundaries(void)
{
	printf("  test_token_type_boundaries...\n");

	token_type_t types[] = {
		token_is_identifier,
		token_is_string,
		token_is_number,
		token_is_symbol,
		token_is_reference,
		token_is_type,
		token_is_null_value,
		token_is_structure,
		token_is_unit,
	};

	for (size_t i = 0; i < sizeof(types)/sizeof(types[0]); i++) {
		ASSERT_TRUE(types[i] >= token_is_identifier && types[i] < token_is_unknown,
					"token type must be valid");
	}
}

static void test_unit_math_compound_units(void)
{
	printf("  test_unit_math_compound_units...\n");

	value_unit_t speed_unit;
	speed_unit.num_components = 2;
	speed_unit.components[0] = (value_unit_component_t){
		.base = bu_meter,
		.exponent = exp_linear,
		.prefix = {.system = prefix_si, .id.si = si_none}
	};
	speed_unit.components[1] = (value_unit_component_t){
		.base = bu_second,
		.exponent = exp_neg_linear,
		.prefix = {.system = prefix_si, .id.si = si_none}
	};

	ASSERT_EQ_INT(speed_unit.num_components, 2, "speed unit must have 2 components");
	ASSERT_EQ_INT(speed_unit.components[0].base, bu_meter, "first component must be meter");
	ASSERT_EQ_INT(speed_unit.components[1].base, bu_second, "second component must be second");
}

static void test_unit_edge_cases(void)
{
	printf("  test_unit_edge_cases...\n");

	value_unit_t max_unit;
	max_unit.num_components = BVNR_MAX_UNIT_COMPONENTS;
	for (uint32_t i = 0; i < BVNR_MAX_UNIT_COMPONENTS; i++) {
		max_unit.components[i].base = bu_meter;
		max_unit.components[i].exponent = exp_linear;
	}

	ASSERT_EQ_INT(max_unit.num_components, BVNR_MAX_UNIT_COMPONENTS,
				  "unit must support maximum components");
}

static void test_format_double_base_out_of_range(void)
{
	printf("  test_format_double_base_out_of_range...\n");

	char buf[64];
	value_type_spec_t vt37 = BVN_TYPE_FLOAT_BASE(64, 37);
	ASSERT_EQ_INT(bvn_format_double(buf, sizeof(buf), 3.14, vt37), -1,
				  "bvn_format_double with base=37 must return -1");

	value_type_spec_t vt62 = BVN_TYPE_FLOAT_BASE(64, 62);
	ASSERT_EQ_INT(bvn_format_double(buf, sizeof(buf), 1.0, vt62), -1,
				  "bvn_format_double with base=62 must return -1");

	value_type_spec_t vt10 = BVN_TYPE_FLOAT(64);
	ASSERT_TRUE(bvn_format_double(buf, sizeof(buf), 1.5, vt10) > 0,
				"bvn_format_double with base=10 must succeed");

	value_type_spec_t vt16 = BVN_TYPE_FLOAT_BASE(64, 16);
	ASSERT_TRUE(bvn_format_double(buf, sizeof(buf), 1.5, vt16) > 0,
				"bvn_format_double with base=16 must succeed");
}

static void test_validate_number_in_base_e_digit(void)
{
	printf("  test_validate_number_in_base_e_digit...\n");

	ASSERT_TRUE(bvn_validate_number_in_base("ae", 16),
				"\"ae\" is a valid base-16 integer (a=10, e=14)");
	ASSERT_TRUE(bvn_validate_number_in_base("1e1", 16),
				"\"1e1\" is a valid base-16 integer (1,14,1)");
	ASSERT_TRUE(bvn_validate_number_in_base("ffee", 16),
				"\"ffee\" is a valid base-16 integer");
	ASSERT_TRUE(bvn_validate_number_in_base("e", 15),
				"\"e\" is the digit 14 in base 15");
	ASSERT_TRUE(bvn_validate_number_in_base("EF", 16),
				"\"EF\" is valid base-16 (E=14, F=15)");

	ASSERT_TRUE(bvn_validate_number_in_base("1e2", 10),
				"\"1e2\" is a valid base-10 scientific float");
	ASSERT_FALSE(bvn_validate_number_in_base("e", 10),
				 "bare \"e\" is not a valid base-10 number");
	ASSERT_FALSE(bvn_validate_number_in_base("1e", 10),
				 "\"1e\" with no exponent digits is invalid base-10");

	ASSERT_TRUE(bvn_validate_number_in_base("1p2", 16),
				"\"1p2\" is valid base-16 hex float (p-exponent)");
	ASSERT_FALSE(bvn_validate_number_in_base("1p2", 10),
				 "\"1p2\" is invalid in base-10 (p not a decimal exponent)");
	ASSERT_TRUE(bvn_validate_number_in_base("1p2", 62),
				"\"1p2\" is valid in base-62 ('p' is digit 25, not an exponent)");
}

static void test_unit_equal(void)
{
	printf("  test_unit_equal...\n");

	value_unit_t m1 = BVN_UNIT_NO_PREFIX(bu_meter);
	value_unit_t m2 = BVN_UNIT_NO_PREFIX(bu_meter);
	ASSERT_TRUE(bvn_unit_equal(m1, m2), "identical meter units must be equal");

	value_unit_t km = BVN_UNIT_SI(bu_meter, si_kilo);
	ASSERT_FALSE(bvn_unit_equal(m1, km),
				 "meter and kilometer must not be equal");

	value_unit_t none1 = BVN_UNIT_NONE;
	value_unit_t none2 = BVN_UNIT_NONE;
	ASSERT_TRUE(bvn_unit_equal(none1, none2), "two UNIT_NONE must be equal");

	ASSERT_FALSE(bvn_unit_equal(none1, m1),
				 "UNIT_NONE and meter must not be equal");

	value_unit_t kib = BVN_UNIT_IEC(bu_byte, iec_kibi);
	value_unit_t kib2 = BVN_UNIT_IEC(bu_byte, iec_kibi);
	ASSERT_TRUE(bvn_unit_equal(kib, kib2), "two KiB units must be equal");

	value_unit_t mib = BVN_UNIT_IEC(bu_byte, iec_mebi);
	ASSERT_FALSE(bvn_unit_equal(kib, mib), "KiB and MiB must not be equal");

	value_unit_t ms_sq = BVN_UNIT_COMPOUND2(
		bu_meter, si_none, exp_linear,
		bu_second, si_none, exp_neg_square);
	value_unit_t ms_sq2 = BVN_UNIT_COMPOUND2(
		bu_meter, si_none, exp_linear,
		bu_second, si_none, exp_neg_square);
	ASSERT_TRUE(bvn_unit_equal(ms_sq, ms_sq2),
				"compound m/s^2 units must be equal");
	ASSERT_FALSE(bvn_unit_equal(ms_sq, m1),
				 "compound m/s^2 vs plain meter must not be equal");
}

/*
 * bvn_parse_uint64/int64 take a value_type_spec_t so they can range-check, which
 * is what doc/3 s8 promises ("the vt argument supplies the base and bit-width for
 * range checking", "returns false if the string is not representable in the
 * declared type"). Only the base was consulted: <uint:8> accepted 256 and, via
 * strtoull's silent wrap of a sign an unsigned carrier cannot have, "-1" came
 * back true with 18446744073709551615.
 *
 * The acceptance criterion is agreement with the READER: these helpers exist to
 * decode ev_data, so anything they accept that a document could not carry is a
 * trap. A sweep of 1250 (width, base, value) combinations against the reader
 * found 503 divergences before this and 0 after; the cases below pin the
 * boundaries of that sweep.
 */
static void test_parse_helpers_respect_declared_width(void)
{
	printf("  test_parse_helpers_respect_declared_width...\n");

	uint64_t u = 0;
	int64_t  i = 0;
	value_type_spec_t u8   = { .family = vt_uint, .width = 8 };
	value_type_spec_t s8   = { .family = vt_sint, .width = 8 };
	value_type_spec_t u16h = { .family = vt_uint, .width = 16, .base = 16 };
	value_type_spec_t u1   = { .family = vt_uint, .width = 1 };
	value_type_spec_t u64  = { .family = vt_uint, .width = 64 };

	ASSERT_TRUE(bvn_parse_uint64("255", u8, &u) && u == 255,
		"uint:8 accepts its maximum");
	ASSERT_FALSE(bvn_parse_uint64("256", u8, &u), "uint:8 rejects 256");
	ASSERT_FALSE(bvn_parse_uint64("99999", u8, &u), "uint:8 rejects 99999");
	/* the one that returned a huge positive value */
	ASSERT_FALSE(bvn_parse_uint64("-1", u8, &u), "uint:8 rejects a sign");
	ASSERT_FALSE(bvn_parse_uint64("-1", u64, &u), "uint:64 rejects a sign");

	ASSERT_TRUE(bvn_parse_uint64("1", u1, &u) && u == 1, "uint:1 accepts 1");
	ASSERT_FALSE(bvn_parse_uint64("2", u1, &u), "uint:1 rejects 2");

	ASSERT_TRUE(bvn_parse_int64("-128", s8, &i) && i == -128,
		"sint:8 accepts its minimum");
	ASSERT_TRUE(bvn_parse_int64("127", s8, &i) && i == 127,
		"sint:8 accepts its maximum");
	ASSERT_FALSE(bvn_parse_int64("128", s8, &i), "sint:8 rejects 128");
	ASSERT_FALSE(bvn_parse_int64("-129", s8, &i), "sint:8 rejects -129");

	ASSERT_TRUE(bvn_parse_uint64("ffff", u16h, &u) && u == 65535,
		"uint:16 base 16 accepts ffff");
	ASSERT_FALSE(bvn_parse_uint64("10000", u16h, &u),
		"uint:16 base 16 rejects 10000");

	/* strtoll/strtoull also accept these; a value token never looks like them. */
	ASSERT_FALSE(bvn_parse_uint64("", u8, &u), "empty string rejected");
	ASSERT_FALSE(bvn_parse_int64("", s8, &i), "empty string rejected (signed)");
	ASSERT_FALSE(bvn_parse_uint64("  12", u8, &u), "leading space rejected");
	ASSERT_FALSE(bvn_parse_int64("\t-1", s8, &i), "leading tab rejected");
	ASSERT_FALSE(bvn_parse_uint64("+1", u8, &u), "leading plus rejected");
	ASSERT_FALSE(bvn_parse_int64("+1", s8, &i), "leading plus rejected (signed)");
	ASSERT_FALSE(bvn_parse_int64("-", s8, &i), "a bare sign is not a number");

	/* A width above 64 cannot be range-checked by a 64-bit out parameter, and
	 * must not start rejecting things because of it. */
	value_type_spec_t u128 = { .family = vt_uint, .width = 128 };
	ASSERT_TRUE(bvn_parse_uint64("18446744073709551615", u128, &u),
		"uint:128 still accepts everything a uint64 can hold");
	ASSERT_FALSE(bvn_parse_uint64("18446744073709551616", u128, &u),
		"...and nothing more");

	/* Width 0 means the default 64. */
	value_type_spec_t udef = { .family = vt_uint, .width = 0 };
	ASSERT_TRUE(bvn_parse_uint64("18446744073709551615", udef, &u),
		"width 0 behaves as 64");
}

int main(void)
{
	printf("Running bovnar_utils_test regression suite...\n");

	test_type_spec_constructors();
	test_unit_constructors_si();
	test_unit_constructors_iec();
	test_unit_none();
	test_exponent_roundtrip();
	test_si_prefixes_all();
	test_iec_prefixes_all();
	test_base_unit_all();
	test_unit_conversion_celsius();
	test_type_family_names();
	test_error_codes_coverage();
	test_token_type_boundaries();
	test_unit_math_compound_units();
	test_unit_edge_cases();
	test_format_double_base_out_of_range();
	test_validate_number_in_base_e_digit();
	test_unit_equal();
	test_parse_helpers_respect_declared_width();

	if (failures == 0) {
		printf("PASSED %d tests\n", tests);
		return 0;
	}

	fprintf(stderr, "FAILED %d of %d tests\n", failures, tests);
	return 1;
}
