// SPDX-License-Identifier: MIT
//! `bovnar::literals` (the C validators, literal helpers and type-annotation
//! parser), the DOM builder (`NewNode`, `NewStruct`, `NewArray`,
//! `DomDoc::create` / `add`), and the `ok` flag of `unit_convert_factor`.

use bovnar::literals::{self, TypeAnnotation};
use bovnar::units::{convert_value, unit_convert_factor};
use bovnar::{
    DomDoc, DomType, Error, ErrorCode, NewArray, NewNode, NewStruct, Signedness, Unit, Value,
    ValueTypeFamily, ValueTypeSpec,
};
use num_bigint::BigInt;

fn vt(family: ValueTypeFamily, width: u32, base: u32) -> ValueTypeSpec {
    ValueTypeSpec {
        family: family as i32,
        width,
        base,
    }
}

fn annotation(text: &str) -> TypeAnnotation {
    literals::parse_type_annotation(text).unwrap_or_else(|e| panic!("{text:?}: {e}"))
}

fn annotation_error(text: &str) -> Option<ErrorCode> {
    match literals::parse_type_annotation(text) {
        Ok(a) => panic!("{text:?} was accepted as {a:?}"),
        Err(e @ Error::Parse { .. }) => e.code(),
        Err(e) => panic!("{text:?}: not a parse error: {e:?}"),
    }
}

// ---------------------------------------------------------------------------
// parse_type_annotation

#[test]
fn a_type_annotation_parses_into_spec_unit_and_spelling() {
    let a = annotation("uint:32,_16,k~g");
    assert_eq!(a.value_type, vt(ValueTypeFamily::Uint, 32, 16));
    assert_eq!(a.unit.to_string(), "k~g");
    assert_eq!(a.unit_source, "k~g");

    // Parameters are told apart by their class, not their place.
    assert_eq!(
        annotation("uint:_16,32").value_type,
        vt(ValueTypeFamily::Uint, 32, 16)
    );
    assert_eq!(
        annotation("uint: 32, _16").value_type,
        vt(ValueTypeFamily::Uint, 32, 16)
    );

    let plain = annotation("float");
    assert_eq!(plain.value_type, vt(ValueTypeFamily::Float, 0, 0));
    assert_eq!(plain.unit.num_components(), 0);
    assert_eq!(plain.unit_source, "");

    // float_fix's q lands in base; a datetime's epoch is its index there and
    // is no unit.
    assert_eq!(
        annotation("float_fix:32,q8").value_type,
        vt(ValueTypeFamily::FloatFix, 32, 8)
    );
    let tai = annotation("datetime:64,tai");
    assert_eq!(
        tai.value_type.base,
        bovnar::datetime_epoch_index("tai").unwrap()
    );
    assert_eq!(
        (tai.unit.num_components(), tai.unit_source.as_str()),
        (0, "")
    );

    // no_unit is no unit, spelled.
    let none = annotation("float:64,no_unit");
    assert_eq!(
        (none.unit.num_components(), none.unit_source.as_str()),
        (0, "no_unit")
    );

    // A profile spelling is kept beside the canonical unit, a comma inside
    // its braces included.
    let ml = annotation("float:64,ucum:mL{total}");
    assert_eq!(
        (ml.unit.to_string(), ml.unit_source.as_str()),
        ("m~L".into(), "ucum:mL{total}")
    );
    let cells = annotation("float:64,ucum:{cells,total},_16");
    assert_eq!(cells.unit_source, "ucum:{cells,total}");
    assert_eq!(cells.value_type.base, 16);
}

#[test]
fn a_malformed_annotation_is_illegal_value_type() {
    for text in [
        "",
        "<uint:8>",
        "integer",
        "uint:",
        "uint:8,",
        "uint:8,,_16",
        "uint;8",
        "uint:8,16",
        "uint:_16,_16",
        "uint:8,m,s",
        "uint:32769",
        "uint:_1",
        "uint:_63",
        "uint:_10001",
        "sint:_64",
        "sint:_85",
        "bool:8",
        "bool:0",
        "utf8:m",
        "float:24",
        "float:_8",
        "float_fix:32,q32",
        "float_fix:_16",
        "float_dec:48",
        "uint:q8",
        "datetime:_16",
        "datetime:64,martian",
    ] {
        assert_eq!(
            annotation_error(text),
            Some(ErrorCode::IllegalValueType),
            "{text:?}"
        );
    }
    // ... at the boundaries the accepted neighbours stand on.
    for text in [
        "uint:32768",
        "uint:_2",
        "uint:_62",
        "uint:_64",
        "uint:_85",
        "float:96",
        "float_fix:32,q31",
        "float_dec:256",
        "sint:_62",
    ] {
        annotation(text);
    }
}

#[test]
fn a_bad_unit_names_which_flag_failed() {
    // Over 255 bytes: unit_too_long, at 255 the length passes (and the text
    // is then judged as a unit).
    let long = format!("uint:8,{}", "m".repeat(256));
    assert_eq!(annotation_error(&long), Some(ErrorCode::UnitTooLong));
    let at_cap = format!("uint:8,{}", "m".repeat(255));
    assert_eq!(annotation_error(&at_cap), Some(ErrorCode::UnitIllegal));
    // Present but not a unit, and the reader's finer codes for a profile.
    assert_eq!(
        annotation_error("uint:8,parsec_per_fortnight"),
        Some(ErrorCode::UnitIllegal)
    );
    assert_eq!(
        annotation_error("uint:8,nosuch:m"),
        Some(ErrorCode::UnitProfileUnknown)
    );
    // Braces nested past the scanner's bound keep no text, and are refused
    // all the same.
    let deep = format!("uint:8,{}", "{".repeat(17));
    assert_eq!(annotation_error(&deep), Some(ErrorCode::UnitIllegal));
}

#[test]
fn an_annotation_parses_as_a_document_reads_it() {
    // The value a reader reports for the same annotation.
    for text in [
        "uint:16,_16,k~m",
        "sint:8",
        "float:32,m/s",
        "float_fix:16,q4",
    ] {
        let a = annotation(text);
        let doc = bovnar::parse_typed(format!(".v = <{text}> 1;").as_bytes()).unwrap();
        let Value::Struct(entries) = doc else {
            panic!()
        };
        let Value::Quantity(q) = &entries[0].1 else {
            panic!("{entries:?}")
        };
        assert_eq!(q.vtype, a.value_type, "{text}");
        assert_eq!(q.unit, a.unit, "{text}");
    }
}

// ---------------------------------------------------------------------------
// Validators

#[test]
fn identifier_symbol_and_reference_boundaries() {
    for s in [
        "a",
        "_",
        "_a9",
        "Ab",
        "a-b",
        "a+b",
        "é",
        "aé",
        "\u{c0}x",
        "x\u{10ffff}",
    ] {
        assert!(literals::validate_identifier(s), "{s:?}");
        assert!(literals::validate_symbol(s), "{s:?}");
    }
    for s in [
        "", "1a", "-a", "a b", "a.b", "a,b", "a:b", "a=b", "a\u{a0}b", "\u{bf}", "a\u{7f}", "a\0b",
        "a\tb",
    ] {
        assert!(!literals::validate_identifier(s), "{s:?}");
        assert!(!literals::validate_symbol(s), "{s:?}");
    }
    for s in [".a", ".a.b", ".a[0]", ".a[0][12].b", ".é.x-y"] {
        assert!(literals::validate_reference(s), "{s:?}");
    }
    for s in [
        "", "a", ".", ".a.", "..a", ".1", ".a[]", ".a[x]", ".a[0]b", "&.a", ".a b",
    ] {
        assert!(!literals::validate_reference(s), "{s:?}");
    }
}

#[test]
fn the_codec_key_check_agrees_with_the_c_validator() {
    // bovnar::codec checks a mux key natively; the C validator is the rule.
    for key in [
        "mux", "_", "a-b", "é", "a b", "a.b", "1a", "", "a\u{a0}", "\u{c0}", "x~", "a+",
    ] {
        assert_eq!(
            bovnar::codec::MuxWriter::new(key).is_ok(),
            literals::validate_identifier(key),
            "{key:?}"
        );
    }
}

#[test]
fn number_validators_boundaries() {
    for s in ["0", "-1", "01", "1.", ".5", "1.5e3", "1E+5", "1e-5"] {
        assert!(literals::validate_number(s), "{s:?}");
    }
    for s in [
        "", "-", "+1", "1e", "e5", ".", "1.2.3", "1e5.0", "inf", "0x1", " 1",
    ] {
        assert!(!literals::validate_number(s), "{s:?}");
    }
    for s in ["inf", "ninf", "nan"] {
        assert!(literals::is_special_number_string(s));
    }
    for s in ["-inf", "+inf", "NaN", "infinity", "", "in"] {
        assert!(!literals::is_special_number_string(s), "{s:?}");
    }

    // Integer digits: '-' only, and no sign at all in bases 64 and 85.
    assert!(literals::validate_digits_for_base("ff", 16));
    assert!(literals::validate_digits_for_base("-FF", 16));
    assert!(!literals::validate_digits_for_base("+ff", 16));
    assert!(!literals::validate_digits_for_base("fg", 16));
    assert!(literals::validate_digits_for_base("zZ", 62));
    assert!(literals::validate_digits_for_base("+/", 64));
    assert!(literals::validate_digits_for_base("-", 85));
    assert!(!literals::validate_digits_for_base("2", 2));
    // The base is not checked against those a type may declare: base 1 has
    // the one digit 0, base 0 none.
    assert!(literals::validate_digits_for_base("0", 1));
    assert!(!literals::validate_digits_for_base("1", 1));
    assert!(!literals::validate_digits_for_base("0", 0));
    assert!(literals::validate_digits_for_base("zZ", 99));
    assert!(!literals::validate_digits_for_base("", 10));
    assert!(!literals::validate_digits_for_base("-", 10));

    // Float carriers take '+' and an exponent marker that is no digit.
    assert!(literals::validate_number_in_base("+ff", 16));
    assert!(literals::validate_number_in_base("1.8p3", 16));
    assert!(literals::validate_number_in_base("1.5e3", 10));
    assert!(literals::validate_number_in_base("1p4", 8));
    assert!(!literals::validate_number_in_base("1p4", 12));
    assert!(!literals::validate_number_in_base("1e", 10));
    assert!(!literals::validate_number_in_base(".", 10));
    // 'e' is the digit 14 from base 15 on.
    assert!(literals::validate_number_in_base("1e5", 15));
    assert_eq!(
        literals::validate_number_in_base("+5.", 85),
        literals::validate_digits_for_base("+5.", 85)
    );
}

#[test]
fn range_validators_boundaries() {
    assert!(literals::validate_uint_range("255", 8, 10));
    assert!(!literals::validate_uint_range("256", 8, 10));
    assert!(literals::validate_uint_range("ff", 8, 16));
    assert!(!literals::validate_uint_range("100", 8, 16));
    assert!(!literals::validate_uint_range("-0", 8, 10));
    assert!(literals::validate_uint_range("nan", 8, 10));
    assert!(literals::validate_uint_range(
        "99999999999999999999999",
        0,
        10
    ));
    let u128_max = u128::MAX.to_string();
    assert!(literals::validate_uint_range(&u128_max, 128, 10));
    assert!(!literals::validate_uint_range(&"1".repeat(40), 128, 10));
    assert!(!literals::validate_uint_range(
        "340282366920938463463374607431768211456",
        128,
        10
    ));
    // Not digits: refused, though strtoull reads "" and " 5" as numbers.
    for bad in ["", "+", " 5", "5 ", "1_0"] {
        assert!(!literals::validate_uint_range(bad, 8, 10), "{bad:?}");
        assert!(!literals::validate_uint_range(bad, 128, 10), "{bad:?}");
        assert!(!literals::validate_sint_range(bad, 8, 10), "{bad:?}");
    }
    assert!(!literals::validate_sint_range("-", 8, 10));

    assert!(literals::validate_sint_range("127", 8, 10));
    assert!(!literals::validate_sint_range("128", 8, 10));
    assert!(literals::validate_sint_range("-128", 8, 10));
    assert!(!literals::validate_sint_range("-129", 8, 10));
    assert!(literals::validate_sint_range("-80", 8, 16));
    assert!(!literals::validate_sint_range("-81", 8, 16));
    let i128_min = i128::MIN.to_string();
    assert!(literals::validate_sint_range(&i128_min, 128, 10));
    let below = (BigInt::from(i128::MIN) - BigInt::from(1)).to_string();
    assert!(!literals::validate_sint_range(&below, 128, 10));
}

#[test]
fn validate_string_is_utf8_well_formedness() {
    let cases: [&[u8]; 10] = [
        b"abc",
        b"\xc3\xa9",
        b"\xc3",
        b"\xed\xa0\x80",
        b"\xc0\xae",
        b"\xf4\x90\x80\x80",
        b"\x00\x01\x7f",
        b"\xe2\x82",
        b"\xf0\x9f\x98\x80",
        b"",
    ];
    for bytes in cases {
        assert_eq!(
            literals::validate_string(bytes),
            std::str::from_utf8(bytes).is_ok(),
            "{bytes:?}"
        );
    }
}

#[test]
fn char_to_digit_alphabets() {
    assert_eq!(literals::char_to_digit('a', 16), Some(10));
    assert_eq!(literals::char_to_digit('A', 16), Some(10));
    assert_eq!(literals::char_to_digit('A', 62), Some(36));
    assert_eq!(literals::char_to_digit('g', 16), None);
    assert_eq!(literals::char_to_digit('A', 64), Some(0));
    assert_eq!(literals::char_to_digit('+', 64), Some(62));
    assert_eq!(literals::char_to_digit('!', 85), Some(0));
    assert_eq!(literals::char_to_digit('u', 85), Some(84));
    assert_eq!(literals::char_to_digit('v', 85), None);
    assert_eq!(literals::char_to_digit('0', 1), Some(0));
    assert_eq!(literals::char_to_digit('0', 0), None);
    assert_eq!(literals::char_to_digit('é', 62), None);
}

#[test]
fn min_digits_for_type_answers_integer_widths() {
    assert_eq!(
        literals::min_digits_for_type(vt(ValueTypeFamily::Uint, 8, 16)),
        2
    );
    assert_eq!(
        literals::min_digits_for_type(vt(ValueTypeFamily::Uint, 8, 0)),
        3
    );
    assert_eq!(
        literals::min_digits_for_type(vt(ValueTypeFamily::Sint, 8, 2)),
        7
    );
    assert_eq!(
        literals::min_digits_for_type(vt(ValueTypeFamily::Uint, 64, 0)),
        20
    );
    assert_eq!(
        literals::min_digits_for_type(vt(ValueTypeFamily::Uint, 0, 0)),
        0
    );
    assert_eq!(
        literals::min_digits_for_type(vt(ValueTypeFamily::Uint, 128, 0)),
        0
    );
    assert_eq!(
        literals::min_digits_for_type(vt(ValueTypeFamily::Utf8, 0, 0)),
        0
    );
    // A float type is answered as an unsigned integer of its width.
    assert_eq!(
        literals::min_digits_for_type(vt(ValueTypeFamily::Float, 64, 0)),
        20
    );
}

// ---------------------------------------------------------------------------
// Formatters and parsers

#[test]
fn integer_formatters() {
    assert_eq!(literals::format_uint64(255, 16, 0).unwrap(), "ff");
    assert_eq!(literals::format_uint64(5, 2, 8).unwrap(), "00000101");
    assert_eq!(literals::format_uint64(0, 10, 0).unwrap(), "0");
    assert_eq!(
        literals::format_uint64(u64::MAX, 2, 0).unwrap(),
        "1".repeat(64)
    );
    assert_eq!(literals::format_uint64(61, 62, 0).unwrap(), "Z");
    assert_eq!(literals::format_uint64(62, 64, 0).unwrap(), "+");
    // min_digits is capped at 64.
    assert_eq!(literals::format_uint64(1, 10, 100).unwrap().len(), 64);
    for base in [0, 1, 63, 86] {
        assert!(literals::format_uint64(1, base, 0).is_err(), "{base}");
    }
    assert_eq!(literals::format_int64(-255, 16, 4).unwrap(), "-00ff");
    assert_eq!(
        literals::format_int64(i64::MIN, 10, 0).unwrap(),
        i64::MIN.to_string()
    );
    assert_eq!(literals::format_int64(5, 85, 0).unwrap(), "&");
    assert!(literals::format_int64(-5, 85, 0).is_err());
    assert!(literals::format_int64(-5, 64, 0).is_err());
}

#[test]
fn float_formatter() {
    let f64t = vt(ValueTypeFamily::Float, 64, 0);
    assert_eq!(literals::format_double(0.1, f64t).unwrap(), "1e-1");
    assert_eq!(literals::format_double(-0.0, f64t).unwrap(), "-0.0");
    assert_eq!(literals::format_double(f64::INFINITY, f64t).unwrap(), "inf");
    assert_eq!(literals::format_double(f64::NAN, f64t).unwrap(), "nan");
    assert_eq!(literals::format_double(5e-324, f64t).unwrap(), "5e-324");
    assert_eq!(literals::format_double(1e300, f64t).unwrap(), "1e+300");
    assert_eq!(
        literals::format_double(0.1, vt(ValueTypeFamily::Float, 64, 16)).unwrap(),
        "1.999999999999ap-4"
    );
    // Every finite double's text reads back to it.
    for x in [
        0.1,
        1.0 / 3.0,
        6.02214076e23,
        -2.5e-310,
        f64::MAX,
        f64::MIN_POSITIVE,
    ] {
        let text = literals::format_double(x, f64t).unwrap();
        assert_eq!(literals::parse_double(&text, f64t).unwrap(), x, "{text}");
    }
    // float:16 and float:32 round to IEEE binary16 and binary32 first, as
    // the C writer writes them, and refuse what the format cannot hold.
    let (h16, h32) = (
        vt(ValueTypeFamily::Float, 16, 0),
        vt(ValueTypeFamily::Float, 32, 0),
    );
    assert_eq!(literals::format_double(2049.0, h16).unwrap(), "2.048e+3");
    assert_eq!(
        literals::format_double(16777217.0, h32).unwrap(),
        "1.6777216e+7"
    );
    assert_eq!(literals::format_double(65504.0, h16).unwrap(), "6.5504e+4");
    assert_eq!(literals::format_double(f64::INFINITY, h16).unwrap(), "inf");
    assert!(literals::format_double(1e5, h16).is_err());
    assert!(literals::format_double(-1e39, h32).is_err());
    // Base 10 (or 0) and 16 only.
    assert_eq!(
        literals::format_double(1.5, vt(ValueTypeFamily::Float, 64, 0)).unwrap(),
        "1.5e+0"
    );
    for base in [2, 8, 36, 62] {
        assert!(literals::format_double(1.0, vt(ValueTypeFamily::Float, 64, base)).is_err());
    }
}

#[test]
fn integer_parsers_bound_by_the_type() {
    let u8t = vt(ValueTypeFamily::Uint, 8, 0);
    assert_eq!(literals::parse_uint64("255", u8t).unwrap(), 255);
    assert!(literals::parse_uint64("256", u8t).is_err());
    assert!(literals::parse_uint64("-1", u8t).is_err());
    assert!(literals::parse_uint64("+1", u8t).is_err());
    assert!(literals::parse_uint64(" 1", u8t).is_err());
    assert!(literals::parse_uint64("", u8t).is_err());
    assert!(literals::parse_uint64("1\0", u8t).is_err());
    let u64t = vt(ValueTypeFamily::Uint, 0, 0);
    assert_eq!(
        literals::parse_uint64(&u64::MAX.to_string(), u64t).unwrap(),
        u64::MAX
    );
    assert!(literals::parse_uint64("18446744073709551616", u64t).is_err());

    let s8 = vt(ValueTypeFamily::Sint, 8, 0);
    assert_eq!(literals::parse_int64("-128", s8).unwrap(), -128);
    assert!(literals::parse_int64("-129", s8).is_err());
    assert!(literals::parse_int64("128", s8).is_err());
    assert!(literals::parse_int64("-", s8).is_err());
    let s64 = vt(ValueTypeFamily::Sint, 64, 0);
    assert_eq!(
        literals::parse_int64(&i64::MIN.to_string(), s64).unwrap(),
        i64::MIN
    );

    // Bases past 36, and the Ascii85 digits '-' and '.'.
    assert_eq!(
        literals::parse_int64("-zZ", vt(ValueTypeFamily::Sint, 64, 62)).unwrap(),
        -(35 * 62 + 61)
    );
    assert_eq!(
        literals::parse_uint64("-.", vt(ValueTypeFamily::Uint, 64, 85)).unwrap(),
        12 * 85 + 13
    );
    assert_eq!(
        literals::parse_uint64("+", vt(ValueTypeFamily::Uint, 64, 64)).unwrap(),
        62
    );

    // strtoll's 0x prefix in base 16 is refused, as the reader refuses it.
    let h = vt(ValueTypeFamily::Uint, 64, 16);
    assert!(literals::parse_uint64("0x10", h).is_err());
    assert!(literals::parse_uint64("0X10", h).is_err());
    assert_eq!(literals::parse_uint64("10", h).unwrap(), 16);
    let sh = vt(ValueTypeFamily::Sint, 64, 16);
    assert!(literals::parse_int64("-0x10", sh).is_err());
    assert_eq!(literals::parse_int64("-10", sh).unwrap(), -16);
    assert!(bovnar::parse(b".a = <uint:64,_16> 0x10;").is_err());
}

#[test]
fn float_parsers() {
    let f = vt(ValueTypeFamily::Float, 64, 0);
    assert_eq!(literals::parse_double("1.5", f).unwrap(), 1.5);
    assert_eq!(literals::parse_double("+5", f).unwrap(), 5.0);
    assert_eq!(literals::parse_double("inf", f).unwrap(), f64::INFINITY);
    assert_eq!(
        literals::parse_double("ninf", f).unwrap(),
        f64::NEG_INFINITY
    );
    assert!(literals::parse_double("nan", f).unwrap().is_nan());
    for bad in ["-inf", "", "abc", "1x", "1e", "0x1p3"] {
        assert!(literals::parse_double(bad, f).is_err(), "{bad:?}");
    }
    // Correctly rounded: the largest subnormal, not the smallest normal.
    assert_eq!(
        literals::parse_double("2.2250738585072011e-308", f).unwrap(),
        f64::from_bits(0x000f_ffff_ffff_ffff)
    );
    assert_eq!(literals::parse_double_in_base("1.8p1", 16).unwrap(), 3.0);
    // 'e' is a hex digit.
    assert_eq!(literals::parse_double_in_base("1e3", 16).unwrap(), 483.0);
    assert!(literals::parse_double_in_base("1.5", 8).is_err());
    assert!(literals::parse_double_in_base("1.5", 2).is_err());
}

#[test]
fn the_escape_table() {
    let t = literals::escape_repl_table();
    let mut found: Vec<(u8, u8)> = (0..=255u8)
        .filter(|&i| t[i as usize] != 0)
        .map(|i| (i, t[i as usize]))
        .collect();
    found.sort();
    assert_eq!(
        found,
        vec![
            (b'"', b'"'),
            (b'\\', b'\\'),
            (b'f', 0x0c),
            (b'n', b'\n'),
            (b'r', b'\r'),
            (b't', b'\t'),
            (b'v', 0x0b),
        ]
    );
}

// ---------------------------------------------------------------------------
// unit_convert_factor's ok

#[test]
fn unit_conversion_reports_ok() {
    let u = |s| Unit::parse(s).unwrap();
    let c = unit_convert_factor(&u("k~m"), &u("m")).unwrap();
    assert!(c.ok() && !c.requires_affine);
    // Offsets that do not cancel: no factor to multiply by.
    let c = unit_convert_factor(&u("°C"), &u("K")).unwrap();
    assert!(!c.ok() && c.requires_affine);
    assert_eq!(c.factor, 0.0);
    assert_eq!(convert_value(0.0, &u("°C"), &u("K")).unwrap(), 273.15);
    // An affine unit to itself under another prefix shares its offset.
    let c = unit_convert_factor(&u("°C"), &u("m~°C")).unwrap();
    assert!(c.ok() && c.requires_affine);
    assert_eq!(c.factor, 1000.0);
    assert!(unit_convert_factor(&u("m"), &u("s")).is_err());
}

// ---------------------------------------------------------------------------
// The DOM builder

fn fixture() -> Result<DomDoc, Error> {
    let mut server = NewStruct::new()?;
    server.add("port", NewNode::from_u16(8080)?)?;
    server.add("offset", NewNode::from_i8(-5)?)?;
    server.add("tls", NewNode::null()?)?;
    let mut ports = NewArray::new()?;
    for p in [80u32, 443] {
        ports.push(NewNode::from_u32(p)?)?;
    }
    ports.push(NewStruct::new()?)?;
    server.add("ports", ports)?;
    let mut doc = DomDoc::create()?;
    doc.add("server", server)?;
    doc.add(
        "big",
        NewNode::from_bigint(&(BigInt::from(1) << 100), 128, Signedness::Auto, None)?,
    )?;
    Ok(doc)
}

#[test]
fn a_built_tree_reads_back_through_the_getters() {
    let doc = fixture().unwrap();
    assert_eq!(doc.len(), 2);
    let port = doc.lookup("server.port").unwrap();
    assert_eq!(port.dom_type(), Some(DomType::Int));
    assert_eq!(port.as_u16(), Some(8080));
    assert_eq!(port.value_type(), vt(ValueTypeFamily::Uint, 16, 0));
    assert_eq!(port.unit_str(), "");
    assert_eq!(doc.lookup("server.offset").unwrap().as_i64(), Some(-5));
    assert_eq!(
        doc.lookup("server.offset").unwrap().value_type(),
        vt(ValueTypeFamily::Sint, 8, 0)
    );
    assert!(doc.lookup("server.tls").unwrap().is_null());
    assert_eq!(doc.lookup("server.ports[1]").unwrap().as_u32(), Some(443));
    assert_eq!(doc.lookup("server.ports").unwrap().len(), 3);
    assert_eq!(doc.lookup("server.ports").unwrap().array_dims(), 0);
    let big = doc.get("big").unwrap();
    assert_eq!(big.as_int_str(16).unwrap(), format!("1{}", "0".repeat(25)));
    assert_eq!(big.as_u64(), None);
    assert_eq!(
        doc.to_value().unwrap(),
        Value::Struct(vec![
            (
                "server".into(),
                Value::Struct(vec![
                    ("port".into(), Value::Int(8080)),
                    ("offset".into(), Value::Int(-5)),
                    ("tls".into(), Value::Null),
                    (
                        "ports".into(),
                        Value::Array(vec![Value::Int(80), Value::Int(443), Value::Struct(vec![])])
                    ),
                ])
            ),
            ("big".into(), Value::Int(1i128 << 100)),
        ])
    );
}

#[test]
fn every_narrow_constructor_keeps_its_type() {
    let mut doc = DomDoc::create().unwrap();
    let cases: Vec<(&str, NewNode, ValueTypeFamily, u32)> = vec![
        (
            "i8",
            NewNode::from_i8(i8::MIN).unwrap(),
            ValueTypeFamily::Sint,
            8,
        ),
        (
            "u8",
            NewNode::from_u8(u8::MAX).unwrap(),
            ValueTypeFamily::Uint,
            8,
        ),
        (
            "i16",
            NewNode::from_i16(i16::MIN).unwrap(),
            ValueTypeFamily::Sint,
            16,
        ),
        (
            "u16",
            NewNode::from_u16(u16::MAX).unwrap(),
            ValueTypeFamily::Uint,
            16,
        ),
        (
            "i32",
            NewNode::from_i32(i32::MIN).unwrap(),
            ValueTypeFamily::Sint,
            32,
        ),
        (
            "u32",
            NewNode::from_u32(u32::MAX).unwrap(),
            ValueTypeFamily::Uint,
            32,
        ),
        (
            "i64",
            NewNode::from_i64(i64::MIN).unwrap(),
            ValueTypeFamily::Sint,
            64,
        ),
        (
            "u64",
            NewNode::from_u64(u64::MAX).unwrap(),
            ValueTypeFamily::Uint,
            64,
        ),
    ];
    for (key, node, family, width) in cases {
        assert_eq!(node.dom_type(), Some(DomType::Int));
        doc.add(key, node).unwrap();
        assert_eq!(
            doc.get(key).unwrap().value_type(),
            vt(family, width, 0),
            "{key}"
        );
    }
    assert_eq!(doc.get("i8").unwrap().as_i8(), Some(i8::MIN));
    assert_eq!(doc.get("u64").unwrap().as_u64(), Some(u64::MAX));
    assert_eq!(doc.get("u64").unwrap().as_i64(), None);
    assert_eq!(doc.get("i64").unwrap().as_i64(), Some(i64::MIN));
}

#[test]
fn from_bigint_bounds_and_units() {
    let big = |e: u32| BigInt::from(1) << e;
    // uint:128 holds 2^128 - 1, not 2^128.
    assert!(NewNode::from_bigint(&(big(128) - 1), 128, Signedness::Unsigned, None).is_ok());
    assert!(NewNode::from_bigint(&big(128), 128, Signedness::Unsigned, None).is_err());
    assert!(NewNode::from_bigint(&BigInt::from(-1), 128, Signedness::Unsigned, None).is_err());
    // sint:128 runs from -2^127 to 2^127 - 1.
    assert!(NewNode::from_bigint(&-big(127), 128, Signedness::Signed, None).is_ok());
    assert!(NewNode::from_bigint(&(-big(127) - 1), 128, Signedness::Signed, None).is_err());
    assert!(NewNode::from_bigint(&big(127), 128, Signedness::Signed, None).is_err());
    assert!(NewNode::from_bigint(&(big(127) - 1), 128, Signedness::Signed, None).is_ok());
    // The width: 65 to 32768.
    assert!(NewNode::from_bigint(&BigInt::from(1), 64, Signedness::Auto, None).is_err());
    assert!(NewNode::from_bigint(&BigInt::from(1), 65, Signedness::Auto, None).is_ok());
    assert!(NewNode::from_bigint(&(big(32768) - 1), 32768, Signedness::Auto, None).is_ok());
    assert!(NewNode::from_bigint(&BigInt::from(1), 32769, Signedness::Auto, None).is_err());

    let km = Unit::parse("k~m").unwrap();
    let mut doc = DomDoc::create().unwrap();
    doc.add(
        "d",
        NewNode::from_bigint(&-big(100), 256, Signedness::Auto, Some(&km)).unwrap(),
    )
    .unwrap();
    let d = doc.get("d").unwrap();
    assert_eq!(d.value_type(), vt(ValueTypeFamily::Sint, 256, 0));
    assert_eq!(d.unit_str(), "k~m");
    assert_eq!(d.unit_source(), None);
    assert_eq!(d.as_int_str(10).unwrap(), (-big(100)).to_string());
    assert_eq!(d.value_in_base_units(), -(2f64.powi(100)) * 1000.0);
}

#[test]
fn refused_adds_are_errors_and_free_the_node() {
    let mut doc = DomDoc::create().unwrap();
    doc.add("a", NewNode::from_i32(1).unwrap()).unwrap();
    let err = doc.add("a", NewNode::from_i32(2).unwrap()).unwrap_err();
    assert_eq!(err.code(), Some(ErrorCode::DuplicateStructKey));
    assert_eq!(doc.get("a").unwrap().as_i32(), Some(1));
    for key in ["", "1a", "a.b", "a[0]", "a b", "a\0b"] {
        assert!(
            matches!(
                doc.add(key, NewNode::null().unwrap()),
                Err(Error::Argument { .. })
            ),
            "{key:?}"
        );
    }
    let mut s = NewStruct::new().unwrap();
    s.add("x", NewNode::null().unwrap()).unwrap();
    assert_eq!(
        s.add("x", NewNode::null().unwrap()).unwrap_err().code(),
        Some(ErrorCode::DuplicateStructKey)
    );
    assert!(s.add("", NewNode::null().unwrap()).is_err());
    assert_eq!(s.len(), 1);
    doc.add("s", s).unwrap();
    assert_eq!(doc.len(), 2);
}

#[test]
fn a_parsed_document_takes_built_nodes_too() {
    let mut doc = DomDoc::parse(b".a = 1;").unwrap();
    doc.add("b", NewNode::from_u8(2).unwrap()).unwrap();
    assert_eq!(
        doc.add("a", NewNode::null().unwrap()).unwrap_err().code(),
        Some(ErrorCode::DuplicateStructKey)
    );
    assert_eq!(
        doc.to_value().unwrap(),
        Value::Struct(vec![
            ("a".into(), Value::Int(1)),
            ("b".into(), Value::Int(2))
        ])
    );
}

#[test]
fn unattached_nodes_are_dropped_cleanly() {
    // Dropped before they are attached, with children, and many times over:
    // each frees its own subtree once (run under valgrind or ASan for the
    // leak check; here it must at least not crash).
    for _ in 0..1000 {
        let mut s = NewStruct::new().unwrap();
        let mut a = NewArray::new().unwrap();
        a.push(NewNode::from_u64(1).unwrap()).unwrap();
        a.push(
            NewNode::from_bigint(&(BigInt::from(1) << 200), 256, Signedness::Auto, None).unwrap(),
        )
        .unwrap();
        s.add("a", a).unwrap();
        drop(s);
        drop(NewNode::null().unwrap());
        drop(NewNode::from_bigint(&BigInt::from(-7), 100, Signedness::Auto, None).unwrap());
    }
    // Nodes and documents move between threads.
    let node = NewNode::from_i64(5).unwrap();
    let doc = std::thread::spawn(move || {
        let mut doc = DomDoc::create().unwrap();
        doc.add("n", node).unwrap();
        doc
    })
    .join()
    .unwrap();
    assert_eq!(doc.get("n").unwrap().as_i64(), Some(5));
}

#[test]
fn an_empty_built_document() {
    let doc = DomDoc::create().unwrap();
    assert!(doc.is_empty());
    assert_eq!(doc.to_value().unwrap(), Value::Struct(vec![]));
    assert!(doc.lookup("a").is_none());
}
