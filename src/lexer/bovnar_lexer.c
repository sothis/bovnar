#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "bovnar.h"
#include "bvn_lexer_impl.h"
#include "bvn_val_impl.h"
static inline void bvn_set_error_pos(bvnr_reader_t* r, uint32_t byte,
	uint64_t offset)
{
	bvnr_lexer_t* l = &r->lex;
	r->val.error_line   = l->line;
	r->val.error_column = l->column;
	r->val.error_byte   = byte;
	r->val.error_offset = offset;
}
static inline void bvn_lexer_set_error(bvnr_reader_t* r, error_code_t err)
{
	r->val.last_error = err;
	bvn_set_error_pos(r, (uint32_t)r->lex.byte & 0xffu,
		r->lex.text_bytes > 0 ? r->lex.text_bytes - 1 : 0);
}
static inline void bvn_notify_error(bvnr_reader_t* p)
{
	bvnr_validator_t* v = &p->val;
	if (v->on_error) {
		v->on_error(v->userdata, v->last_error,
					v->error_line, v->error_column,
					v->error_byte, v->error_offset);
	}
}
static void bvn_enter_resync(bvnr_reader_t* p)
{
	bvnr_lexer_t* l = &p->lex;
	l->resync_saved_struct_nesting = l->struct_nesting_level;
	l->utf8_need = 0;
	l->utf8_lo = 0;
	l->utf8_hi = 0;
	l->str_len    = 0;
	l->type_len   = 0;
	l->inline_unit_len = 0;
	l->token_type = token_is_unknown;
	bvn_acc_reset(&p->val);
	p->val.value_type          = BVN_TYPE_PLAIN;
	p->val.parsed_unit         = BVN_UNIT_NO_PREFIX(bu_none);
	p->val.has_annotation_unit = false;
	l->resync_array_depth  = 0;
	l->resync_struct_depth = 0;
	l->array_items         = 0;
	l->next_state          = resync;
	++l->recovery_count;
	l->in_array_element    = false;
	l->curr_row_size       = 0;
	l->array_row_size      = 0;
	memset(l->arr_frames, 0,
	       (l->max_array_nesting + 1u) * sizeof(bvn_array_frame_t));
}
#define ESC_VALID 0x100u
static const uint16_t bvn_escape_lut[256] = {
	['t']  = ESC_VALID | '\t',  ['n']  = ESC_VALID | '\n',
	['v']  = ESC_VALID | '\v',  ['f']  = ESC_VALID | '\f',
	['r']  = ESC_VALID | '\r',  ['"']  = ESC_VALID | '"',
	['\\'] = ESC_VALID | '\\',
};
static inline bool bvn_decode_escape(bvnr_reader_t* p, uint8_t* out)
{
	uint16_t e = bvn_escape_lut[(uint8_t)p->lex.byte];
	if (!(e & ESC_VALID)) {
		bvn_lexer_set_error(p, error_illegal_escape_sequence);
		return false;
	}
	*out = (uint8_t)(e & 0xffu);
	return true;
}
static inline bool bvn_push_bounded(
	bvnr_reader_t* p, uint8_t* dst, uint32_t len, uint32_t max,
	error_code_t overflow, state_t after)
{
	if (len == max) {
		bvn_lexer_set_error(p, overflow);
		return false;
	}
	dst[len] = (uint8_t)p->lex.byte;
	p->lex.next_state = after;
	return true;
}
static inline bool bvn_push_str(bvnr_reader_t* p, uint32_t max,
	error_code_t overflow, state_t after)
{
	if (!bvn_push_bounded(p, p->lex.str_data, p->lex.str_len,
						  max, overflow, after))
		return false;
	++p->lex.str_len;
	return true;
}
static inline bool bvn_push_number_byte(bvnr_reader_t* p, state_t after)
{
	return bvn_push_str(p, p->lex.max_number_length,
		error_number_too_long, after);
}
static inline bool bvn_push_string_byte(bvnr_reader_t* p, state_t after)
{
	return bvn_push_str(p, p->lex.max_string_length,
		error_string_too_long, after);
}
static inline bool bvn_push_reference_byte(bvnr_reader_t* p, state_t after)
{
	return bvn_push_str(p, p->lex.max_reference_length,
		error_reference_too_long, after);
}
static bool bvn_replace_escaped_byte_impl(
	bvnr_reader_t* p, state_t after)
{
	uint8_t rep;
	if (p->lex.str_len == p->lex.max_string_length) {
		bvn_lexer_set_error(p, error_string_too_long);
		return false;
	}
	if (!bvn_decode_escape(p, &rep))
		return false;
	p->lex.str_data[p->lex.str_len++] = rep;
	p->lex.next_state = after;
	return true;
}
static inline bool bvn_lex_number_begin(bvnr_reader_t* p, bool is_array)
{
	if (p->lex.str_len)
		return true;
	if (is_array)
		p->lex.token_type = token_is_array_number;
	else
		p->lex.token_type = token_is_number;
	return true;
}
static bool bvn_lex_finalize(bvnr_reader_t* p, bvn_lex_sink_fn sink)
{
	bvnr_lexer_t* l = &p->lex;
	token_type_t  tt = l->token_type;
	if (tt == token_is_structure) {
		l->token_type = token_is_unknown;
		return true;
	}
	if (tt == token_is_unknown)
		return true;
	if (tt != token_is_type)
		l->str_data[l->str_len] = 0;
	l->type_data[l->type_len] = 0;
	l->inline_unit_data[l->inline_unit_len] = 0;
	if (tt == token_is_identifier && !l->str_len) {
		bvn_lexer_set_error(p, error_empty_identifier);
		return false;
	}
	bvnr_raw_token_t raw = {
		.type            = tt,
		.event           = (tt == token_is_identifier)
						   ? ev_assignment_start : ev_data,
		.str_data        = l->str_data,
		.str_len         = l->str_len,
		.type_data       = l->type_data,
		.type_len        = l->type_len,
		.inline_unit_data = l->inline_unit_data,
		.inline_unit_len  = l->inline_unit_len,
	};
	if (!sink(p, &raw))
		return false;
	l->type_len          = 0;
	l->str_len           = 0;
	l->inline_unit_len   = 0;
	l->token_type        = token_is_unknown;
	return true;
}
bool bvn_action_set_state(bvnr_reader_t* p)
{
	uint8_t idx = bvn_after_state_idx_table[p->lex.next_state][p->lex.byte];
	p->lex.next_state = bvn_action_target_state[idx];
	return true;
}
bool bvn_action_ignore_whitespace(bvnr_reader_t* p) { (void)p; return true; }
bool bvn_action_comment_intro(bvnr_reader_t* p)
{
	p->lex.last_state = p->lex.next_state;
	p->lex.next_state = comment_intro;
	return true;
}
bool bvn_action_comment_outro(bvnr_reader_t* p)
{
	state_t s = p->lex.last_state;
	if (s == number_outro_nosp) s = number_outro;
	else if (s == string_outro_nosp) s = string_outro;
	p->lex.next_state = s;
	return true;
}
bool bvn_action_copy_utf8bom_byte(bvnr_reader_t* p)
{
	bvnr_lexer_t* l = &p->lex;
	if (l->bom_len < 2) {
		if (!l->bom_len)
			l->last_state = l->next_state;
		l->bom[l->bom_len++] = (uint8_t)l->byte;
		l->next_state = utf8bom_intro;
		return true;
	}
	if (l->bom[0] != 0xef || l->bom[1] != 0xbb || l->byte != 0xbf
		|| l->last_state != undefined) {
		bvn_lexer_set_error(p, error_invalid_byte_order_mark);
		return false;
	}
	l->bom_len    = 0;
	l->next_state = first_bom;
	return true;
}
bool bvn_action_identifier_intro(bvnr_reader_t* p)
{
	p->lex.str_len    = 0;
	p->lex.token_type = token_is_identifier;
	p->lex.next_state = identifier_intro;
	return true;
}
bool bvn_action_copy_identifier_byte(bvnr_reader_t* p)
{
	return bvn_push_str(p, p->lex.max_identifier_length,
		error_identifier_too_long, identifier_body);
}
bool bvn_action_value_intro(bvnr_reader_t* p)
{
	if (!bvn_lex_finalize(p, bvn_val_receive))
		return false;
	if (!bvn_val_on_value_intro(p))
		return false;
	p->lex.str_len          = 0;
	p->lex.array_items      = 0;
	p->lex.token_type       = token_is_null_value;
	p->lex.in_array_element = false;
	bvn_acc_reset(&p->val);
	p->lex.next_state  = value_intro;
	return true;
}
bool bvn_action_value_outro(bvnr_reader_t* p)
{
	if (!bvn_lex_finalize(p, bvn_val_receive))
		return false;
	if (!bvn_val_on_value_outro(p))
		return false;
	uint64_t base  = p->lex.array_nesting_level;
	uint64_t total = p->lex.max_array_nesting + 1u;
	if (base < total)
		memset(p->lex.arr_frames + base, 0,
		       (total - base) * sizeof(bvn_array_frame_t));
	if (!p->lex.struct_nesting_level) {
		p->lex.in_array_element    = false;
		p->lex.array_nesting_level = 0;
		p->lex.curr_row_size       = 0;
		p->lex.array_row_size      = 0;
	}
	p->lex.next_state = value_outro;
	return true;
}
bool bvn_action_type_intro(bvnr_reader_t* p)
{
	p->lex.token_type = token_is_type;
	p->lex.next_state = type_intro;
	return true;
}
bool bvn_action_type_outro(bvnr_reader_t* p)
{
	if (!bvn_lex_finalize(p, bvn_val_receive))
		return false;
	p->lex.next_state = type_outro;
	return true;
}
bool bvn_action_copy_type_byte(bvnr_reader_t* p)
{
	bvnr_lexer_t* l = &p->lex;
	if (l->type_len == sizeof(l->type_data) - 1u) {
		bvn_lexer_set_error(p, error_type_too_long);
		return false;
	}
	l->type_data[l->type_len++] = (uint8_t)l->byte;
	l->next_state = copy_type_byte;
	return true;
}
bool bvn_action_neg_number_intro(bvnr_reader_t* p)
{
	bool is_arr = p->lex.in_array_element;
	if (!bvn_lex_number_begin(p, is_arr)) return false;
	return bvn_push_number_byte(p, neg_number_intro);
}
bool bvn_action_copy_number_byte(bvnr_reader_t* p)
{
	bool is_arr = p->lex.in_array_element;
	if (!bvn_lex_number_begin(p, is_arr)) return false;
	return bvn_push_number_byte(p, copy_number_byte);
}
bool bvn_action_zero_intro(bvnr_reader_t* p)
{
	bool is_arr = p->lex.in_array_element;
	if (!bvn_lex_number_begin(p, is_arr)) return false;
	return bvn_push_number_byte(p, zero_intro);
}
bool bvn_action_fraction_intro(bvnr_reader_t* p)
{
	bool is_arr = p->lex.in_array_element;
	if (!bvn_lex_number_begin(p, is_arr)) return false;
	return bvn_push_number_byte(p, fraction_intro);
}
bool bvn_action_fraction_no_int(bvnr_reader_t* p)
{
	bool is_arr = p->lex.in_array_element;
	if (!bvn_lex_number_begin(p, is_arr)) return false;
	return bvn_push_number_byte(p, fraction_no_int);
}
bool bvn_action_copy_fraction_byte(bvnr_reader_t* p)
{
	return bvn_push_number_byte(p, copy_fraction_byte);
}
bool bvn_action_special_number_intro(bvnr_reader_t* p)
{
	p->lex.str_len    = 0;
	p->lex.token_type = token_is_number;
	p->lex.next_state = sp_start;
	return true;
}
bool bvn_action_arr_special_number_intro(bvnr_reader_t* p)
{
	p->lex.str_len    = 0;
	p->lex.token_type = token_is_array_number;
	p->lex.next_state = sp_start;
	return true;
}
bool bvn_action_array_intro(bvnr_reader_t* p)
{
	if (p->lex.max_array_items &&
		p->lex.array_items == p->lex.max_array_items) {
		bvn_lexer_set_error(p, error_too_many_array_items);
		return false;
	}
	uint64_t level = p->lex.array_nesting_level;
	if (level == p->lex.max_array_nesting) {
		bvn_lexer_set_error(p, error_array_nesting_too_high);
		return false;
	}
	++p->lex.array_items;
	bvn_array_frame_t *f = &p->lex.arr_frames[level];
	f->saved_curr  = p->lex.curr_row_size;
	f->saved_row   = p->lex.array_row_size;
	f->saved_vtype = p->val.value_type;
	f->saved_vunit = p->val.parsed_unit;
	p->lex.curr_row_size  = 0;
	p->lex.array_row_size = (f->in_dim_seq || f->dim_row_size)
	                        ? f->dim_row_size : 0;
	++p->lex.curr_row_size;
	p->lex.token_type       = token_is_null_value;
	p->lex.in_array_element = true;
	bvn_acc_reset(&p->val);
	if (!bvn_val_on_array_intro(p))
		return false;
	++p->lex.array_nesting_level;
	p->lex.next_state = array_intro;
	return true;
}
bool bvn_action_array_outro(bvnr_reader_t* p)
{
	if (!p->lex.array_nesting_level) {
		bvn_lexer_set_error(p, error_unexpected_input_byte);
		return false;
	}
	if (!bvn_lex_finalize(p, bvn_val_receive))
		return false;
	if (!bvn_val_on_array_outro(p, p->lex.curr_row_size,
								&p->lex.array_row_size))
		return false;
	--p->lex.array_nesting_level;
	uint64_t level = p->lex.array_nesting_level;
	bvn_array_frame_t *f = &p->lex.arr_frames[level];
	f->dim_row_size       = p->lex.array_row_size;
	p->lex.curr_row_size  = f->saved_curr + 1u;
	p->lex.array_row_size = f->saved_row;
	p->val.value_type     = f->saved_vtype;
	p->val.parsed_unit    = f->saved_vunit;
	p->lex.next_state = array_outro;
	return true;
}
bool bvn_action_new_array_value(bvnr_reader_t* p)
{
	if (!p->lex.array_nesting_level) {
		bvn_lexer_set_error(p, error_unexpected_input_byte);
		return false;
	}
	if (!bvn_lex_finalize(p, bvn_val_receive))
		return false;
	if (p->lex.max_array_items &&
		p->lex.array_items == p->lex.max_array_items) {
		bvn_lexer_set_error(p, error_too_many_array_items);
		return false;
	}
	++p->lex.array_items;
	p->lex.str_len          = 0;
	p->lex.in_array_element = true;
	bvn_acc_reset(&p->val);
	if (!bvn_val_on_new_array_value(p, p->lex.curr_row_size,
									p->lex.array_row_size))
		return false;
	p->lex.arr_frames[p->lex.array_nesting_level].in_dim_seq = false;
	bvn_array_frame_t *par = &p->lex.arr_frames[p->lex.array_nesting_level - 1u];
	p->val.value_type  = par->saved_vtype;
	p->val.parsed_unit = par->saved_vunit;
	++p->lex.curr_row_size;
	p->lex.token_type = token_is_null_value;
	p->lex.next_state = new_array_value;
	return true;
}
bool bvn_action_array_dim_sep(bvnr_reader_t* p)
{
	if (!bvn_val_receive_event(p, ev_array_dim_start))
		return false;
	p->lex.arr_frames[p->lex.array_nesting_level].in_dim_seq = true;
	p->lex.next_state = array_dim_sep;
	return true;
}
bool bvn_action_exp_intro(bvnr_reader_t* p)
{
	bool is_arr = p->lex.in_array_element;
	if (!bvn_lex_number_begin(p, is_arr)) return false;
	return bvn_push_number_byte(p, exp_intro);
}
bool bvn_action_exp_sign_intro(bvnr_reader_t* p)
{
	return bvn_push_number_byte(p, exp_sign_intro);
}
bool bvn_action_copy_exp_byte(bvnr_reader_t* p)
{
	return bvn_push_number_byte(p, copy_exp_byte);
}
bool bvn_action_kw_advance(bvnr_reader_t* p)
{
	p->lex.next_state = bvn_kw_advance_state[p->lex.next_state];
	return true;
}
static bool bvn_special_keyword_outro(bvnr_reader_t* p,
	const char* s, uint32_t len)
{
	memcpy(p->lex.str_data, s, len);
	p->lex.str_data[len] = '\0';
	p->lex.str_len = (uint16_t)len;
	p->lex.next_state = number_outro_nosp;
	return true;
}
bool bvn_action_sp_nan_outro(bvnr_reader_t* p)
	{ return bvn_special_keyword_outro(p, "nan", 3); }
bool bvn_action_sp_inf_outro(bvnr_reader_t* p)
	{ return bvn_special_keyword_outro(p, "infinity", 8); }
bool bvn_action_sp_neginf_outro(bvnr_reader_t* p)
	{ return bvn_special_keyword_outro(p, "-infinity", 9); }
static bool bvn_tf_family_done(bvnr_reader_t* p,
	const char* name, uint32_t len, state_t after)
{
	memcpy(p->lex.type_data, name, len);
	p->lex.type_len   = (uint16_t)len;
	p->lex.next_state = after;
	return true;
}
bool bvn_action_tf_uint_done(bvnr_reader_t* p)
	{ return bvn_tf_family_done(p, "uint",      4, type_body_outro); }
bool bvn_action_tf_sint_done(bvnr_reader_t* p)
	{ return bvn_tf_family_done(p, "sint",      4, type_body_outro); }
bool bvn_action_tf_float_done(bvnr_reader_t* p)
	{ return bvn_tf_family_done(p, "float",     5, type_body_outro); }
bool bvn_action_tf_utf8_done(bvnr_reader_t* p)
	{ return bvn_tf_family_done(p, "utf8",      4, type_body_outro); }
bool bvn_action_string_intro(bvnr_reader_t* p)
{
	p->lex.token_type = p->lex.in_array_element
		? token_is_array_string : token_is_string;
	p->lex.next_state = string_intro;
	return true;
}
bool bvn_action_copy_string_byte(bvnr_reader_t* p)
{
	return bvn_push_string_byte(p, copy_string_byte);
}
bool bvn_action_replace_escaped_byte(bvnr_reader_t* p)
{
	return bvn_replace_escaped_byte_impl(p, copy_string_byte);
}
bool bvn_action_arr_string_intro(bvnr_reader_t* p)
{
	p->lex.token_type = token_is_array_string;
	p->lex.next_state = string_intro;
	return true;
}
bool bvn_action_symbol_intro(bvnr_reader_t* p)
{
	p->lex.str_len    = 0;
	p->lex.token_type = token_is_symbol;
	return bvn_push_str(p, p->lex.max_symbol_length,
		error_symbol_too_long, symbol_body);
}
bool bvn_action_copy_symbol_byte(bvnr_reader_t* p)
{
	return bvn_push_str(p, p->lex.max_symbol_length,
		error_symbol_too_long, symbol_body);
}
bool bvn_action_reference_intro(bvnr_reader_t* p)
{
	p->lex.str_len    = 0;
	p->lex.token_type = token_is_reference;
	p->lex.next_state = reference_intro;
	return true;
}
bool bvn_action_copy_reference_dot(bvnr_reader_t* p)
{
	return bvn_push_reference_byte(p, reference_segment_intro);
}
bool bvn_action_copy_reference_byte(bvnr_reader_t* p)
{
	return bvn_push_reference_byte(p, reference_segment_body);
}
bool bvn_action_octet_stream_intro(bvnr_reader_t* p)
{
	p->lex.token_type = token_is_octet_stream;
	p->lex.next_state = octet_stream_intro;
	return bvn_val_receive_event(p, ev_octet_stream_start);
}
bool bvn_action_struct_intro(bvnr_reader_t* p)
{
	bvnr_lexer_t* l = &p->lex;
	if (l->struct_nesting_level == l->max_struct_nesting) {
		bvn_lexer_set_error(p, error_struct_nesting_too_high);
		return false;
	}
	++l->struct_nesting_level;
	if (!bvn_val_receive_event(p, ev_struct_start))
		return false;
	l->token_type = token_is_structure;
	l->next_state = struct_intro;
	return true;
}
bool bvn_action_struct_outro(bvnr_reader_t* p)
{
	if (!p->lex.struct_nesting_level) {
		bvn_lexer_set_error(p, error_illegal_struct_close);
		return false;
	}
	--p->lex.struct_nesting_level;
	if (!bvn_val_receive_event(p, ev_struct_end))
		return false;
	p->lex.next_state = struct_outro;
	return true;
}
bool bvn_action_first_comment_intro(bvnr_reader_t* p)
{
	p->lex.last_state = p->lex.next_state;
	p->lex.next_state = first_comment_intro;
	return true;
}
bool bvn_action_first_comment_byte(bvnr_reader_t* p)
{
	p->lex.next_state = (p->lex.byte == 0xefu)
		? first_comment_after_ef : first_comment_intro;
	return true;
}
bool bvn_action_first_comment_after_ef(bvnr_reader_t* p)
{
	if (p->lex.byte == 0xbbu)      p->lex.next_state = first_comment_after_ef_bb;
	else if (p->lex.byte == 0xefu) p->lex.next_state = first_comment_after_ef;
	else                           p->lex.next_state = first_comment_intro;
	return true;
}
bool bvn_action_first_comment_after_ef_bb(bvnr_reader_t* p)
{
	if (p->lex.byte == 0xbfu) {
		bvn_lexer_set_error(p, error_invalid_byte_order_mark);
		return false;
	}
	p->lex.next_state = (p->lex.byte == 0xefu)
		? first_comment_after_ef : first_comment_intro;
	return true;
}
bool bvn_action_first_comment_outro(bvnr_reader_t* p)
{
	p->lex.next_state = first_bom;
	return true;
}
bool bvn_action_type_null_then_value_outro(bvnr_reader_t* p)
{
	p->lex.token_type = token_is_null_value;
	p->lex.str_len    = 0;
	if (!bvn_lex_finalize(p, bvn_val_receive))
		return false;
	return bvn_action_value_outro(p);
}
bool bvn_action_type_null_then_new_array_value(bvnr_reader_t* p)
{
	p->lex.token_type = token_is_null_value;
	p->lex.str_len    = 0;
	if (!bvn_lex_finalize(p, bvn_val_receive))
		return false;
	return bvn_action_new_array_value(p);
}
bool bvn_action_type_null_then_array_outro(bvnr_reader_t* p)
{
	p->lex.token_type = token_is_null_value;
	p->lex.str_len    = 0;
	if (!bvn_lex_finalize(p, bvn_val_receive))
		return false;
	return bvn_action_array_outro(p);
}
bool bvn_action_resync_skip(bvnr_reader_t* p)
{
	p->lex.next_state = resync;
	return true;
}
bool bvn_action_resync_open_bracket(bvnr_reader_t* p)
{
	if (p->lex.byte == '[')
		++p->lex.resync_array_depth;
	else
		++p->lex.resync_struct_depth;
	p->lex.next_state = resync;
	return true;
}
bool bvn_action_resync_close_bracket(bvnr_reader_t* p)
{
	bvnr_lexer_t* l = &p->lex;
	if (l->byte == ']') {
		if (l->resync_array_depth > 0) {
			--l->resync_array_depth;
			l->next_state = resync;
			return true;
		}
		if (l->array_nesting_level > 0) {
			uint64_t effective_row = l->array_row_size
				? l->array_row_size : l->curr_row_size;
			if (!bvn_val_on_array_outro(p, effective_row,
						    &l->array_row_size))
				return false;
			--l->array_nesting_level;
			uint64_t level = l->array_nesting_level;
			bvn_array_frame_t *f = &l->arr_frames[level];
			f->dim_row_size    = l->array_row_size;
			l->curr_row_size   = f->saved_curr + 1u;
			l->array_row_size  = f->saved_row;
			p->val.value_type  = f->saved_vtype;
			p->val.parsed_unit = f->saved_vunit;
			l->in_array_element = (l->array_nesting_level > 0);
			l->next_state = array_outro;
		} else {
			l->next_state = resync;
		}
	} else if (l->byte == '}') {
		if (l->resync_struct_depth > 0) {
			--l->resync_struct_depth;
			l->next_state = resync;
			return true;
		}
		if (l->struct_nesting_level > 0) {
			--l->struct_nesting_level;
			if (!bvn_val_receive_event(p, ev_struct_end))
				return false;
			l->next_state = struct_outro;
		} else {
			l->next_state = resync;
		}
	} else {
		l->next_state = resync;
	}
	return true;
}
static void bvn_resync_semicolon_reset(bvnr_reader_t* p)
{
	bvnr_lexer_t* l = &p->lex;
	l->token_type           = token_is_unknown;
	l->str_len              = 0;
	l->type_len             = 0;
	l->in_array_element     = false;
	l->struct_nesting_level = l->resync_saved_struct_nesting;
	l->array_nesting_level  = 0;
	l->resync_array_depth   = 0;
	l->resync_struct_depth  = 0;
	l->array_items          = 0;
	l->curr_row_size        = 0;
	l->array_row_size       = 0;
	memset(l->arr_frames, 0,
	       (l->max_array_nesting + 1u) * sizeof(bvn_array_frame_t));
	p->val.value_type          = BVN_TYPE_PLAIN;
	p->val.parsed_unit         = BVN_UNIT_NO_PREFIX(bu_none);
	p->val.has_annotation_unit = false;
	bvn_acc_reset(&p->val);
	l->next_state = value_outro;
}
bool bvn_action_resync_semicolon(bvnr_reader_t* p)
{
	bvnr_lexer_t* l = &p->lex;
	if (l->resync_array_depth > 0 || l->resync_struct_depth > 0) {
		l->next_state = resync;
		return true;
	}
	while (l->array_nesting_level > 0) {
		--l->array_nesting_level;
		if (!bvn_val_receive_event(p, ev_array_row_end)) {
			bvn_resync_semicolon_reset(p);
			return false;
		}
	}
	bvn_resync_semicolon_reset(p);
	return true;
}
bool bvn_action_resync_string_intro(bvnr_reader_t* p)
{
	p->lex.next_state = resync_string;
	return true;
}
bool bvn_action_resync_string_byte(bvnr_reader_t* p)
{
	p->lex.next_state = resync_string;
	return true;
}
bool bvn_action_resync_string_escape(bvnr_reader_t* p)
{
	p->lex.next_state = resync_string_escape;
	return true;
}
bool bvn_action_resync_string_escape_byte(bvnr_reader_t* p)
{
	p->lex.next_state = resync_string;
	return true;
}
bool bvn_action_resync_string_outro(bvnr_reader_t* p)
{
	p->lex.next_state = resync;
	return true;
}
bool bvn_action_resync_comment_intro(bvnr_reader_t* p)
{
	p->lex.next_state = resync_comment;
	return true;
}
bool bvn_action_resync_comment_byte(bvnr_reader_t* p)
{
	p->lex.next_state = resync_comment;
	return true;
}
bool bvn_action_resync_comment_outro(bvnr_reader_t* p)
{
	p->lex.next_state = resync;
	return true;
}
bool bvn_action_inline_unit_intro(bvnr_reader_t* p)
{
	if (p->lex.in_array_element) {
		bvn_lexer_set_error(p, error_unexpected_input_byte);
		return false;
	}
	bvnr_lexer_t* l = &p->lex;
	l->inline_unit_len = 0;
	l->inline_unit_data[l->inline_unit_len++] = (uint8_t)l->byte;
	l->next_state = inline_unit_body;
	return true;
}
bool bvn_action_copy_inline_unit_byte(bvnr_reader_t* p)
{
	bvnr_lexer_t* l = &p->lex;
	if (l->inline_unit_len == sizeof(l->inline_unit_data) - 1u) {
		bvn_lexer_set_error(p, error_unit_too_long);
		return false;
	}
	l->inline_unit_data[l->inline_unit_len++] = (uint8_t)l->byte;
	l->next_state = inline_unit_body;
	return true;
}
bool bvn_action_to_inline_unit_outro(bvnr_reader_t* p)
{
	p->lex.next_state = inline_unit_outro;
	return true;
}
typedef struct {
	bvnr_reader_t* p;
	const uint8_t*  resid;
	uint32_t        resid_left;
} octet_source_t;
static inline void bvn_capture_os_error(bvnr_reader_t* p, error_code_t err)
{
	bvnr_lexer_t* l = &p->lex;
	p->val.last_error   = err;
	p->val.error_line   = l->line;
	p->val.error_column = l->column;
	p->val.error_byte   = 0;
	p->val.error_offset = l->processed_bytes;
}
static bool bvn_os_read_exact(
	octet_source_t* src, void* output, uint32_t length)
{
	bvnr_reader_t* p   = src->p;
	uint8_t*        buf = (uint8_t*)output;
	if (!length) {
		bvn_capture_os_error(p, error_invalid_argument);
		bvn_notify_error(p);
		return false;
	}
	uint32_t from_resid = (src->resid_left < length)
		? src->resid_left : length;
	if (from_resid) {
		memcpy(buf, src->resid, from_resid);
		if (p->lex.use_dbg) {
			if (!bvn_sink_push(&p->lex.src_dbg, src->resid,
							  from_resid)) {
				bvn_capture_os_error(p, error_writing_to_sink);
				bvn_notify_error(p);
				return false;
			}
		}
		src->resid      += from_resid;
		src->resid_left -= from_resid;
	}
	uint32_t need = length - from_resid;
	if (!need) return true;
	uint32_t total = 0, got;
	while (total != need) {
		if (!bvn_source_pull(&p->lex.src, buf + from_resid + total,
						  need - total, &got) || !got) {
			bvn_capture_os_error(p,
				error_read_complete_chunk_failed);
			bvn_notify_error(p);
			return false;
		}
		p->lex.processed_bytes += (uint64_t)got;
		if (p->lex.max_file_size &&
			p->lex.processed_bytes > p->lex.max_file_size) {
			bvn_capture_os_error(p, error_file_too_long);
			bvn_notify_error(p);
			return false;
		}
		if (p->lex.use_dbg) {
			if (!bvn_sink_push(&p->lex.src_dbg,
							  buf + from_resid + total, got)) {
				bvn_capture_os_error(p, error_writing_to_sink);
				bvn_notify_error(p);
				return false;
			}
		}
		total += got;
	}
	return true;
}
static int32_t bvn_read_octet_stream(
	bvnr_reader_t* p, const uint8_t* resid, uint32_t resid_len)
{
	octet_source_t src = { .p = p, .resid = resid, .resid_left = resid_len };
	uint8_t  tag, lenbuf[2];
	uint32_t chunklen;
	for (;;) {
		if (!bvn_os_read_exact(&src, &tag, 1))
			return -1;
		if (tag == 0x00) {
			p->lex.token_type = token_is_unknown;
			if (!bvn_val_receive_event(p, ev_octet_stream_end))
				return -1;
			p->lex.next_state = octet_stream_outro;
			return (int32_t)src.resid_left;
		}
		if (tag != 0x01) {
			bvn_capture_os_error(p, error_octet_stream_out_of_sync);
			bvn_notify_error(p);
			return -1;
		}
		if (!bvn_os_read_exact(&src, lenbuf, 2))
			return -1;
		chunklen = (uint32_t)lenbuf[0]
				 | ((uint32_t)lenbuf[1] << 8);
		if (!chunklen)
			chunklen = 65536u;
		if (!bvn_os_read_exact(&src, p->lex.str_data, chunklen))
			return -1;
		if (!bvn_val_receive_octet_chunk(p, p->lex.str_data, chunklen))
			return -1;
	}
}
bool bvn_utf8_classify_leader(
	uint32_t b, uint8_t* need, uint8_t* lo, uint8_t* hi)
{
	if (b <= 0x7f) { *need = 0; *lo = 0;    *hi = 0;    return true;  }
	if (b <= 0xc1) return false;
	if (b <= 0xdf) { *need = 1; *lo = 0x80; *hi = 0xbf; return true;  }
	if (b == 0xe0) { *need = 2; *lo = 0xa0; *hi = 0xbf; return true;  }
	if (b == 0xed) { *need = 2; *lo = 0x80; *hi = 0x9f; return true;  }
	if (b <= 0xef) { *need = 2; *lo = 0x80; *hi = 0xbf; return true;  }
	if (b == 0xf0) { *need = 3; *lo = 0x90; *hi = 0xbf; return true;  }
	if (b == 0xf4) { *need = 3; *lo = 0x80; *hi = 0x8f; return true;  }
	if (b <= 0xf3) { *need = 3; *lo = 0x80; *hi = 0xbf; return true;  }
	return false;
}
typedef struct {
	uint8_t need, lo, hi;
} utf8_state_t;
static inline bool bvn_utf8_feed_st(utf8_state_t* st, uint32_t b)
{
	if (st->need) {
		if (b < st->lo || b > st->hi) {
			*st = (utf8_state_t){0};
			return false;
		}
		--st->need;
		st->lo = 0x80;
		st->hi = 0xbf;
		return true;
	}
	return bvn_utf8_classify_leader(b, &st->need, &st->lo, &st->hi);
}
static bool bvn_utf8_feed(bvnr_lexer_t* l, uint32_t b)
{
	utf8_state_t st = { l->utf8_need, l->utf8_lo, l->utf8_hi };
	bool ok = bvn_utf8_feed_st(&st, b);
	l->utf8_need = st.need;
	l->utf8_lo   = st.lo;
	l->utf8_hi   = st.hi;
	return ok;
}
bool bvn_validate_string(const uint8_t* data, size_t len)
{
	utf8_state_t st = {0};
	for (size_t i = 0; i < len; i++) {
		if (!bvn_utf8_feed_st(&st, data[i]))
			return false;
	}
	return st.need == 0;
}
static inline bool bvn_call_action_for_new_byte(bvnr_reader_t* p)
{
	uint8_t idx = bvn_after_state_idx_table[p->lex.next_state][p->lex.byte];
	if (!idx || !bvn_action_table[idx]) {
		bvn_lexer_set_error(p, error_unexpected_input_byte);
		return false;
	}
	return bvn_action_table[idx](p);
}
static inline void bvn_advance_line(bvnr_lexer_t* l, uint8_t prev)
{
	if (l->byte == 0x0d) {
		++l->line;
		l->column = 0;
	} else if (l->byte == 0x0a && prev != 0x0d) {
		++l->line;
		l->column = 0;
	}
}
static bool bvn_interpret_input_buffer(
	bvnr_reader_t* p, const uint8_t* data, uint32_t len,
	uint32_t* consumed)
{
	bvnr_lexer_t* l = &p->lex;
	for (uint32_t idx = 0; idx < len; ++idx) {
		if (l->max_text_bytes &&
			l->text_bytes == l->max_text_bytes) {
			uint8_t offending = data[idx];
			uint64_t col = (offending == 0x09u)
				? ((l->column >> 2u) + 1u) << 2u
				: l->column + 1u;
			p->val.last_error   = error_text_data_too_long;
			p->val.error_line   = l->line;
			p->val.error_column = col;
			p->val.error_byte   = (uint32_t)offending & 0xffu;
			p->val.error_offset = l->text_bytes;
			bvn_notify_error(p);
			return false;
		}
		++l->text_bytes;
		l->byte = data[idx];
		if (l->byte == 0x09)
			l->column = ((l->column >> 2u) + 1u) << 2u;
		else
			++l->column;
		uint8_t prev = l->prev_byte;
		l->prev_byte = (uint8_t)l->byte;
		if (!bvn_utf8_feed(l, (uint32_t)l->byte)) {
			p->val.last_error = error_invalid_utf8_byte;
			bvn_set_error_pos(p, (uint32_t)l->byte & 0xffu,
				l->text_bytes - 1);
			bvn_notify_error(p);
			bvn_advance_line(l, prev);
			if (l->continue_on_error) {
				bvn_enter_resync(p);
				continue;
			} else {
				return false;
			}
		}
		if (!bvn_call_action_for_new_byte(p)) {
			bvn_set_error_pos(p, (uint32_t)l->byte & 0xffu,
				l->text_bytes - 1);
			bvn_notify_error(p);
			bvn_advance_line(l, prev);
			if (l->continue_on_error) {
				bvn_enter_resync(p);
			} else {
				return false;
			}
		} else {
			bvn_advance_line(l, prev);
		}
		if (l->next_state == octet_stream_intro) {
			*consumed = idx + 1;
			return true;
		}
	}
	*consumed = len;
	return true;
}
static inline void bvn_set_eof_error(bvnr_reader_t* p, error_code_t err)
{
	p->val.last_error = err;
	bvn_set_error_pos(p, 0, p->lex.processed_bytes);
}
void bvn_lex_destroy(bvnr_lexer_t* l)
{
	if (!l)
		return;
	free(l->arr_frames);
	l->arr_frames = NULL;
}
bool bvn_lex_init(bvnr_lexer_t* l, const bvnr_source_t* src,
	const bvnr_sink_t* dbg_sink, bvnr_read_flags_t* opts)
{
	memset(l, 0, sizeof(*l));
	l->next_state  = undefined;
	l->line        = 1;
	if (src)
		l->src = *src;
	if (dbg_sink && dbg_sink->push) {
		l->src_dbg = *dbg_sink;
		l->use_dbg = true;
	}
	if (opts) {
		l->max_identifier_length = opts->max_identifier_length;
		l->max_string_length     = opts->max_string_length;
		l->max_number_length     = opts->max_number_length;
		l->max_symbol_length     = opts->max_symbol_length;
		l->max_reference_length  = opts->max_reference_length;
		l->max_array_items       = opts->max_array_items;
		l->max_text_bytes        = opts->max_text_bytes;
		l->max_file_size         = opts->max_file_size;
		l->max_struct_nesting    = opts->max_struct_nesting;
		l->max_array_nesting     = opts->max_array_nesting;
		l->continue_on_error     = opts->continue_on_error;
	}
	if (!l->max_identifier_length)	l->max_identifier_length	= max_identifier_length;
	if (!l->max_number_length)		l->max_number_length		= max_number_length;
	if (!l->max_string_length)		l->max_string_length		= max_string_length;
	if (!l->max_symbol_length)		l->max_symbol_length		= max_symbol_length;
	if (!l->max_reference_length)	l->max_reference_length		= max_reference_length;
	if (!l->max_struct_nesting)		l->max_struct_nesting		= max_struct_nesting;
	if (!l->max_array_nesting)		l->max_array_nesting		= max_array_nesting;
	if (!l->max_array_items)		l->max_array_items			= max_array_items;
	if (!l->max_text_bytes)			l->max_text_bytes			= max_text_bytes;
	if (!l->max_file_size)			l->max_file_size			= max_file_size;

	l->arr_frames = calloc(l->max_array_nesting + 1u, sizeof(bvn_array_frame_t));
	if (!l->arr_frames)
		return false;
	return true;
}
bool bvn_lex_run(bvnr_reader_t* r)
{
	bvnr_lexer_t* l = &r->lex;
	uint8_t data[BOVN_READ_BUFFER_SIZE];
	uint32_t nb, off, consumed;
	int32_t  os_leftover;
	if (!bvn_val_receive_event(r, ev_stream_start))
		return false;
	for (;;) {
		if (!bvn_source_pull(&l->src, data, BOVN_READ_BUFFER_SIZE, &nb)) {
			bvn_set_eof_error(r, error_reading_from_source_fd);
			bvn_notify_error(r);
			return false;
		}
		if (!nb) {
			if ((l->next_state == value_outro ||
				 l->next_state == undefined   ||
				 l->next_state == first_bom) &&
				!l->struct_nesting_level) {
				r->val.last_error = error_none;
				break;
			}
			if ((l->next_state == comment_intro ||
				 l->next_state == ignore_comment_byte) &&
				l->last_state == value_outro &&
				!l->struct_nesting_level) {
				r->val.last_error = error_none;
				break;
			}
			if (l->next_state == resync ||
				l->next_state == resync_string ||
				l->next_state == resync_string_escape ||
				l->next_state == resync_comment) {
				bvn_notify_error(r);
				return false;
			}
			bvn_set_eof_error(r, error_got_incomplete_bvnr_stream);
			bvn_notify_error(r);
			return false;
		}
		l->processed_bytes += (uint64_t)nb;
		if (l->max_file_size &&
			l->processed_bytes > l->max_file_size) {
			bvn_set_eof_error(r, error_file_too_long);
			bvn_notify_error(r);
			return false;
		}
		off = 0;
		while (off < nb) {
			uint32_t text_start = off;
			if (!bvn_interpret_input_buffer(r, data + off,
											nb - off, &consumed))
				return false;
			off += consumed;
			if (l->use_dbg && consumed) {
				if (!bvn_sink_push(&l->src_dbg,
								  data + text_start,
								  consumed)) {
					bvn_set_eof_error(r,
						error_writing_to_sink);
					bvn_notify_error(r);
					return false;
				}
			}
			if (l->next_state != octet_stream_intro)
				break;
			os_leftover = bvn_read_octet_stream(r,
				data + off, nb - off);
			if (os_leftover < 0)
				return false;
			off = nb - (uint32_t)os_leftover;
		}
	}
	return true;
}
