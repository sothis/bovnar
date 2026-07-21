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

#ifndef BVN_VAL_IMPL_H_
#define BVN_VAL_IMPL_H_
#include "bvn_lexer_impl.h"
#include "bvn_int.h"
#define BVN_SER_WBUF_SIZE 65536u
#define BOVN_READ_BUFFER_SIZE	65536u
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
	/* No max_file_size default: 0 == BVNR_FILESIZE_UNLIMITED (endless). */
} bvn_limit_defaults_t;
typedef struct bvnr_serializer_s {
	bvnr_sink_t		sink;
	bool			pretty;
	uint32_t		indent;
	bool			need_semi;
	bool			finished;
	bool			stream_begun;
	bool			version_emitted;
	uint32_t		array_depth;
	uint8_t			max_array_nesting;
	uint8_t			max_struct_nesting;
	bool			arr_need_comma[UINT8_MAX+1];
	bool			in_octet_stream;
	uint32_t		struct_depth;
	uint32_t		struct_depth_at_array_start[UINT8_MAX+1];
	/* Event-grammar state for bvn_writer_validate_event. The serializer assumes a
	 * well-ordered event stream — it happily writes ">" for a lone annotation_end
	 * or ".k=.k=" for two assignments in a row — so the ordering has to be
	 * enforced before any bytes are produced, not discovered by the reader later. */
	bool			w_awaiting_value;  /* assignment_start, value not yet written */
	bool			w_in_annotation;   /* between annotation_start and _end */
	bool			w_after_row_end;   /* a row just closed; "/" may follow */
	bool			w_after_dim;       /* a "/" was written; a row may follow */
	bool			w_stream_ended;    /* ev_stream_end seen; nothing may follow */
	bool			had_type_annotation;
	bool			emitted_type_param;
	bool			emitted_unit;	/* a unit was emitted in the annotation,
						 * so an inline unit on the value need
						 * not (and must not) be re-appended */
	void*			event_userdata;
	bool			(*on_event)(void* userdata, bvnr_event_t e, bvnr_data_t* data);
	bvn_unit_flags_t	unit_flags;
	/* Set when serialisation fails for a reason of its own rather than because
	 * the sink filled up — a BVN_UNIT_REDUCE rescale that cannot be written
	 * exactly, say. bvnr_write_event prefers this over the generic sink error, so
	 * the caller is told what actually went wrong. error_none means "no reason of
	 * my own; assume the sink". */
	error_code_t		ser_error;
	/* When a BVN_UNIT_REDUCE rescale replaced the value's digits, the text that
	 * actually reached the sink, so bvnr_write_event can show the observer what it
	 * wrote rather than what the caller passed. Owned; freed and cleared per
	 * event. */
	char*			ser_value_text;
	uint32_t		ser_value_len;
	value_unit_t		ser_value_unit;   /* the unit that went with it */
	value_unit_t		ser_reduced_unit; /* scratch: the unit being emitted */
	uint8_t			wbuf[BVN_SER_WBUF_SIZE];
	uint32_t		wbuf_pos;
} bvnr_serializer_t;
#define BVN_UNIT_IS_NO_UNIT(u) \
	((u).num_components == 1u && (u).components[0].base == bu_none)
#define BVN_TYPE_CACHE_KEY_CAP  64u
#define BVN_IU_CACHE_KEY_CAP    32u
#define BVN_TYPE_CACHE_UBUF_CAP 64u
#define BVN_TYPE_CACHE_SLOTS    16u
#define BVN_IU_CACHE_SLOTS      16u
typedef struct bvn_type_cache_slot_s {
	uint16_t		key_len;
	uint8_t			ubuf_len;
	bool			type_ok;
	bool			unit_ok;
	bool			unit_too_long;
	value_type_spec_t	vtype;
	value_unit_t		unit;
	uint8_t			key[BVN_TYPE_CACHE_KEY_CAP];
	uint8_t			ubuf[BVN_TYPE_CACHE_UBUF_CAP];
} bvn_type_cache_slot_t;
typedef struct bvn_iu_cache_slot_s {
	uint8_t			key_len;
	bool			ok;
	value_unit_t		unit;
	uint8_t			key[BVN_IU_CACHE_KEY_CAP];
} bvn_iu_cache_slot_t;
typedef struct bvnr_validator_s {
	value_type_spec_t	value_type;
	value_unit_t		parsed_unit;
	value_type_spec_t	inferred_default_vtype;
	uint32_t		parsed_unit_serial;
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
	bool			(*want_unit)
			(void* userdata, const bvnr_data_t* data,
			 value_unit_t* want, uint32_t* want_base);
	bool			want_unit_allow_nonterminating;
	uint32_t		max_conversion_length;
	/* Scratch for lossless want_unit conversion, allocated lazily and reused per
	 * value; freed in bvn_val_free. num/den hold the exact converted rational,
	 * conv_text the rendered string handed to the callback. */
	bvn_int_t*		conv_num;
	bvn_int_t*		conv_den;
	char*			conv_text;
	uint32_t		conv_text_cap;
	bvnr_on_error_fn	on_error;
	uint8_t			tcache_count;
	uint8_t			tcache_next;
	uint8_t			iucache_count;
	uint8_t			iucache_next;
	bvn_type_cache_slot_t	tcache[BVN_TYPE_CACHE_SLOTS];
	bvn_iu_cache_slot_t	iucache[BVN_IU_CACHE_SLOTS];
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
void bvn_val_free(bvnr_validator_t* v);
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
bool bvn_ser_emit_version(bvnr_serializer_t* s, uint16_t major, uint16_t minor);
bool bvn_writer_set_error(bvnr_writer_t* w, error_code_t err);
#endif
