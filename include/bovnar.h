#ifndef BOVNAR_H_
#define BOVNAR_H_
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "bvn_float.h"
#include "bvn_int.h"
#ifdef __cplusplus
extern "C" {
#endif
#define BVNR_MAX_UNIT_COMPONENTS		8
#define BVN_MAX_INT_WIDTH			32768u
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
	error_expected_string_in_array      = 23,
	error_expected_number_in_array      = 24,
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
	error_recovered                     = 37,
	error_unit_mismatch                 = 38
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
	bu_delisle, bu_newton_temp, bu_reaumur, bu_romer
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
} bvnr_write_flags_t;
typedef struct bvnr_source_s bvnr_source_t;
typedef struct bvnr_sink_s   bvnr_sink_t;
typedef bool (*bvnr_pull_fn)
	(bvnr_source_t* s, void* buf, uint32_t want, uint32_t* got);
typedef bool (*bvnr_push_fn)
	(bvnr_sink_t* s, const void* buf, uint32_t len);
typedef bool (*bvnr_flush_fn)
	(bvnr_sink_t* s);
struct bvnr_source_s {
	bvnr_pull_fn	pull;
	int		fd;
	const uint8_t*	mem_ptr;
	uint64_t	mem_left;
};
struct bvnr_sink_s {
	bvnr_push_fn	push;
	bvnr_flush_fn	flush;
	int		fd;
	bool		is_mem;
	uint8_t*	mem_ptr;
	uint32_t	mem_left;
	uint64_t	mem_written;
};
static inline bool bvn_source_pull(
	bvnr_source_t* s, void* buf, uint32_t want, uint32_t* got)
{
	return s->pull(s, buf, want, got);
}
static inline bool bvn_sink_push(
	bvnr_sink_t* s, const void* buf, uint32_t len)
{
	return s->push(s, buf, len);
}
typedef struct bvnr_reader_s bvnr_reader_t;
typedef struct bvnr_writer_s bvnr_writer_t;
#define BVN_TYPE_PLAIN \
	((value_type_spec_t){ .family = vt_plain, .width = 0,  .base = 0  })
#define BVN_TYPE_UTF8 \
	((value_type_spec_t){ .family = vt_utf8,  .width = 0,  .base = 0  })
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
	if (s.family == vt_float_fix || s.family == vt_float_dec)
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
void bvnr_source_from_fd(bvnr_source_t* s, int fd);
void bvnr_source_from_mem(bvnr_source_t* s, const void* buf, uint64_t len);
void bvnr_sink_to_fd(bvnr_sink_t* s, int fd);
void bvnr_sink_to_mem(bvnr_sink_t* s, void* buf, uint32_t cap);
uint64_t bvnr_sink_bytes_written(const bvnr_sink_t* s);
bvnr_reader_t* bvnr_reader_create(void);
void           bvnr_reader_destroy(bvnr_reader_t* r);
bool bvnr_open_read_source(
	bvnr_reader_t* r, const bvnr_source_t* src,
	const bvnr_sink_t* src_mirror, bvnr_read_flags_t* options);
bool bvnr_open_read_mem(
	bvnr_reader_t* r, const void* buf, uint64_t len,
	void* mirror_buf, uint32_t mirror_cap,
	bvnr_read_flags_t* options);
bool bvnr_read(bvnr_reader_t* r);
typedef struct bvnr_canon_observer_s bvnr_canon_observer_t;
bvnr_canon_observer_t* bvnr_canon_observer_create(
	const bvnr_sink_t* sink, bool pretty);
void bvnr_canon_observer_destroy(bvnr_canon_observer_t* obs);
bool bvnr_canon_observer_on_event(
	void* obs, bvnr_event_t ev, bvnr_data_t* data);
bool bvnr_canon_observer_finish(bvnr_canon_observer_t* obs);
error_code_t bvnr_reader_get_error(const bvnr_reader_t* r);
uint64_t     bvnr_reader_get_error_line  (const bvnr_reader_t* r);
uint64_t     bvnr_reader_get_error_column(const bvnr_reader_t* r);
uint32_t     bvnr_reader_get_error_byte  (const bvnr_reader_t* r);
uint64_t     bvnr_reader_get_error_offset(const bvnr_reader_t* r);
uint64_t     bvnr_reader_get_recovery_count(const bvnr_reader_t* r);
bvnr_writer_t* bvnr_writer_create(void);
void           bvnr_writer_destroy(bvnr_writer_t* w);
bool bvnr_open_write_sink(
	bvnr_writer_t* w, const bvnr_sink_t* sink,
	bool pretty, bvnr_write_flags_t* options);
bool bvnr_open_write_mem(
	bvnr_writer_t* w, void* buf, uint32_t cap,
	bool pretty, bvnr_write_flags_t* options);
bool bvnr_write_event(
	bvnr_writer_t* w, bvnr_event_t ev, bvnr_data_t* data);
bool bvnr_write_finish(bvnr_writer_t* w);
error_code_t bvnr_writer_get_error(const bvnr_writer_t* w);
uint64_t     bvnr_writer_get_error_offset(const bvnr_writer_t* w);
uint64_t     bvnr_writer_bytes_written(const bvnr_writer_t* w);
const char*  bvn_error_to_string(error_code_t code);
value_unit_t bvn_parse_unit(const uint8_t* unit, bool* ok);
value_unit_t bvn_parse_unit_n(const uint8_t* unit, uint32_t len, bool* ok);
int32_t      bvn_unit_to_string(value_unit_t u, char* buf, size_t bufsize);
int32_t      bvn_unit_to_string_ex(value_unit_t u, char* buf, size_t bufsize,
                                    bvn_unit_flags_t flags);
bool         bvn_unit_valid(value_unit_t u);
bool         bvn_unit_equal(value_unit_t a, value_unit_t b);
double       bvn_unit_prefix_factor(value_unit_t u);
int32_t      bvn_unit_prefix_exponent(value_unit_t u);
bvn_unit_flags_t bvnr_writer_unit_flags(const bvnr_writer_t* w);
value_type_spec_t bvn_parse_type_annotation(
	const uint8_t* str, uint32_t len,
	bool* type_ok, bool* unit_ok, bool* unit_too_long,
	value_unit_t* out_unit,
	uint8_t* unit_buf, uint8_t* unit_buf_len);
bool bvn_validate_identifier(const char* id);
bool bvn_validate_symbol(const char* surr);
bool bvn_validate_reference(const char* link);
bool bvn_validate_number(const char* s);
bool bvn_is_special_number_string(const char* s);
bool bvn_validate_digits_for_base(const char* s, uint32_t base);
bool bvn_validate_number_in_base(const char* s, uint32_t base);
bool bvn_validate_uint_range(const char* s, uint32_t w, uint32_t base);
bool bvn_validate_sint_range(const char* s, uint32_t w, uint32_t base);
bool bvn_validate_string(const uint8_t* data, size_t len);
uint32_t bvn_char_to_digit(uint32_t c, uint32_t base);
uint32_t bvn_min_digits_for_type(value_type_spec_t vt);
int32_t bvn_format_uint64(
	char* buf, size_t bufsize, uint64_t value,
	uint32_t base, uint32_t min_digits);
int32_t bvn_format_int64(
	char* buf, size_t bufsize, int64_t value,
	uint32_t base, uint32_t min_digits);
int32_t bvn_format_double(
	char* buf, size_t bufsize, double value, value_type_spec_t vt);
bool bvn_parse_int64(const char* s, value_type_spec_t vt, int64_t* out);
bool bvn_parse_uint64(const char* s, value_type_spec_t vt, uint64_t* out);
bool bvn_parse_double(const char* s, value_type_spec_t vt, double* out);
bool bvn_parse_double_in_base(const char* s, uint32_t base, double* out);
bool bvn_looks_like_double(const char* s);
const uint8_t* bvn_get_escape_repl_table(void);
bool bvnr_write_type_annotation(bvnr_writer_t* w,
				value_type_spec_t vt,
				value_unit_t vu);
bool bvnr_write_string(bvnr_writer_t* w, const char* key, const char* value);
bool bvnr_write_plain (bvnr_writer_t* w, const char* key, const char* value);
bool bvnr_write_null  (bvnr_writer_t* w, const char* key);
bool bvnr_write_bool  (bvnr_writer_t* w, const char* key, bool value);
bool bvnr_write_uint(bvnr_writer_t* w, const char* key,
			 uint32_t width, uint64_t value);
bool bvnr_write_sint(bvnr_writer_t* w, const char* key,
			 uint32_t width, int64_t value);
bool bvnr_write_float(bvnr_writer_t* w, const char* key,
			  uint32_t width, double value);
bool bvnr_write_float_fix(bvnr_writer_t* w, const char* key,
			   uint32_t width, uint32_t q, double value);
bool bvnr_write_float_dec(bvnr_writer_t* w, const char* key,
			   uint32_t width, double value);
bool bvnr_write_uint_unit(bvnr_writer_t* w, const char* key,
			   uint32_t width, uint64_t value, value_unit_t unit);
bool bvnr_write_sint_unit(bvnr_writer_t* w, const char* key,
			   uint32_t width, int64_t value, value_unit_t unit);
bool bvnr_write_float_unit(bvnr_writer_t* w, const char* key,
				uint32_t width, double value, value_unit_t unit);
bool bvnr_write_float_fix_unit(bvnr_writer_t* w, const char* key,
				uint32_t width, uint32_t q,
				double value, value_unit_t unit);
bool bvnr_write_float_dec_unit(bvnr_writer_t* w, const char* key,
				uint32_t width,
				double value, value_unit_t unit);
bool bvnr_write_bvnf(bvnr_writer_t* w, const char* key,
			 const bvn_float_t* f, uint32_t width);
bool bvnr_write_bvnf_unit(bvnr_writer_t* w, const char* key,
			   const bvn_float_t* f,
			   uint32_t width, value_unit_t unit);
bool bvnr_write_bvnf_base(bvnr_writer_t* w, const char* key,
			   const bvn_float_t* f,
			   uint32_t width, uint32_t base);
bool bvnr_write_bvnf_base_unit(bvnr_writer_t* w, const char* key,
				const bvn_float_t* f,
				uint32_t width, uint32_t base,
				value_unit_t unit);
bool bvnr_write_bvni(bvnr_writer_t* w, const char* key,
		     const bvn_int_t* n,
		     uint32_t width, uint32_t base);
bool bvnr_write_bvni_unit(bvnr_writer_t* w, const char* key,
			   const bvn_int_t* n,
			   uint32_t width, uint32_t base,
			   value_unit_t unit);
bool bvnr_write_struct_start(bvnr_writer_t* w, const char* key);
bool bvnr_write_struct_end  (bvnr_writer_t* w);
#ifdef __cplusplus
}
#endif
#endif
