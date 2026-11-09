// SPDX-License-Identifier: MIT
//! The C library's validators and literal helpers (`bovnar.h`): the checks
//! the writer applies to keys, symbols, references and numbers, the
//! integer and float literal parsers and formatters it uses, and the parser
//! of a type annotation.
//!
//! Each function is the C function of the same name with the `bvn_` prefix
//! dropped, and answers exactly as it does; the notes say where that is not
//! what the name suggests. A string holding NUL is none of the things these
//! check: a validator answers `false` for it, a parser fails.

use crate::enums::ErrorCode;
use crate::error::{Error, Result};
use crate::ffi::{self, ValueTypeSpec, literal_fns as c};
use crate::unit::Unit;

/// A parsed type annotation: what [`parse_type_annotation`] returns.
#[derive(Clone, Debug, PartialEq)]
#[non_exhaustive]
pub struct TypeAnnotation {
    /// The family, width and base (for a `datetime`, `base` holds the epoch
    /// index; for a `float_fix`, the q).
    pub value_type: ValueTypeSpec,
    /// The unit; [`Unit::default`] (no components) when the annotation has
    /// no unit parameter, or spells it `no_unit`.
    pub unit: Unit,
    /// The unit parameter exactly as written, `""` when there is none. A
    /// `datetime`'s epoch name is not a unit and is not here.
    pub unit_source: String,
}

/// `bvn_parse_type_annotation`: the text between the angle brackets of a
/// type annotation, `"uint:32,_16,k~g"` say, as the reader parses it.
///
/// A refusal is an [`Error::Parse`] with the code the reader gives the same
/// annotation in a document: `illegal_value_type` when the annotation itself
/// is malformed (an unknown family, a parameter given twice or not allowed
/// for the family, a width or base out of range), `unit_too_long` for a unit
/// parameter over 255 bytes, and for one that is not a unit this build knows
/// the code [`crate::units::unit_error_code`] gives it (`unit_illegal`, or a
/// profile code). The reader's version gates are not applied: `datetime`
/// (spec 1.1) and profile units (spec 1.2) parse here whatever a document
/// declares.
pub fn parse_type_annotation(text: &str) -> Result<TypeAnnotation> {
    let a = c::parse_type_annotation(text.as_bytes()).ok_or_else(|| {
        Error::argument(format!(
            "a type annotation is at most 4 GiB, got {} bytes",
            text.len()
        ))
    })?;
    if !a.type_ok {
        return Err(Error::parse_code(ErrorCode::IllegalValueType.into()));
    }
    // The unit parameter is a run of the input between ASCII commas, so it
    // is UTF-8 whenever the input is.
    let unit_source = String::from_utf8_lossy(&a.unit_text).into_owned();
    if !a.unit_ok {
        let code = if a.unit_too_long {
            ErrorCode::UnitTooLong.into()
        } else if unit_source.is_empty() {
            // Brace or bracket nesting past the bound: no text was kept.
            ErrorCode::UnitIllegal.into()
        } else {
            ffi::unit_error_code(&unit_source)
        };
        return Err(Error::parse_code(code));
    }
    Ok(TypeAnnotation {
        value_type: a.spec,
        unit: Unit(a.unit),
        unit_source,
    })
}

/// `bvn_validate_identifier`: may `s` be a key? It starts with an ASCII
/// letter, `_` or a character from U+00C0 on, and holds no space, control
/// character, U+0080 to U+00BF, or punctuation spec §4.2 forbids in a key
/// (`-` and `+` are allowed); and it is valid UTF-8. There is no length
/// limit here: the reader's `max_identifier_length` (255 by default) is
/// separate.
pub fn validate_identifier(s: &str) -> bool {
    c::validate_identifier(s)
}

/// `bvn_validate_symbol`: a symbol is spelled as a key is.
pub fn validate_symbol(s: &str) -> bool {
    c::validate_symbol(s)
}

/// `bvn_validate_reference`: a path such as `.a.b[0].c`, without the `&`
/// that marks a reference in a document. Every segment is an identifier,
/// each optionally followed by `[digits]` indexes.
pub fn validate_reference(s: &str) -> bool {
    c::validate_reference(s)
}

/// `bvn_validate_number`: a decimal number, `-`? digits with at most one `.`
/// and an optional exponent (`e` or `E`, an optional sign, digits). No
/// leading `+`, and not `inf`, `ninf` or `nan` (see
/// [`is_special_number_string`]).
pub fn validate_number(s: &str) -> bool {
    c::validate_number(s)
}

/// `bvn_is_special_number_string`: exactly `inf`, `ninf` or `nan`.
pub fn is_special_number_string(s: &str) -> bool {
    c::is_special_number_string(s)
}

/// `bvn_validate_digits_for_base`: an optional `-` and at least one digit of
/// `base`, the form of an integer literal. In bases 64 and 85, where `+` and
/// `-` are digits, there is no sign. The base is not checked against the
/// ones a type may declare: in base 1 the one digit is `0`, and a base
/// above 62 (other than 64 and 85) has the 62 digits of base 62.
pub fn validate_digits_for_base(s: &str, base: u32) -> bool {
    c::validate_digits_for_base(s, base)
}

/// `bvn_validate_number_in_base`: the form of a quoted float literal in
/// `base`: either sign, digits of `base` with at most one `.`, and an
/// exponent marked `e`/`E` while `e` is no digit (bases up to 14) or
/// `p`/`P` in the power-of-two bases up to 16, its digits decimal. Bases 64
/// and 85 take only [`validate_digits_for_base`]'s digit strings.
pub fn validate_number_in_base(s: &str, base: u32) -> bool {
    c::validate_number_in_base(s, base)
}

/// `bvn_validate_uint_range`: does the unsigned literal `s` in `base` fit
/// `width` bits? A width of 0 means unconstrained, and `inf`, `ninf` and
/// `nan` pass. It is meant for text the digit check has passed: an empty
/// string, and in base 10 anything but digits after an optional sign, is
/// refused, but a leading `+` is taken.
pub fn validate_uint_range(s: &str, width: u32, base: u32) -> bool {
    c::validate_uint_range(s, width, base)
}

/// `bvn_validate_sint_range`: [`validate_uint_range`] for a signed literal,
/// from `-2^(width-1)` to `2^(width-1) - 1`.
pub fn validate_sint_range(s: &str, width: u32, base: u32) -> bool {
    c::validate_sint_range(s, width, base)
}

/// `bvn_validate_string`: is `data` well-formed UTF-8 (no overlong form, no
/// surrogate, nothing past U+10FFFF, no sequence cut short)? The same test
/// as [`std::str::from_utf8`]. It says nothing about the control bytes the
/// writer refuses in a string: NUL passes.
pub fn validate_string(data: &[u8]) -> bool {
    c::validate_string(data)
}

/// `bvn_char_to_digit`: the value of digit `ch` in `base`, `None` when it is
/// no digit of that base. Up to base 36 a letter's case does not matter;
/// above it `a`–`z` are 10–35 and `A`–`Z` 36–61. Bases 64 and 85 use the
/// Base64 and Ascii85 alphabets.
pub fn char_to_digit(ch: char, base: u32) -> Option<u32> {
    let d = c::char_to_digit(u32::from(ch), base);
    (d < base).then_some(d)
}

/// `bvn_min_digits_for_type`: how many digits of the type's base the largest
/// value of an integer type of up to 64 bits takes (`uint:8,_16` is 2). 0
/// for a width of 0 or above 64 and for a type that is not numeric; a float
/// type is answered as if it were an unsigned integer of its width.
pub fn min_digits_for_type(vt: ValueTypeSpec) -> u32 {
    c::min_digits_for_type(vt)
}

/// `bvn_format_uint64`: `value` in `base`, padded with zero digits to
/// `min_digits` (which the C function caps at 64). Fails for a base other
/// than 2 to 62, 64 or 85.
pub fn format_uint64(value: u64, base: u32, min_digits: u32) -> Result<String> {
    c::format_uint64(value, base, min_digits).ok_or_else(|| {
        Error::argument(format!(
            "format_uint64: base {base} is not one a literal takes"
        ))
    })
}

/// `bvn_format_int64`: as [`format_uint64`], with a `-` before a negative
/// value. Also fails for a negative value in base 64 or 85, which have no
/// sign.
pub fn format_int64(value: i64, base: u32, min_digits: u32) -> Result<String> {
    c::format_int64(value, base, min_digits).ok_or_else(|| {
        Error::argument(format!(
            "format_int64: {value} cannot be written in base {base}"
        ))
    })
}

/// `bvn_format_double`: the literal the writer emits for `value` under type
/// `vt`: the shortest digits that read back to the same double (`0.1` is
/// `1e-1`), in the type's base, 10 (also for a base of 0) or 16 for a hex
/// float; any other base fails. A `float:16` or `float:32` value is first
/// rounded to IEEE binary16 or binary32, as the writer writes it (2049 is
/// `2.048e+3` under `float:16`), then printed with the digits of a 16- or
/// 32-bit significand, enough to read back as that value without rounding to
/// the format (65504 is `6.5504e+4`); a finite value the format cannot hold
/// fails.
pub fn format_double(value: f64, vt: ValueTypeSpec) -> Result<String> {
    c::format_double(value, vt)
        .ok_or_else(|| Error::argument(format!("format_double: {value} under {vt:?}")))
}

/// `bvn_parse_int64`: the signed literal `s` in the type's base, refused
/// when it does not fit the type's width (64 when it declares none). No
/// leading `+` or space, and no `0x` prefix in base 16: what the reader
/// takes.
pub fn parse_int64(s: &str, vt: ValueTypeSpec) -> Result<i64> {
    c::parse_int64(s, vt).ok_or_else(|| refused("parse_int64", s, vt))
}

/// `bvn_parse_uint64`: [`parse_int64`] for an unsigned literal; there is no
/// sign (in bases 64 and 85 `+` and `-` are digits).
pub fn parse_uint64(s: &str, vt: ValueTypeSpec) -> Result<u64> {
    c::parse_uint64(s, vt).ok_or_else(|| refused("parse_uint64", s, vt))
}

/// `bvn_parse_double`: [`parse_double_in_base`] in the type's base.
pub fn parse_double(s: &str, vt: ValueTypeSpec) -> Result<f64> {
    c::parse_double(s, vt).ok_or_else(|| refused("parse_double", s, vt))
}

/// `bvn_parse_double_in_base`: the float literal `s` as the nearest double,
/// rounded once from its exact value. Base 10 or 16 only (a hex float has
/// no `0x` and its exponent is `p`); either sign, and `inf`, `ninf` and
/// `nan`, but not `-inf`.
pub fn parse_double_in_base(s: &str, base: u32) -> Result<f64> {
    c::parse_double_in_base(s, base).ok_or_else(|| {
        Error::argument(format!(
            "parse_double_in_base: {s:?} is not a float literal in base {base}"
        ))
    })
}

/// `bvn_get_escape_repl_table`: for each byte, the byte the escape `\` +
/// that byte stands for in a string, or 0 when it is no escape of this
/// table (`t` → TAB, `n`, `v`, `f`, `r`, `"`, `\`). The `\x` and `\u`
/// escapes of spec 1.1 take digits and are not in it.
pub fn escape_repl_table() -> &'static [u8; 256] {
    c::escape_repl_table()
}

fn refused(what: &str, s: &str, vt: ValueTypeSpec) -> Error {
    Error::argument(format!("{what}: {s:?} is not a value of {vt:?}"))
}
