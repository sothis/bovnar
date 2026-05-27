#ifndef BVN_LEXER_IMPL_H_
#define BVN_LEXER_IMPL_H_
#include "bovnar.h"
#define BVN_TYPE_BUF_CAP        (UINT8_MAX + 1)
#define BVN_STR_BUF_CAP         (UINT16_MAX + 1)
#define BVN_INLINE_UNIT_BUF_CAP (UINT8_MAX + 1)
typedef enum state_e {
	undefined,
	first_bom,
	utf8bom_intro,
	comment_intro,
	ignore_comment_byte,
	comment_outro,
	first_comment_intro,
	first_comment_after_ef,
	first_comment_after_ef_bb,
	first_comment_outro,
	identifier_intro,
	identifier_body,
	identifier_outro,
	type_intro,
	type_noparams,
	type_outro,
	type_body_outro,
	copy_type_byte,
	tf_u,
	tf_ui, tf_uin,
	tf_ut, tf_utf,
	tf_s, tf_si, tf_sin,
	tf_f, tf_fl, tf_flo, tf_floa,
	value_intro,
		array_intro,
		new_array_value,
		array_outro,
		array_dim_sep,
		neg_number_intro,
		zero_intro,
		copy_number_byte,
		fraction_intro,
		fraction_no_int,
		copy_fraction_byte,
		number_outro,
		exp_intro,
		exp_sign_intro,
		copy_exp_byte,
		sp_start,
		sp_nan1, sp_nan2, sp_nan3,
		sp_inf1, sp_inf2, sp_inf3, sp_inf4,
		sp_inf5, sp_inf6, sp_inf7, sp_inf8,
		sp_neg1, sp_neg2, sp_neg3, sp_neg4,
		sp_neg5, sp_neg6, sp_neg7, sp_neg8, sp_neg9,
	symbol_intro,
	symbol_body,
	symbol_outro,
	reference_intro,
	reference_segment_intro,
	reference_segment_body,
	reference_outro,
	copy_string_byte,
	escape_from_copy,
	string_intro,
	string_outro_nosp,
	string_outro,
	octet_stream_intro,
	octet_stream_outro,
	struct_intro,
	struct_outro,
	value_outro,
	number_outro_nosp,
	inline_unit_body,
	inline_unit_outro,
	resync,
	resync_string,
	resync_string_escape,
	resync_comment,
	dimension_state
} state_t;
enum action_id {
	ACT_NONE = 0,
	ACT_ignore_whitespace,
	ACT_comment_intro,
	ACT_ignore_comment_byte,
	ACT_comment_outro,
	ACT_copy_utf8bom_byte,
	ACT_identifier_intro,
	ACT_copy_identifier_byte,
	ACT_to_identifier_outro,
	ACT_value_intro,
	ACT_value_outro,
	ACT_type_intro,
	ACT_type_outro,
	ACT_copy_type_byte,
	ACT_to_type_body_outro,
	ACT_neg_number_intro,
	ACT_copy_number_byte,
	ACT_zero_intro,
	ACT_fraction_intro,
	ACT_fraction_no_int,
	ACT_copy_fraction_byte,
	ACT_to_number_outro,
	ACT_special_number_intro,
	ACT_arr_special_number_intro,
	ACT_kw_advance,
	ACT_sp_to_nan1,
	ACT_sp_to_inf1,
	ACT_sp_to_neg1,
	ACT_sp_nan_outro,
	ACT_sp_inf_outro,
	ACT_sp_neginf_outro,
	ACT_tf_to_u,
	ACT_tf_to_s,
	ACT_tf_to_f,
	ACT_tf_u_to_ui,
	ACT_tf_u_to_ut,
	ACT_tf_uint_done,
	ACT_tf_sint_done,
	ACT_tf_float_done,
	ACT_tf_utf8_done,
	ACT_array_intro,
	ACT_array_outro,
	ACT_new_array_value,
	ACT_array_dim_sep,
	ACT_exp_intro,
	ACT_exp_sign_intro,
	ACT_copy_exp_byte,
	ACT_string_intro,
	ACT_string_outro,
	ACT_copy_string_byte,
	ACT_replace_escaped_byte,
	ACT_escape_from_copy,
	ACT_arr_string_intro,
	ACT_symbol_intro,
	ACT_copy_symbol_byte,
	ACT_to_symbol_outro,
	ACT_reference_intro,
	ACT_copy_reference_dot,
	ACT_copy_reference_byte,
	ACT_to_reference_outro,
	ACT_octet_stream_intro,
	ACT_struct_intro,
	ACT_struct_outro,
	ACT_first_comment_intro,
	ACT_first_comment_byte,
	ACT_first_comment_after_ef,
	ACT_first_comment_after_ef_bb,
	ACT_first_comment_outro,
	ACT_type_null_then_value_outro,
	ACT_type_null_then_new_array_value,
	ACT_type_null_then_array_outro,
	ACT_resync_skip,
	ACT_resync_open_bracket,
	ACT_resync_close_bracket,
	ACT_resync_semicolon,
	ACT_resync_string_intro,
	ACT_resync_string_byte,
	ACT_resync_string_escape,
	ACT_resync_string_escape_byte,
	ACT_resync_string_outro,
	ACT_resync_comment_intro,
	ACT_resync_comment_byte,
	ACT_resync_comment_outro,
	ACT_inline_unit_intro,
	ACT_copy_inline_unit_byte,
	ACT_to_inline_unit_outro,
	ACT_to_string_outro,
	ACT__count
};
typedef bool (*action_t)(bvnr_reader_t* p);
typedef struct bvnr_raw_token_s {
	token_type_t	type;
	bvnr_event_t	event;
	const uint8_t*	str_data;
	uint32_t	str_len;
	const uint8_t*	type_data;
	uint32_t	type_len;
	const uint8_t*	inline_unit_data;
	uint32_t	inline_unit_len;
} bvnr_raw_token_t;
typedef bool (*bvn_lex_sink_fn)(
	bvnr_reader_t* r, const bvnr_raw_token_t* tok);
typedef struct bvn_array_frame_s {
	uint64_t		saved_curr;
	uint64_t		saved_row;
	value_type_spec_t	saved_vtype;
	value_unit_t		saved_vunit;
	uint64_t		dim_row_size;
	bool			in_dim_seq;
} bvn_array_frame_t;
typedef struct bvnr_lexer_s {
	bvnr_source_t		src;
	bvnr_sink_t		src_dbg;
	bool			use_dbg;
	int32_t			byte;
	token_type_t		token_type;
	state_t			next_state;
	state_t			last_state;
	uint8_t			struct_nesting_level;
	uint64_t		array_items;
	uint64_t		array_row_size;
	uint64_t		curr_row_size;
	bool			in_array_element;
	uint8_t			array_nesting_level;
	uint16_t		arr_used_max;
	bvn_array_frame_t	*arr_frames;
	uint64_t		processed_bytes;
	uint64_t		text_bytes;
	uint8_t			utf8_need;
	uint8_t			utf8_lo;
	uint8_t			utf8_hi;
	uint8_t			bom_len;
	uint8_t			type_len;
	uint16_t		str_len;
	uint8_t			bom[2];
	uint8_t			*type_data;
	uint8_t			*str_data;
	uint8_t			*inline_unit_data;
	uint8_t			inline_unit_len;
	uint64_t		line;
	uint64_t		column;
	uint8_t			prev_byte;
	uint16_t		max_identifier_length;
	uint16_t		max_string_length;
	uint16_t		max_number_length;
	uint16_t		max_symbol_length;
	uint16_t		max_reference_length;
	uint64_t		max_array_items;
	uint64_t		max_text_bytes;
	uint64_t		max_file_size;
	uint8_t			max_struct_nesting;
	uint8_t			max_array_nesting;
	bool			continue_on_error;
	uint32_t		resync_array_depth;
	uint32_t		resync_struct_depth;
	uint8_t			resync_saved_struct_nesting;
	uint64_t		recovery_count;
} bvnr_lexer_t;
extern const uint8_t  bvn_after_state_idx_table[dimension_state][256];
extern const action_t bvn_action_table[ACT__count];
extern const state_t  bvn_action_target_state[ACT__count];
extern const state_t  bvn_kw_advance_state[dimension_state];
bool bvn_lex_init(bvnr_lexer_t* l, const bvnr_source_t* src, const bvnr_sink_t* dbg_sink, bvnr_read_flags_t* opts);
void bvn_lex_destroy(bvnr_lexer_t* l);
bool bvn_lex_run(bvnr_reader_t* r);
bool bvn_action_set_state                 (bvnr_reader_t* p);
bool bvn_action_ignore_whitespace         (bvnr_reader_t* p);
bool bvn_action_comment_intro             (bvnr_reader_t* p);
bool bvn_action_comment_outro             (bvnr_reader_t* p);
bool bvn_action_copy_utf8bom_byte         (bvnr_reader_t* p);
bool bvn_action_identifier_intro          (bvnr_reader_t* p);
bool bvn_action_copy_identifier_byte      (bvnr_reader_t* p);
bool bvn_action_value_intro               (bvnr_reader_t* p);
bool bvn_action_value_outro               (bvnr_reader_t* p);
bool bvn_action_type_intro                (bvnr_reader_t* p);
bool bvn_action_type_outro                (bvnr_reader_t* p);
bool bvn_action_copy_type_byte            (bvnr_reader_t* p);
bool bvn_action_neg_number_intro          (bvnr_reader_t* p);
bool bvn_action_copy_number_byte          (bvnr_reader_t* p);
bool bvn_action_zero_intro                (bvnr_reader_t* p);
bool bvn_action_fraction_intro            (bvnr_reader_t* p);
bool bvn_action_fraction_no_int           (bvnr_reader_t* p);
bool bvn_action_copy_fraction_byte        (bvnr_reader_t* p);
bool bvn_action_special_number_intro      (bvnr_reader_t* p);
bool bvn_action_arr_special_number_intro  (bvnr_reader_t* p);
bool bvn_action_kw_advance                (bvnr_reader_t* p);
bool bvn_action_sp_nan_outro              (bvnr_reader_t* p);
bool bvn_action_sp_inf_outro              (bvnr_reader_t* p);
bool bvn_action_sp_neginf_outro           (bvnr_reader_t* p);
bool bvn_action_tf_uint_done              (bvnr_reader_t* p);
bool bvn_action_tf_sint_done              (bvnr_reader_t* p);
bool bvn_action_tf_float_done             (bvnr_reader_t* p);
bool bvn_action_tf_utf8_done              (bvnr_reader_t* p);
bool bvn_action_array_intro               (bvnr_reader_t* p);
bool bvn_action_array_outro               (bvnr_reader_t* p);
bool bvn_action_new_array_value           (bvnr_reader_t* p);
bool bvn_action_array_dim_sep             (bvnr_reader_t* p);
bool bvn_action_exp_intro                 (bvnr_reader_t* p);
bool bvn_action_exp_sign_intro            (bvnr_reader_t* p);
bool bvn_action_copy_exp_byte             (bvnr_reader_t* p);
bool bvn_action_string_intro              (bvnr_reader_t* p);
bool bvn_action_copy_string_byte          (bvnr_reader_t* p);
bool bvn_action_replace_escaped_byte      (bvnr_reader_t* p);
bool bvn_action_arr_string_intro          (bvnr_reader_t* p);
bool bvn_action_symbol_intro              (bvnr_reader_t* p);
bool bvn_action_copy_symbol_byte          (bvnr_reader_t* p);
bool bvn_action_reference_intro           (bvnr_reader_t* p);
bool bvn_action_copy_reference_dot        (bvnr_reader_t* p);
bool bvn_action_copy_reference_byte       (bvnr_reader_t* p);
bool bvn_action_octet_stream_intro        (bvnr_reader_t* p);
bool bvn_action_struct_intro              (bvnr_reader_t* p);
bool bvn_action_struct_outro              (bvnr_reader_t* p);
bool bvn_action_first_comment_intro       (bvnr_reader_t* p);
bool bvn_action_first_comment_byte        (bvnr_reader_t* p);
bool bvn_action_first_comment_after_ef    (bvnr_reader_t* p);
bool bvn_action_first_comment_after_ef_bb (bvnr_reader_t* p);
bool bvn_action_first_comment_outro       (bvnr_reader_t* p);
bool bvn_action_type_null_then_value_outro       (bvnr_reader_t* p);
bool bvn_action_type_null_then_new_array_value   (bvnr_reader_t* p);
bool bvn_action_type_null_then_array_outro       (bvnr_reader_t* p);
bool bvn_action_resync_skip              (bvnr_reader_t* p);
bool bvn_action_resync_open_bracket      (bvnr_reader_t* p);
bool bvn_action_resync_close_bracket     (bvnr_reader_t* p);
bool bvn_action_resync_semicolon         (bvnr_reader_t* p);
bool bvn_action_resync_string_intro      (bvnr_reader_t* p);
bool bvn_action_resync_string_byte       (bvnr_reader_t* p);
bool bvn_action_resync_string_escape     (bvnr_reader_t* p);
bool bvn_action_resync_string_escape_byte(bvnr_reader_t* p);
bool bvn_action_resync_string_outro      (bvnr_reader_t* p);
bool bvn_action_resync_comment_intro     (bvnr_reader_t* p);
bool bvn_action_resync_comment_byte      (bvnr_reader_t* p);
bool bvn_action_resync_comment_outro     (bvnr_reader_t* p);
bool bvn_action_inline_unit_intro        (bvnr_reader_t* p);
bool bvn_action_copy_inline_unit_byte    (bvnr_reader_t* p);
bool bvn_action_to_inline_unit_outro     (bvnr_reader_t* p);
bool bvn_utf8_classify_leader(
	uint32_t b, uint8_t* need, uint8_t* lo, uint8_t* hi);
bool bvn_pull_mem(bvnr_source_t* s, void* buf, uint32_t want, uint32_t* got);
#endif
