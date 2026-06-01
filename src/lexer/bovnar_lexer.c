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

#include <stdlib.h>
#include <string.h>
#include "bovnar.h"
#include "bvn_io_impl.h"
#include "bvn_lexer_impl.h"
#include "bvn_val_impl.h"
/*
 * ===========================================================================
 * Lexer / tokenizer
 * ===========================================================================
 *
 * The lexer is a table-driven state machine. For every (state, input byte)
 * pair, bvn_after_state_idx_table (generated in bovnar_state_table.c) yields
 * an action index; bvn_action_table maps that index to a handler function and
 * bvn_action_target_state gives the fall-through next state. This design was
 * chosen over a hand-written switch because the bovnar grammar has ~90 states
 * and the byte-classification logic is dense and regular: encoding it as a
 * flat table keeps the hot loop branch-light (one table lookup + one indirect
 * call per byte) and makes the grammar auditable as data rather than control
 * flow.
 *
 * As tokens complete, the lexer pushes raw tokens into the validator
 * (bvn_val_receive*), which performs the semantic layer. The lexer itself
 * only recognises lexical structure and enforces length/UTF-8 limits.
 *
 * Two cross-cutting concerns shape the rest of this file:
 *
 *  - Performance: a byte-at-a-time indirect-call loop is slow for the common
 *    case of long identifiers/strings/numbers. The "run LUT" tables below let
 *    the inner loop detect a maximal run of bytes that all self-loop in the
 *    current copy-state and bulk-memcpy them in one shot (bvn_try_bulk_run /
 *    bvn_try_bulk_ws), falling back to the per-byte path at run boundaries.
 *
 *  - Error recovery: with continue_on_error set, a lexical error does not
 *    abort the parse. Instead the machine enters the `resync` family of
 *    states (bvn_enter_resync + bvn_action_resync_*), discards bytes up to the
 *    next safe boundary (a top-level ';'), emits the structural close events
 *    the validator needs to stay balanced, and resumes — counting each event
 *    in recovery_count.
 */

/*
 * Run lookup tables: for each "copy byte" state, lut[b] == 1 iff byte b would
 * keep the machine in that same state (i.e. it is a legal continuation that
 * does not terminate the token). Built once from the authoritative transition
 * table so the two can never drift apart. These power the bulk-copy fast path.
 */
static uint8_t bvn_run_lut_string[256];
static uint8_t bvn_run_lut_ident[256];
static uint8_t bvn_run_lut_number[256];
static uint8_t bvn_run_lut_fraction[256];
static uint8_t bvn_run_lut_exp[256];
static uint8_t bvn_run_lut_symbol[256];
static uint8_t bvn_run_lut_inline_unit[256];
static uint8_t bvn_run_lut_reference[256];
static uint8_t bvn_run_lut_type[256];
static uint8_t bvn_run_lut_ws[256];
static uint8_t bvn_ws_accepting_state[dimension_state];
static bool    bvn_run_luts_inited = false;
/*
 * Mark every printable/high byte that the transition table maps to the given
 * "self" action (the action that copies a byte and stays in the same state).
 * Control bytes 0x09/0x0a/0x0d are deliberately excluded so the bulk path
 * never swallows line-affecting whitespace, keeping line/column accounting
 * exact.
 */
static void bvn_build_run_lut(uint8_t lut[256], state_t st, uint8_t self_action)
{
	for (int b = 0x20; b <= 0x7e; ++b) {
		if (b == 0x09 || b == 0x0a || b == 0x0d)
			continue;
		if (bvn_after_state_idx_table[st][b] == self_action)
			lut[b] = 1;
	}
	for (int b = 0x80; b <= 0xff; ++b) {
		if (bvn_after_state_idx_table[st][b] == self_action)
			lut[b] = 1;
	}
}
/*
 * Populate all run LUTs plus the whitespace-accepting-state table. Marked as a
 * constructor so the tables are ready before main() even if the library is
 * used without an explicit init call; bvn_lex_init also calls it (guarded by
 * bvn_run_luts_inited) so static-init order is never relied upon.
 */
__attribute__((constructor))
static void bvn_init_run_luts(void)
{
	if (bvn_run_luts_inited) return;
	bvn_build_run_lut(bvn_run_lut_string,      copy_string_byte,
	                  ACT_copy_string_byte);
	bvn_build_run_lut(bvn_run_lut_ident,       identifier_body,
	                  ACT_copy_identifier_byte);
	bvn_build_run_lut(bvn_run_lut_number,      copy_number_byte,
	                  ACT_copy_number_byte);
	bvn_build_run_lut(bvn_run_lut_fraction,    copy_fraction_byte,
	                  ACT_copy_fraction_byte);
	bvn_build_run_lut(bvn_run_lut_exp,         copy_exp_byte,
	                  ACT_copy_exp_byte);
	bvn_build_run_lut(bvn_run_lut_symbol,      symbol_body,
	                  ACT_copy_symbol_byte);
	bvn_build_run_lut(bvn_run_lut_inline_unit, inline_unit_body,
	                  ACT_copy_inline_unit_byte);
	bvn_build_run_lut(bvn_run_lut_reference,   reference_segment_body,
	                  ACT_copy_reference_byte);
	bvn_build_run_lut(bvn_run_lut_type,        copy_type_byte,
	                  ACT_copy_type_byte);
	bvn_run_lut_ws[0x09] = 1;
	bvn_run_lut_ws[0x0a] = 1;
	bvn_run_lut_ws[0x0b] = 1;
	bvn_run_lut_ws[0x0c] = 1;
	bvn_run_lut_ws[0x0d] = 1;
	bvn_run_lut_ws[0x20] = 1;
	for (int s = 0; s < (int)dimension_state; ++s)
		bvn_ws_accepting_state[s] = (
			bvn_after_state_idx_table[s][0x20] == ACT_ignore_whitespace &&
			bvn_after_state_idx_table[s][0x0a] == ACT_ignore_whitespace
		) ? 1u : 0u;
	bvn_run_luts_inited = true;
}
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
/*
 * Begin error recovery (only reached when continue_on_error is set).
 *
 * The tricky part is that the validator downstream tracks open arrays and
 * structs; if we just skipped bytes we would leave it with dangling open
 * scopes and every subsequent token would be misinterpreted. So before
 * switching to the `resync` state we synthesise the array-row-end events
 * needed to close every currently-open array level, restoring each parent
 * frame's saved type/unit as we unwind. Struct nesting is intentionally left
 * standing and remembered in resync_saved_struct_nesting: structs are closed
 * later, at the resync boundary, because a stray byte inside a struct should
 * not silently discard the whole enclosing object. All scratch state (string
 * buffers, UTF-8 progress, accumulator) is reset so the next good token starts
 * clean, and recovery_count is bumped for diagnostics.
 */
static bool bvn_enter_resync(bvnr_reader_t* p)
{
	bvnr_lexer_t* l = &p->lex;
	l->resync_saved_struct_nesting = l->struct_nesting_level;
	while (l->array_nesting_level > 0) {
		if (!bvn_val_receive_event(p, ev_array_row_end)) {
			p->val.value_type  = BVN_TYPE_PLAIN;
			p->val.parsed_unit = BVN_UNIT_NO_PREFIX(bu_none);
			return false;
		}
		--l->array_nesting_level;
		if (l->array_nesting_level > 0) {
			bvn_array_frame_t *f = &l->arr_frames[l->array_nesting_level];
			p->val.value_type         = f->saved_vtype;
			p->val.parsed_unit        = f->saved_vunit;
			p->val.parsed_unit_serial = f->saved_unit_serial;
		}
	}
	l->utf8_need = 0;
	l->utf8_lo = 0;
	l->utf8_hi = 0;
	l->bom_len    = 0;
	l->str_len    = 0;
	l->type_len   = 0;
	l->inline_unit_len = 0;
	l->token_type = token_is_unknown;
	bvn_acc_reset(&p->val);
	p->val.value_type          = BVN_TYPE_PLAIN;
	p->val.parsed_unit         = BVN_UNIT_NO_PREFIX(bu_none);
	p->val.has_annotation_unit = false;
	p->val.error_line          = 0;
	p->val.error_column        = 0;
	p->val.error_byte          = 0;
	p->val.error_offset        = 0;
	l->resync_array_depth  = 0;
	l->resync_struct_depth = 0;
	l->array_items         = 0;
	l->next_state          = resync;
	++l->recovery_count;
	l->in_array_element    = false;
	l->curr_row_size       = 0;
	l->array_row_size      = 0;
	if (l->arr_used_max)
		memset(l->arr_frames, 0,
		       l->arr_used_max * sizeof(bvn_array_frame_t));
	l->arr_used_max = 0;
	return true;
}
/*
 * Escape decode table. The high bit (ESC_VALID) flags a legal escape letter;
 * the low byte holds the replacement character. A single table lookup replaces
 * a switch and lets bvn_decode_escape stay branch-free on the hot path. Only
 * the bovnar-approved escapes are present; everything else decodes as invalid.
 */
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
/*
 * Append the current byte to a length-bounded buffer, raising `overflow` if
 * the per-token limit is hit. Every variable-length token (identifier, string,
 * number, symbol, reference) funnels through this so the configurable size
 * caps are enforced uniformly in one place — the defence against a hostile
 * stream trying to exhaust memory with one unbounded token. The thin
 * bvn_push_str/_number/_string/_reference wrappers just bind the right limit
 * and error code.
 */
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
/*
 * Emit the token that has just finished accumulating.
 *
 * Called at every token boundary (whitespace, separators, closing bytes). It
 * NUL-terminates the three scratch buffers — the validator and unit/type
 * parsers expect C strings even though lengths are also passed — packages them
 * into a bvnr_raw_token_t, and hands them to the sink (normally bvn_val_receive).
 * Structural tokens carry no payload and are filtered out early; an identifier
 * maps to an assignment-start event, everything else to a data event. On
 * success all scratch lengths are zeroed so the next token starts fresh.
 */
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
/*
 * ---------------------------------------------------------------------------
 * Action handlers
 * ---------------------------------------------------------------------------
 * One handler per ACT_* index. Each is invoked with the just-read byte already
 * in p->lex.byte and is responsible for (a) mutating accumulator/scratch state
 * and (b) setting p->lex.next_state. They return false (after recording an
 * error) to abort or trigger recovery. Most are tiny state-setters; the few
 * with real logic (arrays, structs, octet streams, resync) are commented
 * individually below.
 *
 * bvn_action_set_state is the degenerate handler for pure transitions that do
 * no work; the dispatch loop special-cases it to skip the indirect call.
 */
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
	/* number_outro_nosp is vestigial (unreachable since nan/inf/ninf
	 * became bare keywords); the first branch is dead but harmless. */
	if      (s == number_outro_nosp) s = number_outro;
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
	uint32_t base = p->lex.array_nesting_level;
	uint32_t hwm  = p->lex.arr_used_max;
	if (hwm > base) {
		memset(p->lex.arr_frames + base, 0,
		       (hwm - base) * sizeof(bvn_array_frame_t));
	}
	p->lex.arr_used_max = (uint16_t)base;
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
	if (l->type_len == BVN_TYPE_BUF_CAP - 1u) {
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
	p->val.acc_has_dot = true;
	return bvn_push_number_byte(p, fraction_intro);
}
bool bvn_action_fraction_no_int(bvnr_reader_t* p)
{
	bool is_arr = p->lex.in_array_element;
	if (!bvn_lex_number_begin(p, is_arr)) return false;
	p->val.acc_has_dot = true;
	return bvn_push_number_byte(p, fraction_no_int);
}
bool bvn_action_copy_fraction_byte(bvnr_reader_t* p)
{
	return bvn_push_number_byte(p, copy_fraction_byte);
}
/*
 * '[' — open an array level.
 *
 * bovnar arrays are rectangular/multi-dimensional, so each nesting level needs
 * its own bookkeeping: the per-row element count and the established row size
 * to check ragged rows against. We push the parent level's counters AND its
 * value-type/unit onto arr_frames[level] (a manually managed stack sized at
 * init to max_array_nesting+1) so they can be restored on the matching ']'.
 * Saving type/unit per frame is what lets an inner array inherit, then later
 * not clobber, the element type annotation of its parent. in_dim_seq carries
 * the expected row width for subsequent sibling rows of a multi-dim array.
 */
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
	f->saved_unit_serial = p->val.parsed_unit_serial;
	p->lex.curr_row_size  = 0;
	/* UINT64_MAX = "row width not yet established" (an empty row has width 0,
	 * a real value, so 0 can no longer double as the unset sentinel). */
	p->lex.array_row_size = f->in_dim_seq ? f->dim_row_size : UINT64_MAX;
	++p->lex.curr_row_size;
	/*
	 * The array we are about to open tracks its OWN '/'-dimension sequence in
	 * the child frame (arr_frames[level + 1]). Start that frame clean: a row
	 * width established by a *previous, unrelated* array instance at this depth
	 * (a sibling branch reached via the parent's own '/' or ',') must never
	 * leak in and falsely constrain this fresh array's nested rows. Only a '/'
	 * occurring inside this very array may later set in_dim_seq here. Without
	 * this reset, "[[1,2]/[3,4]]/[[5,6,7]/[8,9,10]]" wrongly rejects the second
	 * branch's inner rows against the first branch's width. The index is in
	 * range: the nesting guard above rejected level == max_array_nesting, and
	 * arr_frames is sized max_array_nesting + 1.
	 */
	bvn_array_frame_t *child = &p->lex.arr_frames[level + 1u];
	child->in_dim_seq   = false;
	child->dim_row_size = 0;
	p->lex.token_type       = token_is_null_value;
	p->lex.in_array_element = true;
	bvn_acc_reset(&p->val);
	if (!bvn_val_on_array_intro(p))
		return false;
	++p->lex.array_nesting_level;
	if (p->lex.array_nesting_level > p->lex.arr_used_max)
		p->lex.arr_used_max = p->lex.array_nesting_level;
	p->lex.next_state = array_intro;
	return true;
}
/*
 * ']' — close the current array level. Finalises any pending element, asks the
 * validator to verify this row's width against the established row size, then
 * pops the saved frame: the just-closed row's width becomes the parent frame's
 * dim_row_size (so the parent can enforce that sibling rows match), and the
 * parent's counters/type are restored. The unit_serial guard avoids
 * needlessly overwriting a unit that the inner scope never changed.
 */
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
	if (p->val.parsed_unit_serial != f->saved_unit_serial) {
		p->val.parsed_unit         = f->saved_vunit;
		p->val.parsed_unit_serial  = f->saved_unit_serial;
	}
	p->lex.next_state = array_outro;
	return true;
}
/*
 * ']' reached directly from the array_intro state — only whitespace or comments
 * appeared between '[' and ']'. This is an EMPTY array row (zero elements).
 * array_intro pre-seeds the first element position with a null placeholder
 * (curr_row_size == 1, token_is_null_value); an empty row must NOT finalise it,
 * so — unlike bvn_action_array_outro — this emits no ev_data, reports a row
 * width of 0, and then pops the frame identically. "[]" is therefore distinct
 * from "[null]" (one null element). A width of 0 is a real width: an empty
 * '/'-row may only sit beside other empty rows ([]/[] is valid; []/[1] is a
 * row-size mismatch).
 */
bool bvn_action_array_outro_empty(bvnr_reader_t* p)
{
	if (!p->lex.array_nesting_level) {
		bvn_lexer_set_error(p, error_unexpected_input_byte);
		return false;
	}
	/* Clear the null placeholder array_intro pre-seeded. We skip
	 * bvn_lex_finalize (which would emit it as an element AND reset the token),
	 * so reset the pending-token state here — otherwise the lingering
	 * token_is_null_value would be emitted as a phantom value when the
	 * enclosing assignment/element is finalised. */
	p->lex.token_type = token_is_unknown;
	p->lex.str_len    = 0u;
	p->lex.curr_row_size = 0u;
	if (!bvn_val_on_array_outro(p, 0u, &p->lex.array_row_size))
		return false;
	--p->lex.array_nesting_level;
	uint64_t level = p->lex.array_nesting_level;
	bvn_array_frame_t *f = &p->lex.arr_frames[level];
	f->dim_row_size       = p->lex.array_row_size;
	p->lex.curr_row_size  = f->saved_curr + 1u;
	p->lex.array_row_size = f->saved_row;
	p->val.value_type     = f->saved_vtype;
	if (p->val.parsed_unit_serial != f->saved_unit_serial) {
		p->val.parsed_unit         = f->saved_vunit;
		p->val.parsed_unit_serial  = f->saved_unit_serial;
	}
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
	if (p->val.parsed_unit_serial != par->saved_unit_serial) {
		p->val.parsed_unit         = par->saved_vunit;
		p->val.parsed_unit_serial  = par->saved_unit_serial;
	}
	++p->lex.curr_row_size;
	p->lex.token_type = token_is_null_value;
	p->lex.next_state = new_array_value;
	return true;
}
/*
 * Dimension separator between rows of a multi-dimensional array. Emits
 * ev_array_dim_start and flags the current frame as a dimension sequence so
 * the next opened row inherits the sibling row width to validate against.
 */
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
	p->val.acc_has_exp = true;
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
/*
 * The grammar recognises the type-family keywords (uint/sint/float/utf8/bool)
 * letter-by-letter via the tf_* states. Once matched, bvn_tf_family_done writes
 * the canonical spelling into type_data so downstream parsing sees a normal
 * type token. (The special floats nan/inf/ninf are no longer lexed here: they
 * are bare reserved keywords reclassified from symbols by the validator, like
 * null/true/false.)
 */
static bool bvn_tf_family_done(bvnr_reader_t* p,
	const char* name, uint32_t len, state_t after)
{
	memcpy(p->lex.type_data, name, len);
	p->lex.type_len   = (uint8_t)len;
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
bool bvn_action_tf_bool_done(bvnr_reader_t* p)
	{ return bvn_tf_family_done(p, "bool",      4, type_body_outro); }
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
/*
 * Entry to a binary octet stream. This only flips the state and signals the
 * validator; the actual length-prefixed binary framing is read out-of-band by
 * bvn_read_octet_stream because that data is not text and must bypass the
 * UTF-8/whitespace machinery entirely (see the main loop in bvn_lex_run).
 */
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
	if (!bvn_val_receive_event(p, ev_struct_start))
		return false;
	++l->struct_nesting_level;
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
/*
 * ---------------------------------------------------------------------------
 * Resync (error-recovery) handlers
 * ---------------------------------------------------------------------------
 * Active only after bvn_enter_resync. The goal is to reach the next safe
 * top-level boundary (a ';' that is not inside a bracket pair or string) and
 * resume normal parsing there. While skipping we still have to track bracket
 * and string/comment nesting so a ';' inside skipped-over junk is not mistaken
 * for the boundary — resync_array_depth / resync_struct_depth count brackets
 * opened *after* recovery began. resync_string/_comment states swallow string
 * and comment bodies wholesale so their contents can't trigger false resyncs.
 */
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
/*
 * A closing bracket seen during resync. If it merely balances a bracket opened
 * since recovery started, just decrement the resync depth. Otherwise it closes
 * a real array/struct that was open before the error, so we must emit the
 * corresponding close events to the validator (unwinding array frames, or
 * closing all arrays then the struct for '}') to keep its scope stack
 * balanced. This is what lets recovery rejoin the stream cleanly at a point
 * inside a still-open container rather than only at the very top level.
 */
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
			if (!bvn_val_receive_event(p, ev_array_row_end))
				return false;
			--l->array_nesting_level;
			uint64_t level = l->array_nesting_level;
			bvn_array_frame_t *f = &l->arr_frames[level];
			f->dim_row_size      = l->curr_row_size;
			l->curr_row_size     = f->saved_curr + 1u;
			l->array_row_size    = f->saved_row;
			p->val.value_type    = f->saved_vtype;
			p->val.parsed_unit   = f->saved_vunit;
			l->in_array_element  = (l->array_nesting_level > 0);
			l->next_state        = resync;
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
			while (l->array_nesting_level > 0) {
				if (!bvn_val_receive_event(p, ev_array_row_end))
					return false;
				--l->array_nesting_level;
				if (l->array_nesting_level > 0) {
					bvn_array_frame_t *f = &l->arr_frames[l->array_nesting_level];
					p->val.value_type  = f->saved_vtype;
					p->val.parsed_unit = f->saved_vunit;
				}
			}
			l->curr_row_size    = 0;
			l->array_row_size   = 0;
			l->in_array_element = false;
			if (l->arr_used_max)
				memset(l->arr_frames, 0,
				       l->arr_used_max *
				       sizeof(bvn_array_frame_t));
			l->arr_used_max = 0;
			--l->struct_nesting_level;
			if (!bvn_val_receive_event(p, ev_struct_end))
				return false;
			l->next_state = resync;
		} else {
			l->next_state = resync;
		}
	} else {
		l->next_state = resync;
	}
	return true;
}
/*
 * Reached a ';' boundary during recovery: collapse everything back to a clean
 * value_outro state. Any structs opened since recovery began are closed (down
 * to the nesting level captured when recovery started), all array/scratch
 * state is cleared, and the value type/unit are reset to plain. After this the
 * machine is indistinguishable from having just finished a well-formed
 * assignment, so the next byte resumes normal parsing.
 */
static bool bvn_resync_semicolon_reset(bvnr_reader_t* p)
{
	bvnr_lexer_t* l = &p->lex;
	bool ok = true;
	while (l->struct_nesting_level > l->resync_saved_struct_nesting) {
		--l->struct_nesting_level;
		if (!bvn_val_receive_event(p, ev_struct_end)) {
			ok = false;
			break;
		}
	}
	if (l->struct_nesting_level < l->resync_saved_struct_nesting)
		l->resync_saved_struct_nesting = l->struct_nesting_level;
	l->token_type           = token_is_unknown;
	l->str_len              = 0;
	l->type_len             = 0;
	l->inline_unit_len      = 0;
	l->utf8_need            = 0;
	l->utf8_lo              = 0;
	l->utf8_hi              = 0;
	l->in_array_element     = false;
	l->struct_nesting_level = l->resync_saved_struct_nesting;
	l->array_nesting_level  = 0;
	l->resync_array_depth   = 0;
	l->resync_struct_depth  = 0;
	l->array_items          = 0;
	l->curr_row_size        = 0;
	l->array_row_size       = 0;
	if (l->arr_used_max)
		memset(l->arr_frames, 0,
		       l->arr_used_max * sizeof(bvn_array_frame_t));
	l->arr_used_max = 0;
	p->val.value_type          = BVN_TYPE_PLAIN;
	p->val.parsed_unit         = BVN_UNIT_NO_PREFIX(bu_none);
	p->val.has_annotation_unit = false;
	bvn_acc_reset(&p->val);
	l->next_state = value_outro;
	return ok;
}
bool bvn_action_resync_semicolon(bvnr_reader_t* p)
{
	bvnr_lexer_t* l = &p->lex;
	if (l->resync_array_depth > 0 || l->resync_struct_depth > 0) {
		l->next_state = resync;
		return true;
	}
	while (l->array_nesting_level > 0) {
		if (!bvn_val_receive_event(p, ev_array_row_end)) {
			(void)bvn_resync_semicolon_reset(p);
			return false;
		}
		--l->array_nesting_level;
		if (l->array_nesting_level > 0) {
			bvn_array_frame_t *f = &l->arr_frames[l->array_nesting_level];
			p->val.value_type         = f->saved_vtype;
			p->val.parsed_unit        = f->saved_vunit;
			p->val.parsed_unit_serial = f->saved_unit_serial;
		}
	}
	if (!bvn_resync_semicolon_reset(p))
		return false;
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
/*
 * Inline unit suffix (e.g. the `kg` in `42 kg`). Disallowed inside array
 * elements — array element types/units come from the array's annotation, not
 * from each element — hence the early rejection. The bytes are accumulated in
 * a dedicated small buffer and later parsed by the unit machinery.
 */
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
	if (l->inline_unit_len == BVN_INLINE_UNIT_BUF_CAP - 1u) {
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
/*
 * ---------------------------------------------------------------------------
 * Octet-stream reading
 * ---------------------------------------------------------------------------
 * An octet stream is raw binary framed as repeated [0x01][len_lo][len_hi]
 * [len bytes] chunks terminated by a 0x00 tag. Because the framing is binary,
 * it is read directly from the source here rather than through the text state
 * machine. When the text loop hands control over, some bytes of the next chunk
 * may already sit in the text buffer; those "residual" bytes are consumed
 * first (src->resid) before pulling more from the underlying source, and any
 * bytes left over after the stream ends are returned so the text loop can
 * resume exactly where the binary data stopped.
 */

/*
 * Fill `output` with exactly `length` bytes, drawing first from the residual
 * buffer then from the source, looping over short reads. Also mirrors bytes to
 * the debug sink and enforces the file-size cap, so the binary path honours the
 * same limits and tee behaviour as the text path.
 */
static bool bvn_os_read_exact(
	octet_source_t* src, void* output, uint32_t length)
{
	bvnr_reader_t* p   = src->p;
	uint8_t*        buf = (uint8_t*)output;
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
/*
 * Drive the chunk loop: read a tag, and on 0x01 read the 16-bit little-endian
 * length (where 0 means the maximal 65536, since 0-length chunks are useless)
 * and forward that many bytes to the validator; on 0x00 emit the stream-end
 * event and stop. Any other tag is a framing desync. *out_leftover reports how
 * many residual bytes were not consumed so the caller can rewind the text loop.
 */
static bool bvn_read_octet_stream(
	bvnr_reader_t* p, const uint8_t* resid, uint32_t resid_len,
	uint32_t* out_leftover)
{
	octet_source_t src = { .p = p, .resid = resid, .resid_left = resid_len };
	uint8_t  tag, lenbuf[2];
	uint32_t chunklen;
	for (;;) {
		if (!bvn_os_read_exact(&src, &tag, 1))
			return false;
		if (tag == 0x00) {
			p->lex.token_type = token_is_unknown;
			p->lex.str_len    = 0;
			if (!bvn_val_receive_event(p, ev_octet_stream_end))
				return false;
			p->lex.next_state = octet_stream_outro;
			*out_leftover = src.resid_left;
			return true;
		}
		if (tag != 0x01) {
			bvn_capture_os_error(p, error_octet_stream_out_of_sync);
			bvn_notify_error(p);
			return false;
		}
		if (!bvn_os_read_exact(&src, lenbuf, 2))
			return false;
		chunklen = (uint32_t)lenbuf[0]
				 | ((uint32_t)lenbuf[1] << 8);
		if (!chunklen)
			chunklen = 65536u;
		if (!bvn_os_read_exact(&src, p->lex.str_data, chunklen))
			return false;
		if (!bvn_val_receive_octet_chunk(p, p->lex.str_data, chunklen))
			return false;
	}
}
/*
 * Classify a UTF-8 leading byte: how many continuation bytes follow (`need`)
 * and the valid range [lo,hi] of the *first* continuation. The per-leader lo/hi
 * ranges are the standard UTF-8 well-formedness constraints from RFC 3629 /
 * Unicode Table 3-7 that reject overlong encodings, surrogates (0xED) and
 * out-of-range code points (0xF4), so validation is exact rather than the
 * naive "0x80-0xBF for every trailing byte". Returns false for bytes that can
 * never begin a sequence (0x80-0xC1, 0xF5-0xFF).
 */
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
	if (b <= 0xf3) { *need = 3; *lo = 0x80; *hi = 0xbf; return true;  }
	if (b == 0xf4) { *need = 3; *lo = 0x80; *hi = 0x8f; return true;  }
	return false;
}
typedef struct {
	uint8_t need, lo, hi;
} utf8_state_t;
/*
 * Incremental UTF-8 validator: feed one byte, return false on malformation.
 * Holding the decode state in a tiny struct (need/lo/hi) lets the same logic
 * serve both the streaming lexer (state stored across buffer boundaries in
 * bvn_utf8_feed) and the one-shot bvn_validate_string. After the first
 * continuation byte the range relaxes to the generic 0x80-0xBF.
 */
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
/*
 * Public helper: is the whole buffer well-formed UTF-8? Runs the incremental
 * validator to completion and requires no pending continuation bytes at the
 * end (a truncated multibyte sequence at EOF is invalid).
 */
bool bvn_validate_string(const uint8_t* data, size_t len)
{
	utf8_state_t st = {0};
	for (size_t i = 0; i < len; i++) {
		if (!bvn_utf8_feed_st(&st, data[i]))
			return false;
	}
	return st.need == 0;
}
/*
 * The core per-byte step: look up the action for (current state, byte), then
 * dispatch. Whitespace is the hottest case so it short-circuits before the
 * indirect call; a zero index means the byte is illegal in this state. Pure
 * state transitions (bvn_action_set_state) are inlined here too, so an actual
 * function-pointer call only happens for handlers that do real work.
 */
static inline bool bvn_call_action_for_new_byte(bvnr_reader_t* p)
{
	uint8_t idx = bvn_after_state_idx_table[p->lex.next_state][p->lex.byte];
	if (idx == ACT_ignore_whitespace)
		return true;
	if (!idx) {
		bvn_lexer_set_error(p, error_unexpected_input_byte);
		return false;
	}
	action_t fn = bvn_action_table[idx];
	if (fn == bvn_action_set_state) {
		p->lex.next_state = bvn_action_target_state[idx];
		return true;
	}
	return fn(p);
}
static inline void bvn_advance_line(bvnr_lexer_t* l, uint8_t prev)
{
	if (l->byte == 0x0d) {
		++l->line;
		l->column = 0;
	} else if (l->byte == 0x0a) {
		if (prev != 0x0d)
			++l->line;
		l->column = 0;
	}
}
/*
 * Scan ahead to find the end of a maximal run of string-body bytes starting at
 * `start`: stops at a control byte, a quote, a backslash (escape), or any
 * malformed/incomplete UTF-8. Returns the index one past the last byte that is
 * safe to bulk-copy — crucially it never returns mid-multibyte-sequence
 * (last_complete tracks the last code-point boundary), so a UTF-8 character
 * split across the run boundary is left for the per-byte path to handle.
 */
static inline uint32_t bvn_string_ext_end(
	const uint8_t* data, uint32_t start, uint32_t len)
{
	uint8_t  need = 0, lo = 0, hi = 0;
	uint32_t last_complete = start;
	uint32_t i = start;
	while (i < len) {
		uint8_t b = data[i];
		/* 0x00-0x1F and 0x7F (DEL) are rejected control bytes in string
		 * bodies (BVN_REJECT_ASCII_CTRL); break so the per-byte path
		 * raises the error rather than the fast path swallowing them.
		 * Without the explicit 0x7F check the bulk scanner would drift
		 * from the authoritative transition table and accept DEL. */
		if (b < 0x20u || b == 0x7fu || b == 0x22u || b == 0x5cu)
			break;
		if (b < 0x80u) {
			if (need) return last_complete;
			last_complete = i + 1u;
		} else if (need) {
			if (b < lo || b > hi) return last_complete;
			--need;
			lo = 0x80u; hi = 0xbfu;
			if (!need) last_complete = i + 1u;
		} else {
			if (!bvn_utf8_classify_leader(b, &need, &lo, &hi))
				return last_complete;
		}
		++i;
	}
	return need ? last_complete : i;
}
/*
 * Bulk fast path. If the machine is sitting in one of the "copy byte" states,
 * find the longest run of bytes from `start` that all stay in that state and
 * memcpy them into the target buffer in a single call, returning how many
 * bytes were consumed (0 = fall back to the per-byte loop). This is the main
 * throughput win: long strings/identifiers/numbers no longer pay one indirect
 * call per byte. All the limits the per-byte path enforces (buffer capacity,
 * max_string_length, max_text_bytes) are re-applied to the run length so the
 * fast path can never overrun a bound. Strings get a dedicated branch because
 * their terminator set differs; the rest share a run-LUT-driven scanner that
 * also walks complete UTF-8 sequences. l->byte/prev_byte are updated to the
 * last copied byte so the per-byte path resumes consistently.
 */
static inline uint32_t bvn_try_bulk_run(
	bvnr_lexer_t* l, const uint8_t* data, uint32_t start, uint32_t len)
{
	state_t st = l->next_state;
	if (st == copy_string_byte) {
		uint32_t end = bvn_string_ext_end(data, start, len);
		uint32_t n   = end - start;
		if (!n) return 0;
		uint32_t avail = (uint32_t)l->max_string_length - (uint32_t)l->str_len;
		if (!avail) return 0;
		if (n > avail) n = avail;
		if (l->max_text_bytes) {
			uint64_t tb = l->max_text_bytes - l->text_bytes;
			if (!tb) return 0;
			if ((uint64_t)n > tb) n = (uint32_t)tb;
		}
		memcpy(l->str_data + l->str_len, data + start, n);
		l->str_len     = (uint16_t)(l->str_len + n);
		l->text_bytes += n;
		l->column     += n;
		l->byte        = data[start + n - 1u];
		l->prev_byte   = data[start + n - 1u];
		return n;
	}
	const uint8_t* lut;
	uint8_t* dst;
	uint32_t dst_len;
	uint32_t dst_cap;
	bool is_str_buf = true;
	switch (st) {
	case identifier_body:
		lut = bvn_run_lut_ident;
		dst_cap = l->max_identifier_length;
		break;
	case copy_number_byte:
		lut = bvn_run_lut_number;
		dst_cap = l->max_number_length;
		break;
	case copy_fraction_byte:
		lut = bvn_run_lut_fraction;
		dst_cap = l->max_number_length;
		break;
	case copy_exp_byte:
		lut = bvn_run_lut_exp;
		dst_cap = l->max_number_length;
		break;
	case symbol_body:
		lut = bvn_run_lut_symbol;
		dst_cap = l->max_symbol_length;
		break;
	case reference_segment_body:
		lut = bvn_run_lut_reference;
		dst_cap = l->max_reference_length;
		break;
	case inline_unit_body:
		lut = bvn_run_lut_inline_unit;
		dst = l->inline_unit_data;
		dst_len = l->inline_unit_len;
		dst_cap = BVN_INLINE_UNIT_BUF_CAP - 1u;
		is_str_buf = false;
		break;
	case copy_type_byte:
		lut = bvn_run_lut_type;
		dst = l->type_data;
		dst_len = l->type_len;
		dst_cap = BVN_TYPE_BUF_CAP - 1u;
		is_str_buf = false;
		break;
	default:
		return 0;
	}
	if (is_str_buf) {
		dst     = l->str_data;
		dst_len = l->str_len;
	}
	if (!lut[data[start]])
		return 0;
	uint32_t end = start;
	uint32_t last_complete;
	while (end < len) {
		uint32_t a = end;
		while (a < len && data[a] < 0x80u && lut[data[a]]) ++a;
		last_complete = a;
		end = a;
		if (end >= len) break;
		uint8_t b = data[end];
		if (b < 0x80u) break;
		if (!lut[b]) break;
		uint8_t need = 0, lo = 0, hi = 0;
		if (!bvn_utf8_classify_leader(b, &need, &lo, &hi)) break;
		++end;
		bool seq_ok = true;
		while (need) {
			if (end >= len) { seq_ok = false; break; }
			uint8_t cb = data[end];
			if (cb < lo || cb > hi) { seq_ok = false; break; }
			--need;
			lo = 0x80u; hi = 0xbfu;
			++end;
		}
		if (!seq_ok) break;
		last_complete = end;
	}
	end = last_complete;
	if (end == start) return 0;
	uint32_t n = end - start;
	uint32_t avail = dst_cap - dst_len;
	if (!avail) return 0;
	if (n > avail) n = avail;
	if (l->max_text_bytes) {
		uint64_t tb = l->max_text_bytes - l->text_bytes;
		if (!tb) return 0;
		if ((uint64_t)n > tb) n = (uint32_t)tb;
	}
	memcpy(dst + dst_len, data + start, n);
	if (is_str_buf) {
		l->str_len = (uint16_t)(dst_len + n);
	} else if (st == copy_type_byte) {
		l->type_len = (uint8_t)(dst_len + n);
	} else {
		l->inline_unit_len = (uint8_t)(dst_len + n);
	}
	l->text_bytes += n;
	l->column     += n;
	l->byte        = data[start + n - 1u];
	l->prev_byte   = data[start + n - 1u];
	return n;
}
/*
 * Companion fast path for whitespace runs, used only in states that accept
 * whitespace as a no-op. It collapses a span of spaces/tabs/newlines in one
 * pass while keeping line and column accounting exact — including tab stops
 * (rounded up to multiples of 4) and CR/LF handling (a CRLF pair counts as one
 * line). This matters because indentation in pretty-printed bovnar is common.
 */
static inline uint32_t bvn_try_bulk_ws(
	bvnr_lexer_t* l, const uint8_t* data, uint32_t start, uint32_t len)
{
	state_t st = l->next_state;
	if (!bvn_ws_accepting_state[st])
		return 0;
	uint64_t col  = l->column;
	uint64_t line = l->line;
	uint8_t  prev = l->prev_byte;
	uint32_t end  = start;
	while (end < len) {
		uint8_t b = data[end];
		if (b == 0x20)      { ++col; }
		else if (b == 0x09) { col = ((col >> 2u) + 1u) << 2u; }
		else if (b == 0x0a) {
			if (prev != 0x0d) ++line;
			col = 0;
		}
		else if (b == 0x0d) { ++line; col = 0; }
		else if (b == 0x0b || b == 0x0c) { ++col; }
		else break;
		prev = b;
		++end;
	}
	uint32_t n = end - start;
	if (!n) return 0;
	if (l->max_text_bytes &&
	    l->text_bytes + (uint64_t)n > l->max_text_bytes)
		return 0;
	l->text_bytes += n;
	l->line        = line;
	l->column      = col;
	l->byte        = data[end - 1u];
	l->prev_byte   = prev;
	return n;
}
/*
 * Run the state machine over one contiguous input buffer.
 *
 * For each position it first tries the bulk fast paths (only when not mid
 * UTF-8 sequence), then falls back to the exact per-byte path: enforce the
 * text-byte cap, validate UTF-8 incrementally, update line/column, and invoke
 * the byte's action. On a per-byte error it either aborts or, under
 * continue_on_error, enters resync and keeps going. The one place it returns
 * early mid-buffer is when an octet-stream intro is hit: *consumed marks where
 * binary framing takes over so the caller can switch to bvn_read_octet_stream.
 */
static bool bvn_interpret_input_buffer(
	bvnr_reader_t* p, const uint8_t* data, uint32_t len,
	uint32_t* consumed)
{
	bvnr_lexer_t* l = &p->lex;
	for (uint32_t idx = 0; idx < len; ++idx) {
		if (l->utf8_need == 0u) {
			uint32_t n = bvn_try_bulk_run(l, data, idx, len);
			if (n) {
				idx += n - 1u;
				continue;
			}
			if (bvn_run_lut_ws[data[idx]]) {
				n = bvn_try_bulk_ws(l, data, idx, len);
				if (n) {
					idx += n - 1u;
					continue;
				}
			}
		}
		if (l->max_text_bytes &&
			l->text_bytes == l->max_text_bytes) {
			uint8_t offending = data[idx];
			uint64_t col;
			if (offending == 0x09u)
				col = ((l->column >> 2u) + 1u) << 2u;
			else if (l->utf8_need == 0)
				col = l->column + 1u;
			else
				col = l->column;
			p->val.last_error   = error_text_data_too_long;
			p->val.error_line   = l->line;
			p->val.error_column = col;
			p->val.error_byte   = (uint32_t)offending & 0xffu;
			p->val.error_offset = l->text_bytes;
			bvn_notify_error(p);
			return false;
		}
		++l->text_bytes;
		uint8_t b = data[idx];
		l->byte = b;
		uint8_t prev = l->prev_byte;
		l->prev_byte = b;
		uint8_t saved_need = l->utf8_need;
		bool line_advanced = false;
		if (b < 0x80u && saved_need == 0u) {
		} else if (!bvn_utf8_feed(l, (uint32_t)b)) {
			if (l->byte == 0x09)
				l->column = ((l->column >> 2u) + 1u) << 2u;
			else if (saved_need == 0)
				++l->column;
			p->val.last_error = error_invalid_utf8_byte;
			bvn_set_error_pos(p, (uint32_t)l->byte & 0xffu,
				l->text_bytes - 1);
			bvn_notify_error(p);
			bvn_advance_line(l, prev);
			line_advanced = true;
			if (l->continue_on_error) {
				bool was_continuation = (saved_need > 0);
				if (!bvn_enter_resync(p))
					return false;
				if (!was_continuation || l->byte == 0x09 ||
					l->byte == 0x0a || l->byte == 0x0d) {
					continue;
				}
				if (!bvn_utf8_feed(l, (uint32_t)l->byte)) {
					++l->column;
					continue;
				}
				++l->column;
			} else {
				return false;
			}
		}
		if (l->byte == 0x09)
			l->column = ((l->column >> 2u) + 1u) << 2u;
		else if (saved_need == 0)
			++l->column;
		if (!bvn_call_action_for_new_byte(p)) {
			bvn_set_error_pos(p, (uint32_t)l->byte & 0xffu,
				l->text_bytes - 1);
			bvn_notify_error(p);
			if (!line_advanced)
				bvn_advance_line(l, prev);
			if (l->continue_on_error) {
				if (!bvn_enter_resync(p))
					return false;
				/*
				 * A semantic error (e.g. value-out-of-range) is only
				 * detected once the value's terminating ';' has been
				 * consumed, so the stream is already at a clean
				 * assignment boundary. Skipping forward to the *next*
				 * ';' would swallow the following — possibly valid —
				 * assignment. Per §13.2 ("resume on ';' at the saved
				 * nesting depth") treat the just-consumed ';' as that
				 * boundary and resume normal parsing immediately.
				 */
				if (l->byte == (uint8_t)';')
					bvn_resync_semicolon_reset(p);
			} else {
				return false;
			}
		} else {
			if (!line_advanced)
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
/*
 * Free lexer-owned heap. Note str_data/type_data/inline_unit_data are a single
 * allocation (see bvn_lex_init) carved into three regions, so only str_data is
 * freed; the other two pointers just alias into it and are nulled.
 */
void bvn_lex_destroy(bvnr_lexer_t* l)
{
	if (!l)
		return;
	free(l->arr_frames);
	l->arr_frames = NULL;
	free(l->str_data);
	l->str_data         = NULL;
	l->type_data        = NULL;
	l->inline_unit_data = NULL;
}
/*
 * Initialise a lexer: copy caller limits, falling back to the compiled-in
 * defaults for any left at zero, then allocate the array-frame stack and the
 * scratch buffers. The three scratch buffers are deliberately carved from one
 * malloc to save two allocations and keep them in one cache-friendly block;
 * bvn_lex_destroy relies on this layout. An optional debug sink tees the raw
 * bytes consumed, used by the canonicaliser/observer tooling.
 */
bool bvn_lex_init(bvnr_lexer_t* l, const bvnr_source_t* src,
	const bvnr_sink_t* dbg_sink, bvnr_read_flags_t* opts)
{
	bvn_init_run_luts();
	memset(l, 0, sizeof(*l));
	l->next_state  = undefined;
	l->line        = 1;
	if (src)
		l->src = *src;
	if (dbg_sink && bvn_sink_impl_c(dbg_sink)->push) {
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
	uint8_t* bufs = (uint8_t*)malloc(BVN_STR_BUF_CAP + BVN_TYPE_BUF_CAP +
	                                 BVN_INLINE_UNIT_BUF_CAP);
	if (!bufs) {
		free(l->arr_frames);
		l->arr_frames = NULL;
		return false;
	}
	l->str_data         = bufs;
	l->type_data        = bufs + BVN_STR_BUF_CAP;
	l->inline_unit_data = bufs + BVN_STR_BUF_CAP + BVN_TYPE_BUF_CAP;
	return true;
}
/*
 * Top-level read loop: pull buffer, lex it, repeat until EOF.
 *
 * Two source modes are handled. For an in-memory source the lexer walks the
 * caller's buffer directly in <=1 MiB windows (no copy into stack_data) — the
 * zero-copy fast path that motivated exposing bvn_pull_mem. For an fd source
 * it reads into a stack buffer. At EOF the next_state is inspected to decide
 * whether the stream ended cleanly (only legal at a top-level value boundary
 * with no open structs) or is truncated/incomplete; mid-resync EOF is also a
 * truncation. The octet-stream intro is the one place the text loop yields to
 * the binary reader and then resumes at the returned offset. stream_start /
 * stream_end events bracket the whole run for the validator.
 */
#define BVN_MEM_CHUNK_MAX (1u << 20)
bool bvn_lex_run(bvnr_reader_t* r)
{
	bvnr_lexer_t* l = &r->lex;
	uint8_t stack_data[BOVN_READ_BUFFER_SIZE];
	const uint8_t* data;
	uint32_t nb, off, consumed;
	uint32_t os_leftover;
	bvn_source_impl_t* src_impl = bvn_source_impl(&l->src);
	bool is_mem_src = (src_impl->pull == bvn_pull_mem);
	if (!bvn_val_receive_event(r, ev_stream_start))
		return false;
	for (;;) {
		if (is_mem_src) {
			uint64_t avail = src_impl->mem_left;
			if (!avail) {
				nb = 0;
				data = NULL;
			} else {
				nb = (avail > (uint64_t)BVN_MEM_CHUNK_MAX)
					? BVN_MEM_CHUNK_MAX : (uint32_t)avail;
				data = src_impl->mem_ptr;
				src_impl->mem_ptr  += nb;
				src_impl->mem_left -= nb;
			}
		} else if (!bvn_source_pull(&l->src, stack_data,
		                            BOVN_READ_BUFFER_SIZE, &nb)) {
			bvn_set_eof_error(r, error_reading_from_source_fd);
			bvn_notify_error(r);
			return false;
		} else {
			data = stack_data;
		}
		if (!nb) {
			if ((l->next_state == value_outro ||
				 l->next_state == undefined   ||
				 l->next_state == first_bom   ||
				 l->next_state == first_comment_intro) &&
				!l->struct_nesting_level) {
				/* first_comment_intro: a document that is nothing but a
				 * leading comment terminated by EOF (no trailing newline)
				 * is a valid empty document — the EBNF comment production
				 * ends on CR | LF | EOF. Without this a comment-only file
				 * missing its final newline wrongly reported
				 * error_got_incomplete_bvnr_stream. */
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
				bvn_set_eof_error(r, error_got_incomplete_bvnr_stream);
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
			if (l->next_state == octet_stream_intro) {
				if (!bvn_read_octet_stream(r,
					data + off, nb - off, &os_leftover))
					return false;
				off = nb - os_leftover;
			}
		}
	}
	if (!bvn_val_receive_event(r, ev_stream_end))
		return false;
	return true;
}
