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

#ifndef BOVNAR_H_
#define BOVNAR_H_
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "bvn_float.h"
#include "bvn_int.h"
#ifndef BVN_API
#  if defined(_WIN32) || defined(__CYGWIN__)
#    define BVN_API __declspec(dllexport)
#  elif defined(__GNUC__) || defined(__clang__)
#    define BVN_API __attribute__((visibility("default")))
#  else
#    define BVN_API
#  endif
#endif
#ifdef __cplusplus
extern "C" {
#endif
/*
 * Library/format version. The reference implementation versions in lockstep
 * with the Python package (pyproject.toml, bovnar.__version__). BVNR_VERSION is
 * an int that increases monotonically and is safe for #if comparison, e.g.
 * #if BVNR_VERSION >= 10100.
 */
#define BVNR_VERSION_MAJOR		1
#define BVNR_VERSION_MINOR		1
#define BVNR_VERSION_PATCH		0
#define BVNR_VERSION			((BVNR_VERSION_MAJOR) * 10000 + \
					 (BVNR_VERSION_MINOR) * 100 + \
					 (BVNR_VERSION_PATCH))
#define BVNR_VERSION_STRING		"1.1.0"
/*
 * Highest bovnar *spec* version this build understands. A document may declare
 * the spec version it targets with a leading "#!bovnar <major>.<minor>"
 * directive (see the spec, §"Version directive"); the reader parses it, exposes
 * it via bvnr_reader_get_declared_version(), and — when bvnr_read_flags_t
 * .strict_version is set — refuses a document whose declared version exceeds
 * what this build supports. The spec version tracks the language; it is bumped
 * only when the grammar/type system grows, independently of the release patch
 * level above.
 */
#define BVNR_SPEC_VERSION_MAJOR		1
#define BVNR_SPEC_VERSION_MINOR		1
#define BVNR_MAX_UNIT_COMPONENTS		8
#define BVN_MAX_INT_WIDTH			32768u
/*
 * Sentinel for bvnr_read_flags_t.max_file_size and bvnr_write_flags_t.max_file_size:
 * a value of 0 removes the byte-count cap entirely, enabling unbounded ("endless")
 * streaming through the lexer/validator. This is the default for a zero-initialised
 * flags struct, so endless streams accumulate no byte-count limit unless a cap is
 * requested explicitly. Every byte/offset/line/column counter in the core is already
 * 64-bit, so a single never-ending document (or octet stream) can run to 2^64-1 bytes;
 * the only limit is this policy knob. To impose a cap, set max_file_size to a positive
 * byte count (e.g. 16 MiB for production); once the processed byte count exceeds it the
 * reader raises error_file_too_long.
 */
#define BVNR_FILESIZE_UNLIMITED			0u
typedef enum bvnr_event_e {
	ev_stream_start,
	ev_assignment_start,
	ev_octet_stream_start,
	ev_octet_stream_end,
	ev_struct_start,
	ev_struct_end,
	ev_array_row_start,
	ev_array_row_end,
	ev_array_dim_start,
	ev_data,
	ev_type_annotation_start,
	ev_type_annotation_end,
	ev_type_annotation_type_family,
	ev_type_annotation_type_family_parameter,
	ev_stream_end
} bvnr_event_t;
typedef enum value_type_family_e {
	vt_plain,
	vt_utf8,
	vt_sint,
	vt_uint,
	vt_float,
	vt_float_fix,
	vt_float_dec,
	vt_bool,
	/* Timestamp (spec 1.1). Carried as a signed integer count of seconds since
	 * a named epoch; the epoch is selected by an annotation parameter and stored
	 * as a small dense index in value_type_spec_t.base (0 = unix, the default).
	 * Use bvnr_datetime_epoch_name() / bvnr_datetime_epoch_mjd() to recover the
	 * epoch, then the bvn_dt_* helpers in bvn_datetime.h to convert to civil time. */
	vt_datetime,
	vt_illegal
} value_type_family_t;
typedef enum token_type_e {
	token_is_identifier,
	token_is_string,
	token_is_number,
	token_is_symbol,
	token_is_reference,
	token_is_array_number,
	token_is_array_string,
	token_is_type,
	token_is_octet_stream,
	token_is_null_value,
	token_is_structure,
	token_is_unit,
	token_is_type_width,
	token_is_type_base,
	token_is_type_q,
	token_is_bool,
	token_is_unknown
} token_type_t;
typedef enum error_code_e {
	error_none                          = 0,
	error_unknown_token_type            = 1,
	error_array_row_size_mismatch       = 2,
	error_identifier_too_long           = 3,
	error_empty_identifier              = 4,
	error_struct_nesting_too_high       = 5,
	error_array_nesting_too_high        = 6,
	error_illegal_struct_close          = 7,
	error_string_too_long               = 8,
	error_illegal_escape_sequence       = 9,
	error_number_too_long               = 10,
	error_symbol_too_long               = 11,
	error_reference_too_long            = 12,
	error_read_complete_chunk_failed    = 13,
	error_octet_stream_out_of_sync      = 14,
	error_unexpected_input_byte         = 15,
	error_text_data_too_long            = 16,
	error_reading_from_source_fd        = 17,
	error_got_incomplete_bvnr_stream    = 18,
	error_invalid_utf8_byte             = 19,
	error_invalid_byte_order_mark       = 20,
	error_type_too_long                 = 21,
	error_unit_too_long                 = 22,
	error_expected_string_in_array      = 23,  /* reserved; never set by the library */
	error_expected_number_in_array      = 24,  /* reserved; never set by the library */
	error_illegal_value_type            = 25,
	error_scanner_callback_failed       = 26,
	error_file_too_long                 = 27,
	error_invalid_argument              = 28,
	error_too_many_array_items          = 29,
	error_writing_to_sink               = 30,
	error_sink_buffer_exhausted         = 31,
	error_unit_illegal                  = 32,
	error_base_requires_string_literal  = 33,
	error_type_value_mismatch           = 34,
	error_value_out_of_range            = 35,
	error_digit_not_in_base             = 36,
	error_recovered                     = 37,  /* reserved; never set by the library */
	error_unit_mismatch                 = 38,
	/* Array element homogeneity (spec 1.0): every non-null element of an array
	 * must share the same kind and physical dimension; sibling sub-arrays must
	 * match in length and element shape (recursively); sibling structs must
	 * share the same keys with recursively-matching fields. */
	error_array_element_type_mismatch   = 39,
	error_struct_shape_mismatch         = 40,
	/* A struct (or the top-level document) repeats a key. Keys must be unique
	 * within one scope so lookup, references and iteration always agree. */
	error_duplicate_struct_key          = 41,
	/* A leading "#!bovnar …" version directive is present but malformed — the
	 * "!bovnar" prefix is followed by something that is not a "<major>.<minor>"
	 * decimal version (bad digits, missing dot, trailing junk, overlong). */
	error_invalid_spec_version          = 42,
	/* The document declares a spec version newer than this build supports, and
	 * bvnr_read_flags_t.strict_version was set. Without strict_version the
	 * declared version is stored but not enforced (the parse continues and only
	 * fails if it actually meets syntax the build does not implement). */
	error_unsupported_spec_version      = 43,
	/* spec 1.1 — a \u{…} escape names a value that is not a Unicode scalar
	 * (a surrogate U+D800–U+DFFF, or a code point above U+10FFFF), or the
	 * braced digits are missing/too many. Malformed \x / \u *structure*
	 * (bad hex, missing brace) stays error_illegal_escape_sequence. */
	error_invalid_codepoint             = 44
} error_code_t;
typedef enum prefix_system_e {
	prefix_si,
	prefix_iec
} prefix_system_t;
typedef enum si_prefix_id_e {
	si_none = 0,
	si_quecto, si_ronto, si_yocto, si_zepto, si_atto,
	si_femto, si_pico, si_nano, si_micro, si_milli,
	si_centi, si_deci,
	si_deca, si_hecto, si_kilo, si_mega, si_giga,
	si_tera, si_peta, si_exa, si_zetta, si_yotta,
	si_ronna, si_quetta
} si_prefix_id_t;
typedef enum iec_prefix_id_e {
	iec_none = 0,
	iec_kibi, iec_mebi, iec_gibi, iec_tebi, iec_pebi,
	iec_exbi, iec_zebi, iec_yobi, iec_robi, iec_quebi
} iec_prefix_id_t;
typedef enum value_base_unit_e {
	bu_none = 0,
	bu_bit, bu_byte,
	bu_second, bu_meter, bu_gram, bu_ampere, bu_kelvin,
	bu_mol, bu_candela,
	bu_hertz, bu_newton, bu_pascal, bu_joule, bu_watt,
	bu_volt, bu_ohm, bu_farad, bu_coulomb, bu_siemens,
	bu_weber, bu_tesla, bu_henry, bu_lumen, bu_lux,
	bu_becquerel, bu_gray, bu_sievert, bu_katal,
	bu_liter, bu_minute, bu_hour, bu_day, bu_degree, bu_celsius,
	bu_radian, bu_steradian,
	bu_tonne, bu_bar,
	bu_electronvolt, bu_dalton, bu_astronomical_unit,
	bu_hectare,
	bu_week, bu_year,
	bu_inch, bu_foot, bu_yard, bu_mile, bu_nautical_mile,
	bu_angstrom, bu_light_year, bu_parsec, bu_furlong, bu_fathom,
	bu_pound, bu_ounce, bu_grain, bu_stone, bu_short_ton,
	bu_long_ton, bu_troy_ounce, bu_carat,
	bu_fahrenheit,
	bu_atmosphere, bu_mmhg, bu_torr, bu_psi,
	bu_calorie, bu_btu, bu_erg, bu_therm,
	bu_horsepower,
	bu_pound_force, bu_dyne, bu_kip,
	bu_knot,
	bu_gallon, bu_gallon_uk, bu_quart, bu_pint, bu_cup,
	bu_fluid_ounce, bu_tablespoon, bu_teaspoon, bu_barrel,
	bu_acre, bu_barn,
	bu_arcminute, bu_arcsecond, bu_grad,
	bu_poise, bu_stokes, bu_gauss, bu_maxwell, bu_oersted,
	bu_stilb, bu_phot, bu_galileo,
	bu_curie, bu_roentgen, bu_rem,
	bu_neper,
	bu_decibel,
	bu_rankine,
	bu_slug,
	bu_thou,
	bu_pint_uk, bu_fluid_ounce_uk, bu_quart_uk,
	bu_var,
	bu_volt_ampere,
	bu_kilogram_force,
	bu_inch_hg,
	bu_rpm,
	bu_foot_pound,
	bu_dram,
	bu_pennyweight,
	bu_chain,
	bu_rod,
	bu_gill,
	bu_gill_uk,
	bu_standard_gravity,
	bu_metric_horsepower,
	bu_revolution,
	bu_month,
	bu_fortnight,
	bu_atmosphere_technical,
	bu_tex,
	bu_denier,
	bu_fluid_dram,
	bu_minim,
	bu_peck,
	bu_bushel,
	bu_pfund = 348, bu_zentner, bu_doppelzentner, bu_lot,
	bu_prussian_line, bu_prussian_zoll, bu_prussian_fuss, bu_prussian_elle,
	bu_prussian_rute, bu_klafter, bu_german_mile,
	bu_morgen,
	bu_scheffel,
	bu_survey_foot, bu_league, bu_cable, bu_hand,
	bu_quintal, bu_scruple, bu_baud,
	bu_delisle, bu_newton_temp, bu_reaumur, bu_romer,
	bu_percent, bu_per_mille, bu_per_myriad,
	bu_per_cent_mille, bu_ppm, bu_ppb
} value_base_unit_t;
typedef enum unit_exponent_e {
	exp_invalid    =   0,
	exp_linear     =   1,
	exp_square     =   2,
	exp_cubic      =   3,
	exp_quartic    =   4,
	exp_quintic    =   5,
	exp_sextic     =   6,
	exp_septic     =   7,
	exp_octic      =   8,
	exp_nonic      =   9,
	exp_neg_linear =  -1,
	exp_neg_square =  -2,
	exp_neg_cubic  =  -3,
	exp_neg_quartic=  -4,
	exp_neg_quintic=  -5,
	exp_neg_sextic =  -6,
	exp_neg_septic =  -7,
	exp_neg_octic  =  -8,
	exp_neg_nonic  =  -9
} unit_exponent_t;
typedef struct value_type_spec_s {
	value_type_family_t	family;
	uint32_t		width;
	uint32_t		base;
} value_type_spec_t;
typedef struct value_unit_prefix_s {
	prefix_system_t system;
	union {
		si_prefix_id_t  si;
		iec_prefix_id_t iec;
	} id;
} value_unit_prefix_t;
typedef struct value_unit_component_s {
	value_base_unit_t   base;
	unit_exponent_t     exponent;
	value_unit_prefix_t prefix;
} value_unit_component_t;
typedef struct value_unit_s {
	uint32_t			num_components;
	value_unit_component_t		components[BVNR_MAX_UNIT_COMPONENTS];
} value_unit_t;
typedef struct bvnr_data_s {
	token_type_t		type;
	value_type_spec_t	value_type;
	value_unit_t		value_unit;
	const void*		data;
	uint32_t		length;
} bvnr_data_t;
typedef void (*bvnr_on_error_fn)(
	void* userdata, error_code_t err,
	uint64_t line, uint64_t column,
	uint32_t byte, uint64_t offset);
typedef struct bvnr_read_flags_s {
	uint16_t	max_identifier_length;
	uint16_t	max_string_length;
	uint16_t	max_number_length;
	uint16_t	max_symbol_length;
	uint16_t	max_reference_length;
	uint64_t	max_array_items;
	uint64_t	max_text_bytes;
	uint64_t	max_file_size;
	uint8_t		max_struct_nesting;
	uint8_t		max_array_nesting;
	void*		userdata;
	bool		(*on_unverified)
			(void* userdata, bvnr_event_t e, bvnr_data_t* data);
	bool		(*on_verified)
			(void* userdata, bvnr_event_t e, bvnr_data_t* data);
	bool		continue_on_error;
	bvnr_on_error_fn	on_error;
	/* When true, a leading "#!bovnar <major>.<minor>" directive declaring a
	 * spec version newer than this build supports (BVNR_SPEC_VERSION_*) is a
	 * hard error_unsupported_spec_version. When false (the zero-init default)
	 * the declared version is recorded — query it with
	 * bvnr_reader_get_declared_version() — but not enforced. Production
	 * consumers that must not silently misread a future document should set it. */
	bool		strict_version;
	uint64_t	_reserved[4];
} bvnr_read_flags_t;
typedef uint32_t bvn_unit_flags_t;
#define BVN_UNIT_FLAGS_NONE ((bvn_unit_flags_t)0u)
#define BVN_UNIT_REDUCE     ((bvn_unit_flags_t)(1u << 0))
#define BVN_UNIT_ASCII_EXP  ((bvn_unit_flags_t)(1u << 1))
typedef struct bvnr_write_flags_s {
	uint16_t	max_identifier_length;
	uint16_t	max_string_length;
	uint16_t	max_number_length;
	uint16_t	max_symbol_length;
	uint16_t	max_reference_length;
	uint64_t	max_array_items;
	uint64_t	max_text_bytes;
	uint64_t	max_file_size;
	uint8_t		max_struct_nesting;
	uint8_t		max_array_nesting;
	void*		userdata;
	bool		(*on_event)
			(void* userdata, bvnr_event_t e, bvnr_data_t* data);
	bool		continue_on_error;
	bvnr_on_error_fn	on_error;
	bvn_unit_flags_t	unit_flags;
	/* When true, bvnr_open_write_* emits a leading
	 * "#!bovnar <BVNR_SPEC_VERSION_MAJOR>.<BVNR_SPEC_VERSION_MINOR>" directive
	 * before any value, so the produced document self-declares its spec
	 * version. Equivalent to calling bvnr_write_version() right after open.
	 * Off by default (zero-init). */
	bool		emit_version;
	uint64_t	_reserved[4];
} bvnr_write_flags_t;
typedef struct bvnr_source_s bvnr_source_t;
typedef struct bvnr_sink_s   bvnr_sink_t;
typedef bool (*bvnr_pull_fn)
	(bvnr_source_t* s, void* buf, uint32_t want, uint32_t* got);
typedef bool (*bvnr_push_fn)
	(bvnr_sink_t* s, const void* buf, uint32_t len);
typedef bool (*bvnr_flush_fn)
	(bvnr_sink_t* s);
#define BVNR_SOURCE_RESERVED_SIZE 64
#define BVNR_SINK_RESERVED_SIZE   64
struct bvnr_source_s {
	union {
		void*    _ptr;
		uint64_t _u64;
		uint8_t  _opaque[BVNR_SOURCE_RESERVED_SIZE];
	} _reserved;
};
struct bvnr_sink_s {
	union {
		void*    _ptr;
		uint64_t _u64;
		uint8_t  _opaque[BVNR_SINK_RESERVED_SIZE];
	} _reserved;
};
typedef struct bvnr_reader_s bvnr_reader_t;
typedef struct bvnr_writer_s bvnr_writer_t;
#define BVN_TYPE_PLAIN \
	((value_type_spec_t){ .family = vt_plain, .width = 0,  .base = 0  })
#define BVN_TYPE_UTF8 \
	((value_type_spec_t){ .family = vt_utf8,  .width = 0,  .base = 0  })
#define BVN_TYPE_BOOL \
	((value_type_spec_t){ .family = vt_bool,  .width = 0,  .base = 0  })
#define BVN_TYPE_SINT(w) \
	((value_type_spec_t){ .family = vt_sint,  .width = (w), .base = 0  })
#define BVN_TYPE_UINT(w) \
	((value_type_spec_t){ .family = vt_uint,  .width = (w), .base = 0  })
#define BVN_TYPE_FLOAT(w) \
	((value_type_spec_t){ .family = vt_float, .width = (w), .base = 0  })
#define BVN_TYPE_FLOAT_FIX(w, q) \
	((value_type_spec_t){ .family = vt_float_fix, .width = (w), .base = (q) })
#define BVN_TYPE_FLOAT_DEC(w) \
	((value_type_spec_t){ .family = vt_float_dec, .width = (w), .base = 0  })
#define BVN_TYPE_FLOAT_BASE(w, b) \
	((value_type_spec_t){ .family = vt_float, .width = (w), .base = (b) })
#define BVN_TYPE_UINT_BASE(w,b) \
	((value_type_spec_t){ .family = vt_uint,  .width = (w), .base = (b) })
#define BVN_TYPE_SINT_BASE(w,b) \
	((value_type_spec_t){ .family = vt_sint,  .width = (w), .base = (b) })
#define BVN_UNIT_NO_PREFIX(b) \
	((value_unit_t){ \
		.num_components = 1, \
		.components = {{ \
			.base = (b), .exponent = exp_linear, \
			.prefix.system = prefix_si, .prefix.id.si = si_none \
		}} \
	})
#define BVN_UNIT_SI(b, p) \
	((value_unit_t){ \
		.num_components = 1, \
		.components = {{ \
			.base = (b), .exponent = exp_linear, \
			.prefix.system = prefix_si, .prefix.id.si = (p) \
		}} \
	})
#define BVN_UNIT_IEC(b, p) \
	((value_unit_t){ \
		.num_components = 1, \
		.components = {{ \
			.base = (b), .exponent = exp_linear, \
			.prefix.system = prefix_iec, .prefix.id.iec = (p) \
		}} \
	})
#define BVN_UNIT_SI_EXP(b, p, e) \
	((value_unit_t){ \
		.num_components = 1, \
		.components = {{ \
			.base = (b), .exponent = (e), \
			.prefix.system = prefix_si, .prefix.id.si = (p) \
		}} \
	})
#define BVN_UNIT_NONE  ((value_unit_t){ .num_components = 0 })
#define BVN_UNIT_COMPOUND2(b1, p1, e1, b2, p2, e2) \
	((value_unit_t){ \
		.num_components = 2, \
		.components = { \
			{ .base = (b1), .exponent = (e1), \
			  .prefix.system = prefix_si, .prefix.id.si = (p1) }, \
			{ .base = (b2), .exponent = (e2), \
			  .prefix.system = prefix_si, .prefix.id.si = (p2) } \
		} \
	})
static inline bool bvn_type_spec_eq(
	value_type_spec_t a, value_type_spec_t b)
{
	return a.family == b.family && a.width == b.width && a.base == b.base;
}
static inline uint32_t bvn_effective_width(value_type_spec_t s)
{
	return s.width ? s.width : 64u;
}
static inline uint32_t bvn_effective_base(value_type_spec_t s)
{
	/* float_fix/dec are always decimal. datetime's base field holds an epoch
	 * index, NOT a numeric base, and its carrier is decimal epoch-seconds — so
	 * it must report base 10, or DOM/parse paths (bvn_parse_int64, the DOM
	 * builder) would decode the seconds in the bogus "base" and corrupt it. */
	if (s.family == vt_float_fix || s.family == vt_float_dec ||
	    s.family == vt_datetime)
		return 10u;
	return s.base ? s.base : 10u;
}
static inline uint32_t bvn_effective_q(value_type_spec_t s)
{
	return (s.family == vt_float_fix) ? s.base : 0u;
}
static inline bool bvn_type_is_numeric(value_type_spec_t s)
{
	return s.family == vt_sint      || s.family == vt_uint    ||
		   s.family == vt_float     ||
		   s.family == vt_float_fix || s.family == vt_float_dec;
}
static inline bool bvn_type_is_plain(value_type_spec_t s)
{
	return s.family == vt_plain;
}
BVN_API void bvnr_source_from_fd(bvnr_source_t* s, int fd);
BVN_API void bvnr_source_from_mem(bvnr_source_t* s, const void* buf, uint64_t len);
BVN_API void bvnr_sink_to_fd(bvnr_sink_t* s, int fd);
BVN_API void bvnr_sink_to_mem(bvnr_sink_t* s, void* buf, uint64_t cap);
BVN_API uint64_t bvnr_sink_bytes_written(const bvnr_sink_t* s);
BVN_API bvnr_reader_t* bvnr_reader_create(void);
BVN_API void           bvnr_reader_destroy(bvnr_reader_t* r);
BVN_API bool bvnr_open_read_source(
	bvnr_reader_t* r, const bvnr_source_t* src,
	const bvnr_sink_t* src_mirror, bvnr_read_flags_t* options);
BVN_API bool bvnr_open_read_mem(
	bvnr_reader_t* r, const void* buf, uint64_t len,
	void* mirror_buf, uint64_t mirror_cap,
	bvnr_read_flags_t* options);
BVN_API bool bvnr_read(bvnr_reader_t* r);
typedef struct bvnr_canon_observer_s bvnr_canon_observer_t;
BVN_API bvnr_canon_observer_t* bvnr_canon_observer_create(
	const bvnr_sink_t* sink, bool pretty);
BVN_API void bvnr_canon_observer_destroy(bvnr_canon_observer_t* obs);
BVN_API bool bvnr_canon_observer_on_event(
	void* obs, bvnr_event_t ev, bvnr_data_t* data);
BVN_API bool bvnr_canon_observer_finish(bvnr_canon_observer_t* obs);
BVN_API error_code_t bvnr_reader_get_error(const bvnr_reader_t* r);
BVN_API uint64_t     bvnr_reader_get_error_line  (const bvnr_reader_t* r);
BVN_API uint64_t     bvnr_reader_get_error_column(const bvnr_reader_t* r);
BVN_API uint32_t     bvnr_reader_get_error_byte  (const bvnr_reader_t* r);
BVN_API uint64_t     bvnr_reader_get_error_offset(const bvnr_reader_t* r);
BVN_API uint64_t     bvnr_reader_get_recovery_count(const bvnr_reader_t* r);
/*
 * Spec version a document declared via a leading "#!bovnar <major>.<minor>"
 * directive. Returns true and fills major/minor when the document carried a
 * (well-formed) directive; returns false (leaving the outputs untouched) when
 * it did not. Valid only after bvnr_read(). Either out pointer may be NULL.
 */
BVN_API bool bvnr_reader_get_declared_version(
	const bvnr_reader_t* r, uint16_t* major, uint16_t* minor);
/*
 * Scan a buffer for a leading "#!bovnar <major>.<minor>" directive without a
 * full parse (an optional UTF-8 BOM and leading whitespace are skipped).
 * Returns true and fills major/minor on a well-formed directive, false
 * otherwise. Either out pointer may be NULL.
 */
BVN_API bool bvnr_peek_version(
	const void* buf, uint64_t len, uint16_t* major, uint16_t* minor);
/* Runtime library version (BVNR_VERSION / BVNR_VERSION_STRING). */
BVN_API uint32_t    bvnr_version(void);
BVN_API const char* bvnr_version_string(void);
/* Highest bovnar spec version this build understands (BVNR_SPEC_VERSION_*). */
BVN_API void        bvnr_spec_version(uint16_t* major, uint16_t* minor);
BVN_API bvnr_writer_t* bvnr_writer_create(void);
BVN_API void           bvnr_writer_destroy(bvnr_writer_t* w);
BVN_API bool bvnr_open_write_sink(
	bvnr_writer_t* w, const bvnr_sink_t* sink,
	bool pretty, bvnr_write_flags_t* options);
BVN_API bool bvnr_open_write_mem(
	bvnr_writer_t* w, void* buf, uint64_t cap,
	bool pretty, bvnr_write_flags_t* options);
BVN_API bool bvnr_write_event(
	bvnr_writer_t* w, bvnr_event_t ev, bvnr_data_t* data);
/*
 * Emit a leading "#!bovnar <major>.<minor>" version directive. Must be called
 * immediately after bvnr_open_write_* and before any value is written; doing so
 * later (once output has begun) is error_invalid_argument. Use it to round-trip
 * a directive read from a source document, or pass BVNR_SPEC_VERSION_MAJOR /
 * BVNR_SPEC_VERSION_MINOR to stamp the current spec version.
 */
BVN_API bool bvnr_write_version(
	bvnr_writer_t* w, uint16_t major, uint16_t minor);
BVN_API bool bvnr_write_finish(bvnr_writer_t* w);
BVN_API error_code_t bvnr_writer_get_error(const bvnr_writer_t* w);
BVN_API uint64_t     bvnr_writer_get_error_offset(const bvnr_writer_t* w);
BVN_API uint64_t     bvnr_writer_bytes_written(const bvnr_writer_t* w);
BVN_API const char*  bvn_error_to_string(error_code_t code);
BVN_API value_unit_t bvn_parse_unit(const uint8_t* unit, bool* ok);
BVN_API value_unit_t bvn_parse_unit_n(const uint8_t* unit, uint32_t len, bool* ok);
BVN_API int32_t      bvn_unit_to_string(value_unit_t u, char* buf, size_t bufsize);
BVN_API int32_t      bvn_unit_to_string_ex(value_unit_t u, char* buf, size_t bufsize,
                                    bvn_unit_flags_t flags);
BVN_API bool         bvn_unit_valid(value_unit_t u);
BVN_API bool         bvn_unit_equal(value_unit_t a, value_unit_t b);
BVN_API double       bvn_unit_prefix_factor(value_unit_t u);
BVN_API int32_t      bvn_unit_prefix_exponent(value_unit_t u);
BVN_API bvn_unit_flags_t bvnr_writer_unit_flags(const bvnr_writer_t* w);
/*
 * Datetime epoch helpers (spec 1.1). For a vt_datetime spec, return the epoch's
 * lowercase name ("unix", "tai", "gps", "mjd", "ntp", "galileo", "glonass",
 * "y2000", "beidou") and its epoch day number (the bvn_epoch_t value from
 * bvn_datetime.h, i.e. the MJD of the epoch's instant). For a non-datetime spec
 * the name is "unix" and the day number is the unix epoch, by convention.
 */
BVN_API const char* bvnr_datetime_epoch_name(value_type_spec_t vt);
BVN_API int32_t     bvnr_datetime_epoch_mjd(value_type_spec_t vt);
/* Dense epoch index for an epoch name ("unix", "tai", …) — the value stored in
 * value_type_spec_t.base for a vt_datetime spec. NULL/empty → 0 (unix); an
 * unknown name → -1. Inverse of bvnr_datetime_epoch_name(). */
BVN_API int32_t     bvnr_datetime_epoch_index(const char* name);
BVN_API value_type_spec_t bvn_parse_type_annotation(
	const uint8_t* str, uint32_t len,
	bool* type_ok, bool* unit_ok, bool* unit_too_long,
	value_unit_t* out_unit,
	uint8_t* unit_buf, uint8_t* unit_buf_len);
BVN_API bool bvn_validate_identifier(const char* id);
BVN_API bool bvn_validate_symbol(const char* surr);
BVN_API bool bvn_validate_reference(const char* link);
BVN_API bool bvn_validate_number(const char* s);
BVN_API bool bvn_is_special_number_string(const char* s);
BVN_API bool bvn_validate_digits_for_base(const char* s, uint32_t base);
BVN_API bool bvn_validate_number_in_base(const char* s, uint32_t base);
BVN_API bool bvn_validate_uint_range(const char* s, uint32_t w, uint32_t base);
BVN_API bool bvn_validate_sint_range(const char* s, uint32_t w, uint32_t base);
BVN_API bool bvn_validate_string(const uint8_t* data, size_t len);
BVN_API uint32_t bvn_char_to_digit(uint32_t c, uint32_t base);
BVN_API uint32_t bvn_min_digits_for_type(value_type_spec_t vt);
BVN_API int32_t bvn_format_uint64(
	char* buf, size_t bufsize, uint64_t value,
	uint32_t base, uint32_t min_digits);
BVN_API int32_t bvn_format_int64(
	char* buf, size_t bufsize, int64_t value,
	uint32_t base, uint32_t min_digits);
BVN_API int32_t bvn_format_double(
	char* buf, size_t bufsize, double value, value_type_spec_t vt);
BVN_API bool bvn_parse_int64(const char* s, value_type_spec_t vt, int64_t* out);
BVN_API bool bvn_parse_uint64(const char* s, value_type_spec_t vt, uint64_t* out);
BVN_API bool bvn_parse_double(const char* s, value_type_spec_t vt, double* out);
BVN_API bool bvn_parse_double_in_base(const char* s, uint32_t base, double* out);
BVN_API bool bvn_looks_like_double(const char* s);
BVN_API const uint8_t* bvn_get_escape_repl_table(void);
BVN_API bool bvnr_write_type_annotation(bvnr_writer_t* w,
				value_type_spec_t vt,
				value_unit_t vu);
BVN_API bool bvnr_write_string(bvnr_writer_t* w, const char* key, const char* value);
BVN_API bool bvnr_write_plain (bvnr_writer_t* w, const char* key, const char* value);
BVN_API bool bvnr_write_null  (bvnr_writer_t* w, const char* key);
BVN_API bool bvnr_write_bool  (bvnr_writer_t* w, const char* key, bool value);
BVN_API bool bvnr_write_uint(bvnr_writer_t* w, const char* key,
			 uint32_t width, uint64_t value);
BVN_API bool bvnr_write_sint(bvnr_writer_t* w, const char* key,
			 uint32_t width, int64_t value);
BVN_API bool bvnr_write_float(bvnr_writer_t* w, const char* key,
			  uint32_t width, double value);
/* Write a timestamp (spec 1.1). `epoch` is an epoch name ("unix", "tai", "gps",
 * "mjd", "ntp", "galileo", "glonass", "y2000", "beidou") or NULL for unix; an
 * unknown name is error_invalid_argument. `value` is signed seconds since the
 * epoch. Emits a <datetime:width,epoch> annotation (the epoch is implicit when
 * it is the default unix). The document must declare #!bovnar 1.1 to be re-read. */
BVN_API bool bvnr_write_datetime(bvnr_writer_t* w, const char* key,
			  uint32_t width, const char* epoch, int64_t value);
BVN_API bool bvnr_write_float_fix(bvnr_writer_t* w, const char* key,
			   uint32_t width, uint32_t q, double value);
BVN_API bool bvnr_write_float_dec(bvnr_writer_t* w, const char* key,
			   uint32_t width, double value);
BVN_API bool bvnr_write_uint_unit(bvnr_writer_t* w, const char* key,
			   uint32_t width, uint64_t value, value_unit_t unit);
BVN_API bool bvnr_write_sint_unit(bvnr_writer_t* w, const char* key,
			   uint32_t width, int64_t value, value_unit_t unit);
BVN_API bool bvnr_write_float_unit(bvnr_writer_t* w, const char* key,
				uint32_t width, double value, value_unit_t unit);
BVN_API bool bvnr_write_float_fix_unit(bvnr_writer_t* w, const char* key,
				uint32_t width, uint32_t q,
				double value, value_unit_t unit);
BVN_API bool bvnr_write_float_dec_unit(bvnr_writer_t* w, const char* key,
				uint32_t width,
				double value, value_unit_t unit);
BVN_API bool bvnr_write_bvnf(bvnr_writer_t* w, const char* key,
			 const bvn_float_t* f, uint32_t width);
BVN_API bool bvnr_write_bvnf_unit(bvnr_writer_t* w, const char* key,
			   const bvn_float_t* f,
			   uint32_t width, value_unit_t unit);
BVN_API bool bvnr_write_bvnf_base(bvnr_writer_t* w, const char* key,
			   const bvn_float_t* f,
			   uint32_t width, uint32_t base);
BVN_API bool bvnr_write_bvnf_base_unit(bvnr_writer_t* w, const char* key,
				const bvn_float_t* f,
				uint32_t width, uint32_t base,
				value_unit_t unit);
BVN_API bool bvnr_write_bvni(bvnr_writer_t* w, const char* key,
		     const bvn_int_t* n,
		     uint32_t width, uint32_t base);
BVN_API bool bvnr_write_bvni_unit(bvnr_writer_t* w, const char* key,
			   const bvn_int_t* n,
			   uint32_t width, uint32_t base,
			   value_unit_t unit);
BVN_API bool bvnr_write_struct_start(bvnr_writer_t* w, const char* key);
BVN_API bool bvnr_write_struct_end  (bvnr_writer_t* w);
#ifdef __cplusplus
}
#endif
#endif
