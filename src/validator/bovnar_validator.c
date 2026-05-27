#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "bovnar.h"
#include "bvn_val_impl.h"
#include "bovnar_currency.h"
bvnr_reader_t* bvnr_reader_create(void)
{
	bvnr_reader_t* r = malloc(sizeof(*r));
	if (!r) return NULL;
	memset(r, 0, sizeof(*r));
	return r;
}
void bvnr_reader_destroy(bvnr_reader_t* r)
{
	if (!r) return;
	bvn_lex_destroy(&r->lex);
	free(r);
}
void bvn_val_init(bvnr_validator_t* v, bvnr_read_flags_t* opts)
{
	memset(v, 0, sizeof(*v));
	v->value_type             = BVN_TYPE_PLAIN;
	v->parsed_unit            = BVN_UNIT_NO_PREFIX(bu_none);
	v->inferred_default_vtype = BVN_TYPE_PLAIN;
	if (opts) {
		v->userdata      = opts->userdata;
		v->on_unverified = opts->on_unverified;
		v->on_verified   = opts->on_verified;
		v->on_error      = opts->on_error;
	}
}
static const uint8_t bvn_empty_sentinel[1] = { 0 };
static inline void bvn_normalize_data_ptr(bvnr_data_t* d)
{
	if (d && d->data == NULL)
		d->data = bvn_empty_sentinel;
}
static inline bool bvn_emit_unverified(bvnr_reader_t* r,
									   bvnr_event_t ev, bvnr_data_t* d)
{
	bvnr_validator_t* v = &r->val;
	if (!v->on_unverified) return true;
	bvn_normalize_data_ptr(d);
	if (!v->on_unverified(v->userdata, ev, d)) {
		v->last_error = error_scanner_callback_failed;
		return false;
	}
	return true;
}
static inline bool bvn_emit_verified(bvnr_reader_t* r,
									 bvnr_event_t ev, bvnr_data_t* d)
{
	bvnr_validator_t* v = &r->val;
	if (!v->on_verified) return true;
	bvn_normalize_data_ptr(d);
	if (!v->on_verified(v->userdata, ev, d)) {
		v->last_error = error_scanner_callback_failed;
		return false;
	}
	return true;
}
static inline bool bvn_emit_both(bvnr_reader_t* r,
								 bvnr_event_t ev, bvnr_data_t* d)
{
	bvnr_validator_t* v = &r->val;
	if (!v->on_unverified && !v->on_verified) return true;
	if (!bvn_emit_unverified(r, ev, d)) return false;
	if (!bvn_emit_verified(r, ev, d))   return false;
	return true;
}
void bvn_acc_reset(bvnr_validator_t* v)
{
	v->acc_value     = 0;
	v->acc_overflow  = false;
	v->acc_has_dot   = false;
	v->acc_has_exp   = false;
	v->acc_exp_state = 0;
}
void bvn_acc_digit(bvnr_validator_t* v, uint32_t dv, uint32_t base)
{
	if (v->acc_overflow || v->acc_has_dot) return;
	if (base < 2u) return;
	uint64_t b = (uint64_t)base;
	uint64_t d = (uint64_t)dv;
	if (v->acc_value > (UINT64_MAX - d) / b)
		v->acc_overflow = true;
	else
		v->acc_value = v->acc_value * b + d;
}
static inline uint32_t bvn_effective_base_or_10(value_type_spec_t vt)
{
	if (vt.family == vt_float_fix || vt.family == vt_float_dec)
		return 10u;
	return vt.base ? vt.base : 10u;
}
static bool bvn_emit_default_type_annotation(bvnr_reader_t* r,
	token_type_t tt, const uint8_t* str, uint32_t str_len,
	bool is_currency_unit)
{
	bvnr_validator_t* v = &r->val;
	value_type_spec_t default_type;
	value_unit_t      default_unit = BVN_UNIT_NO_PREFIX(bu_none);
	const char*       family_name;
	uint32_t          family_name_len;
	bool              emit_width = false;
	bool              emit_base  = false;
	bool              emit_unit  = false;
	static const char no_unit_str[] = "no_unit";
	if (tt == token_is_string || tt == token_is_array_string) {
		if (!bvn_type_is_plain(v->inferred_default_vtype) &&
		    bvn_type_spec_eq(BVN_TYPE_UTF8, v->inferred_default_vtype)) {
			v->value_type    = BVN_TYPE_UTF8;
			v->parsed_unit   = BVN_UNIT_NO_PREFIX(bu_none);
			v->unit_data_len = 0;
			return true;
		}
		default_type    = BVN_TYPE_UTF8;
		family_name     = "utf8";
		family_name_len = 4;
	} else if (tt == token_is_number || tt == token_is_array_number) {
		bool has_dot = v->acc_has_dot;
		bool has_exp = v->acc_has_exp;
		bool is_neg  = (str_len > 0 && str[0] == '-');
		if (!is_currency_unit && (has_dot || has_exp)) {
			value_type_spec_t cand = {.family=vt_float,.width=64,.base=0};
			if (!bvn_type_is_plain(v->inferred_default_vtype) &&
			    bvn_type_spec_eq(cand, v->inferred_default_vtype)) {
				v->value_type    = cand;
				v->parsed_unit   = BVN_UNIT_NO_PREFIX(bu_none);
				v->unit_data_len = 7u;
				return true;
			}
			default_type.family = vt_float;
			default_type.width  = 64;
			default_type.base   = 0;
			family_name     = "float";
			family_name_len = 5;
			emit_width = emit_base = emit_unit = true;
		} else if (!is_currency_unit
		           && str_len > (uint32_t)is_neg
		           && (uint8_t)(str[(uint32_t)is_neg] - '0') <= 9u) {
			value_type_spec_t cand;
			cand.family = is_neg ? vt_sint : vt_uint;
			cand.width  = 64;
			cand.base   = 0;
			if (!bvn_type_is_plain(v->inferred_default_vtype) &&
			    bvn_type_spec_eq(cand, v->inferred_default_vtype)) {
				v->value_type    = cand;
				v->parsed_unit   = BVN_UNIT_NO_PREFIX(bu_none);
				v->unit_data_len = 7u;
				return true;
			}
			default_type    = cand;
			family_name     = is_neg ? "sint" : "uint";
			family_name_len = 4;
			emit_width = emit_base = emit_unit = true;
		} else {
			bool is_special = bvn_is_special_number_string(
				(const char*)str);
			if (is_currency_unit) {
				default_type.family = vt_float_dec;
				default_type.width  = 64;
				default_type.base   = 0;
				family_name     = "float_dec";
				family_name_len = 9;
				emit_width = emit_base = emit_unit = true;
			} else if (is_special) {
				default_type.family = vt_float;
				default_type.width  = 64;
				default_type.base   = 0;
				family_name     = "float";
				family_name_len = 5;
				emit_width = emit_base = emit_unit = true;
			} else if (is_neg) {
				default_type.family = vt_sint;
				default_type.width  = 64;
				default_type.base   = 0;
				family_name     = "sint";
				family_name_len = 4;
				emit_width = emit_base = emit_unit = true;
			} else {
				default_type.family = vt_uint;
				default_type.width  = 64;
				default_type.base   = 0;
				family_name     = "uint";
				family_name_len = 4;
				emit_width = emit_base = emit_unit = true;
			}
		}
	} else {
		return true;
	}
	if (!bvn_type_is_plain(v->inferred_default_vtype) &&
	    bvn_type_spec_eq(default_type, v->inferred_default_vtype)) {
		v->value_type    = default_type;
		v->parsed_unit   = default_unit;
		v->unit_data_len = emit_unit ? 7u : 0u;
		return true;
	}
	v->inferred_default_vtype = default_type;
	v->value_type    = default_type;
	v->parsed_unit   = default_unit;
	v->unit_data_len = emit_unit ? 7u : 0u;
	bvnr_data_t d = {0};
	d.type   = token_is_type;
	d.data   = family_name;
	d.length = family_name_len;
	if (!bvn_emit_unverified(r, ev_type_annotation_start, &d))
		return false;
	d.value_type = v->value_type;
	d.value_unit = v->parsed_unit;
	if (!bvn_emit_verified(r, ev_type_annotation_start, &d))
		return false;
	if (!bvn_emit_both(r, ev_type_annotation_type_family, &d))
		return false;
	if (emit_width) {
		d.type   = token_is_type_width;
		d.data   = NULL;
		d.length = 0;
		if (!bvn_emit_both(r,
				ev_type_annotation_type_family_parameter,
				&d))
			return false;
	}
	if (emit_base) {
		d.type   = token_is_type_base;
		d.data   = NULL;
		d.length = 0;
		if (!bvn_emit_both(r,
				ev_type_annotation_type_family_parameter,
				&d))
			return false;
	}
	if (emit_unit) {
		d.type   = token_is_unit;
		d.data   = no_unit_str;
		d.length = 7;
		if (!bvn_emit_both(r,
				ev_type_annotation_type_family_parameter,
				&d))
			return false;
	}
	d.type   = token_is_type;
	d.data   = family_name;
	d.length = family_name_len;
	if (!bvn_emit_both(r, ev_type_annotation_end, &d))
		return false;
	return true;
}
static bool bvn_acc_parse_number(bvnr_validator_t* v,
	const uint8_t* str, uint32_t len, token_type_t tt)
{
	value_type_spec_t vt   = v->value_type;
	uint32_t          base = bvn_effective_base_or_10(vt);
	bool is_neg = (len > 0 && str[0] == '-');
	if (vt.family == vt_uint && is_neg) {
		v->last_error = error_value_out_of_range;
		return false;
	}
	if (tt == token_is_string || tt == token_is_array_string) {
		bool needs_parse =
			(vt.family == vt_uint || vt.family == vt_sint);
		if (!needs_parse) return true;
		uint32_t start = is_neg ? 1u : 0u;
		for (uint32_t i = start; i < len; i++) {
			uint8_t b = str[i];
			uint32_t dv = bvn_char_to_digit((uint32_t)b, base);
			if (dv >= base) {
				v->last_error = error_digit_not_in_base;
				return false;
			}
			bvn_acc_digit(v, dv, base);
		}
		return true;
	}
	if (tt != token_is_number && tt != token_is_array_number)
		return true;
	if (bvn_is_special_number_string((const char*)str))
		return true;
	uint32_t start = is_neg ? 1u : 0u;
	if (base == 10u) {
		for (uint32_t i = start; i < len; i++) {
			uint8_t b = str[i];
			if (b == '.') { v->acc_has_dot = true; continue; }
			if (b == 'e' || b == 'E') {
				v->acc_has_exp = true;
				v->acc_exp_state = 1;
				continue;
			}
			if (v->acc_exp_state > 0) {
				if (v->acc_exp_state == 1 && (b == '+' || b == '-'))
					v->acc_exp_state = 2;
				else
					v->acc_exp_state = 2;
				continue;
			}
			uint32_t dv = (uint32_t)(b - (uint8_t)'0');
			if (dv > 9u) {
				v->last_error = error_digit_not_in_base;
				return false;
			}
			if (!v->acc_overflow && !v->acc_has_dot) {
				if (v->acc_value > (uint64_t)1844674407370955161ULL ||
				    (v->acc_value == (uint64_t)1844674407370955161ULL && dv > 5u))
					v->acc_overflow = true;
				else
					v->acc_value = v->acc_value * 10u + (uint64_t)dv;
			}
		}
		return true;
	}
	for (uint32_t i = start; i < len; i++) {
		uint8_t b = str[i];
		if (b == '.' ) { v->acc_has_dot = true; continue; }
		if ((b == 'e' || b == 'E') && base <= 14u) {
			v->acc_has_exp = true;
			v->acc_exp_state = 1;
			continue;
		}
		if (v->acc_exp_state > 0) {
			if (v->acc_exp_state == 1 && (b == '+' || b == '-')) {
				v->acc_exp_state = 2;
				continue;
			}
			v->acc_exp_state = 2;
			continue;
		}
		uint32_t dv = bvn_char_to_digit((uint32_t)b, base);
		if (dv >= base) {
			v->last_error = error_digit_not_in_base;
			return false;
		}
		bvn_acc_digit(v, dv, base);
	}
	return true;
}
bool bvn_check_acc_range(bvnr_validator_t* v,
	const uint8_t* str, uint32_t str_len, token_type_t tt)
{
	(void)tt;
	value_type_spec_t vt   = v->value_type;
	uint32_t          base = bvn_effective_base_or_10(vt);
	bool is_neg = (str_len > 0 && str[0] == '-');
	if (vt.family != vt_uint && vt.family != vt_sint)
		return true;
	if (bvn_is_special_number_string((const char*)str)) return true;
	uint32_t w = bvn_effective_width(vt);
	if (w > 64u) {
		if (vt.family == vt_uint)
			return bvn_validate_uint_range((const char*)str, w, base);
		return bvn_validate_sint_range((const char*)str, w, base);
	}
	if (v->acc_overflow) {
		v->last_error = error_value_out_of_range;
		return false;
	}
	uint64_t acc = v->acc_value;
	if (vt.family == vt_uint) {
		uint64_t max_val = (w >= 64u) ? UINT64_MAX : (1ULL << w) - 1ULL;
		if (acc > max_val) {
			v->last_error = error_value_out_of_range;
			return false;
		}
	} else {
		uint64_t max_mag;
		if (is_neg)
			max_mag = (w >= 64u) ? (1ULL << 63u) : (1ULL << (w - 1));
		else
			max_mag = (w >= 64u) ? (uint64_t)INT64_MAX : (1ULL << (w - 1)) - 1ULL;
		if (acc > max_mag) {
			v->last_error = error_value_out_of_range;
			return false;
		}
	}
	return true;
}
bool bvn_validate_type_value_compat(bvnr_reader_t* r,
	token_type_t tt, const uint8_t* str, uint32_t str_len)
{
	bvnr_validator_t* v  = &r->val;
	value_type_spec_t vt = v->value_type;
	if (bvn_type_is_plain(vt) || tt == token_is_null_value)
		return true;
	if (vt.family == vt_utf8) {
		if (tt != token_is_string && tt != token_is_array_string) {
			v->last_error = error_type_value_mismatch;
			return false;
		}
		return true;
	}
	if (!bvn_type_is_numeric(vt))
		return true;
	if (tt != token_is_number    && tt != token_is_array_number &&
		tt != token_is_string    && tt != token_is_array_string) {
		v->last_error = error_type_value_mismatch;
		return false;
	}
	if (tt == token_is_string || tt == token_is_array_string) {
		const char* s = (const char*)str;
		uint32_t base = bvn_effective_base(vt);
		if (!bvn_validate_number_in_base(s, base)) {
			v->last_error = error_digit_not_in_base;
			return false;
		}
	}
	if (v->acc_has_exp && vt.family != vt_float &&
		vt.family != vt_float_fix && vt.family != vt_float_dec) {
		v->last_error = error_type_value_mismatch;
		return false;
	}
	if (v->acc_has_dot && vt.family != vt_float &&
		vt.family != vt_float_fix && vt.family != vt_float_dec) {
		v->last_error = error_type_value_mismatch;
		return false;
	}
	return bvn_check_acc_range(v, str, str_len, tt);
}
bool bvn_val_receive(bvnr_reader_t* r, const bvnr_raw_token_t* raw)
{
	bvnr_validator_t* v = &r->val;
	token_type_t      tt = raw->type;
	if (tt == token_is_type) {
		bool type_ok = true, unit_ok = true, unit_too_long = false;
		value_unit_t parsed_unit = BVN_UNIT_NO_PREFIX(bu_none);
		uint8_t unit_buf[UINT8_MAX + 1];
		uint8_t ulen = 0;
		bvnr_data_t d = {0};
		d.type   = token_is_type;
		d.data   = raw->type_data;
		d.length = raw->type_len;
		if (!bvn_emit_unverified(r, ev_type_annotation_start, &d))
			return false;
		uint32_t tcache_hit_idx = (uint32_t)-1;
		for (uint32_t i = 0; i < v->tcache_count; i++) {
			const bvn_type_cache_slot_t* sl = &v->tcache[i];
			if (sl->key_len == raw->type_len &&
			    memcmp(sl->key, raw->type_data, raw->type_len) == 0) {
				tcache_hit_idx = i;
				break;
			}
		}
		if (tcache_hit_idx != (uint32_t)-1) {
			const bvn_type_cache_slot_t* sl =
				&v->tcache[tcache_hit_idx];
			v->value_type = sl->vtype;
			parsed_unit   = sl->unit;
			ulen          = sl->ubuf_len;
			if (ulen)
				memcpy(unit_buf, sl->ubuf, ulen);
			type_ok       = sl->type_ok;
			unit_ok       = sl->unit_ok;
			unit_too_long = sl->unit_too_long;
		} else {
			v->value_type = bvn_parse_type_annotation(
				raw->type_data, raw->type_len,
				&type_ok, &unit_ok, &unit_too_long, &parsed_unit,
				unit_buf, &ulen);
			if (raw->type_len <= BVN_TYPE_CACHE_KEY_CAP &&
			    ulen <= BVN_TYPE_CACHE_UBUF_CAP) {
				uint32_t idx;
				if (v->tcache_count < BVN_TYPE_CACHE_SLOTS) {
					idx = v->tcache_count;
					v->tcache_count = (uint8_t)(idx + 1u);
				} else {
					idx = v->tcache_next;
					v->tcache_next = (uint8_t)((idx + 1u)
						% BVN_TYPE_CACHE_SLOTS);
				}
				bvn_type_cache_slot_t* sl = &v->tcache[idx];
				sl->key_len       = (uint16_t)raw->type_len;
				memcpy(sl->key, raw->type_data, raw->type_len);
				sl->vtype         = v->value_type;
				sl->unit          = parsed_unit;
				sl->ubuf_len      = ulen;
				if (ulen)
					memcpy(sl->ubuf, unit_buf, ulen);
				sl->type_ok       = type_ok;
				sl->unit_ok       = unit_ok;
				sl->unit_too_long = unit_too_long;
			}
		}
		if (!type_ok) {
			v->last_error = error_illegal_value_type;
			return false;
		}
		v->parsed_unit   = parsed_unit;
		v->unit_data_len = ulen;
		if (ulen > 0)
			v->has_annotation_unit = true;
		d.value_type = v->value_type;
		d.value_unit = v->parsed_unit;
		if (!bvn_emit_verified(r, ev_type_annotation_start, &d))
			return false;
		if (!bvn_emit_both(r, ev_type_annotation_type_family, &d))
			return false;
		if (v->value_type.width != 0) {
			d.type   = token_is_type_width;
			d.data   = NULL;
			d.length = 0;
			if (!bvn_emit_both(r,
					ev_type_annotation_type_family_parameter,
					&d))
				return false;
		}
		if (v->value_type.base != 0) {
			d.type = (v->value_type.family == vt_float_fix)
				? token_is_type_q
				: token_is_type_base;
			d.data   = NULL;
			d.length = 0;
			if (!bvn_emit_both(r,
					ev_type_annotation_type_family_parameter,
					&d))
				return false;
		}
		if (ulen > 0) {
			if (v->value_type.family != vt_utf8 && !unit_ok) {
				v->last_error = unit_too_long
								? error_unit_too_long
								: error_unit_illegal;
				return false;
			}
			d.type   = token_is_unit;
			d.data   = unit_buf;
			d.length = ulen;
			if (!bvn_emit_both(r,
					ev_type_annotation_type_family_parameter,
					&d))
				return false;
		}
		d.type   = token_is_type;
		d.data   = raw->type_data;
		d.length = raw->type_len;
		if (!bvn_emit_both(r, ev_type_annotation_end, &d))
			return false;
		return true;
	}
	if (tt == token_is_null_value) {
		bvnr_data_t d = {0};
		d.type       = tt;
		d.value_type = v->value_type;
		d.value_unit = v->parsed_unit;
		return bvn_emit_both(r, raw->event, &d);
	}
	bool         iu_have = false;
	bool         iu_ok   = true;
	value_unit_t iu_unit = BVN_UNIT_NO_PREFIX(bu_none);
	if (raw->inline_unit_len > 0) {
		uint32_t iu_hit_idx = (uint32_t)-1;
		for (uint32_t i = 0; i < v->iucache_count; i++) {
			const bvn_iu_cache_slot_t* sl = &v->iucache[i];
			if (sl->key_len == raw->inline_unit_len &&
			    memcmp(sl->key, raw->inline_unit_data,
			           raw->inline_unit_len) == 0) {
				iu_hit_idx = i;
				break;
			}
		}
		if (iu_hit_idx != (uint32_t)-1) {
			iu_unit = v->iucache[iu_hit_idx].unit;
			iu_ok   = v->iucache[iu_hit_idx].ok;
		} else {
			iu_unit = bvn_parse_unit(raw->inline_unit_data, &iu_ok);
			if (raw->inline_unit_len <= BVN_IU_CACHE_KEY_CAP) {
				uint32_t idx;
				if (v->iucache_count < BVN_IU_CACHE_SLOTS) {
					idx = v->iucache_count;
					v->iucache_count = (uint8_t)(idx + 1u);
				} else {
					idx = v->iucache_next;
					v->iucache_next = (uint8_t)((idx + 1u)
						% BVN_IU_CACHE_SLOTS);
				}
				bvn_iu_cache_slot_t* sl = &v->iucache[idx];
				sl->key_len = (uint8_t)raw->inline_unit_len;
				memcpy(sl->key, raw->inline_unit_data,
				       raw->inline_unit_len);
				sl->unit = iu_unit;
				sl->ok   = iu_ok;
			}
		}
		iu_have = true;
	}
	if (bvn_type_is_plain(v->value_type) &&
		(tt == token_is_number || tt == token_is_array_number ||
		 tt == token_is_string || tt == token_is_array_string)) {
		bool is_currency_unit = false;
		if (iu_have &&
		    (tt == token_is_number || tt == token_is_array_number)) {
			if (iu_ok && iu_unit.num_components == 1 &&
			    bvn_unit_is_currency((int)iu_unit.components[0].base))
				is_currency_unit = true;
		}
		if (!bvn_emit_default_type_annotation(r, tt,
				raw->str_data, raw->str_len, is_currency_unit))
			return false;
	}
	if (tt == token_is_number || tt == token_is_array_number ||
		tt == token_is_string || tt == token_is_array_string) {
		bvn_acc_reset(v);
		if (!bvn_acc_parse_number(v, raw->str_data,
								  raw->str_len, tt))
			return false;
	}
	if (!bvn_validate_type_value_compat(r, tt,
										raw->str_data, raw->str_len))
		return false;
	if (iu_have) {
		if (!iu_ok) {
			v->last_error = error_unit_illegal;
			return false;
		}
		if (v->has_annotation_unit &&
			!bvn_unit_equal(v->parsed_unit, iu_unit)) {
			v->last_error = error_unit_mismatch;
			return false;
		}
		v->parsed_unit = iu_unit;
	}
	token_type_t dt = tt;
	bvnr_data_t d = {0};
	d.type       = dt;
	d.value_type = v->value_type;
	d.value_unit = v->parsed_unit;
	if (tt != token_is_null_value) {
		d.data   = raw->str_data;
		d.length = raw->str_len;
	}
	return bvn_emit_both(r, raw->event, &d);
}
bool bvn_val_receive_event(bvnr_reader_t* r, bvnr_event_t ev)
{
	bvnr_validator_t* v = &r->val;
	if (!v->on_unverified && !v->on_verified) return true;
	bvnr_data_t d = {0};
	d.value_type = v->value_type;
	d.value_unit = v->parsed_unit;
	return bvn_emit_both(r, ev, &d);
}
bool bvn_val_receive_octet_chunk(
	bvnr_reader_t* r, const uint8_t* data, uint32_t len)
{
	bvnr_data_t d = {0};
	d.type       = token_is_octet_stream;
	d.value_type = BVN_TYPE_PLAIN;
	d.data       = data;
	d.length     = len;
	return bvn_emit_both(r, ev_data, &d);
}
bool bvn_val_on_value_intro(bvnr_reader_t* r)
{
	bvnr_validator_t* v = &r->val;
	v->value_type             = BVN_TYPE_PLAIN;
	v->parsed_unit            = BVN_UNIT_NO_PREFIX(bu_none);
	v->inferred_default_vtype = BVN_TYPE_PLAIN;
	v->has_annotation_unit    = false;
	bvn_acc_reset(v);
	return true;
}
bool bvn_val_on_value_outro(bvnr_reader_t* r)
{
	bvnr_validator_t* v = &r->val;
	v->value_type             = BVN_TYPE_PLAIN;
	v->parsed_unit            = BVN_UNIT_NO_PREFIX(bu_none);
	v->inferred_default_vtype = BVN_TYPE_PLAIN;
	v->has_annotation_unit    = false;
	bvn_acc_reset(v);
	return true;
}
bool bvn_val_on_array_intro(bvnr_reader_t* r)
{
	bvnr_data_t d = {0};
	d.value_type = r->val.value_type;
	d.value_unit = r->val.parsed_unit;
	bvn_acc_reset(&r->val);
	r->val.has_annotation_unit = false;
	return bvn_emit_unverified(r, ev_array_row_start, &d) &&
		   bvn_emit_verified(r, ev_array_row_start, &d);
}
bool bvn_val_on_array_outro(bvnr_reader_t* r,
	uint64_t curr_row_size, uint64_t* array_row_size)
{
	bvnr_validator_t* v = &r->val;
	bvnr_data_t       d = {0};
	if (!*array_row_size)
		*array_row_size = curr_row_size;
	else if (*array_row_size != curr_row_size) {
		v->last_error = error_array_row_size_mismatch;
		return false;
	}
	return bvn_emit_both(r, ev_array_row_end, &d);
}
bool bvn_val_on_new_array_value(bvnr_reader_t* r,
	uint64_t curr_row_size, uint64_t array_row_size)
{
	bvnr_validator_t* v = &r->val;
	if (array_row_size && curr_row_size >= array_row_size) {
		v->last_error = error_array_row_size_mismatch;
		return false;
	}
	bvn_acc_reset(&r->val);
	v->has_annotation_unit = false;
	return true;
}
bool bvnr_read(bvnr_reader_t* r)
{
	return bvn_lex_run(r);
}
error_code_t bvnr_reader_get_error(const bvnr_reader_t* r)
{
	return r ? r->val.last_error : error_none;
}
uint64_t bvnr_reader_get_error_line(const bvnr_reader_t* r)
{
	return r ? r->val.error_line : 0;
}
uint64_t bvnr_reader_get_error_column(const bvnr_reader_t* r)
{
	return r ? r->val.error_column : 0;
}
uint32_t bvnr_reader_get_error_byte(const bvnr_reader_t* r)
{
	return r ? r->val.error_byte : 0;
}
uint64_t bvnr_reader_get_error_offset(const bvnr_reader_t* r)
{
	return r ? r->val.error_offset : 0;
}
uint64_t bvnr_reader_get_recovery_count(const bvnr_reader_t* r)
{
	return r ? r->lex.recovery_count : 0;
}
bool bvnr_open_read_source(
	bvnr_reader_t* r, const bvnr_source_t* src,
	const bvnr_sink_t* dbg_sink, bvnr_read_flags_t* options)
{
	if (!r)
		return false;
	bvn_lex_destroy(&r->lex);
	memset(&r->lex, 0, sizeof(r->lex));
	if (!src || !src->pull)
		return false;
	if (!bvn_lex_init(&r->lex, src, dbg_sink, options))
		return false;
	bvn_val_init(&r->val, options);
	return true;
}
bool bvnr_open_read_mem(
	bvnr_reader_t* r, const void* buf, uint64_t len,
	void* dbg_buf, uint64_t dbg_cap,
	bvnr_read_flags_t* options)
{
	bvnr_source_t src;
	bvnr_sink_t   dbg;
	bvnr_source_from_mem(&src, buf, len);
	if (dbg_buf && dbg_cap)
		bvnr_sink_to_mem(&dbg, dbg_buf, dbg_cap);
	return bvnr_open_read_source(r, &src,
		(dbg_buf && dbg_cap) ? &dbg : NULL, options);
}
