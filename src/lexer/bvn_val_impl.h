#ifndef BVN_VAL_IMPL_H_
#define BVN_VAL_IMPL_H_
#include "bvn_lexer_impl.h"
#define BVN_SER_WBUF_SIZE 65536u
#define BOVN_READ_BUFFER_SIZE	4096u
typedef enum bvn_limit_defaults_e {
	max_identifier_length = UINT8_MAX,
	max_number_length     = UINT16_MAX,
	max_string_length     = UINT16_MAX,
	max_symbol_length     = UINT8_MAX,
	max_reference_length  = UINT16_MAX,
	max_struct_nesting    = 64,
	max_array_nesting     = 64,
	max_array_items       = 2147483647,
	max_text_bytes        = 2147483647,
	max_file_size         = 2147483647,
} bvn_limit_defaults_t;
typedef struct bvnr_serializer_s {
	bvnr_sink_t		sink;
	bool			pretty;
	uint32_t		indent;
	bool			need_semi;
	bool			finished;
	bool			stream_begun;
	uint32_t		array_depth;
	uint8_t			max_array_nesting;
	uint8_t			max_struct_nesting;
	bool			arr_need_comma[UINT8_MAX+1];
	bool			in_octet_stream;
	uint32_t		struct_depth;
	uint32_t		struct_depth_at_array_start[UINT8_MAX+1];
	bool			had_type_annotation;
	bool			emitted_type_param;
	void*			event_userdata;
	bool			(*on_event)(void* userdata, bvnr_event_t e, bvnr_data_t* data);
	bvn_unit_flags_t	unit_flags;
	uint8_t			wbuf[BVN_SER_WBUF_SIZE];
	uint32_t		wbuf_pos;
} bvnr_serializer_t;
#define BVN_TYPE_CACHE_KEY_CAP  64u
#define BVN_IU_CACHE_KEY_CAP    32u
#define BVN_TYPE_CACHE_UBUF_CAP 64u
typedef struct bvnr_validator_s {
	value_type_spec_t	value_type;
	value_unit_t		parsed_unit;
	uint64_t		acc_value;
	bool			acc_overflow;
	bool			acc_has_dot;
	bool			acc_has_exp;
	uint8_t			acc_exp_state;
	uint8_t			unit_data_len;
	bool			has_annotation_unit;
	void*			userdata;
	error_code_t		last_error;
	uint64_t		error_line;
	uint64_t		error_column;
	uint32_t		error_byte;
	uint64_t		error_offset;
	bool			(*on_unverified)
			(void* userdata, bvnr_event_t e, bvnr_data_t* data);
	bool			(*on_verified)
			(void* userdata, bvnr_event_t e, bvnr_data_t* data);
	bvnr_on_error_fn	on_error;
	bool			tcache_valid;
	bool			tcache_type_ok;
	bool			tcache_unit_ok;
	bool			tcache_unit_too_long;
	uint16_t		tcache_key_len;
	uint8_t			tcache_ubuf_len;
	value_type_spec_t	tcache_vtype;
	value_unit_t		tcache_unit;
	uint8_t			tcache_key[BVN_TYPE_CACHE_KEY_CAP];
	uint8_t			tcache_ubuf[BVN_TYPE_CACHE_UBUF_CAP];
	bool			iucache_valid;
	bool			iucache_ok;
	uint8_t			iucache_key_len;
	value_unit_t		iucache_unit;
	uint8_t			iucache_key[BVN_IU_CACHE_KEY_CAP];
} bvnr_validator_t;
struct bvnr_reader_s {
	bvnr_lexer_t		lex;
	bvnr_validator_t	val;
};
struct bvnr_writer_s {
	bvnr_serializer_t	ser;
	bvnr_validator_t	val;
};
void bvn_val_init(bvnr_validator_t* v, bvnr_read_flags_t* opts);
bool bvn_val_receive(bvnr_reader_t* r, const bvnr_raw_token_t* raw);
bool bvn_val_receive_event(bvnr_reader_t* r, bvnr_event_t ev);
bool bvn_val_receive_octet_chunk(
	bvnr_reader_t* r, const uint8_t* data, uint32_t len);
bool bvn_val_on_value_intro(bvnr_reader_t* r);
bool bvn_val_on_value_outro(bvnr_reader_t* r);
bool bvn_val_on_array_intro(bvnr_reader_t* r);
bool bvn_val_on_array_outro(bvnr_reader_t* r,
	uint64_t curr_row_size, uint64_t* array_row_size);
bool bvn_val_on_new_array_value(bvnr_reader_t* r,
	uint64_t curr_row_size, uint64_t array_row_size);
bool bvn_validate_type_value_compat(bvnr_reader_t* r,
	token_type_t tt, const uint8_t* str, uint32_t str_len);
bool bvn_check_acc_range(bvnr_validator_t* v,
	const uint8_t* str, uint32_t str_len,
	token_type_t tt);
void bvn_acc_reset(bvnr_validator_t* v);
void bvn_acc_digit(bvnr_validator_t* v, uint32_t dv, uint32_t base);
bool bvn_ser_serialize_event(
	bvnr_serializer_t* s, bvnr_event_t ev, bvnr_data_t* d);
bool bvn_ser_flush_wbuf(bvnr_serializer_t* s);
bool bvn_ser_finish_stream(bvnr_serializer_t* s);
bool bvn_writer_set_error(bvnr_writer_t* w, error_code_t err);
#endif
