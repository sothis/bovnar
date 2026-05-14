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
int32_t bvn_exponent_to_int(unit_exponent_t e);
unit_exponent_t bvn_int_to_exponent(int32_t n);
double bvn_unit_to_si_factor(value_unit_t u,
                              bool *is_affine,
                              double *affine_offset,
                              bool *ok);
value_unit_t bvn_unit_reduce(value_unit_t u, double *scale, bool *overflow);
bool bvn_unit_dimension_vector(value_unit_t u,
                                int32_t dims[bvn_si_dim_count]);
bool bvn_units_compatible(value_unit_t a, value_unit_t b);
double bvn_unit_convert_factor(value_unit_t a, value_unit_t b,
                                bool *ok, bool *requires_affine);
bool bvn_prefix_unit_valid(value_unit_prefix_t prefix, value_base_unit_t base);
#ifdef __cplusplus
}
#endif
#endif
