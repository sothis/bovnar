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
