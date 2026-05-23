#ifndef BVN_INTERNAL_DIMS_H_
#define BVN_INTERNAL_DIMS_H_
#include "bovnar.h"
#define BVN_EVENT_COUNT             14
#define BVN_ERROR_COUNT             39
#define BVN_PREFIX_SYSTEM_COUNT      2
#define BVN_SI_PREFIX_COUNT         25
#define BVN_IEC_PREFIX_COUNT        11
#define BVN_VALUE_BASE_UNIT_COUNT  342
typedef char bvn_internal_dims_event_check[
	(ev_type_annotation_type_family_parameter + 1 == BVN_EVENT_COUNT) ? 1 : -1];
typedef char bvn_internal_dims_error_check[
	(error_unit_mismatch + 1 == BVN_ERROR_COUNT) ? 1 : -1];
typedef char bvn_internal_dims_prefix_system_check[
	(prefix_iec + 1 == BVN_PREFIX_SYSTEM_COUNT) ? 1 : -1];
typedef char bvn_internal_dims_si_prefix_check[
	(si_quetta + 1 == BVN_SI_PREFIX_COUNT) ? 1 : -1];
typedef char bvn_internal_dims_iec_prefix_check[
	(iec_quebi + 1 == BVN_IEC_PREFIX_COUNT) ? 1 : -1];
typedef char bvn_internal_dims_value_base_unit_check[
	(bu_scheffel + 1 == BVN_VALUE_BASE_UNIT_COUNT) ? 1 : -1];
#endif
