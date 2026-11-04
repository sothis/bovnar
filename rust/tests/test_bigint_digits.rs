// SPDX-License-Identifier: MIT
//! Integers far wider than any machine integer, up to the format's 32768
//! bits.
//!
//! An integer of thousands of digits is written here as a `Value::Quantity`:
//! its exact decimal literal under a `uint`/`sint` annotation as wide as the
//! number, built from a `BigInt`, and read back exactly through
//! `parse_typed` (`Quantity::raw` parsed as a `BigInt`, and
//! `Quantity::decimal`) or through `DomNode::as_int_str`. Untyped `parse`
//! gives such an integer as a `Value::BigInt`.

use bovnar::{
    BaseUnit, Decimal, Error, Event, EventData, Exponent, Quantity, ReadOptions, Reader, SIPrefix,
    Unit, Value, ValueTypeFamily, ValueTypeSpec, Writer, parse_typed, to_string, write_array,
};
use num_bigint::BigInt;
use num_traits::{One, Pow};

/// The wide integers under test: 10**4300 + 12345, -(10**9000) + 1, 2**32767 - 1.
fn big() -> [BigInt; 3] {
    let ten = BigInt::from(10);
    [
        Pow::pow(&ten, 4300u32) + 12345,
        -Pow::pow(&ten, 9000u32) + 1,
        (BigInt::one() << 32767) - 1,
    ]
}

/// The type `n` is written under: a `uint`/`sint` exactly as
/// wide as the number (`write_bvni(key, n, width=n.bit_length())`, one more
/// bit for a negative number).
fn wide_type(n: &BigInt) -> ValueTypeSpec {
    let (family, width) = if n.sign() == num_bigint::Sign::Minus {
        (ValueTypeFamily::Sint, n.bits() + 1)
    } else {
        (ValueTypeFamily::Uint, n.bits())
    };
    ValueTypeSpec {
        family: family as i32,
        width: u32::try_from(width).unwrap(),
        base: 0,
    }
}

/// `n` as a typed scalar of type `vt`.
fn int_of(n: &BigInt, vt: ValueTypeSpec) -> Value {
    Value::Quantity(Box::new(Quantity::new(&n.to_string(), vt, Unit::default())))
}

/// `n` under its own exact width.
fn wide(n: &BigInt) -> Value {
    int_of(n, wide_type(n))
}

/// The fields of a typed parse of `doc`.
fn typed_fields(doc: &[u8]) -> Vec<(String, Value)> {
    let Value::Struct(fields) = parse_typed(doc).unwrap() else {
        panic!("not a struct");
    };
    fields
}

/// The exact integer a typed scalar holds.
fn typed_int(v: &Value) -> BigInt {
    let Value::Quantity(q) = v else {
        panic!("not a quantity: {v:?}");
    };
    q.raw.as_deref().unwrap().parse().unwrap()
}

#[test]
fn test_loads_dumps_round_trip() {
    for n in big() {
        let doc = to_string(&Value::Struct(vec![("a".into(), wide(&n))]), true).unwrap();
        let back = &typed_fields(&doc)[0].1;
        assert_eq!(typed_int(back), n);
        let Value::Quantity(q) = back else {
            unreachable!()
        };
        assert_eq!(q.decimal().unwrap(), Some(Decimal::from(n.clone())));
    }
}

#[test]
fn test_array_round_trip() {
    for n in big() {
        let doc = to_string(
            &Value::Struct(vec![(
                "a".into(),
                // The whole array is annotated with the widest element's
                // type, so both elements carry it.
                Value::Array(vec![wide(&n), int_of(&BigInt::one(), wide_type(&n))]),
            )]),
            true,
        )
        .unwrap();
        let Value::Array(back) = &typed_fields(&doc)[0].1 else {
            panic!("not an array");
        };
        let back: Vec<BigInt> = back.iter().map(typed_int).collect();
        assert_eq!(back, [n, BigInt::one()]);
    }
}

/// A wide literal written by hand keeps every digit through the typed parse.
#[test]
fn test_loads_a_hand_written_wide_literal() {
    let digits = "9".repeat(5000);
    let doc = format!(".a = <uint:32768> {digits};");
    let Value::Struct(fields) = parse_typed(doc.as_bytes()).unwrap() else {
        panic!("not a struct");
    };
    let Value::Quantity(q) = &fields[0].1 else {
        panic!("not a quantity: {:?}", fields[0].1);
    };
    assert_eq!(q.raw.as_deref(), Some(digits.as_str()));
}

/// km -> m multiplies 5000 nines by 1000: the exact rational and the
/// converted literal carry every digit.
#[test]
fn test_converted_rational_of_a_wide_result() {
    let nines = "9".repeat(5000);
    let doc = format!(".d = <uint:32768,k~m> {nines};");
    let want_digits = format!("{nines}000");
    let mut seen = Vec::new();
    let mut want = |_: &EventData<'_>| {
        Some((
            Unit::si(BaseUnit::Meter, SIPrefix::None, Exponent::LINEAR),
            10,
        ))
    };
    let mut on_event = |d: &EventData<'_>| {
        if d.event == Event::Data && d.converted {
            seen.push((
                d.converted_rational(),
                String::from_utf8(d.converted_text.to_vec()).unwrap(),
            ));
        }
        true
    };
    Reader::new()
        .unwrap()
        .read_mem_want_unit(
            doc.as_bytes(),
            &ReadOptions {
                max_conversion_length: 20000,
                ..ReadOptions::default()
            },
            &mut want,
            Some(&mut on_event),
        )
        .unwrap();
    let (rational, text) = &seen[0];
    assert_eq!(rational, &Some((want_digits.clone(), "1".into())));
    assert_eq!(text, &want_digits);
}

#[test]
fn test_quantity_from_number() {
    let from_number = |n: &BigInt, width| {
        Quantity::from_number(
            n.clone(),
            ValueTypeFamily::FloatDec,
            width,
            None,
            Unit::default(),
        )
    };
    for n in big() {
        let q = from_number(&n, 256).unwrap();
        assert_eq!(q.decimal().unwrap(), Some(Decimal::from(n.clone())));
        let doc = Value::Struct(vec![("q".into(), Value::Quantity(Box::new(q)))]);
        let Value::Struct(fields) = parse_typed(&to_string(&doc, true).unwrap()).unwrap() else {
            panic!("not a struct");
        };
        let Value::Quantity(back) = &fields[0].1 else {
            panic!("not a quantity: {:?}", fields[0].1);
        };
        assert_eq!(back.decimal().unwrap(), Some(Decimal::from(n.clone())));
        // decimal64 cannot hold it: a range refusal
        assert!(matches!(from_number(&n, 64), Err(Error::Argument { .. })));
    }
}

#[test]
fn test_dom_to_native() {
    for n in big() {
        let doc = to_string(
            &Value::Struct(vec![
                ("a".into(), wide(&n)),
                ("s".into(), Value::Struct(vec![("b".into(), wide(&n))])),
            ]),
            true,
        )
        .unwrap();
        let dom = bovnar::DomDoc::parse(&doc).unwrap();
        let digits = n.to_string();
        assert_eq!(dom.get("a").unwrap().as_int_str(10), Some(digits.clone()));
        assert_eq!(dom.lookup("s.b").unwrap().as_int_str(10), Some(digits));
    }
}

#[test]
fn test_write_array_under_a_wide_annotation() {
    let n: BigInt = Pow::pow(&BigInt::from(10), 9000u32);
    let vt = ValueTypeSpec {
        family: ValueTypeFamily::Uint as i32,
        width: 32768,
        base: 0,
    };
    let mut w = Writer::to_mem(bovnar::writer::DEFAULT_MEM_CAP, false).unwrap();
    write_array(
        &mut w,
        "a",
        &[int_of(&n, vt), Value::Int(2)],
        Some(vt),
        None,
        None,
    )
    .unwrap();
    w.finish().unwrap();
    let Value::Array(back) = &typed_fields(w.get_output())[0].1 else {
        panic!("not an array");
    };
    let back: Vec<BigInt> = back.iter().map(typed_int).collect();
    assert_eq!(back, [n, BigInt::from(2)]);
}

/// Integers past `i128` are `Value::BigInt`, not their digits as a string,
/// and `to_string` writes them back as integers of a width that holds them.
#[test]
fn test_wide_integers_are_bigints_and_round_trip() {
    use bovnar::parse;
    let u256_max: BigInt = (BigInt::one() << 256u32) - 1;
    let s256_min: BigInt = -(BigInt::one() << 255u32);
    let cases: Vec<(String, Value)> = vec![
        (
            "<uint:128> 340282366920938463463374607431768211455".into(),
            Value::BigInt(BigInt::from(u128::MAX)),
        ),
        (
            format!("<uint:256,_16> \"{}\"", "f".repeat(64)),
            Value::BigInt(u256_max.clone()),
        ),
        (
            format!("<sint:256> {s256_min}"),
            Value::BigInt(s256_min.clone()),
        ),
        (
            "<sint:128> -170141183460469231731687303715884105728".into(),
            Value::Int(i128::MIN),
        ),
    ];
    for (lit, want) in cases {
        let src = format!(".a = {lit};");
        let doc = parse(src.as_bytes()).unwrap_or_else(|e| panic!("{src}: {e}"));
        assert_eq!(
            doc,
            Value::Struct(vec![("a".into(), want.clone())]),
            "{src}"
        );
        let out = to_string(&doc, false).unwrap();
        assert_eq!(
            parse(&out).unwrap(),
            doc,
            "{}",
            String::from_utf8_lossy(&out)
        );
    }
    // The width is the smallest power of two from 128; a negative power of
    // two needs no extra sign bit.
    let out = to_string(
        &Value::Struct(vec![("a".into(), Value::BigInt(s256_min))]),
        false,
    )
    .unwrap();
    assert!(String::from_utf8_lossy(&out).contains("<sint:256>"));
    let out = to_string(
        &Value::Struct(vec![("a".into(), Value::BigInt(u256_max + 1))]),
        false,
    )
    .unwrap();
    assert!(String::from_utf8_lossy(&out).contains("<uint:512>"));
}

/// `i128::MIN` reads back: its magnitude has no positive `i128`.
#[test]
fn test_i128_min_round_trips() {
    use bovnar::parse;
    let doc = Value::Struct(vec![("a".into(), Value::Int(i128::MIN))]);
    let out = to_string(&doc, false).unwrap();
    assert_eq!(
        parse(&out).unwrap(),
        doc,
        "{}",
        String::from_utf8_lossy(&out)
    );
}

/// The DOM gives the same values as `parse`.
#[test]
fn test_dom_wide_integers_are_bigints() {
    use bovnar::DomDoc;
    let src = format!(".a = <uint:256,_16> \"{}\";", "f".repeat(64));
    let dom = DomDoc::parse(src.as_bytes()).unwrap().to_value().unwrap();
    assert_eq!(dom, bovnar::parse(src.as_bytes()).unwrap());
    assert!(matches!(&dom, Value::Struct(e) if matches!(e[0].1, Value::BigInt(_))));
}
