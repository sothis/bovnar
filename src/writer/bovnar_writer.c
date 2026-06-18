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

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "bovnar.h"
#include "bvn_datetime.h"
#include "bvn_float.h"
#include "bvn_io_impl.h"
#include "bvn_val_impl.h"
/*
 * ===========================================================================
 * Writer / serializer
 * ===========================================================================
 *
 * The writer is the inverse of the reader: the caller drives it with the same
 * bvnr_event_t stream the reader emits (so reader output can be piped straight
 * into a writer to canonicalise/pretty-print), and it produces bovnar bytes.
 *
 * Each bvnr_write_event goes through two stages, mirroring the reader's
 * lexer/validator split but in reverse:
 *
 *   1. bvn_writer_validate_event — reject anything that would produce an
 *      ill-formed or out-of-spec document (bad identifier, value out of range
 *      for its type, unbalanced struct close, illegal type spec, ...). This is
 *      what makes the writer safe to feed with caller-supplied data: it cannot
 *      be coaxed into emitting a stream the reader would reject.
 *   2. bvn_ser_serialize_event — actually format the bytes.
 *
 * Output buffering: for an fd sink, bytes are accumulated in wbuf and flushed
 * in BVN_SER_WBUF_SIZE chunks to amortise write() syscalls; for a memory sink
 * the wbuf is bypassed and bytes are copied straight in (bvn_ser_push). Errors
 * are sticky — once w->val.last_error is set, every further call short-circuits
 * — so callers can write a whole document and check once at bvnr_write_finish.
 *
 * Separator bookkeeping is the fiddly part: bovnar uses ';' between top-level/
 * struct members and ',' between array elements, and pretty mode adds
 * indentation/newlines. need_semi, the per-depth arr_need_comma[], and
 * struct_depth_at_array_start[] together decide when a separator is due — see
 * bvn_ser_emit_pending_comma / bvn_ser_mark_value_done.
 */

/*
 * Writer = serializer + a validator (reused only for its type/unit state and
 * error fields). Created zeroed and inert; bvnr_open_write_* installs the sink.
 */
bvnr_writer_t* bvnr_writer_create(void)
{
	bvnr_writer_t* w = malloc(sizeof(*w));
	if (!w) return NULL;
	memset(w, 0, sizeof(*w));
	return w;
}
void bvnr_writer_destroy(bvnr_writer_t* w)
{
	free(w);
}
/*
 * Record the first error and the byte offset it occurred at (flushed bytes plus
 * what is still buffered), then return false so callers can `return
 * bvn_writer_set_error(...)` as a one-liner. Because errors are sticky upstream,
 * only the first error's position is kept — the most useful one to report.
 */
bool bvn_writer_set_error(bvnr_writer_t* w, error_code_t err)
{
	w->val.last_error   = err;
	w->val.error_offset = bvnr_sink_bytes_written(&w->ser.sink)
	                    + (uint64_t)w->ser.wbuf_pos;
	return false;
}
bool bvn_ser_flush_wbuf(bvnr_serializer_t* s)
{
	if (!s->wbuf_pos) return true;
	if (!bvn_sink_push(&s->sink, s->wbuf, s->wbuf_pos)) return false;
	s->wbuf_pos = 0;
	return true;
}
/*
 * Central output primitive. Memory sinks are written through directly (no
 * point double-buffering a buffer into another buffer), while fd sinks
 * accumulate into wbuf and flush a full block at a time, turning many tiny
 * emits (one per byte/separator) into a few large write() calls. All the
 * push_byte/push_str/indent/newline helpers below funnel through here so the
 * buffering policy lives in exactly one place.
 */
static bool bvn_ser_push(bvnr_serializer_t* s,
	const void* data, uint32_t len)
{
	if (bvn_sink_impl(&s->sink)->is_mem)
		return bvn_sink_push(&s->sink, data, len);
	const uint8_t* src = (const uint8_t*)data;
	uint32_t pos = 0;
	while (pos < len) {
		uint32_t space = BVN_SER_WBUF_SIZE - s->wbuf_pos;
		uint32_t copy  = (len - pos) < space ? (len - pos) : space;
		memcpy(s->wbuf + s->wbuf_pos, src + pos, copy);
		s->wbuf_pos += copy;
		pos         += copy;
		if (s->wbuf_pos == BVN_SER_WBUF_SIZE) {
			if (!bvn_ser_flush_wbuf(s)) return false;
		}
	}
	return true;
}
static bool bvn_ser_push_byte(bvnr_serializer_t* s, uint8_t b)
{
	return bvn_ser_push(s, &b, 1);
}
static bool bvn_ser_push_str(bvnr_serializer_t* s, const char* str)
{
	return bvn_ser_push(s, str, (uint32_t)strlen(str));
}
static bool bvn_ser_indent(bvnr_serializer_t* s)
{
	if (!s->pretty || !s->indent) return true;
	uint8_t tabs[64];
	uint32_t fill = s->indent > 64u ? 64u : s->indent;
	memset(tabs, '\t', fill);
	uint32_t remaining = s->indent;
	while (remaining > 0) {
		uint32_t chunk = remaining > 64u ? 64u : remaining;
		if (!bvn_ser_push(s, tabs, chunk)) return false;
		remaining -= chunk;
	}
	return true;
}
static bool bvn_ser_newline(bvnr_serializer_t* s)
{
	if (!s->pretty) return true;
	return bvn_ser_push(s, "\n", 1);
}
static bool bvn_ser_space(bvnr_serializer_t* s)
{
	if (!s->pretty) return true;
	return bvn_ser_push_byte(s, ' ');
}
bool bvn_ser_finish_stream(bvnr_serializer_t* s)
{
	if (!s->need_semi) return bvn_ser_flush_wbuf(s);
	if (!bvn_ser_push_byte(s, ';')) return false;
	if (s->pretty && !bvn_ser_push_byte(s, '\n')) return false;
	s->need_semi = false;
	return bvn_ser_flush_wbuf(s);
}
static bool bvn_validate_string_content(bvnr_writer_t* w,
	const uint8_t* data, uint32_t length)
{
	if (length == 0) return true;
	if (!data) return bvn_writer_set_error(w, error_invalid_argument);
	for (uint32_t i = 0; i < length; i++) {
		uint8_t c = data[i];
		if (c <= 0x08 || (c >= 0x0E && c <= 0x1F) || c == 0x7F) {
			return bvn_writer_set_error(w,
				error_unexpected_input_byte);
		}
	}
	if (!bvn_validate_string(data, (size_t)length)) {
		return bvn_writer_set_error(w, error_invalid_utf8_byte);
	}
	return true;
}
static bool bvn_validate_id_for_writer(bvnr_writer_t* w,
	const void* data, uint32_t length)
{
	if (!data || length == 0) {
		return bvn_writer_set_error(w, error_empty_identifier);
	}
	char  static_buf[256];
	char *buf = static_buf;
	bool  need_free = false;
	if (length >= sizeof(static_buf)) {
		buf = malloc((size_t)length + 1u);
		if (!buf)
			return bvn_writer_set_error(w, error_invalid_argument);
		need_free = true;
	}
	memcpy(buf, data, length);
	buf[length] = '\0';
	bool ok = true;
	if (!bvn_validate_identifier(buf))
		ok = bvn_writer_set_error(w, error_invalid_argument);
	if (need_free) free(buf);
	return ok;
}
/*
 * Validate a numeric literal the caller wants to emit against its declared
 * type: digits legal in the base, sign legal for the family (no '-' for uint),
 * dot/exponent only for float families, and the value within the type's range.
 *
 * The static_buf/malloc pattern recurring through these validators exists
 * because the underlying string validators want a NUL-terminated C string, but
 * event payloads are (ptr,len) and not necessarily terminated. Short values use
 * a stack buffer (the overwhelmingly common case, zero allocation); only an
 * unusually long literal falls back to malloc. The same idiom appears in the
 * id/symbol/reference/string-as-number validators.
 */
static bool bvn_validate_number_for_writer(bvnr_writer_t* w,
	const void* data, uint32_t length, value_type_spec_t vt)
{
	if (!data || length == 0) return true;
	char static_buf[256];
	char *buf = static_buf;
	bool need_free = false;
	if (length >= sizeof(static_buf)) {
		buf = malloc((size_t)length + 1u);
		if (!buf)
			return bvn_writer_set_error(w, error_number_too_long);
		need_free = true;
	}
	memcpy(buf, data, length);
	buf[length] = '\0';
	bool ok = true;
	if (bvn_is_special_number_string(buf)) {
		if (!bvn_type_is_numeric(vt) && !bvn_type_is_plain(vt)) {
			ok = bvn_writer_set_error(w,
				error_type_value_mismatch);
		}
		goto out;
	}
	{
		uint32_t base  = bvn_effective_base(vt);
		uint32_t width = bvn_effective_width(vt);
		/* In bases 64/85 a leading '-' is a digit, not a negative sign. */
		if (vt.family == vt_uint && buf[0] == '-' &&
		    base != 64u && base != 85u) {
			ok = bvn_writer_set_error(w, error_value_out_of_range);
			goto out;
		}
		if (bvn_type_is_numeric(vt) && base != 10) {
			if (!bvn_validate_number_in_base(buf, base)) {
				ok = bvn_writer_set_error(w,
					error_digit_not_in_base);
				goto out;
			}
		} else {
			if (!bvn_validate_number(buf)) {
				ok = bvn_writer_set_error(w, error_type_value_mismatch);
				goto out;
			}
			bool has_dot = false, has_exp = false;
			for (uint32_t i = 0; i < length; i++) {
				if (buf[i] == '.') has_dot = true;
				if (buf[i] == 'e' || buf[i] == 'E') has_exp = true;
			}
			if ((has_dot || has_exp) && vt.family != vt_float &&
				vt.family != vt_float_fix &&
				vt.family != vt_float_dec &&
				vt.family != vt_plain) {
				ok = bvn_writer_set_error(w, error_type_value_mismatch);
				goto out;
			}
		}
		if (vt.family == vt_uint && vt.width) {
			if (!bvn_validate_uint_range(buf, width, base)) {
				ok = bvn_writer_set_error(w,
					error_value_out_of_range);
				goto out;
			}
		} else if ((vt.family == vt_sint || vt.family == vt_datetime) && vt.width) {
			/* datetime carrier is a signed integer (base 10); range-check like sint */
			if (!bvn_validate_sint_range(buf, width, base)) {
				ok = bvn_writer_set_error(w,
					error_value_out_of_range);
				goto out;
			}
		} else if (vt.family == vt_float_fix) {
			/* Refuse to emit a value the declared Q-format can't hold
			 * (would saturate/wrap on decode); matches the reader. */
			if (!bvn_float_str_fits_fix(buf, 10u, width,
									    bvn_effective_q(vt))) {
				ok = bvn_writer_set_error(w,
					error_value_out_of_range);
				goto out;
			}
		}
	}
out:
	if (need_free) free(buf);
	return ok;
}
static bool bvn_validate_string_as_number(bvnr_writer_t* w,
	const void* data, uint32_t length, value_type_spec_t vt)
{
	if (!data || length == 0) return true;
	char static_buf[256];
	char *buf = static_buf;
	bool need_free = false;
	if (length >= sizeof(static_buf)) {
		buf = malloc((size_t)length + 1u);
		if (!buf)
			return bvn_writer_set_error(w, error_string_too_long);
		need_free = true;
	}
	memcpy(buf, data, length);
	buf[length] = '\0';
	bool ok = true;
	{
		uint32_t base  = bvn_effective_base(vt);
		uint32_t width = bvn_effective_width(vt);
		/* In bases 64/85 a leading '-' is a digit, not a negative sign. */
		if (vt.family == vt_uint && buf[0] == '-' &&
		    base != 64u && base != 85u) {
			ok = bvn_writer_set_error(w, error_value_out_of_range);
			goto out;
		}
		if (vt.family == vt_uint || vt.family == vt_sint) {
			if (!bvn_validate_digits_for_base(buf, base)) {
				ok = bvn_writer_set_error(w,
					error_digit_not_in_base);
				goto out;
			}
		} else if (vt.family == vt_float ||
			   vt.family == vt_float_fix ||
			   vt.family == vt_float_dec) {
			if (!bvn_validate_number_in_base(buf, base)) {
				ok = bvn_writer_set_error(w,
					error_digit_not_in_base);
				goto out;
			}
		}
		if (vt.family == vt_uint && vt.width) {
			if (!bvn_validate_uint_range(buf, width, base)) {
				ok = bvn_writer_set_error(w,
					error_value_out_of_range);
				goto out;
			}
		} else if ((vt.family == vt_sint || vt.family == vt_datetime) && vt.width) {
			/* datetime carrier is a signed integer (base 10); range-check like sint */
			if (!bvn_validate_sint_range(buf, width, base)) {
				ok = bvn_writer_set_error(w,
					error_value_out_of_range);
				goto out;
			}
		} else if (vt.family == vt_float_fix) {
			if (!bvn_float_str_fits_fix(buf, 10u, width,
									    bvn_effective_q(vt))) {
				ok = bvn_writer_set_error(w,
					error_value_out_of_range);
				goto out;
			}
		}
	}
out:
	if (need_free) free(buf);
	return ok;
}
static bool bvn_validate_symbol_for_writer(bvnr_writer_t* w,
	const void* data, uint32_t length)
{
	if (length == 0 || !data)
		return bvn_writer_set_error(w, error_empty_identifier);
	char  static_buf[256];
	char *buf = static_buf;
	bool  need_free = false;
	if (length >= sizeof(static_buf)) {
		buf = malloc((size_t)length + 1u);
		if (!buf)
			return bvn_writer_set_error(w, error_symbol_too_long);
		need_free = true;
	}
	memcpy(buf, data, length);
	buf[length] = '\0';
	bool ok = true;
	if (!bvn_validate_symbol(buf))
		ok = bvn_writer_set_error(w, error_type_value_mismatch);
	/* A symbol whose text is a reserved keyword (null/true/false/on/off/
	 * nan/inf/ninf) is emitted bare and would be reclassified by the reader
	 * into a null/bool/float value, silently losing the symbol type. Symbols
	 * have no quoted form, so the only correct option is to reject it. */
	else if ((length == 4 && memcmp(buf, "null", 4) == 0) ||
	         (length == 4 && memcmp(buf, "true", 4) == 0) ||
	         (length == 2 && memcmp(buf, "on",   2) == 0) ||
	         (length == 5 && memcmp(buf, "false",5) == 0) ||
	         (length == 3 && memcmp(buf, "off",  3) == 0) ||
	         (length == 3 && memcmp(buf, "nan",  3) == 0) ||
	         (length == 3 && memcmp(buf, "inf",  3) == 0) ||
	         (length == 4 && memcmp(buf, "ninf", 4) == 0))
		ok = bvn_writer_set_error(w, error_type_value_mismatch);
	if (need_free) free(buf);
	return ok;
}
static bool bvn_validate_reference_for_writer(bvnr_writer_t* w,
	const void* data, uint32_t length)
{
	if (length == 0 || !data)
		return bvn_writer_set_error(w, error_invalid_argument);
	char static_buf[256];
	char *buf = static_buf;
	bool need_free = false;
	if (length >= sizeof(static_buf)) {
		buf = malloc((size_t)length + 1u);
		if (!buf)
			return bvn_writer_set_error(w, error_reference_too_long);
		need_free = true;
	}
	memcpy(buf, data, length);
	buf[length] = '\0';
	bool ok = true;
	if (!bvn_validate_reference(buf))
		ok = bvn_writer_set_error(w, error_type_value_mismatch);
	if (need_free) free(buf);
	return ok;
}
/*
 * Validate a type annotation itself (independent of any value): width and base
 * must be legal for the family. These rules encode the format spec — e.g.
 * float widths are 16 or multiples of 32 up to the max precision; float_fix's
 * fractional-bit count q must be smaller than the total width; float_dec takes
 * no base; an explicit numeric base must be 2-62, 64 or 85 (the supported digit
 * alphabets); bool carries neither width nor base. Catching these here means a
 * malformed <...> can never reach the byte stream.
 */
static bool bvn_validate_type_spec_for_writer(bvnr_writer_t* w,
	value_type_spec_t vt)
{
	if (vt.family == vt_uint || vt.family == vt_sint) {
		if (vt.width > BVN_MAX_INT_WIDTH)
			return bvn_writer_set_error(w, error_illegal_value_type);
		/* Bases 64 and 85 use '+'/'-' as digit characters, leaving no sign
		 * character, so they are unsigned-only: reject signed integers. */
		if (vt.family == vt_sint && (vt.base == 64u || vt.base == 85u))
			return bvn_writer_set_error(w, error_illegal_value_type);
	}
	/* datetime carrier width has the same bound as an integer (base holds the
	 * epoch index, not a numeric base, so it is not range-checked here). */
	if (vt.family == vt_datetime && vt.width > BVN_MAX_INT_WIDTH)
		return bvn_writer_set_error(w, error_illegal_value_type);
	if (vt.family == vt_float) {
		if (vt.base != 0 && vt.base != 10 && vt.base != 16) {
			return bvn_writer_set_error(w,
				error_illegal_value_type);
		}
		if (vt.width != 0 && vt.width != 16 &&
		    (vt.width % 32u != 0u || vt.width > BVN_FLOAT_MAX_PREC)) {
			return bvn_writer_set_error(w,
				error_illegal_value_type);
		}
	}
	if (vt.family == vt_float_fix || vt.family == vt_float_dec) {
		if (vt.width != 0  && vt.width != 16  && vt.width != 32 &&
			vt.width != 64 && vt.width != 128 && vt.width != 256) {
			return bvn_writer_set_error(w, error_illegal_value_type);
		}
	}
	if (vt.family == vt_float_fix) {
		uint32_t eff_w = vt.width ? vt.width : 64u;
		if (vt.base >= eff_w)
			return bvn_writer_set_error(w, error_illegal_value_type);
	}
	if (vt.family == vt_float_dec) {
		if (vt.base != 0)
			return bvn_writer_set_error(w, error_illegal_value_type);
	}
	/*
	 * Numeral-base validity applies only to the families whose .base field
	 * is actually a numeral base (uint/sint/float). For float_fix the .base
	 * field holds Q (fractional bits, validated above against the width), and
	 * float_dec leaves it 0 (checked above); neither must be forced into the
	 * {2-62,64,85} numeral-base set, or valid specs like <float_fix:64,q63>
	 * would be wrongly rejected.
	 */
	if ((vt.family == vt_uint || vt.family == vt_sint ||
	     vt.family == vt_float) && vt.base != 0) {
		if (!((vt.base >= 2 && vt.base <= 62) ||
			  vt.base == 64 || vt.base == 85)) {
			return bvn_writer_set_error(w,
				error_illegal_value_type);
		}
	}
	if (vt.family == vt_bool || vt.family == vt_utf8) {
		/* bool and utf8 are parameterless families; the reader rejects a
		 * width or base on them (error_illegal_value_type), so the writer
		 * must too, or it would emit e.g. <utf8:40> that it can't read back. */
		if (vt.width != 0 || vt.base != 0)
			return bvn_writer_set_error(w, error_illegal_value_type);
	}
	return true;
}
static bool bvn_check_type_value_compat(bvnr_writer_t* w,
	token_type_t tt, value_type_spec_t vt)
{
	if (bvn_type_is_plain(vt) || tt == token_is_null_value)
		return true;
	if (vt.family == vt_utf8) {
		if (tt != token_is_string && tt != token_is_array_string) {
			return bvn_writer_set_error(w,
				error_type_value_mismatch);
		}
		return true;
	}
	if (vt.family == vt_bool) {
		if (tt != token_is_bool) {
			return bvn_writer_set_error(w,
				error_type_value_mismatch);
		}
		return true;
	}
	if (vt.family == vt_uint || vt.family == vt_sint ||
		vt.family == vt_float ||
		vt.family == vt_float_fix || vt.family == vt_float_dec) {
		if (tt != token_is_number && tt != token_is_array_number &&
			tt != token_is_string && tt != token_is_array_string) {
			return bvn_writer_set_error(w,
				error_type_value_mismatch);
		}
		return true;
	}
	return true;
}
/*
 * Stage 1 of bvnr_write_event: semantic validation of one event before any
 * bytes are produced. Besides per-kind value checks it enforces structural
 * invariants the serializer assumes — stream_start happens once, struct closes
 * are balanced, nesting depths stay within the configured caps — and snapshots
 * the active value_type/unit from annotation events so subsequent data events
 * can be range-checked against it. Returning false here (via
 * bvn_writer_set_error) aborts the write before the stream is corrupted.
 */
static bool bvn_writer_validate_event(bvnr_writer_t* w,
	bvnr_event_t ev, bvnr_data_t* data)
{
	switch (ev) {
	case ev_stream_start:
		if (w->ser.stream_begun)
			return bvn_writer_set_error(w, error_invalid_argument);
		w->ser.stream_begun = true;
		return true;
	case ev_stream_end:
		return true;
	case ev_assignment_start:
		w->ser.stream_begun = true;
		return bvn_validate_id_for_writer(w,
			data->data, data->length);
	case ev_type_annotation_start:
		w->ser.stream_begun = true;
		w->val.value_type  = data->value_type;
		w->val.parsed_unit = data->value_unit;
		return bvn_validate_type_spec_for_writer(w,
			data->value_type);
	case ev_type_annotation_type_family:
	case ev_type_annotation_type_family_parameter:
		w->val.value_type  = data->value_type;
		w->val.parsed_unit = data->value_unit;
		break;
	case ev_type_annotation_end:
		break;
	case ev_data: {
		w->ser.stream_begun = true;
		token_type_t tt = data->type;
		value_type_spec_t vt = data->value_type;
		if (!bvn_check_type_value_compat(w, tt, vt))
			return false;
		if (tt == token_is_null_value) {
		} else if (tt == token_is_number ||
				   tt == token_is_array_number) {
			if (!bvn_validate_number_for_writer(w,
					data->data, data->length, vt))
				return false;
		} else if (tt == token_is_string ||
				   tt == token_is_array_string) {
			if (!bvn_validate_string_content(w,
					(const uint8_t*)data->data, data->length))
				return false;
			if (bvn_type_is_numeric(vt)) {
				if (!bvn_validate_string_as_number(w,
						data->data, data->length, vt))
					return false;
			}
		} else if (tt == token_is_bool) {
			const char* bs = (const char*)data->data;
			uint32_t    bn = data->length;
			bool ok = bs && (
				(bn == 4 && memcmp(bs, "true",  4) == 0) ||
				(bn == 5 && memcmp(bs, "false", 5) == 0) ||
				(bn == 2 && memcmp(bs, "on",    2) == 0) ||
				(bn == 3 && memcmp(bs, "off",   3) == 0));
			if (!ok)
				return bvn_writer_set_error(w,
					error_type_value_mismatch);
		} else if (tt == token_is_symbol) {
			if (!bvn_validate_symbol_for_writer(w,
					data->data, data->length))
				return false;
		} else if (tt == token_is_reference) {
			if (!bvn_validate_reference_for_writer(w,
					data->data, data->length))
				return false;
		} else if (tt == token_is_octet_stream) {
			if (data->length == 0 || data->length > 65536u)
				return bvn_writer_set_error(w,
					error_invalid_argument);
		}
		break;
	}
	case ev_struct_end:
		w->ser.stream_begun = true;
		if (w->ser.struct_depth == 0)
			return bvn_writer_set_error(w, error_illegal_struct_close);
		break;
	case ev_struct_start:
		w->ser.stream_begun = true;
		if (w->ser.struct_depth >= (uint32_t)w->ser.max_struct_nesting)
			return bvn_writer_set_error(w, error_struct_nesting_too_high);
		break;
	case ev_array_row_start:
		w->ser.stream_begun = true;
		if (w->ser.array_depth >= w->ser.max_array_nesting)
			return bvn_writer_set_error(w, error_array_nesting_too_high);
		break;
	default:
		w->ser.stream_begun = true;
		break;
	}
	return true;
}
/*
 * Emit a quoted string, escaping only the characters that must be escaped. The
 * run_start cursor batches every maximal span of literal (non-escaped) bytes
 * into a single push, so a clean string is written essentially as one memcpy
 * plus the surrounding quotes rather than byte-by-byte. UTF-8 multibyte
 * sequences pass through verbatim (the validate stage already proved they are
 * well-formed), which is why no per-byte UTF-8 handling is needed here.
 */
static bool bvn_ser_serialize_string(bvnr_serializer_t* s,
	const uint8_t* data, uint32_t len)
{
	if (!bvn_ser_push_byte(s, '"')) return false;
	uint32_t run_start = 0;
	for (uint32_t i = 0; i < len; i++) {
		uint8_t c = data[i];
		const char* esc = NULL;
		switch (c) {
		case '\t': esc = "\\t";  break;
		case '\n': esc = "\\n";  break;
		case '\v': esc = "\\v";  break;
		case '\f': esc = "\\f";  break;
		case '\r': esc = "\\r";  break;
		case '"':  esc = "\\\""; break;
		case '\\': esc = "\\\\"; break;
		default:   continue;
		}
		if (i > run_start) {
			if (!bvn_ser_push(s, data + run_start, i - run_start))
				return false;
		}
		if (!bvn_ser_push(s, esc, 2)) return false;
		run_start = i + 1u;
	}
	if (len > run_start) {
		if (!bvn_ser_push(s, data + run_start, len - run_start))
			return false;
	}
	return bvn_ser_push_byte(s, '"');
}
/*
 * Comma placement in arrays is subtle: a comma separates *array elements*, but
 * a struct or nested array opened as an element contains its own values that
 * must NOT get element-commas. We disambiguate by remembering the struct depth
 * at which each array level began (struct_depth_at_array_start). A value is a
 * "direct" element of array level idx only if the current struct depth equals
 * the depth recorded when that array opened; values produced while deeper
 * inside a struct are not direct elements and so don't arm the comma flag.
 *
 * bvn_ser_emit_pending_comma writes the comma due before the next element;
 * bvn_ser_mark_value_done arms the flag after a complete value is emitted.
 */
static inline bool bvn_ser_is_direct_array_element(
	const bvnr_serializer_t* s, uint32_t idx)
{
	if (idx >= s->max_array_nesting)
		return false;
	return s->struct_depth ==
		   s->struct_depth_at_array_start[idx];
}
static bool bvn_ser_emit_pending_comma(bvnr_serializer_t* s)
{
	if (s->array_depth == 0)
		return true;
	uint32_t idx = s->array_depth - 1u;
	if (idx >= s->max_array_nesting)
		return true;
	if (!bvn_ser_is_direct_array_element(s, idx))
		return true;
	if (!s->arr_need_comma[idx])
		return true;
	if (!bvn_ser_push_byte(s, ','))
		return false;
	if (!bvn_ser_space(s))
		return false;
	s->arr_need_comma[idx] = false;
	return true;
}
static void bvn_ser_mark_value_done(bvnr_serializer_t* s)
{
	if (s->array_depth == 0)
		return;
	uint32_t idx = s->array_depth - 1u;
	if (idx >= s->max_array_nesting)
		return;
	if (!bvn_ser_is_direct_array_element(s, idx))
		return;
	s->arr_need_comma[idx] = true;
}
/*
 * Stage 2: turn one (already-validated) event into bytes.
 *
 * This is the formatter that knows the concrete bovnar syntax: `.key = ` for
 * assignments, `<...>` type annotations with the `:`-then-`,` parameter
 * punctuation, `{}` structs, `[]` arrays with `/` dimension separators,
 * `nan`/`inf`/`ninf` bare keywords for special floats, `&` reference prefix, and the 0x00/0x01
 * binary octet-stream framing. Interleaved through every case is the separator
 * state machine (need_semi, pending commas, indentation) so output is
 * syntactically correct in both compact and pretty modes. had_type_annotation
 * suppresses the pending comma between a value's annotation and the value
 * itself, since they form one element.
 */
/*
 * spec 1.1 — decide whether a datetime value carrying captured ISO sub-second
 * digits should be re-emitted as an ISO literal (so the fraction, which the
 * whole-second integer carrier cannot hold, survives the round-trip), and if so
 * compute the civil UTC fields. Pure: produces no output, so the path is chosen
 * before the serializer writes any bytes.
 *
 * Returns false (caller emits the plain integer carrier) when the value is not a
 * datetime, has no fraction, the carrier text can't be parsed, or the civil year
 * falls outside the 0000..9999 an ISO literal's 4-digit year allows — the last
 * guard matters because the public writer API could be handed a fraction with an
 * out-of-literal-range carrier. The atomic GNSS epochs never reach here: they
 * reject ISO literals at read time and so never carry a fraction.
 */
static bool bvn_ser_datetime_to_civil(const bvnr_data_t* d, bvn_datetime_t* dt)
{
	if (d->value_type.family != vt_datetime ||
	    d->frac_data == NULL || d->frac_length == 0 || d->data == NULL)
		return false;
	/* The fraction is spliced verbatim into the reconstructed ISO literal, so it
	 * must be pure ASCII digits. The reader only ever captures digits, but the
	 * public bvnr_write_event() API could hand us anything; reject (and fall back
	 * to the integer carrier) rather than emit a literal that will not re-parse. */
	const uint8_t* fd = (const uint8_t*)d->frac_data;
	for (uint32_t i = 0; i < d->frac_length; i++)
		if (fd[i] < '0' || fd[i] > '9')
			return false;
	char numbuf[24];
	if (d->length == 0 || d->length >= sizeof numbuf)
		return false;
	memcpy(numbuf, d->data, d->length);
	numbuf[d->length] = '\0';
	int64_t secs;
	if (!bvn_parse_int64(numbuf, d->value_type, &secs))
		return false;
	const char* ep = bvnr_datetime_epoch_name(d->value_type);
	/* Only the epochs whose ISO literal the reader accepts may be re-emitted as a
	 * literal. The atomic GNSS epochs reject literals at read time, so they never
	 * carry a reader-captured fraction; one supplied via the writer API falls back
	 * to the integer carrier rather than producing a literal the reader rejects. */
	if (ep == NULL ||
	    strcmp(ep, "gps") == 0 || strcmp(ep, "galileo") == 0 ||
	    strcmp(ep, "glonass") == 0 || strcmp(ep, "beidou") == 0)
		return false;
	/* Zero-init so the validity guard is deterministic even when a converter
	 * leaves *dt untouched: bvn_dt_tai_seconds_to_utc() does so on the tai
	 * underflow near INT64_MIN, and bvn_dt_epoch_seconds_to_datetime() leaves
	 * dt->date untouched when the MJD is outside the gregorian range. The fields
	 * then stay zero (month 0) and the guard below rejects them. */
	memset(dt, 0, sizeof *dt);
	if (strcmp(ep, "tai") == 0)
		bvn_dt_tai_seconds_to_utc(dt, secs);
	else
		bvn_dt_epoch_seconds_to_datetime(dt,
			(bvn_epoch_t)bvnr_datetime_epoch_mjd(d->value_type), secs);
	/* A well-formed civil time the reader can round-trip has month 1..12, a valid
	 * day, and a 4-digit year; reject anything else (including the untouched-zero
	 * case above) and emit the plain integer carrier instead. */
	return dt->date.month >= 1 && dt->date.month <= 12 &&
	       dt->date.day   >= 1 && dt->date.day   <= 31 &&
	       dt->date.year  >= 0 && dt->date.year  <= 9999;
}
bool bvn_ser_serialize_event(bvnr_serializer_t* s,
	bvnr_event_t ev, bvnr_data_t* d)
{
	switch (ev) {
	case ev_stream_start:
		break;
	case ev_stream_end:
		break;
	case ev_assignment_start:
		if (s->need_semi) {
			if (!bvn_ser_push_byte(s, ';')) return false;
			if (!bvn_ser_newline(s)) return false;
		}
		if (!bvn_ser_indent(s)) return false;
		if (!bvn_ser_push_byte(s, '.')) return false;
		if (d->data && d->length) {
			if (!bvn_ser_push(s, d->data, d->length))
				return false;
		}
		if (!bvn_ser_space(s)) return false;
		if (!bvn_ser_push_byte(s, '=')) return false;
		if (!bvn_ser_space(s)) return false;
		s->need_semi = false;
		s->had_type_annotation = false;
		s->emitted_type_param = false;
		break;
	case ev_type_annotation_start:
		if (!bvn_ser_emit_pending_comma(s))
			return false;
		s->had_type_annotation = true;
		s->emitted_type_param = false;
		if (!bvn_ser_push_byte(s, '<')) return false;
		break;
	case ev_type_annotation_type_family: {
		const char* fname;
		switch (d->value_type.family) {
		case vt_uint:      fname = "uint";      break;
		case vt_sint:      fname = "sint";      break;
		case vt_float:     fname = "float";     break;
		case vt_float_fix: fname = "float_fix"; break;
		case vt_float_dec: fname = "float_dec"; break;
		case vt_utf8:      fname = "utf8";      break;
		case vt_bool:      fname = "bool";      break;
		case vt_datetime:  fname = "datetime";  break;
		default:           fname = "plain";     break;
		}
		if (!bvn_ser_push_str(s, fname))
			return false;
		break;
	}
	case ev_type_annotation_type_family_parameter: {
		bool use_colon = !s->emitted_type_param;
		if (d->type == token_is_type_width) {
			char buf[12];
			if (!bvn_ser_push_byte(s, ':')) return false;
			int32_t n = bvn_format_uint64(buf, sizeof(buf),
				bvn_effective_width(d->value_type), 10u, 0u);
			if (n < 0) return false;
			if (!bvn_ser_push(s, buf, (uint32_t)n))
				return false;
			s->emitted_type_param = true;
		} else if (d->type == token_is_type_base) {
			char buf[12];
			if (!bvn_ser_push_byte(s, use_colon ? ':' : ',')) return false;
			if (!bvn_ser_push_byte(s, '_')) return false;
			int32_t n = bvn_format_uint64(buf, sizeof(buf),
				bvn_effective_base(d->value_type), 10u, 0u);
			if (n < 0) return false;
			if (!bvn_ser_push(s, buf, (uint32_t)n))
				return false;
			s->emitted_type_param = true;
		} else if (d->type == token_is_type_q) {
			char buf[12];
			if (!bvn_ser_push_byte(s, use_colon ? ':' : ',')) return false;
			if (!bvn_ser_push_byte(s, 'q')) return false;
			int32_t n = bvn_format_uint64(buf, sizeof(buf),
				bvn_effective_q(d->value_type), 10u, 0u);
			if (n < 0) return false;
			if (!bvn_ser_push(s, buf, (uint32_t)n))
				return false;
			s->emitted_type_param = true;
		} else if (d->type == token_is_unit) {
			if (!bvn_ser_push_byte(s, use_colon ? ':' : ','))
				return false;
			if (d->data && d->length) {
				if (!bvn_ser_push(s, d->data, d->length))
					return false;
			}
			s->emitted_type_param = true;
		}
		break;
	}
	case ev_type_annotation_end:
		if (!bvn_ser_push_byte(s, '>')) return false;
		if (!bvn_ser_space(s)) return false;
		break;
	case ev_struct_start:
		if (!s->had_type_annotation) {
			if (!bvn_ser_emit_pending_comma(s))
				return false;
		}
		s->had_type_annotation = false;
		if (!bvn_ser_push_byte(s, '{')) return false;
		if (!bvn_ser_newline(s)) return false;
		s->indent++;
		s->need_semi = false;
		s->struct_depth++;
		break;
	case ev_struct_end:
		/* Self-protect against an unbalanced struct-end, mirroring the array-depth
		 * guard below: the canon-observer path drives this serializer with no
		 * validation, so without a matching struct-start indent/struct_depth would
		 * underflow and the next bvn_ser_indent would emit ~4 billion tab bytes. */
		if (s->struct_depth == 0)
			return false;
		s->indent--;
		if (s->need_semi) {
			if (!bvn_ser_push_byte(s, ';')) return false;
			if (!bvn_ser_newline(s)) return false;
		}
		if (!bvn_ser_indent(s)) return false;
		if (!bvn_ser_push_byte(s, '}')) return false;
		s->need_semi = true;
		s->struct_depth--;
		bvn_ser_mark_value_done(s);
		break;
	case ev_array_row_start: {
		/* Self-protect the fixed-size depth arrays. The validating writer path
		 * already rejects over-deep nesting (bvn_writer_validate_event), but the
		 * canon-observer path drives this serializer directly with no validation,
		 * so the bound is enforced here too rather than trusting the event source. */
		if (s->array_depth >= s->max_array_nesting)
			return false;
		if (!s->had_type_annotation) {
			if (!bvn_ser_emit_pending_comma(s))
				return false;
		}
		s->had_type_annotation = false;
		s->struct_depth_at_array_start[s->array_depth] = s->struct_depth;
		s->arr_need_comma[s->array_depth] = false;
		if (!bvn_ser_push_byte(s, '[')) return false;
		s->array_depth++;
		break;
	}
	case ev_array_row_end:
		if (s->array_depth > 0)
			s->array_depth--;
		if (!bvn_ser_push_byte(s, ']')) return false;
		s->need_semi = true;
		bvn_ser_mark_value_done(s);
		break;
	case ev_array_dim_start:
		if (s->pretty) {
			if (!bvn_ser_push_str(s, " / ")) return false;
		} else {
			if (!bvn_ser_push_str(s, "/")) return false;
		}
		if (s->array_depth > 0) {
			uint32_t idx = s->array_depth - 1u;
			if (idx < s->max_array_nesting &&
				bvn_ser_is_direct_array_element(s, idx)) {
				s->arr_need_comma[idx] = false;
			}
		}
		break;
	case ev_octet_stream_start:
		if (!s->had_type_annotation) {
			if (!bvn_ser_emit_pending_comma(s))
				return false;
		}
		s->had_type_annotation = false;
		if (!bvn_ser_push_byte(s, 0x00)) return false;
		s->in_octet_stream = true;
		break;
	case ev_octet_stream_end:
		if (!bvn_ser_push_byte(s, 0x00)) return false;
		s->in_octet_stream = false;
		s->need_semi = true;
		bvn_ser_mark_value_done(s);
		break;
	case ev_data:
		if (!s->had_type_annotation) {
			if (!bvn_ser_emit_pending_comma(s))
				return false;
		}
		s->had_type_annotation = false;
		if (d->type == token_is_null_value) {
			/* A null *direct array element* is written as the explicit "null"
			 * keyword. A null can no longer be an empty slot: an empty slot with
			 * no delimiting comma is now an empty array "[]" (zero elements), so
			 * "[null]" (one null) must stay distinct from "[]". Emitting "null"
			 * uniformly keeps every position unambiguous and round-trip-safe.
			 * Top-level nulls (".x = ;") and struct-field nulls are unaffected. */
			if (s->array_depth > 0 &&
				bvn_ser_is_direct_array_element(s, s->array_depth - 1u)) {
				if (!bvn_ser_push_str(s, "null"))
					return false;
			}
		} else if (d->type == token_is_number ||
				   d->type == token_is_array_number) {
			bvn_datetime_t dt;
			if (bvn_ser_datetime_to_civil(d, &dt)) {
				/* spec 1.1 — re-emit as an ISO literal so the captured
				 * sub-second digits round-trip:
				 * YYYY-MM-DDTHH:MM:SS.<frac>Z (always UTC). */
				char head[24];
				int hn = snprintf(head, sizeof head,
					"%04lld-%02lld-%02lldT%02lld:%02lld:%02lld",
					(long long)dt.date.year, (long long)dt.date.month,
					(long long)dt.date.day, (long long)dt.hour,
					(long long)dt.minute, (long long)dt.second);
				if (hn < 0 || (size_t)hn >= sizeof head)
					return false;
				if (!bvn_ser_push(s, head, (uint32_t)hn)) return false;
				if (!bvn_ser_push_byte(s, '.')) return false;
				if (!bvn_ser_push(s, d->frac_data, d->frac_length))
					return false;
				if (!bvn_ser_push_byte(s, 'Z')) return false;
			} else if (d->data && d->length) {
				/* Special floats (nan/inf/ninf) are emitted as the
				 * bare keyword, exactly like any other number token —
				 * no sigil. */
				if (!bvn_ser_push(s, d->data, d->length))
					return false;
			}
		} else if (d->type == token_is_string ||
				   d->type == token_is_array_string) {
			if (!bvn_ser_serialize_string(s,
					(const uint8_t*)d->data, d->length))
				return false;
		} else if (d->type == token_is_bool) {
			const char* bs = (const char*)d->data;
			uint32_t    bn = d->length;
			bool tv = bs && ((bn == 4 && memcmp(bs, "true", 4) == 0) ||
			                 (bn == 2 && memcmp(bs, "on", 2) == 0));
			if (!bvn_ser_push_str(s, tv ? "true" : "false"))
				return false;
		} else if (d->type == token_is_symbol) {
			if (d->data && d->length) {
				if (!bvn_ser_push(s, d->data, d->length))
					return false;
			}
		} else if (d->type == token_is_reference) {
			if (!bvn_ser_push_byte(s, '&')) return false;
			if (d->data && d->length) {
				if (!bvn_ser_push(s, d->data, d->length))
					return false;
			}
		} else if (d->type == token_is_octet_stream) {
			if (d->data && d->length) {
				if (d->length > 65536u)
					return false;
				uint8_t hdr[3];
				uint32_t clen = d->length;
				hdr[0] = 0x01;
				if (clen == 65536u) clen = 0;
				hdr[1] = (uint8_t)(clen & 0xFFu);
				hdr[2] = (uint8_t)((clen >> 8) & 0xFFu);
				if (!bvn_ser_push(s, hdr, 3))
					return false;
				if (!bvn_ser_push(s, d->data, d->length))
					return false;
			}
		}
		s->need_semi = true;
		bvn_ser_mark_value_done(s);
		break;
	default:
		break;
	}
	return true;
}
static void bvn_writer_init(bvnr_writer_t* w, bvnr_write_flags_t* opts)
{
	memset(w, 0, sizeof(*w));
	w->ser.pretty = false;
	w->ser.indent = 0;
	w->ser.need_semi = false;
	w->ser.finished = false;
	w->ser.array_depth = 0;
	w->ser.in_octet_stream = false;
	w->ser.struct_depth = 0;
	w->ser.had_type_annotation = false;
	w->ser.emitted_type_param = false;
	w->ser.wbuf_pos = 0;
	memset(w->ser.arr_need_comma, 0, sizeof(w->ser.arr_need_comma));
	memset(w->ser.struct_depth_at_array_start, 0,
		   sizeof(w->ser.struct_depth_at_array_start));
	w->val.value_type  = BVN_TYPE_PLAIN;
	w->val.parsed_unit = BVN_UNIT_NO_PREFIX(bu_none);
	if (opts) {
		w->ser.event_userdata    = opts->userdata;
		w->ser.on_event          = opts->on_event;
		w->val.on_error          = opts->on_error;
		w->ser.max_array_nesting = opts->max_array_nesting;
		w->ser.max_struct_nesting = opts->max_struct_nesting;
		w->ser.unit_flags        = opts->unit_flags;
	}
	if (!w->ser.max_array_nesting)
		w->ser.max_array_nesting = max_array_nesting;
	if (!w->ser.max_struct_nesting)
		w->ser.max_struct_nesting = max_struct_nesting;
}
bool bvnr_open_write_sink(
	bvnr_writer_t* w, const bvnr_sink_t* sink,
	bool pretty, bvnr_write_flags_t* options)
{
	if (!w || !sink || !bvn_sink_impl_c(sink)->push) return false;
	bvn_writer_init(w, options);
	w->ser.sink   = *sink;
	w->ser.pretty = pretty;
	if (options && options->emit_version)
		return bvnr_write_version(w,
			(uint16_t)BVNR_SPEC_VERSION_MAJOR,
			(uint16_t)BVNR_SPEC_VERSION_MINOR);
	return true;
}
bool bvnr_open_write_mem(
	bvnr_writer_t* w, void* buf, uint64_t cap,
	bool pretty, bvnr_write_flags_t* options)
{
	if (!w || !buf) return false;
	bvnr_sink_t sink;
	bvnr_sink_to_mem(&sink, buf, cap);
	return bvnr_open_write_sink(w, &sink, pretty, options);
}
/*
 * Public event entry point: validate, serialize, then notify the optional
 * on_event observer. Bails immediately if a prior error is latched or the
 * stream is already finished, so a caller can write a whole document without
 * checking after every call. A serialize failure is mapped to the right sink
 * error (buffer exhausted vs. write failed) for accurate diagnostics.
 */
bool bvnr_write_event(
	bvnr_writer_t* w, bvnr_event_t ev, bvnr_data_t* data)
{
	if (!w) return false;
	if (!data) return bvn_writer_set_error(w, error_invalid_argument);
	if (w->val.last_error != error_none)
		return false;
	if (w->ser.finished)
		return bvn_writer_set_error(w, error_invalid_argument);
	if (!bvn_writer_validate_event(w, ev, data))
		return false;
	if (!bvn_ser_serialize_event(&w->ser, ev, data))
		return bvn_writer_set_error(w, bvn_sink_impl(&w->ser.sink)->is_mem
			? error_sink_buffer_exhausted
			: error_writing_to_sink);
	if (w->ser.on_event) {
		if (!w->ser.on_event(w->ser.event_userdata, ev, data))
			return bvn_writer_set_error(w, error_scanner_callback_failed);
	}
	return true;
}
/*
 * Emit a leading "#!bovnar <major>.<minor>" version directive. Legal only before
 * any output has begun (right after open); afterwards it is error_invalid_argument.
 * The directive is a single comment line, so it never affects separator
 * bookkeeping — it is written straight through ahead of the value stream.
 */
bool bvnr_write_version(bvnr_writer_t* w, uint16_t major, uint16_t minor)
{
	if (!w) return false;
	if (w->val.last_error != error_none)
		return false;
	if (w->ser.finished || w->ser.stream_begun || w->ser.version_emitted)
		return bvn_writer_set_error(w, error_invalid_argument);
	char line[48];
	int n = snprintf(line, sizeof(line), "#!bovnar %u.%u\n",
		(unsigned)major, (unsigned)minor);
	if (n <= 0 || (size_t)n >= sizeof(line))
		return bvn_writer_set_error(w, error_invalid_argument);
	if (!bvn_ser_push(&w->ser, line, (uint32_t)n))
		return bvn_writer_set_error(w, bvn_sink_impl(&w->ser.sink)->is_mem
			? error_sink_buffer_exhausted
			: error_writing_to_sink);
	w->ser.version_emitted = true;
	return true;
}
/*
 * Close out the document: refuse if any struct/array is still open (that would
 * be a truncated stream), emit the trailing ';' if one is pending, flush the
 * write buffer and the sink, and latch finished so no further events are
 * accepted. Must be called for output to be complete — the final separator and
 * any buffered bytes are only guaranteed on disk after this returns true.
 */
bool bvnr_write_finish(bvnr_writer_t* w)
{
	if (!w) return false;
	if (w->val.last_error != error_none)
		return false;
	if (w->ser.struct_depth > 0 || w->ser.array_depth > 0)
		return bvn_writer_set_error(w, error_got_incomplete_bvnr_stream);
	if (!bvn_ser_finish_stream(&w->ser))
		return bvn_writer_set_error(w, bvn_sink_impl(&w->ser.sink)->is_mem
			? error_sink_buffer_exhausted
			: error_writing_to_sink);
	{
		bvn_sink_impl_t* si = bvn_sink_impl(&w->ser.sink);
		if (si->flush) {
			if (!si->flush(&w->ser.sink))
				return bvn_writer_set_error(w, si->is_mem
					? error_sink_buffer_exhausted
					: error_writing_to_sink);
		}
	}
	w->ser.finished = true;
	return true;
}
error_code_t bvnr_writer_get_error(const bvnr_writer_t* w)
{
	if (!w) return error_invalid_argument;
	return w->val.last_error;
}
uint64_t bvnr_writer_get_error_offset(const bvnr_writer_t* w)
{
	if (!w) return 0;
	return w->val.error_offset;
}
uint64_t bvnr_writer_bytes_written(const bvnr_writer_t* w)
{
	if (!w) return 0;
	return bvnr_sink_bytes_written(&w->ser.sink) + w->ser.wbuf_pos;
}
bvn_unit_flags_t bvnr_writer_unit_flags(const bvnr_writer_t* w)
{
	if (!w) return BVN_UNIT_FLAGS_NONE;
	return w->ser.unit_flags;
}
