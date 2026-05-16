#ifndef _POSIX_C_SOURCE
#  define _POSIX_C_SOURCE 200809L
#endif
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <math.h>
#include "bovnar.h"
#include "bvn_internal_dims.h"
#include "bvn_float.h"
#include "bovnar_si_units.h"
#include "bvn_unit_impl.h"
#define BVN_DEC_SCRATCH_SIZE 10000u
uint32_t bvn_char_to_digit(uint32_t c, uint32_t base)
{
	if (base == 64u) {
		if (c >= 'A' && c <= 'Z') return c - 'A';
		if (c >= 'a' && c <= 'z') return c - 'a' + 26u;
		if (c >= '0' && c <= '9') return c - '0' + 52u;
		if (c == '+')             return 62u;
		if (c == '/')             return 63u;
		return base;
	}
	if (base == 85u) {
		if (c >= '!' && c <= 'u') return c - '!';
		return base;
	}
	uint32_t d;
	if      (c >= '0' && c <= '9') d = c - '0';
	else if (c >= 'a' && c <= 'z') d = 10u + (c - 'a');
	else if (c >= 'A' && c <= 'Z') d = (base > 36u) ? 36u + (c - 'A')
													 : 10u + (c - 'A');
	else return base;
	return d < base ? d : base;
}
static uint32_t bvn_digit_to_char(uint32_t d, uint32_t base)
{
	if (base == 64u) {
		static const char b64[] =
			"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"
			"0123456789+/";
		return (d < 64u) ? (uint32_t)(uint8_t)b64[d] : 0u;
	}
	if (base == 85u)
		return (d < 85u) ? ('!' + d) : 0u;
	if (d <  10u) return '0' + d;
	if (d <  36u) return 'a' + (d - 10u);
	if (d <  62u) return 'A' + (d - 36u);
	return 0u;
}
bool bvn_is_special_number_string(const char* s)
{
	if (!s) return false;
	return strcmp(s, "nan")      == 0
		|| strcmp(s, "infinity") == 0
		|| strcmp(s, "-infinity") == 0;
}
bool bvn_validate_digits_for_base(const char* s, uint32_t base)
{
	if (!s || !*s) return false;
	uint32_t i = 0;
	if (s[0] == '-' || s[0] == '+') i = 1;
	if (!s[i]) return false;
	for (; s[i]; i++) {
		if (bvn_char_to_digit((uint8_t)s[i], base) >= base)
			return false;
	}
	return true;
}
bool bvn_validate_number_in_base(const char* s, uint32_t base)
{
	if (!s || !*s) return false;
	uint32_t i = 0;
	if (s[0] == '-' || s[0] == '+') i = 1;
	if (!s[i]) return false;
	bool has_dot = false, has_exp = false;
	bool has_mant_digit = false, has_exp_digit = false;
	for (; s[i]; i++) {
		char c = s[i];
		if (c == '.') {
			if (has_dot || has_exp) return false;
			has_dot = true;
			continue;
		}
		if (c == 'e' || c == 'E' || (base == 16u && (c == 'p' || c == 'P'))) {
			if (has_exp || !has_mant_digit) return false;
			has_exp = true;
			if (s[i + 1] == '+' || s[i + 1] == '-') i++;
			continue;
		}
		if (c == '+' || c == '-') return false;
		if (bvn_char_to_digit((uint8_t)c, base) >= base)
			return false;
		if (has_exp)
			has_exp_digit = true;
		else
			has_mant_digit = true;
	}
	return has_mant_digit && (!has_exp || has_exp_digit);
}
static int bvn_pow2m1_dec(uint32_t n, char* buf, size_t cap)
{
	if (n == 0) {
		buf[0] = '0'; buf[1] = '\0';
		return 1;
	}
	uint8_t *dig = malloc(BVN_DEC_SCRATCH_SIZE);
	if (!dig) return -1;
	uint32_t nd = 1;
	dig[0] = 1;
	for (uint32_t i = 0; i < n; i++) {
		uint16_t carry = 0;
		for (uint32_t j = 0; j < nd; j++) {
			uint16_t v = (uint16_t)((uint16_t)dig[j] * 2u + carry);
			dig[j] = (uint8_t)(v % 10);
			carry  = v / 10;
		}
		while (carry && nd < BVN_DEC_SCRATCH_SIZE) {
			dig[nd++] = (uint8_t)(carry % 10);
			carry /= 10;
		}
	}
	int borrow = 1;
	for (uint32_t j = 0; j < nd && borrow; j++) {
		int v = (int)dig[j] - borrow;
		if (v < 0) { dig[j] = (uint8_t)(v + 10); borrow = 1; }
		else       { dig[j] = (uint8_t)v;         borrow = 0; }
	}
	while (nd > 1 && dig[nd - 1] == 0) nd--;
	uint32_t len = nd;
	if (len + 1 > cap) len = (uint32_t)cap - 1;
	for (uint32_t j = 0; j < len; j++)
		buf[j] = (char)('0' + dig[nd - 1 - j]);
	buf[len] = '\0';
	free(dig);
	return (int)len;
}
static int bvn_cmp_dec(const char* a, const char* b)
{
	while (*a == '0' && a[1]) a++;
	while (*b == '0' && b[1]) b++;
	size_t la = strlen(a), lb = strlen(b);
	if (la < lb) return -1;
	if (la > lb) return  1;
	return strcmp(a, b);
}
static bool bvn_digits_to_dec(const char* src, uint32_t base,
	char* buf, size_t cap)
{
	uint8_t *dec = malloc(BVN_DEC_SCRATCH_SIZE);
	if (!dec) return false;
	uint32_t nd = 1;
	dec[0] = 0;
	for (; *src; src++) {
		uint32_t d = bvn_char_to_digit((uint8_t)*src, base);
		if (d >= base) { free(dec); return false; }
		uint32_t carry = d;
		for (uint32_t i = 0; i < nd; i++) {
			uint32_t v = (uint32_t)dec[i] * base + carry;
			dec[i] = (uint8_t)(v % 10u);
			carry  = v / 10u;
		}
		while (carry) {
			if (nd >= BVN_DEC_SCRATCH_SIZE) { free(dec); return false; }
			dec[nd++] = (uint8_t)(carry % 10u);
			carry /= 10u;
		}
	}
	if (nd + 1u > cap) { free(dec); return false; }
	for (uint32_t i = 0; i < nd; i++)
		buf[i] = (char)('0' + dec[nd - 1u - i]);
	buf[nd] = '\0';
	free(dec);
	return true;
}
bool bvn_validate_uint_range(const char* s, uint32_t w, uint32_t base)
{
	if (!s) return true;
	if (w == 0) return true;
	if (bvn_is_special_number_string(s)) return true;
	if (s[0] == '-') return false;
	const char* p = s;
	if (*p == '+') p++;
	if (w <= 64) {
		uint64_t maxv = (w >= 64) ? UINT64_MAX : (1ULL << w) - 1ULL;
		if (base == 10) {
			char* end;
			errno = 0;
			uint64_t v = strtoull(p, &end, 10);
			if (*end || errno == ERANGE) return false;
			return v <= maxv;
		}
		uint64_t v = 0;
		for (; *p; p++) {
			uint32_t d = bvn_char_to_digit((uint8_t)*p, base);
			if (d >= base) return false;
			if (v > (UINT64_MAX - d) / base) return false;
			v = v * base + d;
		}
		return v <= maxv;
	}
	if (base == 10) {
		char *maxs = malloc(BVN_DEC_SCRATCH_SIZE);
		if (!maxs) return false;
		int r = bvn_pow2m1_dec(w, maxs, BVN_DEC_SCRATCH_SIZE);
		bool ok = (r >= 0) && (bvn_cmp_dec(p, maxs) <= 0);
		free(maxs);
		return ok;
	}
	{
		char *dec  = malloc(BVN_DEC_SCRATCH_SIZE);
		char *maxs = malloc(BVN_DEC_SCRATCH_SIZE);
		if (!dec || !maxs) { free(dec); free(maxs); return false; }
		bool ok = false;
		if (bvn_digits_to_dec(p, base, dec, BVN_DEC_SCRATCH_SIZE)) {
			int r = bvn_pow2m1_dec(w, maxs, BVN_DEC_SCRATCH_SIZE);
			ok = (r >= 0) && (bvn_cmp_dec(dec, maxs) <= 0);
		}
		free(dec);
		free(maxs);
		return ok;
	}
}
bool bvn_validate_sint_range(const char* s, uint32_t w, uint32_t base)
{
	if (!s) return true;
	if (w == 0) return true;
	if (bvn_is_special_number_string(s)) return true;
	bool neg = (s[0] == '-');
	const char* abs = s;
	if (neg || s[0] == '+') abs++;
	if (w <= 64) {
		if (base == 10) {
			char* end;
			errno = 0;
			int64_t v = strtoll(s, &end, 10);
			if (*end || errno == ERANGE) return false;
			int64_t lo = (w >= 64)
				? INT64_MIN
				: -(int64_t)(1ULL << (w - 1));
			int64_t hi = (w >= 64)
				? INT64_MAX
				: (int64_t)((1ULL << (w - 1)) - 1ULL);
			return v >= lo && v <= hi;
		}
		uint64_t v = 0;
		for (const char* p = abs; *p; p++) {
			uint32_t d = bvn_char_to_digit((uint8_t)*p, base);
			if (d >= base) return false;
			if (v > (UINT64_MAX - d) / base) return false;
			v = v * base + d;
		}
		uint64_t mag = neg
			? ((w >= 64) ? (1ULL << 63) : (1ULL << (w - 1)))
			: ((w >= 64) ? (uint64_t)INT64_MAX : (1ULL << (w - 1)) - 1ULL);
		return v <= mag;
	}
	if (base == 10) {
		char *maxs = malloc(BVN_DEC_SCRATCH_SIZE);
		if (!maxs) return false;
		int r = bvn_pow2m1_dec(w - 1, maxs, BVN_DEC_SCRATCH_SIZE);
		if (r < 0) { free(maxs); return false; }
		if (!neg) {
			bool ok = bvn_cmp_dec(abs, maxs) <= 0;
			free(maxs);
			return ok;
		}
		char *tmp = malloc(BVN_DEC_SCRATCH_SIZE);
		if (!tmp) { free(maxs); return false; }
		strncpy(tmp, maxs, BVN_DEC_SCRATCH_SIZE - 1u);
		tmp[BVN_DEC_SCRATCH_SIZE - 1u] = '\0';
		int len = (int)strlen(tmp);
		int carry = 1;
		for (int i = len - 1; i >= 0 && carry; i--) {
			int d = (tmp[i] - '0') + carry;
			tmp[i] = (char)('0' + (d % 10));
			carry = d / 10;
		}
		if (carry) {
			memmove(tmp + 1, tmp, (size_t)len + 1);
			tmp[0] = '1';
		}
		bool ok = bvn_cmp_dec(abs, tmp) <= 0;
		free(maxs);
		free(tmp);
		return ok;
	}
	{
		char *dec  = malloc(BVN_DEC_SCRATCH_SIZE);
		char *maxs = malloc(BVN_DEC_SCRATCH_SIZE);
		if (!dec || !maxs) { free(dec); free(maxs); return false; }
		bool ok = false;
		if (!bvn_digits_to_dec(abs, base, dec, BVN_DEC_SCRATCH_SIZE))
			goto sint_bigint_done;
		int r = bvn_pow2m1_dec(w - 1, maxs, BVN_DEC_SCRATCH_SIZE);
		if (r < 0)
			goto sint_bigint_done;
		if (!neg) {
			ok = bvn_cmp_dec(dec, maxs) <= 0;
			goto sint_bigint_done;
		}
		{
			char *tmp = malloc(BVN_DEC_SCRATCH_SIZE);
			if (!tmp) goto sint_bigint_done;
			strncpy(tmp, maxs, BVN_DEC_SCRATCH_SIZE - 1u);
			tmp[BVN_DEC_SCRATCH_SIZE - 1u] = '\0';
			int len = (int)strlen(tmp);
			int carry = 1;
			for (int i = len - 1; i >= 0 && carry; i--) {
				int d = (tmp[i] - '0') + carry;
				tmp[i] = (char)('0' + (d % 10));
				carry = d / 10;
			}
			if (carry) {
				memmove(tmp + 1, tmp, (size_t)len + 1);
				tmp[0] = '1';
			}
			ok = bvn_cmp_dec(dec, tmp) <= 0;
			free(tmp);
		}
sint_bigint_done:
		free(dec);
		free(maxs);
		return ok;
	}
}
const char* bvn_error_to_string(error_code_t code)
{
	switch (code) {
	case error_none:                      return "none";
	case error_unknown_token_type:        return "unknown_token_type";
	case error_array_row_size_mismatch:   return "array_row_size_mismatch";
	case error_identifier_too_long:       return "identifier_too_long";
	case error_empty_identifier:          return "empty_identifier";
	case error_struct_nesting_too_high:   return "struct_nesting_too_high";
	case error_array_nesting_too_high:    return "array_nesting_too_high";
	case error_illegal_struct_close:      return "illegal_struct_close";
	case error_string_too_long:           return "string_too_long";
	case error_illegal_escape_sequence:   return "illegal_escape_sequence";
	case error_number_too_long:           return "number_too_long";
	case error_symbol_too_long:           return "symbol_too_long";
	case error_reference_too_long:        return "reference_too_long";
	case error_read_complete_chunk_failed:return "read_complete_chunk_failed";
	case error_octet_stream_out_of_sync:  return "octet_stream_out_of_sync";
	case error_unexpected_input_byte:     return "unexpected_input_byte";
	case error_text_data_too_long:        return "text_data_too_long";
	case error_reading_from_source_fd:    return "reading_from_source_fd";
	case error_got_incomplete_bvnr_stream:return "incomplete_bvnr_stream";
	case error_invalid_utf8_byte:         return "invalid_utf8_byte";
	case error_invalid_byte_order_mark:   return "invalid_byte_order_mark";
	case error_type_too_long:             return "type_too_long";
	case error_unit_too_long:             return "unit_too_long";
	case error_expected_string_in_array:  return "expected_string_in_array";
	case error_expected_number_in_array:  return "expected_number_in_array";
	case error_illegal_value_type:        return "illegal_value_type";
	case error_scanner_callback_failed:   return "scanner_callback_failed";
	case error_file_too_long:             return "file_too_long";
	case error_invalid_argument:          return "invalid_argument";
	case error_too_many_array_items:      return "too_many_array_items";
	case error_writing_to_sink:           return "writing_to_sink";
	case error_sink_buffer_exhausted:     return "sink_buffer_exhausted";
	case error_unit_illegal:              return "unit_illegal";
	case error_base_requires_string_literal: return "base_requires_string_literal";
	case error_type_value_mismatch:       return "type_value_mismatch";
	case error_value_out_of_range:        return "value_out_of_range";
	case error_digit_not_in_base:         return "digit_not_in_base";
	case error_recovered:                 return "recovered";
	case error_unit_mismatch:             return "unit_mismatch";
	default:                              return "unknown_error";
	}
}
typedef struct { const char* a; uint32_t len; value_base_unit_t u; } bu_entry_t;
typedef struct { const char* a; uint32_t len; si_prefix_id_t   p; } si_entry_t;
typedef struct { const char* a; uint32_t len; iec_prefix_id_t  p; } iec_entry_t;
static const bu_entry_t bu_table[] = {
	{"electronvolts", 13, bu_electronvolt},
	{"electronvolt",  12, bu_electronvolt},
	{"steradians",    10, bu_steradian},  {"becquerels",   10, bu_becquerel},
	{"steradian",      9, bu_steradian},  {"becquerel",     9, bu_becquerel},
	{"hectares",       8, bu_hectare},    {"candelas",      8, bu_candela},
	{"coulombs",       8, bu_coulomb},    {"sieverts",      8, bu_sievert},
	{"radians",        7, bu_radian},     {"daltons",       7, bu_dalton},
	{"seconds",        7, bu_second},     {"amperes",       7, bu_ampere},
	{"kelvins",        7, bu_kelvin},     {"candela",       7, bu_candela},
	{"newtons",        7, bu_newton},     {"pascals",       7, bu_pascal},
	{"coulomb",        7, bu_coulomb},    {"siemens",       7, bu_siemens},
	{"henries",        7, bu_henry},      {"celsius",       7, bu_celsius},
	{"minutes",        7, bu_minute},     {"hectare",       7, bu_hectare},
	{"sievert",        7, bu_sievert},    {"degrees",       7, bu_degree},
	{"radian",         6, bu_radian},     {"tonnes",        6, bu_tonne},
	{"dalton",         6, bu_dalton},     {"second",        6, bu_second},
	{"metres",         6, bu_meter},      {"meters",        6, bu_meter},
	{"ampere",         6, bu_ampere},     {"kelvin",        6, bu_kelvin},
	{"newton",         6, bu_newton},     {"pascal",        6, bu_pascal},
	{"joules",         6, bu_joule},      {"farads",        6, bu_farad},
	{"webers",         6, bu_weber},      {"teslas",        6, bu_tesla},
	{"henrys",         6, bu_henry},      {"lumens",        6, bu_lumen},
	{"katals",         6, bu_katal},      {"litres",        6, bu_liter},
	{"liters",         6, bu_liter},      {"minute",        6, bu_minute},
	{"degree",         6, bu_degree},
	{"tonne",          5, bu_tonne},      {"metre",         5, bu_meter},
	{"meter",          5, bu_meter},      {"grams",         5, bu_gram},
	{"joule",          5, bu_joule},      {"farad",         5, bu_farad},
	{"weber",          5, bu_weber},      {"tesla",         5, bu_tesla},
	{"henry",          5, bu_henry},      {"lumen",         5, bu_lumen},
	{"katal",          5, bu_katal},      {"litre",         5, bu_liter},
	{"liter",          5, bu_liter},      {"weeks",         5, bu_week},
	{"years",          5, bu_year},       {"moles",         5, bu_mol},
	{"hertz",          5, bu_hertz},      {"watts",         5, bu_watt},
	{"volts",          5, bu_volt},       {"grays",         5, bu_gray},
	{"hours",          5, bu_hour},       {"Bytes",         5, bu_byte},
	{"bytes",          5, bu_byte},       {"degrC",         5, bu_celsius},
	{"bars",           4, bu_bar},        {"week",          4, bu_week},
	{"year",           4, bu_year},       {"gram",          4, bu_gram},
	{"amps",           4, bu_ampere},     {"mole",          4, bu_mol},
	{"watt",           4, bu_watt},       {"volt",          4, bu_volt},
	{"ohms",           4, bu_ohm},        {"days",          4, bu_day},
	{"hour",           4, bu_hour},       {"Byte",          4, bu_byte},
	{"byte",           4, bu_byte},
	{"bits",           4, bu_bit},        {"gray",          4, bu_gray},
	{"degC",           4, bu_celsius},     {"degr",          4, bu_degree},
	{"\xc2\xb0""C",   3, bu_celsius},
	{"\xe2\x84\xa6",  3, bu_ohm},
	{"deg",            3, bu_degree},
	{"bar",            3, bu_bar},        {"sec",           3, bu_second},
	{"amp",            3, bu_ampere},     {"ohm",           3, bu_ohm},
	{"lux",            3, bu_lux},        {"day",           3, bu_day},
	{"bit",            3, bu_bit},        {"kat",           3, bu_katal},
	{"min",            3, bu_minute},     {"mol",           3, bu_mol},
	{"rad",            3, bu_radian},
	{"Bq",             2, bu_becquerel},  {"Gy",            2, bu_gray},
	{"Hz",             2, bu_hertz},      {"Pa",            2, bu_pascal},
	{"Sv",             2, bu_sievert},    {"Wb",            2, bu_weber},
	{"cd",             2, bu_candela},    {"lm",            2, bu_lumen},
	{"lx",             2, bu_lux},        {"sr",            2, bu_steradian},
	{"ha",             2, bu_hectare},    {"wk",            2, bu_week},
	{"yr",             2, bu_year},       {"au",            2, bu_astronomical_unit},
	{"Da",             2, bu_dalton},     {"eV",            2, bu_electronvolt},
	{"\xc2\xb0",      2, bu_degree},
	{"B",              1, bu_byte},       {"C",             1, bu_coulomb},
	{"F",              1, bu_farad},      {"H",             1, bu_henry},
	{"J",              1, bu_joule},      {"K",             1, bu_kelvin},
	{"L",              1, bu_liter},      {"N",             1, bu_newton},
	{"l",              1, bu_liter},
	{"S",              1, bu_siemens},    {"T",             1, bu_tesla},
	{"V",              1, bu_volt},       {"W",             1, bu_watt},
	{"A",              1, bu_ampere},     {"b",             1, bu_bit},
	{"d",              1, bu_day},        {"g",             1, bu_gram},
	{"h",              1, bu_hour},       {"m",             1, bu_meter},
	{"s",              1, bu_second},     {"t",             1, bu_tonne},
	{NULL, 0, bu_none}
};
static const si_entry_t si_table[] = {
	{"Q",  1, si_quetta}, {"R",  1, si_ronna},  {"Y",  1, si_yotta},
	{"Z",  1, si_zetta},  {"E",  1, si_exa},    {"P",  1, si_peta},
	{"T",  1, si_tera},   {"G",  1, si_giga},   {"M",  1, si_mega},
	{"k",  1, si_kilo},   {"h",  1, si_hecto},  {"da", 2, si_deca},
	{"d",  1, si_deci},   {"c",  1, si_centi},  {"m",  1, si_milli},
	{"\xc2\xb5", 2, si_micro},
	{"n",  1, si_nano},   {"p",  1, si_pico},   {"f",  1, si_femto},
	{"a",  1, si_atto},   {"z",  1, si_zepto},  {"y",  1, si_yocto},
	{"r",  1, si_ronto},  {"q",  1, si_quecto},
	{NULL, 0, si_none}
};
static const iec_entry_t iec_table[] = {
	{"Qi", 2, iec_quebi}, {"Ri", 2, iec_robi},
	{"Yi", 2, iec_yobi},  {"Zi", 2, iec_zebi},  {"Ei", 2, iec_exbi},
	{"Pi", 2, iec_pebi},  {"Ti", 2, iec_tebi},  {"Gi", 2, iec_gibi},
	{"Mi", 2, iec_mebi},  {"Ki", 2, iec_kibi},
	{NULL, 0, iec_none}
};
static uint32_t parse_unit_exponent_suffix(
	const char* s, uint32_t len, unit_exponent_t* exp)
{
	if (len >= 2 && s[len - 2] == '^') {
		char d = s[len - 1];
		if (d >= '1' && d <= '9') {
			static const unit_exponent_t pos_tab[10] = {
				exp_invalid,  exp_linear,  exp_square,  exp_cubic,
				exp_quartic,  exp_quintic, exp_sextic,  exp_septic,
				exp_octic,    exp_nonic
			};
			*exp = pos_tab[(uint8_t)(d - '0')];
			return 2;
		}
	}
	if (len >= 3 && s[len - 3] == '^' &&
		(s[len - 2] == '+' || s[len - 2] == '-')) {
		char sign = s[len - 2];
		char d    = s[len - 1];
		if (d >= '1' && d <= '9') {
			static const unit_exponent_t pos_tab[10] = {
				exp_invalid,  exp_linear,  exp_square,  exp_cubic,
				exp_quartic,  exp_quintic, exp_sextic,  exp_septic,
				exp_octic,    exp_nonic
			};
			static const unit_exponent_t neg_tab[10] = {
				exp_invalid,  exp_neg_linear, exp_neg_square,
				exp_neg_cubic, exp_neg_quartic, exp_neg_quintic,
				exp_neg_sextic, exp_neg_septic, exp_neg_octic,
				exp_neg_nonic
			};
			uint8_t idx = (uint8_t)(d - '0');
			*exp = (sign == '-') ? neg_tab[idx] : pos_tab[idx];
			return 3;
		}
	}
	unit_exponent_t base_exp = exp_linear;
	uint32_t        dig_len  = 0;
	if (len >= 3 &&
		(uint8_t)s[len - 3] == 0xE2 &&
		(uint8_t)s[len - 2] == 0x81) {
		switch ((uint8_t)s[len - 1]) {
		case 0xB4: base_exp = exp_quartic;  dig_len = 3; break;
		case 0xB5: base_exp = exp_quintic;  dig_len = 3; break;
		case 0xB6: base_exp = exp_sextic;   dig_len = 3; break;
		case 0xB7: base_exp = exp_septic;   dig_len = 3; break;
		case 0xB8: base_exp = exp_octic;    dig_len = 3; break;
		case 0xB9: base_exp = exp_nonic;    dig_len = 3; break;
		default:   break;
		}
	}
	if (!dig_len && len >= 2 && (uint8_t)s[len - 2] == 0xC2) {
		switch ((uint8_t)s[len - 1]) {
		case 0xB9: base_exp = exp_linear; dig_len = 2; break;
		case 0xB2: base_exp = exp_square; dig_len = 2; break;
		case 0xB3: base_exp = exp_cubic;  dig_len = 2; break;
		default:   break;
		}
	}
	if (!dig_len)
		return 0;
	uint32_t pos      = len - dig_len;
	bool     negative = false;
	uint32_t sign_len = 0;
	if (pos >= 3 &&
		(uint8_t)s[pos - 3] == 0xE2 &&
		(uint8_t)s[pos - 2] == 0x81) {
		uint8_t b = (uint8_t)s[pos - 1];
		if      (b == 0xBB) { negative = true;  sign_len = 3; }
		else if (b == 0xBA) { negative = false; sign_len = 3; }
	}
	if (negative) {
		switch (base_exp) {
		case exp_linear:  base_exp = exp_neg_linear;  break;
		case exp_square:  base_exp = exp_neg_square;  break;
		case exp_cubic:   base_exp = exp_neg_cubic;   break;
		case exp_quartic: base_exp = exp_neg_quartic; break;
		case exp_quintic: base_exp = exp_neg_quintic; break;
		case exp_sextic:  base_exp = exp_neg_sextic;  break;
		case exp_septic:  base_exp = exp_neg_septic;  break;
		case exp_octic:   base_exp = exp_neg_octic;   break;
		case exp_nonic:   base_exp = exp_neg_nonic;   break;
		default:          break;
		}
	}
	*exp = base_exp;
	return dig_len + sign_len;
}
static unit_exponent_t bvn_negate_exponent(unit_exponent_t e)
{
	switch (e) {
	case exp_linear:      return exp_neg_linear;
	case exp_square:      return exp_neg_square;
	case exp_cubic:       return exp_neg_cubic;
	case exp_quartic:     return exp_neg_quartic;
	case exp_quintic:     return exp_neg_quintic;
	case exp_sextic:      return exp_neg_sextic;
	case exp_septic:      return exp_neg_septic;
	case exp_octic:       return exp_neg_octic;
	case exp_nonic:       return exp_neg_nonic;
	case exp_neg_linear:  return exp_linear;
	case exp_neg_square:  return exp_square;
	case exp_neg_cubic:   return exp_cubic;
	case exp_neg_quartic: return exp_quartic;
	case exp_neg_quintic: return exp_quintic;
	case exp_neg_sextic:  return exp_sextic;
	case exp_neg_septic:  return exp_septic;
	case exp_neg_octic:   return exp_octic;
	case exp_neg_nonic:   return exp_nonic;
	default:              return e;
	}
}
static value_unit_component_t bvn_parse_single_unit_component(
	const char* s, uint32_t len, bool* ok)
{
	value_unit_component_t r = {
		.base = bu_none,
		.exponent = exp_linear,
		.prefix.system = prefix_si,
		.prefix.id.si  = si_none
	};
	*ok = true;
	if (!s || !len) { *ok = false; return r; }
	uint32_t suf = parse_unit_exponent_suffix(s, len, &r.exponent);
	len -= suf;
	if (len == 0) { *ok = false; return r; }
	const bu_entry_t* best = NULL;
	for (const bu_entry_t* e = bu_table; e->a; e++) {
		if (e->len > len)
			continue;
		if (memcmp(s + len - e->len, e->a, e->len) == 0) {
			best = e;
			break;
		}
	}
	if (!best) { *ok = false; return r; }
	r.base = best->u;
	uint32_t plen = len - best->len;
	if (plen == 0) {
		r.prefix.system = prefix_si;
		r.prefix.id.si  = si_none;
		return r;
	}
	if (plen < 2 || s[plen - 1] != '-') {
		*ok = false;
		return r;
	}
	uint32_t plen2 = plen - 1;
	for (const iec_entry_t* e = iec_table; e->a; e++) {
		if (e->len == plen2 &&
			memcmp(s, e->a, plen2) == 0) {
			r.prefix.system = prefix_iec;
			r.prefix.id.iec = e->p;
			if (!bvn_prefix_unit_valid(r.prefix, r.base)) {
				*ok = false;
			}
			return r;
		}
	}
	for (const si_entry_t* e = si_table; e->a; e++) {
		if (e->len == plen2 &&
			memcmp(s, e->a, plen2) == 0) {
			r.prefix.system = prefix_si;
			r.prefix.id.si  = e->p;
			if (!bvn_prefix_unit_valid(r.prefix, r.base)) {
				*ok = false;
			}
			return r;
		}
	}
	*ok = false;
	return r;
}
value_unit_t bvn_parse_unit_n(const uint8_t* unit, uint32_t len, bool* ok)
{
	value_unit_t result = { .num_components = 0 };
	*ok = true;
	if (!unit || !len) { *ok = false; return result; }
	const char* s    = (const char*)unit;
	uint32_t    slen = len;
	if (slen == 7 && memcmp(s, "no_unit", 7) == 0) {
		return result;
	}
	bool has_sep = false;
	for (uint32_t i = 0; i < slen; ) {
		uint8_t b = (uint8_t)s[i];
		if (b == '*' || b == '/') { has_sep = true; break; }
		if (b == 0xC2 && i + 1 < slen && (uint8_t)s[i + 1] == 0xB7)
			{ has_sep = true; break; }
		i++;
	}
	if (!has_sep) {
		bool comp_ok;
		value_unit_component_t comp =
			bvn_parse_single_unit_component(s, slen, &comp_ok);
		if (!comp_ok) { *ok = false; return result; }
		result.num_components = 1;
		result.components[0]  = comp;
		return result;
	}
	bool     in_denominator = false;
	uint32_t comp_start     = 0;
	uint32_t i              = 0;
	while (i <= slen) {
		bool     at_end      = (i == slen);
		bool     is_sep      = false;
		uint32_t sep_len     = 0;
		bool     is_division = false;
		if (!at_end) {
			uint8_t b = (uint8_t)s[i];
			if (b == '*') {
				is_sep  = true;
				sep_len = 1;
			} else if (b == '/') {
				is_sep      = true;
				sep_len     = 1;
				is_division = true;
			} else if (b == 0xC2 && i + 1 < slen &&
					   (uint8_t)s[i + 1] == 0xB7) {
				is_sep  = true;
				sep_len = 2;
			}
		}
		if (at_end || is_sep) {
			uint32_t comp_len = i - comp_start;
			if (comp_len > 0) {
				if (result.num_components >= BVNR_MAX_UNIT_COMPONENTS) {
					*ok = false;
					return result;
				}
				bool comp_ok;
				value_unit_component_t comp =
					bvn_parse_single_unit_component(
						s + comp_start, comp_len, &comp_ok);
				if (!comp_ok) { *ok = false; return result; }
				if (in_denominator) {
					comp.exponent =
						bvn_negate_exponent(comp.exponent);
				}
				result.components[result.num_components++] = comp;
			} else if (!at_end || comp_start > 0) {
				*ok = false;
				return result;
			}
			if (is_division)
				in_denominator = true;
			comp_start = i + sep_len;
		}
		if (is_sep)
			i += sep_len;
		else
			i++;
	}
	if (result.num_components == 0)
		*ok = false;
	return result;
}
value_unit_t bvn_parse_unit(const uint8_t* unit, bool* ok)
{
	if (!unit) { *ok = false; return (value_unit_t){ .num_components = 0 }; }
	return bvn_parse_unit_n(unit, (uint32_t)strlen((const char*)unit), ok);
}
static const char* si_prefix_str(si_prefix_id_t p)
{
	switch (p) {
	case si_quecto: return "q";  case si_ronto: return "r";
	case si_yocto:  return "y";  case si_zepto: return "z";
	case si_atto:   return "a";  case si_femto: return "f";
	case si_pico:   return "p";  case si_nano:  return "n";
	case si_micro:  return "\xc2\xb5";
	case si_milli:  return "m";  case si_centi: return "c";
	case si_deci:   return "d";  case si_deca:  return "da";
	case si_hecto:  return "h";  case si_kilo:  return "k";
	case si_mega:   return "M";  case si_giga:  return "G";
	case si_tera:   return "T";  case si_peta:  return "P";
	case si_exa:    return "E";  case si_zetta: return "Z";
	case si_yotta:  return "Y";  case si_ronna: return "R";
	case si_quetta: return "Q";  default:       return "";
	}
}
static const char* iec_prefix_str(iec_prefix_id_t p)
{
	switch (p) {
	case iec_kibi: return "Ki"; case iec_mebi: return "Mi";
	case iec_gibi: return "Gi"; case iec_tebi: return "Ti";
	case iec_pebi: return "Pi"; case iec_exbi: return "Ei";
	case iec_zebi: return "Zi"; case iec_yobi: return "Yi";
	case iec_robi: return "Ri"; case iec_quebi: return "Qi";
	default:       return "";
	}
}
static const char* base_unit_str(value_base_unit_t b)
{
	switch (b) {
	case bu_bit:       return "b";
	case bu_byte:      return "B";
	case bu_second:    return "s";
	case bu_meter:     return "m";
	case bu_gram:      return "g";
	case bu_ampere:    return "A";
	case bu_kelvin:    return "K";
	case bu_mol:       return "mol";
	case bu_candela:   return "cd";
	case bu_hertz:     return "Hz";
	case bu_newton:    return "N";
	case bu_pascal:    return "Pa";
	case bu_joule:     return "J";
	case bu_watt:      return "W";
	case bu_volt:      return "V";
	case bu_ohm:       return "\xe2\x84\xa6";
	case bu_farad:     return "F";
	case bu_coulomb:   return "C";
	case bu_siemens:   return "S";
	case bu_weber:     return "Wb";
	case bu_tesla:     return "T";
	case bu_henry:     return "H";
	case bu_lumen:     return "lm";
	case bu_lux:       return "lx";
	case bu_becquerel: return "Bq";
	case bu_gray:      return "Gy";
	case bu_sievert:   return "Sv";
	case bu_katal:     return "kat";
	case bu_liter:     return "L";
	case bu_minute:    return "min";
	case bu_hour:      return "h";
	case bu_day:       return "d";
	case bu_degree:    return "\xc2\xb0";
	case bu_celsius:   return "\xc2\xb0""C";
	case bu_radian:            return "rad";
	case bu_steradian:         return "sr";
	case bu_tonne:             return "t";
	case bu_bar:               return "bar";
	case bu_electronvolt:      return "eV";
	case bu_dalton:            return "Da";
	case bu_astronomical_unit: return "au";
	case bu_hectare:           return "ha";
	case bu_week:              return "wk";
	case bu_year:              return "yr";
	default:           return "";
	}
}
static int32_t bvn_write_exponent_suffix(
	char* buf, size_t bufsize, unit_exponent_t e)
{
	int32_t pos = 0;
	switch (e) {
	case exp_linear:
		break;
	case exp_square:
		if (pos + 2 >= (int32_t)bufsize) return -1;
		buf[pos++] = (char)0xc2;
		buf[pos++] = (char)0xb2;
		break;
	case exp_cubic:
		if (pos + 2 >= (int32_t)bufsize) return -1;
		buf[pos++] = (char)0xc2;
		buf[pos++] = (char)0xb3;
		break;
	case exp_quartic:
		if (pos + 3 >= (int32_t)bufsize) return -1;
		buf[pos++] = (char)0xe2;
		buf[pos++] = (char)0x81;
		buf[pos++] = (char)0xb4;
		break;
	case exp_quintic:
		if (pos + 3 >= (int32_t)bufsize) return -1;
		buf[pos++] = (char)0xe2;
		buf[pos++] = (char)0x81;
		buf[pos++] = (char)0xb5;
		break;
	case exp_sextic:
		if (pos + 3 >= (int32_t)bufsize) return -1;
		buf[pos++] = (char)0xe2;
		buf[pos++] = (char)0x81;
		buf[pos++] = (char)0xb6;
		break;
	case exp_septic:
		if (pos + 3 >= (int32_t)bufsize) return -1;
		buf[pos++] = (char)0xe2;
		buf[pos++] = (char)0x81;
		buf[pos++] = (char)0xb7;
		break;
	case exp_octic:
		if (pos + 3 >= (int32_t)bufsize) return -1;
		buf[pos++] = (char)0xe2;
		buf[pos++] = (char)0x81;
		buf[pos++] = (char)0xb8;
		break;
	case exp_nonic:
		if (pos + 3 >= (int32_t)bufsize) return -1;
		buf[pos++] = (char)0xe2;
		buf[pos++] = (char)0x81;
		buf[pos++] = (char)0xb9;
		break;
	case exp_neg_linear:
		if (pos + 5 >= (int32_t)bufsize) return -1;
		buf[pos++] = (char)0xe2;
		buf[pos++] = (char)0x81;
		buf[pos++] = (char)0xbb;
		buf[pos++] = (char)0xc2;
		buf[pos++] = (char)0xb9;
		break;
	case exp_neg_square:
		if (pos + 5 >= (int32_t)bufsize) return -1;
		buf[pos++] = (char)0xe2;
		buf[pos++] = (char)0x81;
		buf[pos++] = (char)0xbb;
		buf[pos++] = (char)0xc2;
		buf[pos++] = (char)0xb2;
		break;
	case exp_neg_cubic:
		if (pos + 5 >= (int32_t)bufsize) return -1;
		buf[pos++] = (char)0xe2;
		buf[pos++] = (char)0x81;
		buf[pos++] = (char)0xbb;
		buf[pos++] = (char)0xc2;
		buf[pos++] = (char)0xb3;
		break;
	case exp_neg_quartic:
		if (pos + 6 >= (int32_t)bufsize) return -1;
		buf[pos++] = (char)0xe2;
		buf[pos++] = (char)0x81;
		buf[pos++] = (char)0xbb;
		buf[pos++] = (char)0xe2;
		buf[pos++] = (char)0x81;
		buf[pos++] = (char)0xb4;
		break;
	case exp_neg_quintic:
		if (pos + 6 >= (int32_t)bufsize) return -1;
		buf[pos++] = (char)0xe2;
		buf[pos++] = (char)0x81;
		buf[pos++] = (char)0xbb;
		buf[pos++] = (char)0xe2;
		buf[pos++] = (char)0x81;
		buf[pos++] = (char)0xb5;
		break;
	case exp_neg_sextic:
		if (pos + 6 >= (int32_t)bufsize) return -1;
		buf[pos++] = (char)0xe2;
		buf[pos++] = (char)0x81;
		buf[pos++] = (char)0xbb;
		buf[pos++] = (char)0xe2;
		buf[pos++] = (char)0x81;
		buf[pos++] = (char)0xb6;
		break;
	case exp_neg_septic:
		if (pos + 6 >= (int32_t)bufsize) return -1;
		buf[pos++] = (char)0xe2;
		buf[pos++] = (char)0x81;
		buf[pos++] = (char)0xbb;
		buf[pos++] = (char)0xe2;
		buf[pos++] = (char)0x81;
		buf[pos++] = (char)0xb7;
		break;
	case exp_neg_octic:
		if (pos + 6 >= (int32_t)bufsize) return -1;
		buf[pos++] = (char)0xe2;
		buf[pos++] = (char)0x81;
		buf[pos++] = (char)0xbb;
		buf[pos++] = (char)0xe2;
		buf[pos++] = (char)0x81;
		buf[pos++] = (char)0xb8;
		break;
	case exp_neg_nonic:
		if (pos + 6 >= (int32_t)bufsize) return -1;
		buf[pos++] = (char)0xe2;
		buf[pos++] = (char)0x81;
		buf[pos++] = (char)0xbb;
		buf[pos++] = (char)0xe2;
		buf[pos++] = (char)0x81;
		buf[pos++] = (char)0xb9;
		break;
	case exp_invalid:
		return -1;
	}
	buf[pos] = '\0';
	return pos;
}
static int32_t bvn_write_exponent_suffix_ascii(
	char* buf, size_t bufsize, unit_exponent_t e)
{
	int32_t pos = 0;
	switch (e) {
	case exp_linear:
		break;
	case exp_square:
		if (pos + 2 >= (int32_t)bufsize) return -1;
		buf[pos++] = '^'; buf[pos++] = '2';
		break;
	case exp_cubic:
		if (pos + 2 >= (int32_t)bufsize) return -1;
		buf[pos++] = '^'; buf[pos++] = '3';
		break;
	case exp_quartic:
		if (pos + 2 >= (int32_t)bufsize) return -1;
		buf[pos++] = '^'; buf[pos++] = '4';
		break;
	case exp_quintic:
		if (pos + 2 >= (int32_t)bufsize) return -1;
		buf[pos++] = '^'; buf[pos++] = '5';
		break;
	case exp_sextic:
		if (pos + 2 >= (int32_t)bufsize) return -1;
		buf[pos++] = '^'; buf[pos++] = '6';
		break;
	case exp_septic:
		if (pos + 2 >= (int32_t)bufsize) return -1;
		buf[pos++] = '^'; buf[pos++] = '7';
		break;
	case exp_octic:
		if (pos + 2 >= (int32_t)bufsize) return -1;
		buf[pos++] = '^'; buf[pos++] = '8';
		break;
	case exp_nonic:
		if (pos + 2 >= (int32_t)bufsize) return -1;
		buf[pos++] = '^'; buf[pos++] = '9';
		break;
	case exp_neg_linear:
		if (pos + 3 >= (int32_t)bufsize) return -1;
		buf[pos++] = '^'; buf[pos++] = '-'; buf[pos++] = '1';
		break;
	case exp_neg_square:
		if (pos + 3 >= (int32_t)bufsize) return -1;
		buf[pos++] = '^'; buf[pos++] = '-'; buf[pos++] = '2';
		break;
	case exp_neg_cubic:
		if (pos + 3 >= (int32_t)bufsize) return -1;
		buf[pos++] = '^'; buf[pos++] = '-'; buf[pos++] = '3';
		break;
	case exp_neg_quartic:
		if (pos + 3 >= (int32_t)bufsize) return -1;
		buf[pos++] = '^'; buf[pos++] = '-'; buf[pos++] = '4';
		break;
	case exp_neg_quintic:
		if (pos + 3 >= (int32_t)bufsize) return -1;
		buf[pos++] = '^'; buf[pos++] = '-'; buf[pos++] = '5';
		break;
	case exp_neg_sextic:
		if (pos + 3 >= (int32_t)bufsize) return -1;
		buf[pos++] = '^'; buf[pos++] = '-'; buf[pos++] = '6';
		break;
	case exp_neg_septic:
		if (pos + 3 >= (int32_t)bufsize) return -1;
		buf[pos++] = '^'; buf[pos++] = '-'; buf[pos++] = '7';
		break;
	case exp_neg_octic:
		if (pos + 3 >= (int32_t)bufsize) return -1;
		buf[pos++] = '^'; buf[pos++] = '-'; buf[pos++] = '8';
		break;
	case exp_neg_nonic:
		if (pos + 3 >= (int32_t)bufsize) return -1;
		buf[pos++] = '^'; buf[pos++] = '-'; buf[pos++] = '9';
		break;
	case exp_invalid:
		return -1;
	}
	buf[pos] = '\0';
	return pos;
}
static int32_t bvn_write_unit_component(
	char* buf, size_t bufsize, const value_unit_component_t* c)
{
	int32_t pos = 0;
	bool has_prefix = false;
	if (c->prefix.system == prefix_iec && c->prefix.id.iec != iec_none) {
		const char* p = iec_prefix_str(c->prefix.id.iec);
		size_t plen = strlen(p);
		if (pos + (int32_t)plen >= (int32_t)bufsize) return -1;
		memcpy(buf + pos, p, plen);
		pos += (int32_t)plen;
		has_prefix = true;
	} else if (c->prefix.system == prefix_si && c->prefix.id.si != si_none) {
		const char* p = si_prefix_str(c->prefix.id.si);
		size_t plen = strlen(p);
		if (pos + (int32_t)plen >= (int32_t)bufsize) return -1;
		memcpy(buf + pos, p, plen);
		pos += (int32_t)plen;
		has_prefix = true;
	}
	if (has_prefix) {
		if (pos + 1 >= (int32_t)bufsize) return -1;
		buf[pos++] = '-';
	}
	{
		const char* bu = base_unit_str(c->base);
		size_t bulen = strlen(bu);
		if (pos + (int32_t)bulen >= (int32_t)bufsize) return -1;
		memcpy(buf + pos, bu, bulen);
		pos += (int32_t)bulen;
	}
	int32_t w = bvn_write_exponent_suffix(
		buf + pos, bufsize - (size_t)pos,
		c->exponent);
	if (w < 0) return -1;
	pos += w;
	return pos;
}
static int32_t bvn_write_unit_component_ex(
	char* buf, size_t bufsize,
	const value_unit_component_t* c,
	bvn_unit_flags_t flags)
{
	int32_t pos = 0;
	bool has_prefix = false;
	if (c->prefix.system == prefix_iec && c->prefix.id.iec != iec_none) {
		const char* p = iec_prefix_str(c->prefix.id.iec);
		size_t plen = strlen(p);
		if (pos + (int32_t)plen >= (int32_t)bufsize) return -1;
		memcpy(buf + pos, p, plen);
		pos += (int32_t)plen;
		has_prefix = true;
	} else if (c->prefix.system == prefix_si && c->prefix.id.si != si_none) {
		const char* p = si_prefix_str(c->prefix.id.si);
		size_t plen = strlen(p);
		if (pos + (int32_t)plen >= (int32_t)bufsize) return -1;
		memcpy(buf + pos, p, plen);
		pos += (int32_t)plen;
		has_prefix = true;
	}
	if (has_prefix) {
		if (pos + 1 >= (int32_t)bufsize) return -1;
		buf[pos++] = '-';
	}
	{
		const char* bu = base_unit_str(c->base);
		size_t bulen = strlen(bu);
		if (pos + (int32_t)bulen >= (int32_t)bufsize) return -1;
		memcpy(buf + pos, bu, bulen);
		pos += (int32_t)bulen;
	}
	int32_t w;
	if (flags & BVN_UNIT_ASCII_EXP)
		w = bvn_write_exponent_suffix_ascii(buf + pos,
		                                    bufsize - (size_t)pos,
		                                    c->exponent);
	else
		w = bvn_write_exponent_suffix(buf + pos,
		                               bufsize - (size_t)pos,
		                               c->exponent);
	if (w < 0) return -1;
	pos += w;
	return pos;
}
bool bvn_unit_valid(value_unit_t u)
{
	if (u.num_components > BVNR_MAX_UNIT_COMPONENTS)
		return false;
	for (uint32_t i = 0; i < u.num_components; i++) {
		const value_unit_component_t *c = &u.components[i];
		if (c->exponent == exp_invalid)
			return false;
		if ((uint32_t)c->base >= BVN_VALUE_BASE_UNIT_COUNT)
			return false;
		if (!bvn_prefix_unit_valid(c->prefix, c->base))
			return false;
	}
	return true;
}
int32_t bvn_unit_to_string_ex(value_unit_t u, char* buf, size_t bufsize,
                               bvn_unit_flags_t flags)
{
	if (!buf || bufsize < 1)
		return -1;
	if (flags & BVN_UNIT_REDUCE) {
		double   scale;
		bool     overflow;
		if (!bvn_unit_valid(u))
			return -1;
		u = bvn_unit_reduce(u, &scale, &overflow);
	} else {
		if (!bvn_unit_valid(u))
			return -1;
	}
	uint32_t nc = u.num_components < BVNR_MAX_UNIT_COMPONENTS
	            ? u.num_components : BVNR_MAX_UNIT_COMPONENTS;
	if (nc == 0) {
		if (bufsize < 8)
			return -1;
		memcpy(buf, "no_unit", 7);
		buf[7] = '\0';
		return 7;
	}
	if (nc == 1
		&& u.components[0].base == bu_none
		&& u.components[0].prefix.system == prefix_si
		&& u.components[0].prefix.id.si == si_none
		&& u.components[0].exponent == exp_linear) {
		if (bufsize < 8)
			return -1;
		memcpy(buf, "no_unit", 7);
		buf[7] = '\0';
		return 7;
	}
	bool ascii = (flags & BVN_UNIT_ASCII_EXP) != 0;
	if (nc == 1) {
		return bvn_write_unit_component_ex(
			buf, bufsize, &u.components[0], flags);
	}
	uint32_t num_num = 0, num_den = 0;
	uint32_t num_idx[BVNR_MAX_UNIT_COMPONENTS];
	uint32_t den_idx[BVNR_MAX_UNIT_COMPONENTS];
	for (uint32_t i = 0; i < nc; i++) {
		if (bvni_is_neg_exp(u.components[i].exponent))
			den_idx[num_den++] = i;
		else
			num_idx[num_num++] = i;
	}
	int32_t pos = 0;
	if (num_num > 0 && num_den > 0) {
		for (uint32_t n = 0; n < num_num; n++) {
			if (n > 0) {
				if (ascii) {
					if (pos + 1 >= (int32_t)bufsize) return -1;
					buf[pos++] = '*';
				} else {
					if (pos + 2 >= (int32_t)bufsize) return -1;
					buf[pos++] = (char)0xc2;
					buf[pos++] = (char)0xb7;
				}
			}
			int32_t w = bvn_write_unit_component_ex(
				buf + pos, bufsize - (size_t)pos,
				&u.components[num_idx[n]], flags);
			if (w < 0) return -1;
			pos += w;
		}
		if (pos + 1 >= (int32_t)bufsize) return -1;
		buf[pos++] = '/';
		for (uint32_t d = 0; d < num_den; d++) {
			if (d > 0) {
				if (ascii) {
					if (pos + 1 >= (int32_t)bufsize) return -1;
					buf[pos++] = '*';
				} else {
					if (pos + 2 >= (int32_t)bufsize) return -1;
					buf[pos++] = (char)0xc2;
					buf[pos++] = (char)0xb7;
				}
			}
			value_unit_component_t tmp = u.components[den_idx[d]];
			tmp.exponent = bvn_negate_exponent(tmp.exponent);
			int32_t w = bvn_write_unit_component_ex(
				buf + pos, bufsize - (size_t)pos,
				&tmp, flags);
			if (w < 0) return -1;
			pos += w;
		}
	} else {
		for (uint32_t i = 0; i < nc; i++) {
			if (i > 0) {
				if (ascii) {
					if (pos + 1 >= (int32_t)bufsize) return -1;
					buf[pos++] = '*';
				} else {
					if (pos + 2 >= (int32_t)bufsize) return -1;
					buf[pos++] = (char)0xc2;
					buf[pos++] = (char)0xb7;
				}
			}
			int32_t w = bvn_write_unit_component_ex(
				buf + pos, bufsize - (size_t)pos,
				&u.components[i], flags);
			if (w < 0) return -1;
			pos += w;
		}
	}
	buf[pos] = '\0';
	return pos;
}
int32_t bvn_unit_to_string(value_unit_t u, char* buf, size_t bufsize)
{
	if (!buf || bufsize < 1)
		return -1;
	if (!bvn_unit_valid(u))
		return -1;
	uint32_t nc = u.num_components < BVNR_MAX_UNIT_COMPONENTS
	            ? u.num_components : BVNR_MAX_UNIT_COMPONENTS;
	if (nc == 0) {
		if (bufsize < 8)
			return -1;
		memcpy(buf, "no_unit", 7);
		buf[7] = '\0';
		return 7;
	}
	if (nc == 1
		&& u.components[0].base == bu_none
		&& u.components[0].prefix.system == prefix_si
		&& u.components[0].prefix.id.si == si_none
		&& u.components[0].exponent == exp_linear) {
		if (bufsize < 8)
			return -1;
		memcpy(buf, "no_unit", 7);
		buf[7] = '\0';
		return 7;
	}
	if (nc == 1) {
		return bvn_write_unit_component(
			buf, bufsize, &u.components[0]);
	}
	uint32_t num_num = 0, num_den = 0;
	uint32_t num_idx[BVNR_MAX_UNIT_COMPONENTS];
	uint32_t den_idx[BVNR_MAX_UNIT_COMPONENTS];
	for (uint32_t i = 0; i < nc; i++) {
		if (bvni_is_neg_exp(u.components[i].exponent)) {
			den_idx[num_den++] = i;
		} else {
			num_idx[num_num++] = i;
		}
	}
	int32_t pos = 0;
	if (num_num > 0 && num_den > 0) {
		for (uint32_t n = 0; n < num_num; n++) {
			if (n > 0) {
				if (pos + 2 >= (int32_t)bufsize) return -1;
				buf[pos++] = (char)0xc2;
				buf[pos++] = (char)0xb7;
			}
			int32_t w = bvn_write_unit_component(
				buf + pos, bufsize - (size_t)pos,
				&u.components[num_idx[n]]);
			if (w < 0) return -1;
			pos += w;
		}
		if (pos + 1 >= (int32_t)bufsize) return -1;
		buf[pos++] = '/';
		for (uint32_t d = 0; d < num_den; d++) {
			if (d > 0) {
				if (pos + 2 >= (int32_t)bufsize) return -1;
				buf[pos++] = (char)0xc2;
				buf[pos++] = (char)0xb7;
			}
			value_unit_component_t tmp = u.components[den_idx[d]];
			tmp.exponent = bvn_negate_exponent(tmp.exponent);
			int32_t w = bvn_write_unit_component(
				buf + pos, bufsize - (size_t)pos,
				&tmp);
			if (w < 0) return -1;
			pos += w;
		}
	} else {
		for (uint32_t i = 0; i < nc; i++) {
			if (i > 0) {
				if (pos + 2 >= (int32_t)bufsize) return -1;
				buf[pos++] = (char)0xc2;
				buf[pos++] = (char)0xb7;
			}
			int32_t w = bvn_write_unit_component(
				buf + pos, bufsize - (size_t)pos,
				&u.components[i]);
			if (w < 0) return -1;
			pos += w;
		}
	}
	buf[pos] = '\0';
	return pos;
}
static double bvn_single_component_factor(value_unit_component_t c)
{
	double pf      = bvni_prefix_factor(c);
	int32_t abs_exp = bvni_exp_abs(c.exponent);
	double result  = pow(pf, (double)abs_exp);
	if (bvni_is_neg_exp(c.exponent))
		result = 1.0 / result;
	return result;
}
double bvn_unit_prefix_factor(value_unit_t u)
{
	double f = 1.0;
	for (uint32_t i = 0; i < u.num_components && i < BVNR_MAX_UNIT_COMPONENTS; i++) {
		if (u.components[i].exponent == exp_invalid)
			continue;
		f *= bvn_single_component_factor(u.components[i]);
	}
	return f;
}
int32_t bvn_unit_prefix_exponent(value_unit_t u)
{
	int32_t total = 0;
	for (uint32_t i = 0; i < u.num_components && i < BVNR_MAX_UNIT_COMPONENTS; i++) {
		if (u.components[i].exponent == exp_invalid)
			continue;
		total += bvni_prefix_exp_int(u.components[i]);
	}
	return total;
}
value_type_spec_t bvn_parse_type_annotation(
	const uint8_t* str, uint32_t len,
	bool* type_ok, bool* unit_ok, bool* unit_too_long,
	value_unit_t* out_unit,
	uint8_t* unit_buf, uint8_t* unit_buf_len)
{
	value_type_spec_t r = BVN_TYPE_PLAIN;
	*type_ok      = true;
	*unit_ok      = true;
	*unit_too_long = false;
	*unit_buf_len = 0;
	*out_unit     = BVN_UNIT_NO_PREFIX(bu_none);
	if (!str || !len) { *type_ok = false; return r; }
	uint32_t pos = 0;
	if (pos + 9 <= len && memcmp(str + pos, "float_fix", 9) == 0) {
		r.family = vt_float_fix; pos += 9;
	} else if (pos + 9 <= len && memcmp(str + pos, "float_dec", 9) == 0) {
		r.family = vt_float_dec; pos += 9;
	} else if (pos + 5 <= len && memcmp(str + pos, "float", 5) == 0) {
		r.family = vt_float; pos += 5;
	} else if (pos + 4 <= len && memcmp(str + pos, "uint", 4) == 0) {
		r.family = vt_uint; pos += 4;
	} else if (pos + 4 <= len && memcmp(str + pos, "utf8", 4) == 0) {
		r.family = vt_utf8; pos += 4;
	} else if (pos + 4 <= len && memcmp(str + pos, "sint", 4) == 0) {
		r.family = vt_sint; pos += 4;
	} else {
		*type_ok = false;
		return r;
	}
	if (pos < len) {
		if (str[pos] != ':') {
			*type_ok = false;
			return r;
		}
		pos++;
		while (pos < len && str[pos] == ' ') pos++;
	}
	bool need_comma = false;
	while (pos < len) {
		if (need_comma) {
			if (str[pos] != ',') {
				*type_ok = false;
				return r;
			}
			pos++;
			while (pos < len && str[pos] == ' ') pos++;
		}
		need_comma = true;
		if (pos >= len || str[pos] == ',')
			continue;
		uint8_t c = str[pos];
		if (c >= '0' && c <= '9') {
			uint64_t w = 0;
			while (pos < len && str[pos] >= '0' && str[pos] <= '9') {
				uint32_t d = (uint32_t)(str[pos] - '0');
				if (w > ((uint64_t)UINT32_MAX - d) / 10u) {
					*type_ok = false;
					return r;
				}
				w = w * 10u + d;
				pos++;
			}
			if (pos < len && str[pos] != ',') {
				*type_ok = false;
				return r;
			}
			r.width = (uint32_t)w;
		} else if (c == '_') {
			pos++;
			if (pos >= len || str[pos] < '0' || str[pos] > '9') {
				*type_ok = false;
				return r;
			}
			uint32_t b = 0;
			while (pos < len && str[pos] >= '0' && str[pos] <= '9') {
				b = b * 10u + (uint32_t)(str[pos] - '0');
				if (b > 10000u) {
					*type_ok = false;
					return r;
				}
				pos++;
			}
			if (pos < len && str[pos] != ',') {
				*type_ok = false;
				return r;
			}
			if (r.family == vt_float_fix || r.family == vt_float_dec) {
				*type_ok = false;
				return r;
			}
			if (!((b >= 2u && b <= 62u) || b == 64u || b == 85u)) {
				*type_ok = false;
				return r;
			}
			r.base = b;
		} else if (c == 'q' && pos + 1 < len &&
				   str[pos + 1] >= '0' && str[pos + 1] <= '9') {
			if (r.family != vt_float_fix) {
				*type_ok = false;
				return r;
			}
			pos++;
			uint32_t q = 0;
			while (pos < len && str[pos] >= '0' && str[pos] <= '9') {
				q = q * 10u + (uint32_t)(str[pos] - '0');
				if (q > 256u) {
					*type_ok = false;
					return r;
				}
				pos++;
			}
			if (pos < len && str[pos] != ',') {
				*type_ok = false;
				return r;
			}
			r.base = q;
		} else {
			uint32_t ustart = pos;
			while (pos < len && str[pos] != ',')
				pos++;
			uint32_t ulen = pos - ustart;
			if (ulen > UINT8_MAX) {
				*unit_ok      = false;
				*unit_too_long = true;
				return r;
			}
			memcpy(unit_buf, str + ustart, ulen);
			unit_buf[ulen] = '\0';
			*unit_buf_len  = (uint8_t)ulen;
			if (ulen == 7u && memcmp(unit_buf, "no_unit", 7) == 0) {
				*out_unit = BVN_UNIT_NONE;
			} else {
				bool uok = true;
				*out_unit = bvn_parse_unit_n(unit_buf, ulen, &uok);
				if (!uok)
					*unit_ok = false;
			}
		}
	}
	if (r.family == vt_sint || r.family == vt_uint) {
		if (r.width > BVN_MAX_INT_WIDTH) {
			*type_ok = false;
			return r;
		}
	}
	if (r.family == vt_float) {
		if (r.base != 0u && r.base != 10u && r.base != 16u) {
			*type_ok = false;
			return r;
		}
		if (r.width != 0u && r.width != 16u &&
			(r.width % 32u != 0u || r.width > BVN_FLOAT_MAX_PREC)) {
			*type_ok = false;
			return r;
		}
	}
	if (r.family == vt_float_fix || r.family == vt_float_dec) {
		if (r.width != 0u  && r.width != 16u  && r.width != 32u &&
			r.width != 64u && r.width != 128u && r.width != 256u) {
			*type_ok = false;
			return r;
		}
	}
	if (r.family == vt_float_fix) {
		uint32_t eff_w = r.width ? r.width : 64u;
		if (r.base >= eff_w) {
			*type_ok = false;
			return r;
		}
	}
	return r;
}
bool bvn_validate_identifier(const char* id)
{
	if (!id || !*id) return false;
	if (!((*id >= 'a' && *id <= 'z') || (*id >= 'A' && *id <= 'Z') ||
		  *id == '_' ||
		  ((uint8_t)*id >= 0xc3 && (uint8_t)*id <= 0xf4)))
		return false;
	for (const char* p = id + 1; *p; p++) {
		unsigned char c = (unsigned char)*p;
		if (c <= 0x20 || c == 0x7f || c == 0xc2) return false;
		if (c == '"' || c == '#' || c == ',' || c == '.' ||
			c == '/' || c == ';' || c == '<' || c == '=' ||
			c == '>' || c == '[' || c == ']' || c == '{' ||
			c == '}' || c == '!' || c == '$' || c == '%' ||
			c == '&' || c == '\''|| c == '(' || c == ')' ||
			c == '*' || c == ':' || c == '?' || c == '@' ||
			c == '\\'|| c == '^' || c == '`' || c == '|' ||
			c == '~')
			return false;
	}
	return true;
}
bool bvn_validate_symbol(const char* surr)
{
	if (!surr || !*surr) return false;
	return bvn_validate_identifier(surr);
}
bool bvn_validate_reference(const char* link)
{
	if (!link || !*link) return false;
	const char* p = link;
	if (*p == '.') p++;
	while (*p) {
		if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
			  *p == '_' ||
			  ((uint8_t)*p >= 0xc3 && (uint8_t)*p <= 0xf4)))
			return false;
		p++;
		while (*p && *p != '.') {
			unsigned char c = (unsigned char)*p;
			if (c < 0x20 || c == 0x7f || c == 0xc2) return false;
			if (c == '"' || c == '#' || c == ',' || c == '/' ||
				c == ';' || c == '<' || c == '=' || c == '>' ||
				c == '[' || c == ']' || c == '{' || c == '}' ||
				c == '!' || c == '$' || c == '%' || c == '&' ||
				c == '\''|| c == '(' || c == ')' || c == '*' ||
				c == ':' || c == '?' || c == '@' || c == '\\' ||
				c == '^' || c == '`' || c == '|' || c == '~')
				return false;
			p++;
		}
		if (*p == '.') p++;
	}
	return true;
}
bool bvn_validate_number(const char* s)
{
	if (!s || !*s) return false;
	const char* p = s;
	if (*p == '-') p++;
	if (!*p) return false;
	bool has_dot = false, has_exp = false;
	bool has_mant_dig = false, has_exp_dig = false;
	for (; *p; p++) {
		if (*p >= '0' && *p <= '9') {
			if (has_exp) has_exp_dig = true;
			else         has_mant_dig = true;
			continue;
		}
		if (*p == '.') { if (has_dot || has_exp) return false; has_dot = true; continue; }
		if (*p == 'e' || *p == 'E') {
			if (has_exp || !has_mant_dig) return false;
			has_exp = true;
			if (p[1] == '+' || p[1] == '-') p++;
			continue;
		}
		return false;
	}
	return has_mant_dig && (!has_exp || has_exp_dig);
}
int32_t bvn_format_uint64(char* buf, size_t bufsize, uint64_t value,
						  uint32_t base, uint32_t min_digits)
{
	bool valid_base = (base >= 2u && base <= 62u) ||
					  base == 64u || base == 85u;
	if (!valid_base || !buf || bufsize < 2) return -1;
	if (min_digits > 64u) min_digits = 64u;
	char     tmp[65];
	int      pos      = 64;
	uint32_t zero_ch  = bvn_digit_to_char(0u, base);
	tmp[pos] = '\0';
	if (value == 0) {
		tmp[--pos] = (char)zero_ch;
	} else {
		while (value) {
			uint32_t d  = (uint32_t)(value % base);
			uint32_t ch = bvn_digit_to_char(d, base);
			if (!ch) return -1;
			tmp[--pos] = (char)ch;
			value /= base;
		}
	}
	while ((64 - pos) < (int)min_digits) tmp[--pos] = (char)zero_ch;
	int len = 64 - pos;
	if ((size_t)(len + 1) > bufsize) return -1;
	memcpy(buf, tmp + pos, (size_t)len + 1);
	return len;
}
int32_t bvn_format_int64(char* buf, size_t bufsize, int64_t value,
						 uint32_t base, uint32_t min_digits)
{
	bool neg = (value < 0);
	if (neg) {
		if (bufsize < 2) return -1;
		buf[0] = '-';
		uint64_t abs_val = (uint64_t)0 - (uint64_t)value;
		int32_t r = bvn_format_uint64(buf + 1, bufsize - 1,
									  abs_val, base, min_digits);
		return r < 0 ? r : r + 1;
	}
	return bvn_format_uint64(buf, bufsize, (uint64_t)value, base, min_digits);
}
uint32_t bvn_min_digits_for_type(value_type_spec_t vt)
{
	if (!bvn_type_is_numeric(vt) || !vt.width) return 0;
	uint32_t w = vt.width;
	uint32_t base = bvn_effective_base(vt);
	uint32_t digits = 1;
	if (w > 64u) w = 64u;
	uint64_t maxv = (vt.family == vt_sint)
		? ((uint64_t)1u << (w - 1u)) - 1u
		: (w >= 64u) ? UINT64_MAX : ((uint64_t)1u << w) - 1u;
	uint64_t v = maxv;
	while (v >= base) { v /= base; digits++; }
	return digits;
}
bool bvn_parse_int64(const char* s, value_type_spec_t vt, int64_t* out)
{
	if (!s || !out) return false;
	uint32_t base = bvn_effective_base(vt);
	if (base >= 2u && base <= 36u) {
		char* end;
		errno = 0;
		long long v = strtoll(s, &end, (int)base);
		if (*end || errno == ERANGE) return false;
		*out = (int64_t)v;
		return true;
	}
	bool neg = (s[0] == '-');
	const char* p = s + (neg ? 1u : 0u);
	if (!*p) return false;
	uint64_t acc = 0;
	while (*p) {
		uint32_t d = bvn_char_to_digit((uint8_t)*p, base);
		if (d >= base) return false;
		if (acc > (UINT64_MAX - d) / base) return false;
		acc = acc * base + d;
		p++;
	}
	if (neg) {
		if (acc > (uint64_t)INT64_MAX + 1u) return false;
		*out = (acc == (uint64_t)INT64_MAX + 1u)
		       ? INT64_MIN
		       : -(int64_t)acc;
	} else {
		if (acc > (uint64_t)INT64_MAX) return false;
		*out = (int64_t)acc;
	}
	return true;
}
bool bvn_parse_uint64(const char* s, value_type_spec_t vt, uint64_t* out)
{
	if (!s || !out) return false;
	uint32_t base = bvn_effective_base(vt);
	if (base >= 2u && base <= 36u) {
		char* end;
		errno = 0;
		unsigned long long v = strtoull(s, &end, (int)base);
		if (*end || errno == ERANGE) return false;
		*out = (uint64_t)v;
		return true;
	}
	const char* p = s;
	if (*p == '+') p++;
	if (!*p) return false;
	uint64_t acc = 0;
	while (*p) {
		uint32_t d = bvn_char_to_digit((uint8_t)*p, base);
		if (d >= base) return false;
		if (acc > (UINT64_MAX - d) / base) return false;
		acc = acc * base + d;
		p++;
	}
	*out = acc;
	return true;
}
bool bvn_parse_double_in_base(const char* s, uint32_t base, double* out)
{
	if (!s || !out) return false;
	bvn_limb_t _dlimbs[BVN_FLOAT_NLIMBS(64u)];
	bvn_float_t f;
	bvn_float_init_buf(&f, 64u, _dlimbs, BVN_FLOAT_NLIMBS(64u));
	if (!bvn_float_from_str(&f, s, base)) {
		if (base == 10) {
			*out = bvn_float_parse_f64(s);
			return true;
		}
		return false;
	}
	return bvn_float_to_double(&f, out);
}
bool bvn_looks_like_double(const char* s)
{
	if (!s) return false;
	for (; *s; s++)
		if (*s == '.' || *s == 'e' || *s == 'E' || *s == 'p' || *s == 'P')
			return true;
	return false;
}
int32_t bvn_format_double(char* buf, size_t bufsize, double value,
						  value_type_spec_t vt)
{
	if (!buf || bufsize < 2) return -1;
	bvn_limb_t _dlimbs[BVN_FLOAT_NLIMBS(64u)];
	bvn_float_t f;
	uint32_t prec = bvn_effective_width(vt);
	if (!prec || prec > 64u) prec = 64u;
	bvn_float_init_buf(&f, prec, _dlimbs, BVN_FLOAT_NLIMBS(64u));
	if (!bvn_float_from_double(&f, value)) return -1;
	uint32_t base = bvn_effective_base(vt);
	if (!base || base > 36u) base = 10u;
	return bvn_float_to_str(&f, buf, bufsize, base);
}
bool bvn_parse_double(const char* s, value_type_spec_t vt, double* out)
{
	return bvn_parse_double_in_base(s, bvn_effective_base(vt), out);
}
const uint8_t* bvn_get_escape_repl_table(void)
{
	static const uint8_t table[256] = {
		['t']  = '\t',  ['n']  = '\n', ['v']  = '\v',
		['f']  = '\f',  ['r']  = '\r', ['"']  = '"',
		['\\'] = '\\',
	};
	return table;
}
