#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "bovnar.h"
#include "bvn_float.h"
#include "bvn_int.h"
#include "bvn_val_impl.h"
static bool emit_key(bvnr_writer_t *w, const char *key)
{
	if (!key)
		return bvn_writer_set_error(w, error_invalid_argument);
	bvnr_data_t d = {
		.type   = token_is_identifier,
		.data   = (const void *)key,
		.length = (uint32_t)strlen(key),
	};
	return bvnr_write_event(w, ev_assignment_start, &d);
}
bool bvnr_write_type_annotation(bvnr_writer_t *w,
				value_type_spec_t vt,
				value_unit_t vu)
{
	if (vu.num_components == 1u && vu.components[0].base == bu_none)
		vu = BVN_UNIT_NONE;
	bvnr_data_t d = {
		.type       = token_is_type,
		.value_type = vt,
		.value_unit = vu,
	};
	if (!bvnr_write_event(w, ev_type_annotation_start,       &d)) return false;
	if (!bvnr_write_event(w, ev_type_annotation_type_family, &d)) return false;
	if ((bvn_type_is_numeric(vt) || vt.family == vt_utf8) && vt.width != 0) {
		d.type   = token_is_type_width;
		d.data   = NULL;
		d.length = 0;
		if (!bvnr_write_event(w,
				ev_type_annotation_type_family_parameter, &d))
			return false;
	}
	if (vt.family == vt_float && vt.base != 0u && vt.base != 10u) {
		d.type   = token_is_type_base;
		d.data   = NULL;
		d.length = 0;
		if (!bvnr_write_event(w,
				ev_type_annotation_type_family_parameter, &d))
			return false;
	}
	if (vt.family == vt_sint || vt.family == vt_uint) {
		if (vt.base != 0u && vt.base != 10u) {
			d.type   = token_is_type_base;
			d.data   = NULL;
			d.length = 0;
			if (!bvnr_write_event(w,
					ev_type_annotation_type_family_parameter, &d))
				return false;
		}
	}
	if (vt.family == vt_float_fix && vt.base != 0u) {
		d.type   = token_is_type_q;
		d.data   = NULL;
		d.length = 0;
		if (!bvnr_write_event(w,
				ev_type_annotation_type_family_parameter, &d))
			return false;
	}
	if (vu.num_components > 0) {
		char ubuf[512];
		bvn_unit_flags_t uflags = bvnr_writer_unit_flags(w);
		int32_t ulen = bvn_unit_to_string_ex(vu, ubuf, sizeof(ubuf), uflags);
		if (ulen < 0) return bvn_writer_set_error(w, error_unit_too_long);
		d.type   = token_is_unit;
		d.data   = (const void *)ubuf;
		d.length = (uint32_t)ulen;
		if (!bvnr_write_event(w,
				ev_type_annotation_type_family_parameter, &d))
			return false;
	}
	d.type   = token_is_type;
	d.data   = NULL;
	d.length = 0;
	return bvnr_write_event(w, ev_type_annotation_end, &d);
}
static bool emit_numeric_tt(bvnr_writer_t *w,
			     value_type_spec_t vt, value_unit_t vu,
			     const char *valbuf, token_type_t tt)
{
	if (!bvnr_write_type_annotation(w, vt, vu)) return false;
	bvnr_data_t d = {
		.type       = tt,
		.value_type = vt,
		.value_unit = vu,
		.data       = (const void *)valbuf,
		.length     = (uint32_t)strlen(valbuf),
	};
	return bvnr_write_event(w, ev_data, &d);
}
static bool emit_numeric(bvnr_writer_t *w,
			 value_type_spec_t vt, value_unit_t vu,
			 const char *valbuf)
{
	return emit_numeric_tt(w, vt, vu, valbuf, token_is_number);
}
bool bvnr_write_string(bvnr_writer_t *w, const char *key, const char *value)
{
	if (!emit_key(w, key)) return false;
	bvnr_data_t d = {
		.type   = token_is_string,
		.data   = (const void *)value,
		.length = (uint32_t)strlen(value),
	};
	return bvnr_write_event(w, ev_data, &d);
}
bool bvnr_write_plain(bvnr_writer_t *w, const char *key, const char *value)
{
	if (!emit_key(w, key)) return false;
	bvnr_data_t d = {
		.type   = token_is_symbol,
		.data   = (const void *)value,
		.length = (uint32_t)strlen(value),
	};
	return bvnr_write_event(w, ev_data, &d);
}
bool bvnr_write_null(bvnr_writer_t *w, const char *key)
{
	if (!emit_key(w, key)) return false;
	bvnr_data_t d = { .type = token_is_null_value };
	return bvnr_write_event(w, ev_data, &d);
}
bool bvnr_write_bool(bvnr_writer_t *w, const char *key, bool value)
{
	if (!emit_key(w, key)) return false;
	const char *sym = value ? "true" : "false";
	bvnr_data_t d = {
		.type   = token_is_symbol,
		.data   = (const void *)sym,
		.length = (uint32_t)strlen(sym),
	};
	return bvnr_write_event(w, ev_data, &d);
}
bool bvnr_write_uint(bvnr_writer_t *w, const char *key,
			 uint32_t width, uint64_t value)
{
	return bvnr_write_uint_unit(w, key, width, value, BVN_UNIT_NONE);
}
bool bvnr_write_uint_unit(bvnr_writer_t *w, const char *key,
			   uint32_t width, uint64_t value, value_unit_t unit)
{
	if (!emit_key(w, key)) return false;
	value_type_spec_t vt = BVN_TYPE_UINT(width);
	char buf[32];
	if (bvn_format_uint64(buf, sizeof(buf), value, 10, 0) < 0)
	{
		w->val.last_error   = error_number_too_long;
		w->val.error_offset = bvnr_sink_bytes_written(&w->ser.sink)
		                    + (uint64_t)w->ser.wbuf_pos;
		return false;
	}
	return emit_numeric(w, vt, unit, buf);
}
bool bvnr_write_sint(bvnr_writer_t *w, const char *key,
			 uint32_t width, int64_t value)
{
	return bvnr_write_sint_unit(w, key, width, value, BVN_UNIT_NONE);
}
bool bvnr_write_sint_unit(bvnr_writer_t *w, const char *key,
			   uint32_t width, int64_t value, value_unit_t unit)
{
	if (!emit_key(w, key)) return false;
	value_type_spec_t vt = BVN_TYPE_SINT(width);
	char buf[32];
	if (bvn_format_int64(buf, sizeof(buf), value, 10, 0) < 0)
	{
		w->val.last_error   = error_number_too_long;
		w->val.error_offset = bvnr_sink_bytes_written(&w->ser.sink)
		                    + (uint64_t)w->ser.wbuf_pos;
		return false;
	}
	return emit_numeric(w, vt, unit, buf);
}
bool bvnr_write_float(bvnr_writer_t *w, const char *key,
			  uint32_t width, double value)
{
	return bvnr_write_float_unit(w, key, width, value, BVN_UNIT_NONE);
}
bool bvnr_write_float_unit(bvnr_writer_t *w, const char *key,
			    uint32_t width, double value, value_unit_t unit)
{
	if (!emit_key(w, key)) return false;
	if (width > 64u)
		return bvn_writer_set_error(w, error_illegal_value_type);
	value_type_spec_t vt = BVN_TYPE_FLOAT(width);
	if (width != 0u && width <= 32u) value = (double)(float)value;
	char buf[64];
	if (bvn_format_double(buf, sizeof(buf), value, vt) < 0)
	{
		w->val.last_error   = error_number_too_long;
		w->val.error_offset = bvnr_sink_bytes_written(&w->ser.sink)
		                    + (uint64_t)w->ser.wbuf_pos;
		return false;
	}
	return emit_numeric(w, vt, unit, buf);
}
bool bvnr_write_float_fix(bvnr_writer_t *w, const char *key,
			   uint32_t width, uint32_t q, double value)
{
	return bvnr_write_float_fix_unit(w, key, width, q, value, BVN_UNIT_NONE);
}
bool bvnr_write_float_fix_unit(bvnr_writer_t *w, const char *key,
			        uint32_t width, uint32_t q,
			        double value, value_unit_t unit)
{
	if (!emit_key(w, key)) return false;
	if (width > 64u)
		return bvn_writer_set_error(w, error_illegal_value_type);
	value_type_spec_t vt = BVN_TYPE_FLOAT_FIX(width, q);
	if (width != 0u && width <= 32u) value = (double)(float)value;
	char buf[64];
	if (bvn_format_double(buf, sizeof(buf), value, vt) < 0)
	{
		w->val.last_error   = error_number_too_long;
		w->val.error_offset = bvnr_sink_bytes_written(&w->ser.sink)
		                    + (uint64_t)w->ser.wbuf_pos;
		return false;
	}
	return emit_numeric(w, vt, unit, buf);
}
bool bvnr_write_float_dec(bvnr_writer_t *w, const char *key,
			   uint32_t width, double value)
{
	return bvnr_write_float_dec_unit(w, key, width, value, BVN_UNIT_NONE);
}
bool bvnr_write_float_dec_unit(bvnr_writer_t *w, const char *key,
			        uint32_t width,
			        double value, value_unit_t unit)
{
	if (!emit_key(w, key)) return false;
	if (width > 64u)
		return bvn_writer_set_error(w, error_illegal_value_type);
	value_type_spec_t vt = BVN_TYPE_FLOAT_DEC(width);
	if (width != 0u && width <= 32u) value = (double)(float)value;
	char buf[64];
	if (bvn_format_double(buf, sizeof(buf), value, vt) < 0)
	{
		w->val.last_error   = error_number_too_long;
		w->val.error_offset = bvnr_sink_bytes_written(&w->ser.sink)
		                    + (uint64_t)w->ser.wbuf_pos;
		return false;
	}
	return emit_numeric(w, vt, unit, buf);
}
bool bvnr_write_struct_start(bvnr_writer_t *w, const char *key)
{
	if (!emit_key(w, key)) return false;
	bvnr_data_t d = {0};
	return bvnr_write_event(w, ev_struct_start, &d);
}
bool bvnr_write_struct_end(bvnr_writer_t *w)
{
	bvnr_data_t d = {0};
	return bvnr_write_event(w, ev_struct_end, &d);
}
bool bvnr_write_bvnf_unit(bvnr_writer_t *w, const char *key,
			   const bvn_float_t *f,
			   uint32_t width, value_unit_t unit)
{
	return bvnr_write_bvnf_base_unit(w, key, f, width, 10u, unit);
}
bool bvnr_write_bvnf(bvnr_writer_t *w, const char *key,
			 const bvn_float_t *f, uint32_t width)
{
	return bvnr_write_bvnf_base_unit(w, key, f, width, 10u, BVN_UNIT_NONE);
}
bool bvnr_write_bvnf_base_unit(bvnr_writer_t *w, const char *key,
				const bvn_float_t *f,
				uint32_t width, uint32_t base,
				value_unit_t unit)
{
	if (!f) return false;
	uint32_t effective_base = base ? base : 10u;
	if (effective_base != 10u && effective_base != 16u) return false;
	if (!emit_key(w, key)) return false;
	uint32_t prec = width ? width : (uint32_t)f->_prec;
	value_type_spec_t vt = BVN_TYPE_FLOAT_BASE(prec,
		effective_base == 10u ? 0u : 16u);
	size_t bufsz = bvn_float_str_bufsize(prec, effective_base);
	char *buf = malloc(bufsz);
	if (!buf) return false;
	int32_t n = bvn_float_to_str(f, buf, bufsz, effective_base);
	bool ok = false;
	if (n > 0) {
		token_type_t tt = (effective_base == 10u)
			? token_is_number : token_is_string;
		ok = emit_numeric_tt(w, vt, unit, buf, tt);
	}
	free(buf);
	return ok;
}
bool bvnr_write_bvnf_base(bvnr_writer_t *w, const char *key,
			   const bvn_float_t *f,
			   uint32_t width, uint32_t base)
{
	return bvnr_write_bvnf_base_unit(w, key, f, width, base, BVN_UNIT_NONE);
}
bool bvnr_write_bvni_unit(bvnr_writer_t *w, const char *key,
			   const bvn_int_t *n,
			   uint32_t width, uint32_t base,
			   value_unit_t unit)
{
	if (!n) return false;
	uint32_t effective_base = base ? base : 10u;
	if (!emit_key(w, key)) return false;
	value_type_family_t fam = n->negative ? vt_sint : vt_uint;
	value_type_spec_t vt;
	if (fam == vt_sint)
		vt = BVN_TYPE_SINT_BASE(width, effective_base == 10u ? 0u : effective_base);
	else
		vt = BVN_TYPE_UINT_BASE(width, effective_base == 10u ? 0u : effective_base);
	uint32_t actual_bits = n->nused * 32u;
	uint32_t bits = width ? width : (actual_bits ? actual_bits : 64u);
	if (actual_bits > bits)
		bits = actual_bits;
	size_t bufsz = bvn_int_str_bufsize(bits, effective_base);
	char *buf = malloc(bufsz);
	if (!buf) return bvn_writer_set_error(w, error_invalid_argument);
	int32_t nr = bvn_int_to_str(n, buf, bufsz, effective_base);
	bool ok = false;
	if (nr > 0) {
		token_type_t tt = (effective_base == 10u)
			? token_is_number : token_is_string;
		ok = emit_numeric_tt(w, vt, unit, buf, tt);
	} else {
		bvn_writer_set_error(w, error_value_out_of_range);
	}
	free(buf);
	return ok;
}
bool bvnr_write_bvni(bvnr_writer_t *w, const char *key,
		     const bvn_int_t *n,
		     uint32_t width, uint32_t base)
{
	return bvnr_write_bvni_unit(w, key, n, width, base, BVN_UNIT_NONE);
}
