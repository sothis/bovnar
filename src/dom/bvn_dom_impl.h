#ifndef BVN_DOM_IMPL_H_
#define BVN_DOM_IMPL_H_
#include "bovnar.h"
#include "bovnar_dom.h"
#include "bvn_int.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
struct bvn_dom_node_s {
	bvn_dom_type_t       type;
	value_type_spec_t    value_type;
	value_unit_t         value_unit;
	union {
		int64_t       int_val;
		bvn_int_t *bigint;
		double        float_val;
		struct { char    *data; uint32_t len; } str;
		struct { uint8_t *data; uint32_t len; } octets;
	} val;
	struct {
		bvn_dom_entry_t *entries;
		uint32_t         count;
		uint32_t         cap;
	} members;
	struct {
		bvn_dom_node_t **items;
		uint32_t         count;
		uint32_t         cap;
		uint32_t         num_dims;
		uint32_t         rows_per_dim[8];
	} arr;
};
struct bvn_dom_doc_s {
	bvn_dom_entry_t *entries;
	uint32_t         count;
	uint32_t         cap;
	error_code_t     parse_error;
};
#endif
