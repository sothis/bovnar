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
	BVN_DOM_OCTET_STREAM
} bvn_dom_type_t;
typedef struct bvn_dom_node_s  bvn_dom_node_t;
typedef struct bvn_dom_doc_s   bvn_dom_doc_t;
typedef struct bvn_dom_entry_s {
	char            *key;
	bvn_dom_node_t  *value;
} bvn_dom_entry_t;
bvn_dom_doc_t *bvn_dom_doc_create(void);
void           bvn_dom_doc_destroy(bvn_dom_doc_t *doc);
bvn_dom_doc_t *bvn_dom_parse(const void *data, uint32_t len);
bvn_dom_doc_t *bvn_dom_parse_fd(int fd);
bvn_dom_doc_t *bvn_dom_parse_fd_ex(int fd, uint64_t max_bytes);
error_code_t   bvn_dom_doc_get_parse_error(const bvn_dom_doc_t *doc);
bvn_dom_node_t *bvn_dom_lookup(const bvn_dom_doc_t *doc,
							   const char *path);
bvn_dom_node_t *bvn_dom_struct_get(const bvn_dom_node_t *node,
								   const char *key);
bvn_dom_node_t *bvn_dom_array_at(const bvn_dom_node_t *node,
								 uint32_t index);
bvn_dom_type_t  bvn_dom_node_type(const bvn_dom_node_t *node);
bool            bvn_dom_is_null(const bvn_dom_node_t *node);
bool bvn_dom_get_float(const bvn_dom_node_t *node, double *out);
bool bvn_dom_get_string(const bvn_dom_node_t *node,
						const char **out, uint32_t *len);
bool bvn_dom_get_symbol(const bvn_dom_node_t *node,
						const char **out, uint32_t *len);
bool bvn_dom_get_reference(const bvn_dom_node_t *node,
						   const char **out, uint32_t *len);
bool bvn_dom_get_octets(const bvn_dom_node_t *node,
						const uint8_t **out, uint32_t *len);
value_type_spec_t bvn_dom_get_value_type(const bvn_dom_node_t *node);
value_unit_t      bvn_dom_get_unit(const bvn_dom_node_t *node);
int32_t bvn_dom_get_unit_string(const bvn_dom_node_t *node,
								char *buf, size_t bufsize);
double bvn_dom_get_value_in_base_units(const bvn_dom_node_t *node);
uint32_t bvn_dom_struct_count(const bvn_dom_node_t *node);
uint32_t bvn_dom_array_count(const bvn_dom_node_t *node);
uint32_t bvn_dom_array_dims(const bvn_dom_node_t *node);
const bvn_dom_entry_t *bvn_dom_struct_entries(const bvn_dom_node_t *node);
const bvn_dom_entry_t *bvn_dom_doc_entries(const bvn_dom_doc_t *doc);
uint32_t               bvn_dom_doc_count(const bvn_dom_doc_t *doc);
bvn_dom_node_t *bvn_dom_node_alloc(bvn_dom_type_t t);
void            bvn_dom_node_destroy(bvn_dom_node_t *n);
bool bvn_dom_struct_add(bvn_dom_node_t *s,
						const char *key, uint32_t klen,
						bvn_dom_node_t *val);
bool bvn_dom_doc_add(bvn_dom_doc_t *d,
					 const char *key, uint32_t klen,
					 bvn_dom_node_t *val);
bool bvn_dom_array_append(bvn_dom_node_t *a, bvn_dom_node_t *elem);
char *bvn_dom_strdup(const char *s, uint32_t len);
const bvn_int_t *bvn_dom_get_bigint(const bvn_dom_node_t *node);
char *bvn_dom_int_to_str(const bvn_dom_node_t *node, uint32_t base);
bool bvn_dom_get_i64(const bvn_dom_node_t *node, int64_t  *out);
bool bvn_dom_get_u64(const bvn_dom_node_t *node, uint64_t *out);
bool bvn_dom_get_i32(const bvn_dom_node_t *node, int32_t  *out);
bool bvn_dom_get_u32(const bvn_dom_node_t *node, uint32_t *out);
bool bvn_dom_get_i16(const bvn_dom_node_t *node, int16_t  *out);
bool bvn_dom_get_u16(const bvn_dom_node_t *node, uint16_t *out);
bool bvn_dom_get_i8 (const bvn_dom_node_t *node, int8_t   *out);
bool bvn_dom_get_u8 (const bvn_dom_node_t *node, uint8_t  *out);
bvn_dom_node_t *bvn_dom_node_from_i64(int64_t  v);
bvn_dom_node_t *bvn_dom_node_from_u64(uint64_t v);
bvn_dom_node_t *bvn_dom_node_from_i32(int32_t  v);
bvn_dom_node_t *bvn_dom_node_from_u32(uint32_t v);
bvn_dom_node_t *bvn_dom_node_from_i16(int16_t  v);
bvn_dom_node_t *bvn_dom_node_from_u16(uint16_t v);
bvn_dom_node_t *bvn_dom_node_from_i8 (int8_t   v);
bvn_dom_node_t *bvn_dom_node_from_u8 (uint8_t  v);
bvn_dom_node_t *bvn_dom_node_from_bigint(bvn_int_t     *bigint,
										  value_type_spec_t vt,
										  value_unit_t      vu);
#ifdef __cplusplus
}
#endif
#endif
