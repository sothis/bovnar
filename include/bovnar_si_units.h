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

#ifndef BOVNAR_SI_UNITS_H_
#define BOVNAR_SI_UNITS_H_
#include "bovnar.h"
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef enum bvn_si_dim_idx_e {
	bvn_si_dim_meter    = 0,
	bvn_si_dim_kilogram = 1,
	bvn_si_dim_second   = 2,
	bvn_si_dim_ampere   = 3,
	bvn_si_dim_kelvin   = 4,
	bvn_si_dim_mol      = 5,
	bvn_si_dim_candela  = 6,
	bvn_si_dim_count    = 7
} bvn_si_dim_idx_t;
BVN_API int32_t bvn_exponent_to_int(unit_exponent_t e);
BVN_API unit_exponent_t bvn_int_to_exponent(int32_t n);
BVN_API double bvn_unit_to_si_factor(value_unit_t u,
                              bool *is_affine,
                              double *affine_offset,
                              bool *ok);
BVN_API value_unit_t bvn_unit_reduce(value_unit_t u, double *scale, bool *overflow);
BVN_API bool bvn_unit_dimension_vector(value_unit_t u,
                                int32_t dims[bvn_si_dim_count]);
BVN_API bool bvn_units_compatible(value_unit_t a, value_unit_t b);
BVN_API double bvn_unit_convert_factor(value_unit_t a, value_unit_t b,
                                bool *ok, bool *requires_affine);
BVN_API bool bvn_prefix_unit_valid(value_unit_prefix_t prefix, value_base_unit_t base);
#ifdef __cplusplus
}
#endif
#endif
