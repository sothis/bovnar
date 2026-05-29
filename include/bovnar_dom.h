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

#ifndef BOVNAR_DOM_H_
#define BOVNAR_DOM_H_
#include "bovnar.h"
#include "bvn_int.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef enum bvn_dom_type_e {
	BVN_DOM_NULL,
	BVN_DOM_INT,
	BVN_DOM_FLOAT,
	BVN_DOM_STRING,
	BVN_DOM_SYMBOL,
	BVN_DOM_REFERENCE,
	BVN_DOM_STRUCT,
	BVN_DOM_ARRAY,
	BVN_DOM_OCTET_STREAM,
	BVN_DOM_BOOL
} bvn_dom_type_t;
typedef struct bvn_dom_node_s  bvn_dom_node_t;
typedef struct bvn_dom_doc_s   bvn_dom_doc_t;
typedef struct bvn_dom_entry_s {
	char            *key;
	bvn_dom_node_t  *value;
} bvn_dom_entry_t;
BVN_API bvn_dom_doc_t *bvn_dom_doc_create(void);
BVN_API void           bvn_dom_doc_destroy(bvn_dom_doc_t *doc);
BVN_API bvn_dom_doc_t *bvn_dom_parse(const void *data, uint32_t len);
BVN_API bvn_dom_doc_t *bvn_dom_parse_fd(int fd);
BVN_API bvn_dom_doc_t *bvn_dom_parse_fd_ex(int fd, uint64_t max_bytes);
BVN_API error_code_t   bvn_dom_doc_get_parse_error(const bvn_dom_doc_t *doc);
BVN_API bvn_dom_node_t *bvn_dom_lookup(const bvn_dom_doc_t *doc,
							   const char *path);
BVN_API bvn_dom_node_t *bvn_dom_struct_get(const bvn_dom_node_t *node,
								   const char *key);
BVN_API bvn_dom_node_t *bvn_dom_array_at(const bvn_dom_node_t *node,
								 uint32_t index);
BVN_API bvn_dom_type_t  bvn_dom_node_type(const bvn_dom_node_t *node);
BVN_API bool            bvn_dom_is_null(const bvn_dom_node_t *node);
BVN_API bool bvn_dom_get_float(const bvn_dom_node_t *node, double *out);
BVN_API bool bvn_dom_get_bool(const bvn_dom_node_t *node, bool *out);
BVN_API bool bvn_dom_get_string(const bvn_dom_node_t *node,
						const char **out, uint32_t *len);
BVN_API bool bvn_dom_get_symbol(const bvn_dom_node_t *node,
						const char **out, uint32_t *len);
BVN_API bool bvn_dom_get_reference(const bvn_dom_node_t *node,
						   const char **out, uint32_t *len);
BVN_API bool bvn_dom_get_octets(const bvn_dom_node_t *node,
						const uint8_t **out, uint32_t *len);
BVN_API value_type_spec_t bvn_dom_get_value_type(const bvn_dom_node_t *node);
BVN_API value_unit_t      bvn_dom_get_unit(const bvn_dom_node_t *node);
BVN_API int32_t bvn_dom_get_unit_string(const bvn_dom_node_t *node,
								char *buf, size_t bufsize);
BVN_API double bvn_dom_get_value_in_base_units(const bvn_dom_node_t *node);
BVN_API uint32_t bvn_dom_struct_count(const bvn_dom_node_t *node);
BVN_API uint32_t bvn_dom_array_count(const bvn_dom_node_t *node);
BVN_API uint32_t bvn_dom_array_dims(const bvn_dom_node_t *node);
BVN_API const bvn_dom_entry_t *bvn_dom_struct_entries(const bvn_dom_node_t *node);
BVN_API const bvn_dom_entry_t *bvn_dom_doc_entries(const bvn_dom_doc_t *doc);
BVN_API uint32_t               bvn_dom_doc_count(const bvn_dom_doc_t *doc);
BVN_API bvn_dom_node_t *bvn_dom_node_alloc(bvn_dom_type_t t);
BVN_API void            bvn_dom_node_destroy(bvn_dom_node_t *n);
BVN_API bool bvn_dom_struct_add(bvn_dom_node_t *s,
						const char *key, uint32_t klen,
						bvn_dom_node_t *val);
BVN_API bool bvn_dom_doc_add(bvn_dom_doc_t *d,
					 const char *key, uint32_t klen,
					 bvn_dom_node_t *val);
BVN_API bool bvn_dom_array_append(bvn_dom_node_t *a, bvn_dom_node_t *elem);
BVN_API char *bvn_dom_strdup(const char *s, uint32_t len);
BVN_API const bvn_int_t *bvn_dom_get_bigint(const bvn_dom_node_t *node);
BVN_API char *bvn_dom_int_to_str(const bvn_dom_node_t *node, uint32_t base);
BVN_API void  bvn_dom_free_string(char *s);
BVN_API bool bvn_dom_get_i64(const bvn_dom_node_t *node, int64_t  *out);
BVN_API bool bvn_dom_get_u64(const bvn_dom_node_t *node, uint64_t *out);
BVN_API bool bvn_dom_get_i32(const bvn_dom_node_t *node, int32_t  *out);
BVN_API bool bvn_dom_get_u32(const bvn_dom_node_t *node, uint32_t *out);
BVN_API bool bvn_dom_get_i16(const bvn_dom_node_t *node, int16_t  *out);
BVN_API bool bvn_dom_get_u16(const bvn_dom_node_t *node, uint16_t *out);
BVN_API bool bvn_dom_get_i8 (const bvn_dom_node_t *node, int8_t   *out);
BVN_API bool bvn_dom_get_u8 (const bvn_dom_node_t *node, uint8_t  *out);
BVN_API bvn_dom_node_t *bvn_dom_node_from_i64(int64_t  v);
BVN_API bvn_dom_node_t *bvn_dom_node_from_u64(uint64_t v);
BVN_API bvn_dom_node_t *bvn_dom_node_from_i32(int32_t  v);
BVN_API bvn_dom_node_t *bvn_dom_node_from_u32(uint32_t v);
BVN_API bvn_dom_node_t *bvn_dom_node_from_i16(int16_t  v);
BVN_API bvn_dom_node_t *bvn_dom_node_from_u16(uint16_t v);
BVN_API bvn_dom_node_t *bvn_dom_node_from_i8 (int8_t   v);
BVN_API bvn_dom_node_t *bvn_dom_node_from_u8 (uint8_t  v);
BVN_API bvn_dom_node_t *bvn_dom_node_from_bigint(bvn_int_t     *bigint,
										  value_type_spec_t vt,
										  value_unit_t      vu);
#ifdef __cplusplus
}
#endif
#endif
