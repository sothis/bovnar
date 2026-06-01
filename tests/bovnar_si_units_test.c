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
	ASSERT_TRUE(bvn_int_to_exponent(99) == exp_invalid,  "99 → exp_invalid (out of range)");
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

static void test_si_factor_out_of_range_base(void)
{
	printf("  si_factor out-of-range base...\n");
	bool aff; double off; bool ok = true;

	value_unit_t bad = {
		.num_components = 1,
		.components = {
			{ (value_base_unit_t)BVN_VALUE_BASE_UNIT_COUNT,
			  exp_linear,
			  {prefix_si, .id.si = si_none} }
		}
	};
	bvn_unit_to_si_factor(bad, &aff, &off, &ok);
	ASSERT_TRUE(!ok, "out-of-range base → ok=false");
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

	value_unit_t m5_m5 = {
		.num_components = 2,
		.components = {
			{ bu_meter, exp_quintic, {prefix_si, .id.si=si_none} },
			{ bu_meter, exp_quintic, {prefix_si, .id.si=si_none} }
		}
	};
	overflow = false;
	bvn_unit_reduce(m5_m5, &scale, &overflow);
	ASSERT_TRUE(overflow, "m^5*m^5 reduce: overflow=true (exp 10 exceeds range)");

	value_unit_t g5_g5 = {
		.num_components = 2,
		.components = {
			{ bu_gram, exp_quintic, {prefix_si, .id.si=si_none} },
			{ bu_gram, exp_quintic, {prefix_si, .id.si=si_none} }
		}
	};
	overflow = false;
	scale    = 1.0;
	bvn_unit_reduce(g5_g5, &scale, &overflow);
	ASSERT_TRUE(overflow, "g^5*g^5 reduce: overflow=true (exp 10 exceeds range)");
	ASSERT_EQ_DBL(scale, 1e-30, 1e-40,
	              "g^5*g^5 reduce: scale=1e-30 (gram to_si_factor 1e-3 ^ 10)");

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
	                (value_base_unit_t)BVN_VALUE_BASE_UNIT_COUNT),
	            "out-of-range base: prefix_valid=false");
	ASSERT_TRUE(!bvn_prefix_unit_valid(pfx_si_none,
	                (value_base_unit_t)(BVN_VALUE_BASE_UNIT_COUNT + 5)),
	            "far out-of-range base: prefix_valid=false");

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
	test_si_factor_out_of_range_base();
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
