#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "bovnar.h"
#include "bvn_float.h"
#include "bvn_io_impl.h"
#include "bvn_val_impl.h"
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
		if (vt.family == vt_uint && buf[0] == '-') {
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
		} else if (vt.family == vt_sint && vt.width) {
			if (!bvn_validate_sint_range(buf, width, base)) {
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
		if (vt.family == vt_uint && buf[0] == '-') {
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
		} else if (vt.family == vt_sint && vt.width) {
			if (!bvn_validate_sint_range(buf, width, base)) {
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
static bool bvn_validate_type_spec_for_writer(bvnr_writer_t* w,
	value_type_spec_t vt)
{
	if (vt.family == vt_uint || vt.family == vt_sint) {
		if (vt.width > BVN_MAX_INT_WIDTH)
			return bvn_writer_set_error(w, error_illegal_value_type);
	}
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
	if (bvn_type_is_numeric(vt) && vt.base != 0) {
		if (!((vt.base >= 2 && vt.base <= 62) ||
			  vt.base == 64 || vt.base == 85)) {
			return bvn_writer_set_error(w,
				error_illegal_value_type);
		}
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
		} else if (d->type == token_is_number ||
				   d->type == token_is_array_number) {
			if (d->data && d->length) {
				const char* str = (const char*)d->data;
				if (bvn_is_special_number_string(str)) {
					if (!bvn_ser_push_byte(s, '$'))
						return false;
					if (!bvn_ser_push(s, d->data, d->length))
						return false;
					if (!bvn_ser_push_byte(s, '$'))
						return false;
				} else {
					if (!bvn_ser_push(s, d->data, d->length))
						return false;
				}
			}
		} else if (d->type == token_is_string ||
				   d->type == token_is_array_string) {
			if (!bvn_ser_serialize_string(s,
					(const uint8_t*)d->data, d->length))
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
bool bvnr_write_finish(bvnr_writer_t* w)
{
	if (!w) return false;
	/* A sticky error means an earlier event was rejected after the key
	 * and '=' were already emitted, leaving a dangling assignment in the
	 * sink.  Finishing cannot un-emit it, so report failure rather than
	 * claim success on truncated, unparseable output (mirrors the
	 * last_error guard at the top of bvnr_write_event). */
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
				return false;
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
