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
#include <stdlib.h>
#include <string.h>
#include "bovnar.h"
#include "bvn_internal_dims.h"
#include "bovnar_si_units.h"
#include "bvn_int.h"
#include "bvn_float.h"

static int failures = 0;
static int tests    = 0;

#define ASSERT_EQ_DBL(a, b, tol, msg) do {                          \
	tests++;                                                     \
	double _a = (a), _b = (b);                                   \
	if (fabs(_a - _b) > (tol)) {                                 \
		fprintf(stderr, "FAIL line %d: %s\n  got %.15g, expected %.15g\n", \
		        __LINE__, (msg), _a, _b);                     \
		failures++;                                           \
	}                                                             \
} while (0)

#define ASSERT_EQ_INT(a, b, msg) do {                                \
	tests++;                                                     \
	int64_t _a = (int64_t)(a), _b = (int64_t)(b);               \
	if (_a != _b) {                                              \
		fprintf(stderr, "FAIL line %d: %s\n  got %lld, expected %lld\n", \
		        __LINE__, (msg), (long long)_a, (long long)_b); \
		failures++;                                           \
	}                                                             \
} while (0)

#define ASSERT_TRUE(cond, msg) do {                                   \
	tests++;                                                     \
	if (!(cond)) {                                                \
		fprintf(stderr, "FAIL line %d: %s\n", __LINE__, (msg)); \
		failures++;                                           \
	}                                                             \
} while (0)

static void test_exponent_to_int(void)
{
	printf("  exponent_to_int...\n");
	ASSERT_EQ_INT(bvn_exponent_to_int(exp_linear),      1,  "exp_linear → 1");
	ASSERT_EQ_INT(bvn_exponent_to_int(exp_square),      2,  "exp_square → 2");
	ASSERT_EQ_INT(bvn_exponent_to_int(exp_cubic),       3,  "exp_cubic → 3");
	ASSERT_EQ_INT(bvn_exponent_to_int(exp_neg_linear), -1,  "exp_neg_linear → -1");
	ASSERT_EQ_INT(bvn_exponent_to_int(exp_neg_square), -2,  "exp_neg_square → -2");
	ASSERT_EQ_INT(bvn_exponent_to_int(exp_neg_cubic),  -3,  "exp_neg_cubic → -3");
	ASSERT_EQ_INT(bvn_exponent_to_int(exp_invalid),     0,  "exp_invalid → 0");
}

static void test_exp_invalid_is_zero_init(void)
{
	printf("  exp_invalid is zero-initialised sentinel...\n");
	/*
	 * The whole point of making exp_invalid = 0: a zero-initialised
	 * value_unit_component_t must carry exp_invalid, not a silent linear.
	 */
	value_unit_component_t zeroed;
	memset(&zeroed, 0, sizeof(zeroed));
	ASSERT_TRUE(zeroed.exponent == exp_invalid,
	            "zero-init component has exp_invalid");
}

static void test_int_to_exponent(void)
{
	printf("  int_to_exponent...\n");
	ASSERT_TRUE(bvn_int_to_exponent(0)  == exp_invalid,    "0 → exp_invalid");
	ASSERT_TRUE(bvn_int_to_exponent(1)  == exp_linear,      "1 → exp_linear");
	ASSERT_TRUE(bvn_int_to_exponent(2)  == exp_square,      "2 → exp_square");
	ASSERT_TRUE(bvn_int_to_exponent(3)  == exp_cubic,       "3 → exp_cubic");
	ASSERT_TRUE(bvn_int_to_exponent(-1) == exp_neg_linear,  "-1 → exp_neg_linear");
	ASSERT_TRUE(bvn_int_to_exponent(-2) == exp_neg_square,  "-2 → exp_neg_square");
	ASSERT_TRUE(bvn_int_to_exponent(-3) == exp_neg_cubic,   "-3 → exp_neg_cubic");
	/* The range is [BVN_EXPONENT_MIN, BVN_EXPONENT_MAX], not the ±9 the named
	 * enumerators cover, so a two- or three-digit exponent round-trips. */
	ASSERT_TRUE(bvn_int_to_exponent(99)   == 99,   "99 round-trips");
	ASSERT_TRUE(bvn_int_to_exponent(100)  == 100,  "100 is the maximum");
	ASSERT_TRUE(bvn_int_to_exponent(-100) == -100, "-100 is the minimum");
	ASSERT_TRUE(bvn_exponent_to_int(bvn_int_to_exponent(-42)) == -42,
	            "-42 survives both directions");
	/* Zero stays reserved, and anything past the bounds is refused. */
	ASSERT_TRUE(bvn_int_to_exponent(0)    == exp_invalid, "0 → exp_invalid");
	ASSERT_TRUE(bvn_int_to_exponent(101)  == exp_invalid, "101 → exp_invalid");
	ASSERT_TRUE(bvn_int_to_exponent(-101) == exp_invalid, "-101 → exp_invalid");
	ASSERT_TRUE(bvn_exponent_to_int((unit_exponent_t)5000) == 0,
	            "an out-of-range struct value reads as 0, it does not abort");
}

static void test_si_factor_simple(void)
{
	printf("  si_factor simple units...\n");
	bool aff; double off; bool si_ok = true;

	ASSERT_EQ_DBL(bvn_unit_to_si_factor(BVN_UNIT_NO_PREFIX(bu_meter), &aff, &off, &si_ok),
	              1.0, 1e-15, "m → 1.0");
	ASSERT_TRUE(!aff, "m not affine");

	ASSERT_EQ_DBL(bvn_unit_to_si_factor(BVN_UNIT_SI(bu_meter, si_kilo), &aff, &off, &si_ok),
	              1e3, 1e-10, "km → 1e3");

	ASSERT_EQ_DBL(bvn_unit_to_si_factor(BVN_UNIT_NO_PREFIX(bu_gram), &aff, &off, &si_ok),
	              1e-3, 1e-15, "g → 1e-3");

	ASSERT_EQ_DBL(bvn_unit_to_si_factor(BVN_UNIT_SI(bu_gram, si_kilo), &aff, &off, &si_ok),
	              1.0, 1e-15, "kg → 1.0");

	ASSERT_EQ_DBL(bvn_unit_to_si_factor(BVN_UNIT_NO_PREFIX(bu_second), &aff, &off, &si_ok),
	              1.0, 1e-15, "s → 1.0");

	ASSERT_EQ_DBL(bvn_unit_to_si_factor(BVN_UNIT_NO_PREFIX(bu_minute), &aff, &off, &si_ok),
	              60.0, 1e-15, "min → 60");

	ASSERT_EQ_DBL(bvn_unit_to_si_factor(BVN_UNIT_NO_PREFIX(bu_hour), &aff, &off, &si_ok),
	              3600.0, 1e-15, "h → 3600");

	ASSERT_EQ_DBL(bvn_unit_to_si_factor(BVN_UNIT_NO_PREFIX(bu_day), &aff, &off, &si_ok),
	              86400.0, 1e-10, "d → 86400");

	ASSERT_EQ_DBL(bvn_unit_to_si_factor(BVN_UNIT_NO_PREFIX(bu_liter), &aff, &off, &si_ok),
	              1e-3, 1e-15, "L → 1e-3");

	double cf = bvn_unit_to_si_factor(BVN_UNIT_NO_PREFIX(bu_celsius), &aff, &off, &si_ok);
	ASSERT_EQ_DBL(cf, 1.0, 1e-15, "°C factor = 1.0");
	ASSERT_TRUE(aff, "°C is affine");
	ASSERT_EQ_DBL(off, 273.15, 1e-10, "°C offset = 273.15");

	ASSERT_EQ_DBL(bvn_unit_to_si_factor(BVN_UNIT_NO_PREFIX(bu_kelvin), &aff, &off, &si_ok),
	              1.0, 1e-15, "K → 1.0");
	ASSERT_TRUE(!aff, "K not affine");
}

static void test_si_factor_derived(void)
{
	printf("  si_factor derived units...\n");
	bool aff; double off; bool si_ok = true;

	ASSERT_EQ_DBL(bvn_unit_to_si_factor(BVN_UNIT_NO_PREFIX(bu_newton), &aff, &off, &si_ok),
	              1.0, 1e-15, "N → 1.0");

	ASSERT_EQ_DBL(bvn_unit_to_si_factor(BVN_UNIT_SI(bu_newton, si_kilo), &aff, &off, &si_ok),
	              1e3, 1e-10, "kN → 1e3");

	ASSERT_EQ_DBL(bvn_unit_to_si_factor(BVN_UNIT_NO_PREFIX(bu_pascal), &aff, &off, &si_ok),
	              1.0, 1e-15, "Pa → 1.0");

	ASSERT_EQ_DBL(bvn_unit_to_si_factor(BVN_UNIT_NO_PREFIX(bu_joule), &aff, &off, &si_ok),
	              1.0, 1e-15, "J → 1.0");

	ASSERT_EQ_DBL(bvn_unit_to_si_factor(BVN_UNIT_SI(bu_joule, si_kilo), &aff, &off, &si_ok),
	              1e3, 1e-10, "kJ → 1e3");

	ASSERT_EQ_DBL(bvn_unit_to_si_factor(BVN_UNIT_NO_PREFIX(bu_hertz), &aff, &off, &si_ok),
	              1.0, 1e-15, "Hz → 1.0");

	ASSERT_EQ_DBL(bvn_unit_to_si_factor(BVN_UNIT_SI(bu_hertz, si_kilo), &aff, &off, &si_ok),
	              1e3, 1e-10, "kHz → 1e3");

	ASSERT_EQ_DBL(bvn_unit_to_si_factor(BVN_UNIT_NO_PREFIX(bu_watt), &aff, &off, &si_ok),
	              1.0, 1e-15, "W → 1.0");

	ASSERT_EQ_DBL(bvn_unit_to_si_factor(BVN_UNIT_SI(bu_watt, si_milli), &aff, &off, &si_ok),
	              1e-3, 1e-15, "mW → 1e-3");

	ASSERT_EQ_DBL(bvn_unit_to_si_factor(BVN_UNIT_NO_PREFIX(bu_volt), &aff, &off, &si_ok),
	              1.0, 1e-15, "V → 1.0");

	ASSERT_EQ_DBL(bvn_unit_to_si_factor(BVN_UNIT_NO_PREFIX(bu_katal), &aff, &off, &si_ok),
	              1.0, 1e-15, "kat → 1.0");
}

static void test_si_factor_compound(void)
{
	printf("  si_factor compound units...\n");
	bool aff; double off; bool si_ok = true;

	value_unit_t m_per_s = {
		.num_components = 2,
		.components = {
			{ bu_meter,  exp_linear,      {prefix_si, .id.si=si_none} },
			{ bu_second, exp_neg_linear,  {prefix_si, .id.si=si_none} }
		}
	};
	ASSERT_EQ_DBL(bvn_unit_to_si_factor(m_per_s, &aff, &off, &si_ok),
	              1.0, 1e-15, "m/s → 1.0");

	value_unit_t m_per_s2 = {
		.num_components = 2,
		.components = {
			{ bu_meter,  exp_linear,      {prefix_si, .id.si=si_none} },
			{ bu_second, exp_neg_square,  {prefix_si, .id.si=si_none} }
		}
	};
	ASSERT_EQ_DBL(bvn_unit_to_si_factor(m_per_s2, &aff, &off, &si_ok),
	              1.0, 1e-15, "m/s² → 1.0");

	value_unit_t kg_m_s2 = {
		.num_components = 3,
		.components = {
			{ bu_gram,   exp_linear,      {prefix_si, .id.si=si_kilo} },
			{ bu_meter,  exp_linear,      {prefix_si, .id.si=si_none} },
			{ bu_second, exp_neg_square,  {prefix_si, .id.si=si_none} }
		}
	};
	ASSERT_EQ_DBL(bvn_unit_to_si_factor(kg_m_s2, &aff, &off, &si_ok),
	              1.0, 1e-15, "k~g·m/s² → 1.0");

	value_unit_t km_per_s = {
		.num_components = 2,
		.components = {
			{ bu_meter,  exp_linear,      {prefix_si, .id.si=si_kilo} },
			{ bu_second, exp_neg_linear,  {prefix_si, .id.si=si_none} }
		}
	};
	ASSERT_EQ_DBL(bvn_unit_to_si_factor(km_per_s, &aff, &off, &si_ok),
	              1e3, 1e-10, "k~m/s → 1e3");

	value_unit_t kg_per_m3 = {
		.num_components = 2,
		.components = {
			{ bu_gram,  exp_linear,      {prefix_si, .id.si=si_kilo} },
			{ bu_meter, exp_neg_cubic,   {prefix_si, .id.si=si_none} }
		}
	};
	ASSERT_EQ_DBL(bvn_unit_to_si_factor(kg_per_m3, &aff, &off, &si_ok),
	              1.0, 1e-15, "k~g/m³ → 1.0");

	value_unit_t g_per_m3 = {
		.num_components = 2,
		.components = {
			{ bu_gram,  exp_linear,      {prefix_si, .id.si=si_none} },
			{ bu_meter, exp_neg_cubic,   {prefix_si, .id.si=si_none} }
		}
	};
	ASSERT_EQ_DBL(bvn_unit_to_si_factor(g_per_m3, &aff, &off, &si_ok),
	              1e-3, 1e-15, "g/m³ → 1e-3");

	value_unit_t kg_m2_s2 = {
		.num_components = 3,
		.components = {
			{ bu_gram,   exp_linear,      {prefix_si, .id.si=si_kilo} },
			{ bu_meter,  exp_square,      {prefix_si, .id.si=si_none} },
			{ bu_second, exp_neg_square,  {prefix_si, .id.si=si_none} }
		}
	};
	ASSERT_EQ_DBL(bvn_unit_to_si_factor(kg_m2_s2, &aff, &off, &si_ok),
	              1.0, 1e-15, "k~g·m²/s² → 1.0");

	value_unit_t L_per_min = {
		.num_components = 2,
		.components = {
			{ bu_liter,  exp_linear,      {prefix_si, .id.si=si_none} },
			{ bu_minute, exp_neg_linear,  {prefix_si, .id.si=si_none} }
		}
	};
	ASSERT_EQ_DBL(bvn_unit_to_si_factor(L_per_min, &aff, &off, &si_ok),
	              1e-3 / 60.0, 1e-18, "L/min → 1e-3/60");

	ASSERT_EQ_DBL(bvn_unit_to_si_factor(BVN_UNIT_IEC(bu_byte, iec_kibi), &aff, &off, &si_ok),
	              1024.0, 1e-10, "Ki~B → 1024");

	ASSERT_EQ_DBL(bvn_unit_to_si_factor(BVN_UNIT_IEC(bu_byte, iec_tebi), &aff, &off, &si_ok),
	              1099511627776.0, 1e5, "Ti~B → 2^40");
}

static void test_dimension_vector(void)
{
	printf("  dimension_vector...\n");
	int32_t dims[bvn_si_dim_count];

	bvn_unit_dimension_vector(BVN_UNIT_NO_PREFIX(bu_meter), dims);
	ASSERT_EQ_INT(dims[bvn_si_dim_meter], 1, "m dim[m]=1");
	ASSERT_EQ_INT(dims[bvn_si_dim_kilogram], 0, "m dim[kg]=0");

	bvn_unit_dimension_vector(BVN_UNIT_NO_PREFIX(bu_gram), dims);
	ASSERT_EQ_INT(dims[bvn_si_dim_meter], 0, "g dim[m]=0");
	ASSERT_EQ_INT(dims[bvn_si_dim_kilogram], 1, "g dim[kg]=1");

	bvn_unit_dimension_vector(BVN_UNIT_NO_PREFIX(bu_newton), dims);
	ASSERT_EQ_INT(dims[bvn_si_dim_meter],    1,  "N dim[m]=1");
	ASSERT_EQ_INT(dims[bvn_si_dim_kilogram], 1,  "N dim[kg]=1");
	ASSERT_EQ_INT(dims[bvn_si_dim_second],  -2, "N dim[s]=-2");

	bvn_unit_dimension_vector(BVN_UNIT_NO_PREFIX(bu_pascal), dims);
	ASSERT_EQ_INT(dims[bvn_si_dim_meter],   -1, "Pa dim[m]=-1");
	ASSERT_EQ_INT(dims[bvn_si_dim_kilogram], 1, "Pa dim[kg]=1");
	ASSERT_EQ_INT(dims[bvn_si_dim_second],  -2, "Pa dim[s]=-2");

	bvn_unit_dimension_vector(BVN_UNIT_NO_PREFIX(bu_joule), dims);
	ASSERT_EQ_INT(dims[bvn_si_dim_meter],    2,  "J dim[m]=2");
	ASSERT_EQ_INT(dims[bvn_si_dim_kilogram], 1,  "J dim[kg]=1");
	ASSERT_EQ_INT(dims[bvn_si_dim_second],  -2, "J dim[s]=-2");

	bvn_unit_dimension_vector(BVN_UNIT_NO_PREFIX(bu_watt), dims);
	ASSERT_EQ_INT(dims[bvn_si_dim_second], -3, "W dim[s]=-3");

	bvn_unit_dimension_vector(BVN_UNIT_NO_PREFIX(bu_hertz), dims);
	ASSERT_EQ_INT(dims[bvn_si_dim_second], -1, "Hz dim[s]=-1");

	bvn_unit_dimension_vector(BVN_UNIT_NO_PREFIX(bu_volt), dims);
	ASSERT_EQ_INT(dims[bvn_si_dim_ampere], -1, "V dim[A]=-1");

	bvn_unit_dimension_vector(BVN_UNIT_NO_PREFIX(bu_ohm), dims);
	ASSERT_EQ_INT(dims[bvn_si_dim_ampere], -2, "Ω dim[A]=-2");

	bvn_unit_dimension_vector(BVN_UNIT_NO_PREFIX(bu_farad), dims);
	ASSERT_EQ_INT(dims[bvn_si_dim_meter],    -2, "F dim[m]=-2");
	ASSERT_EQ_INT(dims[bvn_si_dim_kilogram], -1, "F dim[kg]=-1");
	ASSERT_EQ_INT(dims[bvn_si_dim_second],    4, "F dim[s]=4");
	ASSERT_EQ_INT(dims[bvn_si_dim_ampere],    2, "F dim[A]=2");

	value_unit_t m_per_s2 = {
		.num_components = 2,
		.components = {
			{ bu_meter,  exp_linear,     {prefix_si, .id.si=si_none} },
			{ bu_second, exp_neg_square, {prefix_si, .id.si=si_none} }
		}
	};
	bvn_unit_dimension_vector(m_per_s2, dims);
	ASSERT_EQ_INT(dims[bvn_si_dim_meter],  1,  "m/s² dim[m]=1");
	ASSERT_EQ_INT(dims[bvn_si_dim_second], -2, "m/s² dim[s]=-2");

	value_unit_t kg_m_s2 = {
		.num_components = 3,
		.components = {
			{ bu_gram,   exp_linear,     {prefix_si, .id.si=si_kilo} },
			{ bu_meter,  exp_linear,     {prefix_si, .id.si=si_none} },
			{ bu_second, exp_neg_square, {prefix_si, .id.si=si_none} }
		}
	};
	bvn_unit_dimension_vector(kg_m_s2, dims);
	ASSERT_EQ_INT(dims[bvn_si_dim_meter],    1,  "k~g·m/s² dim[m]=1");
	ASSERT_EQ_INT(dims[bvn_si_dim_kilogram], 1,  "k~g·m/s² dim[kg]=1");
	ASSERT_EQ_INT(dims[bvn_si_dim_second],  -2, "k~g·m/s² dim[s]=-2");

	bvn_unit_dimension_vector(BVN_UNIT_NO_PREFIX(bu_liter), dims);
	ASSERT_EQ_INT(dims[bvn_si_dim_meter], 3, "L dim[m]=3");

	bvn_unit_dimension_vector(BVN_UNIT_NO_PREFIX(bu_minute), dims);
	ASSERT_EQ_INT(dims[bvn_si_dim_second], 1, "min dim[s]=1");

	bvn_unit_dimension_vector(BVN_UNIT_NO_PREFIX(bu_celsius), dims);
	ASSERT_EQ_INT(dims[bvn_si_dim_kelvin], 1, "°C dim[K]=1");
}

static void test_units_compatible(void)
{
	printf("  units_compatible...\n");

	value_unit_t newton = BVN_UNIT_NO_PREFIX(bu_newton);
	value_unit_t kg_m_s2 = {
		.num_components = 3,
		.components = {
			{ bu_gram,   exp_linear,     {prefix_si, .id.si=si_kilo} },
			{ bu_meter,  exp_linear,     {prefix_si, .id.si=si_none} },
			{ bu_second, exp_neg_square, {prefix_si, .id.si=si_none} }
		}
	};
	ASSERT_TRUE(bvn_units_compatible(newton, kg_m_s2),
	            "N compatible with k~g·m/s²");

	value_unit_t pascal = BVN_UNIT_NO_PREFIX(bu_pascal);
	value_unit_t N_per_m2 = {
		.num_components = 2,
		.components = {
			{ bu_newton, exp_linear,     {prefix_si, .id.si=si_none} },
			{ bu_meter,  exp_neg_square, {prefix_si, .id.si=si_none} }
		}
	};
	ASSERT_TRUE(bvn_units_compatible(pascal, N_per_m2),
	            "Pa compatible with N/m²");

	ASSERT_TRUE(!bvn_units_compatible(
		BVN_UNIT_NO_PREFIX(bu_meter),
		BVN_UNIT_NO_PREFIX(bu_second)),
	            "m NOT compatible with s");

	value_unit_t joule = BVN_UNIT_NO_PREFIX(bu_joule);
	value_unit_t N_m = {
		.num_components = 2,
		.components = {
			{ bu_newton, exp_linear, {prefix_si, .id.si=si_none} },
			{ bu_meter,  exp_linear, {prefix_si, .id.si=si_none} }
		}
	};
	ASSERT_TRUE(bvn_units_compatible(joule, N_m),
	            "J compatible with N·m");
}

static void test_convert_factor(void)
{
	printf("  convert_factor...\n");
	bool ok;
	bool req_aff;

	ASSERT_EQ_DBL(
		bvn_unit_convert_factor(
			BVN_UNIT_SI(bu_meter, si_kilo),
			BVN_UNIT_NO_PREFIX(bu_meter),
			&ok, &req_aff),
		1e3, 1e-10, "km → m = 1e3");
	ASSERT_TRUE(ok, "km → m ok");

	ASSERT_EQ_DBL(
		bvn_unit_convert_factor(
			BVN_UNIT_NO_PREFIX(bu_meter),
			BVN_UNIT_SI(bu_meter, si_kilo),
			&ok, &req_aff),
		1e-3, 1e-15, "m → km = 1e-3");
	ASSERT_TRUE(ok, "m → km ok");

	ASSERT_EQ_DBL(
		bvn_unit_convert_factor(
			BVN_UNIT_SI(bu_newton, si_kilo),
			BVN_UNIT_NO_PREFIX(bu_newton),
			&ok, &req_aff),
		1e3, 1e-10, "kN → N = 1e3");
	ASSERT_TRUE(ok, "kN → N ok");

	value_unit_t kg_m_s2 = {
		.num_components = 3,
		.components = {
			{ bu_gram,   exp_linear,     {prefix_si, .id.si=si_kilo} },
			{ bu_meter,  exp_linear,     {prefix_si, .id.si=si_none} },
			{ bu_second, exp_neg_square, {prefix_si, .id.si=si_none} }
		}
	};
	ASSERT_EQ_DBL(
		bvn_unit_convert_factor(
			BVN_UNIT_NO_PREFIX(bu_newton),
			kg_m_s2,
			&ok, &req_aff),
		1.0, 1e-15, "N → k~g·m/s² = 1.0");
	ASSERT_TRUE(ok, "N → k~g·m/s² ok");

	ASSERT_EQ_DBL(
		bvn_unit_convert_factor(
			BVN_UNIT_SI(bu_joule, si_kilo),
			BVN_UNIT_NO_PREFIX(bu_joule),
			&ok, &req_aff),
		1e3, 1e-10, "kJ → J = 1e3");
	ASSERT_TRUE(ok, "kJ → J ok");

	bvn_unit_convert_factor(
		BVN_UNIT_NO_PREFIX(bu_meter),
		BVN_UNIT_NO_PREFIX(bu_second),
		&ok, &req_aff);
	ASSERT_TRUE(!ok, "m → s incompatible");

	bvn_unit_convert_factor(
		BVN_UNIT_NO_PREFIX(bu_celsius),
		BVN_UNIT_NO_PREFIX(bu_kelvin),
		&ok, &req_aff);
	ASSERT_TRUE(!ok, "°C → K not simple factor");

	ASSERT_EQ_DBL(
		bvn_unit_convert_factor(
			BVN_UNIT_NO_PREFIX(bu_minute),
			BVN_UNIT_NO_PREFIX(bu_second),
			&ok, &req_aff),
		60.0, 1e-10, "min → s = 60");
	ASSERT_TRUE(ok, "min → s ok");

	ASSERT_EQ_DBL(
		bvn_unit_convert_factor(
			BVN_UNIT_NO_PREFIX(bu_hour),
			BVN_UNIT_NO_PREFIX(bu_second),
			&ok, &req_aff),
		3600.0, 1e-10, "h → s = 3600");
	ASSERT_TRUE(ok, "h → s ok");

	bvn_unit_convert_factor(
		BVN_UNIT_NO_PREFIX(bu_liter),
		BVN_UNIT_NO_PREFIX(bu_meter),
		&ok, &req_aff);
	ASSERT_TRUE(!ok, "L → m NOT compatible");

	value_unit_t kg_per_m_s2 = {
		.num_components = 3,
		.components = {
			{ bu_gram,   exp_linear,      {prefix_si, .id.si=si_kilo} },
			{ bu_meter,  exp_neg_linear,  {prefix_si, .id.si=si_none} },
			{ bu_second, exp_neg_square,  {prefix_si, .id.si=si_none} }
		}
	};
	ASSERT_EQ_DBL(
		bvn_unit_convert_factor(
			BVN_UNIT_NO_PREFIX(bu_pascal),
			kg_per_m_s2,
			&ok, &req_aff),
		1.0, 1e-15, "Pa → k~g/(m·s²) = 1.0");
	ASSERT_TRUE(ok, "Pa → k~g/(m·s²) ok");
}

static void test_unit_reduce(void)
{
	printf("  unit_reduce...\n");
	double scale;
	bool overflow = false;

	value_unit_t m1 = BVN_UNIT_NO_PREFIX(bu_meter);
	value_unit_t r1 = bvn_unit_reduce(m1, &scale, &overflow);
	ASSERT_EQ_INT((int64_t)r1.num_components, 1, "m reduce → 1 component");
	ASSERT_EQ_INT((int64_t)r1.components[0].base, (int64_t)bu_meter, "m reduce → meter");
	ASSERT_EQ_INT((int64_t)r1.components[0].exponent, (int64_t)exp_linear, "m reduce → exp_linear");
	ASSERT_EQ_DBL(scale, 1.0, 1e-15, "m reduce → scale 1");

	value_unit_t m_m = {
		.num_components = 2,
		.components = {
			{ bu_meter, exp_linear, {prefix_si, .id.si=si_none} },
			{ bu_meter, exp_linear, {prefix_si, .id.si=si_none} }
		}
	};
	value_unit_t r2 = bvn_unit_reduce(m_m, &scale, &overflow);
	ASSERT_EQ_INT((int64_t)r2.num_components, 1, "m·m reduce → 1 component");
	ASSERT_EQ_INT((int64_t)r2.components[0].base, (int64_t)bu_meter, "m·m reduce → meter");
	ASSERT_EQ_INT((int64_t)r2.components[0].exponent, (int64_t)exp_square, "m·m reduce → exp_square");
	ASSERT_EQ_DBL(scale, 1.0, 1e-15, "m·m reduce → scale 1");

	value_unit_t km_cm = {
		.num_components = 2,
		.components = {
			{ bu_meter, exp_linear, {prefix_si, .id.si=si_kilo}  },
			{ bu_meter, exp_linear, {prefix_si, .id.si=si_centi} }
		}
	};
	value_unit_t r3 = bvn_unit_reduce(km_cm, &scale, &overflow);
	ASSERT_EQ_INT((int64_t)r3.num_components, 1, "k~m·cm reduce → 1 component");
	ASSERT_EQ_INT((int64_t)r3.components[0].base, (int64_t)bu_meter, "k~m·cm reduce → meter");
	ASSERT_EQ_INT((int64_t)r3.components[0].exponent, (int64_t)exp_square, "k~m·cm reduce → exp_square");
	ASSERT_EQ_DBL(scale, 10.0, 1e-10, "k~m·cm reduce → scale 10");

	value_unit_t m_s_s = {
		.num_components = 3,
		.components = {
			{ bu_meter,  exp_linear,     {prefix_si, .id.si=si_none} },
			{ bu_second, exp_neg_linear, {prefix_si, .id.si=si_none} },
			{ bu_second, exp_linear,     {prefix_si, .id.si=si_none} }
		}
	};
	value_unit_t r4 = bvn_unit_reduce(m_s_s, &scale, &overflow);
	ASSERT_EQ_INT((int64_t)r4.num_components, 1, "m/s·s reduce → 1 component");
	ASSERT_EQ_INT((int64_t)r4.components[0].base, (int64_t)bu_meter, "m/s·s reduce → meter");
	ASSERT_EQ_DBL(scale, 1.0, 1e-15, "m/s·s reduce → scale 1");

	value_unit_t m2_s2_s2 = {
		.num_components = 3,
		.components = {
			{ bu_meter,  exp_square,      {prefix_si, .id.si=si_none} },
			{ bu_second, exp_neg_square,  {prefix_si, .id.si=si_none} },
			{ bu_second, exp_square,      {prefix_si, .id.si=si_none} }
		}
	};
	value_unit_t r5 = bvn_unit_reduce(m2_s2_s2, &scale, &overflow);
	ASSERT_EQ_INT((int64_t)r5.num_components, 1, "m²/s²·s² reduce → 1 component");
	ASSERT_EQ_INT((int64_t)r5.components[0].base, (int64_t)bu_meter, "m²/s²·s² reduce → meter");
	ASSERT_EQ_INT((int64_t)r5.components[0].exponent, (int64_t)exp_square, "m²/s²·s² reduce → exp_square");
	ASSERT_EQ_DBL(scale, 1.0, 1e-15, "m²/s²·s² reduce → scale 1");

	value_unit_t g_g = {
		.num_components = 2,
		.components = {
			{ bu_gram, exp_linear, {prefix_si, .id.si=si_none} },
			{ bu_gram, exp_linear, {prefix_si, .id.si=si_none} }
		}
	};
	value_unit_t r6 = bvn_unit_reduce(g_g, &scale, &overflow);
	ASSERT_EQ_INT((int64_t)r6.num_components, 1, "g·g reduce → 1 component");
	ASSERT_EQ_INT((int64_t)r6.components[0].exponent, (int64_t)exp_square, "g·g reduce → exp_square");

	value_unit_t kg_g = {
		.num_components = 2,
		.components = {
			{ bu_gram, exp_linear, {prefix_si, .id.si=si_kilo} },
			{ bu_gram, exp_linear, {prefix_si, .id.si=si_none} }
		}
	};
	value_unit_t r7 = bvn_unit_reduce(kg_g, &scale, &overflow);
	ASSERT_EQ_INT((int64_t)r7.num_components, 1, "k~g·g reduce → 1 component");
	ASSERT_EQ_INT((int64_t)r7.components[0].exponent, (int64_t)exp_square, "k~g·g reduce → exp_square");
	ASSERT_EQ_DBL(scale, 1e3, 1e-10, "k~g·g reduce → scale 1e3");

	value_unit_t m_sinv_s = {
		.num_components = 3,
		.components = {
			{ bu_meter,  exp_linear,      {prefix_si, .id.si=si_none} },
			{ bu_second, exp_neg_linear,  {prefix_si, .id.si=si_none} },
			{ bu_second, exp_linear,      {prefix_si, .id.si=si_none} }
		}
	};
	value_unit_t r8 = bvn_unit_reduce(m_sinv_s, &scale, &overflow);
	ASSERT_EQ_INT((int64_t)r8.num_components, 1, "m·s⁻¹·s reduce → 1 comp");
	ASSERT_EQ_INT((int64_t)r8.components[0].base, (int64_t)bu_meter, "m·s⁻¹·s reduce → meter");
	ASSERT_EQ_DBL(scale, 1.0, 1e-15, "m·s⁻¹·s reduce → scale 1");
}

/* m·m⁻¹ — full SI cancellation, no info units involved */
static void test_unit_reduce_full_cancel_si(void)
{
	printf("  unit_reduce full SI cancellation (m/m)...\n");
	double scale;
	bool overflow = false;

	value_unit_t m_minv = {
		.num_components = 2,
		.components = {
			{ bu_meter, exp_linear,     {prefix_si, .id.si=si_none} },
			{ bu_meter, exp_neg_linear, {prefix_si, .id.si=si_none} }
		}
	};
	value_unit_t r = bvn_unit_reduce(m_minv, &scale, &overflow);
	ASSERT_EQ_INT((int64_t)r.num_components, 0,
	              "m·m⁻¹ reduce → 0 components (full cancel)");
	ASSERT_EQ_DBL(scale, 1.0, 1e-15, "m·m⁻¹ reduce → scale 1.0");
	ASSERT_TRUE(!overflow, "m·m⁻¹ reduce → no overflow");
}

static void test_value_conversion_examples(void)
{
	printf("  value conversion examples...\n");
	bool aff; double off; bool si_ok = true;

	value_unit_t km = BVN_UNIT_SI(bu_meter, si_kilo);
	double f_km = bvn_unit_to_si_factor(km, &aff, &off, &si_ok);
	ASSERT_EQ_DBL(5.0 * f_km, 5000.0, 1e-10, "5 km = 5000 m");

	value_unit_t kg_m_s2 = {
		.num_components = 3,
		.components = {
			{ bu_gram,   exp_linear,     {prefix_si, .id.si=si_kilo} },
			{ bu_meter,  exp_linear,     {prefix_si, .id.si=si_none} },
			{ bu_second, exp_neg_square, {prefix_si, .id.si=si_none} }
		}
	};
	double f_force = bvn_unit_to_si_factor(kg_m_s2, &aff, &off, &si_ok);
	ASSERT_EQ_DBL(9.81 * f_force, 9.81, 1e-10, "9.81 k~g·m/s² = 9.81 N");

	double f_min = bvn_unit_to_si_factor(BVN_UNIT_NO_PREFIX(bu_minute), &aff, &off, &si_ok);
	ASSERT_EQ_DBL(30.0 * f_min, 1800.0, 1e-10, "30 min = 1800 s");

	double f_hr = bvn_unit_to_si_factor(BVN_UNIT_NO_PREFIX(bu_hour), &aff, &off, &si_ok);
	ASSERT_EQ_DBL(1.5 * f_hr, 5400.0, 1e-10, "1.5 h = 5400 s");

	double f_L = bvn_unit_to_si_factor(BVN_UNIT_NO_PREFIX(bu_liter), &aff, &off, &si_ok);
	ASSERT_EQ_DBL(5.0 * f_L, 0.005, 1e-15, "5 L = 0.005 m³");

	double f_C = bvn_unit_to_si_factor(BVN_UNIT_NO_PREFIX(bu_celsius), &aff, &off, &si_ok);
	ASSERT_TRUE(aff, "°C is affine");
	ASSERT_EQ_DBL(23.5 * f_C + off, 296.65, 1e-10, "23.5 °C = 296.65 K");

	double f_Pa = bvn_unit_to_si_factor(BVN_UNIT_NO_PREFIX(bu_pascal), &aff, &off, &si_ok);
	ASSERT_EQ_DBL(101325.0 * f_Pa, 101325.0, 1e-10, "101325 Pa = 101325 SI");

	value_unit_t kPa = BVN_UNIT_SI(bu_pascal, si_kilo);
	double f_kPa = bvn_unit_to_si_factor(kPa, &aff, &off, &si_ok);
	ASSERT_EQ_DBL(100.0 * f_kPa, 1e5, 1e-10, "100 kPa = 1e5 SI");

	value_unit_t kHz = BVN_UNIT_SI(bu_hertz, si_kilo);
	double f_kHz = bvn_unit_to_si_factor(kHz, &aff, &off, &si_ok);
	ASSERT_EQ_DBL(2.4 * f_kHz, 2400.0, 1e-10, "2.4 kHz = 2400 SI");

	value_unit_t KiB = BVN_UNIT_IEC(bu_byte, iec_kibi);
	double f_KiB = bvn_unit_to_si_factor(KiB, &aff, &off, &si_ok);
	ASSERT_EQ_DBL(1024.0 * f_KiB, 1048576.0, 1e5, "1024 Ki~B in bytes");

	double f_day = bvn_unit_to_si_factor(BVN_UNIT_NO_PREFIX(bu_day), &aff, &off, &si_ok);
	ASSERT_EQ_DBL(1.0 * f_day, 86400.0, 1e-10, "1 d = 86400 s");

	value_unit_t kJ = BVN_UNIT_SI(bu_joule, si_kilo);
	double f_kJ = bvn_unit_to_si_factor(kJ, &aff, &off, &si_ok);
	ASSERT_EQ_DBL(100.0 * f_kJ, 1e5, 1e-10, "100 kJ = 1e5 SI");
}

static void test_parse_and_factor(void)
{
	printf("  parse + si_factor integration...\n");
	bool aff, uok; double off; bool si_ok = true;

	value_unit_t u1 = bvn_parse_unit((const uint8_t*)"m/s²", &uok);
	ASSERT_TRUE(uok, "m/s² parses ok");
	double f1 = bvn_unit_to_si_factor(u1, &aff, &off, &si_ok);
	ASSERT_EQ_DBL(f1, 1.0, 1e-15, "m/s² → 1.0");

	value_unit_t u2 = bvn_parse_unit((const uint8_t*)"k~m/s", &uok);
	ASSERT_TRUE(uok, "k~m/s parses ok");
	double f2 = bvn_unit_to_si_factor(u2, &aff, &off, &si_ok);
	ASSERT_EQ_DBL(f2, 1e3, 1e-10, "k~m/s → 1e3");

	value_unit_t u3 = bvn_parse_unit((const uint8_t*)"k~g·m/s²", &uok);
	ASSERT_TRUE(uok, "k~g·m/s² parses ok");
	double f3 = bvn_unit_to_si_factor(u3, &aff, &off, &si_ok);
	ASSERT_EQ_DBL(f3, 1.0, 1e-15, "k~g·m/s² → 1.0");

	value_unit_t u4 = bvn_parse_unit((const uint8_t*)"k~g/m³", &uok);
	ASSERT_TRUE(uok, "k~g/m³ parses ok");
	double f4 = bvn_unit_to_si_factor(u4, &aff, &off, &si_ok);
	ASSERT_EQ_DBL(f4, 1.0, 1e-15, "k~g/m³ → 1.0");

	value_unit_t u5 = bvn_parse_unit((const uint8_t*)"L/min", &uok);
	ASSERT_TRUE(uok, "L/min parses ok");
	double f5 = bvn_unit_to_si_factor(u5, &aff, &off, &si_ok);
	ASSERT_EQ_DBL(f5, 1e-3 / 60.0, 1e-18, "L/min → 1e-3/60");

	value_unit_t u6 = bvn_parse_unit((const uint8_t*)"k~J", &uok);
	ASSERT_TRUE(uok, "k~J parses ok");
	double f6 = bvn_unit_to_si_factor(u6, &aff, &off, &si_ok);
	ASSERT_EQ_DBL(f6, 1e3, 1e-10, "k~J → 1e3");

	value_unit_t u7 = bvn_parse_unit((const uint8_t*)"Ti~B", &uok);
	ASSERT_TRUE(uok, "Ti~B parses ok");
	double f7 = bvn_unit_to_si_factor(u7, &aff, &off, &si_ok);
	ASSERT_EQ_DBL(f7, 1099511627776.0, 1e5, "Ti~B → 2^40");

	value_unit_t u8 = bvn_parse_unit((const uint8_t*)"°C", &uok);
	ASSERT_TRUE(uok, "°C parses ok");
	double f8 = bvn_unit_to_si_factor(u8, &aff, &off, &si_ok);
	ASSERT_TRUE(aff, "°C is affine");
	ASSERT_EQ_DBL(f8, 1.0, 1e-15, "°C factor = 1");
	ASSERT_EQ_DBL(off, 273.15, 1e-10, "°C offset = 273.15");

	value_unit_t u9 = bvn_parse_unit((const uint8_t*)"Pa", &uok);
	ASSERT_TRUE(uok, "Pa parses ok");
	double f9 = bvn_unit_to_si_factor(u9, &aff, &off, &si_ok);
	ASSERT_EQ_DBL(f9, 1.0, 1e-15, "Pa → 1.0");

	value_unit_t u10 = bvn_parse_unit((const uint8_t*)"k~Pa", &uok);
	ASSERT_TRUE(uok, "k~Pa parses ok");
	double f10 = bvn_unit_to_si_factor(u10, &aff, &off, &si_ok);
	ASSERT_EQ_DBL(f10, 1e3, 1e-10, "k~Pa → 1e3");

	value_unit_t u11 = bvn_parse_unit((const uint8_t*)"m*s", &uok);
	ASSERT_TRUE(uok, "m*s parses ok");
	double f11 = bvn_unit_to_si_factor(u11, &aff, &off, &si_ok);
	ASSERT_EQ_DBL(f11, 1.0, 1e-15, "m*s → 1.0");

	value_unit_t u12 = bvn_parse_unit((const uint8_t*)"V/m", &uok);
	ASSERT_TRUE(uok, "V/m parses ok");
	double f12 = bvn_unit_to_si_factor(u12, &aff, &off, &si_ok);
	ASSERT_EQ_DBL(f12, 1.0, 1e-15, "V/m → 1.0");
}

static void test_all_derived_dimensions(void)
{
	printf("  all derived SI unit dimensions...\n");
	int32_t dims[bvn_si_dim_count];

	bvn_unit_dimension_vector(BVN_UNIT_NO_PREFIX(bu_hertz), dims);
	ASSERT_EQ_INT((int64_t)dims[bvn_si_dim_second], -1, "Hz → s⁻¹");

	bvn_unit_dimension_vector(BVN_UNIT_NO_PREFIX(bu_newton), dims);
	ASSERT_EQ_INT((int64_t)dims[bvn_si_dim_meter],    1,  "N → m¹");
	ASSERT_EQ_INT((int64_t)dims[bvn_si_dim_kilogram], 1,  "N → kg¹");
	ASSERT_EQ_INT((int64_t)dims[bvn_si_dim_second],  -2,  "N → s⁻²");

	bvn_unit_dimension_vector(BVN_UNIT_NO_PREFIX(bu_pascal), dims);
	ASSERT_EQ_INT((int64_t)dims[bvn_si_dim_meter],   -1,  "Pa → m⁻¹");
	ASSERT_EQ_INT((int64_t)dims[bvn_si_dim_kilogram], 1,  "Pa → kg¹");
	ASSERT_EQ_INT((int64_t)dims[bvn_si_dim_second],  -2,  "Pa → s⁻²");

	bvn_unit_dimension_vector(BVN_UNIT_NO_PREFIX(bu_joule), dims);
	ASSERT_EQ_INT((int64_t)dims[bvn_si_dim_meter],    2,  "J → m²");
	ASSERT_EQ_INT((int64_t)dims[bvn_si_dim_kilogram], 1,  "J → kg¹");
	ASSERT_EQ_INT((int64_t)dims[bvn_si_dim_second],  -2,  "J → s⁻²");

	bvn_unit_dimension_vector(BVN_UNIT_NO_PREFIX(bu_watt), dims);
	ASSERT_EQ_INT((int64_t)dims[bvn_si_dim_meter],    2,  "W → m²");
	ASSERT_EQ_INT((int64_t)dims[bvn_si_dim_kilogram], 1,  "W → kg¹");
	ASSERT_EQ_INT((int64_t)dims[bvn_si_dim_second],  -3,  "W → s⁻³");

	bvn_unit_dimension_vector(BVN_UNIT_NO_PREFIX(bu_volt), dims);
	ASSERT_EQ_INT((int64_t)dims[bvn_si_dim_meter],    2,  "V → m²");
	ASSERT_EQ_INT((int64_t)dims[bvn_si_dim_kilogram], 1,  "V → kg¹");
	ASSERT_EQ_INT((int64_t)dims[bvn_si_dim_second],  -3,  "V → s⁻³");
	ASSERT_EQ_INT((int64_t)dims[bvn_si_dim_ampere],  -1,  "V → A⁻¹");

	bvn_unit_dimension_vector(BVN_UNIT_NO_PREFIX(bu_ohm), dims);
	ASSERT_EQ_INT((int64_t)dims[bvn_si_dim_ampere],  -2,  "Ω → A⁻²");

	bvn_unit_dimension_vector(BVN_UNIT_NO_PREFIX(bu_farad), dims);
	ASSERT_EQ_INT((int64_t)dims[bvn_si_dim_meter],   -2,  "F → m⁻²");
	ASSERT_EQ_INT((int64_t)dims[bvn_si_dim_kilogram],-1,  "F → kg⁻¹");
	ASSERT_EQ_INT((int64_t)dims[bvn_si_dim_second],   4,  "F → s⁴");
	ASSERT_EQ_INT((int64_t)dims[bvn_si_dim_ampere],   2,  "F → A²");

	bvn_unit_dimension_vector(BVN_UNIT_NO_PREFIX(bu_coulomb), dims);
	ASSERT_EQ_INT((int64_t)dims[bvn_si_dim_second],  1,  "C → s¹");
	ASSERT_EQ_INT((int64_t)dims[bvn_si_dim_ampere],  1,  "C → A¹");

	bvn_unit_dimension_vector(BVN_UNIT_NO_PREFIX(bu_siemens), dims);
	ASSERT_EQ_INT((int64_t)dims[bvn_si_dim_kilogram],-1,  "S → kg⁻¹");
	ASSERT_EQ_INT((int64_t)dims[bvn_si_dim_meter],   -2,  "S → m⁻²");
	ASSERT_EQ_INT((int64_t)dims[bvn_si_dim_second],   3,  "S → s³");
	ASSERT_EQ_INT((int64_t)dims[bvn_si_dim_ampere],   2,  "S → A²");

	bvn_unit_dimension_vector(BVN_UNIT_NO_PREFIX(bu_weber), dims);
	ASSERT_EQ_INT((int64_t)dims[bvn_si_dim_ampere],  -1,  "Wb → A⁻¹");

	bvn_unit_dimension_vector(BVN_UNIT_NO_PREFIX(bu_tesla), dims);
	ASSERT_EQ_INT((int64_t)dims[bvn_si_dim_meter],    0,  "T → m⁰");
	ASSERT_EQ_INT((int64_t)dims[bvn_si_dim_kilogram], 1,  "T → kg¹");
	ASSERT_EQ_INT((int64_t)dims[bvn_si_dim_second],  -2,  "T → s⁻²");
	ASSERT_EQ_INT((int64_t)dims[bvn_si_dim_ampere],  -1,  "T → A⁻¹");

	bvn_unit_dimension_vector(BVN_UNIT_NO_PREFIX(bu_henry), dims);
	ASSERT_EQ_INT((int64_t)dims[bvn_si_dim_ampere],  -2,  "H → A⁻²");

	bvn_unit_dimension_vector(BVN_UNIT_NO_PREFIX(bu_lumen), dims);
	ASSERT_EQ_INT((int64_t)dims[bvn_si_dim_candela], 1,  "lm → cd¹");

	bvn_unit_dimension_vector(BVN_UNIT_NO_PREFIX(bu_lux), dims);
	ASSERT_EQ_INT((int64_t)dims[bvn_si_dim_meter],   -2,  "lx → m⁻²");
	ASSERT_EQ_INT((int64_t)dims[bvn_si_dim_candela],  1,  "lx → cd¹");

	bvn_unit_dimension_vector(BVN_UNIT_NO_PREFIX(bu_becquerel), dims);
	ASSERT_EQ_INT((int64_t)dims[bvn_si_dim_second],  -1,  "Bq → s⁻¹");

	bvn_unit_dimension_vector(BVN_UNIT_NO_PREFIX(bu_gray), dims);
	ASSERT_EQ_INT((int64_t)dims[bvn_si_dim_meter],    2,  "Gy → m²");
	ASSERT_EQ_INT((int64_t)dims[bvn_si_dim_second],  -2,  "Gy → s⁻²");
	ASSERT_EQ_INT((int64_t)dims[bvn_si_dim_kilogram], 0,  "Gy → kg⁰");

	bvn_unit_dimension_vector(BVN_UNIT_NO_PREFIX(bu_sievert), dims);
	ASSERT_EQ_INT((int64_t)dims[bvn_si_dim_meter],    2,  "Sv → m²");
	ASSERT_EQ_INT((int64_t)dims[bvn_si_dim_second],  -2,  "Sv → s⁻²");

	bvn_unit_dimension_vector(BVN_UNIT_NO_PREFIX(bu_katal), dims);
	ASSERT_EQ_INT((int64_t)dims[bvn_si_dim_mol],      1,  "kat → mol¹");
	ASSERT_EQ_INT((int64_t)dims[bvn_si_dim_second],  -1,  "kat → s⁻¹");
}

static void test_unit_reduce_iec(void)
{
	printf("  unit_reduce IEC prefixes...\n");
	double scale;
	bool overflow = false;

	value_unit_t KiB_KiB = {
		.num_components = 2,
		.components = {
			{ bu_byte, exp_linear,     {prefix_iec, .id.iec=iec_kibi} },
			{ bu_byte, exp_neg_linear, {prefix_iec, .id.iec=iec_kibi} }
		}
	};
	value_unit_t r1 = bvn_unit_reduce(KiB_KiB, &scale, &overflow);
	ASSERT_EQ_INT((int64_t)r1.num_components, 0,
	              "Ki~B/Ki~B reduces to 0 components (dimensionless cancellation)");
	ASSERT_EQ_DBL(scale, 1.0, 1e-10,
	              "Ki~B/Ki~B scale = 1.0");

	value_unit_t MiB_per_KiB = {
		.num_components = 2,
		.components = {
			{ bu_byte, exp_linear,     {prefix_iec, .id.iec=iec_mebi} },
			{ bu_byte, exp_neg_linear, {prefix_iec, .id.iec=iec_kibi} }
		}
	};
	value_unit_t r2 = bvn_unit_reduce(MiB_per_KiB, &scale, &overflow);
	ASSERT_EQ_INT((int64_t)r2.num_components, 0,
	              "Mi~B/Ki~B reduces to 0 components (byte dimension cancels)");
	ASSERT_EQ_DBL(scale, 1024.0, 1e-6,
	              "Mi~B/Ki~B scale = 1024 (2^10), not 1000 (SI)");

	value_unit_t TiB2_per_GiB = {
		.num_components = 2,
		.components = {
			{ bu_byte, exp_square,     {prefix_iec, .id.iec=iec_tebi} },
			{ bu_byte, exp_neg_linear, {prefix_iec, .id.iec=iec_gibi} }
		}
	};
	value_unit_t r3 = bvn_unit_reduce(TiB2_per_GiB, &scale, &overflow);
	ASSERT_EQ_INT((int64_t)r3.num_components, 1,
	              "Ti~B²/Gi~B reduces to 1 component");
	ASSERT_EQ_DBL(scale, 1125899906842624.0, 1e6,
	              "Ti~B²/Gi~B scale = 2^50");

	value_unit_t km_KiB = {
		.num_components = 2,
		.components = {
			{ bu_meter, exp_linear, {prefix_si,  .id.si =si_kilo}  },
			{ bu_byte,  exp_linear, {prefix_iec, .id.iec=iec_kibi} }
		}
	};
	value_unit_t r4 = bvn_unit_reduce(km_KiB, &scale, &overflow);
	ASSERT_EQ_INT((int64_t)r4.num_components, 2,
	              "k~m · Ki~B reduces to 2 components");
	ASSERT_EQ_DBL(scale, 1000.0 * 1024.0, 1e-6,
	              "k~m · Ki~B scale = 1e3 * 1024");
}

static void test_si_factor_affine_nonlinear(void)
{
	printf("  si_factor affine unit with non-linear exponent...\n");
	bool aff; double off; bool si_ok;

	si_ok = true;
	bvn_unit_to_si_factor(BVN_UNIT_NO_PREFIX(bu_celsius), &aff, &off, &si_ok);
	ASSERT_TRUE(si_ok,  "°C^1 ok=true");
	ASSERT_TRUE(aff,    "°C^1 is_affine=true");

	value_unit_t degC2 = BVN_UNIT_SI_EXP(bu_celsius, si_none, exp_square);
	si_ok = true;
	bvn_unit_to_si_factor(degC2, &aff, &off, &si_ok);
	ASSERT_TRUE(!si_ok, "°C² ok=false (affine with non-linear exponent)");

	value_unit_t degC_neg = BVN_UNIT_SI_EXP(bu_celsius, si_none, exp_neg_linear);
	si_ok = true;
	bvn_unit_to_si_factor(degC_neg, &aff, &off, &si_ok);
	ASSERT_TRUE(!si_ok, "°C⁻¹ ok=false (affine with negative exponent)");

	value_unit_t K2 = BVN_UNIT_SI_EXP(bu_kelvin, si_none, exp_square);
	si_ok = true;
	bvn_unit_to_si_factor(K2, &aff, &off, &si_ok);
	ASSERT_TRUE(si_ok,  "K² ok=true (not affine)");
	ASSERT_TRUE(!aff,   "K² is_affine=false");
}

static void test_convert_factor_error_kinds(void)
{
	printf("  convert_factor failure mode distinction...\n");
	bool ok; bool req_aff;

	bvn_unit_convert_factor(
		BVN_UNIT_NO_PREFIX(bu_meter),
		BVN_UNIT_NO_PREFIX(bu_second),
		&ok, &req_aff);
	ASSERT_TRUE(!ok,      "m→s: ok=false");
	ASSERT_TRUE(!req_aff, "m→s: requires_affine=false (incompatible dims)");

	bvn_unit_convert_factor(
		BVN_UNIT_NO_PREFIX(bu_celsius),
		BVN_UNIT_NO_PREFIX(bu_kelvin),
		&ok, &req_aff);
	ASSERT_TRUE(!ok,     "°C→K: ok=false (needs additive offset)");
	ASSERT_TRUE(req_aff, "°C→K: requires_affine=true");

	double f = bvn_unit_convert_factor(
		BVN_UNIT_NO_PREFIX(bu_celsius),
		BVN_UNIT_NO_PREFIX(bu_celsius),
		&ok, &req_aff);
	ASSERT_TRUE(ok,      "°C→°C: ok=true");
	ASSERT_TRUE(req_aff, "°C→°C: requires_affine=true (same offset, factor valid)");
	ASSERT_EQ_DBL(f, 1.0, 1e-15, "°C→°C factor = 1.0");

	f = bvn_unit_convert_factor(
		BVN_UNIT_SI(bu_meter, si_kilo),
		BVN_UNIT_NO_PREFIX(bu_meter),
		&ok, &req_aff);
	ASSERT_TRUE(ok,       "km→m: ok=true");
	ASSERT_TRUE(!req_aff, "km→m: requires_affine=false");
	ASSERT_EQ_DBL(f, 1e3, 1e-10, "km→m factor = 1e3");
}

/* Prefixed affine → non-affine: m-°C → K must fail (different offsets) */
static void test_convert_factor_prefixed_affine_to_nonaffine(void)
{
	printf("  convert_factor prefixed affine → K (fix: must fail)...\n");
	bool ok; bool req_aff;

	value_unit_t mdegC = BVN_UNIT_SI(bu_celsius, si_milli);
	value_unit_t kelvin = BVN_UNIT_NO_PREFIX(bu_kelvin);

	bvn_unit_convert_factor(mdegC, kelvin, &ok, &req_aff);
	ASSERT_TRUE(!ok,     "m-°C → K: ok=false (affine to non-affine)");
	ASSERT_TRUE(req_aff, "m-°C → K: requires_affine=true");
}

static void test_si_factor_affine_compound(void)
{
	printf("  si_factor compound affine (two linear affine components)...\n");
	bool aff; double off; bool ok;

	value_unit_t degC_degC = {
		.num_components = 2,
		.components = {
			{ bu_celsius, exp_linear, {prefix_si, .id.si=si_none} },
			{ bu_celsius, exp_linear, {prefix_si, .id.si=si_none} }
		}
	};
	ok = true;
	bvn_unit_to_si_factor(degC_degC, &aff, &off, &ok);
	ASSERT_TRUE(!ok, "degC*degC: ok=false (two affine linear components)");
}

static void test_unit_reduce_exponent_overflow(void)
{
	printf("  unit_reduce exponent overflow...\n");
	double scale;
	bool overflow = false;

	value_unit_t m5_m5 = {
		.num_components = 2,
		.components = {
			{ bu_meter, exp_quintic, {prefix_si, .id.si=si_none} },
			{ bu_meter, exp_quintic, {prefix_si, .id.si=si_none} }
		}
	};
	value_unit_t r = bvn_unit_reduce(m5_m5, &scale, &overflow);
	bool bad_exp_invalid = false;
	for (uint32_t i = 0; i < r.num_components; i++) {
		if (r.components[i].base == bu_meter &&
		    r.components[i].exponent == exp_invalid)
			bad_exp_invalid = true;
	}
	ASSERT_TRUE(!bad_exp_invalid,
	            "m^5*m^5 reduce: no component with exp_invalid (overflow → scale)");

	value_unit_t m_nine = {
		.num_components = 3,
		.components = {
			{ bu_meter, exp_cubic,   {prefix_si, .id.si=si_none} },
			{ bu_meter, exp_cubic,   {prefix_si, .id.si=si_none} },
			{ bu_meter, exp_cubic,   {prefix_si, .id.si=si_none} }
		}
	};
	value_unit_t r2 = bvn_unit_reduce(m_nine, &scale, &overflow);
	ASSERT_EQ_INT((int64_t)r2.num_components, 1,
	              "m^3*m^3*m^3 (=m^9) reduce → 1 component");
	ASSERT_TRUE(r2.components[0].exponent == exp_nonic,
	            "m^9 reduce → exp_nonic");
	ASSERT_EQ_DBL(scale, 1.0, 1e-15, "m^9 reduce → scale 1.0");
}

static void test_si_factor_degree(void)
{
	printf("  si_factor degree...\n");
	bool aff; double off; bool ok = true;
	double f = bvn_unit_to_si_factor(BVN_UNIT_NO_PREFIX(bu_degree),
	                                  &aff, &off, &ok);
	ASSERT_TRUE(ok,   "°  ok=true");
	ASSERT_TRUE(!aff, "°  not affine");
	ASSERT_EQ_DBL(f, 3.14159265358979323846 / 180.0, 1e-15, "° → π/180");

	value_unit_t mdeg = BVN_UNIT_SI(bu_degree, si_milli);
	ok = true;
	f = bvn_unit_to_si_factor(mdeg, &aff, &off, &ok);
	ASSERT_TRUE(ok, "m-° ok=true");
	ASSERT_EQ_DBL(f, 1e-3 * 3.14159265358979323846 / 180.0, 1e-18, "m-° → 1e-3·π/180");
}

static void test_units_compatible_info(void)
{
	printf("  units_compatible info units...\n");

	ASSERT_TRUE(!bvn_units_compatible(
		BVN_UNIT_NO_PREFIX(bu_bit),
		BVN_UNIT_NO_PREFIX(bu_byte)),
		"bit NOT compatible with byte");

	ASSERT_TRUE(bvn_units_compatible(
		BVN_UNIT_NO_PREFIX(bu_bit),
		BVN_UNIT_NO_PREFIX(bu_bit)),
		"bit compatible with bit");

	ASSERT_TRUE(bvn_units_compatible(
		BVN_UNIT_NO_PREFIX(bu_byte),
		BVN_UNIT_NO_PREFIX(bu_byte)),
		"byte compatible with byte");

	ASSERT_TRUE(!bvn_units_compatible(
		BVN_UNIT_NO_PREFIX(bu_byte),
		BVN_UNIT_NONE),
		"byte NOT compatible with dimensionless");

	ASSERT_TRUE(!bvn_units_compatible(
		BVN_UNIT_NO_PREFIX(bu_bit),
		BVN_UNIT_NONE),
		"bit NOT compatible with dimensionless");

	ASSERT_TRUE(bvn_units_compatible(
		BVN_UNIT_IEC(bu_byte, iec_kibi),
		BVN_UNIT_IEC(bu_byte, iec_mebi)),
		"Ki~B compatible with Mi~B (both byte, scale differs)");

	ASSERT_TRUE(!bvn_units_compatible(
		BVN_UNIT_IEC(bu_bit, iec_kibi),
		BVN_UNIT_IEC(bu_byte, iec_kibi)),
		"Ki~b NOT compatible with Ki~B");
}

/* exp_invalid in an info-unit component must make the unit incompatible */
static void test_units_compatible_exp_invalid_info(void)
{
	printf("  units_compatible exp_invalid in info unit...\n");

	value_unit_t bad_bit = {
		.num_components = 1,
		.components = {
			{ bu_bit, exp_invalid, {prefix_si, .id.si=si_none} }
		}
	};
	ASSERT_TRUE(!bvn_units_compatible(bad_bit, BVN_UNIT_NO_PREFIX(bu_bit)),
	            "bit with exp_invalid NOT compatible with bit");
	ASSERT_TRUE(!bvn_units_compatible(BVN_UNIT_NO_PREFIX(bu_bit), bad_bit),
	            "bit NOT compatible with bit(exp_invalid) — symmetric");

	value_unit_t bad_byte = {
		.num_components = 1,
		.components = {
			{ bu_byte, exp_invalid, {prefix_si, .id.si=si_none} }
		}
	};
	ASSERT_TRUE(!bvn_units_compatible(bad_byte, BVN_UNIT_NO_PREFIX(bu_byte)),
	            "byte with exp_invalid NOT compatible with byte");
}

/*
 * The id space is blocked, so "not a unit" is no longer one region past the
 * end. There are three shapes of it and a bounds check catches only the last:
 * the dead space below the first block, a hole inside a block that a
 * vocabulary has not grown into yet, and the space above every block. All
 * three must refuse.
 */
/*
 * Every out-parameter is optional, and the same way in every function.
 *
 * It used to be half true. bvn_unit_reduce guarded `overflow` and dereferenced
 * `scale` — one function, two rules. bvn_unit_convert_value guarded `out` while
 * bvn_unit_convert_factor dereferenced both of its. bvn_parse_unit checked its
 * INPUT pointer and not its `ok`. Each of those is a segfault in a caller that
 * had no use for the value, and no way to find out except by trying.
 *
 * A crash cannot be asserted on, so what this pins is the ANSWER: passing NULL
 * must not change what the function returns.
 */
static void test_out_parameters_are_optional(void)
{
	printf("  NULL out-parameters...\n");
	bool pok = false;
	value_unit_t m  = bvn_parse_unit((const uint8_t *)"m", &pok);
	value_unit_t km = bvn_parse_unit((const uint8_t *)"k~m", &pok);
	value_unit_t c  = bvn_parse_unit((const uint8_t *)"\xc2\xb0" "C", &pok);

	bool aff = false, ok = false;
	double off = 0.0;
	double want = bvn_unit_to_si_factor(km, &aff, &off, &ok);
	ASSERT_TRUE(ok && want == 1000.0, "the reference answer");
	ASSERT_EQ_DBL(bvn_unit_to_si_factor(km, NULL, NULL, NULL), want, 0.0,
	              "si_factor: same answer with every out-parameter NULL");
	/* An affine unit exercises the branch that WRITES through them. */
	(void)bvn_unit_to_si_factor(c, NULL, NULL, NULL);

	double scale = 0.0;
	bool ovf = false;
	value_unit_t r1 = bvn_unit_reduce(km, &scale, &ovf);
	value_unit_t r2 = bvn_unit_reduce(km, NULL, NULL);
	ASSERT_TRUE(bvn_unit_equal(r1, r2), "reduce: same unit with both NULL");
	ASSERT_EQ_DBL(scale, 1000.0, 1e-12, "reduce: the reference scale");

	int32_t dims[bvn_si_dim_count];
	ASSERT_TRUE(bvn_unit_dimension_vector(m, dims) ==
	            bvn_unit_dimension_vector(m, NULL),
	            "dimension_vector: NULL dims is the bare predicate");
	ASSERT_TRUE(!bvn_unit_dimension_vector(
	                bvn_parse_unit((const uint8_t *)"$USD", &pok), NULL),
	            "dimension_vector: and still says no for a currency");

	bool ra = false;
	double f = bvn_unit_convert_factor(km, m, &ok, &ra);
	ASSERT_EQ_DBL(bvn_unit_convert_factor(km, m, NULL, NULL), f, 0.0,
	              "convert_factor: same answer with both NULL");
	/* The affine path writes through requires_affine, so cover it too. */
	(void)bvn_unit_convert_factor(c, bvn_parse_unit((const uint8_t *)"K", &pok),
	                              NULL, NULL);

	value_unit_t nf;
	ASSERT_TRUE(bvn_unit_si_normal_form(km, &nf) ==
	            bvn_unit_si_normal_form(km, NULL),
	            "si_normal_form: NULL out is the bare predicate");
	ASSERT_TRUE(bvn_unit_convert_value(1.0, km, m, NULL),
	            "convert_value: NULL out still reports success");

	value_unit_t p = bvn_parse_unit((const uint8_t *)"m/s", NULL);
	ASSERT_TRUE(bvn_unit_equal(p, bvn_parse_unit((const uint8_t *)"m/s", &pok)),
	            "parse_unit: NULL ok parses the same unit");
	value_unit_t bad = bvn_parse_unit((const uint8_t *)"not_a_unit", NULL);
	ASSERT_EQ_INT(bad.num_components, 0,
	              "parse_unit: a refusal with NULL ok is BVN_UNIT_NONE");
	ASSERT_EQ_INT(bvn_parse_unit_n((const uint8_t *)"m/s", 3, NULL).num_components,
	              2, "parse_unit_n: NULL ok is fine too");
}

/*
 * A factor a double cannot hold is a FAILURE, at both ends.
 *
 * Overflow was already refused. Underflow was not, because 0.0 is finite:
 * q~m^100 is 10^-3000, the product underflowed to exactly zero, and the
 * function reported success — so every value in that unit converted to 0 and
 * nothing said it had not. Zero is unreachable for a real unit (every
 * to_si_factor and every prefix is positive and non-zero), so it can only ever
 * mean the product fell off the bottom.
 */
static void test_si_factor_refuses_what_a_double_cannot_hold(void)
{
	printf("  si_factor over- and underflow...\n");
	static const struct { const char *unit; bool want_ok; } cases[] = {
		{ "Q~m^100",     false },   /* 10^3000  -> +inf */
		{ "q~m^100",     false },   /* 10^-3000 -> 0.0  */
		{ "Q~m^9",       true  },   /* 10^270, large but representable */
		{ "q~m^9",       true  },   /* 10^-270, small but representable */
		{ "m^100/s^100", true  },   /* huge exponents, factor exactly 1 */
	};
	for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
		bool pok = false;
		value_unit_t u = bvn_parse_unit((const uint8_t *)cases[i].unit, &pok);
		ASSERT_TRUE(pok, "the extreme unit parses");
		bool aff = false, ok = false;
		double off = 0.0;
		double f = bvn_unit_to_si_factor(u, &aff, &off, &ok);
		ASSERT_TRUE(ok == cases[i].want_ok,
		            "si_factor reports the representability of its answer");
		if (ok) {
			ASSERT_TRUE(f != 0.0 && f == f && f - f == 0.0,
			            "a successful factor is finite and non-zero");
		}
	}
}

static void test_si_factor_undefined_base(void)
{
	printf("  si_factor undefined base...\n");
	static const int UNDEFINED[] = {
		BVN_UNIT_ID_FIRST - 1,                    /* below every block   */
		BVN_UNIT_NATIVE_LAST + 1,                 /* hole in block 10    */
		BVN_PROFILE_UCUM_OPAQUE_LAST + 1,         /* hole in block 20    */
		70 * BVN_UNIT_BLOCK_SIZE,                 /* an unassigned block */
		BVN_CURRENCY_LAST + 1,                    /* hole in block 90    */
		99 * BVN_UNIT_BLOCK_SIZE                  /* above every block   */
	};
	for (size_t i = 0; i < sizeof(UNDEFINED) / sizeof(UNDEFINED[0]); i++) {
		bool aff; double off; bool ok = true;
		value_unit_t bad = {
			.num_components = 1,
			.components = {
				{ (value_base_unit_t)UNDEFINED[i],
				  exp_linear,
				  {prefix_si, .id.si = si_none} }
			}
		};
		bvn_unit_to_si_factor(bad, &aff, &off, &ok);
		ASSERT_TRUE(!ok, "undefined base → ok=false");
		ASSERT_TRUE(!bvn_unit_valid(bad), "undefined base → unit invalid");
	}
}

/* num_components > BVNR_MAX_UNIT_COMPONENTS: extra entries silently ignored */
static void test_si_factor_num_components_overflow(void)
{
	printf("  si_factor num_components > MAX (clamped)...\n");
	bool aff; double off; bool ok = true;

	value_unit_t u;
	u.num_components = BVNR_MAX_UNIT_COMPONENTS + 42;
	for (uint32_t i = 0; i < BVNR_MAX_UNIT_COMPONENTS; i++) {
		u.components[i].base          = bu_meter;
		u.components[i].exponent      = exp_linear;
		u.components[i].prefix.system = prefix_si;
		u.components[i].prefix.id.si  = si_none;
	}
	double f = bvn_unit_to_si_factor(u, &aff, &off, &ok);
	ASSERT_TRUE(ok, "oversized num_components does not crash or set ok=false");
	(void)f;
}

static void test_convert_factor_prefixed_affine(void)
{
	printf("  convert_factor prefixed affine (m-°C → °C)...\n");
	bool ok; bool req_aff;

	value_unit_t mdegC = BVN_UNIT_SI(bu_celsius, si_milli);
	value_unit_t degC  = BVN_UNIT_NO_PREFIX(bu_celsius);

	double f = bvn_unit_convert_factor(mdegC, degC, &ok, &req_aff);
	ASSERT_TRUE(ok,      "m-°C → °C: ok=true (same offset, ratio of scale factors)");
	ASSERT_TRUE(req_aff, "m-°C → °C: requires_affine=true");
	ASSERT_EQ_DBL(f, 1e-3, 1e-15, "m-°C → °C: factor=1e-3");

	f = bvn_unit_convert_factor(degC, degC, &ok, &req_aff);
	ASSERT_TRUE(ok,       "°C → °C: ok=true");
	ASSERT_TRUE(req_aff,  "°C → °C: requires_affine=true");
	ASSERT_EQ_DBL(f, 1.0, 1e-15, "°C → °C: factor=1.0");
}

static void test_unit_reduce_overflow_flag(void)
{
	printf("  unit_reduce overflow flag...\n");
	double scale;
	bool overflow;

	value_unit_t m_nine = {
		.num_components = 3,
		.components = {
			{ bu_meter, exp_cubic, {prefix_si, .id.si=si_none} },
			{ bu_meter, exp_cubic, {prefix_si, .id.si=si_none} },
			{ bu_meter, exp_cubic, {prefix_si, .id.si=si_none} }
		}
	};
	overflow = true;
	bvn_unit_reduce(m_nine, &scale, &overflow);
	ASSERT_TRUE(!overflow, "m^9 reduce: overflow=false (within range)");

	/* Exponent 10 is INSIDE the range. It was refused here for as long as the
	 * reduction kept a ±9 cap that the exponent type had left behind years
	 * earlier, and the refusal was not a rejection: m^5*m^5 reduced to the
	 * DIMENSIONLESS unit with scale 1.0, because metre's SI factor is 1.0 and
	 * the fold left nothing behind. */
	value_unit_t m5_m5 = {
		.num_components = 2,
		.components = {
			{ bu_meter, exp_quintic, {prefix_si, .id.si=si_none} },
			{ bu_meter, exp_quintic, {prefix_si, .id.si=si_none} }
		}
	};
	overflow = false;
	scale    = 1.0;
	value_unit_t m10 = bvn_unit_reduce(m5_m5, &scale, &overflow);
	ASSERT_TRUE(!overflow, "m^5*m^5 reduce: overflow=false (exp 10 is in range)");
	ASSERT_EQ_INT(m10.num_components, 1, "m^5*m^5 reduce: one component");
	ASSERT_EQ_INT((int)m10.components[0].base, (int)bu_meter,
	              "m^5*m^5 reduce: still metres");
	ASSERT_EQ_INT(bvn_exponent_to_int(m10.components[0].exponent), 10,
	              "m^5*m^5 reduce: exponent 10");
	ASSERT_EQ_DBL(scale, 1.0, 1e-12, "m^5*m^5 reduce: no scale folded out");

	value_unit_t g5_g5 = {
		.num_components = 2,
		.components = {
			{ bu_gram, exp_quintic, {prefix_si, .id.si=si_none} },
			{ bu_gram, exp_quintic, {prefix_si, .id.si=si_none} }
		}
	};
	overflow = false;
	scale    = 1.0;
	value_unit_t g10 = bvn_unit_reduce(g5_g5, &scale, &overflow);
	ASSERT_TRUE(!overflow, "g^5*g^5 reduce: overflow=false (exp 10 is in range)");
	ASSERT_EQ_INT(bvn_exponent_to_int(g10.components[0].exponent), 10,
	              "g^5*g^5 reduce: exponent 10");
	ASSERT_EQ_DBL(scale, 1.0, 1e-12,
	              "g^5*g^5 reduce: the gram factor stays IN the unit, not in the scale");

	/* Past the range there is genuinely nothing to return, and the component
	 * IS dropped — which is what *overflow warns about. m^100*m^100 is m^200. */
	value_unit_t m100_m100 = {
		.num_components = 2,
		.components = {
			{ bu_meter, (unit_exponent_t)BVN_EXPONENT_MAX, {prefix_si, .id.si=si_none} },
			{ bu_meter, (unit_exponent_t)BVN_EXPONENT_MAX, {prefix_si, .id.si=si_none} }
		}
	};
	overflow = false;
	scale    = 1.0;
	bvn_unit_reduce(m100_m100, &scale, &overflow);
	ASSERT_TRUE(overflow, "m^100*m^100 reduce: overflow=true (exp 200 leaves the range)");

	/*
	 * NULL overflow pointer: verify the guard inside bvn_unit_reduce
	 * works by observing that bvn_units_compatible still returns a valid
	 * answer (non-crash) after the call.  We do NOT read the local
	 * `overflow` variable here because bvn_unit_reduce writes nothing to
	 * it when the pointer is NULL.
	 */
	bool call_completed = false;
	bvn_unit_reduce(BVN_UNIT_NO_PREFIX(bu_meter), &scale, NULL);
	call_completed = true;
	ASSERT_TRUE(call_completed, "NULL overflow param: no crash");
}

static void test_unit_reduce_bu_none_component(void)
{
	printf("  unit_reduce bu_none component...\n");
	double scale;
	bool overflow;

	value_unit_t m_none = {
		.num_components = 2,
		.components = {
			{ bu_meter, exp_linear, {prefix_si, .id.si=si_none} },
			{ bu_none,  exp_linear, {prefix_si, .id.si=si_none} }
		}
	};
	overflow = false;
	scale    = 1.0;
	value_unit_t r = bvn_unit_reduce(m_none, &scale, &overflow);
	ASSERT_EQ_INT((int64_t)r.num_components, 1,
	              "m*bu_none reduce → 1 component (bu_none silently dropped)");
	ASSERT_EQ_INT((int64_t)r.components[0].base, (int64_t)bu_meter,
	              "m*bu_none reduce → meter survives");
	ASSERT_EQ_DBL(scale, 1.0, 1e-15,
	              "m*bu_none reduce → scale 1.0");
	ASSERT_TRUE(!overflow, "m*bu_none reduce → no overflow");
}

static void test_parse_unit_n(void)
{
	printf("  bvn_parse_unit_n length-bounded...\n");
	bool ok;

	uint8_t buf[] = "k~m/s GARBAGE";
	value_unit_t u = bvn_parse_unit_n(buf, 5, &ok);
	ASSERT_TRUE(ok, "parse_unit_n 'k~m/s' (len=5) ok");
	ASSERT_EQ_INT((int64_t)u.num_components, 2, "parse_unit_n: 2 components");
	ASSERT_EQ_INT((int64_t)u.components[0].base, (int64_t)bu_meter, "parse_unit_n: meter");
	ASSERT_EQ_INT((int64_t)u.components[0].prefix.id.si, (int64_t)si_kilo, "parse_unit_n: kilo");

	uint8_t m[] = "m";
	u = bvn_parse_unit_n(m, 1, &ok);
	ASSERT_TRUE(ok, "parse_unit_n 'm' len=1 ok");
	ASSERT_EQ_INT((int64_t)u.components[0].base, (int64_t)bu_meter, "parse_unit_n: 'm'");

	ok = true;
	bvn_parse_unit_n(NULL, 0, &ok);
	ASSERT_TRUE(!ok, "parse_unit_n NULL: ok=false");
}
static void test_parse_unit_parens(void)
{
	printf("  parenthesised unit grouping...\n");
	bool ok;

	/* A grouped denominator equals the flat form dimensionally (pressure). */
	value_unit_t grouped = bvn_parse_unit((const uint8_t*)"k~g/(m·s²)", &ok);
	ASSERT_TRUE(ok, "parse 'k~g/(m·s²)' ok");
	value_unit_t flat = bvn_parse_unit((const uint8_t*)"k~g/m·s²", &ok);
	ASSERT_TRUE(ok, "parse 'k~g/m·s²' ok");
	ASSERT_TRUE(bvn_units_compatible(grouped, flat),
		"k~g/(m·s²) == k~g/m·s² dimensionally");

	/* Grouping changes the sign: (k~g/m)·s² is kg·m⁻¹·s², not the pressure. */
	value_unit_t areal = bvn_parse_unit((const uint8_t*)"(k~g/m)·s²", &ok);
	ASSERT_TRUE(ok, "parse '(k~g/m)·s²' ok");
	ASSERT_TRUE(!bvn_units_compatible(areal, grouped),
		"(k~g/m)·s² differs from k~g/(m·s²)");

	/* a/(b/c) == a·c/b */
	value_unit_t nested = bvn_parse_unit((const uint8_t*)"m/(s/A)", &ok);
	ASSERT_TRUE(ok, "parse 'm/(s/A)' ok");
	value_unit_t nested_flat = bvn_parse_unit((const uint8_t*)"m·A/s", &ok);
	ASSERT_TRUE(ok, "parse 'm·A/s' ok");
	ASSERT_TRUE(bvn_units_compatible(nested, nested_flat),
		"m/(s/A) == m·A/s");

	/* Malformed groups are rejected. */
	bvn_parse_unit((const uint8_t*)"k~g/(m·s²", &ok);
	ASSERT_TRUE(!ok, "unmatched '(' rejected");
	bvn_parse_unit((const uint8_t*)"()", &ok);
	ASSERT_TRUE(!ok, "empty group '()' rejected");
	bvn_parse_unit((const uint8_t*)"m(s)", &ok);
	ASSERT_TRUE(!ok, "implicit multiply 'm(s)' rejected");
	bvn_parse_unit((const uint8_t*)"(m·s)²", &ok);
	ASSERT_TRUE(!ok, "group exponent '(m·s)²' rejected");
	bvn_parse_unit((const uint8_t*)"m·()", &ok);
	ASSERT_TRUE(!ok, "empty group in expression rejected");

	/* Parenless expressions are unchanged (regression guard). */
	value_unit_t plain = bvn_parse_unit((const uint8_t*)"m/s²", &ok);
	ASSERT_TRUE(ok, "parse 'm/s²' ok (parenless regression)");
	ASSERT_EQ_INT((int64_t)plain.num_components, 2, "m/s² has 2 components");
}

static void test_parse_no_unit_normalised(void)
{
	printf("  parse 'no_unit' normalised to BVN_UNIT_NONE...\n");
	bool ok;

	value_unit_t u = bvn_parse_unit((const uint8_t*)"no_unit", &ok);
	ASSERT_TRUE(ok, "parse 'no_unit' ok");
	ASSERT_EQ_INT((int64_t)u.num_components, 0,
	              "'no_unit' parses to num_components==0 (BVN_UNIT_NONE)");

	char buf[64];
	int32_t r = bvn_unit_to_string(u, buf, sizeof(buf));
	ASSERT_TRUE(r > 0, "BVN_UNIT_NONE to_string > 0");
	ASSERT_TRUE(strcmp(buf, "no_unit") == 0, "BVN_UNIT_NONE serializes to 'no_unit'");

	value_unit_t reparsed = bvn_parse_unit((const uint8_t*)buf, &ok);
	ASSERT_TRUE(ok, "'no_unit' re-parse ok");
	ASSERT_EQ_INT((int64_t)reparsed.num_components, 0,
	              "'no_unit' round-trip stays at num_components==0");

	ASSERT_TRUE(bvn_units_compatible(u, BVN_UNIT_NONE),
	            "parsed no_unit compatible with BVN_UNIT_NONE");
}

static void test_unit_to_string_num_components_clamp(void)
{
	printf("  bvn_unit_to_string num_components > MAX clamp...\n");
	char buf[256];

	value_unit_t bad;
	bad.num_components = BVNR_MAX_UNIT_COMPONENTS + 99;
	for (uint32_t i = 0; i < BVNR_MAX_UNIT_COMPONENTS; i++) {
		bad.components[i].base          = bu_meter;
		bad.components[i].exponent      = exp_linear;
		bad.components[i].prefix.system = prefix_si;
		bad.components[i].prefix.id.si  = si_none;
	}
	int32_t r = bvn_unit_to_string(bad, buf, sizeof(buf));
	ASSERT_TRUE(r == -1, "to_string with oversized num_components returns -1 (invalid unit)");
}

static void test_neg_exp_round_trip(void)
{
	printf("  negative-exponent round-trip (serialize -> parse)...\n");
	bool ok;
	char buf[64];

	value_unit_t m_per_s2 = {
		.num_components = 2,
		.components = {
			{ bu_meter,  exp_linear,     {prefix_si, .id.si=si_none} },
			{ bu_second, exp_neg_square, {prefix_si, .id.si=si_none} }
		}
	};
	int32_t r = bvn_unit_to_string(m_per_s2, buf, sizeof(buf));
	ASSERT_TRUE(r > 0, "m/s² to_string ok");

	value_unit_t reparsed = bvn_parse_unit((const uint8_t*)buf, &ok);
	ASSERT_TRUE(ok, "m/s² round-trip parse ok");
	bool aff; double off; bool si_ok = true;
	double f = bvn_unit_to_si_factor(reparsed, &aff, &off, &si_ok);
	ASSERT_TRUE(si_ok, "m/s² round-trip si_factor ok");
	ASSERT_EQ_DBL(f, 1.0, 1e-15, "m/s² round-trip factor = 1.0");

	value_unit_t kgs = {
		.num_components = 2,
		.components = {
			{ bu_gram,   exp_linear,     {prefix_si, .id.si=si_kilo} },
			{ bu_second, exp_neg_linear, {prefix_si, .id.si=si_none} }
		}
	};
	r = bvn_unit_to_string(kgs, buf, sizeof(buf));
	ASSERT_TRUE(r > 0, "k~g/s to_string ok");
	reparsed = bvn_parse_unit((const uint8_t*)buf, &ok);
	ASSERT_TRUE(ok, "k~g/s round-trip parse ok");
	f = bvn_unit_to_si_factor(reparsed, &aff, &off, &si_ok);
	ASSERT_TRUE(si_ok, "k~g/s round-trip si_factor ok");
	ASSERT_EQ_DBL(f, 1.0, 1e-15, "k~g/s round-trip factor = 1.0");
}

static void test_si_prefix_info_unit_restrictions(void)
{
	printf("  si prefix restrictions on info units...\n");

	value_unit_prefix_t pfx;

	pfx = (value_unit_prefix_t){prefix_si, .id.si = si_milli};
	ASSERT_TRUE(!bvn_prefix_unit_valid(pfx, bu_bit),
	            "si_milli on bu_bit: rejected");
	ASSERT_TRUE(!bvn_prefix_unit_valid(pfx, bu_byte),
	            "si_milli on bu_byte: rejected");

	pfx = (value_unit_prefix_t){prefix_si, .id.si = si_micro};
	ASSERT_TRUE(!bvn_prefix_unit_valid(pfx, bu_bit),
	            "si_micro on bu_bit: rejected");
	ASSERT_TRUE(!bvn_prefix_unit_valid(pfx, bu_byte),
	            "si_micro on bu_byte: rejected");

	pfx = (value_unit_prefix_t){prefix_si, .id.si = si_nano};
	ASSERT_TRUE(!bvn_prefix_unit_valid(pfx, bu_bit),
	            "si_nano on bu_bit: rejected");
	ASSERT_TRUE(!bvn_prefix_unit_valid(pfx, bu_byte),
	            "si_nano on bu_byte: rejected");

	pfx = (value_unit_prefix_t){prefix_si, .id.si = si_hecto};
	ASSERT_TRUE(!bvn_prefix_unit_valid(pfx, bu_bit),
	            "si_hecto on bu_bit: rejected (positive but below kilo)");
	ASSERT_TRUE(!bvn_prefix_unit_valid(pfx, bu_byte),
	            "si_hecto on bu_byte: rejected (positive but below kilo)");

	pfx = (value_unit_prefix_t){prefix_si, .id.si = si_deca};
	ASSERT_TRUE(!bvn_prefix_unit_valid(pfx, bu_bit),
	            "si_deca on bu_bit: rejected (positive but below kilo)");
	ASSERT_TRUE(!bvn_prefix_unit_valid(pfx, bu_byte),
	            "si_deca on bu_byte: rejected (positive but below kilo)");

	pfx = (value_unit_prefix_t){prefix_si, .id.si = si_kilo};
	ASSERT_TRUE(bvn_prefix_unit_valid(pfx, bu_bit),
	            "si_kilo on bu_bit: accepted");
	ASSERT_TRUE(bvn_prefix_unit_valid(pfx, bu_byte),
	            "si_kilo on bu_byte: accepted");

	pfx = (value_unit_prefix_t){prefix_si, .id.si = si_mega};
	ASSERT_TRUE(bvn_prefix_unit_valid(pfx, bu_bit),
	            "si_mega on bu_bit: accepted");
	ASSERT_TRUE(bvn_prefix_unit_valid(pfx, bu_byte),
	            "si_mega on bu_byte: accepted");

	pfx = (value_unit_prefix_t){prefix_si, .id.si = si_giga};
	ASSERT_TRUE(bvn_prefix_unit_valid(pfx, bu_bit),
	            "si_giga on bu_bit: accepted");
	ASSERT_TRUE(bvn_prefix_unit_valid(pfx, bu_byte),
	            "si_giga on bu_byte: accepted");

	pfx = (value_unit_prefix_t){prefix_si, .id.si = si_none};
	ASSERT_TRUE(bvn_prefix_unit_valid(pfx, bu_bit),
	            "si_none on bu_bit: accepted (no-prefix sentinel)");
	ASSERT_TRUE(bvn_prefix_unit_valid(pfx, bu_byte),
	            "si_none on bu_byte: accepted (no-prefix sentinel)");
}

static void test_prefix_unit_valid_out_of_range(void)
{
	printf("  bvn_prefix_unit_valid out-of-range base and system...\n");

	value_unit_prefix_t pfx_si_none = {prefix_si, .id.si = si_none};
	ASSERT_TRUE(!bvn_prefix_unit_valid(pfx_si_none,
	                (value_base_unit_t)(BVN_UNIT_NATIVE_LAST + 1)),
	            "hole above block 10: prefix_valid=false");
	ASSERT_TRUE(!bvn_prefix_unit_valid(pfx_si_none,
	                (value_base_unit_t)(BVN_UNIT_ID_FIRST - 1)),
	            "below every block: prefix_valid=false");
	ASSERT_TRUE(!bvn_prefix_unit_valid(pfx_si_none,
	                (value_base_unit_t)(99 * BVN_UNIT_BLOCK_SIZE)),
	            "above every block: prefix_valid=false");
	/* Negative ids reach the same table index arithmetic. They used to be
	 * caught by an unsigned cast that made them enormous; the slot map has to
	 * refuse them on its own terms. */
	ASSERT_TRUE(!bvn_prefix_unit_valid(pfx_si_none, (value_base_unit_t)(-1)),
	            "negative base: prefix_valid=false");

	/* out-of-range prefix.system */
	value_unit_prefix_t pfx_bad_sys = {
		.system = (prefix_system_t)BVN_PREFIX_SYSTEM_COUNT,
		.id.si  = si_none
	};
	ASSERT_TRUE(!bvn_prefix_unit_valid(pfx_bad_sys, bu_meter),
	            "out-of-range prefix.system: prefix_valid=false");
	value_unit_prefix_t pfx_bad_sys2 = {
		.system = (prefix_system_t)(BVN_PREFIX_SYSTEM_COUNT + 3),
		.id.si  = si_none
	};
	ASSERT_TRUE(!bvn_prefix_unit_valid(pfx_bad_sys2, bu_meter),
	            "far out-of-range prefix.system: prefix_valid=false");
}

/* Invalid prefix/base combo on a programmatically built unit must set ok=false */
static void test_si_factor_invalid_prefix_combo(void)
{
	printf("  si_factor invalid prefix/base combo (si_kilo on byte)...\n");
	bool aff; double off; bool ok = true;

	value_unit_t si_kilo_byte = {
		.num_components = 1,
		.components = {
			{ bu_byte, exp_linear, {prefix_si, .id.si=si_kilo} }
		}
	};
	bvn_unit_to_si_factor(si_kilo_byte, &aff, &off, &ok);
	ASSERT_TRUE(ok,  "si_kilo on bu_byte: ok=true (SI prefix on info unit now valid)");

	ok = true;
	value_unit_t iec_kibi_meter = {
		.num_components = 1,
		.components = {
			{ bu_meter, exp_linear, {prefix_iec, .id.iec=iec_kibi} }
		}
	};
	bvn_unit_to_si_factor(iec_kibi_meter, &aff, &off, &ok);
	ASSERT_TRUE(!ok, "iec_kibi on bu_meter: ok=false (invalid combo)");
}

/* Invalid prefix/base on dimension_vector must return false */
static void test_dimension_vector_invalid_prefix(void)
{
	printf("  bvn_unit_dimension_vector with invalid prefix...\n");
	int32_t dims[bvn_si_dim_count];

	value_unit_t si_kilo_byte = {
		.num_components = 1,
		.components = {
			{ bu_byte, exp_linear, {prefix_si, .id.si=si_kilo} }
		}
	};
	ASSERT_TRUE(bvn_unit_dimension_vector(si_kilo_byte, dims),
	            "si_kilo on bu_byte: dim_vector returns true (SI prefix on info unit now valid)");

	/* exp_invalid must also return false */
	value_unit_t bad_exp = {
		.num_components = 1,
		.components = {
			{ bu_meter, exp_invalid, {prefix_si, .id.si=si_none} }
		}
	};
	ASSERT_TRUE(!bvn_unit_dimension_vector(bad_exp, dims),
	            "exp_invalid: dim_vector returns false");
}

static void test_convert_value(void)
{
	printf("  convert_value...\n");
	double out = 0.0;

	/* multiplicative: 5 km → 5000 m */
	ASSERT_TRUE(bvn_unit_convert_value(5.0,
			BVN_UNIT_SI(bu_meter, si_kilo),
			BVN_UNIT_NO_PREFIX(bu_meter), &out),
		"5 km → m: ok");
	ASSERT_EQ_DBL(out, 5000.0, 1e-9, "5 km → 5000 m");

	/* affine: 25 °C → 298.15 K (two-step through SI) */
	ASSERT_TRUE(bvn_unit_convert_value(25.0,
			BVN_UNIT_NO_PREFIX(bu_celsius),
			BVN_UNIT_NO_PREFIX(bu_kelvin), &out),
		"25 °C → K: ok");
	ASSERT_EQ_DBL(out, 298.15, 1e-9, "25 °C → 298.15 K");

	/* affine reverse: 273.15 K → 0 °C */
	ASSERT_TRUE(bvn_unit_convert_value(273.15,
			BVN_UNIT_NO_PREFIX(bu_kelvin),
			BVN_UNIT_NO_PREFIX(bu_celsius), &out),
		"273.15 K → °C: ok");
	ASSERT_EQ_DBL(out, 0.0, 1e-9, "273.15 K → 0 °C");

	/* dimensionally incompatible: m → s must fail and not touch out */
	out = -12345.0;
	ASSERT_TRUE(!bvn_unit_convert_value(1.0,
			BVN_UNIT_NO_PREFIX(bu_meter),
			BVN_UNIT_NO_PREFIX(bu_second), &out),
		"m → s: incompatible → false");
	ASSERT_EQ_DBL(out, -12345.0, 1e-9, "m → s: out untouched on failure");

	/* NULL out is "do not report the result", not a refusal — the same rule as
	 * every other out-parameter in the unit API, which leaves this as the
	 * predicate "can this conversion be done". It used to return false, so a
	 * caller asking that question got "no" for m → m. */
	ASSERT_TRUE(bvn_unit_convert_value(1.0,
			BVN_UNIT_NO_PREFIX(bu_meter),
			BVN_UNIT_NO_PREFIX(bu_meter), NULL),
		"NULL out: m → m still succeeds");
	ASSERT_TRUE(!bvn_unit_convert_value(1.0,
			BVN_UNIT_NO_PREFIX(bu_meter),
			BVN_UNIT_NO_PREFIX(bu_second), NULL),
		"NULL out: m → s still fails");
}

/* Convert an exact-rational value (given as a decimal/int string in `vbase`)
 * from unit `from` to `to`, render in `obase`, and compare against `expect`
 * (NULL means the render is expected to be non-terminating → -1). */
static void check_rat(const char *vs, uint32_t vbase, bool isint,
		      value_unit_t from, value_unit_t to, uint32_t obase,
		      bool expect_exact_factor, const char *expect,
		      const char *msg)
{
	bvn_int_t *vn = bvn_int_alloc(), *vd = bvn_int_alloc();
	bvn_int_t *on = bvn_int_alloc(), *od = bvn_int_alloc();
	bool p = isint ? (bvn_int_from_str(vn, vs, vbase) &&
			  bvn_int_from_uint64(vd, 1u))
		       : bvn_float_parse_rational(vs, vbase, vn, vd);
	ASSERT_TRUE(p, msg);
	bool exact = false;
	bool ok = bvn_unit_convert_rational(vn, vd, from, to, on, od, &exact);
	if (p) {
		ASSERT_TRUE(ok, msg);
		ASSERT_TRUE(exact == expect_exact_factor, msg);
		char buf[4096]; bool rexact = false;
		int32_t len = bvn_rational_to_str(on, od, obase, buf, sizeof buf, &rexact);
		if (expect == NULL) {
			/* Specifically the non-terminating signal, not a generic
			 * failure — the rational itself is still exact. */
			ASSERT_TRUE(len == BVN_RATIONAL_NONTERMINATING && !rexact, msg);
		} else {
			ASSERT_TRUE(len >= 0 && rexact && strcmp(buf, expect) == 0, msg);
		}
	}
	bvn_int_free(vn); bvn_int_free(vd); bvn_int_free(on); bvn_int_free(od);
}

static void test_convert_rational(void)
{
	printf("  convert_rational (lossless)...\n");
	value_unit_t km = BVN_UNIT_SI(bu_meter, si_kilo), m = BVN_UNIT_NO_PREFIX(bu_meter);
	value_unit_t mile = BVN_UNIT_NO_PREFIX(bu_mile), inch = BVN_UNIT_NO_PREFIX(bu_inch);
	value_unit_t GiB = BVN_UNIT_IEC(bu_byte, iec_gibi), B = BVN_UNIT_NO_PREFIX(bu_byte);
	value_unit_t degC = BVN_UNIT_NO_PREFIX(bu_celsius), K = BVN_UNIT_NO_PREFIX(bu_kelvin);
	value_unit_t degF = BVN_UNIT_NO_PREFIX(bu_fahrenheit);
	value_unit_t deg = BVN_UNIT_NO_PREFIX(bu_degree), rad = BVN_UNIT_NO_PREFIX(bu_radian);

	check_rat("5", 10, true, km, m, 10, true, "5000", "5 km -> 5000 m");
	check_rat("1", 10, true, mile, m, 10, true, "1609.344", "1 mile -> 1609.344 m");
	check_rat("1", 10, true, mile, inch, 10, true, "63360", "1 mile -> 63360 in");
	check_rat("3", 10, true, GiB, B, 10, true, "3221225472", "3 GiB -> bytes");
	/* wide float, base-2 output (base conversion of a fraction) */
	check_rat("1.0009765625", 10, false, km, m, 2, true,
		  "1111101000.1111101", "1.0009765625 km -> m in base 2");
	/* non-decimal-base carrier + base conversion */
	check_rat("FF", 16, true, km, m, 10, true, "255000", "0xFF km -> 255000 m");
	check_rat("255", 10, true, m, m, 16, true, "ff", "255 -> base16 ff (pure base)");
	/* affine, exact via overrides */
	check_rat("25", 10, true, degC, K, 10, true, "298.15", "25 C -> 298.15 K");
	check_rat("98.6", 10, false, degF, degC, 10, true, "37", "98.6 F -> 37 C");
	/* non-terminating in requested base -> render fails (inexact delivery) */
	check_rat("1", 10, true, m, mile, 10, true, NULL, "1 m -> mile nonterminating b10");
	/* irrational factor -> exact flag false (the decimal render may still
	 * terminate; it is only an approximation, so the reader rejects it). */
	{
		bvn_int_t *vn = bvn_int_alloc(), *vd = bvn_int_alloc();
		bvn_int_t *on = bvn_int_alloc(), *od = bvn_int_alloc();
		bvn_int_from_uint64(vn, 90u); bvn_int_from_uint64(vd, 1u);
		bool exact = true;
		ASSERT_TRUE(bvn_unit_convert_rational(vn, vd, deg, rad, on, od, &exact),
			    "90 deg -> rad converts");
		ASSERT_TRUE(!exact, "90 deg -> rad flagged inexact (irrational)");
		bvn_int_free(vn); bvn_int_free(vd); bvn_int_free(on); bvn_int_free(od);
	}
	/* multiprecision integer: 2^128-1 km -> m (×1000), exact */
	check_rat("340282366920938463463374607431768211455", 10, true, km, m, 10, true,
		  "340282366920938463463374607431768211455000", "uint128 max km -> m");

	/* incompatible dims -> convert returns false */
	{
		bvn_int_t *vn = bvn_int_alloc(), *vd = bvn_int_alloc();
		bvn_int_t *on = bvn_int_alloc(), *od = bvn_int_alloc();
		bvn_int_from_uint64(vn, 5u); bvn_int_from_uint64(vd, 1u);
		bool exact = false;
		ASSERT_TRUE(!bvn_unit_convert_rational(vn, vd, m, B, on, od, &exact),
			    "m -> B incompatible returns false");
		bvn_int_free(vn); bvn_int_free(vd); bvn_int_free(on); bvn_int_free(od);
	}
}

/* Build the rational num/den from two decimal strings, run it through
 * bvn_rational_to_str in `base`, and compare. `expect` NULL means the expansion
 * must be reported non-terminating. */
static void check_str(const char *ns, const char *ds, uint32_t base,
		      const char *expect, const char *msg)
{
	bvn_int_t *n = bvn_int_alloc(), *d = bvn_int_alloc();
	ASSERT_TRUE(n && d && bvn_int_from_str(n, ns, 10) &&
		    bvn_int_from_str(d, ds, 10), msg);
	char buf[512]; bool exact = false;
	int32_t len = bvn_rational_to_str(n, d, base, buf, sizeof buf, &exact);
	if (expect == NULL) {
		ASSERT_TRUE(len == BVN_RATIONAL_NONTERMINATING && !exact, msg);
	} else {
		ASSERT_TRUE(len >= 0 && exact && strcmp(buf, expect) == 0, msg);
		ASSERT_TRUE(len == (int32_t)strlen(expect), msg);
		/* The advertised bound must actually be enough. */
		ASSERT_TRUE(bvn_rational_str_bufsize(n, d, base) >= strlen(expect) + 1u,
			    msg);
	}
	bvn_int_free(n); bvn_int_free(d);
}

static void test_rational_to_str_bases(void)
{
	printf("  rational_to_str (full base range)...\n");
	/* Every base bvnr can write, not just 2..36. 255 = 4·62+7, 3·64+63,
	 * 3·85+0; base 64 spells its digits A-Za-z0-9+/ and base 85 from '!'. */
	check_str("255", "1", 36, "73",  "255 in base 36");
	check_str("255", "1", 37, "6x",  "255 in base 37");
	check_str("255", "1", 62, "47",  "255 in base 62");
	check_str("255", "1", 64, "D/",  "255 in base 64");
	check_str("255", "1", 85, "$!",  "255 in base 85");
	/* A high base's ZERO digit is not '0' — Base64 spells it 'A', Ascii85 '!' —
	 * and it is what both the integer part and the fraction's leading padding
	 * must use, exactly as bvn_int_to_str renders zero. 1/4096 is 1·64⁻², so
	 * base 64 gives "A.AB"; 1/7225 is 1·85⁻², so base 85 gives "!.!\"". */
	check_str("1", "4096", 64, "A.AB", "1/4096 in base 64 zero-pads with 'A'");
	check_str("1", "7225", 85, "!.!\"", "1/7225 in base 85 zero-pads with '!'");
	check_str("1", "8", 10, "0.125", "1/8 in base 10");
	check_str("1", "8", 2, "0.001", "1/8 in base 2");
	check_str("-3", "8", 10, "-0.375", "negative fraction keeps its sign");
	check_str("0", "5", 10, "0", "zero renders as the bare zero digit");
	/* Non-terminating is reported as such, distinctly from an error. */
	check_str("1", "3", 10, NULL, "1/3 does not terminate in base 10");
	check_str("1", "1000", 2, NULL, "1/1000 does not terminate in base 2");

	/* Bases 64/85 have no sign character, so a negative value is unrepresentable
	 * — a hard -1, never a '-' that would read back as a digit. */
	{
		bvn_int_t *n = bvn_int_alloc(), *d = bvn_int_alloc();
		bvn_int_from_str(n, "-255", 10); bvn_int_from_uint64(d, 1u);
		char buf[64]; bool exact = true;
		ASSERT_TRUE(bvn_rational_to_str(n, d, 85, buf, sizeof buf, &exact) == -1 &&
			    !exact, "negative value refused in sign-less base 85");
		ASSERT_TRUE(bvn_rational_to_str(n, d, 64, buf, sizeof buf, &exact) == -1,
			    "negative value refused in sign-less base 64");
		/* An unsupported base is refused too (63 is not a bvnr base). */
		ASSERT_TRUE(bvn_rational_to_str(n, d, 63, buf, sizeof buf, &exact) == -1,
			    "base 63 refused");
		ASSERT_TRUE(!bvn_rational_base_valid(63u) && !bvn_rational_base_valid(1u) &&
			    bvn_rational_base_valid(62u) && bvn_rational_base_valid(64u) &&
			    bvn_rational_base_valid(85u), "base validity predicate");
		bvn_int_free(n); bvn_int_free(d);
	}
}

static void test_rational_to_str_never_truncates(void)
{
	printf("  rational_to_str (short buffer refused)...\n");
	/* Truncating an exact expansion yields a DIFFERENT number, so a buffer that
	 * cannot hold the result is refused outright rather than filled part-way. */
	bvn_int_t *n = bvn_int_alloc(), *d = bvn_int_alloc();
	ASSERT_TRUE(n && d && bvn_int_from_str(n, "123456789", 10) &&
		    bvn_int_from_str(d, "1000", 10), "short-buffer setup");
	const char *full = "123456.789";
	size_t need = bvn_rational_str_bufsize(n, d, 10);
	ASSERT_TRUE(need >= strlen(full) + 1u, "bufsize bound covers the result");

	for (size_t cap = 2; cap <= strlen(full); cap++) {
		char small[32];
		memset(small, '#', sizeof small);
		bool exact = true;
		int32_t len = bvn_rational_to_str(n, d, 10, small, cap, &exact);
		ASSERT_TRUE(len == BVN_RATIONAL_TOO_LONG,
			    "short buffer says TOO_LONG, never a partial length");
		ASSERT_TRUE(!exact, "short buffer never claims exactness");
		ASSERT_TRUE(small[0] == '#', "short buffer left unwritten");
	}
	{
		char ok[32]; bool exact = false;
		int32_t len = bvn_rational_to_str(n, d, 10, ok, strlen(full) + 1u, &exact);
		ASSERT_TRUE(len == (int32_t)strlen(full) && exact &&
			    strcmp(ok, full) == 0, "exact-fit buffer succeeds");
	}
	bvn_int_free(n); bvn_int_free(d);
}

static void test_exact_factor_table(void)
{
	printf("  exact rational factor table...\n");
	value_unit_t Pa   = BVN_UNIT_NO_PREFIX(bu_pascal);
	value_unit_t torr = BVN_UNIT_NO_PREFIX(bu_torr);
	value_unit_t m    = BVN_UNIT_NO_PREFIX(bu_meter);
	value_unit_t ftUS = BVN_UNIT_NO_PREFIX(bu_survey_foot);
	value_unit_t K    = BVN_UNIT_NO_PREFIX(bu_kelvin);
	value_unit_t Ra   = BVN_UNIT_NO_PREFIX(bu_rankine);
	value_unit_t Hz   = BVN_UNIT_NO_PREFIX(bu_hertz);
	value_unit_t rpm  = BVN_UNIT_NO_PREFIX(bu_rpm);
	value_unit_t ms   = BVN_UNIT_COMPOUND2(bu_meter, si_none, exp_linear,
					       bu_second, si_none, exp_neg_linear);
	value_unit_t kn   = BVN_UNIT_NO_PREFIX(bu_knot);
	value_unit_t rad  = BVN_UNIT_NO_PREFIX(bu_radian);
	value_unit_t pc   = BVN_UNIT_NO_PREFIX(bu_parsec);
	value_unit_t Oe   = BVN_UNIT_NO_PREFIX(bu_oersted);
	value_unit_t Am   = BVN_UNIT_COMPOUND2(bu_ampere, si_none, exp_linear,
					       bu_meter, si_none, exp_neg_linear);

	/* These factors are exact rationals with a terminating decimal only at
	 * particular multiples — the whole point being that the ANSWER is right.
	 * 1 Torr = 101325/760 Pa, so 760 Torr is exactly 101325 Pa; the old table
	 * held a rounded double and produced 101324.9999999999988. */
	check_rat("760", 10, true, torr, Pa, 10, true, "101325",
		  "760 Torr -> exactly 101325 Pa");
	/* 1 ftUS = 1200/3937 m exactly, so 3937 of them are exactly 1200 m. */
	check_rat("3937", 10, true, ftUS, m, 10, true, "1200",
		  "3937 ftUS -> exactly 1200 m");
	/* 9 °Ra = 5 K exactly (slope 5/9, the same as °F). */
	check_rat("9", 10, true, Ra, K, 10, true, "5", "9 degRa -> exactly 5 K");
	/* 60 rpm = 1 Hz exactly (slope 1/60). */
	check_rat("60", 10, true, rpm, Hz, 10, true, "1", "60 rpm -> exactly 1 Hz");
	/* 900 kn = 463 m/s exactly (slope 463/900). */
	check_rat("900", 10, true, kn, ms, 10, true, "463", "900 kn -> exactly 463 m/s");

	/* And at a value where the true rational does NOT terminate in base 10, the
	 * conversion must REFUSE rather than hand back a rounded 17-digit decimal.
	 * This is the half that used to be inconsistent: °F refused while °Ra, rpm
	 * and kn silently returned wrong "exact" answers. */
	check_rat("1", 10, true, Ra,  K,  10, true, NULL, "1 degRa -> K nonterminating");
	check_rat("1", 10, true, rpm, Hz, 10, true, NULL, "1 rpm -> Hz nonterminating");
	check_rat("1", 10, true, kn,  ms, 10, true, NULL, "1 kn -> m/s nonterminating");

	/* π-based factors carry no exact rational at all and must be flagged
	 * inexact — parsec (648000/π au) and oersted (1000/4π A/m) were missing
	 * that flag while the angle units had it. */
	{
		value_unit_t froms[] = { pc, Oe, BVN_UNIT_NO_PREFIX(bu_degree) };
		value_unit_t tos[]   = { m,  Am, rad };
		const char *msgs[]   = { "parsec flagged irrational",
					 "oersted flagged irrational",
					 "degree flagged irrational" };
		for (size_t i = 0; i < sizeof froms / sizeof froms[0]; i++) {
			bvn_int_t *vn = bvn_int_alloc(), *vd = bvn_int_alloc();
			bvn_int_t *on = bvn_int_alloc(), *od = bvn_int_alloc();
			bvn_int_from_uint64(vn, 1u); bvn_int_from_uint64(vd, 1u);
			bool exact = true;
			ASSERT_TRUE(bvn_unit_convert_rational(vn, vd, froms[i], tos[i],
							      on, od, &exact), msgs[i]);
			ASSERT_TRUE(!exact, msgs[i]);
			bvn_int_free(vn); bvn_int_free(vd);
			bvn_int_free(on); bvn_int_free(od);
		}
	}
}

static void test_prefix_exponent_interaction(void)
{
	printf("  prefixed components with exponents...\n");
	value_unit_t m = BVN_UNIT_NO_PREFIX(bu_meter);
	value_unit_t m2 = m, m3 = m, km2 = BVN_UNIT_SI(bu_meter, si_kilo), km3 = km2;
	m2.components[0].exponent  = exp_square;
	m3.components[0].exponent  = exp_cubic;
	km2.components[0].exponent = exp_square;
	km3.components[0].exponent = exp_cubic;

	/* A prefix on a squared component scales by 10^(3*2), not 10^(3*2*2): the
	 * prefix exponent helper already folds in |exp|, and multiplying by it a
	 * second time turned 1 km² into 10^12 m². */
	check_rat("1", 10, true, km2, m2, 10, true, "1000000",  "1 km2 -> 1e6 m2");
	check_rat("1", 10, true, km3, m3, 10, true, "1000000000", "1 km3 -> 1e9 m3");
	check_rat("1", 10, true, m2, km2, 10, true, "0.000001", "1 m2 -> 1e-6 km2");

	/* A prefix on a NEGATIVE exponent must scale down, not up. m/km is 1/1000;
	 * applying the sign twice made it 1000 and the round trip 10^6 out. */
	value_unit_t m_per_km = BVN_UNIT_COMPOUND2(bu_meter, si_none, exp_linear,
						   bu_meter, si_kilo, exp_neg_linear);
	value_unit_t m_per_m  = BVN_UNIT_COMPOUND2(bu_meter, si_none, exp_linear,
						   bu_meter, si_none, exp_neg_linear);
	check_rat("6", 10, true, m_per_km, m_per_m, 10, true, "0.006", "6 m/km -> 0.006");
	check_rat("6", 10, true, m_per_m, m_per_km, 10, true, "6000",  "6 m/m -> 6000 m/km");

	/* Same for a milli prefix, which scales the other way. */
	value_unit_t m_per_mm = BVN_UNIT_COMPOUND2(bu_meter, si_none, exp_linear,
						   bu_meter, si_milli, exp_neg_linear);
	check_rat("6", 10, true, m_per_mm, m_per_m, 10, true, "6000", "6 m/mm -> 6000");

	/* IEC prefixes travel the base-2 path and must behave identically. */
	value_unit_t GiB_s = { .num_components = 2, .components = {
		{ .base = bu_byte, .exponent = exp_linear,
		  .prefix = { .system = prefix_iec, .id.iec = iec_gibi } },
		{ .base = bu_second, .exponent = exp_neg_linear,
		  .prefix = { .system = prefix_si, .id.si = si_none } } } };
	value_unit_t B_s = BVN_UNIT_COMPOUND2(bu_byte, si_none, exp_linear,
					      bu_second, si_none, exp_neg_linear);
	check_rat("2", 10, true, GiB_s, B_s, 10, true, "2147483648", "2 GiB/s -> bytes/s");

	/* bu_none is a real table row that can carry a prefix — a dimensionless
	 * kilo. Every other function in the library scales by 1000 for it; skipping
	 * the component here made this the lone dissenter. */
	value_unit_t kilo_none = BVN_UNIT_SI(bu_none, si_kilo);
	value_unit_t plain     = BVN_UNIT_NO_PREFIX(bu_none);
	check_rat("3000", 10, true, plain, kilo_none, 10, true, "3", "3000 -> 3 kilo");
	check_rat("3", 10, true, kilo_none, plain, 10, true, "3000", "3 kilo -> 3000");
}

/*
 * Structural guard: the exact-rational factor builder and the legacy double one
 * model the same physics, so for any unit pair they must agree to within double
 * rounding. Sweeping unit x prefix x exponent here is what catches a whole CLASS
 * of factor-assembly bug — a prefix raised to the wrong power, a sign applied
 * twice, a component silently skipped — rather than the one instance someone
 * happened to write a case for.
 */
static double rat_to_double(const bvn_int_t *n, const bvn_int_t *d)
{
	char bn[4096], bd[4096];
	if (bvn_int_to_str(n, bn, sizeof bn, 10) < 0) return NAN;
	if (bvn_int_to_str(d, bd, sizeof bd, 10) < 0) return NAN;
	return strtod(bn, NULL) / strtod(bd, NULL);
}
static void test_rational_matches_double_path(void)
{
	printf("  exact vs double factor agreement (sweep)...\n");
	static const value_base_unit_t U[] = {
		bu_meter, bu_second, bu_gram, bu_ampere, bu_kelvin, bu_newton,
		bu_joule, bu_watt, bu_pascal, bu_hertz, bu_volt, bu_inch, bu_foot,
		bu_mile, bu_pound, bu_liter, bu_byte, bu_bit, bu_hour, bu_none,
	};
	static const si_prefix_id_t P[] = { si_none, si_kilo, si_milli, si_mega, si_micro };
	static const unit_exponent_t E[] = { exp_linear, exp_square, exp_cubic,
					     exp_neg_linear, exp_neg_square };
	const size_t NU = sizeof U / sizeof U[0], NP = sizeof P / sizeof P[0],
		     NE = sizeof E / sizeof E[0];
	int compared = 0, mismatches = 0;

	for (size_t i = 0; i < NU; i++)
	for (size_t p = 0; p < NP; p++)
	for (size_t e = 0; e < NE; e++) {
		/* prefixed+exponented unit vs. the same unit unprefixed */
		value_unit_t f = BVN_UNIT_SI(U[i], P[p]);
		value_unit_t t = BVN_UNIT_NO_PREFIX(U[i]);
		f.components[0].exponent = E[e];
		t.components[0].exponent = E[e];
		/* and the same pair as the denominator of a compound */
		value_unit_t cf = BVN_UNIT_COMPOUND2(bu_meter, si_none, exp_linear,
						     U[i], P[p], E[e]);
		value_unit_t ct = BVN_UNIT_COMPOUND2(bu_meter, si_none, exp_linear,
						     U[i], si_none, E[e]);
		value_unit_t pairs[][2] = { { f, t }, { t, f }, { cf, ct }, { ct, cf } };

		for (size_t k = 0; k < sizeof pairs / sizeof pairs[0]; k++) {
			value_unit_t a = pairs[k][0], b = pairs[k][1];
			if (!bvn_units_compatible(a, b)) continue;
			double dref;
			bool dok = bvn_unit_convert_value(6.0, a, b, &dref);
			bvn_int_t *vn = bvn_int_alloc(), *vd = bvn_int_alloc();
			bvn_int_t *on = bvn_int_alloc(), *od = bvn_int_alloc();
			bvn_int_from_uint64(vn, 6u); bvn_int_from_uint64(vd, 1u);
			bool exact = false;
			bool rok = bvn_unit_convert_rational(vn, vd, a, b, on, od, &exact);
			if (dok != rok) {
				mismatches++;
			} else if (rok && exact) {
				double r = rat_to_double(on, od);
				double scale = fabs(dref) > 1e-300 ? fabs(dref) : 1.0;
				compared++;
				if (!(fabs(r - dref) / scale < 1e-9))
					mismatches++;
			}
			bvn_int_free(vn); bvn_int_free(vd);
			bvn_int_free(on); bvn_int_free(od);
		}
	}
	ASSERT_TRUE(compared > 1000, "sweep actually compared a meaningful number of pairs");
	ASSERT_EQ_INT(mismatches, 0, "exact and double factor paths agree everywhere");
}


static void test_identity_still_validates_structure(void)
{
	printf("  identity conversion validates structure...\n");
	/* The identity short-circuit exists so a unit with no SI row (a currency) or
	 * an irrational factor can convert to itself. It must not become a hole that
	 * waves through a MALFORMED unit, which every other entry point rejects. */
	value_unit_t good = BVN_UNIT_NO_PREFIX(bu_meter);

	value_unit_t bad_exp = good;
	bad_exp.components[0].exponent = exp_invalid;

	value_unit_t bad_pfx = good;                    /* IEC prefix on a metre */
	bad_pfx.components[0].prefix.system = prefix_iec;
	bad_pfx.components[0].prefix.id.iec = iec_gibi;

	value_unit_t oob = good;                        /* base index off the table */
	oob.components[0].base = (value_base_unit_t)9999;

	value_unit_t toomany = good;                    /* count past the array */
	toomany.num_components = BVNR_MAX_UNIT_COMPONENTS + 1u;

	value_unit_t bad[] = { bad_exp, bad_pfx, oob, toomany };
	for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
		double out = -1.0;
		ASSERT_TRUE(!bvn_unit_convert_value(7.0, bad[i], bad[i], &out),
			    "malformed unit refused by convert_value identity");
		bvn_int_t *vn = bvn_int_alloc(), *vd = bvn_int_alloc();
		bvn_int_t *on = bvn_int_alloc(), *od = bvn_int_alloc();
		bvn_int_from_uint64(vn, 7u); bvn_int_from_uint64(vd, 1u);
		bool exact = false;
		ASSERT_TRUE(!bvn_unit_convert_rational(vn, vd, bad[i], bad[i],
						       on, od, &exact),
			    "malformed unit refused by convert_rational identity");
		bvn_int_free(vn); bvn_int_free(vd); bvn_int_free(on); bvn_int_free(od);
	}

	/* ...while the cases the short-circuit exists for still work. */
	double out = 0.0;
	ASSERT_TRUE(bvn_unit_convert_value(7.0, good, good, &out) && out == 7.0,
		    "well-formed identity converts");
	bool ok = false;
	value_unit_t usd = bvn_parse_unit((const uint8_t *)"$USD", &ok);
	if (ok) {
		out = 0.0;
		ASSERT_TRUE(bvn_unit_convert_value(7.0, usd, usd, &out) && out == 7.0,
			    "currency identity converts despite having no SI row");
	}
	value_unit_t deg = BVN_UNIT_NO_PREFIX(bu_degree);
	out = 0.0;
	ASSERT_TRUE(bvn_unit_convert_value(7.0, deg, deg, &out) && out == 7.0,
		    "irrational-factor identity converts");
}


static void test_malformed_exponent_never_aborts(void)
{
	printf("  out-of-enum exponent is rejected, not fatal...\n");
	/* A caller can fill a value_unit_t by hand, and an exponent outside
	 * unit_exponent_t used to pass bvn_unit_valid, get silently dropped by the
	 * formatter, and then trip an assert() that the shipped Release build kept
	 * live — a library taking down its host process over a bad argument.
	 *
	 * 10 used to be out of range and is now an ordinary exponent, so the value
	 * that exercises this has to be past BVN_EXPONENT_MAX. */
	value_unit_t u = BVN_UNIT_NO_PREFIX(bu_meter);
	u.components[0].exponent = (unit_exponent_t)(BVN_EXPONENT_MAX + 1);
	char buf[64];
	ASSERT_TRUE(!bvn_unit_valid(u), "out-of-enum exponent fails bvn_unit_valid");
	ASSERT_TRUE(bvn_unit_to_string(u, buf, sizeof buf) < 0,
		    "out-of-enum exponent is refused, not silently dropped");
	/* These must simply return; reaching them at all used to abort. */
	(void)bvn_unit_prefix_factor(u);
	(void)bvn_unit_prefix_exponent(u);
	double scale = 1.0; bool ovf = false;
	(void)bvn_unit_reduce(u, &scale, &ovf);
	bool ok = true;
	(void)bvn_unit_to_si_factor(u, &(bool){0}, &(double){0}, &ok);
	ASSERT_TRUE(!ok, "out-of-enum exponent has no SI factor");
}

static void test_unwritable_units_are_refused(void)
{
	printf("  bu_none components do not format as unparseable text...\n");
	/* bu_none contributes no symbol, so anything past the plain "no_unit" shape
	 * formatted as "k~" or "m·k~" — which this library's own parser rejects, and
	 * which the writer would have embedded in a document. */
	char buf[64];
	ASSERT_TRUE(bvn_unit_to_string(BVN_UNIT_NO_PREFIX(bu_none), buf, sizeof buf) == 7 &&
		    strcmp(buf, "no_unit") == 0, "the plain dimensionless unit still formats");

	value_unit_t bad[] = {
		BVN_UNIT_SI(bu_none, si_kilo),
		BVN_UNIT_COMPOUND2(bu_meter, si_none, exp_linear, bu_none, si_none, exp_linear),
		BVN_UNIT_COMPOUND2(bu_meter, si_none, exp_linear, bu_none, si_kilo, exp_linear),
	};
	for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
		ASSERT_TRUE(bvn_unit_to_string(bad[i], buf, sizeof buf) < 0,
			    "an unwritable bu_none unit is refused by the formatter");
		/* and whatever it does produce must never be text the parser rejects */
		int32_t n = bvn_unit_to_string_ex(bad[i], buf, sizeof buf, BVN_UNIT_REDUCE);
		if (n >= 0) {
			bool pok = false;
			(void)bvn_parse_unit((const uint8_t *)buf, &pok);
			ASSERT_TRUE(pok, "REDUCE output round-trips through the parser");
		}
	}
}

static void test_reduce_keeps_a_prefixed_dimensionless_scale(void)
{
	printf("  reduce folds a prefixed bu_none into the scale...\n");
	/* bu_none carries no dimension but it CAN carry a prefix. Dropping the whole
	 * component dropped that 1000x with it — the same defect fixed earlier in
	 * bvn_unit_to_si_rational, of which this was the last holdout. */
	double scale = 1.0; bool ovf = false;
	value_unit_t r = bvn_unit_reduce(BVN_UNIT_SI(bu_none, si_kilo), &scale, &ovf);
	ASSERT_EQ_INT((int64_t)r.num_components, 0,
		      "a dimensionless component does not survive reduction");
	ASSERT_EQ_DBL(scale, 1000.0, 1e-9, "...but its prefix scale does");
	ASSERT_TRUE(!ovf, "no overflow reported");

	/* and it must not resurrect the component in a compound */
	value_unit_t mixed = BVN_UNIT_COMPOUND2(bu_meter, si_none, exp_linear,
						bu_none, si_kilo, exp_linear);
	scale = 1.0; ovf = false;
	r = bvn_unit_reduce(mixed, &scale, &ovf);
	ASSERT_EQ_INT((int64_t)r.num_components, 1, "only the metre survives");
	ASSERT_EQ_INT((int64_t)r.components[0].base, (int64_t)bu_meter, "and it is the metre");
	ASSERT_EQ_DBL(scale, 1000.0, 1e-9, "with the dimensionless kilo folded in");
}

static void test_convert_factor_identity(void)
{
	printf("  convert_factor identity...\n");
	/* bvn_unit_convert_value and _rational grew an identity short-circuit; the
	 * public factor API did not, so it still refused $USD -> $USD — and Python's
	 * convert_factor with it. */
	bool ok = false, aff = false;
	value_unit_t usd = bvn_parse_unit((const uint8_t *)"$USD", &ok);
	if (ok) {
		bool cok = false;
		double f = bvn_unit_convert_factor(usd, usd, &cok, &aff);
		ASSERT_TRUE(cok && f == 1.0, "a currency converts to itself with factor 1");
	}
	/* A pair the normal path already handled must keep its exact old answer,
	 * including the requires_affine signal. */
	bool cok = false; aff = false;
	double f = bvn_unit_convert_factor(BVN_UNIT_NO_PREFIX(bu_celsius),
					   BVN_UNIT_NO_PREFIX(bu_celsius), &cok, &aff);
	ASSERT_TRUE(cok && f == 1.0 && aff,
		    "degC -> degC keeps ok, factor 1 and requires_affine");
	/* Malformed stays refused. */
	value_unit_t bad = BVN_UNIT_NO_PREFIX(bu_meter);
	bad.components[0].exponent = exp_invalid;
	cok = false;
	(void)bvn_unit_convert_factor(bad, bad, &cok, &aff);
	ASSERT_TRUE(!cok, "a malformed unit is still refused by convert_factor");
}


static void test_logarithmic_units_refuse(void)
{
	printf("  logarithmic units are their own quantity kind...\n");
	/* dB and Np are logarithms of a ratio, not linear quantities: 20 dB is a
	 * ratio of 100, not twice 10 dB, so the multiply-by-a-factor model cannot
	 * express a conversion between them: relating two logarithmic scales is a
	 * change of base. ISO 80000-3's 1 Np = 8.685889 dB relates two LEVELS
	 * referred consistently to one kind of quantity, which is not what a
	 * value_unit_t carries — and dB is written against both the power
	 * (10*log10) and field (20*log10) conventions with nothing in the
	 * annotation recording which. Both carried factor 1.0 and no dimension, so
	 * they compared as compatible and "1 dB" converted to "1 Np": wrong by a
	 * factor of 8.7, silently. They are now tracked like bit/byte. */
	value_unit_t dB = BVN_UNIT_NO_PREFIX(bu_decibel);
	value_unit_t Np = BVN_UNIT_NO_PREFIX(bu_neper);
	value_unit_t none = BVN_UNIT_NO_PREFIX(bu_none);
	value_unit_t B  = BVN_UNIT_NO_PREFIX(bu_byte);
	double out = -1.0;

	ASSERT_TRUE(!bvn_units_compatible(dB, Np), "dB and Np are not interconvertible");
	ASSERT_TRUE(!bvn_unit_convert_value(1.0, dB, Np, &out),
		    "1 dB -> Np is refused rather than answered with 1");
	ASSERT_TRUE(!bvn_units_compatible(Np, dB), "...symmetrically");
	ASSERT_TRUE(!bvn_units_compatible(dB, none),
		    "a logarithmic ratio is not a plain number");
	ASSERT_TRUE(!bvn_units_compatible(dB, B),
		    "nor interchangeable with another dimensionless kind");

	/* Each still converts to itself and across its own prefixes. */
	ASSERT_TRUE(bvn_units_compatible(dB, dB) &&
		    bvn_unit_convert_value(3.0, dB, dB, &out) && out == 3.0,
		    "dB -> dB is the identity");
	ASSERT_TRUE(bvn_unit_convert_value(1.0, BVN_UNIT_SI(bu_decibel, si_milli),
					   dB, &out) && out == 0.001,
		    "prefixed dB still scales within its own kind");
	/* pH is the same argument in another field: -log10(activity), so pH 7 is
	 * not "7 of" anything and one unit down is ten times the concentration.
	 * Carrying the empty dimension and factor 1.0, it would otherwise convert
	 * into a plain count or a percentage — wrong by orders of magnitude and
	 * silent, which is the failure this kind table exists to refuse. */
	{
		value_unit_t pH = BVN_UNIT_NO_PREFIX(bu_ph_scale);
		value_unit_t pct = BVN_UNIT_NO_PREFIX(bu_percent);
		double phout = -1.0;

		ASSERT_TRUE(!bvn_units_compatible(pH, none),
			    "pH is not a plain number");
		ASSERT_TRUE(!bvn_units_compatible(pH, pct),
			    "pH is not a percentage");
		ASSERT_TRUE(!bvn_unit_convert_value(7.2, pH, pct, &phout),
			    "7.2 pH -> % is refused rather than answered with 720");
		ASSERT_TRUE(!bvn_units_compatible(pH, dB) &&
			    !bvn_units_compatible(pH, Np),
			    "each logarithmic scale is its own kind");
		ASSERT_TRUE(bvn_units_compatible(pH, pH) &&
			    bvn_unit_convert_value(7.2, pH, pH, &phout) && phout == 7.2,
			    "pH -> pH is the identity");
	}

	/* And the pre-existing kinds are untouched. */
	ASSERT_TRUE(!bvn_units_compatible(B, BVN_UNIT_NO_PREFIX(bu_bit)),
		    "byte and bit stay separate kinds");
	ASSERT_TRUE(bvn_unit_convert_value(1.0, BVN_UNIT_SI(bu_meter, si_kilo),
					   BVN_UNIT_NO_PREFIX(bu_meter), &out) && out == 1000.0,
		    "ordinary units are unaffected");
}

static void test_rational_to_str_reports_too_long(void)
{
	printf("  rational_to_str distinguishes too-long from failure...\n");
	/* bufsize doubles as a work limit, so "does not fit" has to be tellable from
	 * a genuine failure — the reader turns one into a limit error and the other
	 * into invalid_argument. */
	bvn_int_t *n = bvn_int_alloc(), *d = bvn_int_alloc();
	bvn_int_from_str(n, "123456789", 10); bvn_int_from_str(d, "1000", 10);
	char small[8]; bool exact = true;
	ASSERT_TRUE(bvn_rational_to_str(n, d, 10, small, sizeof small, &exact)
		    == BVN_RATIONAL_TOO_LONG, "a short buffer reports TOO_LONG");
	ASSERT_TRUE(!exact, "and does not claim exactness");
	/* A negative value in a sign-less base is a different, genuine failure. */
	bvn_int_from_str(n, "-5", 10); bvn_int_from_uint64(d, 1u);
	char big[64];
	ASSERT_TRUE(bvn_rational_to_str(n, d, 85, big, sizeof big, &exact) == -1,
		    "an unrepresentable sign is still a plain -1");
	bvn_int_free(n); bvn_int_free(d);
}

static void test_wide_denominator_renders_in_every_base(void)
{
	printf("  wide denominators render in large bases too...\n");
	/* The digits used to be produced via base^k, an intermediate that overflows
	 * the big-int ceiling long before the answer does — so a value whose
	 * expansion terminates rendered in base 40 and hard-failed in base 50. */
	bvn_int_t *n = bvn_int_alloc(), *d = bvn_int_alloc(), *t = bvn_int_alloc();
	bvn_int_from_uint64(n, 1u); bvn_int_from_uint64(d, 1u);
	bvn_int_from_uint64(t, 10u);
	for (int i = 0; i < 3000; i++) bvn_int_mul(d, d, t);      /* d = 10^3000 */
	for (uint32_t base = 10; base <= 60; base += 10) {
		size_t need = bvn_rational_str_bufsize(n, d, base);
		char *buf = malloc(need);
		bool exact = false;
		int32_t ret = buf ? bvn_rational_to_str(n, d, base, buf, need, &exact) : -1;
		ASSERT_TRUE(ret > 0 && exact,
			    "1/10^3000 renders exactly in every base, not just small ones");
		free(buf);
	}
	bvn_int_free(n); bvn_int_free(d); bvn_int_free(t);
}


static value_unit_t bvni_test_u2(value_base_unit_t a, unit_exponent_t ea,
				 value_base_unit_t b, unit_exponent_t eb)
{
	value_unit_t u = { .num_components = 2, .components = {
		{ .base = a, .exponent = ea, .prefix = {prefix_si, .id.si = si_none} },
		{ .base = b, .exponent = eb, .prefix = {prefix_si, .id.si = si_none} } } };
	return u;
}

static void test_angle_is_its_own_quantity_kind(void)
{
	printf("  angles are their own quantity kind...\n");
	value_unit_t rad = BVN_UNIT_NO_PREFIX(bu_radian);
	value_unit_t deg = BVN_UNIT_NO_PREFIX(bu_degree);
	value_unit_t rev = BVN_UNIT_NO_PREFIX(bu_revolution);
	value_unit_t sr  = BVN_UNIT_NO_PREFIX(bu_steradian);
	value_unit_t rad2 = rad; rad2.components[0].exponent = exp_square;
	value_unit_t rpm = BVN_UNIT_NO_PREFIX(bu_rpm);
	value_unit_t Hz  = BVN_UNIT_NO_PREFIX(bu_hertz);
	value_unit_t rev_min = bvni_test_u2(bu_revolution, exp_linear,
					    bu_minute, exp_neg_linear);
	value_unit_t rad_s   = bvni_test_u2(bu_radian, exp_linear,
					    bu_second, exp_neg_linear);
	double out = -1.0;

	/* An angular rate is not a cycle rate. rpm counts cycles per minute while a
	 * revolution is 2*pi radians, so these used to convert into each other 2*pi
	 * wrong — silently, because SI calls both dimensionless. */
	ASSERT_TRUE(!bvn_units_compatible(rev_min, rpm),
		    "rev/min and rpm are not interconvertible");
	ASSERT_TRUE(!bvn_unit_convert_value(1.0, rev_min, rpm, &out),
		    "1 rev/min -> rpm is refused rather than answered with 1/(2pi)");
	ASSERT_TRUE(!bvn_units_compatible(rad_s, Hz),
		    "angular frequency is not frequency");
	ASSERT_TRUE(!bvn_units_compatible(rad, BVN_UNIT_NO_PREFIX(bu_none)),
		    "an angle is not a bare count");

	/* The conversions people actually want must survive — this is why angle is
	 * ONE shared kind rather than one kind per unit, as bit and byte are. */
	ASSERT_TRUE(bvn_unit_convert_value(1.0, rev, rad, &out) &&
		    out > 6.283 && out < 6.284, "1 rev -> 2pi rad");
	ASSERT_TRUE(bvn_unit_convert_value(1.0, rev, deg, &out) &&
		    out > 359.99 && out < 360.01, "1 rev -> 360 deg");
	ASSERT_TRUE(bvn_unit_convert_value(180.0, deg, rad, &out) &&
		    out > 3.1415 && out < 3.1416, "180 deg -> pi rad");
	/* A steradian IS rad^2, hence weight 2 rather than a kind of its own. */
	ASSERT_TRUE(bvn_unit_convert_value(1.0, sr, rad2, &out) && out == 1.0,
		    "sr -> rad^2 still converts");
	ASSERT_TRUE(bvn_unit_convert_value(1.0, rad2, sr, &out) && out == 1.0,
		    "...and back");
	/* And nothing outside the kinds moved. */
	ASSERT_TRUE(bvn_unit_convert_value(1.0, rpm, Hz, &out) &&
		    out > 0.01666 && out < 0.01667, "rpm -> Hz is unaffected");
}

static void test_named_si_collapse_respects_kinds(void)
{
	printf("  the named-SI collapse does not cross a quantity kind...\n");
	/* bvni_reduce_to_named_si matched on the SI dimension vector alone, which is
	 * [0,...,0] for every dimensionless kind — so "B/s" collapsed onto "Hz",
	 * turning a data rate into a frequency and producing a unit the library
	 * itself calls incompatible with the one it replaced. */
	static const struct { value_base_unit_t num; const char *keep; } cases[] = {
		{ bu_byte,   "B/s"   },
		{ bu_bit,    "b/s"   },
		{ bu_radian, "rad/s" },
	};
	for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
		value_unit_t u = bvni_test_u2(cases[i].num, exp_linear,
					      bu_second, exp_neg_linear);
		char buf[80];
		int32_t n = bvn_unit_to_string_ex(u, buf, sizeof buf, BVN_UNIT_REDUCE);
		ASSERT_TRUE(n > 0 && strcmp(buf, cases[i].keep) == 0,
			    "a dimensionless kind is not collapsed onto a named SI unit");
		/* whatever it reduces to must remain compatible with the original */
		bool pok = false;
		value_unit_t back = bvn_parse_unit((const uint8_t *)buf, &pok);
		ASSERT_TRUE(pok && bvn_units_compatible(u, back),
			    "the reduced form stays compatible with what it replaced");
	}
}


static void test_affine_unit_in_a_product_has_no_si_value(void)
{
	printf("  an affine scale inside a product is refused, not guessed...\n");
	/* The offset is a number of kelvin. In a product whose SI unit is K/s or
	 * K*m there is nowhere to add it, and the code that tried added it
	 * unscaled: 20 °C/h converted to K/h as 983360, and 20 °C*m to K*m as
	 * 293.15 regardless of the metres. Both are arithmetic on a quantity that
	 * does not exist, delivered with no diagnostic. */
	static const struct { value_base_unit_t other; unit_exponent_t exp;
			      const char *what; } cases[] = {
		{ bu_hour,  exp_neg_linear, "°C/h" },
		{ bu_meter, exp_linear,     "°C*m" },
		{ bu_second, exp_neg_linear, "°C/s" },
	};
	for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
		value_unit_t u = bvni_test_u2(bu_celsius, exp_linear,
					      cases[i].other, cases[i].exp);
		bool aff = false, ok = true; double off = 0.0;
		bvn_unit_to_si_factor(u, &aff, &off, &ok);
		ASSERT_TRUE(!ok, "an affine base in a product has no SI factor");

		/* and the conversion built on it must fail rather than answer */
		value_unit_t k = bvni_test_u2(bu_kelvin, exp_linear,
					      cases[i].other, cases[i].exp);
		double out = 12345.0;
		ASSERT_TRUE(!bvn_unit_convert_value(20.0, u, k, &out),
			    "converting an affine product is refused");
		ASSERT_TRUE(out == 12345.0, "the output is left untouched");
	}
	/* Alone, it still converts exactly as before. */
	double out = 0.0;
	ASSERT_TRUE(bvn_unit_convert_value(20.0, BVN_UNIT_NO_PREFIX(bu_celsius),
					   BVN_UNIT_NO_PREFIX(bu_kelvin), &out),
		    "a lone affine unit still converts");
	ASSERT_TRUE(fabs(out - 293.15) < 1e-9, "20 °C is 293.15 K");
}

static void test_temperature_difference_is_its_own_quantity_kind(void)
{
	printf("  a temperature difference is not a temperature...\n");
	/* 25 °C is 298.15 K; a RISE of 25 degrees is 25 K. Before the Δ units there
	 * was no spelling for the second, so the format converted it as the first —
	 * wrong by 273.15, with the two documents byte-identical. The Δ units carry
	 * their own quantity kind, which is what refuses the conversion that used to
	 * be silently wrong. */
	value_unit_t dK  = BVN_UNIT_NO_PREFIX(bu_delta_kelvin);
	value_unit_t dF  = BVN_UNIT_NO_PREFIX(bu_delta_fahrenheit);
	value_unit_t dDe = BVN_UNIT_NO_PREFIX(bu_delta_delisle);
	value_unit_t K   = BVN_UNIT_NO_PREFIX(bu_kelvin);
	value_unit_t C   = BVN_UNIT_NO_PREFIX(bu_celsius);
	value_unit_t F   = BVN_UNIT_NO_PREFIX(bu_fahrenheit);

	/* The refusals this exists for. Note it is not a dimension check: all six
	 * units below are Θ¹. */
	ASSERT_TRUE(!bvn_units_compatible(dK, K), "ΔK is not K");
	ASSERT_TRUE(!bvn_units_compatible(C, dK), "°C is not ΔK");
	ASSERT_TRUE(!bvn_units_compatible(F, dF), "°F is not Δ°F");
	double out = 4242.0;
	ASSERT_TRUE(!bvn_unit_convert_value(25.0, dK, K, &out),
		    "ΔK -> K is refused rather than answered");
	ASSERT_TRUE(out == 4242.0, "the output is left untouched");
	ASSERT_TRUE(!bvn_unit_convert_value(25.0, C, dK, &out),
		    "°C -> ΔK is refused: only the author knows which was meant");

	/* Δ°C IS ΔK — the Celsius interval is the kelvin (SI Brochure 9th ed.
	 * §2.3.1) — so units.bvnr aliases it rather than adding a row. Two units
	 * that must compare equal and convert by exactly 1 would be a distinction
	 * with no content; this is what pins that they are one unit. */
	bool ok = false;
	value_unit_t dC = bvn_parse_unit((const uint8_t *)"Δ°C", &ok);
	ASSERT_TRUE(ok, "Δ°C parses");
	ASSERT_TRUE(bvn_unit_equal(dC, dK), "Δ°C IS ΔK, not merely equal in value");
	value_unit_t dRa = bvn_parse_unit((const uint8_t *)"Δ°Ra", &ok);
	ASSERT_TRUE(ok, "Δ°Ra parses");
	ASSERT_TRUE(bvn_unit_equal(dRa, dF), "Δ°Ra IS Δ°F");

	/* The intervals convert among themselves, by their scales' own slopes, with
	 * no offset anywhere. Δ°De is negative because Delisle runs backwards. */
	ASSERT_TRUE(bvn_unit_convert_value(9.0, dF, dK, &out),
		    "Δ°F -> ΔK converts");
	ASSERT_TRUE(fabs(out - 5.0) < 1e-12, "9 Δ°F is 5 ΔK (5/9, no offset)");
	ASSERT_TRUE(bvn_unit_convert_value(3.0, dDe, dK, &out),
		    "Δ°De -> ΔK converts");
	ASSERT_TRUE(fabs(out + 2.0) < 1e-12, "3 Δ°De is -2 ΔK (Delisle inverts)");

	/* The SI normal form must reduce an interval to ΔK and never to K. Getting
	 * this wrong is not cosmetic: the form is screened against the unit it came
	 * from, so a K here would report "no SI form" and a normalising pass would
	 * silently leave every temperature difference in a document alone. */
	value_unit_t nf;
	ASSERT_TRUE(bvn_unit_si_normal_form(dF, &nf), "Δ°F has an SI normal form");
	ASSERT_TRUE(bvn_unit_equal(nf, dK), "Δ°F normalises to ΔK, not K");

	/* INSIDE A COMPOUND THE DISTINCTION DOES NOT ARISE, AND IS NOT MADE. An
	 * affine scale is meaningful only alone at exponent 1, so a K in a compound
	 * was ALREADY an interval — W/(m²·K) has always been a U-value. Counting the
	 * kind there would make W/(m²·ΔK) a different unit and break every U-value
	 * written to date, in exchange for separating two spellings of one quantity.
	 * So the kind is significant exactly where the offset was the hazard. */
	static const struct { value_base_unit_t other; unit_exponent_t exp;
			      const char *what; } compounds[] = {
		{ bu_meter,  exp_neg_linear, "per metre (a lapse rate)" },
		{ bu_second, exp_neg_linear, "per second (a heating rate)" },
		{ bu_watt,   exp_linear,     "times a watt" },
	};
	for (size_t i = 0; i < sizeof compounds / sizeof compounds[0]; i++) {
		value_unit_t with_delta = bvni_test_u2(bu_delta_kelvin, exp_linear,
						       compounds[i].other,
						       compounds[i].exp);
		value_unit_t with_kelvin = bvni_test_u2(bu_kelvin, exp_linear,
							compounds[i].other,
							compounds[i].exp);
		ASSERT_TRUE(bvn_units_compatible(with_delta, with_kelvin),
			    "ΔK and K are the same unit inside a compound");
		out = 0.0;
		ASSERT_TRUE(bvn_unit_convert_value(6.5, with_delta, with_kelvin, &out),
			    "and the conversion is the identity");
		ASSERT_TRUE(fabs(out - 6.5) < 1e-12, "factor 1, no rescale");
		/*
		 * "The same unit" means bvn_unit_equal, not merely compatible, and
		 * this line is the one that was missing. Compatibility was already
		 * true and the assertion above already SAID "the same unit", so the
		 * gap was invisible: bvn_unit_equal compares bases structurally, and
		 * bu_delta_kelvin is not bu_kelvin, so the two spellings the design
		 * calls one unit were separated one layer up from the kind scoping.
		 * Equality is what the annotation/inline agreement rule uses, so a
		 * document annotated `W/(m²·ΔK)` with an inline `W/(m²·K)` suffix was
		 * refused as error_unit_mismatch -- the exact U-value the scoping rule
		 * exists to keep working.
		 */
		ASSERT_TRUE(bvn_unit_equal(with_delta, with_kelvin),
			    "...and EQUAL, which is what the document rule compares");
	}
	/* Same at a negative exponent alone: an expansion coefficient. */
	value_unit_t inv_delta  = { .num_components = 1, .components = {
		{ .base = bu_delta_kelvin, .exponent = exp_neg_linear,
		  .prefix = {prefix_si, .id.si = si_none} } } };
	value_unit_t inv_kelvin = { .num_components = 1, .components = {
		{ .base = bu_kelvin, .exponent = exp_neg_linear,
		  .prefix = {prefix_si, .id.si = si_none} } } };
	ASSERT_TRUE(bvn_units_compatible(inv_delta, inv_kelvin),
		    "ΔK^-1 and K^-1 are the same unit");
	ASSERT_TRUE(bvn_unit_equal(inv_delta, inv_kelvin),
		    "ΔK^-1 and K^-1 are EQUAL too");

	/*
	 * And the hazard is untouched: the scoping is "a lone component at
	 * exponent 1", so THERE the two must stay apart. Asserted beside the
	 * merge, because a fix that folded ΔK into K unconditionally would satisfy
	 * every line above and destroy the reason the unit exists.
	 */
	ASSERT_TRUE(!bvn_unit_equal(dK, BVN_UNIT_NO_PREFIX(bu_kelvin)),
		    "a lone ΔK at exponent 1 is still NOT equal to K");
	ASSERT_TRUE(!bvn_units_compatible(dK, BVN_UNIT_NO_PREFIX(bu_kelvin)),
		    "...nor compatible with it");
	/* A prefixed interval in a compound folds the same way. */
	value_unit_t m_delta = { .num_components = 2, .components = {
		{ .base = bu_delta_kelvin, .exponent = exp_linear,
		  .prefix = {prefix_si, .id.si = si_milli} },
		{ .base = bu_meter, .exponent = exp_neg_linear,
		  .prefix = {prefix_si, .id.si = si_none} } } };
	value_unit_t m_kelvin = { .num_components = 2, .components = {
		{ .base = bu_kelvin, .exponent = exp_linear,
		  .prefix = {prefix_si, .id.si = si_milli} },
		{ .base = bu_meter, .exponent = exp_neg_linear,
		  .prefix = {prefix_si, .id.si = si_none} } } };
	ASSERT_TRUE(bvn_unit_equal(m_delta, m_kelvin),
		    "m~ΔK/m equals m~K/m — the prefix rides along");
	/* ...and the OTHER five interval units do not fold: Δ°F is 5/9 K, not a
	 * spelling of the kelvin, and its scale counterpart is affine. */
	ASSERT_TRUE(!bvn_unit_equal(
			bvni_test_u2(bu_delta_fahrenheit, exp_linear,
				     bu_meter, exp_neg_linear),
			bvni_test_u2(bu_kelvin, exp_linear,
				     bu_meter, exp_neg_linear)),
		    "Δ°F/m is not K/m — only ΔK folds");
}

static void test_photometric_units_carry_the_steradian(void)
{
	printf("  luminous flux is not luminous intensity...\n");
	/* lm = cd*sr and lx = lm/m². Without the steradian's kind weight the table
	 * refused lm <-> cd*sr on the kind rule while converting lm <-> cd at
	 * factor 1 — the same claim with the sr silently dropped. */
	value_unit_t lm     = BVN_UNIT_NO_PREFIX(bu_lumen);
	value_unit_t cd     = BVN_UNIT_NO_PREFIX(bu_candela);
	value_unit_t cd_sr  = bvni_test_u2(bu_candela, exp_linear,
					   bu_steradian, exp_linear);
	ASSERT_TRUE(!bvn_units_compatible(lm, cd),
		    "a lumen is not a candela");
	ASSERT_TRUE(bvn_units_compatible(lm, cd_sr),
		    "a lumen IS a candela-steradian");

	value_unit_t lx     = BVN_UNIT_NO_PREFIX(bu_lux);
	value_unit_t cd_m2  = bvni_test_u2(bu_candela, exp_linear,
					   bu_meter, exp_neg_square);
	value_unit_t lm_m2  = bvni_test_u2(bu_lumen, exp_linear,
					   bu_meter, exp_neg_square);
	ASSERT_TRUE(!bvn_units_compatible(lx, cd_m2),
		    "illuminance is not luminance");
	ASSERT_TRUE(bvn_units_compatible(lx, lm_m2),
		    "a lux IS a lumen per square metre");
	/* phot is illuminance (lm/cm²), stilb is luminance (cd/cm²): same SI
	 * dimension vector, and they must stay apart for the same reason. */
	ASSERT_TRUE(!bvn_units_compatible(BVN_UNIT_NO_PREFIX(bu_phot),
					  BVN_UNIT_NO_PREFIX(bu_stilb)),
		    "a phot is not a stilb");
	ASSERT_TRUE(bvn_units_compatible(BVN_UNIT_NO_PREFIX(bu_phot), lx),
		    "a phot IS an illuminance");
}

static void test_named_si_collapse_never_substitutes_a_named_unit(void)
{
	printf("  the named-SI collapse does not rewrite one named unit as another...\n");
	/* Sv and Gy share a dimension vector and carry no kind, so the first match
	 * in bvni_si_named_derived won and BVN_UNIT_REDUCE wrote an equivalent dose
	 * out as an absorbed dose — in the document, not merely in a conversion. */
	static const struct { value_base_unit_t base; const char *keep; } same[] = {
		{ bu_sievert,     "Sv"  },
		{ bu_becquerel,   "Bq"  },
		{ bu_baud,        "Bd"  },
		{ bu_var,         "var" },
		{ bu_volt_ampere, "VA"  },
		{ bu_rem,         "rem" },
		{ bu_candela,     "cd"  },
	};
	for (size_t i = 0; i < sizeof same / sizeof same[0]; i++) {
		char buf[80];
		int32_t n = bvn_unit_to_string_ex(BVN_UNIT_NO_PREFIX(same[i].base),
						  buf, sizeof buf, BVN_UNIT_REDUCE);
		ASSERT_TRUE(n > 0 && strcmp(buf, same[i].keep) == 0,
			    "a named unit reduces to itself, not to a sibling");
	}
	/* An OVERFLOWING reduction is refused, not formatted. bvn_unit_reduce drops
	 * a component when its summed exponent leaves [BVN_EXPONENT_MIN,
	 * BVN_EXPONENT_MAX], and the result then serialises as "no_unit":
	 * dimensionless, and not compatible with what it replaced. The flag was
	 * already raised and was being read only to skip the cosmetic collapse.
	 *
	 * These used to be m⁹·m² and friends, because the cap was 9 — stale by the
	 * whole distance between ±9 and ±100. They are now written at the real
	 * boundary; the m¹¹ cases below prove the same inputs reduce cleanly. */
	{
		static const char *overflowing[] = { "m^100·m^100", "m^60·m^60",
						     "s^99·s^99", "m^100·m^100·s",
						     "k~m^100·m^100" };
		for (size_t i = 0; i < sizeof overflowing / sizeof overflowing[0]; i++) {
			bool pok = false;
			value_unit_t u = bvn_parse_unit((const uint8_t *)overflowing[i], &pok);
			char b[80];
			ASSERT_TRUE(pok, "the over-range product parses");
			ASSERT_TRUE(bvn_unit_to_string_ex(u, b, sizeof b,
							  BVN_UNIT_REDUCE) < 0,
				    "an overflowing reduction is refused, not written");
			/* without REDUCE it is an ordinary unit and still writes */
			ASSERT_TRUE(bvn_unit_to_string_ex(u, b, sizeof b,
							  BVN_UNIT_FLAGS_NONE) > 0,
				    "the unreduced form is unaffected");
		}
		/* And the products that merely leave ±9 reduce and format, which is
		 * the regression the boundary above is guarding. */
		static const char *in_range[] = { "m⁹·m²", "m⁹·m⁹", "s⁹·s³",
						  "m⁹·m²·s", "k~m⁹·m²" };
		for (size_t i = 0; i < sizeof in_range / sizeof in_range[0]; i++) {
			bool pok = false;
			value_unit_t u = bvn_parse_unit((const uint8_t *)in_range[i], &pok);
			char b[80];
			ASSERT_TRUE(pok, "the in-range product parses");
			ASSERT_TRUE(bvn_unit_to_string_ex(u, b, sizeof b,
							  BVN_UNIT_REDUCE) > 0,
				    "an in-range reduction is written, not refused");
		}
	}
	/* The guard must be narrow. A lone base at exponent ONE already names its
	 * quantity; a lone base at any OTHER exponent is a compound in all but
	 * spelling, and collapsing it is what this function is for. Guarding on the
	 * component count alone stopped s⁻¹ becoming Hz — and cost more than that,
	 * because m~s⁻¹ reduces to s⁻¹ with a scale of 1000 that the collapse used
	 * to give back as the k~ on k~Hz. Without it the scale is simply dropped. */
	{
		static const struct { const char *in, *want; } collapses[] = {
			{ "s⁻¹",   "Hz"   },   /* the named form of the same quantity  */
			{ "m~s⁻¹", "k~Hz" },   /* per millisecond IS kilohertz         */
			{ "k~s⁻¹", "m~Hz" },
		};
		for (size_t i = 0; i < sizeof collapses / sizeof collapses[0]; i++) {
			bool pok = false;
			value_unit_t u = bvn_parse_unit((const uint8_t *)collapses[i].in, &pok);
			char b[80];
			ASSERT_TRUE(pok, "the exponent form parses");
			ASSERT_TRUE(bvn_unit_to_string_ex(u, b, sizeof b, BVN_UNIT_REDUCE) > 0 &&
				    strcmp(b, collapses[i].want) == 0,
				    "a lone base at a non-unit exponent still collapses");
		}
	}
	/* What the collapse is FOR still works: a compound folds into the named
	 * unit it spells out, and a prefix folded into the scale comes back. */
	char buf[80];
	value_unit_t as = bvni_test_u2(bu_ampere, exp_linear,
				       bu_second, exp_linear);
	ASSERT_TRUE(bvn_unit_to_string_ex(as, buf, sizeof buf, BVN_UNIT_REDUCE) > 0 &&
		    strcmp(buf, "C") == 0, "A·s still collapses to C");
	value_unit_t kN = BVN_UNIT_NO_PREFIX(bu_newton);
	kN.components[0].prefix.system = prefix_si;
	kN.components[0].prefix.id.si  = si_kilo;
	ASSERT_TRUE(bvn_unit_to_string_ex(kN, buf, sizeof buf, BVN_UNIT_REDUCE) > 0 &&
		    strcmp(buf, "k~N") == 0, "k~N keeps its prefix through REDUCE");
}

static void test_info_prefix_rule_follows_magnitude_not_enum_order(void)
{
	printf("  \"kilo and up\" on bit/byte is a magnitude test...\n");
	/* The rule used to compare enum ids, which only agreed with magnitude
	 * because prefixes.bvnr happens to be listed in ascending order — while
	 * that file requires ids to be append-only. */
	static const struct { si_prefix_id_t p; bool want; } cases[] = {
		{ si_none,   true  }, { si_kilo,  true  }, { si_mega, true },
		{ si_quetta, true  }, { si_hecto, false }, { si_deca, false },
		{ si_milli,  false }, { si_quecto, false },
	};
	for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
		value_unit_prefix_t pfx = { prefix_si, .id.si = cases[i].p };
		ASSERT_TRUE(bvn_prefix_unit_valid(pfx, bu_byte) == cases[i].want,
			    "the info prefix rule admits exactly kilo and up");
		ASSERT_TRUE(bvn_prefix_unit_valid(pfx, bu_bit) == cases[i].want,
			    "and says the same for the bit");
		/* every one of them is fine on an ordinary unit */
		ASSERT_TRUE(bvn_prefix_unit_valid(pfx, bu_meter),
			    "an ordinary unit takes any SI prefix");
	}
}

static void test_prefixed_currency_converts(void)
{
	printf("  a prefixed currency converts to its unprefixed form...\n");
	/* Currencies carry no SI dimension by design, so bvn_unit_dimension_vector
	 * fails for them and bvn_units_compatible calls every currency incompatible
	 * even with itself. The identity short-circuit rescued "$USD -> $USD", but
	 * "k~$USD -> $USD" is not an identity — it differs by one prefix, with
	 * nothing between the two but a factor of 1000. Every entry point refused it,
	 * so a prefixed currency could neither be read through want_unit nor written
	 * under BVN_UNIT_REDUCE. */
	bool ok1 = false, ok2 = false;
	value_unit_t kusd = bvn_parse_unit((const uint8_t *)"k~$USD", &ok1);
	value_unit_t usd  = bvn_parse_unit((const uint8_t *)"$USD",   &ok2);
	if (!ok1 || !ok2) { ASSERT_TRUE(false, "currency units parse"); return; }

	double out = -1.0;
	ASSERT_TRUE(bvn_unit_convert_value(5.0, kusd, usd, &out) && out == 5000.0,
		    "5 k$USD is 5000 $USD");
	out = -1.0;
	ASSERT_TRUE(bvn_unit_convert_value(5000.0, usd, kusd, &out) && out == 5.0,
		    "...and back");
	/* exact too: the scale is a power of ten, so the rational path stays lossless
	 * without ever consulting si_conv_table, which has no row for a currency */
	check_rat("5", 10, true, kusd, usd, 10, true, "5000", "5 k$USD -> 5000 $USD exactly");
	check_rat("5000", 10, true, usd, kusd, 10, true, "5", "5000 $USD -> 5 k$USD exactly");

	/* A compound carrying a currency works the same way. */
	bool ok3 = false, ok4 = false;
	value_unit_t a = bvn_parse_unit((const uint8_t *)"$USD/oz_t",   &ok3);
	value_unit_t b = bvn_parse_unit((const uint8_t *)"k~$USD/oz_t", &ok4);
	if (ok3 && ok4)
		check_rat("2000", 10, true, a, b, 10, true, "2",
			  "2000 $USD/oz_t -> 2 k$USD/oz_t");

	/* Genuinely different currencies must STILL be refused — the match is on the
	 * base units, so only the prefix may differ. */
	bool oke = false;
	value_unit_t eur = bvn_parse_unit((const uint8_t *)"$EUR", &oke);
	if (oke) {
		out = -1.0;
		ASSERT_TRUE(!bvn_unit_convert_value(5.0, usd, eur, &out),
			    "$USD does not convert to $EUR");
		ASSERT_TRUE(!bvn_unit_convert_value(5.0, kusd, eur, &out),
			    "...nor does a prefixed one");
	}
	/* And a physical unit still takes the ordinary path, unchanged. */
	out = -1.0;
	ASSERT_TRUE(bvn_unit_convert_value(5.0, BVN_UNIT_SI(bu_meter, si_kilo),
					   BVN_UNIT_NO_PREFIX(bu_meter), &out) &&
		    out == 5000.0, "physical units are unaffected");
	out = -1.0;
	ASSERT_TRUE(!bvn_unit_convert_value(5.0, BVN_UNIT_NO_PREFIX(bu_meter),
					    BVN_UNIT_NO_PREFIX(bu_second), &out),
		    "and a real dimension mismatch is still a mismatch");
}

/*
 * THE TWO CONVERSION ENGINES MUST AGREE, FOR EVERY PAIR THEY BOTH ACCEPT.
 *
 * bvn_unit_convert_value works in doubles off si_conv_table's .to_si_factor;
 * bvn_unit_convert_rational works in bignums off the .factor_num/.factor_den
 * columns of the same rows. Two independent numbers per unit, edited by hand in
 * units.bvnr, and until now only spot-checked against each other on the handful
 * of pairs somebody thought to write a case for. A factor whose decimal and
 * whose rational disagree is the worst shape of defect this library can have:
 * both paths answer confidently, and which one a caller gets depends on whether
 * it asked for losslessness.
 *
 * So: sweep the whole registry, both directions, and compare wherever both
 * engines deliver. Also checked is that they AGREE ON REFUSING -- a pair one
 * converts and the other declines is the same defect wearing a different hat.
 *
 * The second sweep adds prefixes and exponents, which is where the rational
 * path stops using si_conv_table at all and does its own powers of ten and two
 * (bvn_int_mul_pow10 / bvn_int_shl in the prefix-only shortcut).
 *
 * Only terminating expansions are compared: a non-terminating one is exact as a
 * rational and has no decimal to hold against the double, which is the point of
 * BVN_RATIONAL_NONTERMINATING.
 */
static void compare_engines(value_unit_t ua, value_unit_t ub,
                            long *compared, long *disagreed, const char *what)
{
	double dv = 0.0;
	bool dok = bvn_unit_convert_value(1.0, ua, ub, &dv);
	bvn_int_t *vn = bvn_int_alloc(), *vd = bvn_int_alloc();
	bvn_int_t *on = bvn_int_alloc(), *od = bvn_int_alloc();
	if (!vn || !vd || !on || !od) {
		bvn_int_free(vn); bvn_int_free(vd); bvn_int_free(on); bvn_int_free(od);
		return;
	}
	bvn_int_from_uint64(vn, 1u);
	bvn_int_from_uint64(vd, 1u);
	bool exact = false;
	bool rok = bvn_unit_convert_rational(vn, vd, ua, ub, on, od, &exact);
	if (dok != rok) {
		(*disagreed)++;
		if (*disagreed <= 10)
			fprintf(stderr, "  %s: %d -> %d: double says %d, rational says %d\n",
			        what, (int)ua.components[0].base,
			        (int)ub.components[0].base, (int)dok, (int)rok);
	} else if (rok && exact) {
		char buf[4096];
		bool rex = false;
		int32_t len = bvn_rational_to_str(on, od, 10u, buf, sizeof buf, &rex);
		if (len > 0 && rex) {
			double rv = strtod(buf, NULL);
			double tol = fabs(dv) * 1e-9 + 1e-300;
			if (fabs(rv - dv) <= tol) {
				(*compared)++;
			} else {
				(*disagreed)++;
				if (*disagreed <= 10)
					fprintf(stderr, "  %s: %d -> %d: double %.17g, rational %s\n",
					        what, (int)ua.components[0].base,
					        (int)ub.components[0].base, dv, buf);
			}
		}
	}
	bvn_int_free(vn); bvn_int_free(vd); bvn_int_free(on); bvn_int_free(od);
}

static void test_both_conversion_engines_agree(void)
{
	printf("  every convertible pair, both engines...\n");
	long compared = 0, disagreed = 0;

	for (int a = BVN_UNIT_NATIVE_FIRST; a <= BVN_UNIT_NATIVE_LAST; a++) {
		for (int b = BVN_UNIT_NATIVE_FIRST; b <= BVN_UNIT_NATIVE_LAST; b++) {
			value_unit_t ua = BVN_UNIT_NO_PREFIX((value_base_unit_t)a);
			value_unit_t ub = BVN_UNIT_NO_PREFIX((value_base_unit_t)b);
			if (!bvn_unit_valid(ua) || !bvn_unit_valid(ub))
				continue;
			compare_engines(ua, ub, &compared, &disagreed, "bare");
		}
	}
	/* 1986 native pairs convert; 110 have an irrational factor and 1079 more
	 * expand non-terminating in base 10, leaving these to compare. */
	ASSERT_TRUE(compared > 700, "the bare sweep really compared something");
	ASSERT_EQ_INT(disagreed, 0, "double and rational agree on every bare pair");

	static const si_prefix_id_t PFX[] = {
		si_none, si_milli, si_kilo, si_micro, si_mega
	};
	static const int EXP[] = { 1, 2, -1, -3 };
	long pcompared = 0, pdisagreed = 0;
	for (int a = BVN_UNIT_NATIVE_FIRST; a <= BVN_UNIT_NATIVE_LAST; a++) {
		for (int b = BVN_UNIT_NATIVE_FIRST; b <= BVN_UNIT_NATIVE_LAST; b++) {
			for (size_t p = 0; p < sizeof PFX / sizeof PFX[0]; p++) {
				for (size_t e = 0; e < sizeof EXP / sizeof EXP[0]; e++) {
					unit_exponent_t ex = bvn_int_to_exponent(EXP[e]);
					value_unit_t ua = BVN_UNIT_SI_EXP(
						(value_base_unit_t)a, PFX[p], ex);
					value_unit_t ub = BVN_UNIT_SI_EXP(
						(value_base_unit_t)b, si_none, ex);
					if (!bvn_unit_valid(ua) || !bvn_unit_valid(ub))
						continue;
					compare_engines(ua, ub, &pcompared, &pdisagreed,
					                "prefixed");
				}
			}
		}
	}
	ASSERT_TRUE(pcompared > 10000, "the prefixed sweep really compared something");
	ASSERT_EQ_INT(pdisagreed, 0,
	              "double and rational agree with prefixes and exponents too");
	printf("    %ld bare + %ld prefixed comparisons\n", compared, pcompared);
}

int main(void)
{
	printf("══════════════════════════════════════\n");
	printf("  Bovnar SI Units Test Suite\n");
	printf("══════════════════════════════════════\n\n");

	test_exponent_to_int();
	test_exp_invalid_is_zero_init();
	test_int_to_exponent();
	test_si_factor_simple();
	test_si_factor_derived();
	test_si_factor_compound();
	test_si_factor_affine_nonlinear();
	test_si_factor_affine_compound();
	test_si_factor_invalid_prefix_combo();
	test_dimension_vector();
	test_dimension_vector_invalid_prefix();
	test_units_compatible();
	test_units_compatible_info();
	test_units_compatible_exp_invalid_info();
	test_convert_factor();
	test_convert_factor_error_kinds();
	test_convert_factor_prefixed_affine();
	test_convert_factor_prefixed_affine_to_nonaffine();
	test_convert_value();
	test_convert_rational();
	test_rational_to_str_bases();
	test_rational_to_str_never_truncates();
	test_exact_factor_table();
	test_prefix_exponent_interaction();
	test_rational_matches_double_path();
	test_identity_still_validates_structure();
	test_malformed_exponent_never_aborts();
	test_unwritable_units_are_refused();
	test_reduce_keeps_a_prefixed_dimensionless_scale();
	test_convert_factor_identity();
	test_prefixed_currency_converts();
	test_logarithmic_units_refuse();
	test_angle_is_its_own_quantity_kind();
	test_named_si_collapse_respects_kinds();
	test_named_si_collapse_never_substitutes_a_named_unit();
	test_affine_unit_in_a_product_has_no_si_value();
	test_photometric_units_carry_the_steradian();
	test_temperature_difference_is_its_own_quantity_kind();
	test_info_prefix_rule_follows_magnitude_not_enum_order();
	test_rational_to_str_reports_too_long();
	test_wide_denominator_renders_in_every_base();
	test_unit_reduce();
	test_unit_reduce_full_cancel_si();
	test_unit_reduce_iec();
	test_unit_reduce_exponent_overflow();
	test_unit_reduce_overflow_flag();
	test_unit_reduce_bu_none_component();
	test_value_conversion_examples();
	test_parse_and_factor();
	test_all_derived_dimensions();
	test_si_factor_degree();
	test_out_parameters_are_optional();
	test_both_conversion_engines_agree();
	test_si_factor_refuses_what_a_double_cannot_hold();
	test_si_factor_undefined_base();
	test_si_factor_num_components_overflow();
	test_parse_unit_n();
	test_parse_unit_parens();
	test_parse_no_unit_normalised();
	test_unit_to_string_num_components_clamp();
	test_neg_exp_round_trip();
	test_prefix_unit_valid_out_of_range();
	test_si_prefix_info_unit_restrictions();

	printf("\n──────────────────────────────────────\n");
	printf("  Results: %d tests, %d failures\n", tests, failures);
	printf("──────────────────────────────────────\n");

	return failures ? 1 : 0;
}
