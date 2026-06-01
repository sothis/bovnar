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

#include "bvn_lexer_impl.h"
/*
 * ===========================================================================
 * Lexer transition tables (the "grammar as data")
 * ===========================================================================
 *
 * This file is pure data backing the table-driven lexer in bovnar_lexer.c. It
 * is kept separate from the engine so the grammar can be reviewed and modified
 * as tables without touching the interpreter, and so the large rodata lives in
 * its own translation unit.
 *
 *   bvn_after_state_idx_table[state][byte]  -> ACT_* action index
 *   bvn_action_table[ACT_*]                 -> handler function pointer
 *   bvn_action_target_state[ACT_*]          -> next state for pure transitions
 *   bvn_kw_advance_state[state]             -> next state for keyword matching
 *
 * The BVN_EACH_256 / BVN_WHITESPACE / BVN_UTF8_* / BVN_REJECT_ASCII_CTRL
 * macros are designated-initialiser helpers that fill whole byte ranges of a
 * row at once: e.g. one macro marks every UTF-8 continuation byte, another
 * rejects every disallowed ASCII control byte. This keeps each state's row
 * declarative — you list the bytes that matter and a macro supplies the
 * uniform default for the rest — and guarantees all 256 columns are defined.
 * An action index of 0 (ACT_NONE) means "byte illegal in this state" and is
 * what the engine turns into error_unexpected_input_byte.
 */
#define BVN_EACH_256(a) \
	[0x00]=(a),[0x01]=(a),[0x02]=(a),[0x03]=(a), \
	[0x04]=(a),[0x05]=(a),[0x06]=(a),[0x07]=(a), \
	[0x08]=(a),[0x09]=(a),[0x0a]=(a),[0x0b]=(a), \
	[0x0c]=(a),[0x0d]=(a),[0x0e]=(a),[0x0f]=(a), \
	[0x10]=(a),[0x11]=(a),[0x12]=(a),[0x13]=(a), \
	[0x14]=(a),[0x15]=(a),[0x16]=(a),[0x17]=(a), \
	[0x18]=(a),[0x19]=(a),[0x1a]=(a),[0x1b]=(a), \
	[0x1c]=(a),[0x1d]=(a),[0x1e]=(a),[0x1f]=(a), \
	[0x20]=(a),[0x21]=(a),[0x22]=(a),[0x23]=(a), \
	[0x24]=(a),[0x25]=(a),[0x26]=(a),[0x27]=(a), \
	[0x28]=(a),[0x29]=(a),[0x2a]=(a),[0x2b]=(a), \
	[0x2c]=(a),[0x2d]=(a),[0x2e]=(a),[0x2f]=(a), \
	[0x30]=(a),[0x31]=(a),[0x32]=(a),[0x33]=(a), \
	[0x34]=(a),[0x35]=(a),[0x36]=(a),[0x37]=(a), \
	[0x38]=(a),[0x39]=(a),[0x3a]=(a),[0x3b]=(a), \
	[0x3c]=(a),[0x3d]=(a),[0x3e]=(a),[0x3f]=(a), \
	[0x40]=(a),[0x41]=(a),[0x42]=(a),[0x43]=(a), \
	[0x44]=(a),[0x45]=(a),[0x46]=(a),[0x47]=(a), \
	[0x48]=(a),[0x49]=(a),[0x4a]=(a),[0x4b]=(a), \
	[0x4c]=(a),[0x4d]=(a),[0x4e]=(a),[0x4f]=(a), \
	[0x50]=(a),[0x51]=(a),[0x52]=(a),[0x53]=(a), \
	[0x54]=(a),[0x55]=(a),[0x56]=(a),[0x57]=(a), \
	[0x58]=(a),[0x59]=(a),[0x5a]=(a),[0x5b]=(a), \
	[0x5c]=(a),[0x5d]=(a),[0x5e]=(a),[0x5f]=(a), \
	[0x60]=(a),[0x61]=(a),[0x62]=(a),[0x63]=(a), \
	[0x64]=(a),[0x65]=(a),[0x66]=(a),[0x67]=(a), \
	[0x68]=(a),[0x69]=(a),[0x6a]=(a),[0x6b]=(a), \
	[0x6c]=(a),[0x6d]=(a),[0x6e]=(a),[0x6f]=(a), \
	[0x70]=(a),[0x71]=(a),[0x72]=(a),[0x73]=(a), \
	[0x74]=(a),[0x75]=(a),[0x76]=(a),[0x77]=(a), \
	[0x78]=(a),[0x79]=(a),[0x7a]=(a),[0x7b]=(a), \
	[0x7c]=(a),[0x7d]=(a),[0x7e]=(a),[0x7f]=(a), \
	[0x80]=(a),[0x81]=(a),[0x82]=(a),[0x83]=(a), \
	[0x84]=(a),[0x85]=(a),[0x86]=(a),[0x87]=(a), \
	[0x88]=(a),[0x89]=(a),[0x8a]=(a),[0x8b]=(a), \
	[0x8c]=(a),[0x8d]=(a),[0x8e]=(a),[0x8f]=(a), \
	[0x90]=(a),[0x91]=(a),[0x92]=(a),[0x93]=(a), \
	[0x94]=(a),[0x95]=(a),[0x96]=(a),[0x97]=(a), \
	[0x98]=(a),[0x99]=(a),[0x9a]=(a),[0x9b]=(a), \
	[0x9c]=(a),[0x9d]=(a),[0x9e]=(a),[0x9f]=(a), \
	[0xa0]=(a),[0xa1]=(a),[0xa2]=(a),[0xa3]=(a), \
	[0xa4]=(a),[0xa5]=(a),[0xa6]=(a),[0xa7]=(a), \
	[0xa8]=(a),[0xa9]=(a),[0xaa]=(a),[0xab]=(a), \
	[0xac]=(a),[0xad]=(a),[0xae]=(a),[0xaf]=(a), \
	[0xb0]=(a),[0xb1]=(a),[0xb2]=(a),[0xb3]=(a), \
	[0xb4]=(a),[0xb5]=(a),[0xb6]=(a),[0xb7]=(a), \
	[0xb8]=(a),[0xb9]=(a),[0xba]=(a),[0xbb]=(a), \
	[0xbc]=(a),[0xbd]=(a),[0xbe]=(a),[0xbf]=(a), \
	[0xc0]=(a),[0xc1]=(a),[0xc2]=(a),[0xc3]=(a), \
	[0xc4]=(a),[0xc5]=(a),[0xc6]=(a),[0xc7]=(a), \
	[0xc8]=(a),[0xc9]=(a),[0xca]=(a),[0xcb]=(a), \
	[0xcc]=(a),[0xcd]=(a),[0xce]=(a),[0xcf]=(a), \
	[0xd0]=(a),[0xd1]=(a),[0xd2]=(a),[0xd3]=(a), \
	[0xd4]=(a),[0xd5]=(a),[0xd6]=(a),[0xd7]=(a), \
	[0xd8]=(a),[0xd9]=(a),[0xda]=(a),[0xdb]=(a), \
	[0xdc]=(a),[0xdd]=(a),[0xde]=(a),[0xdf]=(a), \
	[0xe0]=(a),[0xe1]=(a),[0xe2]=(a),[0xe3]=(a), \
	[0xe4]=(a),[0xe5]=(a),[0xe6]=(a),[0xe7]=(a), \
	[0xe8]=(a),[0xe9]=(a),[0xea]=(a),[0xeb]=(a), \
	[0xec]=(a),[0xed]=(a),[0xee]=(a),[0xef]=(a), \
	[0xf0]=(a),[0xf1]=(a),[0xf2]=(a),[0xf3]=(a), \
	[0xf4]=(a),[0xf5]=(a),[0xf6]=(a),[0xf7]=(a), \
	[0xf8]=(a),[0xf9]=(a),[0xfa]=(a),[0xfb]=(a), \
	[0xfc]=(a),[0xfd]=(a),[0xfe]=(a),[0xff]=(a)
#define BVN_WHITESPACE(a) \
	[0x09]=(a),[0x0a]=(a),[0x0b]=(a), \
	[0x0c]=(a),[0x0d]=(a),[0x20]=(a)
#define BVN_DIGITS(a) \
	[0x30]=(a),[0x31]=(a),[0x32]=(a),[0x33]=(a),[0x34]=(a), \
	[0x35]=(a),[0x36]=(a),[0x37]=(a),[0x38]=(a),[0x39]=(a)
#define BVN_ALPHA_LOWER(a) \
	[0x61]=(a),[0x62]=(a),[0x63]=(a),[0x64]=(a),[0x65]=(a), \
	[0x66]=(a),[0x67]=(a),[0x68]=(a),[0x69]=(a),[0x6a]=(a), \
	[0x6b]=(a),[0x6c]=(a),[0x6d]=(a),[0x6e]=(a),[0x6f]=(a), \
	[0x70]=(a),[0x71]=(a),[0x72]=(a),[0x73]=(a),[0x74]=(a), \
	[0x75]=(a),[0x76]=(a),[0x77]=(a),[0x78]=(a),[0x79]=(a), \
	[0x7a]=(a)
#define BVN_ALPHA_UPPER(a) \
	[0x41]=(a),[0x42]=(a),[0x43]=(a),[0x44]=(a),[0x45]=(a), \
	[0x46]=(a),[0x47]=(a),[0x48]=(a),[0x49]=(a),[0x4a]=(a), \
	[0x4b]=(a),[0x4c]=(a),[0x4d]=(a),[0x4e]=(a),[0x4f]=(a), \
	[0x50]=(a),[0x51]=(a),[0x52]=(a),[0x53]=(a),[0x54]=(a), \
	[0x55]=(a),[0x56]=(a),[0x57]=(a),[0x58]=(a),[0x59]=(a), \
	[0x5a]=(a)
#define BVN_UTF8_LEADER(a) \
	[0xc2]=(a),[0xc3]=(a),[0xc4]=(a),[0xc5]=(a),[0xc6]=(a),[0xc7]=(a), \
	[0xc8]=(a),[0xc9]=(a),[0xca]=(a),[0xcb]=(a),[0xcc]=(a),[0xcd]=(a), \
	[0xce]=(a),[0xcf]=(a),[0xd0]=(a),[0xd1]=(a),[0xd2]=(a),[0xd3]=(a), \
	[0xd4]=(a),[0xd5]=(a),[0xd6]=(a),[0xd7]=(a),[0xd8]=(a),[0xd9]=(a), \
	[0xda]=(a),[0xdb]=(a),[0xdc]=(a),[0xdd]=(a),[0xde]=(a),[0xdf]=(a), \
	[0xe0]=(a),[0xe1]=(a),[0xe2]=(a),[0xe3]=(a),[0xe4]=(a),[0xe5]=(a), \
	[0xe6]=(a),[0xe7]=(a),[0xe8]=(a),[0xe9]=(a),[0xea]=(a),[0xeb]=(a), \
	[0xec]=(a),[0xed]=(a),[0xee]=(a),[0xef]=(a),[0xf0]=(a),[0xf1]=(a), \
	[0xf2]=(a),[0xf3]=(a),[0xf4]=(a)
#define BVN_UTF8_CONTINUATION(a) \
	[0x80]=(a),[0x81]=(a),[0x82]=(a),[0x83]=(a), \
	[0x84]=(a),[0x85]=(a),[0x86]=(a),[0x87]=(a), \
	[0x88]=(a),[0x89]=(a),[0x8a]=(a),[0x8b]=(a), \
	[0x8c]=(a),[0x8d]=(a),[0x8e]=(a),[0x8f]=(a), \
	[0x90]=(a),[0x91]=(a),[0x92]=(a),[0x93]=(a), \
	[0x94]=(a),[0x95]=(a),[0x96]=(a),[0x97]=(a), \
	[0x98]=(a),[0x99]=(a),[0x9a]=(a),[0x9b]=(a), \
	[0x9c]=(a),[0x9d]=(a),[0x9e]=(a),[0x9f]=(a), \
	[0xa0]=(a),[0xa1]=(a),[0xa2]=(a),[0xa3]=(a), \
	[0xa4]=(a),[0xa5]=(a),[0xa6]=(a),[0xa7]=(a), \
	[0xa8]=(a),[0xa9]=(a),[0xaa]=(a),[0xab]=(a), \
	[0xac]=(a),[0xad]=(a),[0xae]=(a),[0xaf]=(a), \
	[0xb0]=(a),[0xb1]=(a),[0xb2]=(a),[0xb3]=(a), \
	[0xb4]=(a),[0xb5]=(a),[0xb6]=(a),[0xb7]=(a), \
	[0xb8]=(a),[0xb9]=(a),[0xba]=(a),[0xbb]=(a), \
	[0xbc]=(a),[0xbd]=(a),[0xbe]=(a),[0xbf]=(a)
#define BVN_REJECT_ASCII_CTRL \
	[0x00]=ACT_NONE,[0x01]=ACT_NONE,[0x02]=ACT_NONE,[0x03]=ACT_NONE, \
	[0x04]=ACT_NONE,[0x05]=ACT_NONE,[0x06]=ACT_NONE,[0x07]=ACT_NONE, \
	[0x08]=ACT_NONE,[0x0e]=ACT_NONE,[0x0f]=ACT_NONE,[0x10]=ACT_NONE, \
	[0x11]=ACT_NONE,[0x12]=ACT_NONE,[0x13]=ACT_NONE,[0x14]=ACT_NONE, \
	[0x15]=ACT_NONE,[0x16]=ACT_NONE,[0x17]=ACT_NONE,[0x18]=ACT_NONE, \
	[0x19]=ACT_NONE,[0x1a]=ACT_NONE,[0x1b]=ACT_NONE,[0x1c]=ACT_NONE, \
	[0x1d]=ACT_NONE,[0x1e]=ACT_NONE,[0x1f]=ACT_NONE,[0x7f]=ACT_NONE
const uint8_t bvn_after_state_idx_table[dimension_state][256] = {
	[undefined] = {
		BVN_WHITESPACE(ACT_ignore_whitespace),
		[0x23] = ACT_first_comment_intro,
		[0x2e] = ACT_identifier_intro,
		[0xef] = ACT_copy_utf8bom_byte,
	},
	[first_bom] = {
		BVN_WHITESPACE(ACT_ignore_whitespace),
		[0x23] = ACT_first_comment_intro,
		[0x2e] = ACT_identifier_intro,
	},
	[utf8bom_intro] = {
		BVN_EACH_256(ACT_copy_utf8bom_byte),
	},
	[comment_intro] = {
		BVN_EACH_256(ACT_ignore_comment_byte),
		BVN_REJECT_ASCII_CTRL,
		[0x0a] = ACT_comment_outro,
		[0x0d] = ACT_comment_outro,
	},
	[ignore_comment_byte] = {
		BVN_EACH_256(ACT_ignore_comment_byte),
		BVN_REJECT_ASCII_CTRL,
		[0x0a] = ACT_comment_outro,
		[0x0d] = ACT_comment_outro,
	},
	[comment_outro] = { 0 },
	[first_comment_intro] = {
		BVN_EACH_256(ACT_first_comment_byte),
		BVN_REJECT_ASCII_CTRL,
		[0x0a] = ACT_first_comment_outro,
		[0x0d] = ACT_first_comment_outro,
	},
	[first_comment_after_ef] = {
		BVN_EACH_256(ACT_first_comment_after_ef),
		BVN_REJECT_ASCII_CTRL,
		[0x0a] = ACT_first_comment_outro,
		[0x0d] = ACT_first_comment_outro,
	},
	[first_comment_after_ef_bb] = {
		BVN_EACH_256(ACT_first_comment_after_ef_bb),
		BVN_REJECT_ASCII_CTRL,
		[0x0a] = ACT_first_comment_outro,
		[0x0d] = ACT_first_comment_outro,
	},
	[identifier_intro] = {
		BVN_ALPHA_UPPER(ACT_copy_identifier_byte),
		BVN_ALPHA_LOWER(ACT_copy_identifier_byte),
		[0x5f] = ACT_copy_identifier_byte,
		BVN_UTF8_LEADER(ACT_copy_identifier_byte),
		[0xc2] = ACT_NONE,
	},
	[identifier_body] = {
		BVN_EACH_256(ACT_copy_identifier_byte),
		BVN_REJECT_ASCII_CTRL,
		BVN_WHITESPACE(ACT_to_identifier_outro),
		[0x22] = ACT_NONE, [0x23] = ACT_NONE, [0x2c] = ACT_NONE,
		[0x2e] = ACT_NONE, [0x2f] = ACT_NONE, [0x3b] = ACT_NONE,
		[0x3c] = ACT_NONE, [0x3d] = ACT_value_intro,
		[0x3e] = ACT_NONE, [0x5b] = ACT_NONE, [0x5d] = ACT_NONE,
		[0x7b] = ACT_NONE, [0x7d] = ACT_NONE, [0x21] = ACT_NONE,
		[0x24] = ACT_NONE, [0x25] = ACT_NONE, [0x26] = ACT_NONE,
		[0x27] = ACT_NONE, [0x28] = ACT_NONE, [0x29] = ACT_NONE,
		[0x2a] = ACT_NONE, [0x2b] = ACT_copy_identifier_byte,
		[0x2d] = ACT_copy_identifier_byte, [0x3a] = ACT_NONE,
		[0x3f] = ACT_NONE, [0x40] = ACT_NONE, [0x5c] = ACT_NONE,
		[0x5e] = ACT_NONE, [0x60] = ACT_NONE, [0x7c] = ACT_NONE,
		[0x7e] = ACT_NONE, [0xc2] = ACT_NONE,
	},
	[identifier_outro] = {
		BVN_WHITESPACE(ACT_ignore_whitespace),
		[0x23] = ACT_comment_intro,
		[0x3d] = ACT_value_intro,
	},
	[type_intro] = {
		BVN_WHITESPACE(ACT_ignore_whitespace),
		[0x23] = ACT_comment_intro,
		['u'] = ACT_tf_to_u,
		['s'] = ACT_tf_to_s,
		['f'] = ACT_tf_to_f,
		['b'] = ACT_tf_to_b,
	},
	[type_noparams] = {
		BVN_WHITESPACE(ACT_ignore_whitespace),
		[0x23] = ACT_comment_intro,
		[0x3e] = ACT_type_outro,
	},
	[tf_u] = { ['i'] = ACT_tf_u_to_ui, ['t'] = ACT_tf_u_to_ut },
	[tf_ui]  = { ['n'] = ACT_kw_advance },
	[tf_uin] = { ['t'] = ACT_tf_uint_done },
	[tf_ut]  = { ['f'] = ACT_kw_advance },
	[tf_utf] = { ['8'] = ACT_tf_utf8_done },
	[tf_s]   = { ['i'] = ACT_kw_advance },
	[tf_si]  = { ['n'] = ACT_kw_advance },
	[tf_sin] = { ['t'] = ACT_tf_sint_done },
	[tf_f]    = { ['l'] = ACT_kw_advance },
	[tf_fl]   = { ['o'] = ACT_kw_advance },
	[tf_flo]  = { ['a'] = ACT_kw_advance },
	[tf_floa] = { ['t'] = ACT_tf_float_done },
	[tf_b]    = { ['o'] = ACT_kw_advance },
	[tf_bo]   = { ['o'] = ACT_kw_advance },
	[tf_boo]  = { ['l'] = ACT_tf_bool_done },
	[copy_type_byte] = {
		BVN_WHITESPACE(ACT_to_type_body_outro),
		[0x24] = ACT_copy_type_byte,
		[0x25] = ACT_copy_type_byte,
		[0x28] = ACT_copy_type_byte,
		[0x29] = ACT_copy_type_byte,
		[0x2a] = ACT_copy_type_byte,
		[0x2b] = ACT_copy_type_byte,
		[0x2c] = ACT_copy_type_byte,
		[0x2d] = ACT_copy_type_byte,
		[0x2e] = ACT_copy_type_byte,
		[0x2f] = ACT_copy_type_byte,
		BVN_DIGITS(ACT_copy_type_byte),
		[0x3a] = ACT_copy_type_byte,
		[0x3e] = ACT_type_outro,
		BVN_ALPHA_UPPER(ACT_copy_type_byte),
		[0x5e] = ACT_copy_type_byte,
		[0x5f] = ACT_copy_type_byte,
		[0x7e] = ACT_copy_type_byte,
		BVN_ALPHA_LOWER(ACT_copy_type_byte),
		BVN_UTF8_CONTINUATION(ACT_copy_type_byte),
		BVN_UTF8_LEADER(ACT_copy_type_byte),
	},
	[type_body_outro] = {
		BVN_WHITESPACE(ACT_ignore_whitespace),
		[0x23] = ACT_comment_intro,
		[0x24] = ACT_copy_type_byte,
		[0x25] = ACT_copy_type_byte,
		[0x28] = ACT_copy_type_byte,
		[0x29] = ACT_copy_type_byte,
		[0x2a] = ACT_copy_type_byte,
		[0x2b] = ACT_copy_type_byte,
		[0x2c] = ACT_copy_type_byte,
		[0x2d] = ACT_copy_type_byte,
		[0x2e] = ACT_copy_type_byte,
		[0x2f] = ACT_copy_type_byte,
		BVN_DIGITS(ACT_copy_type_byte),
		[0x3a] = ACT_copy_type_byte,
		[0x3e] = ACT_type_outro,
		BVN_ALPHA_UPPER(ACT_copy_type_byte),
		[0x5e] = ACT_copy_type_byte,
		[0x5f] = ACT_copy_type_byte,
		[0x7e] = ACT_copy_type_byte,
		BVN_ALPHA_LOWER(ACT_copy_type_byte),
		BVN_UTF8_CONTINUATION(ACT_copy_type_byte),
		BVN_UTF8_LEADER(ACT_copy_type_byte),
	},
	[type_outro] = {
		BVN_WHITESPACE(ACT_ignore_whitespace),
		[0x00] = ACT_octet_stream_intro,
		[0x22] = ACT_string_intro,
		[0x23] = ACT_comment_intro,
		[0x24] = ACT_special_number_intro,
		[0x26] = ACT_reference_intro,
		[0x2c] = ACT_type_null_then_new_array_value,
		[0x2d] = ACT_neg_number_intro,
		[0x2e] = ACT_fraction_no_int,
		BVN_DIGITS(ACT_copy_number_byte),
		[0x30] = ACT_zero_intro,
		[0x3b] = ACT_type_null_then_value_outro,
		[0x5b] = ACT_array_intro,
		[0x5d] = ACT_type_null_then_array_outro,
		[0x5f] = ACT_symbol_intro,
		[0x7b] = ACT_struct_intro,
		BVN_ALPHA_UPPER(ACT_symbol_intro),
		BVN_ALPHA_LOWER(ACT_symbol_intro),
		BVN_UTF8_LEADER(ACT_symbol_intro),
		[0xc2] = ACT_NONE,
	},
	[value_intro] = {
		BVN_WHITESPACE(ACT_ignore_whitespace),
		[0x00] = ACT_octet_stream_intro,
		[0x22] = ACT_string_intro,
		[0x23] = ACT_comment_intro,
		[0x24] = ACT_special_number_intro,
		[0x26] = ACT_reference_intro,
		[0x2d] = ACT_neg_number_intro,
		[0x2e] = ACT_fraction_no_int,
		BVN_DIGITS(ACT_copy_number_byte),
		[0x30] = ACT_zero_intro,
		[0x3b] = ACT_value_outro,
		[0x3c] = ACT_type_intro,
		BVN_ALPHA_UPPER(ACT_symbol_intro),
		[0x5b] = ACT_array_intro,
		[0x5f] = ACT_symbol_intro,
		BVN_ALPHA_LOWER(ACT_symbol_intro),
		[0x7b] = ACT_struct_intro,
		BVN_UTF8_LEADER(ACT_symbol_intro),
		[0xc2] = ACT_NONE,
	},
	[array_intro] = {
		BVN_WHITESPACE(ACT_ignore_whitespace),
		[0x00] = ACT_octet_stream_intro,
		[0x22] = ACT_arr_string_intro,
		[0x23] = ACT_comment_intro,
		[0x24] = ACT_arr_special_number_intro,
		[0x26] = ACT_reference_intro,
		[0x2c] = ACT_new_array_value,
		[0x2d] = ACT_neg_number_intro,
		[0x2e] = ACT_fraction_no_int,
		BVN_DIGITS(ACT_copy_number_byte),
		[0x30] = ACT_zero_intro,
		[0x3c] = ACT_type_intro,
		[0x5b] = ACT_array_intro,
		/* "]" straight after "[" (only ws/comments between) = empty array. */
		[0x5d] = ACT_array_outro_empty,
		[0x5f] = ACT_symbol_intro,
		[0x7b] = ACT_struct_intro,
		BVN_ALPHA_UPPER(ACT_symbol_intro),
		BVN_ALPHA_LOWER(ACT_symbol_intro),
		BVN_UTF8_LEADER(ACT_symbol_intro),
		[0xc2] = ACT_NONE,
	},
	[new_array_value] = {
		BVN_WHITESPACE(ACT_ignore_whitespace),
		[0x00] = ACT_octet_stream_intro,
		[0x22] = ACT_arr_string_intro,
		[0x23] = ACT_comment_intro,
		[0x24] = ACT_arr_special_number_intro,
		[0x26] = ACT_reference_intro,
		[0x2c] = ACT_new_array_value,
		[0x2d] = ACT_neg_number_intro,
		[0x2e] = ACT_fraction_no_int,
		BVN_DIGITS(ACT_copy_number_byte),
		[0x30] = ACT_zero_intro,
		[0x3c] = ACT_type_intro,
		[0x5b] = ACT_array_intro,
		[0x5d] = ACT_array_outro,
		[0x5f] = ACT_symbol_intro,
		[0x7b] = ACT_struct_intro,
		BVN_ALPHA_UPPER(ACT_symbol_intro),
		BVN_ALPHA_LOWER(ACT_symbol_intro),
		BVN_UTF8_LEADER(ACT_symbol_intro),
		[0xc2] = ACT_NONE,
	},
	[array_outro] = {
		BVN_WHITESPACE(ACT_ignore_whitespace),
		[0x23] = ACT_comment_intro,
		[0x2c] = ACT_new_array_value,
		[0x2f] = ACT_array_dim_sep,
		[0x3b] = ACT_value_outro,
		[0x5d] = ACT_array_outro,
	},
	[array_dim_sep] = {
		BVN_WHITESPACE(ACT_ignore_whitespace),
		[0x23] = ACT_comment_intro,
		[0x5b] = ACT_array_intro,
	},
	[neg_number_intro] = {
		[0x2e] = ACT_fraction_no_int,
		BVN_DIGITS(ACT_copy_number_byte),
		[0x30] = ACT_zero_intro,
	},
	[zero_intro] = {
		BVN_WHITESPACE(ACT_to_number_outro),
		[0x2e] = ACT_fraction_intro,
		BVN_DIGITS(ACT_copy_number_byte),
		[0x2c] = ACT_new_array_value,
		[0x3b] = ACT_value_outro,
		[0x5d] = ACT_array_outro,
		[0x45] = ACT_exp_intro,
		[0x65] = ACT_exp_intro,
	},
	[copy_number_byte] = {
		BVN_WHITESPACE(ACT_to_number_outro),
		[0x2e] = ACT_fraction_intro,
		BVN_DIGITS(ACT_copy_number_byte),
		[0x2c] = ACT_new_array_value,
		[0x3b] = ACT_value_outro,
		[0x5d] = ACT_array_outro,
		[0x45] = ACT_exp_intro,
		[0x65] = ACT_exp_intro,
	},
	[fraction_intro] = {
		BVN_WHITESPACE(ACT_to_number_outro),
		BVN_DIGITS(ACT_copy_fraction_byte),
		[0x2c] = ACT_new_array_value,
		[0x3b] = ACT_value_outro,
		[0x5d] = ACT_array_outro,
		[0x45] = ACT_exp_intro,
		[0x65] = ACT_exp_intro,
	},
	[fraction_no_int] = {
		BVN_DIGITS(ACT_copy_fraction_byte),
	},
	[copy_fraction_byte] = {
		BVN_WHITESPACE(ACT_to_number_outro),
		BVN_DIGITS(ACT_copy_fraction_byte),
		[0x2c] = ACT_new_array_value,
		[0x3b] = ACT_value_outro,
		[0x5d] = ACT_array_outro,
		[0x45] = ACT_exp_intro,
		[0x65] = ACT_exp_intro,
	},
	[number_outro] = {
		BVN_WHITESPACE(ACT_ignore_whitespace),
		[0x23] = ACT_comment_intro,
		[0x2c] = ACT_new_array_value,
		[0x3b] = ACT_value_outro,
		[0x5d] = ACT_array_outro,
		[0x24] = ACT_inline_unit_intro,
		[0x25] = ACT_inline_unit_intro,
		BVN_ALPHA_UPPER(ACT_inline_unit_intro),
		BVN_ALPHA_LOWER(ACT_inline_unit_intro),
		[0x5f] = ACT_inline_unit_intro,
		BVN_UTF8_LEADER(ACT_inline_unit_intro),
	},
	[number_outro_nosp] = {
		BVN_WHITESPACE(ACT_to_number_outro),
		[0x23] = ACT_comment_intro,
		[0x2c] = ACT_new_array_value,
		[0x3b] = ACT_value_outro,
		[0x5d] = ACT_array_outro,
	},
	[exp_intro] = {
		[0x2b] = ACT_exp_sign_intro,
		[0x2d] = ACT_exp_sign_intro,
		BVN_DIGITS(ACT_copy_exp_byte),
	},
	[exp_sign_intro] = {
		BVN_DIGITS(ACT_copy_exp_byte),
	},
	[copy_exp_byte] = {
		BVN_WHITESPACE(ACT_to_number_outro),
		BVN_DIGITS(ACT_copy_exp_byte),
		[0x2c] = ACT_new_array_value,
		[0x3b] = ACT_value_outro,
		[0x5d] = ACT_array_outro,
	},
	[sp_start] = {
		['n'] = ACT_sp_to_nan1,
		['i'] = ACT_sp_to_inf1,
		['-'] = ACT_sp_to_neg1,
	},
	[sp_nan1] = { ['a'] = ACT_kw_advance },
	[sp_nan2] = { ['n'] = ACT_sp_nan_outro },
	[sp_inf1] = { ['n'] = ACT_kw_advance },
	[sp_inf2] = { ['f'] = ACT_sp_inf_outro },
	[sp_neg1] = { ['i'] = ACT_kw_advance },
	[sp_neg2] = { ['n'] = ACT_kw_advance },
	[sp_neg3] = { ['f'] = ACT_sp_neginf_outro },
	[symbol_body] = {
		BVN_EACH_256(ACT_copy_symbol_byte),
		BVN_REJECT_ASCII_CTRL,
		BVN_WHITESPACE(ACT_to_symbol_outro),
		[0x22] = ACT_NONE, [0x23] = ACT_NONE, [0x2c] = ACT_new_array_value,
		[0x2e] = ACT_NONE, [0x2f] = ACT_NONE, [0x3b] = ACT_value_outro,
		[0x3c] = ACT_NONE, [0x3d] = ACT_NONE, [0x3e] = ACT_NONE,
		[0x5b] = ACT_NONE, [0x5d] = ACT_array_outro, [0x7b] = ACT_NONE,
		[0x7d] = ACT_NONE, [0x21] = ACT_NONE, [0x24] = ACT_NONE,
		[0x25] = ACT_NONE, [0x26] = ACT_NONE, [0x27] = ACT_NONE,
		[0x28] = ACT_NONE, [0x29] = ACT_NONE, [0x2a] = ACT_NONE,
		[0x2b] = ACT_copy_symbol_byte, [0x2d] = ACT_copy_symbol_byte,
		[0x3a] = ACT_NONE, [0x3f] = ACT_NONE, [0x40] = ACT_NONE,
		[0x5c] = ACT_NONE, [0x5e] = ACT_NONE, [0x60] = ACT_NONE,
		[0x7c] = ACT_NONE, [0x7e] = ACT_NONE, [0xc2] = ACT_NONE,
	},
	[symbol_outro] = {
		BVN_WHITESPACE(ACT_ignore_whitespace),
		[0x23] = ACT_comment_intro,
		[0x2c] = ACT_new_array_value,
		[0x3b] = ACT_value_outro,
		[0x5d] = ACT_array_outro,
	},
	[reference_intro] = {
		[0x2e] = ACT_copy_reference_dot,
	},
	[reference_segment_intro] = {
		BVN_ALPHA_UPPER(ACT_copy_reference_byte),
		BVN_ALPHA_LOWER(ACT_copy_reference_byte),
		[0x5f] = ACT_copy_reference_byte,
		BVN_UTF8_LEADER(ACT_copy_reference_byte),
		[0xc2] = ACT_NONE,
	},
	[reference_segment_body] = {
		BVN_EACH_256(ACT_copy_reference_byte),
		BVN_REJECT_ASCII_CTRL,
		BVN_WHITESPACE(ACT_to_reference_outro),
		[0x22] = ACT_NONE, [0x23] = ACT_NONE, [0x2c] = ACT_new_array_value,
		[0x2e] = ACT_copy_reference_dot, [0x2f] = ACT_NONE,
		[0x3b] = ACT_value_outro, [0x3c] = ACT_NONE,
		[0x3d] = ACT_NONE, [0x3e] = ACT_NONE,
		[0x5b] = ACT_NONE, [0x5d] = ACT_array_outro,
		[0x7b] = ACT_NONE, [0x7d] = ACT_NONE,
		[0x21] = ACT_NONE, [0x24] = ACT_NONE,
		[0x25] = ACT_NONE, [0x26] = ACT_NONE,
		[0x27] = ACT_NONE, [0x28] = ACT_NONE, [0x29] = ACT_NONE,
		[0x2a] = ACT_NONE,
		[0x2b] = ACT_copy_reference_byte,
		[0x2d] = ACT_copy_reference_byte,
		[0x3a] = ACT_NONE, [0x3f] = ACT_NONE,
		[0x40] = ACT_NONE, [0x5c] = ACT_NONE,
		[0x5e] = ACT_NONE, [0x60] = ACT_NONE,
		[0x7c] = ACT_NONE, [0x7e] = ACT_NONE,
		[0xc2] = ACT_NONE,
	},
	[reference_outro] = {
		BVN_WHITESPACE(ACT_ignore_whitespace),
		[0x23] = ACT_comment_intro,
		[0x2c] = ACT_new_array_value,
		[0x3b] = ACT_value_outro,
		[0x5d] = ACT_array_outro,
	},
	[copy_string_byte] = {
		BVN_EACH_256(ACT_copy_string_byte),
		BVN_REJECT_ASCII_CTRL,
		[0x22] = ACT_string_outro,
		[0x5c] = ACT_escape_from_copy,
	},
	[escape_from_copy] = {
		/* Every byte after a backslash is handed to bvn_decode_escape,
		 * which accepts the seven bovnar escapes (\" \\ \f \n \r \t \v)
		 * and rejects anything else as error_illegal_escape_sequence.
		 * Routing all 256 columns here (rather than leaving non-escape
		 * bytes at ACT_NONE) is what makes that error reachable instead
		 * of the generic error_unexpected_input_byte. */
		BVN_EACH_256(ACT_replace_escaped_byte),
	},
	[string_intro] = {
		BVN_EACH_256(ACT_copy_string_byte),
		BVN_REJECT_ASCII_CTRL,
		[0x22] = ACT_string_outro,
		[0x5c] = ACT_escape_from_copy,
	},
	[string_outro_nosp] = {
		BVN_WHITESPACE(ACT_to_string_outro),
		[0x22] = ACT_string_intro,
		[0x23] = ACT_comment_intro,
		[0x2c] = ACT_new_array_value,
		[0x3b] = ACT_value_outro,
		[0x5d] = ACT_array_outro,
	},
	[string_outro] = {
		BVN_WHITESPACE(ACT_ignore_whitespace),
		[0x22] = ACT_string_intro,
		[0x23] = ACT_comment_intro,
		[0x2c] = ACT_new_array_value,
		[0x3b] = ACT_value_outro,
		[0x5d] = ACT_array_outro,
		[0x24] = ACT_inline_unit_intro,
		[0x25] = ACT_inline_unit_intro,
		BVN_ALPHA_UPPER(ACT_inline_unit_intro),
		BVN_ALPHA_LOWER(ACT_inline_unit_intro),
		[0x5f] = ACT_inline_unit_intro,
		BVN_UTF8_LEADER(ACT_inline_unit_intro),
	},
	[octet_stream_intro] = { 0 },
	[inline_unit_body] = {
		BVN_ALPHA_UPPER(ACT_copy_inline_unit_byte),
		BVN_ALPHA_LOWER(ACT_copy_inline_unit_byte),
		BVN_DIGITS(ACT_copy_inline_unit_byte),
		[0x2a] = ACT_copy_inline_unit_byte,
		[0x2b] = ACT_copy_inline_unit_byte,
		[0x2d] = ACT_copy_inline_unit_byte,
		[0x2e] = ACT_copy_inline_unit_byte,
		[0x2f] = ACT_copy_inline_unit_byte,
		[0x3a] = ACT_copy_inline_unit_byte,
		[0x5e] = ACT_copy_inline_unit_byte,
		[0x5f] = ACT_copy_inline_unit_byte,
		[0x7e] = ACT_copy_inline_unit_byte,
		[0x24] = ACT_copy_inline_unit_byte,
		[0x25] = ACT_copy_inline_unit_byte,
		[0x28] = ACT_copy_inline_unit_byte,
		[0x29] = ACT_copy_inline_unit_byte,
		BVN_UTF8_CONTINUATION(ACT_copy_inline_unit_byte),
		BVN_UTF8_LEADER(ACT_copy_inline_unit_byte),
		BVN_WHITESPACE(ACT_to_inline_unit_outro),
		[0x23] = ACT_comment_intro,
		[0x3b] = ACT_value_outro,
	},
	[inline_unit_outro] = {
		BVN_WHITESPACE(ACT_ignore_whitespace),
		[0x23] = ACT_comment_intro,
		[0x3b] = ACT_value_outro,
	},
	[octet_stream_outro] = {
		BVN_WHITESPACE(ACT_ignore_whitespace),
		[0x23] = ACT_comment_intro,
		[0x2c] = ACT_new_array_value,
		[0x3b] = ACT_value_outro,
		[0x5d] = ACT_array_outro,
	},
	[struct_intro] = {
		BVN_WHITESPACE(ACT_ignore_whitespace),
		[0x23] = ACT_comment_intro,
		[0x2e] = ACT_identifier_intro,
		[0x7d] = ACT_struct_outro,
	},
	[struct_outro] = {
		BVN_WHITESPACE(ACT_ignore_whitespace),
		[0x23] = ACT_comment_intro,
		[0x2c] = ACT_new_array_value,
		[0x3b] = ACT_value_outro,
		[0x5d] = ACT_array_outro,
	},
	[value_outro] = {
		BVN_WHITESPACE(ACT_ignore_whitespace),
		[0x23] = ACT_comment_intro,
		[0x2e] = ACT_identifier_intro,
		[0x7d] = ACT_struct_outro,
	},
	[resync] = {
		BVN_EACH_256(ACT_resync_skip),
		[0x22] = ACT_resync_string_intro,
		[0x23] = ACT_resync_comment_intro,
		[0x3b] = ACT_resync_semicolon,
		[0x5b] = ACT_resync_open_bracket,
		[0x5d] = ACT_resync_close_bracket,
		[0x7b] = ACT_resync_open_bracket,
		[0x7d] = ACT_resync_close_bracket,
	},
	[resync_string] = {
		BVN_EACH_256(ACT_resync_string_byte),
		[0x22] = ACT_resync_string_outro,
		[0x5c] = ACT_resync_string_escape,
	},
	[resync_string_escape] = {
		BVN_EACH_256(ACT_resync_string_escape_byte),
	},
	[resync_comment] = {
		BVN_EACH_256(ACT_resync_comment_byte),
		[0x0a] = ACT_resync_comment_outro,
		[0x0d] = ACT_resync_comment_outro,
	},
};
#undef BVN_EACH_256
#undef BVN_WHITESPACE
#undef BVN_DIGITS
#undef BVN_ALPHA_LOWER
#undef BVN_ALPHA_UPPER
#undef BVN_UTF8_LEADER
#undef BVN_UTF8_CONTINUATION
#undef BVN_REJECT_ASCII_CTRL
const action_t bvn_action_table[ACT__count] = {
	[ACT_NONE]                       = NULL,
	[ACT_ignore_whitespace]          = bvn_action_ignore_whitespace,
	[ACT_comment_intro]              = bvn_action_comment_intro,
	[ACT_ignore_comment_byte]        = bvn_action_set_state,
	[ACT_comment_outro]              = bvn_action_comment_outro,
	[ACT_copy_utf8bom_byte]          = bvn_action_copy_utf8bom_byte,
	[ACT_identifier_intro]           = bvn_action_identifier_intro,
	[ACT_copy_identifier_byte]       = bvn_action_copy_identifier_byte,
	[ACT_to_identifier_outro]        = bvn_action_set_state,
	[ACT_value_intro]                = bvn_action_value_intro,
	[ACT_value_outro]                = bvn_action_value_outro,
	[ACT_type_intro]                 = bvn_action_type_intro,
	[ACT_type_outro]                 = bvn_action_type_outro,
	[ACT_copy_type_byte]             = bvn_action_copy_type_byte,
	[ACT_to_type_body_outro]         = bvn_action_set_state,
	[ACT_neg_number_intro]           = bvn_action_neg_number_intro,
	[ACT_copy_number_byte]           = bvn_action_copy_number_byte,
	[ACT_zero_intro]                 = bvn_action_zero_intro,
	[ACT_fraction_intro]             = bvn_action_fraction_intro,
	[ACT_fraction_no_int]            = bvn_action_fraction_no_int,
	[ACT_copy_fraction_byte]         = bvn_action_copy_fraction_byte,
	[ACT_to_number_outro]            = bvn_action_set_state,
	[ACT_special_number_intro]       = bvn_action_special_number_intro,
	[ACT_arr_special_number_intro]   = bvn_action_arr_special_number_intro,
	[ACT_kw_advance]                 = bvn_action_kw_advance,
	[ACT_sp_to_nan1]                 = bvn_action_set_state,
	[ACT_sp_to_inf1]                 = bvn_action_set_state,
	[ACT_sp_to_neg1]                 = bvn_action_set_state,
	[ACT_sp_nan_outro]               = bvn_action_sp_nan_outro,
	[ACT_sp_inf_outro]               = bvn_action_sp_inf_outro,
	[ACT_sp_neginf_outro]            = bvn_action_sp_neginf_outro,
	[ACT_tf_to_u]                    = bvn_action_set_state,
	[ACT_tf_to_s]                    = bvn_action_set_state,
	[ACT_tf_to_f]                    = bvn_action_set_state,
	[ACT_tf_to_b]                    = bvn_action_set_state,
	[ACT_tf_u_to_ui]                 = bvn_action_set_state,
	[ACT_tf_u_to_ut]                 = bvn_action_set_state,
	[ACT_tf_uint_done]               = bvn_action_tf_uint_done,
	[ACT_tf_sint_done]               = bvn_action_tf_sint_done,
	[ACT_tf_float_done]              = bvn_action_tf_float_done,
	[ACT_tf_utf8_done]               = bvn_action_tf_utf8_done,
	[ACT_tf_bool_done]               = bvn_action_tf_bool_done,
	[ACT_array_intro]                = bvn_action_array_intro,
	[ACT_array_outro]                = bvn_action_array_outro,
	[ACT_array_outro_empty]          = bvn_action_array_outro_empty,
	[ACT_new_array_value]            = bvn_action_new_array_value,
	[ACT_array_dim_sep]              = bvn_action_array_dim_sep,
	[ACT_exp_intro]                  = bvn_action_exp_intro,
	[ACT_exp_sign_intro]             = bvn_action_exp_sign_intro,
	[ACT_copy_exp_byte]              = bvn_action_copy_exp_byte,
	[ACT_string_intro]               = bvn_action_string_intro,
	[ACT_string_outro]               = bvn_action_set_state,
	[ACT_copy_string_byte]           = bvn_action_copy_string_byte,
	[ACT_replace_escaped_byte]       = bvn_action_replace_escaped_byte,
	[ACT_escape_from_copy]           = bvn_action_set_state,
	[ACT_arr_string_intro]           = bvn_action_arr_string_intro,
	[ACT_symbol_intro]               = bvn_action_symbol_intro,
	[ACT_copy_symbol_byte]           = bvn_action_copy_symbol_byte,
	[ACT_to_symbol_outro]            = bvn_action_set_state,
	[ACT_reference_intro]            = bvn_action_reference_intro,
	[ACT_copy_reference_dot]         = bvn_action_copy_reference_dot,
	[ACT_copy_reference_byte]        = bvn_action_copy_reference_byte,
	[ACT_to_reference_outro]         = bvn_action_set_state,
	[ACT_octet_stream_intro]         = bvn_action_octet_stream_intro,
	[ACT_struct_intro]               = bvn_action_struct_intro,
	[ACT_struct_outro]               = bvn_action_struct_outro,
	[ACT_first_comment_intro]        = bvn_action_first_comment_intro,
	[ACT_first_comment_byte]         = bvn_action_first_comment_byte,
	[ACT_first_comment_after_ef]     = bvn_action_first_comment_after_ef,
	[ACT_first_comment_after_ef_bb]  = bvn_action_first_comment_after_ef_bb,
	[ACT_first_comment_outro]        = bvn_action_first_comment_outro,
	[ACT_type_null_then_value_outro]     = bvn_action_type_null_then_value_outro,
	[ACT_type_null_then_new_array_value] = bvn_action_type_null_then_new_array_value,
	[ACT_type_null_then_array_outro]     = bvn_action_type_null_then_array_outro,
	[ACT_resync_skip]               = bvn_action_resync_skip,
	[ACT_resync_open_bracket]       = bvn_action_resync_open_bracket,
	[ACT_resync_close_bracket]      = bvn_action_resync_close_bracket,
	[ACT_resync_semicolon]          = bvn_action_resync_semicolon,
	[ACT_resync_string_intro]       = bvn_action_resync_string_intro,
	[ACT_resync_string_byte]        = bvn_action_resync_string_byte,
	[ACT_resync_string_escape]      = bvn_action_resync_string_escape,
	[ACT_resync_string_escape_byte] = bvn_action_resync_string_escape_byte,
	[ACT_resync_string_outro]       = bvn_action_resync_string_outro,
	[ACT_resync_comment_intro]      = bvn_action_resync_comment_intro,
	[ACT_resync_comment_byte]       = bvn_action_resync_comment_byte,
	[ACT_resync_comment_outro]      = bvn_action_resync_comment_outro,
	[ACT_inline_unit_intro]         = bvn_action_inline_unit_intro,
	[ACT_copy_inline_unit_byte]     = bvn_action_copy_inline_unit_byte,
	[ACT_to_inline_unit_outro]      = bvn_action_to_inline_unit_outro,
	[ACT_to_string_outro]           = bvn_action_set_state,
};
const state_t bvn_action_target_state[ACT__count] = {
	[ACT_ignore_comment_byte]       = ignore_comment_byte,
	[ACT_to_identifier_outro]       = identifier_outro,
	[ACT_to_type_body_outro]        = type_body_outro,
	[ACT_to_number_outro]           = number_outro,
	[ACT_string_outro]              = string_outro_nosp,
	[ACT_to_string_outro]           = string_outro,
	[ACT_escape_from_copy]          = escape_from_copy,
	[ACT_to_symbol_outro]           = symbol_outro,
	[ACT_to_reference_outro]        = reference_outro,
	[ACT_sp_to_nan1]                = sp_nan1,
	[ACT_sp_to_inf1]                = sp_inf1,
	[ACT_sp_to_neg1]                = sp_neg1,
	[ACT_tf_to_u]                   = tf_u,
	[ACT_tf_to_s]                   = tf_s,
	[ACT_tf_to_f]                   = tf_f,
	[ACT_tf_to_b]                   = tf_b,
	[ACT_tf_u_to_ui]                = tf_ui,
	[ACT_tf_u_to_ut]                = tf_ut,
};
const state_t bvn_kw_advance_state[dimension_state] = {
	[sp_nan1] = sp_nan2,
	[sp_inf1] = sp_inf2,
	[sp_neg1] = sp_neg2,  [sp_neg2] = sp_neg3,
	[tf_ui]      = tf_uin,
	[tf_ut]      = tf_utf,
	[tf_s]       = tf_si,   [tf_si]      = tf_sin,
	[tf_f]       = tf_fl,   [tf_fl]      = tf_flo,  [tf_flo]     = tf_floa,
	[tf_b]       = tf_bo,   [tf_bo]      = tf_boo,
};
