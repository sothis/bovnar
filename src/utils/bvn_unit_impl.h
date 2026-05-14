#ifndef BVN_UNIT_IMPL_H_
#define BVN_UNIT_IMPL_H_
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include "bovnar.h"
#include "bvn_internal_dims.h"
#include "bovnar_si_units.h"
typedef struct {
	double  factor;
	int32_t exp;
} bvni_pfx_t;
static const bvni_pfx_t bvni_si_pfx_table[] = {
	[si_none]   = {1e0,   0},
	[si_quecto] = {1e-30,-30}, [si_ronto]  = {1e-27,-27}, [si_yocto] = {1e-24,-24},
	[si_zepto]  = {1e-21,-21}, [si_atto]   = {1e-18,-18}, [si_femto] = {1e-15,-15},
	[si_pico]   = {1e-12,-12}, [si_nano]   = {1e-9,  -9}, [si_micro] = {1e-6,  -6},
	[si_milli]  = {1e-3,  -3}, [si_centi]  = {1e-2,  -2}, [si_deci]  = {1e-1,  -1},
	[si_deca]   = {1e1,    1}, [si_hecto]  = {1e2,    2}, [si_kilo]  = {1e3,    3},
	[si_mega]   = {1e6,    6}, [si_giga]   = {1e9,    9}, [si_tera]  = {1e12,  12},
	[si_peta]   = {1e15,  15}, [si_exa]    = {1e18,  18}, [si_zetta] = {1e21,  21},
	[si_yotta]  = {1e24,  24}, [si_ronna]  = {1e27,  27}, [si_quetta]= {1e30,  30},
};
static const bvni_pfx_t bvni_iec_pfx_table[] = {
	[iec_none]  = {1.0,                                            0},
	[iec_kibi]  = {(double)(1ull << 10),                          10},
	[iec_mebi]  = {(double)(1ull << 20),                          20},
	[iec_gibi]  = {(double)(1ull << 30),                          30},
	[iec_tebi]  = {(double)(1ull << 40),                          40},
	[iec_pebi]  = {(double)(1ull << 50),                          50},
	[iec_exbi]  = {(double)(1ull << 60),                          60},
	[iec_zebi]  = {(double)(1ull << 35) * (double)(1ull << 35),   70},
	[iec_yobi]  = {(double)(1ull << 40) * (double)(1ull << 40),   80},
	[iec_robi]  = {(double)(1ull << 45) * (double)(1ull << 45),   90},
	[iec_quebi] = {(double)(1ull << 50) * (double)(1ull << 50),  100},
};
static inline double bvni_prefix_factor(value_unit_component_t c)
{
	if (c.prefix.system == prefix_iec) {
		if (c.prefix.id.iec < BVN_IEC_PREFIX_COUNT)
			return bvni_iec_pfx_table[c.prefix.id.iec].factor;
		return 1.0;
	}
	if (c.prefix.id.si < BVN_SI_PREFIX_COUNT)
		return bvni_si_pfx_table[c.prefix.id.si].factor;
	return 1.0;
}
static inline bool bvni_is_neg_exp(unit_exponent_t e)
{
	return e == exp_neg_linear  || e == exp_neg_square  ||
	       e == exp_neg_cubic   || e == exp_neg_quartic ||
	       e == exp_neg_quintic || e == exp_neg_sextic  ||
	       e == exp_neg_septic  || e == exp_neg_octic   ||
	       e == exp_neg_nonic;
}
static inline int32_t bvni_exp_abs(unit_exponent_t e)
{
	assert(e != exp_invalid);
	int32_t v = bvn_exponent_to_int(e);
	return v < 0 ? -v : v;
}
static inline int32_t bvni_prefix_exp_int(value_unit_component_t c)
{
	assert(c.exponent != exp_invalid);
	int32_t base_exp;
	if (c.prefix.system == prefix_iec) {
		base_exp = (c.prefix.id.iec < BVN_IEC_PREFIX_COUNT)
		         ? bvni_iec_pfx_table[c.prefix.id.iec].exp : 0;
	} else {
		base_exp = (c.prefix.id.si < BVN_SI_PREFIX_COUNT)
		         ? bvni_si_pfx_table[c.prefix.id.si].exp : 0;
	}
	int32_t abs_exp = bvni_exp_abs(c.exponent);
	int32_t result  = base_exp * abs_exp;
	if (bvni_is_neg_exp(c.exponent))
		result = -result;
	return result;
}
#endif
