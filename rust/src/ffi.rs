// SPDX-License-Identifier: MIT
//! Hand-written declarations of the C API in `bovnar.h`: its functions and
//! the layouts of the structs passed by value.
//!
//! This is the only module allowed to use `unsafe`. Besides the raw
//! declarations it holds the small safe handles the other modules build on, so
//! every pointer the library hands out is dereferenced here and nowhere else.

// The declaration list covers the C API; the safe layer above it does not
// reach every function yet.
#![allow(dead_code)]

use std::ffi::{CStr, CString, c_char, c_int, c_void};

/// Storage reserved for the opaque `bvnr_source_t` / `bvnr_sink_t`. C uses `BVNR_SOURCE_RESERVED_SIZE` /
/// `BVNR_SINK_RESERVED_SIZE` bytes (64 each today); 256 is a deliberate
/// margin, since a mirror may be larger than the C struct (C writes within
/// its own bounds) but never smaller. The ABI unit test asserts
/// `size >= C's size` against the live layout, so a C reserve that outgrows
/// this fails the tests instead of corrupting memory.
pub const OPAQUE_BYTES: usize = 256;
/// `BVN_MAX_UNIT_COMPONENTS`.
pub const MAX_UNIT_COMPONENTS: usize = 32;
/// `BVNR_UNIT_STRING_MAX`: the longest unit string the library writes.
pub const UNIT_STRING_MAX: usize = 1088;

/// `bvnr_on_error_fn`.
pub type OnErrorFn = extern "C" fn(*mut c_void, c_int, u64, u64, u32, u64);
/// `bvnr_event_callback`: `bool (*)(void*, bvnr_event_t, const bvnr_data_t*)`.
pub type EventCallbackFn = extern "C" fn(*mut c_void, c_int, *const BvnrData) -> bool;
/// `bvnr_read_flags_t.want_unit`.
pub type WantUnitFn = extern "C" fn(*mut c_void, *const BvnrData, *mut ValueUnit, *mut u32) -> bool;
/// `bvnr_doc_stream_opts_t.on_document`.
pub type OnDocumentFn = extern "C" fn(*mut c_void, u64, bool, c_int) -> bool;
/// `bvnr_mux_on_msg_fn`.
pub type MuxOnMsgFn = extern "C" fn(*mut c_void, u64, *const c_void, u64) -> bool;
/// `bvnr_pull_fn`.
pub type PullFn = extern "C" fn(*mut BvnrSource, *mut c_void, u32, *mut u32) -> bool;
/// `bvnr_push_fn`.
pub type PushFn = extern "C" fn(*mut BvnrSink, *const c_void, u32) -> bool;
/// `bvnr_flush_fn`.
pub type FlushFn = extern "C" fn(*mut BvnrSink) -> bool;
/// `BVNR_NEXT_ERROR`, `BVNR_NEXT_NONE`, `BVNR_NEXT_DOCUMENT` /
/// `BVNR_NEXT_MESSAGE` and `BVNR_NEXT_EVENT` (`bovnar_stream.h`).
pub const NEXT_ERROR: c_int = -1;
pub const NEXT_NONE: c_int = 0;
pub const NEXT_DOCUMENT: c_int = 1;
pub const NEXT_MESSAGE: c_int = 1;
pub const NEXT_EVENT: c_int = 2;

#[repr(C)]
#[derive(Clone, Copy)]
pub union PrefixId {
    pub si: c_int,
    pub iec: c_int,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct ValueUnitPrefix {
    pub system: c_int,
    pub id: PrefixId,
}

impl ValueUnitPrefix {
    /// The prefix id, whichever system it belongs to.
    pub fn id_raw(&self) -> c_int {
        // SAFETY: both union fields are a C int, so either reads the same int.
        unsafe { self.id.si }
    }
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct ValueUnitComponent {
    pub base: c_int,
    pub exponent: c_int,
    pub prefix: ValueUnitPrefix,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct ValueUnit {
    pub num_components: u32,
    pub components: [ValueUnitComponent; MAX_UNIT_COMPONENTS],
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, Hash)]
pub struct ValueTypeSpec {
    pub family: c_int,
    pub width: u32,
    pub base: u32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct BvnrConverted {
    pub unit: ValueUnit,
    pub text: *const c_char,
    pub length: u32,
    pub base: u32,
    pub num: *mut c_void,
    pub den: *mut c_void,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct BvnrData {
    pub type_: c_int,
    pub value_type: ValueTypeSpec,
    pub value_unit: ValueUnit,
    pub data: *const c_void,
    pub length: u32,
    pub frac_data: *const c_void,
    pub frac_length: u32,
    pub unit_source: *const c_void,
    pub unit_source_length: u32,
    pub converted: bool,
    pub conv: BvnrConverted,
}

#[repr(C)]
pub struct BvnrSource {
    pub opaque: [u64; OPAQUE_BYTES / 8],
}

#[repr(C)]
pub struct BvnrSink {
    pub opaque: [u64; OPAQUE_BYTES / 8],
}

#[repr(C)]
pub struct BvnrReadFlags {
    pub max_identifier_length: u16,
    pub max_string_length: u16,
    pub max_number_length: u16,
    pub max_symbol_length: u16,
    pub max_reference_length: u16,
    pub max_array_items: u64,
    pub max_text_bytes: u64,
    pub max_file_size: u64,
    pub max_struct_nesting: u8,
    pub max_array_nesting: u8,
    pub userdata: *mut c_void,
    pub on_unverified: Option<EventCallbackFn>,
    pub on_verified: Option<EventCallbackFn>,
    pub continue_on_error: bool,
    pub on_error: Option<OnErrorFn>,
    pub strict_version: bool,
    pub text_only: bool,
    pub want_unit_allow_nonterminating: bool,
    pub max_conversion_length: u32,
    pub want_unit: Option<WantUnitFn>,
    pub _reserved: [u64; 2],
}

#[repr(C)]
pub struct BvnrUnitTarget {
    pub unit: *const c_char,
    pub base: u32,
}

#[repr(C)]
pub struct BvnrUnitRule {
    pub path: *const c_char,
    pub unit: *const c_char,
    pub base: u32,
    pub mode: c_int,
}

#[repr(C)]
pub struct BvnrUnitPolicy {
    pub rules: *const BvnrUnitRule,
    pub num_rules: u32,
    pub targets: *const BvnrUnitTarget,
    pub num_targets: u32,
    pub base: u32,
    pub normalise: c_int,
    pub on_inexact: c_int,
    pub require_unit: bool,
    pub require_dimension_of: *const *const c_char,
    pub num_require_dimension_of: u32,
}

#[repr(C)]
pub struct BvnrWriteFlags {
    pub max_identifier_length: u16,
    pub max_string_length: u16,
    pub max_number_length: u16,
    pub max_symbol_length: u16,
    pub max_reference_length: u16,
    pub max_array_items: u64,
    pub max_text_bytes: u64,
    pub max_file_size: u64,
    pub max_struct_nesting: u8,
    pub max_array_nesting: u8,
    pub userdata: *mut c_void,
    pub on_event: Option<EventCallbackFn>,
    pub continue_on_error: bool,
    pub on_error: Option<OnErrorFn>,
    pub unit_flags: u32,
    pub emit_version: bool,
    pub _reserved: [u64; 4],
}

#[repr(C)]
pub struct BvnrDocStreamOpts {
    pub flags: *mut BvnrReadFlags,
    pub userdata: *mut c_void,
    pub on_document: Option<OnDocumentFn>,
    pub continue_past_failed: bool,
    pub max_document_size: u64,
}

/// `bvnr_demux_item_t`: what `bvnr_demux_next` hands out.
#[repr(C)]
pub struct BvnrDemuxItem {
    pub channel: u64,
    pub data: *const u8,
    pub length: u64,
    pub event: c_int,
    pub value: *const BvnrData,
}

#[repr(C)]
pub struct BvnDomEntry {
    pub key: *const c_char,
    pub value: *mut c_void,
}

#[repr(C)]
pub struct BvnCurrencyInfo {
    pub code: [c_char; 5],
    pub numeric_code: u16,
    pub minor_unit: u8,
    pub is_crypto: bool,
    pub name: [c_char; 48],
}

/// Implements `zeroed()` for the plain-data structs above.
macro_rules! zeroed {
    ($($t:ty),*) => {$(
        impl $t {
            /// The all-zero value, which is what the C API expects as "unset"
            /// (`= {0}` in C): no data, no unit, defaults for every limit.
            pub fn zeroed() -> Self {
                // SAFETY: every field is an integer, bool, raw pointer, array
                // of those, or Option<fn>; all-zero is a valid value of each.
                unsafe { std::mem::zeroed() }
            }
        }
    )*};
}
zeroed!(ValueUnit, BvnrData, BvnrReadFlags, BvnrWriteFlags);

// Raw declarations.
unsafe extern "C" {
    pub fn bvnr_reader_create() -> *mut c_void;
    pub fn bvnr_reader_destroy(r: *mut c_void);
    pub fn bvnr_canon_observer_create(sink: *mut BvnrSink, pretty: bool) -> *mut c_void;
    pub fn bvnr_canon_observer_set_version(obs: *mut c_void, major: u16, minor: u16);
    pub fn bvnr_canon_observer_destroy(obs: *mut c_void);
    pub fn bvnr_canon_observer_on_event(obs: *mut c_void, ev: c_int, data: *mut BvnrData) -> bool;
    pub fn bvnr_canon_observer_finish(obs: *mut c_void) -> bool;
    pub fn bvnr_source_from_fd(s: *mut BvnrSource, fd: c_int);
    pub fn bvnr_source_from_mem(s: *mut BvnrSource, buf: *mut c_void, len: u64);
    pub fn bvnr_open_read_source(
        r: *mut c_void,
        src: *mut BvnrSource,
        src_mirror: *mut BvnrSink,
        options: *mut BvnrReadFlags,
    ) -> bool;
    pub fn bvnr_open_read_mem(
        r: *mut c_void,
        buf: *mut c_void,
        len: u64,
        mirror_buf: *mut c_void,
        mirror_cap: u64,
        options: *mut BvnrReadFlags,
    ) -> bool;
    pub fn bvnr_read(r: *mut c_void) -> bool;
    pub fn bvnr_reader_get_error(r: *mut c_void) -> c_int;
    pub fn bvnr_reader_get_error_line(r: *mut c_void) -> u64;
    pub fn bvnr_reader_get_error_column(r: *mut c_void) -> u64;
    pub fn bvnr_reader_get_error_offset(r: *mut c_void) -> u64;
    pub fn bvnr_reader_get_error_byte(r: *mut c_void) -> u32;
    pub fn bvnr_reader_get_recovery_count(r: *mut c_void) -> u64;
    pub fn bvnr_reader_get_skipped_bytes(r: *mut c_void) -> u64;
    pub fn bvnr_reader_get_declared_version(
        r: *mut c_void,
        major: *mut u16,
        minor: *mut u16,
    ) -> bool;
    pub fn bvnr_peek_version(buf: *mut c_void, len: u64, major: *mut u16, minor: *mut u16) -> bool;
    pub fn bvnr_version() -> u32;
    pub fn bvnr_version_string() -> *const c_char;
    pub fn bvnr_spec_version(major: *mut u16, minor: *mut u16);
    pub fn bvnr_write_version(w: *mut c_void, major: u16, minor: u16) -> bool;
    pub fn bvnr_datetime_epoch_name(vt: ValueTypeSpec) -> *const c_char;
    pub fn bvnr_datetime_epoch_mjd(vt: ValueTypeSpec) -> i32;
    pub fn bvnr_datetime_epoch_index(name: *const c_char) -> i32;
    pub fn bvnr_writer_create() -> *mut c_void;
    pub fn bvnr_writer_destroy(w: *mut c_void);
    pub fn bvnr_sink_to_fd(s: *mut BvnrSink, fd: c_int);
    pub fn bvnr_sink_to_mem(s: *mut BvnrSink, buf: *mut c_void, cap: u64);
    pub fn bvnr_sink_bytes_written(s: *mut BvnrSink) -> u64;
    pub fn bvnr_open_write_sink(
        w: *mut c_void,
        sink: *mut BvnrSink,
        pretty: bool,
        options: *mut BvnrWriteFlags,
    ) -> bool;
    pub fn bvnr_open_write_mem(
        w: *mut c_void,
        buf: *mut c_void,
        cap: u64,
        pretty: bool,
        options: *mut BvnrWriteFlags,
    ) -> bool;
    pub fn bvnr_write_event(w: *mut c_void, ev: c_int, data: *mut BvnrData) -> bool;
    pub fn bvnr_write_finish(w: *mut c_void) -> bool;
    pub fn bvnr_write_flush(w: *mut c_void) -> bool;
    pub fn bvnr_writer_get_error(w: *mut c_void) -> c_int;
    pub fn bvnr_writer_get_error_offset(w: *mut c_void) -> u64;
    pub fn bvnr_writer_bytes_written(w: *mut c_void) -> u64;
    pub fn bvnr_writer_unit_flags(w: *mut c_void) -> u32;
    pub fn bvn_parse_uint64(s: *const c_char, vt: ValueTypeSpec, out: *mut u64) -> bool;
    pub fn bvn_parse_int64(s: *const c_char, vt: ValueTypeSpec, out: *mut i64) -> bool;
    pub fn bvn_parse_double(s: *const c_char, vt: ValueTypeSpec, out: *mut f64) -> bool;
    pub fn bvn_format_uint64(
        buf: *mut c_char,
        bufsize: usize,
        value: u64,
        base: u32,
        min_digits: u32,
    ) -> i32;
    pub fn bvn_format_int64(
        buf: *mut c_char,
        bufsize: usize,
        value: i64,
        base: u32,
        min_digits: u32,
    ) -> i32;
    pub fn bvn_format_double(
        buf: *mut c_char,
        bufsize: usize,
        value: f64,
        vt: ValueTypeSpec,
    ) -> i32;
    pub fn bvn_parse_unit(unit: *const u8, ok: *mut bool) -> ValueUnit;
    pub fn bvn_parse_unit_n(unit: *const u8, len: u32, ok: *mut bool) -> ValueUnit;
    pub fn bvn_unit_to_string(u: ValueUnit, buf: *mut c_char, bufsize: usize) -> i32;
    pub fn bvn_unit_to_string_ex(u: ValueUnit, buf: *mut c_char, bufsize: usize, flags: u32)
    -> i32;
    pub fn bvn_unit_valid(u: ValueUnit) -> bool;
    pub fn bvn_unit_error_code(unit: *const u8, len: u32) -> c_int;
    pub fn bvn_unit_is_profile_only(u: ValueUnit) -> bool;
    pub fn bvn_unit_profile_count() -> u32;
    pub fn bvn_unit_profile_name(index: u32) -> *const c_char;
    pub fn bvn_unit_to_profile(
        ns: *const c_char,
        u: ValueUnit,
        buf: *mut c_char,
        bufsize: usize,
    ) -> i32;
    pub fn bvn_unit_to_ucum(u: ValueUnit, buf: *mut c_char, bufsize: usize) -> i32;
    pub fn bvn_unit_prefix_factor(u: ValueUnit) -> f64;
    pub fn bvn_unit_prefix_exponent(u: ValueUnit) -> i32;
    pub fn bvn_units_compatible(a: ValueUnit, b: ValueUnit) -> bool;
    pub fn bvn_unit_equal(a: ValueUnit, b: ValueUnit) -> bool;
    pub fn bvn_units_convertible(a: ValueUnit, b: ValueUnit) -> bool;
    pub fn bvn_unit_si_normal_form(u: ValueUnit, out: *mut ValueUnit) -> bool;
    pub fn bvnr_reader_set_unit_policy(r: *mut c_void, policy: *mut BvnrUnitPolicy) -> bool;
    pub fn bvnr_writer_set_unit_policy(w: *mut c_void, policy: *mut BvnrUnitPolicy) -> bool;
    pub fn bvn_unit_convert_factor(
        a: ValueUnit,
        b: ValueUnit,
        ok: *mut bool,
        requires_affine: *mut bool,
    ) -> f64;
    pub fn bvn_unit_convert_value(
        value: f64,
        from: ValueUnit,
        to: ValueUnit,
        out: *mut f64,
    ) -> bool;
    pub fn bvn_unit_to_si_factor(
        u: ValueUnit,
        is_affine: *mut bool,
        affine_offset: *mut f64,
        ok: *mut bool,
    ) -> f64;
    pub fn bvn_rational_str_bufsize(num: *mut c_void, den: *mut c_void, base: u32) -> usize;
    pub fn bvn_rational_to_str(
        num: *mut c_void,
        den: *mut c_void,
        base: u32,
        buf: *mut c_char,
        bufsize: usize,
        exact: *mut bool,
    ) -> i32;
    pub fn bvn_int_bitlen(n: *mut c_void) -> c_int;
    pub fn bvn_int_alloc() -> *mut c_void;
    pub fn bvn_int_free(n: *mut c_void);
    pub fn bvn_int_from_str(n: *mut c_void, s: *const c_char, base: u32) -> bool;
    pub fn bvn_unit_convert_rational(
        vnum: *const c_void,
        vden: *const c_void,
        from: ValueUnit,
        to: ValueUnit,
        out_num: *mut c_void,
        out_den: *mut c_void,
        exact: *mut bool,
    ) -> bool;
    pub fn bvn_int_str_bufsize(bits: u32, base: u32) -> usize;
    pub fn bvn_int_to_str(n: *mut c_void, buf: *mut c_char, bufsize: usize, base: u32) -> i32;
    pub fn bvn_unit_dimension_vector(u: ValueUnit, dims: *mut i32) -> bool;
    pub fn bvn_unit_reduce(u: ValueUnit, scale: *mut f64, overflow: *mut bool) -> ValueUnit;
    pub fn bvn_exponent_to_int(e: c_int) -> i32;
    pub fn bvn_int_to_exponent(n: i32) -> c_int;
    pub fn bvn_prefix_unit_valid(prefix: ValueUnitPrefix, base: c_int) -> bool;
    pub fn bvn_validate_identifier(id: *const c_char) -> bool;
    pub fn bvn_validate_symbol(surr: *const c_char) -> bool;
    pub fn bvn_validate_reference(link: *const c_char) -> bool;
    pub fn bvn_validate_uint_range(s: *const c_char, w: u32, base: u32) -> bool;
    pub fn bvn_validate_sint_range(s: *const c_char, w: u32, base: u32) -> bool;
    pub fn bvn_is_special_number_string(s: *const c_char) -> bool;
    pub fn bvn_error_to_string(code: c_int) -> *const c_char;
    pub fn bvn_dom_doc_create() -> *mut c_void;
    pub fn bvn_dom_doc_destroy(doc: *mut c_void);
    pub fn bvn_dom_parse(data: *mut c_void, len: u32) -> *mut c_void;
    pub fn bvn_dom_parse_policy(
        data: *mut c_void,
        len: u32,
        policy: *mut BvnrUnitPolicy,
    ) -> *mut c_void;
    pub fn bvn_dom_parse_fd_policy(
        fd: c_int,
        max_bytes: u64,
        policy: *mut BvnrUnitPolicy,
    ) -> *mut c_void;
    pub fn bvn_dom_parse_fd(fd: c_int) -> *mut c_void;
    pub fn bvn_dom_doc_get_parse_error(doc: *mut c_void) -> c_int;
    pub fn bvn_dom_lookup(doc: *mut c_void, path: *const c_char) -> *mut c_void;
    pub fn bvn_dom_struct_get(node: *mut c_void, key: *const c_char) -> *mut c_void;
    pub fn bvn_dom_array_at(node: *mut c_void, index: u32) -> *mut c_void;
    pub fn bvn_dom_node_type(node: *mut c_void) -> c_int;
    pub fn bvn_dom_is_null(node: *mut c_void) -> bool;
    pub fn bvn_dom_get_float(node: *mut c_void, out: *mut f64) -> bool;
    pub fn bvn_dom_get_bool(node: *mut c_void, out: *mut bool) -> bool;
    pub fn bvn_dom_get_string(node: *mut c_void, out: *mut *const c_void, len: *mut u32) -> bool;
    pub fn bvn_dom_get_symbol(node: *mut c_void, out: *mut *const c_void, len: *mut u32) -> bool;
    pub fn bvn_dom_get_reference(node: *mut c_void, out: *mut *const c_void, len: *mut u32)
    -> bool;
    pub fn bvn_dom_get_octets(node: *mut c_void, out: *mut *const c_void, len: *mut u32) -> bool;
    pub fn bvn_dom_get_value_type(node: *mut c_void) -> ValueTypeSpec;
    pub fn bvn_dom_get_datetime_fraction(node: *mut c_void, len_out: *mut u32) -> *const c_char;
    pub fn bvn_dom_get_unit_source(node: *mut c_void, len_out: *mut u32) -> *const c_char;
    pub fn bvn_dom_get_unit(node: *mut c_void) -> ValueUnit;
    pub fn bvn_dom_get_unit_string(node: *mut c_void, buf: *mut c_char, bufsize: usize) -> i32;
    pub fn bvn_dom_get_value_in_base_units(node: *mut c_void) -> f64;
    pub fn bvn_dom_struct_count(node: *mut c_void) -> u32;
    pub fn bvn_dom_array_count(node: *mut c_void) -> u32;
    pub fn bvn_dom_array_dims(node: *mut c_void) -> u32;
    pub fn bvn_dom_struct_entries(node: *mut c_void) -> *mut BvnDomEntry;
    pub fn bvn_dom_doc_entries(doc: *mut c_void) -> *mut BvnDomEntry;
    pub fn bvn_dom_doc_count(doc: *mut c_void) -> u32;
    pub fn bvn_dom_get_i64(node: *mut c_void, out: *mut i64) -> bool;
    pub fn bvn_dom_get_u64(node: *mut c_void, out: *mut u64) -> bool;
    pub fn bvn_dom_get_i32(node: *mut c_void, out: *mut i32) -> bool;
    pub fn bvn_dom_get_u32(node: *mut c_void, out: *mut u32) -> bool;
    pub fn bvn_dom_get_i16(node: *mut c_void, out: *mut i16) -> bool;
    pub fn bvn_dom_get_u16(node: *mut c_void, out: *mut u16) -> bool;
    pub fn bvn_dom_get_i8(node: *mut c_void, out: *mut i8) -> bool;
    pub fn bvn_dom_get_u8(node: *mut c_void, out: *mut u8) -> bool;
    pub fn bvn_dom_int_to_str(node: *mut c_void, base: u32) -> *mut c_void;
    pub fn bvn_dom_free_string(s: *mut c_void);
    pub fn bvnr_frame_write(sink: *mut BvnrSink, doc: *mut c_void, doc_len: u64) -> bool;
    pub fn bvnr_doc_stream_read(
        src: *mut BvnrSource,
        opts: *mut BvnrDocStreamOpts,
        out_count: *mut u64,
    ) -> bool;
    pub fn bvnr_doc_stream_read_ex(
        src: *mut BvnrSource,
        opts: *mut BvnrDocStreamOpts,
        out_count: *mut u64,
        out_error: *mut c_int,
    ) -> bool;
    pub fn bvnr_mux_begin(w: *mut c_void, key: *const c_char) -> bool;
    pub fn bvnr_mux_send(w: *mut c_void, channel: u64, data: *mut c_void, len: u64) -> bool;
    pub fn bvnr_mux_end(w: *mut c_void) -> bool;
    pub fn bvnr_demux_create(
        on_message: Option<MuxOnMsgFn>,
        userdata: *mut c_void,
        max_message: u64,
    ) -> *mut c_void;
    pub fn bvnr_demux_destroy(dm: *mut c_void);
    pub fn bvnr_demux_set_key(dm: *mut c_void, key: *const c_char) -> bool;
    pub fn bvnr_demux_on_event(demux: *mut c_void, ev: c_int, d: *mut BvnrData) -> bool;
    pub fn bvnr_demux_error(dm: *mut c_void) -> c_int;
    pub fn bvnr_embed_document(
        w: *mut c_void,
        key: *const c_char,
        doc: *mut c_void,
        doc_len: u64,
    ) -> bool;
    pub fn bvnr_parse_embedded(doc: *mut c_void, doc_len: u64, flags: *mut BvnrReadFlags) -> bool;
    pub fn bvnr_source_from_callback(s: *mut BvnrSource, pull: PullFn, userdata: *mut c_void);
    pub fn bvnr_sink_to_callback(
        s: *mut BvnrSink,
        push: PushFn,
        flush: Option<FlushFn>,
        userdata: *mut c_void,
    );
    pub fn bvnr_source_get_userdata(s: *const BvnrSource) -> *mut c_void;
    pub fn bvnr_sink_get_userdata(s: *const BvnrSink) -> *mut c_void;
    pub fn bvnr_open_read_feed(
        r: *mut c_void,
        src_mirror: *const BvnrSink,
        options: *mut BvnrReadFlags,
    ) -> bool;
    pub fn bvnr_read_feed(r: *mut c_void, buf: *const c_void, len: u64) -> bool;
    pub fn bvnr_read_finish(r: *mut c_void) -> bool;
    pub fn bvnr_frame_reader_create(opts: *const BvnrDocStreamOpts) -> *mut c_void;
    pub fn bvnr_frame_reader_create_pull(max_document_size: u64) -> *mut c_void;
    pub fn bvnr_frame_reader_destroy(fr: *mut c_void);
    pub fn bvnr_frame_reader_feed(fr: *mut c_void, buf: *const c_void, len: u64) -> bool;
    pub fn bvnr_frame_reader_next(fr: *mut c_void, doc: *mut *const u8, len: *mut u64) -> c_int;
    pub fn bvnr_frame_reader_finish(fr: *mut c_void) -> bool;
    pub fn bvnr_frame_reader_error(fr: *const c_void) -> c_int;
    pub fn bvnr_frame_reader_count(fr: *const c_void) -> u64;
    pub fn bvnr_demux_open_feed(
        dm: *mut c_void,
        flags: *const BvnrReadFlags,
        deliver_events: bool,
    ) -> bool;
    pub fn bvnr_demux_feed(dm: *mut c_void, buf: *const c_void, len: u64) -> bool;
    pub fn bvnr_demux_next(dm: *mut c_void, item: *mut BvnrDemuxItem) -> c_int;
    pub fn bvnr_demux_finish(dm: *mut c_void) -> bool;
    pub fn bvnr_demux_get_reader(dm: *const c_void) -> *const c_void;
    pub fn bvn_float_alloc(prec: u32) -> *mut c_void;
    pub fn bvn_float_free(f: *mut c_void);
    pub fn bvn_float_from_str(f: *mut c_void, s: *const c_char, base: u32) -> bool;
    pub fn bvn_float_to_str(f: *mut c_void, buf: *mut c_char, bufsize: usize, base: u32) -> i32;
    pub fn bvn_float_str_bufsize(prec: u32, base: u32) -> usize;
    pub fn bvn_float_to_double(f: *mut c_void, out: *mut f64) -> bool;
    pub fn bvn_float_strtoieee_bin(
        s: *const c_char,
        base: u32,
        exp_bits: u32,
        man_bits: u32,
        bias: i32,
        bits: *mut u32,
        bits32: c_int,
    );
    pub fn bvn_float_to_bin16(f: *mut c_void, out: *mut u16);
    pub fn bvn_float_to_bin32(f: *mut c_void, out: *mut u32);
    pub fn bvn_float_to_bin64(f: *mut c_void, out: *mut u64);
    pub fn bvn_float_from_bin16(f: *mut c_void, bits: u16) -> bool;
    pub fn bvn_float_from_bin32(f: *mut c_void, bits: u32) -> bool;
    pub fn bvn_float_from_bin64(f: *mut c_void, bits: u64) -> bool;
    pub fn bvn_float_to_bin128(f: *mut c_void, out: *mut u32);
    pub fn bvn_float_to_bin256(f: *mut c_void, out: *mut u32);
    pub fn bvn_float_from_bin128(f: *mut c_void, bits: *mut u32) -> bool;
    pub fn bvn_float_from_bin256(f: *mut c_void, bits: *mut u32) -> bool;
    pub fn bvn_float_strtodec(s: *const c_char, width: u32, out: *mut u32, bits32: c_int) -> bool;
    pub fn bvn_float_to_dec16(f: *mut c_void, out: *mut u16);
    pub fn bvn_float_to_dec32(f: *mut c_void, out: *mut u32);
    pub fn bvn_float_to_dec64(f: *mut c_void, out: *mut u64);
    pub fn bvn_float_to_dec128(f: *mut c_void, out: *mut u32);
    pub fn bvn_float_to_dec256(f: *mut c_void, out: *mut u32);
    pub fn bvn_float_from_dec16(f: *mut c_void, bits: u16) -> bool;
    pub fn bvn_float_from_dec32(f: *mut c_void, bits: u32) -> bool;
    pub fn bvn_float_from_dec64(f: *mut c_void, bits: u64) -> bool;
    pub fn bvn_float_from_dec128(f: *mut c_void, bits: *mut u32) -> bool;
    pub fn bvn_float_from_dec256(f: *mut c_void, bits: *mut u32) -> bool;
    pub fn bvn_float_to_fix16(f: *mut c_void, frac_bits: u32) -> i16;
    pub fn bvn_float_to_fix32(f: *mut c_void, frac_bits: u32) -> i32;
    pub fn bvn_float_to_fix64(f: *mut c_void, frac_bits: u32) -> i64;
    pub fn bvn_float_to_fix128(f: *mut c_void, frac_bits: u32, out: *mut u32);
    pub fn bvn_float_to_fix256(f: *mut c_void, frac_bits: u32, out: *mut u32);
    pub fn bvn_float_from_fix16(f: *mut c_void, bits: i16, frac_bits: u32) -> bool;
    pub fn bvn_float_from_fix32(f: *mut c_void, bits: i32, frac_bits: u32) -> bool;
    pub fn bvn_float_from_fix64(f: *mut c_void, bits: i64, frac_bits: u32) -> bool;
    pub fn bvn_float_from_fix128(f: *mut c_void, bits: *mut u32, frac_bits: u32) -> bool;
    pub fn bvn_float_from_fix256(f: *mut c_void, bits: *mut u32, frac_bits: u32) -> bool;
    pub fn bvn_float_fix_in_range(f: *mut c_void, total_bits: u32, frac_bits: u32) -> bool;
    pub fn bvn_float_str_fits_fix(
        s: *const c_char,
        base: u32,
        total_bits: u32,
        frac_bits: u32,
    ) -> bool;
    pub fn bvn_float_is_nan(f: *mut c_void) -> bool;
    pub fn bvn_float_is_inf(f: *mut c_void) -> bool;
    pub fn bvn_float_is_zero(f: *mut c_void) -> bool;
    pub fn bvn_float_is_neg(f: *mut c_void) -> bool;
    pub fn bvn_unit_is_currency(base: c_int) -> bool;
    pub fn bvn_unit_is_fiat(base: c_int) -> bool;
    pub fn bvn_unit_is_crypto(base: c_int) -> bool;
    pub fn bvn_currency_info(base: c_int) -> *const BvnCurrencyInfo;
    pub fn bvn_parse_currency_str(s: *const u8, len: u32) -> c_int;
}

/// `bvn_error_to_string`: the C name of an error code, such as
/// `"unexpected_input_byte"`.
pub fn error_name(code: c_int) -> &'static str {
    // SAFETY: the function returns a static NUL-terminated ASCII string and
    // never NULL, for any code (unknown ones included).
    let name = unsafe { std::ffi::CStr::from_ptr(bvn_error_to_string(code)) };
    name.to_str().unwrap_or("unknown")
}

/// Borrows `len` bytes at `ptr` as a slice; NULL or zero length is empty.
///
/// SAFETY: a non-NULL `ptr` must point at `len` readable bytes that stay
/// valid and unchanged for `'a`.
unsafe fn bytes<'a>(ptr: *const c_void, len: u32) -> &'a [u8] {
    if ptr.is_null() || len == 0 {
        &[]
    } else {
        // SAFETY: guaranteed by the caller.
        unsafe { std::slice::from_raw_parts(ptr.cast::<u8>(), len as usize) }
    }
}

/// One reader event as the callback sees it. Borrowed: valid only for the
/// duration of the callback.
#[derive(Clone, Copy)]
pub struct EventView<'a> {
    pub event: c_int,
    pub data: Option<&'a BvnrData>,
    /// `data.data[..data.length]`, or empty.
    pub raw: &'a [u8],
    /// A datetime's sub-second digits, or empty.
    pub frac: &'a [u8],
    /// The unit as the document spelled it, or empty.
    pub unit_source: &'a [u8],
    /// The converted literal when `data.converted`, or empty.
    pub conv_text: &'a [u8],
    /// The exact converted value when `data.converted`.
    pub conv_exact: Option<Rational<'a>>,
}

/// The exact result of a read-time conversion: the reader-owned bignums
/// `conv.num / conv.den`, valid only for the duration of the callback.
#[derive(Clone, Copy)]
pub struct Rational<'a> {
    num: *mut c_void,
    den: *mut c_void,
    _reader: std::marker::PhantomData<&'a BvnrData>,
}

impl std::fmt::Debug for Rational<'_> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str("Rational")
    }
}

impl<'a> Rational<'a> {
    fn of(d: &'a BvnrData) -> Option<Self> {
        (d.converted && !d.conv.num.is_null() && !d.conv.den.is_null()).then_some(Rational {
            num: d.conv.num,
            den: d.conv.den,
            _reader: std::marker::PhantomData,
        })
    }

    /// The reduced numerator and denominator in decimal.
    pub fn parts(&self) -> Option<(String, String)> {
        Some((int_to_decimal(self.num)?, int_to_decimal(self.den)?))
    }

    /// `bvn_rational_to_str`: the value in `base`; `Err` for a base the
    /// library cannot write, `Ok(None)` when the expansion does not
    /// terminate.
    pub fn in_base(&self, base: u32) -> Result<Option<String>, ()> {
        // SAFETY: num/den are live reader-owned bignums for 'a.
        let size = unsafe { bvn_rational_str_bufsize(self.num, self.den, base) };
        if size == 0 {
            return Err(());
        }
        let mut buf = vec![0u8; size];
        let mut exact = false;
        // SAFETY: `buf` holds `size` bytes, as the library asked for.
        let n = unsafe {
            bvn_rational_to_str(
                self.num,
                self.den,
                base,
                buf.as_mut_ptr().cast(),
                size,
                &mut exact,
            )
        };
        if n < 0 || !exact {
            return Ok(None);
        }
        Ok(CStr::from_bytes_until_nul(&buf)
            .ok()
            .map(|t| t.to_string_lossy().into_owned()))
    }
}

/// `bvn_int_to_str` in base 10.
fn int_to_decimal(n: *mut c_void) -> Option<String> {
    // SAFETY: `n` is a live bignum; the getters only read it.
    let bits = unsafe { bvn_int_bitlen(n) }.max(1) as u32;
    // SAFETY: plain values in.
    let size = unsafe { bvn_int_str_bufsize(bits, 10) };
    let mut buf = vec![0u8; size];
    // SAFETY: `buf` holds `size` bytes, as the library asked for.
    if unsafe { bvn_int_to_str(n, buf.as_mut_ptr().cast(), size, 10) } < 0 {
        return None;
    }
    CStr::from_bytes_until_nul(&buf)
        .ok()
        .map(|t| t.to_string_lossy().into_owned())
}

impl<'a> EventView<'a> {
    /// Borrows everything `d` points at for as long as `d` itself.
    ///
    /// SAFETY: every pointer/length pair in `d` must describe bytes valid for
    /// `'a`, as the library guarantees for the duration of a callback.
    unsafe fn new(event: c_int, d: Option<&'a BvnrData>) -> Self {
        // SAFETY: guaranteed by the caller.
        let view = |p: *const c_void, n: u32| unsafe { bytes(p, n) };
        EventView {
            event,
            data: d,
            raw: d.map_or(&[][..], |d| view(d.data, d.length)),
            frac: d.map_or(&[][..], |d| view(d.frac_data, d.frac_length)),
            unit_source: d.map_or(&[][..], |d| view(d.unit_source, d.unit_source_length)),
            conv_text: d
                .filter(|d| d.converted)
                .map_or(&[][..], |d| view(d.conv.text.cast(), d.conv.length)),
            conv_exact: d.and_then(Rational::of),
        }
    }
}

fn c_buf_to_string(buf: &[u8]) -> Option<String> {
    CStr::from_bytes_until_nul(buf)
        .ok()
        .and_then(|c| c.to_str().ok())
        .map(str::to_owned)
}

/// A Rust event handler; returning false stops the read.
pub type Handler<'h> = &'h mut dyn FnMut(&EventView<'_>) -> bool;

/// A `want_unit` hook: the unit (and base) to deliver a value in, or `None`
/// to leave it as written.
pub type WantUnit<'h> = &'h mut dyn FnMut(&EventView<'_>) -> Option<(ValueUnit, u32)>;

/// What the read trampolines reach through `userdata`.
#[derive(Default)]
pub struct Handlers<'h> {
    pub verified: Option<Handler<'h>>,
    pub unverified: Option<Handler<'h>>,
    pub want_unit: Option<WantUnit<'h>>,
    /// Per-document callback of `bvnr_doc_stream_read`: (index, ok, error).
    pub on_document: Option<&'h mut dyn FnMut(u64, bool, c_int) -> bool>,
    /// The first error `on_error` reported: (code, line, column, byte, offset).
    pub first_error: Option<(c_int, u64, u64, u32, u64)>,
    /// Called for every error `on_error` reports, recovered or not, with the
    /// same (code, line, column, byte, offset).
    pub on_error: Option<&'h mut dyn FnMut(c_int, u64, u64, u32, u64)>,
    pub panic: Option<Box<dyn std::any::Any + Send>>,
}

impl<'h> Handlers<'h> {
    /// Points `flags` at the trampolines for the handlers that are set, with
    /// `self` as userdata. `self` must stay put while the library uses it.
    fn install(&mut self, flags: &mut BvnrReadFlags) {
        flags.on_verified = self
            .verified
            .is_some()
            .then_some(on_verified_trampoline as EventCallbackFn);
        flags.on_unverified = self
            .unverified
            .is_some()
            .then_some(on_unverified_trampoline as EventCallbackFn);
        flags.want_unit = self
            .want_unit
            .is_some()
            .then_some(want_unit_trampoline as WantUnitFn);
        flags.on_error = Some(on_error_trampoline as OnErrorFn);
        flags.userdata = (self as *mut Self).cast();
    }

    /// Runs `f` on the handlers, parking a panic instead of unwinding into C.
    fn guard<R>(&mut self, fallback: R, f: impl FnOnce(&mut Self) -> R) -> R {
        if self.panic.is_some() {
            return fallback;
        }
        match std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| f(self))) {
            Ok(r) => r,
            Err(payload) => {
                self.panic = Some(payload);
                fallback
            }
        }
    }

    /// Rethrows a panic a handler raised during the last call.
    fn resume(&mut self) {
        if let Some(payload) = self.panic.take() {
            std::panic::resume_unwind(payload);
        }
    }
}

/// SAFETY: `ud` must be the `Handlers` installed for the running call, alive
/// and otherwise untouched for its duration.
unsafe fn handlers<'a>(ud: *mut c_void) -> &'a mut Handlers<'a> {
    // SAFETY: guaranteed by the caller.
    unsafe { &mut *ud.cast::<Handlers>() }
}

extern "C" fn want_unit_trampoline(
    ud: *mut c_void,
    d: *const BvnrData,
    want: *mut ValueUnit,
    want_base: *mut u32,
) -> bool {
    // SAFETY: `ud` is the installed `Handlers`; `d` is NULL or valid for the
    // call, and its pointers describe bytes valid for the call.
    // The hook runs for a value, so its view is an `ev_data` event.
    let ev_data = crate::enums::Event::Data as c_int;
    let (h, view) = unsafe { (handlers(ud), EventView::new(ev_data, d.as_ref())) };
    let Some((unit, base)) = h.guard(None, |h| h.want_unit.as_mut().and_then(|f| f(&view))) else {
        return false;
    };
    // SAFETY: `want` is writable; `want_base` is NULL or writable.
    unsafe {
        *want = unit;
        if let Some(b) = want_base.as_mut() {
            *b = base;
        }
    }
    true
}

extern "C" fn on_error_trampoline(
    ud: *mut c_void,
    err: c_int,
    line: u64,
    column: u64,
    byte: u32,
    offset: u64,
) {
    // SAFETY: `ud` is the installed `Handlers`.
    let h = unsafe { handlers(ud) };
    h.first_error
        .get_or_insert((err, line, column, byte, offset));
    h.guard((), |h| {
        if let Some(f) = h.on_error.as_mut() {
            f(err, line, column, byte, offset);
        }
    });
}

extern "C" fn on_verified_trampoline(ud: *mut c_void, ev: c_int, d: *const BvnrData) -> bool {
    dispatch(ud, ev, d, true)
}

extern "C" fn on_unverified_trampoline(ud: *mut c_void, ev: c_int, d: *const BvnrData) -> bool {
    dispatch(ud, ev, d, false)
}

fn dispatch(ud: *mut c_void, ev: c_int, d: *const BvnrData, verified: bool) -> bool {
    // SAFETY: `ud` is the installed `Handlers`; the library passes NULL or a
    // bvnr_data_t whose pointers are valid for this call.
    let (h, view) = unsafe { (handlers(ud), EventView::new(ev, d.as_ref())) };
    h.guard(false, |h| {
        let handler = if verified {
            &mut h.verified
        } else {
            &mut h.unverified
        };
        handler.as_mut().is_none_or(|f| f(&view))
    })
}

/// A unit policy in Rust terms, marshalled by [`with_policy`].
#[derive(Default)]
pub struct PolicySpec {
    /// (path, unit, base, convert)
    pub rules: Vec<(String, String, u32, bool)>,
    /// (unit, base)
    pub targets: Vec<(String, u32)>,
    pub base: u32,
    pub normalise: bool,
    pub leave_inexact: bool,
    pub require_unit: bool,
    pub require_dimension_of: Vec<String>,
}

/// Builds the C `bvnr_unit_policy_t` for `spec` and runs `f` on it (NULL for
/// `None`). False when a string holds a NUL byte or a count overflows.
fn with_policy(spec: Option<&PolicySpec>, f: impl FnOnce(*mut BvnrUnitPolicy) -> bool) -> bool {
    let Some(spec) = spec else {
        return f(std::ptr::null_mut());
    };
    let c = |t: &str| CString::new(t).ok();
    let Some(rule_strs) = spec
        .rules
        .iter()
        .map(|(p, u, _, _)| Some((c(p)?, c(u)?)))
        .collect::<Option<Vec<_>>>()
    else {
        return false;
    };
    let Some(target_strs) = spec
        .targets
        .iter()
        .map(|(u, _)| c(u))
        .collect::<Option<Vec<_>>>()
    else {
        return false;
    };
    let Some(dim_strs) = spec
        .require_dimension_of
        .iter()
        .map(|u| c(u))
        .collect::<Option<Vec<_>>>()
    else {
        return false;
    };
    let rules: Vec<BvnrUnitRule> = spec
        .rules
        .iter()
        .zip(&rule_strs)
        .map(|((_, _, base, convert), (p, u))| BvnrUnitRule {
            path: p.as_ptr(),
            unit: u.as_ptr(),
            base: *base,
            mode: c_int::from(!convert),
        })
        .collect();
    let targets: Vec<BvnrUnitTarget> = spec
        .targets
        .iter()
        .zip(&target_strs)
        .map(|((_, base), u)| BvnrUnitTarget {
            unit: u.as_ptr(),
            base: *base,
        })
        .collect();
    let dims: Vec<*const c_char> = dim_strs.iter().map(|d| d.as_ptr()).collect();
    let (Ok(num_rules), Ok(num_targets), Ok(num_dims)) = (
        u32::try_from(rules.len()),
        u32::try_from(targets.len()),
        u32::try_from(dims.len()),
    ) else {
        return false;
    };
    let mut policy = BvnrUnitPolicy {
        rules: rules.as_ptr(),
        num_rules,
        targets: targets.as_ptr(),
        num_targets,
        base: spec.base,
        normalise: c_int::from(spec.normalise),
        on_inexact: c_int::from(spec.leave_inexact),
        require_unit: spec.require_unit,
        require_dimension_of: dims.as_ptr(),
        num_require_dimension_of: num_dims,
    };
    // Every pointer in `policy` refers into the vectors above, alive until
    // `f` returns.
    f(&mut policy)
}

/// The last error a reader recorded.
pub struct ReaderError {
    pub code: c_int,
    pub line: u64,
    pub column: u64,
    pub offset: u64,
    pub byte: u32,
}

/// An owned `bvnr_reader_t`.
pub struct ReaderHandle(std::ptr::NonNull<c_void>);

impl ReaderHandle {
    /// `bvnr_reader_create`; `None` when the library is out of memory.
    pub fn new() -> Option<Self> {
        // SAFETY: no preconditions; the result is NULL or a fresh reader.
        std::ptr::NonNull::new(unsafe { bvnr_reader_create() }).map(Self)
    }

    /// `bvnr_open_read_mem` + `bvnr_read` over `data` with the limits in
    /// `flags` (its userdata and callbacks are set here). Returns false when
    /// the library reports an error; a handler panic is resumed afterwards.
    pub fn read_mem(
        &mut self,
        data: &[u8],
        mut flags: BvnrReadFlags,
        h: &mut Handlers<'_>,
    ) -> bool {
        h.install(&mut flags);
        // SAFETY: the reader is live; `data`, `flags` and `h` outlive both
        // calls, and nothing else touches `h` meanwhile.
        let ok = unsafe {
            bvnr_open_read_mem(
                self.0.as_ptr(),
                data.as_ptr().cast_mut().cast(),
                data.len() as u64,
                std::ptr::null_mut(),
                0,
                &mut flags,
            ) && bvnr_read(self.0.as_ptr())
        };
        h.resume();
        ok
    }

    /// `bvnr_open_read_source` + `bvnr_read` over `src`, as [`read_mem`]
    /// reads memory.
    ///
    /// [`read_mem`]: Self::read_mem
    pub fn read_source(
        &mut self,
        src: &mut BvnrSource,
        mut flags: BvnrReadFlags,
        h: &mut Handlers<'_>,
    ) -> bool {
        h.install(&mut flags);
        // SAFETY: the reader is live; `src`, `flags` and `h` outlive both
        // calls (the reader copies the source), and nothing else touches `h`
        // meanwhile.
        let ok = unsafe {
            bvnr_open_read_source(self.0.as_ptr(), src, std::ptr::null_mut(), &mut flags)
                && bvnr_read(self.0.as_ptr())
        };
        h.resume();
        ok
    }

    /// `bvnr_reader_set_unit_policy`; `None` clears it.
    pub fn set_unit_policy(&mut self, policy: Option<&PolicySpec>) -> bool {
        let r = self.0.as_ptr();
        // SAFETY: the reader is live; the policy is only read during the call.
        with_policy(policy, |p| unsafe { bvnr_reader_set_unit_policy(r, p) })
    }

    /// `bvnr_reader_get_recovery_count` and `bvnr_reader_get_skipped_bytes`.
    pub fn recovery(&self) -> (u64, u64) {
        let r = self.0.as_ptr();
        // SAFETY: the reader is live; the getters only read it.
        unsafe {
            (
                bvnr_reader_get_recovery_count(r),
                bvnr_reader_get_skipped_bytes(r),
            )
        }
    }

    /// `bvnr_reader_get_declared_version`: the `#!bovnar M.N` directive of
    /// the last read, if it had one.
    pub fn declared_version(&self) -> Option<(u16, u16)> {
        let (mut major, mut minor) = (0u16, 0u16);
        // SAFETY: the reader is live; the getter writes two u16 out-params.
        unsafe { bvnr_reader_get_declared_version(self.0.as_ptr(), &mut major, &mut minor) }
            .then_some((major, minor))
    }

    /// The reader's last error and where it happened.
    pub fn error(&self) -> ReaderError {
        let r = self.0.as_ptr();
        // SAFETY: the reader is live; the getters only read it.
        unsafe {
            ReaderError {
                code: bvnr_reader_get_error(r),
                line: bvnr_reader_get_error_line(r),
                column: bvnr_reader_get_error_column(r),
                offset: bvnr_reader_get_error_offset(r),
                byte: bvnr_reader_get_error_byte(r),
            }
        }
    }
}

// SAFETY: bovnar.h (THREADING): a reader holds its whole parse state and the
// library writes no shared state after startup, so a reader may be used from
// any one thread at a time. Moving it is that; `&mut self` on every read keeps
// it to one thread at once. Not Sync: two threads must not share one reader.
unsafe impl Send for ReaderHandle {}

impl Drop for ReaderHandle {
    fn drop(&mut self) {
        // SAFETY: the reader is live and dropped exactly once.
        unsafe { bvnr_reader_destroy(self.0.as_ptr()) }
    }
}

/// An owned `bvn_dom_doc_t`.
pub struct DocHandle(std::ptr::NonNull<c_void>);

/// A node borrowed from a [`DocHandle`]; valid as long as the document.
#[derive(Clone, Copy)]
pub struct Node<'a>(
    std::ptr::NonNull<c_void>,
    std::marker::PhantomData<&'a DocHandle>,
);

/// Turns a borrowed `(key, node)` C entry array into Rust pairs.
///
/// SAFETY: `entries` must be NULL or point at `count` entries owned by a
/// document that outlives `'a`.
unsafe fn entries<'a>(entries: *const BvnDomEntry, count: u32) -> Vec<(&'a [u8], Node<'a>)> {
    // SAFETY: guaranteed by the caller.
    unsafe { entry_iter(entries, count) }.collect()
}

/// The pairs of [`entries`] one at a time, without collecting them.
///
/// # Safety
/// As for [`entries`].
unsafe fn entry_iter<'a>(
    entries: *const BvnDomEntry,
    count: u32,
) -> impl Iterator<Item = (&'a [u8], Node<'a>)> {
    let list: &'a [BvnDomEntry] = if entries.is_null() {
        &[]
    } else {
        // SAFETY: guaranteed by the caller.
        unsafe { std::slice::from_raw_parts(entries, count as usize) }
    };
    list.iter().filter_map(|e| {
        let node = std::ptr::NonNull::new(e.value)?;
        let key = if e.key.is_null() {
            &[][..]
        } else {
            // SAFETY: a non-NULL key is a NUL-terminated string the
            // document owns.
            unsafe { std::ffi::CStr::from_ptr(e.key) }.to_bytes()
        };
        Some((key, Node(node, std::marker::PhantomData)))
    })
}

impl DocHandle {
    /// `bvn_dom_parse`. `None` only on allocation failure (or input over
    /// 4 GiB, which the C length cannot express); a malformed document
    /// returns a handle whose `parse_error` is set.
    pub fn parse(data: &[u8]) -> Option<Self> {
        let len = u32::try_from(data.len()).ok()?;
        // SAFETY: `data` is valid for `len` bytes and only read during the call.
        let doc = unsafe { bvn_dom_parse(data.as_ptr().cast_mut().cast(), len) };
        std::ptr::NonNull::new(doc).map(Self)
    }

    /// `bvn_dom_parse_policy`: `None` also when the policy cannot be built.
    pub fn parse_policy(data: &[u8], policy: &PolicySpec) -> Option<Self> {
        let len = u32::try_from(data.len()).ok()?;
        let mut doc = std::ptr::null_mut();
        with_policy(Some(policy), |p| {
            // SAFETY: `data` is valid for `len` bytes and `p` for the call;
            // both are only read.
            doc = unsafe { bvn_dom_parse_policy(data.as_ptr().cast_mut().cast(), len, p) };
            true
        });
        std::ptr::NonNull::new(doc).map(Self)
    }

    /// `bvn_dom_doc_get_parse_error`.
    pub fn parse_error(&self) -> c_int {
        // SAFETY: the document is live.
        unsafe { bvn_dom_doc_get_parse_error(self.0.as_ptr()) }
    }

    /// `bvn_dom_lookup`: a dotted path such as `"a.b"`.
    pub fn lookup(&self, path: &str) -> Option<Node<'_>> {
        let path = std::ffi::CString::new(path).ok()?;
        // SAFETY: the document is live; `path` is NUL-terminated.
        let node = unsafe { bvn_dom_lookup(self.0.as_ptr(), path.as_ptr()) };
        std::ptr::NonNull::new(node).map(|n| Node(n, std::marker::PhantomData))
    }

    /// The top-level `(key, node)` pairs, in document order.
    pub fn entries(&self) -> Vec<(&[u8], Node<'_>)> {
        let doc = self.0.as_ptr();
        // SAFETY: the document is live and owns the entry array.
        unsafe { entries(bvn_dom_doc_entries(doc), bvn_dom_doc_count(doc)) }
    }

    /// The top-level node under `key`, found in the entry array in place
    /// (the C DOM keeps no index; `bvn_dom_lookup` scans the same way).
    pub fn get(&self, key: &str) -> Option<Node<'_>> {
        let doc = self.0.as_ptr();
        // SAFETY: the document is live and owns the entry array.
        unsafe { entry_iter(bvn_dom_doc_entries(doc), bvn_dom_doc_count(doc)) }
            .find(|(k, _)| *k == key.as_bytes())
            .map(|(_, n)| n)
    }

    /// `bvn_dom_doc_count`: the number of top-level entries.
    pub fn count(&self) -> usize {
        // SAFETY: the document is live.
        unsafe { bvn_dom_doc_count(self.0.as_ptr()) as usize }
    }
}

// SAFETY: a document is a tree the C DOM allocated and owns alone; no other
// object points into it and the library writes no shared state after startup
// (bovnar.h, THREADING), so it may be moved to and dropped on another thread.
// Not Sync: concurrent reads are not documented as safe.
unsafe impl Send for DocHandle {}

impl Drop for DocHandle {
    fn drop(&mut self) {
        // SAFETY: the document is live and dropped exactly once; no Node can
        // outlive it (they borrow it).
        unsafe { bvn_dom_doc_destroy(self.0.as_ptr()) }
    }
}

impl<'a> Node<'a> {
    fn ptr(self) -> *mut c_void {
        self.0.as_ptr()
    }

    /// `bvn_dom_node_type`.
    pub fn node_type(self) -> c_int {
        // SAFETY: the node is live (borrowed from its document).
        unsafe { bvn_dom_node_type(self.ptr()) }
    }

    /// `bvn_dom_get_value_type`.
    pub fn value_type(self) -> ValueTypeSpec {
        // SAFETY: as above.
        unsafe { bvn_dom_get_value_type(self.ptr()) }
    }

    /// `bvn_dom_get_unit_string`: the canonical unit, empty when unitless.
    pub fn unit_string(self) -> String {
        let mut buf = vec![0u8; UNIT_STRING_MAX];
        // SAFETY: the node is live; `buf` has UNIT_STRING_MAX writable bytes.
        let n = unsafe { bvn_dom_get_unit_string(self.ptr(), buf.as_mut_ptr().cast(), buf.len()) };
        buf.truncate(usize::try_from(n).unwrap_or(0).min(UNIT_STRING_MAX));
        String::from_utf8_lossy(&buf).into_owned()
    }

    /// `bvn_dom_get_i64`.
    pub fn get_i64(self) -> Option<i64> {
        let mut v = 0;
        // SAFETY: the node is live; `v` is a valid out-param.
        unsafe { bvn_dom_get_i64(self.ptr(), &mut v) }.then_some(v)
    }

    /// `bvn_dom_get_u64`.
    pub fn get_u64(self) -> Option<u64> {
        let mut v = 0;
        // SAFETY: as above.
        unsafe { bvn_dom_get_u64(self.ptr(), &mut v) }.then_some(v)
    }

    /// `bvn_dom_get_i32`.
    pub fn get_i32(self) -> Option<i32> {
        let mut v = 0;
        // SAFETY: as above.
        unsafe { bvn_dom_get_i32(self.ptr(), &mut v) }.then_some(v)
    }

    /// `bvn_dom_get_u32`.
    pub fn get_u32(self) -> Option<u32> {
        let mut v = 0;
        // SAFETY: as above.
        unsafe { bvn_dom_get_u32(self.ptr(), &mut v) }.then_some(v)
    }

    /// `bvn_dom_get_i16`.
    pub fn get_i16(self) -> Option<i16> {
        let mut v = 0;
        // SAFETY: as above.
        unsafe { bvn_dom_get_i16(self.ptr(), &mut v) }.then_some(v)
    }

    /// `bvn_dom_get_u16`.
    pub fn get_u16(self) -> Option<u16> {
        let mut v = 0;
        // SAFETY: as above.
        unsafe { bvn_dom_get_u16(self.ptr(), &mut v) }.then_some(v)
    }

    /// `bvn_dom_get_i8`.
    pub fn get_i8(self) -> Option<i8> {
        let mut v = 0;
        // SAFETY: as above.
        unsafe { bvn_dom_get_i8(self.ptr(), &mut v) }.then_some(v)
    }

    /// `bvn_dom_get_u8`.
    pub fn get_u8(self) -> Option<u8> {
        let mut v = 0;
        // SAFETY: as above.
        unsafe { bvn_dom_get_u8(self.ptr(), &mut v) }.then_some(v)
    }

    /// `bvn_dom_get_float`.
    pub fn get_float(self) -> Option<f64> {
        let mut v = 0.0;
        // SAFETY: as above.
        unsafe { bvn_dom_get_float(self.ptr(), &mut v) }.then_some(v)
    }

    /// `bvn_dom_get_bool`.
    pub fn get_bool(self) -> Option<bool> {
        let mut v = false;
        // SAFETY: as above.
        unsafe { bvn_dom_get_bool(self.ptr(), &mut v) }.then_some(v)
    }

    /// The bytes of a string, symbol, reference or octet-stream node,
    /// chosen by `getter`.
    fn get_bytes(
        self,
        getter: unsafe extern "C" fn(*mut c_void, *mut *const c_void, *mut u32) -> bool,
    ) -> Option<&'a [u8]> {
        let mut ptr = std::ptr::null();
        let mut len = 0;
        // SAFETY: the node is live; on success ptr/len describe bytes the
        // node owns, so they live as long as the document.
        unsafe { getter(self.ptr(), &mut ptr, &mut len).then(|| bytes(ptr, len)) }
    }

    /// `bvn_dom_get_string`.
    pub fn get_string(self) -> Option<&'a [u8]> {
        self.get_bytes(bvn_dom_get_string)
    }

    /// `bvn_dom_get_symbol`.
    pub fn get_symbol(self) -> Option<&'a [u8]> {
        self.get_bytes(bvn_dom_get_symbol)
    }

    /// `bvn_dom_get_unit`.
    pub fn unit(self) -> ValueUnit {
        // SAFETY: the node is live.
        unsafe { bvn_dom_get_unit(self.ptr()) }
    }

    /// `bvn_dom_get_value_in_base_units`.
    pub fn value_in_base_units(self) -> f64 {
        // SAFETY: the node is live.
        unsafe { bvn_dom_get_value_in_base_units(self.ptr()) }
    }

    /// `bvn_dom_get_unit_source` and `bvn_dom_get_datetime_fraction`: the
    /// unit's spelling and a datetime's sub-second digits, when present.
    pub fn unit_source_and_fraction(self) -> (Option<&'a [u8]>, Option<&'a [u8]>) {
        let (mut ul, mut fl) = (0u32, 0u32);
        // SAFETY: the node is live; the results are NULL or `len` bytes owned
        // by the document, which outlives 'a.
        unsafe {
            let u = bvn_dom_get_unit_source(self.ptr(), &mut ul);
            let f = bvn_dom_get_datetime_fraction(self.ptr(), &mut fl);
            (
                (!u.is_null()).then(|| bytes(u.cast(), ul)),
                (!f.is_null()).then(|| bytes(f.cast(), fl)),
            )
        }
    }

    /// `bvn_dom_get_reference`.
    pub fn get_reference(self) -> Option<&'a [u8]> {
        self.get_bytes(bvn_dom_get_reference)
    }

    /// `bvn_dom_get_octets`.
    pub fn get_octets(self) -> Option<&'a [u8]> {
        self.get_bytes(bvn_dom_get_octets)
    }

    /// `bvn_dom_int_to_str`: any integer node, however wide, in `base`.
    pub fn int_to_str(self, base: u32) -> Option<String> {
        // SAFETY: the node is live.
        let s = unsafe { bvn_dom_int_to_str(self.ptr(), base) };
        if s.is_null() {
            return None;
        }
        // SAFETY: a non-NULL result is a fresh NUL-terminated string that we
        // own and release exactly once, after copying it.
        unsafe {
            let text = std::ffi::CStr::from_ptr(s.cast())
                .to_string_lossy()
                .into_owned();
            bvn_dom_free_string(s);
            Some(text)
        }
    }

    /// `bvn_dom_struct_count`.
    pub fn struct_count(self) -> u32 {
        // SAFETY: the node is live.
        unsafe { bvn_dom_struct_count(self.ptr()) }
    }

    /// `bvn_dom_array_count`.
    pub fn array_count(self) -> u32 {
        // SAFETY: the node is live.
        unsafe { bvn_dom_array_count(self.ptr()) }
    }

    /// `bvn_dom_array_dims`.
    pub fn array_dims(self) -> u32 {
        // SAFETY: the node is live.
        unsafe { bvn_dom_array_dims(self.ptr()) }
    }

    /// `bvn_dom_struct_get`.
    pub fn struct_get(self, key: &str) -> Option<Node<'a>> {
        let key = std::ffi::CString::new(key).ok()?;
        // SAFETY: the node is live; `key` is NUL-terminated.
        let n = unsafe { bvn_dom_struct_get(self.ptr(), key.as_ptr()) };
        std::ptr::NonNull::new(n).map(|n| Node(n, std::marker::PhantomData))
    }

    /// `bvn_dom_array_at`.
    pub fn array_at(self, index: u32) -> Option<Node<'a>> {
        // SAFETY: the node is live; the library bounds-checks `index`.
        let n = unsafe { bvn_dom_array_at(self.ptr(), index) };
        std::ptr::NonNull::new(n).map(|n| Node(n, std::marker::PhantomData))
    }

    /// The `(key, node)` pairs of a struct node, in document order.
    pub fn struct_entries(self) -> Vec<(&'a [u8], Node<'a>)> {
        // SAFETY: the node is live and owns its entry array.
        unsafe { entries(bvn_dom_struct_entries(self.ptr()), self.struct_count()) }
    }
}

/// An owned `bvnr_writer_t` writing into a buffer it owns, or into a
/// callback sink.
pub struct WriterHandle {
    ptr: std::ptr::NonNull<c_void>,
    /// The memory sink. Allocated once in `open_mem` and never resized, so the
    /// address the library holds stays valid until the writer is destroyed.
    buf: Vec<u8>,
    /// The callback sink's state, from `Box::into_raw` (NULL for none). The
    /// library holds the same pointer as the sink's user pointer, so it is
    /// freed only after the writer is destroyed.
    sink: *mut PushState,
}

impl WriterHandle {
    /// `bvnr_writer_create`; `None` when the library is out of memory.
    pub fn new() -> Option<Self> {
        // SAFETY: no preconditions; the result is NULL or a fresh writer.
        let ptr = std::ptr::NonNull::new(unsafe { bvnr_writer_create() })?;
        Some(WriterHandle {
            ptr,
            buf: Vec::new(),
            sink: std::ptr::null_mut(),
        })
    }

    /// `bvnr_sink_to_callback` + `bvnr_open_write_sink`: the writer pushes
    /// its output through `state` (in 64 KiB blocks, and the rest when it
    /// finishes, then flushes).
    pub fn open_sink(&mut self, state: PushState, pretty: bool, mut flags: BvnrWriteFlags) -> bool {
        let state = Box::into_raw(Box::new(state));
        let mut sink = blank_sink();
        // SAFETY: the writer is live and copies the sink; its user pointer,
        // `state`, is owned by this handle until after the writer is
        // destroyed (an earlier one is released below, once the writer no
        // longer holds it); `flags` is only read during the call.
        let ok = unsafe {
            bvnr_sink_to_callback(
                &mut sink,
                push_trampoline,
                Some(flush_trampoline),
                state.cast(),
            );
            bvnr_open_write_sink(self.ptr.as_ptr(), &mut sink, pretty, &mut flags)
        };
        let old = std::mem::replace(&mut self.sink, state);
        if !old.is_null() {
            // SAFETY: from Box::into_raw, released once; the writer now holds
            // the new sink.
            drop(unsafe { Box::from_raw(old) });
        }
        ok
    }

    /// The callback sink's state, when the writer was opened with one.
    pub fn sink(&self) -> Option<&PushState> {
        // SAFETY: NULL or the live state this handle owns; the library uses
        // it only inside a call, which needs `&mut self`.
        unsafe { self.sink.as_ref() }
    }

    /// Rethrows a panic the sink's writer raised during the last call.
    pub fn resume_sink_panic(&mut self) {
        // SAFETY: as for `sink`.
        if let Some(payload) = unsafe { self.sink.as_mut() }.and_then(|s| s.panic.take()) {
            std::panic::resume_unwind(payload);
        }
    }

    /// `bvnr_open_write_mem` into a fresh buffer of `cap` bytes.
    pub fn open_mem(&mut self, cap: usize, pretty: bool, mut flags: BvnrWriteFlags) -> bool {
        self.buf = vec![0; cap];
        // SAFETY: the writer is live and `buf` (cap bytes) lives, unmoved and
        // unresized, as long as the writer; `flags` is only read during the call.
        unsafe {
            bvnr_open_write_mem(
                self.ptr.as_ptr(),
                self.buf.as_mut_ptr().cast(),
                cap as u64,
                pretty,
                &mut flags,
            )
        }
    }

    /// `bvnr_write_event` with a payload of `data` (key, literal or octet
    /// chunk). False when the library refuses it or `data` exceeds 4 GiB.
    pub fn write_event(
        &mut self,
        event: c_int,
        token_type: c_int,
        value_type: ValueTypeSpec,
        data: &[u8],
    ) -> bool {
        let Ok(length) = u32::try_from(data.len()) else {
            return false;
        };
        let mut d = BvnrData::zeroed();
        d.type_ = token_type;
        d.value_type = value_type;
        d.data = data.as_ptr().cast();
        d.length = length;
        // SAFETY: the writer is live; `d` and the bytes it points at are
        // valid for the call, and the library copies what it keeps.
        unsafe { bvnr_write_event(self.ptr.as_ptr(), event, &mut d) }
    }

    /// `bvnr_write_event` with every payload field: the literal `data`, the
    /// value's unit, a datetime's sub-second digits `frac` and the unit's
    /// spelling `unit_source` (empty for none).
    pub fn write_event_full(
        &mut self,
        event: c_int,
        token_type: c_int,
        value_type: ValueTypeSpec,
        unit: &ValueUnit,
        [data, frac, unit_source]: [&[u8]; 3],
    ) -> bool {
        let (Ok(length), Ok(frac_length), Ok(unit_source_length)) = (
            u32::try_from(data.len()),
            u32::try_from(frac.len()),
            u32::try_from(unit_source.len()),
        ) else {
            return false;
        };
        let mut d = BvnrData::zeroed();
        d.type_ = token_type;
        d.value_type = value_type;
        d.value_unit = *unit;
        d.data = data.as_ptr().cast();
        d.length = length;
        if !frac.is_empty() {
            d.frac_data = frac.as_ptr().cast();
            d.frac_length = frac_length;
        }
        if !unit_source.is_empty() {
            d.unit_source = unit_source.as_ptr().cast();
            d.unit_source_length = unit_source_length;
        }
        // SAFETY: the writer is live; `d` and the bytes it points at are
        // valid for the call, and the library copies what it keeps.
        unsafe { bvnr_write_event(self.ptr.as_ptr(), event, &mut d) }
    }

    /// `bvnr_writer_set_unit_policy`; `None` clears it.
    pub fn set_unit_policy(&mut self, policy: Option<&PolicySpec>) -> bool {
        let w = self.ptr.as_ptr();
        // SAFETY: the writer is live; the policy is only read during the call.
        with_policy(policy, |p| unsafe { bvnr_writer_set_unit_policy(w, p) })
    }

    /// `bvnr_mux_begin`, then `bvnr_mux_send` for each message, then
    /// `bvnr_mux_end`: a multiplexed stream under `key`.
    pub fn mux(&mut self, key: &str, messages: &[(u64, &[u8])]) -> bool {
        self.mux_begin(key)
            && messages.iter().all(|(ch, data)| self.mux_send(*ch, data))
            && self.mux_end()
    }

    /// `bvnr_mux_begin`: opens a multiplexed stream under `key`.
    pub fn mux_begin(&mut self, key: &str) -> bool {
        let Ok(key) = CString::new(key) else {
            return false;
        };
        // SAFETY: the writer is live; `key` is only read during the call.
        unsafe { bvnr_mux_begin(self.ptr.as_ptr(), key.as_ptr()) }
    }

    /// `bvnr_mux_send`: one message on `channel`.
    pub fn mux_send(&mut self, channel: u64, data: &[u8]) -> bool {
        // SAFETY: the writer is live; `data` is only read during the call.
        unsafe {
            bvnr_mux_send(
                self.ptr.as_ptr(),
                channel,
                data.as_ptr().cast_mut().cast(),
                data.len() as u64,
            )
        }
    }

    /// `bvnr_mux_end`: closes the multiplexed stream.
    pub fn mux_end(&mut self) -> bool {
        // SAFETY: the writer is live.
        unsafe { bvnr_mux_end(self.ptr.as_ptr()) }
    }

    /// `bvnr_embed_document`: `doc` as a canonical embedded document under `key`.
    pub fn embed_document(&mut self, key: &str, doc: &[u8]) -> bool {
        let Ok(key) = CString::new(key) else {
            return false;
        };
        // SAFETY: the writer is live; `key` and `doc` are only read during
        // the call.
        unsafe {
            bvnr_embed_document(
                self.ptr.as_ptr(),
                key.as_ptr(),
                doc.as_ptr().cast_mut().cast(),
                doc.len() as u64,
            )
        }
    }

    /// `bvnr_writer_unit_flags`.
    pub fn unit_flags(&self) -> u32 {
        // SAFETY: the writer is live.
        unsafe { bvnr_writer_unit_flags(self.ptr.as_ptr()) }
    }

    /// `bvnr_write_version`.
    pub fn write_version(&mut self, major: u16, minor: u16) -> bool {
        // SAFETY: the writer is live.
        unsafe { bvnr_write_version(self.ptr.as_ptr(), major, minor) }
    }

    /// `bvnr_write_finish`.
    pub fn finish(&mut self) -> bool {
        // SAFETY: the writer is live.
        unsafe { bvnr_write_finish(self.ptr.as_ptr()) }
    }

    /// `bvnr_write_flush`: pushes what is buffered, then calls the sink's
    /// flush (for a callback sink, `flush_trampoline`).
    pub fn flush(&mut self) -> bool {
        // SAFETY: the writer is live.
        unsafe { bvnr_write_flush(self.ptr.as_ptr()) }
    }

    /// `bvnr_writer_get_error` and `bvnr_writer_get_error_offset`.
    pub fn error(&self) -> (c_int, u64) {
        let w = self.ptr.as_ptr();
        // SAFETY: the writer is live; the getters only read it.
        unsafe { (bvnr_writer_get_error(w), bvnr_writer_get_error_offset(w)) }
    }

    /// `bvnr_writer_bytes_written`.
    pub fn bytes_written(&self) -> u64 {
        // SAFETY: the writer is live.
        unsafe { bvnr_writer_bytes_written(self.ptr.as_ptr()) }
    }

    /// What the writer has put into its buffer so far.
    pub fn output(&self) -> &[u8] {
        let n = usize::try_from(self.bytes_written()).unwrap_or(usize::MAX);
        &self.buf[..n.min(self.buf.len())]
    }
}

// SAFETY: bovnar.h (THREADING): a writer holds its whole serialise state and
// the library writes no shared state after startup. The memory sink `buf` is
// owned by this handle and moves with it (its heap address does not change);
// so is the callback sink's state, which is Send (an owned destination is
// `Write + Send`, a scoped one is looked up on the calling thread).
// Not Sync: two threads must not share one writer.
unsafe impl Send for WriterHandle {}

impl Drop for WriterHandle {
    fn drop(&mut self) {
        // SAFETY: the writer is live and destroyed exactly once, before `buf`
        // and the sink state; destroying it pushes nothing.
        unsafe { bvnr_writer_destroy(self.ptr.as_ptr()) }
        if !self.sink.is_null() {
            // SAFETY: from Box::into_raw, released once, after the writer.
            drop(unsafe { Box::from_raw(self.sink) });
        }
    }
}

/// Runs one of the `bvn_format_*` functions on a 128-byte buffer.
fn format(f: impl FnOnce(*mut c_char, usize) -> i32) -> Option<String> {
    let mut buf = [0u8; 128];
    let n = usize::try_from(f(buf.as_mut_ptr().cast(), buf.len())).ok()?;
    String::from_utf8(buf.get(..n)?.to_vec()).ok()
}

/// `bvn_format_uint64`: `value` as digits in `base`.
pub fn format_u64(value: u64, base: u32) -> Option<String> {
    // SAFETY: `buf` has `len` writable bytes for the call.
    format(|buf, len| unsafe { bvn_format_uint64(buf, len, value, base, 0) })
}

/// `bvn_format_int64`: `value` as digits in `base`.
pub fn format_i64(value: i64, base: u32) -> Option<String> {
    // SAFETY: as above.
    format(|buf, len| unsafe { bvn_format_int64(buf, len, value, base, 0) })
}

/// `bvn_format_double`: `value` as the literal for type `vt`.
pub fn format_f64(value: f64, vt: ValueTypeSpec) -> Option<String> {
    // SAFETY: as above.
    format(|buf, len| unsafe { bvn_format_double(buf, len, value, vt) })
}

// ---------------------------------------------------------------------------
// Units: value-in, value-out helpers over `value_unit_t`.

/// Runs a `bvn_unit_to_*` formatter on a `UNIT_STRING_MAX` buffer: the text,
/// or the negative return value the library refused with.
fn unit_text(f: impl FnOnce(*mut c_char, usize) -> i32) -> std::result::Result<String, i32> {
    let mut buf = vec![0u8; UNIT_STRING_MAX];
    let n = f(buf.as_mut_ptr().cast(), buf.len());
    let len = usize::try_from(n).map_err(|_| n)?;
    buf.truncate(len.min(UNIT_STRING_MAX));
    String::from_utf8(buf).map_err(|_| -1)
}

/// `bvn_parse_unit_n`: `None` for text that is not a unit.
pub fn parse_unit(text: &str) -> Option<ValueUnit> {
    let len = u32::try_from(text.len()).ok()?;
    let mut ok = false;
    // SAFETY: `text` has `len` readable bytes for the call; `ok` is writable.
    let u = unsafe { bvn_parse_unit_n(text.as_ptr(), len, &mut ok) };
    ok.then_some(u)
}

/// `bvn_unit_error_code`: why `text` is not a unit (0 when it is one).
pub fn unit_error_code(text: &str) -> c_int {
    let len = u32::try_from(text.len()).unwrap_or(u32::MAX);
    // SAFETY: `text` has at least `len` readable bytes for the call.
    unsafe { bvn_unit_error_code(text.as_ptr(), len) }
}

/// `bvn_unit_to_string_ex` with `flags` (0 is `bvn_unit_to_string`).
pub fn unit_to_string_ex(u: &ValueUnit, flags: u32) -> std::result::Result<String, i32> {
    // SAFETY: the buffer has `len` writable bytes for the call.
    unit_text(|buf, len| unsafe { bvn_unit_to_string_ex(*u, buf, len, flags) })
}

/// `bvn_unit_to_profile`: `u` as a code in vocabulary `ns`.
pub fn unit_to_profile(ns: &str, u: &ValueUnit) -> Option<String> {
    let ns = CString::new(ns).ok()?;
    // SAFETY: `ns` is NUL-terminated; the buffer has `len` writable bytes.
    unit_text(|buf, len| unsafe { bvn_unit_to_profile(ns.as_ptr(), *u, buf, len) }).ok()
}

/// `bvn_unit_to_ucum`.
pub fn unit_to_ucum(u: &ValueUnit) -> Option<String> {
    // SAFETY: the buffer has `len` writable bytes for the call.
    unit_text(|buf, len| unsafe { bvn_unit_to_ucum(*u, buf, len) }).ok()
}

/// `bvn_unit_profile_name` for every index below `bvn_unit_profile_count`.
pub fn unit_profiles() -> Vec<String> {
    // SAFETY: no preconditions.
    let count = unsafe { bvn_unit_profile_count() };
    (0..count)
        .filter_map(|i| {
            // SAFETY: `i` is in range; the result is NULL or a static string.
            let p = unsafe { bvn_unit_profile_name(i) };
            // SAFETY: a non-NULL result is a static NUL-terminated string.
            (!p.is_null()).then(|| unsafe { CStr::from_ptr(p) }.to_string_lossy().into_owned())
        })
        .collect()
}

/// The `value_unit_t` predicates and scalars that take units by value and
/// touch nothing else; each is safe for any bit pattern of its arguments.
pub mod unit_fns {
    use super::*;

    macro_rules! by_value {
        ($($name:ident => $c:ident($($a:ident: $t:ty),*) -> $r:ty;)*) => {$(
            #[doc = concat!("`", stringify!($c), "`.")]
            pub fn $name($($a: $t),*) -> $r {
                // SAFETY: plain values in, a plain value out; the library
                // validates the unit structs it is handed.
                unsafe { super::$c($($a),*) }
            }
        )*};
    }

    by_value! {
        valid => bvn_unit_valid(u: ValueUnit) -> bool;
        is_profile_only => bvn_unit_is_profile_only(u: ValueUnit) -> bool;
        prefix_factor => bvn_unit_prefix_factor(u: ValueUnit) -> f64;
        prefix_exponent => bvn_unit_prefix_exponent(u: ValueUnit) -> i32;
        compatible => bvn_units_compatible(a: ValueUnit, b: ValueUnit) -> bool;
        equal => bvn_unit_equal(a: ValueUnit, b: ValueUnit) -> bool;
        convertible => bvn_units_convertible(a: ValueUnit, b: ValueUnit) -> bool;
        exponent_to_int => bvn_exponent_to_int(e: c_int) -> i32;
        int_to_exponent => bvn_int_to_exponent(n: i32) -> c_int;
        prefix_unit_valid => bvn_prefix_unit_valid(p: ValueUnitPrefix, base: c_int) -> bool;
    }

    /// `bvn_unit_si_normal_form`.
    pub fn si_normal_form(u: ValueUnit) -> Option<ValueUnit> {
        let mut out = ValueUnit::zeroed();
        // SAFETY: `out` is writable for the call.
        unsafe { bvn_unit_si_normal_form(u, &mut out) }.then_some(out)
    }

    /// `bvn_unit_convert_factor`: (factor, ok, requires_affine).
    pub fn convert_factor(a: ValueUnit, b: ValueUnit) -> (f64, bool, bool) {
        let (mut ok, mut affine) = (false, false);
        // SAFETY: both flags are writable for the call.
        let f = unsafe { bvn_unit_convert_factor(a, b, &mut ok, &mut affine) };
        (f, ok, affine)
    }

    /// `bvn_unit_convert_rational` of the decimal fraction `num/den`: the
    /// exact result as decimal (numerator, denominator) and whether the
    /// factor was exact. `None` for incompatible or invalid units, a value
    /// or result past the library's bignum capacity, or no memory.
    pub fn convert_rational(
        num: &str,
        den: &str,
        from: ValueUnit,
        to: ValueUnit,
    ) -> Option<(String, String, bool)> {
        /// Four bignums freed on every path.
        struct Ints([*mut c_void; 4]);
        impl Drop for Ints {
            fn drop(&mut self) {
                for n in self.0 {
                    // SAFETY: NULL or a bignum allocated below, freed once.
                    unsafe { bvn_int_free(n) };
                }
            }
        }
        // SAFETY: no arguments; each result is freed by `Ints`.
        let ints = Ints(std::array::from_fn(|_| unsafe { bvn_int_alloc() }));
        if ints.0.iter().any(|n| n.is_null()) {
            return None;
        }
        let [vnum, vden, onum, oden] = ints.0;
        let (num, den) = (CString::new(num).ok()?, CString::new(den).ok()?);
        let mut exact = false;
        // SAFETY: live bignums and NUL-terminated strings; the outputs are
        // writable for the call.
        let ok = unsafe {
            bvn_int_from_str(vnum, num.as_ptr(), 10)
                && bvn_int_from_str(vden, den.as_ptr(), 10)
                && bvn_unit_convert_rational(vnum, vden, from, to, onum, oden, &mut exact)
        };
        if !ok {
            return None;
        }
        Some((
            super::int_to_decimal(onum)?,
            super::int_to_decimal(oden)?,
            exact,
        ))
    }

    /// `bvn_unit_convert_value`.
    pub fn convert_value(value: f64, from: ValueUnit, to: ValueUnit) -> Option<f64> {
        let mut out = 0.0;
        // SAFETY: `out` is writable for the call.
        unsafe { bvn_unit_convert_value(value, from, to, &mut out) }.then_some(out)
    }

    /// `bvn_unit_to_si_factor`: (factor, is_affine, affine_offset), or
    /// `None` when the unit has no SI conversion.
    pub fn to_si_factor(u: ValueUnit) -> Option<(f64, bool, f64)> {
        let (mut affine, mut offset, mut ok) = (false, 0.0, false);
        // SAFETY: the three outputs are writable for the call.
        let f = unsafe { bvn_unit_to_si_factor(u, &mut affine, &mut offset, &mut ok) };
        ok.then_some((f, affine, offset))
    }

    /// `bvn_unit_dimension_vector`: the exponents of the seven SI base
    /// dimensions.
    pub fn dimension_vector(u: ValueUnit) -> Option<[i32; 7]> {
        let mut dims = [0i32; 7];
        // SAFETY: `dims` has the seven slots the function fills.
        unsafe { bvn_unit_dimension_vector(u, dims.as_mut_ptr()) }.then_some(dims)
    }

    /// `bvn_unit_reduce`: (unit, scale), or `None` when the scale overflows.
    pub fn reduce(u: ValueUnit) -> Option<(ValueUnit, f64)> {
        let (mut scale, mut overflow) = (1.0, false);
        // SAFETY: both outputs are writable for the call.
        let r = unsafe { bvn_unit_reduce(u, &mut scale, &mut overflow) };
        (!overflow).then_some((r, scale))
    }
}

// ---------------------------------------------------------------------------
// Currencies.

/// `bvn_unit_is_currency`, `bvn_unit_is_fiat`, `bvn_unit_is_crypto`.
pub fn currency_kind(base: c_int) -> (bool, bool, bool) {
    // SAFETY: plain value in, plain value out, for any id.
    unsafe {
        (
            bvn_unit_is_currency(base),
            bvn_unit_is_fiat(base),
            bvn_unit_is_crypto(base),
        )
    }
}

/// `bvn_currency_info`: (code, numeric code, minor unit, is crypto, name).
pub fn currency_info(base: c_int) -> Option<(String, u16, u8, bool, String)> {
    // SAFETY: the result is NULL or a row of the static catalogue.
    let row = unsafe { bvn_currency_info(base).as_ref() }?;
    let text = |chars: &[c_char]| {
        let bytes: Vec<u8> = chars
            .iter()
            .take_while(|&&c| c != 0)
            // c_char is i8 on x86 and u8 on aarch64 Linux: take the byte as is.
            .map(|&c| c.to_ne_bytes()[0])
            .collect();
        String::from_utf8_lossy(&bytes).into_owned()
    };
    Some((
        text(&row.code),
        row.numeric_code,
        row.minor_unit,
        row.is_crypto,
        text(&row.name),
    ))
}

/// `bvn_parse_currency_str`: the base-unit id of a code, 0 when unknown.
pub fn parse_currency(code: &str) -> c_int {
    let len = u32::try_from(code.len()).unwrap_or(u32::MAX);
    // SAFETY: `code` has at least `len` readable bytes for the call.
    unsafe { bvn_parse_currency_str(code.as_ptr(), len) }
}

// ---------------------------------------------------------------------------
// Versions.

/// `bvnr_version_string`: the library version, e.g. `"2.0.2"`.
pub fn version_string() -> String {
    // SAFETY: returns a static NUL-terminated string.
    unsafe { CStr::from_ptr(bvnr_version_string()) }
        .to_string_lossy()
        .into_owned()
}

/// `bvnr_version`: the library version as `major * 10000 + minor * 100 +
/// patch`.
pub fn version_number() -> u32 {
    // SAFETY: no arguments; returns a constant.
    unsafe { bvnr_version() }
}

/// `bvnr_datetime_epoch_index`: the epoch index of `name`, or `None` for a
/// name the library does not know (or one holding a NUL byte).
pub fn datetime_epoch_index(name: &str) -> Option<u32> {
    let name = CString::new(name).ok()?;
    // SAFETY: a NUL-terminated string, only read during the call.
    let idx = unsafe { bvnr_datetime_epoch_index(name.as_ptr()) };
    u32::try_from(idx).ok()
}

/// `bvnr_spec_version`: the newest format version the library implements.
pub fn spec_version() -> (u16, u16) {
    let (mut major, mut minor) = (0u16, 0u16);
    // SAFETY: writes two u16 out-params.
    unsafe { bvnr_spec_version(&mut major, &mut minor) };
    (major, minor)
}

/// `bvnr_peek_version`: the `#!bovnar M.N` directive leading `data`, if any.
pub fn peek_version(data: &[u8]) -> Option<(u16, u16)> {
    let (mut major, mut minor) = (0u16, 0u16);
    // SAFETY: the C side only reads `len` bytes of the buffer.
    unsafe {
        bvnr_peek_version(
            data.as_ptr() as *mut c_void,
            data.len() as u64,
            &mut major,
            &mut minor,
        )
    }
    .then_some((major, minor))
}

// Numbers and datetimes.

/// `bvnr_datetime_epoch_name` and `bvnr_datetime_epoch_mjd` of a datetime type.
pub fn datetime_epoch(vt: ValueTypeSpec) -> (Option<String>, i32) {
    // SAFETY: plain value in; the name is NULL or a static string.
    let (name, mjd) = unsafe { (bvnr_datetime_epoch_name(vt), bvnr_datetime_epoch_mjd(vt)) };
    // SAFETY: a non-NULL name is a static NUL-terminated string.
    let name = (!name.is_null()).then(|| {
        unsafe { CStr::from_ptr(name) }
            .to_string_lossy()
            .into_owned()
    });
    (name, mjd)
}

/// `bvn_float_str_fits_fix`: whether decimal `text` fits a signed fixed
/// point of `total_bits` with `frac_bits` fractional bits.
pub fn float_str_fits_fix(text: &str, total_bits: u32, frac_bits: u32) -> bool {
    let Ok(t) = CString::new(text) else {
        return false;
    };
    // SAFETY: `t` is NUL-terminated and only read.
    unsafe { bvn_float_str_fits_fix(t.as_ptr(), 10, total_bits, frac_bits) }
}

/// `bvn_float_strtodec`: decimal `text` as the IEEE decimal interchange
/// encoding of `width` bits (little-endian bytes), rounded once straight from
/// the string. `None` for a width with no decimal format or a refused literal.
pub fn float_strtodec(text: &str, width: u32) -> Option<Vec<u8>> {
    let words: c_int = match width {
        16 | 32 => 1,
        64 => 2,
        128 => 4,
        256 => 8,
        _ => return None,
    };
    let t = CString::new(text).ok()?;
    let mut buf = [0u32; 8];
    // SAFETY: `t` is NUL-terminated and only read; `buf` holds at least
    // `words` words.
    let ok = unsafe { bvn_float_strtodec(t.as_ptr(), width, buf.as_mut_ptr(), words) };
    ok.then(|| {
        buf.iter()
            .flat_map(|w| w.to_le_bytes())
            .take(width as usize / 8)
            .collect()
    })
}

/// A float literal in `base` (16 for a hex float) as the nearest `f64`, via
/// the library's arbitrary-precision float at 4096 significand bits.
pub fn parse_float_base(text: &str, base: u32) -> Option<f64> {
    let t = CString::new(text).ok()?;
    // SAFETY: the float is allocated, used and freed here; `t` is
    // NUL-terminated and `out` writable.
    unsafe {
        let f = bvn_float_alloc(4096);
        if f.is_null() {
            return None;
        }
        let mut out = 0.0;
        let ok = bvn_float_from_str(f, t.as_ptr(), base) && bvn_float_to_double(f, &mut out);
        bvn_float_free(f);
        ok.then_some(out)
    }
}

/// An owned `bvn_float_t` from `bvn_float_alloc`, freed when dropped: the
/// handle behind [`crate::BvnFloat`]. No struct layout is mirrored; the C
/// library owns the representation.
pub struct FloatHandle {
    ptr: std::ptr::NonNull<c_void>,
    prec: u32,
}

// SAFETY: the float is plain heap memory owned by this handle alone; the
// library keeps no thread-local state for it. Not Sync: `&self` methods pass
// the pointer to C, which must not race a `&mut self` call.
unsafe impl Send for FloatHandle {}

impl Drop for FloatHandle {
    fn drop(&mut self) {
        // SAFETY: the float is live and freed exactly once.
        unsafe { bvn_float_free(self.ptr.as_ptr()) }
    }
}

/// `bytes` (little-endian) as `N` little-endian 32-bit words, zero-padded.
fn le_words<const N: usize>(bytes: &[u8]) -> [u32; N] {
    let mut words = [0u32; N];
    for (i, b) in bytes.iter().take(N * 4).enumerate() {
        words[i / 4] |= u32::from(*b) << (8 * (i % 4));
    }
    words
}

/// `bytes` (little-endian) as an integer of up to 8 bytes, zero-padded.
fn le_u64(bytes: &[u8]) -> u64 {
    let mut buf = [0u8; 8];
    let n = bytes.len().min(8);
    buf[..n].copy_from_slice(&bytes[..n]);
    u64::from_le_bytes(buf)
}

fn words_le_bytes(words: &[u32]) -> Vec<u8> {
    words.iter().flat_map(|w| w.to_le_bytes()).collect()
}

impl FloatHandle {
    /// `bvn_float_alloc(prec)`: a float of `prec` significand bits, or
    /// `None` when the allocation failed.
    pub fn alloc(prec: u32) -> Option<Self> {
        // SAFETY: plain allocation; NULL is handled.
        let ptr = unsafe { bvn_float_alloc(prec) };
        std::ptr::NonNull::new(ptr).map(|ptr| FloatHandle { ptr, prec })
    }

    fn ptr(&self) -> *mut c_void {
        self.ptr.as_ptr()
    }

    /// The precision the float was allocated with.
    pub fn prec(&self) -> u32 {
        self.prec
    }

    /// `bvn_float_from_str`: whether `text` parsed in `base`. Text with a NUL
    /// byte is refused.
    pub fn set_str(&mut self, text: &str, base: u32) -> bool {
        let Ok(t) = CString::new(text) else {
            return false;
        };
        // SAFETY: the float is live and exclusively borrowed; `t` is
        // NUL-terminated and only read.
        unsafe { bvn_float_from_str(self.ptr(), t.as_ptr(), base) }
    }

    pub fn is_nan(&self) -> bool {
        // SAFETY: the float is live; the call only reads it.
        unsafe { bvn_float_is_nan(self.ptr()) }
    }

    pub fn is_inf(&self) -> bool {
        // SAFETY: as in `is_nan`.
        unsafe { bvn_float_is_inf(self.ptr()) }
    }

    pub fn is_zero(&self) -> bool {
        // SAFETY: as in `is_nan`.
        unsafe { bvn_float_is_zero(self.ptr()) }
    }

    pub fn is_neg(&self) -> bool {
        // SAFETY: as in `is_nan`.
        unsafe { bvn_float_is_neg(self.ptr()) }
    }

    /// `bvn_float_to_str` in `base`, into a buffer of
    /// `bvn_float_str_bufsize(prec, base)` bytes; `None` if it did not fit.
    pub fn to_str(&self, base: u32) -> Option<String> {
        // SAFETY: pure size computation.
        let n = unsafe { bvn_float_str_bufsize(self.prec, base) };
        let mut buf = vec![0u8; n.max(1)];
        // SAFETY: the float is live; `buf` holds `buf.len()` writable bytes.
        let wrote =
            unsafe { bvn_float_to_str(self.ptr(), buf.as_mut_ptr().cast(), buf.len(), base) };
        let wrote = usize::try_from(wrote).ok()?;
        buf.truncate(wrote);
        String::from_utf8(buf).ok()
    }

    /// `bvn_float_to_double`.
    pub fn to_double(&self) -> f64 {
        let mut out = 0.0;
        // SAFETY: the float is live; `out` is writable.
        unsafe { bvn_float_to_double(self.ptr(), &mut out) };
        out
    }

    /// `bvn_float_to_bin{width}`: the IEEE binary interchange bytes,
    /// little-endian; `None` for a width with no binary format.
    pub fn to_bin(&self, width: u32) -> Option<Vec<u8>> {
        let f = self.ptr();
        // SAFETY: the float is live; each output holds what its call writes
        // (one integer, or 4 or 8 words).
        unsafe {
            Some(match width {
                16 => {
                    let mut o = 0u16;
                    bvn_float_to_bin16(f, &mut o);
                    o.to_le_bytes().to_vec()
                }
                32 => {
                    let mut o = 0u32;
                    bvn_float_to_bin32(f, &mut o);
                    o.to_le_bytes().to_vec()
                }
                64 => {
                    let mut o = 0u64;
                    bvn_float_to_bin64(f, &mut o);
                    o.to_le_bytes().to_vec()
                }
                128 => {
                    let mut o = [0u32; 4];
                    bvn_float_to_bin128(f, o.as_mut_ptr());
                    words_le_bytes(&o)
                }
                256 => {
                    let mut o = [0u32; 8];
                    bvn_float_to_bin256(f, o.as_mut_ptr());
                    words_le_bytes(&o)
                }
                _ => return None,
            })
        }
    }

    /// `bvn_float_from_bin{width}` of the little-endian `bytes` (zero-padded
    /// to the width): whether the library accepted them; `None` for a width
    /// with no binary format.
    pub fn set_bin(&mut self, width: u32, bytes: &[u8]) -> Option<bool> {
        let f = self.ptr();
        // SAFETY: the float is live and exclusively borrowed; the word arrays
        // hold the 4 or 8 words the calls read.
        unsafe {
            Some(match width {
                16 => bvn_float_from_bin16(f, le_u64(bytes) as u16),
                32 => bvn_float_from_bin32(f, le_u64(bytes) as u32),
                64 => bvn_float_from_bin64(f, le_u64(bytes)),
                128 => bvn_float_from_bin128(f, le_words::<4>(bytes).as_mut_ptr()),
                256 => bvn_float_from_bin256(f, le_words::<8>(bytes).as_mut_ptr()),
                _ => return None,
            })
        }
    }

    /// `bvn_float_to_dec{width}`: the IEEE decimal interchange bytes,
    /// little-endian; `None` for a width with no decimal format.
    pub fn to_dec(&self, width: u32) -> Option<Vec<u8>> {
        let f = self.ptr();
        // SAFETY: as in `to_bin`.
        unsafe {
            Some(match width {
                16 => {
                    let mut o = 0u16;
                    bvn_float_to_dec16(f, &mut o);
                    o.to_le_bytes().to_vec()
                }
                32 => {
                    let mut o = 0u32;
                    bvn_float_to_dec32(f, &mut o);
                    o.to_le_bytes().to_vec()
                }
                64 => {
                    let mut o = 0u64;
                    bvn_float_to_dec64(f, &mut o);
                    o.to_le_bytes().to_vec()
                }
                128 => {
                    let mut o = [0u32; 4];
                    bvn_float_to_dec128(f, o.as_mut_ptr());
                    words_le_bytes(&o)
                }
                256 => {
                    let mut o = [0u32; 8];
                    bvn_float_to_dec256(f, o.as_mut_ptr());
                    words_le_bytes(&o)
                }
                _ => return None,
            })
        }
    }

    /// `bvn_float_from_dec{width}` of the little-endian `bytes`, as
    /// [`FloatHandle::set_bin`].
    pub fn set_dec(&mut self, width: u32, bytes: &[u8]) -> Option<bool> {
        let f = self.ptr();
        // SAFETY: as in `set_bin`.
        unsafe {
            Some(match width {
                16 => bvn_float_from_dec16(f, le_u64(bytes) as u16),
                32 => bvn_float_from_dec32(f, le_u64(bytes) as u32),
                64 => bvn_float_from_dec64(f, le_u64(bytes)),
                128 => bvn_float_from_dec128(f, le_words::<4>(bytes).as_mut_ptr()),
                256 => bvn_float_from_dec256(f, le_words::<8>(bytes).as_mut_ptr()),
                _ => return None,
            })
        }
    }

    /// `bvn_float_to_fix{total_bits}`: the signed Q-format datum as
    /// little-endian two's-complement bytes; `None` for a width with no
    /// fixed-point format.
    pub fn to_fix(&self, total_bits: u32, frac_bits: u32) -> Option<Vec<u8>> {
        let f = self.ptr();
        // SAFETY: as in `to_bin`.
        unsafe {
            Some(match total_bits {
                16 => bvn_float_to_fix16(f, frac_bits).to_le_bytes().to_vec(),
                32 => bvn_float_to_fix32(f, frac_bits).to_le_bytes().to_vec(),
                64 => bvn_float_to_fix64(f, frac_bits).to_le_bytes().to_vec(),
                128 => {
                    let mut o = [0u32; 4];
                    bvn_float_to_fix128(f, frac_bits, o.as_mut_ptr());
                    words_le_bytes(&o)
                }
                256 => {
                    let mut o = [0u32; 8];
                    bvn_float_to_fix256(f, frac_bits, o.as_mut_ptr());
                    words_le_bytes(&o)
                }
                _ => return None,
            })
        }
    }
}

// ---------------------------------------------------------------------------
// Streams: framing, multiplexing, embedding.

/// `bvnr_sink_to_mem` + `bvnr_frame_write` for each document: the framed
/// concatenation, 12 header bytes per document.
pub fn frame_documents(docs: &[&[u8]]) -> Option<Vec<u8>> {
    let total: usize = docs.iter().map(|d| 12 + d.len()).sum();
    let mut out = vec![0u8; total.max(1)];
    let mut sink = BvnrSink {
        opaque: [0; OPAQUE_BYTES / 8],
    };
    // SAFETY: `out` has `total` writable bytes and outlives `sink`; every
    // document is only read during its call.
    let ok = unsafe {
        bvnr_sink_to_mem(&mut sink, out.as_mut_ptr().cast(), total as u64);
        docs.iter()
            .all(|d| bvnr_frame_write(&mut sink, d.as_ptr().cast_mut().cast(), d.len() as u64))
    };
    out.truncate(total);
    ok.then_some(out)
}

/// `bvnr_frame_write` of `doc` into a callback sink over `out`, which is
/// not flushed. The I/O error of a failed push, or `Other` when the library
/// refused the frame; a panic in `out` is resumed.
pub fn frame_write_to(doc: &[u8], out: &mut dyn std::io::Write) -> std::io::Result<()> {
    let mut state = PushState::scoped();
    let mut sink = blank_sink();
    let ok = with_scoped_out(out, || {
        // SAFETY: `state` outlives the sink, which is used only within this
        // closure; `doc` is only read during the call.
        unsafe {
            bvnr_sink_to_callback(&mut sink, push_trampoline, None, (&raw mut state).cast());
            bvnr_frame_write(&mut sink, doc.as_ptr().cast_mut().cast(), doc.len() as u64)
        }
    });
    if let Some(payload) = state.panic.take() {
        std::panic::resume_unwind(payload);
    }
    match state.error.take() {
        Some(e) => Err(e),
        None if ok => Ok(()),
        None => Err(std::io::Error::other("bvnr_frame_write failed")),
    }
}

/// `bvnr_doc_stream_read_ex` over `src`: `h.verified` sees each document's
/// events and `h.on_document` each document's end. Returns (ok, count, the
/// error that ended the stream).
pub fn doc_stream_read(
    src: &mut BvnrSource,
    mut flags: BvnrReadFlags,
    continue_past_failed: bool,
    max_document_size: u64,
    h: &mut Handlers<'_>,
) -> (bool, u64, c_int) {
    h.install(&mut flags);
    let mut opts = BvnrDocStreamOpts {
        flags: &mut flags,
        userdata: flags.userdata,
        on_document: Some(on_document_trampoline as OnDocumentFn),
        continue_past_failed,
        max_document_size,
    };
    let (mut count, mut error) = (0, 0);
    // SAFETY: `src`, `flags`, `opts` and `h` outlive the call, and nothing
    // else touches `h` meanwhile.
    let ok = unsafe { bvnr_doc_stream_read_ex(src, &mut opts, &mut count, &mut error) };
    h.resume();
    (ok, count, error)
}

extern "C" fn on_document_trampoline(ud: *mut c_void, index: u64, ok: bool, err: c_int) -> bool {
    // SAFETY: `ud` is the installed `Handlers`.
    let h = unsafe { handlers(ud) };
    h.guard(false, |h| {
        h.on_document.as_mut().is_none_or(|f| f(index, ok, err))
    })
}

/// `bvnr_parse_embedded`: `h.verified` sees the inner document's events and
/// `h.first_error` records why it failed.
pub fn parse_embedded(inner: &[u8], mut flags: BvnrReadFlags, h: &mut Handlers<'_>) -> bool {
    h.install(&mut flags);
    // SAFETY: `inner`, `flags` and `h` outlive the call; `inner` is only read.
    let ok = unsafe {
        bvnr_parse_embedded(
            inner.as_ptr().cast_mut().cast(),
            inner.len() as u64,
            &mut flags,
        )
    };
    h.resume();
    ok
}

/// Why [`demux_read`] failed.
pub enum DemuxError {
    /// The demultiplexer lost sync (`bvnr_demux_error`).
    Demux(c_int),
    /// The reader refused the stream.
    Read(ReaderError),
    /// The named call could not allocate.
    OutOfMemory(&'static str),
}

extern "C" fn on_message_trampoline(
    ud: *mut c_void,
    ch: u64,
    data: *const c_void,
    len: u64,
) -> bool {
    // SAFETY: `ud` is the message vector `demux_read` keeps alive, and
    // `data` holds `len` bytes for the call.
    let (out, payload) = unsafe {
        let len = usize::try_from(len).unwrap_or(0);
        let payload = if data.is_null() || len == 0 {
            Vec::new()
        } else {
            std::slice::from_raw_parts(data.cast::<u8>(), len).to_vec()
        };
        (&mut *ud.cast::<Vec<(u64, Vec<u8>)>>(), payload)
    };
    out.push((ch, payload));
    true
}

/// A reader feeding `bvnr_demux_on_event`: the messages of a multiplexed
/// stream, in order. `read` reads the document with the reader, the flags and
/// the handlers it is given ([`ReaderHandle::read_mem`] or
/// [`ReaderHandle::read_source`]).
pub fn demux_read(
    read: impl FnOnce(&mut ReaderHandle, BvnrReadFlags, &mut Handlers<'_>) -> bool,
    mut flags: BvnrReadFlags,
    max_message: u64,
    key: Option<&CStr>,
) -> Result<Vec<(u64, Vec<u8>)>, DemuxError> {
    let mut messages: Vec<(u64, Vec<u8>)> = Vec::new();
    let mut reader = ReaderHandle::new().ok_or(DemuxError::OutOfMemory("bvnr_reader_create()"))?;
    // SAFETY: `messages` outlives the demux, which is destroyed below.
    let dm = unsafe {
        bvnr_demux_create(
            Some(on_message_trampoline),
            (&raw mut messages).cast(),
            max_message,
        )
    };
    if dm.is_null() {
        return Err(DemuxError::OutOfMemory("bvnr_demux_create()"));
    }
    // SAFETY: `dm` is live; `key` is NUL-terminated.
    if key.is_some_and(|k| !unsafe { bvnr_demux_set_key(dm, k.as_ptr()) }) {
        // SAFETY: `dm` is live and destroyed once.
        unsafe { bvnr_demux_destroy(dm) };
        // bovnar_stream.h: false only for a NULL demux or a failed allocation.
        return Err(DemuxError::OutOfMemory("bvnr_demux_set_key()"));
    }
    let mut forward = |v: &EventView<'_>| {
        let d = v
            .data
            .map_or(std::ptr::null_mut(), |d| (d as *const BvnrData).cast_mut());
        // SAFETY: `dm` is live; `d` is the event's data, valid for the call
        // and only read by the demux.
        unsafe { bvnr_demux_on_event(dm, v.event, d) }
    };
    let mut h = Handlers {
        verified: Some(&mut forward),
        ..Handlers::default()
    };
    flags.max_file_size = 0;
    let ok = read(&mut reader, flags, &mut h);
    // SAFETY: `dm` is live; it is destroyed exactly once, after its last use.
    let derr = unsafe {
        let e = bvnr_demux_error(dm);
        bvnr_demux_destroy(dm);
        e
    };
    if derr != 0 {
        Err(DemuxError::Demux(derr))
    } else if !ok {
        Err(DemuxError::Read(reader.error()))
    } else {
        Ok(messages)
    }
}

// ---------------------------------------------------------------------------
// Callback sources and sinks: a `std::io::Read` or `Write` behind the
// library's pull and push, so that a read or a write streams instead of
// going through memory.

/// A blank source for a `bvnr_source_from_*` constructor to fill in.
fn blank_source() -> BvnrSource {
    BvnrSource {
        opaque: [0; OPAQUE_BYTES / 8],
    }
}

/// A blank sink for a `bvnr_sink_to_*` constructor to fill in.
fn blank_sink() -> BvnrSink {
    BvnrSink {
        opaque: [0; OPAQUE_BYTES / 8],
    }
}

/// Runs `f` on a memory source over `data`.
pub fn with_mem_source<R>(data: &[u8], f: impl FnOnce(&mut BvnrSource) -> R) -> R {
    let mut src = blank_source();
    // SAFETY: the source only reads `data`, which outlives every use of it:
    // they end when `f` returns.
    unsafe { bvnr_source_from_mem(&mut src, data.as_ptr().cast_mut().cast(), data.len() as u64) };
    f(&mut src)
}

/// What [`pull_trampoline`] reaches through the source's user pointer.
struct PullState<'a> {
    input: &'a mut dyn std::io::Read,
    /// How much of the library's buffer the next read is offered (see
    /// [`PULL_MIN`]).
    offer: usize,
    error: Option<std::io::Error>,
    panic: Option<Box<dyn std::any::Any + Send>>,
}

/// The smallest and largest slice a pull offers its `Read`. The bytes of the
/// library's buffer have to be zeroed before a `Read` may see them, so a
/// reader that returns a few bytes at a time (a socket, a pipe) is offered
/// little, and one that fills what it is offered (a file) is offered twice as
/// much next time, up to what the library asked for.
const PULL_MIN: usize = 4096;
const PULL_MAX: usize = 1 << 20;

extern "C" fn pull_trampoline(
    s: *mut BvnrSource,
    buf: *mut c_void,
    want: u32,
    got: *mut u32,
) -> bool {
    // SAFETY: `s` is the reader's copy of a source `with_source` built, whose
    // user pointer is the `PullState` it keeps alive and otherwise untouched
    // until the read is over; `got` is writable.
    let st = unsafe {
        *got = 0;
        &mut *bvnr_source_get_userdata(s).cast::<PullState<'_>>()
    };
    if st.error.is_some() || st.panic.is_some() {
        return false;
    }
    let want = (want as usize).min(st.offer);
    let out: &mut [u8] = if buf.is_null() || want == 0 {
        &mut []
    } else {
        // SAFETY: the library hands over at least `want` writable bytes at
        // `buf` for the call. They may be uninitialised, and a `Read` may look
        // at the buffer it is given, so they are zeroed before the slice is
        // made.
        unsafe {
            buf.cast::<u8>().write_bytes(0, want);
            std::slice::from_raw_parts_mut(buf.cast::<u8>(), want)
        }
    };
    let input = &mut st.input;
    let read = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        loop {
            match input.read(out) {
                Err(e) if e.kind() == std::io::ErrorKind::Interrupted => {}
                r => break r,
            }
        }
    }));
    match read {
        // `n <= want`, so it fits the u32 the library asked with.
        Ok(Ok(n)) if n <= want => {
            st.offer = if n == want {
                (st.offer * 2).min(PULL_MAX)
            } else if n < want / 4 {
                (st.offer / 2).max(PULL_MIN)
            } else {
                st.offer
            };
            // SAFETY: `got` is writable.
            unsafe { *got = n as u32 };
            true
        }
        Ok(Ok(n)) => {
            st.error = Some(std::io::Error::new(
                std::io::ErrorKind::InvalidData,
                format!("Read::read reported {n} bytes into a buffer of {want}"),
            ));
            false
        }
        Ok(Err(e)) => {
            st.error = Some(e);
            false
        }
        Err(payload) => {
            st.panic = Some(payload);
            false
        }
    }
}

/// Runs `f` on a source that pulls from `input`. A pull that failed makes
/// the result the I/O error, whatever `f` returned (the library saw it as an
/// unreadable source); a panic in `input` is resumed once `f` has returned.
/// A read that is `Interrupted` is retried.
pub fn with_source<R>(
    input: &mut dyn std::io::Read,
    f: impl FnOnce(&mut BvnrSource) -> R,
) -> Result<R, std::io::Error> {
    let mut state = PullState {
        input,
        offer: PULL_MIN,
        error: None,
        panic: None,
    };
    let mut src = blank_source();
    // SAFETY: `state` outlives every use of `src` and of the copies the
    // library makes of it, which end when `f` returns.
    unsafe { bvnr_source_from_callback(&mut src, pull_trampoline, (&raw mut state).cast()) };
    let result = f(&mut src);
    if let Some(payload) = state.panic.take() {
        std::panic::resume_unwind(payload);
    }
    match state.error.take() {
        Some(e) => Err(e),
        None => Ok(result),
    }
}

/// Where a callback sink's bytes go.
enum Out {
    /// A writer the sink owns ([`crate::Writer::to_writer`]).
    Owned(Box<dyn std::io::Write + Send>),
    /// The writer of the innermost [`with_scoped_out`] running on this
    /// thread, for a write that begins and ends inside one call.
    Scoped,
}

thread_local! {
    /// The writer [`with_scoped_out`] lends to the sinks of [`Out::Scoped`].
    static SCOPED_OUT: std::cell::Cell<Option<*mut (dyn std::io::Write + 'static)>> =
        const { std::cell::Cell::new(None) };
}

/// Runs `f` with `out` as the destination of every [`Out::Scoped`] sink on
/// this thread, and takes it back when `f` returns or unwinds. A sink used
/// outside such a call fails as a broken transport, so a writer that outlived
/// its call can never reach a writer that is gone.
pub fn with_scoped_out<R>(out: &mut dyn std::io::Write, f: impl FnOnce() -> R) -> R {
    struct Restore(Option<*mut (dyn std::io::Write + 'static)>);
    impl Drop for Restore {
        fn drop(&mut self) {
            SCOPED_OUT.with(|c| c.set(self.0));
        }
    }
    let raw: *mut (dyn std::io::Write + '_) = out;
    // SAFETY: only the lifetime changes. The pointer is reachable through
    // SCOPED_OUT only until `Restore` puts the previous value back, which
    // happens before `out`'s borrow ends, on return and on unwinding alike;
    // nothing else uses `out` meanwhile.
    let raw = unsafe {
        std::mem::transmute::<*mut (dyn std::io::Write + '_), *mut (dyn std::io::Write + 'static)>(
            raw,
        )
    };
    let _restore = Restore(SCOPED_OUT.with(|c| c.replace(Some(raw))));
    f()
}

/// A callback sink's state: its destination, a cap on what it takes, and
/// what went wrong, for the error the writer reports.
pub struct PushState {
    out: Out,
    cap: u64,
    pushed: u64,
    error: Option<std::io::Error>,
    exhausted: bool,
    panic: Option<Box<dyn std::any::Any + Send>>,
}

impl PushState {
    /// A sink into `out`, which it owns; `cap` bytes at most (`u64::MAX` for
    /// no cap).
    pub fn owned(out: Box<dyn std::io::Write + Send>, cap: u64) -> Self {
        Self::new(Out::Owned(out), cap)
    }

    /// A sink into the writer of the [`with_scoped_out`] running at each push.
    pub fn scoped() -> Self {
        Self::new(Out::Scoped, u64::MAX)
    }

    fn new(out: Out, cap: u64) -> Self {
        PushState {
            out,
            cap,
            pushed: 0,
            error: None,
            exhausted: false,
            panic: None,
        }
    }

    /// The I/O error a push or the flush failed with.
    pub fn io_error(&self) -> Option<&std::io::Error> {
        self.error.as_ref()
    }

    /// Whether a push was refused because it would have passed the cap.
    pub fn exhausted(&self) -> bool {
        self.exhausted
    }

    /// Runs `f` on the destination, for a push of `len` bytes (0 for the
    /// flush). The first failure is kept and every later call refused.
    fn call(
        &mut self,
        len: u64,
        f: impl FnOnce(&mut dyn std::io::Write) -> std::io::Result<()>,
    ) -> bool {
        if self.error.is_some() || self.exhausted || self.panic.is_some() {
            return false;
        }
        if len > self.cap - self.pushed {
            self.exhausted = true;
            return false;
        }
        let unwind = |f: &mut dyn FnMut() -> std::io::Result<()>| {
            std::panic::catch_unwind(std::panic::AssertUnwindSafe(f))
        };
        let mut f = Some(f);
        let result = match &mut self.out {
            Out::Owned(w) => unwind(&mut || f.take().map_or(Ok(()), |f| f(w.as_mut()))),
            Out::Scoped => match SCOPED_OUT.with(std::cell::Cell::get) {
                Some(w) => unwind(&mut || {
                    // SAFETY: a pointer in SCOPED_OUT is the writer the
                    // running `with_scoped_out` borrows, for as long as it is
                    // there; nothing else uses it during the push.
                    let w = unsafe { &mut *w };
                    f.take().map_or(Ok(()), |f| f(w))
                }),
                None => Ok(Err(std::io::Error::new(
                    std::io::ErrorKind::BrokenPipe,
                    "the writer's destination was only lent for a call that has returned",
                ))),
            },
        };
        match result {
            Ok(Ok(())) => {
                self.pushed += len;
                true
            }
            Ok(Err(e)) => {
                self.error = Some(e);
                false
            }
            Err(payload) => {
                self.panic = Some(payload);
                false
            }
        }
    }
}

/// SAFETY: `s` must be a writer's copy of a sink `WriterHandle::open_sink`
/// built, whose user pointer is the live `PushState` the handle owns.
unsafe fn push_state<'a>(s: *mut BvnrSink) -> &'a mut PushState {
    // SAFETY: guaranteed by the caller.
    unsafe { &mut *bvnr_sink_get_userdata(s).cast::<PushState>() }
}

extern "C" fn push_trampoline(s: *mut BvnrSink, buf: *const c_void, len: u32) -> bool {
    // SAFETY: `s` comes from `open_sink`; `buf` holds `len` bytes for the
    // call.
    let (st, data) = unsafe { (push_state(s), bytes(buf, len)) };
    // A push must take every byte or fail: write_all retries short writes and
    // `Interrupted`, and a writer that takes nothing is `WriteZero`.
    st.call(u64::from(len), |w| w.write_all(data))
}

extern "C" fn flush_trampoline(s: *mut BvnrSink) -> bool {
    // SAFETY: `s` comes from `open_sink`.
    let st = unsafe { push_state(s) };
    st.call(0, |w| w.flush())
}

// ---------------------------------------------------------------------------
// Push-fed reading: a reader, a frame reader and a demultiplexer that are
// handed bytes.

/// Borrows `len` bytes at `ptr`; NULL or zero length is empty.
///
/// SAFETY: as for [`bytes`].
unsafe fn bytes64<'a>(ptr: *const u8, len: u64) -> &'a [u8] {
    if ptr.is_null() || len == 0 {
        &[]
    } else {
        // SAFETY: guaranteed by the caller; the bytes exist, so their count
        // fits a usize.
        unsafe { std::slice::from_raw_parts(ptr, len as usize) }
    }
}

/// The handlers of the call in progress on a fed object. The library copies
/// the user pointer when the object is opened, so the pointer it is given is
/// this slot's, which stays put, and each call points the slot at its own
/// [`Handlers`] for as long as it runs. NULL between calls.
struct Slot(*mut c_void);

/// A new slot, owned through the raw pointer the library is also given.
fn new_slot() -> *mut Slot {
    Box::into_raw(Box::new(Slot(std::ptr::null_mut())))
}

/// SAFETY: `slot` must come from [`new_slot`] and be freed exactly once,
/// after the last call that could reach it.
unsafe fn free_slot(slot: *mut Slot) {
    // SAFETY: guaranteed by the caller.
    drop(unsafe { Box::from_raw(slot) });
}

/// Runs `f` with `slot` pointing at `h`; a handler panic is resumed after.
///
/// SAFETY: `slot` must be live.
unsafe fn with_slot<R>(slot: *mut Slot, h: &mut Handlers<'_>, f: impl FnOnce() -> R) -> R {
    // SAFETY: the slot is live; `h` outlives `f`, after which the slot is
    // emptied again, so no callback can reach `h` once it is gone.
    unsafe { (*slot).0 = (h as *mut Handlers<'_>).cast() };
    let result = f();
    // SAFETY: as above.
    unsafe { (*slot).0 = std::ptr::null_mut() };
    h.resume();
    result
}

/// The handlers a slot points at, or NULL outside a call.
///
/// SAFETY: `ud` must be a live [`Slot`].
unsafe fn slot_handlers(ud: *mut c_void) -> *mut c_void {
    // SAFETY: guaranteed by the caller.
    unsafe { (*ud.cast::<Slot>()).0 }
}

extern "C" fn slot_on_verified(ud: *mut c_void, ev: c_int, d: *const BvnrData) -> bool {
    // SAFETY: the user pointer of every fed object is its live slot.
    let h = unsafe { slot_handlers(ud) };
    // Events arrive only inside a feed or a finish, which fill the slot.
    !h.is_null() && dispatch(h, ev, d, true)
}

extern "C" fn slot_on_document(ud: *mut c_void, index: u64, ok: bool, err: c_int) -> bool {
    // SAFETY: as for slot_on_verified.
    let h = unsafe { slot_handlers(ud) };
    !h.is_null() && on_document_trampoline(h, index, ok, err)
}

/// A reader opened with `bvnr_open_read_feed` for one document after
/// another; events go to the handlers of the feed or finish that produces
/// them.
pub struct FeedHandle {
    reader: ReaderHandle,
    slot: *mut Slot,
}

impl FeedHandle {
    /// A reader and its slot; `None` when the library is out of memory.
    pub fn new() -> Option<Self> {
        Some(FeedHandle {
            reader: ReaderHandle::new()?,
            slot: new_slot(),
        })
    }

    /// The reader, for its error and its counters.
    pub fn reader(&self) -> &ReaderHandle {
        &self.reader
    }

    /// The reader, for its unit policy.
    pub fn reader_mut(&mut self) -> &mut ReaderHandle {
        &mut self.reader
    }

    /// `bvnr_open_read_feed` with the limits in `flags` (its userdata and
    /// callbacks are set here): starts a document.
    pub fn open(&mut self, mut flags: BvnrReadFlags) -> bool {
        flags.userdata = self.slot.cast();
        flags.on_verified = Some(slot_on_verified as EventCallbackFn);
        // SAFETY: the reader is live; `flags` is copied during the call, and
        // its user pointer, the slot, lives as long as the reader.
        unsafe { bvnr_open_read_feed(self.reader.0.as_ptr(), std::ptr::null(), &mut flags) }
    }

    /// `bvnr_read_feed`: `h.verified` sees the events `data` completes.
    pub fn feed(&mut self, data: &[u8], h: &mut Handlers<'_>) -> bool {
        let r = self.reader.0.as_ptr();
        // SAFETY: the slot and the reader are live; `data` is only read, and
        // only during the call.
        unsafe {
            with_slot(self.slot, h, || {
                bvnr_read_feed(r, data.as_ptr().cast(), data.len() as u64)
            })
        }
    }

    /// `bvnr_read_finish`: `h.verified` sees the closing events.
    pub fn finish(&mut self, h: &mut Handlers<'_>) -> bool {
        let r = self.reader.0.as_ptr();
        // SAFETY: the slot and the reader are live.
        unsafe { with_slot(self.slot, h, || bvnr_read_finish(r)) }
    }
}

// SAFETY: the reader is Send (see ReaderHandle), and the slot is owned by the
// handle alone and empty between calls. Not Sync, like the reader.
unsafe impl Send for FeedHandle {}

impl Drop for FeedHandle {
    fn drop(&mut self) {
        // The reader is a field and is destroyed after this, but it holds the
        // slot only as an opaque pointer it calls back through inside a feed
        // or finish, of which none is running.
        // SAFETY: the slot came from new_slot and is freed once.
        unsafe { free_slot(self.slot) }
    }
}

/// A `bvnr_frame_reader_create` frame reader: it parses each document of a
/// fed frame stream, with the events and the per-document results going to
/// the handlers of the feed that completes them.
pub struct FrameParseHandle {
    fr: std::ptr::NonNull<c_void>,
    slot: *mut Slot,
}

impl FrameParseHandle {
    /// A frame reader with the limits in `flags` and the policy of
    /// `bvnr_doc_stream_read`; `None` when the library is out of memory.
    pub fn new(
        mut flags: BvnrReadFlags,
        continue_past_failed: bool,
        max_document_size: u64,
    ) -> Option<Self> {
        let slot = new_slot();
        flags.userdata = slot.cast();
        flags.on_verified = Some(slot_on_verified as EventCallbackFn);
        let opts = BvnrDocStreamOpts {
            flags: &mut flags,
            userdata: slot.cast(),
            on_document: Some(slot_on_document as OnDocumentFn),
            continue_past_failed,
            max_document_size,
        };
        // SAFETY: `opts` and `flags` are copied during the call; their user
        // pointer, the slot, lives as long as the frame reader.
        let fr = unsafe { bvnr_frame_reader_create(&opts) };
        match std::ptr::NonNull::new(fr) {
            Some(fr) => Some(FrameParseHandle { fr, slot }),
            None => {
                // SAFETY: the slot came from new_slot, and nothing holds it.
                unsafe { free_slot(slot) };
                None
            }
        }
    }

    /// `bvnr_frame_reader_feed`: `h.verified` sees the events of the
    /// documents `data` reaches into, `h.on_document` each one it completes.
    pub fn feed(&mut self, data: &[u8], h: &mut Handlers<'_>) -> bool {
        let fr = self.fr.as_ptr();
        // SAFETY: the slot and the frame reader are live; in parse mode
        // `data` is only read during the call.
        unsafe {
            with_slot(self.slot, h, || {
                bvnr_frame_reader_feed(fr, data.as_ptr().cast(), data.len() as u64)
            })
        }
    }

    /// `bvnr_frame_reader_finish`.
    pub fn finish(&mut self) -> bool {
        // SAFETY: the frame reader is live; finishing parses nothing.
        unsafe { bvnr_frame_reader_finish(self.fr.as_ptr()) }
    }

    /// `bvnr_frame_reader_error`.
    pub fn error(&self) -> c_int {
        // SAFETY: the frame reader is live.
        unsafe { bvnr_frame_reader_error(self.fr.as_ptr()) }
    }

    /// `bvnr_frame_reader_count`.
    pub fn count(&self) -> u64 {
        // SAFETY: the frame reader is live.
        unsafe { bvnr_frame_reader_count(self.fr.as_ptr()) }
    }
}

// SAFETY: a frame reader holds its whole state, like a reader (bovnar.h,
// THREADING); the slot is owned by the handle alone and empty between calls.
unsafe impl Send for FrameParseHandle {}

impl Drop for FrameParseHandle {
    fn drop(&mut self) {
        // SAFETY: both are live and released once, the frame reader first.
        unsafe {
            bvnr_frame_reader_destroy(self.fr.as_ptr());
            free_slot(self.slot);
        }
    }
}

/// What a pull-style `next` gave.
pub enum Next<T, E> {
    /// A document or an item.
    Item(T),
    /// The input is used up.
    None,
    /// The stream failed, for this reason.
    Error(E),
    /// The input differs from the one still being drained, which was left
    /// alone.
    OtherInput,
}

/// The input a pull-style object is draining: the library keeps a pointer
/// into it between calls. Each call is handed the input again, borrowed for
/// the call, and is refused unless it is the same bytes, so the library only
/// ever reads memory the caller lends it at that moment.
#[derive(Clone, Copy, Default, PartialEq, Eq)]
struct Attached(Option<(usize, usize)>);

impl Attached {
    fn of(input: &[u8]) -> Option<(usize, usize)> {
        Some((input.as_ptr() as usize, input.len()))
    }
}

/// A `bvnr_frame_reader_create_pull` frame reader.
pub struct FramePullHandle {
    fr: std::ptr::NonNull<c_void>,
    attached: Attached,
}

impl FramePullHandle {
    /// `None` when the library is out of memory.
    pub fn new(max_document_size: u64) -> Option<Self> {
        // SAFETY: no preconditions; the result is NULL or a fresh reader.
        let fr = unsafe { bvnr_frame_reader_create_pull(max_document_size) };
        std::ptr::NonNull::new(fr).map(|fr| FramePullHandle {
            fr,
            attached: Attached::default(),
        })
    }

    /// Whether fed bytes are still being drained.
    pub fn is_attached(&self) -> bool {
        self.attached.0.is_some()
    }

    /// Feeds `input` unless it is being drained already, then
    /// `bvnr_frame_reader_next`.
    pub fn next<'s, 'a: 's>(&'s mut self, input: &'a [u8]) -> Next<&'s [u8], c_int> {
        let fr = self.fr.as_ptr();
        match self.attached.0 {
            Some(a) if Attached::of(input) != Some(a) => return Next::OtherInput,
            Some(_) => {}
            None => {
                // SAFETY: the frame reader is live. It keeps a pointer into
                // `input` until `next` reports NONE; it reads through it only
                // in `next`, which is called only with these same bytes
                // borrowed again (checked above).
                if !unsafe { bvnr_frame_reader_feed(fr, input.as_ptr().cast(), input.len() as u64) }
                {
                    return Next::Error(self.error());
                }
                self.attached = Attached(Attached::of(input));
            }
        }
        let (mut doc, mut len) = (std::ptr::null(), 0u64);
        // SAFETY: the frame reader is live and `input` is the fed buffer,
        // borrowed for 'a; the outputs are writable.
        match unsafe { bvnr_frame_reader_next(fr, &mut doc, &mut len) } {
            // SAFETY: the document lies in `input` or in the reader's buffer,
            // valid until the next call, which needs `&mut self`.
            NEXT_DOCUMENT => Next::Item(unsafe { bytes64(doc, len) }),
            NEXT_NONE => {
                self.attached = Attached::default();
                Next::None
            }
            _ => {
                // A failed reader reads no input again.
                self.attached = Attached::default();
                Next::Error(self.error())
            }
        }
    }

    /// `bvnr_frame_reader_finish`; call it only while nothing is attached.
    pub fn finish(&mut self) -> bool {
        // SAFETY: the frame reader is live; finishing reads no input.
        unsafe { bvnr_frame_reader_finish(self.fr.as_ptr()) }
    }

    /// `bvnr_frame_reader_error`.
    pub fn error(&self) -> c_int {
        // SAFETY: the frame reader is live.
        unsafe { bvnr_frame_reader_error(self.fr.as_ptr()) }
    }

    /// `bvnr_frame_reader_count`.
    pub fn count(&self) -> u64 {
        // SAFETY: the frame reader is live.
        unsafe { bvnr_frame_reader_count(self.fr.as_ptr()) }
    }
}

// SAFETY: as for FrameParseHandle; the attached input is only an address,
// read through only while the caller lends the bytes to a call.
unsafe impl Send for FramePullHandle {}

impl Drop for FramePullHandle {
    fn drop(&mut self) {
        // SAFETY: live and destroyed once; destroying reads no input.
        unsafe { bvnr_frame_reader_destroy(self.fr.as_ptr()) }
    }
}

/// An item of a fed demultiplexer.
pub enum DemuxItem<'a> {
    /// (channel, payload)
    Message(u64, &'a [u8]),
    /// A document event this binding knows; its data is the demux's copy.
    Event(crate::enums::Event, EventView<'a>),
}

/// A `bvnr_demux_create` demultiplexer without `on_message`, fed with
/// `bvnr_demux_open_feed` and drained with `bvnr_demux_next`.
pub struct DemuxHandle {
    dm: std::ptr::NonNull<c_void>,
    attached: Attached,
}

/// What a reader counts beside its error: recoveries, the bytes they
/// skipped, and the declared `#!bovnar` version.
pub struct ReaderCounters {
    pub recovery_count: u64,
    pub skipped_bytes: u64,
    pub declared_version: Option<(u16, u16)>,
}

impl DemuxHandle {
    /// A demux for messages up to `max_message` (0 for the default), only of
    /// streams under `key` when there is one. `Err` names the call that could
    /// not allocate.
    pub fn new(max_message: u64, key: Option<&CStr>) -> Result<Self, &'static str> {
        // SAFETY: no callback, so no user pointer to keep alive.
        let dm = unsafe { bvnr_demux_create(None, std::ptr::null_mut(), max_message) };
        let dm = std::ptr::NonNull::new(dm).ok_or("bvnr_demux_create()")?;
        let handle = DemuxHandle {
            dm,
            attached: Attached::default(),
        };
        // SAFETY: the demux is live; `key` is NUL-terminated and copied.
        if key.is_some_and(|k| !unsafe { bvnr_demux_set_key(dm.as_ptr(), k.as_ptr()) }) {
            // bovnar_stream.h: false only for a NULL demux or a failed
            // allocation.
            return Err("bvnr_demux_set_key()");
        }
        Ok(handle)
    }

    /// `bvnr_demux_open_feed` with the limits in `flags` (no callbacks):
    /// starts a document.
    pub fn open(&mut self, mut flags: BvnrReadFlags, deliver_events: bool) -> bool {
        self.attached = Attached::default();
        flags.userdata = std::ptr::null_mut();
        flags.on_verified = None;
        flags.on_unverified = None;
        flags.on_error = None;
        flags.want_unit = None;
        // SAFETY: the demux is live; `flags` is copied during the call. The
        // reader is re-armed, so it holds no pointer into an earlier input.
        unsafe { bvnr_demux_open_feed(self.dm.as_ptr(), &flags, deliver_events) }
    }

    /// Whether fed bytes are still being drained.
    pub fn is_attached(&self) -> bool {
        self.attached.0.is_some()
    }

    /// Feeds `input` unless it is being drained already (or with `None`,
    /// after the finish, feeds nothing), then `bvnr_demux_next`, passing
    /// over an event this binding does not know. A failure carries
    /// `bvnr_demux_error` and the reader's error.
    pub fn next<'s, 'a: 's>(
        &'s mut self,
        input: Option<&'a [u8]>,
    ) -> Next<DemuxItem<'s>, (c_int, Option<ReaderError>)> {
        let dm = self.dm.as_ptr();
        match (self.attached.0, input) {
            (Some(a), Some(input)) if Attached::of(input) != Some(a) => return Next::OtherInput,
            (Some(_), None) => return Next::OtherInput,
            (Some(_), Some(_)) | (None, None) => {}
            (None, Some(input)) => {
                // SAFETY: the demux is live. It keeps a pointer into `input`
                // until `next` reports NONE; it reads through it only in
                // `next`, which is called only with these same bytes borrowed
                // again (checked above).
                if !unsafe { bvnr_demux_feed(dm, input.as_ptr().cast(), input.len() as u64) } {
                    return Next::Error((self.error(), self.reader_error()));
                }
                self.attached = Attached(Attached::of(input));
            }
        }
        let mut item = BvnrDemuxItem {
            channel: 0,
            data: std::ptr::null(),
            length: 0,
            event: 0,
            value: std::ptr::null(),
        };
        let (rc, event) = loop {
            // SAFETY: the demux is live, any attached input is borrowed for
            // 'a, and `item` is writable.
            let rc = unsafe { bvnr_demux_next(dm, &mut item) };
            let event = crate::enums::Event::from_raw(item.event);
            if rc != NEXT_EVENT || event.is_some() {
                break (rc, event);
            }
        };
        match (rc, event) {
            // SAFETY: the message lies in the input or in the channel's
            // buffer, valid until the next call, which needs `&mut self`.
            (NEXT_MESSAGE, _) => Next::Item(DemuxItem::Message(item.channel, unsafe {
                bytes64(item.data, item.length)
            })),
            // SAFETY: `value` points at the demux's copy of the event, whose
            // bytes are in its arena, all valid until the next call.
            (NEXT_EVENT, Some(event)) => Next::Item(DemuxItem::Event(event, unsafe {
                EventView::new(item.event, item.value.as_ref())
            })),
            (NEXT_NONE, _) => {
                self.attached = Attached::default();
                Next::None
            }
            _ => {
                // A failed demux reads no input again.
                self.attached = Attached::default();
                Next::Error((self.error(), self.reader_error()))
            }
        }
    }

    /// `bvnr_demux_finish`; call it only while nothing is attached.
    pub fn finish(&mut self) -> bool {
        // SAFETY: the demux is live; finishing reads no input.
        unsafe { bvnr_demux_finish(self.dm.as_ptr()) }
    }

    /// `bvnr_demux_error`.
    pub fn error(&self) -> c_int {
        // SAFETY: the demux is live.
        unsafe { bvnr_demux_error(self.dm.as_ptr()) }
    }

    /// The fed reader's recovery count, skipped bytes and declared version
    /// (`bvnr_demux_get_reader`), when the demux has been opened.
    pub fn reader_counters(&self) -> Option<ReaderCounters> {
        // SAFETY: the demux is live; the reader it returns is NULL or owned
        // by the demux, and only read here.
        let r = unsafe { bvnr_demux_get_reader(self.dm.as_ptr()) }.cast_mut();
        if r.is_null() {
            return None;
        }
        let (mut major, mut minor) = (0u16, 0u16);
        // SAFETY: as above; the version getter writes two u16 out-params.
        unsafe {
            let declared = bvnr_reader_get_declared_version(r, &mut major, &mut minor);
            Some(ReaderCounters {
                recovery_count: bvnr_reader_get_recovery_count(r),
                skipped_bytes: bvnr_reader_get_skipped_bytes(r),
                declared_version: declared.then_some((major, minor)),
            })
        }
    }

    /// The error of the fed reader, when the demux has been opened.
    pub fn reader_error(&self) -> Option<ReaderError> {
        // SAFETY: the demux is live; the reader it returns is NULL or owned
        // by the demux, and only read here.
        let r = unsafe { bvnr_demux_get_reader(self.dm.as_ptr()) }.cast_mut();
        // SAFETY: as above.
        (!r.is_null()).then(|| unsafe {
            ReaderError {
                code: bvnr_reader_get_error(r),
                line: bvnr_reader_get_error_line(r),
                column: bvnr_reader_get_error_column(r),
                offset: bvnr_reader_get_error_offset(r),
                byte: bvnr_reader_get_error_byte(r),
            }
        })
    }
}

// SAFETY: a demux and its reader hold their whole state (bovnar.h,
// THREADING); the attached input is only an address, read through only while
// the caller lends the bytes to a call.
unsafe impl Send for DemuxHandle {}

impl Drop for DemuxHandle {
    fn drop(&mut self) {
        // SAFETY: live and destroyed once; destroying reads no input.
        unsafe { bvnr_demux_destroy(self.dm.as_ptr()) }
    }
}

/// Checks every mirrored struct against the C compiler's own layout, as printed by the upstream ABI manifest generator
/// (tests/bvnr_abi_dump.c). It lives here rather than in tests/ because the
/// mirrors are private. The generator is compiled by the compiler the `cc`
/// crate picks for the library itself, so an MSVC build compares MSVC's
/// layout. It also checks that every struct and field the manifest lists is
/// mirrored.
#[cfg(all(test, not(target_family = "wasm")))]
mod abi_tests {
    use super::*;
    use std::collections::HashMap;
    use std::mem::{offset_of, size_of};
    use std::path::Path;
    use std::process::Command;

    fn size_of_field<T, F>(_: fn(&T) -> &F) -> usize {
        size_of::<F>()
    }

    macro_rules! layout {
        ($out:ident, $t:ty, $c:literal { $($f:ident),* $(,)? }) => {
            $out.push(($c.to_string(), String::new(), 0, size_of::<$t>()));
            $(
                $out.push((
                    $c.to_string(),
                    // `type_` mirrors C's `type`, a Rust keyword.
                    stringify!($f).trim_end_matches('_').to_string(),
                    offset_of!($t, $f),
                    size_of_field(|s: &$t| &s.$f),
                ));
            )*
        };
    }

    /// (struct, field or "" for the struct itself, offset, size)
    fn rust_layout() -> Vec<(String, String, usize, usize)> {
        let mut v = Vec::new();
        layout!(v, ValueTypeSpec, "value_type_spec_t" { family, width, base });
        layout!(v, ValueUnitPrefix, "value_unit_prefix_t" { system, id });
        layout!(v, ValueUnitComponent, "value_unit_component_t" { base, exponent, prefix });
        layout!(v, ValueUnit, "value_unit_t" { num_components, components });
        layout!(v, BvnrData, "bvnr_data_t" {
            type_, value_type, value_unit, data, length, frac_data, frac_length,
            unit_source, unit_source_length, converted, conv,
        });
        layout!(v, BvnrConverted, "bvnr_converted_t" { unit, text, length, base, num, den });
        layout!(v, BvnrUnitTarget, "bvnr_unit_target_t" { unit, base });
        layout!(v, BvnrUnitRule, "bvnr_unit_rule_t" { path, unit, base, mode });
        layout!(v, BvnrUnitPolicy, "bvnr_unit_policy_t" {
            rules, num_rules, targets, num_targets, base, normalise, on_inexact,
            require_unit, require_dimension_of, num_require_dimension_of,
        });
        layout!(v, BvnrReadFlags, "bvnr_read_flags_t" {
            max_identifier_length, max_string_length, max_number_length,
            max_symbol_length, max_reference_length, max_array_items, max_text_bytes,
            max_file_size, max_struct_nesting, max_array_nesting, userdata,
            on_unverified, on_verified, continue_on_error, on_error, strict_version,
            text_only, want_unit_allow_nonterminating, max_conversion_length,
            want_unit, _reserved,
        });
        layout!(v, BvnrWriteFlags, "bvnr_write_flags_t" {
            max_identifier_length, max_string_length, max_number_length,
            max_symbol_length, max_reference_length, max_array_items, max_text_bytes,
            max_file_size, max_struct_nesting, max_array_nesting, userdata, on_event,
            continue_on_error, on_error, unit_flags, emit_version, _reserved,
        });
        layout!(v, BvnrDocStreamOpts, "bvnr_doc_stream_opts_t" {
            flags, userdata, on_document, continue_past_failed, max_document_size,
        });
        layout!(v, BvnrDemuxItem, "bvnr_demux_item_t" { channel, data, length, event, value });
        layout!(v, BvnDomEntry, "bvn_dom_entry_t" { key, value });
        layout!(v, BvnCurrencyInfo, "bvn_currency_info_t" {
            code, numeric_code, minor_unit, is_crypto, name,
        });
        use timekeeping_fns::{BvnDatetime, BvnGregorianDate};
        layout!(v, BvnGregorianDate, "bvn_gregorian_date_t" {
            year, month, day, leap, wday, yday,
        });
        layout!(v, BvnDatetime, "bvn_datetime_t" { date, hour, minute, second, mjd, submjd });
        v
    }

    type Manifest = HashMap<(String, String), (usize, usize)>;

    /// The C compiler's layout of every struct: (struct, field or "") ->
    /// (offset, size). Built once and shared by the three tests.
    fn c_manifest() -> &'static Manifest {
        static MANIFEST: std::sync::OnceLock<Manifest> = std::sync::OnceLock::new();
        MANIFEST.get_or_init(build_c_manifest)
    }

    fn build_c_manifest() -> Manifest {
        let repo = Path::new(env!("CARGO_MANIFEST_DIR")).parent().unwrap();
        let out_dir = Path::new(env!("OUT_DIR"));
        let exe = out_dir.join(if cfg!(windows) {
            "bvnr_abi_dump.exe"
        } else {
            "bvnr_abi_dump"
        });
        let compiler = cc::Build::new()
            .target(env!("BVNR_BUILD_TARGET"))
            .host(env!("BVNR_BUILD_HOST"))
            .opt_level(0)
            .cargo_metadata(false)
            .get_compiler();
        if cfg!(target_env = "msvc") {
            assert!(
                compiler.is_like_msvc(),
                "an MSVC build must compare MSVC's layout"
            );
        }
        let mut cmd = compiler.to_command();
        if compiler.is_like_msvc() {
            cmd.arg("/I")
                .arg(repo.join("include"))
                .arg(repo.join("tests/bvnr_abi_dump.c"))
                .arg(format!("/Fo{}\\", out_dir.display()))
                .arg(format!("/Fe{}", exe.display()));
        } else {
            cmd.arg("-std=c99")
                .arg("-I")
                .arg(repo.join("include"))
                .arg(repo.join("tests/bvnr_abi_dump.c"))
                .arg("-o")
                .arg(&exe);
        }
        let status = cmd.status().expect("run the C compiler");
        assert!(
            status.success(),
            "compiling bvnr_abi_dump.c with {cmd:?} failed"
        );
        let out = Command::new(&exe).output().expect("run bvnr_abi_dump");
        let text = String::from_utf8(out.stdout).unwrap();

        let mut c: HashMap<(String, String), (usize, usize)> = HashMap::new();
        for line in text.lines() {
            let p: Vec<&str> = line.split_whitespace().collect();
            match p.as_slice() {
                ["STRUCT", s, size] => {
                    c.insert((s.to_string(), String::new()), (0, size.parse().unwrap()));
                }
                ["FIELD", s, f, off, size] => {
                    c.insert(
                        (s.to_string(), f.to_string()),
                        (off.parse().unwrap(), size.parse().unwrap()),
                    );
                }
                _ => panic!("unexpected ABI line: {line}"),
            }
        }

        c
    }

    #[test]
    fn test_struct_size_matches_c() {
        let c = c_manifest();
        for (s, f, off, size) in rust_layout().iter().filter(|e| e.1.is_empty()) {
            let want = c.get(&(s.clone(), f.clone()));
            assert_eq!(want, Some(&(*off, *size)), "{s}: Rust size");
        }
        // The opaque blobs may be larger than C's, never smaller: C would
        // write past a smaller Rust allocation.
        for (s, rust_size) in [
            ("bvnr_source_t", size_of::<BvnrSource>()),
            ("bvnr_sink_t", size_of::<BvnrSink>()),
        ] {
            assert!(
                rust_size >= c[&(s.to_string(), String::new())].1,
                "{s} too small"
            );
        }
    }

    #[test]
    fn test_field_offsets_match_c() {
        let c = c_manifest();
        for (s, f, off, size) in rust_layout().iter().filter(|e| !e.1.is_empty()) {
            let want = c.get(&(s.clone(), f.clone()));
            assert_eq!(want, Some(&(*off, *size)), "{s}.{f}: Rust (offset, size)");
        }
    }

    /// Every struct and field the manifest lists is mirrored (the two opaque
    /// blobs are checked by size only).
    #[test]
    fn test_all_c_structures_are_covered() {
        assert_eq!(
            c_manifest().len(),
            rust_layout().len() + 2,
            "fields mirrored vs. C manifest"
        );
    }
}

/// The civil-time and calendar functions of `bvn_datetime.h` and
/// `bvn_gregorian_date.h`, behind `bovnar::timekeeping`. Not declared:
/// `bvn_gregorian_date_to_mjd_fast` (undefined behaviour outside the range
/// the checked `bvn_gregorian_date_to_mjd` tests for) and `bvn_dt_dump` /
/// `bvn_dt_print` (they print to stdout).
pub mod timekeeping_fns {
    use std::ffi::c_int;

    /// `bvn_gregorian_date_t`.
    #[repr(C)]
    #[derive(Clone, Copy, Debug, Default)]
    pub struct BvnGregorianDate {
        pub year: i64,
        pub month: i64,
        pub day: i64,
        pub leap: i32,
        pub wday: i32,
        pub yday: i32,
    }

    /// `bvn_datetime_t`.
    #[repr(C)]
    #[derive(Clone, Copy, Debug, Default)]
    pub struct BvnDatetime {
        pub date: BvnGregorianDate,
        pub hour: i64,
        pub minute: i64,
        pub second: i64,
        pub mjd: i64,
        pub submjd: i64,
    }

    /// `BVN_GDATE_OVF`: the int64 functions' "no value" sentinel.
    pub const GDATE_OVF: i64 = i64::MIN;

    type Date = BvnGregorianDate;

    // bvn_epoch_t is a C enum: an int.
    unsafe extern "C" {
        fn bvn_dt_datetime_to_epoch_seconds(dt: *mut BvnDatetime, epoch: c_int) -> i64;
        fn bvn_dt_epoch_seconds_to_datetime(dt: *mut BvnDatetime, epoch: c_int, seconds: i64);
        fn bvn_dt_convert_epoch_seconds(from: c_int, to: c_int, seconds: i64) -> i64;
        fn bvn_dt_epoch_seconds_from_gps_time(tow_ms: i64, week: i64, epoch: c_int) -> i64;
        fn bvn_dt_tai_seconds_from_gps_time(tow_ms: i64, week: i64) -> i64;
        fn bvn_dt_epoch_seconds_from_galileo_time(tow_ms: i64, week: i64, epoch: c_int) -> i64;
        fn bvn_dt_tai_seconds_from_galileo_time(tow_ms: i64, week: i64) -> i64;
        fn bvn_dt_utc_to_tai_seconds(dt: *mut BvnDatetime) -> i64;
        fn bvn_dt_tai_seconds_to_utc(dt: *mut BvnDatetime, tai_seconds: i64);
        fn bvn_dt_tai_seconds_to_local_time(
            dt: *mut BvnDatetime,
            tai_seconds: i64,
            timezone_offset_seconds: i64,
            dls_active: c_int,
        );
        fn bvn_dt_is_date_equal(a: *mut BvnDatetime, b: *mut BvnDatetime) -> c_int;
        fn bvn_dt_is_time_equal(a: *mut BvnDatetime, b: *mut BvnDatetime) -> c_int;
        fn bvn_dt_is_equal(a: *mut BvnDatetime, b: *mut BvnDatetime) -> c_int;
        fn bvn_dt_tai_second_is_dls_in_europe(tai_second: i64) -> c_int;

        fn bvn_gregorian_date_to_mjd(g: *const Date) -> i64;
        fn bvn_gregorian_date_mjd_to_wday(mjd: i64) -> i32;
        fn bvn_gregorian_date_mjd_in_leap_year(mjd: i64) -> bool;
        fn bvn_gregorian_date_mjd_to_yday(mjd: i64) -> i32;
        fn bvn_gregorian_date_from_mjd(g: *mut Date, mjd: i64);
        fn bvn_gregorian_date_normalize(g: *mut Date) -> bool;
        fn bvn_gregorian_date_is_equal(a: *mut Date, b: *mut Date) -> bool;
        fn bvn_gregorian_date_easter_sunday_mjd(year: i64) -> i64;
        fn bvn_gregorian_date_calc_easter_sunday(g: *mut Date, year: i64) -> bool;
        fn bvn_gregorian_date_calc_good_friday(g: *mut Date, year: i64) -> bool;
        fn bvn_gregorian_date_calc_easter_monday(g: *mut Date, year: i64) -> bool;
        fn bvn_gregorian_date_calc_ascension_day(g: *mut Date, year: i64) -> bool;
        fn bvn_gregorian_date_calc_whit_monday(g: *mut Date, year: i64) -> bool;
        fn bvn_gregorian_date_is_federal_holiday_in_germany(d: *mut Date) -> bool;
        fn bvn_gregorian_date_is_banking_day_in_germany(d: *mut Date) -> bool;
        fn bvn_gregorian_date_is_business_day_in_germany(d: *mut Date) -> bool;
        fn bvn_gregorian_date_get_last_banking_day_in_month(res: *mut Date, d: *mut Date);
        fn bvn_gregorian_date_get_penultimate_banking_day_in_month(res: *mut Date, d: *mut Date);
        fn bvn_gregorian_date_get_mid_banking_day_in_month(res: *mut Date, d: *mut Date);
        fn bvn_gregorian_date_get_last_sunday_in_month(res: *mut Date, d: *mut Date);
        fn bvn_gregorian_date_get_last_saturday_in_month(res: *mut Date, d: *mut Date);
        fn bvn_gregorian_date_get_first_banking_day_in_month(res: *mut Date, d: *mut Date);
        fn bvn_gregorian_date_get_last_banking_day_in_quarter(
            q: c_int,
            res: *mut Date,
            d: *mut Date,
        );
        fn bvn_gregorian_date_get_first_banking_day_in_quarter(
            q: c_int,
            res: *mut Date,
            d: *mut Date,
        );
        fn bvn_gregorian_date_is_last_banking_day_in_month(d: *mut Date) -> bool;
        fn bvn_gregorian_date_is_penultimate_banking_day_in_month(d: *mut Date) -> bool;
        fn bvn_gregorian_date_is_mid_banking_day_in_month(d: *mut Date) -> bool;
        fn bvn_gregorian_date_is_first_banking_day_in_month(d: *mut Date) -> bool;
        fn bvn_gregorian_date_is_last_banking_day_in_quarter(q: c_int, d: *mut Date) -> bool;
        fn bvn_gregorian_date_is_first_banking_day_in_quarter(q: c_int, d: *mut Date) -> bool;
        fn bvn_gregorian_date_is_daylight_saving_switch_in_germany(d: *mut Date) -> bool;
        fn bvn_gregorian_date_is_day_before_daylight_saving_switch_in_germany(d: *mut Date)
        -> bool;
    }

    macro_rules! by_value {
        ($($name:ident => $c:ident($($a:ident: $t:ty),*) -> $r:ty;)*) => {$(
            #[doc = concat!("`", stringify!($c), "`.")]
            pub fn $name($($a: $t),*) -> $r {
                // SAFETY: plain values in, a plain value out.
                unsafe { $c($($a),*) }
            }
        )*};
    }

    by_value! {
        convert_epoch_seconds => bvn_dt_convert_epoch_seconds(
            from: c_int, to: c_int, seconds: i64) -> i64;
        epoch_seconds_from_gps_time => bvn_dt_epoch_seconds_from_gps_time(
            tow_ms: i64, week: i64, epoch: c_int) -> i64;
        tai_seconds_from_gps_time => bvn_dt_tai_seconds_from_gps_time(
            tow_ms: i64, week: i64) -> i64;
        epoch_seconds_from_galileo_time => bvn_dt_epoch_seconds_from_galileo_time(
            tow_ms: i64, week: i64, epoch: c_int) -> i64;
        tai_seconds_from_galileo_time => bvn_dt_tai_seconds_from_galileo_time(
            tow_ms: i64, week: i64) -> i64;
        mjd_to_wday => bvn_gregorian_date_mjd_to_wday(mjd: i64) -> i32;
        mjd_in_leap_year => bvn_gregorian_date_mjd_in_leap_year(mjd: i64) -> bool;
        mjd_to_yday => bvn_gregorian_date_mjd_to_yday(mjd: i64) -> i32;
        easter_sunday_mjd => bvn_gregorian_date_easter_sunday_mjd(year: i64) -> i64;
    }

    /// `bvn_dt_tai_second_is_dls_in_europe`.
    pub fn tai_second_is_dls_in_europe(tai_second: i64) -> bool {
        // SAFETY: a plain value in, a plain value out.
        unsafe { bvn_dt_tai_second_is_dls_in_europe(tai_second) != 0 }
    }

    /// `bvn_dt_epoch_seconds_to_datetime`; fills every field for any int64.
    pub fn epoch_seconds_to_datetime(epoch: c_int, seconds: i64) -> BvnDatetime {
        let mut dt = BvnDatetime::default();
        // SAFETY: `dt` is writable for the call.
        unsafe { bvn_dt_epoch_seconds_to_datetime(&mut dt, epoch, seconds) };
        dt
    }

    /// `bvn_dt_datetime_to_epoch_seconds` on a copy of `dt` (C normalizes
    /// it in place); `GDATE_OVF` on overflow.
    pub fn datetime_to_epoch_seconds(mut dt: BvnDatetime, epoch: c_int) -> i64 {
        // SAFETY: `dt` is a local, writable for the call.
        unsafe { bvn_dt_datetime_to_epoch_seconds(&mut dt, epoch) }
    }

    /// `bvn_dt_utc_to_tai_seconds` on a copy of `dt`; `GDATE_OVF` on
    /// overflow.
    pub fn utc_to_tai_seconds(mut dt: BvnDatetime) -> i64 {
        // SAFETY: `dt` is a local, writable for the call.
        unsafe { bvn_dt_utc_to_tai_seconds(&mut dt) }
    }

    /// `bvn_dt_tai_seconds_to_utc`; `None` where C leaves the output
    /// untouched (the offset does not fit in int64). Day 0 never comes out
    /// of the library, so it marks "untouched".
    pub fn tai_seconds_to_utc(tai_seconds: i64) -> Option<BvnDatetime> {
        let mut dt = BvnDatetime::default();
        // SAFETY: `dt` is writable for the call.
        unsafe { bvn_dt_tai_seconds_to_utc(&mut dt, tai_seconds) };
        (dt.date.day != 0).then_some(dt)
    }

    /// `bvn_dt_tai_seconds_to_local_time`; `None` as for
    /// [`tai_seconds_to_utc`].
    pub fn tai_seconds_to_local_time(
        tai_seconds: i64,
        offset_seconds: i64,
        dls_active: bool,
    ) -> Option<BvnDatetime> {
        let mut dt = BvnDatetime::default();
        // SAFETY: `dt` is writable for the call.
        unsafe {
            bvn_dt_tai_seconds_to_local_time(
                &mut dt,
                tai_seconds,
                offset_seconds,
                c_int::from(dls_active),
            )
        };
        (dt.date.day != 0).then_some(dt)
    }

    macro_rules! dt_compare {
        ($($name:ident => $c:ident;)*) => {$(
            #[doc = concat!("`", stringify!($c), "`.")]
            pub fn $name(mut a: BvnDatetime, mut b: BvnDatetime) -> bool {
                // SAFETY: two locals, only read by C.
                unsafe { $c(&mut a, &mut b) != 0 }
            }
        )*};
    }

    dt_compare! {
        dt_is_date_equal => bvn_dt_is_date_equal;
        dt_is_time_equal => bvn_dt_is_time_equal;
        dt_is_equal => bvn_dt_is_equal;
    }

    /// `bvn_gregorian_date_to_mjd`; `GDATE_OVF` out of range.
    pub fn date_to_mjd(g: BvnGregorianDate) -> i64 {
        // SAFETY: a valid pointer to a local, only read.
        unsafe { bvn_gregorian_date_to_mjd(&g) }
    }

    /// `bvn_gregorian_date_from_mjd`; `None` where C leaves the output
    /// untouched (`mjd` out of range).
    pub fn date_from_mjd(mjd: i64) -> Option<BvnGregorianDate> {
        let mut g = BvnGregorianDate::default();
        // SAFETY: `g` is writable for the call.
        unsafe { bvn_gregorian_date_from_mjd(&mut g, mjd) };
        (g.day != 0).then_some(g)
    }

    /// `bvn_gregorian_date_normalize` of a copy of `g`; `None` on overflow.
    pub fn date_normalize(mut g: BvnGregorianDate) -> Option<BvnGregorianDate> {
        // SAFETY: `g` is a local, writable for the call.
        unsafe { bvn_gregorian_date_normalize(&mut g) }.then_some(g)
    }

    /// `bvn_gregorian_date_is_equal`.
    pub fn date_is_equal(mut a: BvnGregorianDate, mut b: BvnGregorianDate) -> bool {
        // SAFETY: two locals, only read by C.
        unsafe { bvn_gregorian_date_is_equal(&mut a, &mut b) }
    }

    macro_rules! feast {
        ($($name:ident => $c:ident;)*) => {$(
            #[doc = concat!("`", stringify!($c), "`; `None` when C returns false.")]
            pub fn $name(year: i64) -> Option<BvnGregorianDate> {
                let mut g = BvnGregorianDate::default();
                // SAFETY: `g` is writable for the call.
                unsafe { $c(&mut g, year) }.then_some(g)
            }
        )*};
    }

    feast! {
        calc_easter_sunday => bvn_gregorian_date_calc_easter_sunday;
        calc_good_friday => bvn_gregorian_date_calc_good_friday;
        calc_easter_monday => bvn_gregorian_date_calc_easter_monday;
        calc_ascension_day => bvn_gregorian_date_calc_ascension_day;
        calc_whit_monday => bvn_gregorian_date_calc_whit_monday;
    }

    macro_rules! date_pred {
        ($($name:ident => $c:ident;)*) => {$(
            #[doc = concat!("`", stringify!($c), "`.")]
            pub fn $name(mut d: BvnGregorianDate) -> bool {
                // SAFETY: a local, only read by C.
                unsafe { $c(&mut d) }
            }
        )*};
    }

    date_pred! {
        is_federal_holiday_in_germany => bvn_gregorian_date_is_federal_holiday_in_germany;
        is_banking_day_in_germany => bvn_gregorian_date_is_banking_day_in_germany;
        is_business_day_in_germany => bvn_gregorian_date_is_business_day_in_germany;
        is_last_banking_day_in_month => bvn_gregorian_date_is_last_banking_day_in_month;
        is_penultimate_banking_day_in_month =>
            bvn_gregorian_date_is_penultimate_banking_day_in_month;
        is_mid_banking_day_in_month => bvn_gregorian_date_is_mid_banking_day_in_month;
        is_first_banking_day_in_month => bvn_gregorian_date_is_first_banking_day_in_month;
        is_daylight_saving_switch_in_germany =>
            bvn_gregorian_date_is_daylight_saving_switch_in_germany;
        is_day_before_daylight_saving_switch_in_germany =>
            bvn_gregorian_date_is_day_before_daylight_saving_switch_in_germany;
    }

    macro_rules! date_get {
        ($($name:ident => $c:ident;)*) => {$(
            #[doc = concat!("`", stringify!($c), "`; `None` where C leaves `res` untouched.")]
            pub fn $name(mut d: BvnGregorianDate) -> Option<BvnGregorianDate> {
                let mut res = BvnGregorianDate::default();
                // SAFETY: two distinct locals; C writes `res` only.
                unsafe { $c(&mut res, &mut d) };
                (res.day != 0).then_some(res)
            }
        )*};
    }

    date_get! {
        last_banking_day_in_month => bvn_gregorian_date_get_last_banking_day_in_month;
        penultimate_banking_day_in_month =>
            bvn_gregorian_date_get_penultimate_banking_day_in_month;
        mid_banking_day_in_month => bvn_gregorian_date_get_mid_banking_day_in_month;
        first_banking_day_in_month => bvn_gregorian_date_get_first_banking_day_in_month;
        last_sunday_in_month => bvn_gregorian_date_get_last_sunday_in_month;
        last_saturday_in_month => bvn_gregorian_date_get_last_saturday_in_month;
    }

    /// `bvn_gregorian_date_get_{last,first}_banking_day_in_quarter`; `None`
    /// where C leaves `res` untouched.
    pub fn banking_day_in_quarter(
        last: bool,
        quarter: c_int,
        mut d: BvnGregorianDate,
    ) -> Option<BvnGregorianDate> {
        let mut res = BvnGregorianDate::default();
        // SAFETY: two distinct locals; C writes `res` only.
        unsafe {
            if last {
                bvn_gregorian_date_get_last_banking_day_in_quarter(quarter, &mut res, &mut d)
            } else {
                bvn_gregorian_date_get_first_banking_day_in_quarter(quarter, &mut res, &mut d)
            }
        };
        (res.day != 0).then_some(res)
    }

    /// `bvn_gregorian_date_is_{last,first}_banking_day_in_quarter`.
    pub fn is_banking_day_in_quarter(last: bool, quarter: c_int, mut d: BvnGregorianDate) -> bool {
        // SAFETY: a local, only read by C.
        unsafe {
            if last {
                bvn_gregorian_date_is_last_banking_day_in_quarter(quarter, &mut d)
            } else {
                bvn_gregorian_date_is_first_banking_day_in_quarter(quarter, &mut d)
            }
        }
    }
}
