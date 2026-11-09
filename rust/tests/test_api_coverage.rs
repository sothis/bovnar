// SPDX-License-Identifier: MIT
//! C API reachable from the crate: the read options every reading entry
//! point takes, the `on_error` handler, the DOM's size cap, the demux's
//! reader counters, and the helpers the crate had no binding for.

use bovnar::live::{Demux, DemuxItem, DemuxOptions};
use bovnar::reader::ErrorHandler;
use bovnar::{DomDoc, Error, ErrorCode, ReadHandlers, ReadOptions, Reader, Unit, Value, stream};
use num_bigint::BigInt;
use num_rational::BigRational;

fn code<T>(r: Result<T, Error>) -> Option<ErrorCode> {
    match r {
        Ok(_) => panic!("the read was accepted"),
        Err(e) => e.code(),
    }
}

fn read(doc: &[u8], options: &ReadOptions) -> Result<(), Error> {
    Reader::new()?.read_mem(doc, options, None, None)
}

#[test]
fn every_length_cap_reaches_the_reader() {
    let cases: [(&[u8], ReadOptions, ErrorCode); 5] = [
        (
            b".abc = 1;",
            ReadOptions {
                max_identifier_length: 2,
                ..Default::default()
            },
            ErrorCode::IdentifierTooLong,
        ),
        (
            b".a = \"abcdef\";",
            ReadOptions {
                max_string_length: 3,
                ..Default::default()
            },
            ErrorCode::StringTooLong,
        ),
        (
            b".a = 12345;",
            ReadOptions {
                max_number_length: 3,
                ..Default::default()
            },
            ErrorCode::NumberTooLong,
        ),
        (
            b".a = abcdef;",
            ReadOptions {
                max_symbol_length: 3,
                ..Default::default()
            },
            ErrorCode::SymbolTooLong,
        ),
        (
            b".a = &.abcdef;",
            ReadOptions {
                max_reference_length: 3,
                ..Default::default()
            },
            ErrorCode::ReferenceTooLong,
        ),
    ];
    for (doc, options, want) in cases {
        read(doc, &ReadOptions::default()).unwrap();
        assert_eq!(code(read(doc, &options)), Some(want), "{doc:?}");
        // A cap the document fits is no refusal.
        let fits = ReadOptions {
            max_identifier_length: options.max_identifier_length.max(6) * 2,
            max_string_length: options.max_string_length.max(6) * 2,
            max_number_length: options.max_number_length.max(6) * 2,
            max_symbol_length: options.max_symbol_length.max(6) * 2,
            max_reference_length: options.max_reference_length.max(6) * 2,
            ..options
        };
        read(doc, &fits).unwrap();
    }
}

fn nested_structs(depth: usize) -> Vec<u8> {
    let mut doc = String::new();
    for i in 0..depth {
        doc.push_str(&format!(".s{i} = {{"));
    }
    doc.push_str(".x = 1;");
    for _ in 0..depth {
        doc.push_str("};");
    }
    doc.into_bytes()
}

#[test]
fn nesting_caps_default_to_255_and_lower() {
    // 0 is the format's 255 here, not the C reader's 64.
    read(&nested_structs(100), &ReadOptions::default()).unwrap();
    let low = ReadOptions {
        max_struct_nesting: 3,
        ..Default::default()
    };
    read(&nested_structs(3), &low).unwrap();
    assert_eq!(
        code(read(&nested_structs(4), &low)),
        Some(ErrorCode::StructNestingTooHigh)
    );
    let arrays = ReadOptions {
        max_array_nesting: 1,
        ..Default::default()
    };
    read(b".a = [1, 2];", &arrays).unwrap();
    assert_eq!(
        code(read(b".a = [[1], [2]];", &arrays)),
        Some(ErrorCode::ArrayNestingTooHigh)
    );
}

#[test]
fn on_error_sees_each_recovered_error() {
    let doc = b".a = 1; .b = @; .c = 2; .d = \"x; .e = 3;";
    let mut errors = Vec::new();
    let mut on_error = |e: &Error| errors.push((e.code(), e.position().unwrap()));
    let mut keys = Vec::new();
    let mut on_verified = |d: &bovnar::EventData<'_>| {
        if d.event == bovnar::Event::AssignmentStart {
            keys.push(d.text());
        }
        true
    };
    let options = ReadOptions {
        continue_on_error: true,
        ..Default::default()
    };
    let mut r = Reader::new().unwrap();
    let result = r.read_mem_with(
        doc,
        &options,
        ReadHandlers {
            on_verified: Some(&mut on_verified),
            on_error: Some(&mut on_error as ErrorHandler<'_>),
            ..Default::default()
        },
    );
    // Recovery is reported through on_error only: the read itself succeeds
    // or ends on the last error, and the counter agrees with the handler.
    let recovered = r.recovery_count();
    assert!(recovered >= 1, "{result:?}");
    assert!(errors.len() as u64 >= recovered, "{errors:?}");
    let (_, first) = errors[0];
    assert_eq!(first.line, 1);
    assert_eq!(
        (first.offset, first.byte),
        (13, u32::from(b'@')),
        "the '@' with nothing after it: {errors:?}"
    );
    assert!(keys.contains(&"c".to_string()), "{keys:?}");
}

#[test]
fn on_error_sees_the_error_that_ends_a_read() {
    let mut seen = Vec::new();
    let mut on_error = |e: &Error| seen.push(e.clone());
    let mut r = Reader::new().unwrap();
    let err = r
        .read_mem_with(
            b".a = @;",
            &ReadOptions::default(),
            ReadHandlers {
                on_error: Some(&mut on_error),
                ..Default::default()
            },
        )
        .unwrap_err();
    assert_eq!(seen, vec![err.clone()]);
    assert_eq!(r.error_position(), err.position().unwrap());
}

#[test]
fn a_panic_in_on_error_reaches_the_caller() {
    let caught = std::panic::catch_unwind(|| {
        let mut on_error = |_: &Error| panic!("from on_error");
        let options = ReadOptions {
            continue_on_error: true,
            ..Default::default()
        };
        let mut on_verified = |_: &bovnar::EventData<'_>| true;
        let _ = Reader::new().unwrap().read_mem_with(
            b".a = @; .b = 1; .c = 2;",
            &options,
            ReadHandlers {
                on_verified: Some(&mut on_verified),
                on_error: Some(&mut on_error),
                ..Default::default()
            },
        );
    });
    assert!(caught.is_err());
}

#[test]
fn want_unit_beside_on_unverified() {
    let mut raw = 0;
    let mut on_unverified = |_: &bovnar::EventData<'_>| {
        raw += 1;
        true
    };
    let mut converted = None;
    let mut on_verified = |d: &bovnar::EventData<'_>| {
        if d.event == bovnar::Event::Data {
            converted = Some(String::from_utf8_lossy(d.converted_text).into_owned());
        }
        true
    };
    let mut want = |_: &bovnar::EventData<'_>| Some((Unit::parse("m").unwrap(), 0));
    Reader::new()
        .unwrap()
        .read_mem_with(
            b".d = <uint:64,k~m> 3;",
            &ReadOptions::default(),
            ReadHandlers {
                on_verified: Some(&mut on_verified),
                on_unverified: Some(&mut on_unverified),
                want_unit: Some(&mut want),
                on_error: None,
            },
        )
        .unwrap();
    assert!(raw > 0);
    assert_eq!(converted.as_deref(), Some("3000"));
}

#[test]
fn parse_takes_read_options() {
    let doc = b"#!bovnar 9.0\n.a = 1;";
    assert!(bovnar::parse(doc).is_ok());
    let strict = ReadOptions {
        strict_version: true,
        ..Default::default()
    };
    assert_eq!(
        code(bovnar::parse_with_options(doc, false, &strict)),
        Some(ErrorCode::UnsupportedSpecVersion)
    );
    assert_eq!(
        code(bovnar::parse_reader_with_options(&doc[..], true, &strict)),
        Some(ErrorCode::UnsupportedSpecVersion)
    );
    let short = ReadOptions {
        max_string_length: 2,
        ..Default::default()
    };
    assert_eq!(
        code(bovnar::parse_with_options(b".s = \"abc\";", true, &short)),
        Some(ErrorCode::StringTooLong)
    );
}

#[test]
fn stream_functions_take_read_options() {
    let short = ReadOptions {
        max_string_length: 2,
        ..Default::default()
    };
    let docs = stream::frame_documents(&[&b".s = \"abc\";"[..]]).unwrap();
    assert!(stream::load_documents(&docs, false, false, 0).is_ok());
    assert_eq!(
        code(stream::load_documents_with_options(
            &docs, false, false, 0, &short
        )),
        Some(ErrorCode::StringTooLong)
    );
    assert_eq!(
        code(stream::load_documents_reader_with_options(
            &docs[..],
            false,
            false,
            0,
            &short
        )),
        Some(ErrorCode::StringTooLong)
    );

    let text_only = ReadOptions {
        text_only: true,
        ..Default::default()
    };
    let mux = stream::mux_dump(&[(1u64, b"hi".to_vec())], "mux").unwrap();
    assert!(stream::mux_load(&mux, 0, None).is_ok());
    assert_eq!(
        code(stream::mux_load_with_options(&mux, 0, None, &text_only)),
        Some(ErrorCode::OctetStreamForbidden)
    );
    assert_eq!(
        code(stream::mux_load_reader_with_options(
            &mux[..],
            0,
            None,
            &text_only
        )),
        Some(ErrorCode::OctetStreamForbidden)
    );

    let inner = b".abc = 1;";
    let key = ReadOptions {
        max_identifier_length: 2,
        ..Default::default()
    };
    assert!(stream::parse_embedded(inner, false).is_ok());
    assert_eq!(
        code(stream::parse_embedded_with_options(inner, false, &key)),
        Some(ErrorCode::IdentifierTooLong)
    );
    assert_eq!(
        code(stream::parse_embedded_reader_with_options(
            &inner[..],
            false,
            &key
        )),
        Some(ErrorCode::IdentifierTooLong)
    );
}

#[test]
fn parse_embedded_reports_the_error_that_ended_it() {
    // Under continue_on_error the first error is recovered from; the one
    // reported is the last, which ended the parse.
    let options = ReadOptions {
        continue_on_error: true,
        ..Default::default()
    };
    let err =
        stream::parse_embedded_with_options(b".a = @; .b = \"x", false, &options).unwrap_err();
    assert_eq!(
        err.code(),
        Some(ErrorCode::GotIncompleteBvnrStream),
        "{err:?}"
    );
}

#[test]
fn demux_reports_its_readers_counters() {
    let mut doc = b"#!bovnar 1.1\n".to_vec();
    doc.extend(stream::mux_dump(&[(3u64, b"payload".to_vec())], "mux").unwrap());
    let mut demux = Demux::new(&DemuxOptions::default()).unwrap();
    assert_eq!(demux.declared_version(), None);
    let mut input = &doc[..];
    let mut messages = 0;
    while let Some(item) = demux.next(&mut input).unwrap() {
        if let DemuxItem::Message { .. } = item {
            messages += 1;
        }
    }
    demux.finish().unwrap();
    assert_eq!(messages, 1);
    assert_eq!(demux.declared_version(), Some((1, 1)));
    assert_eq!((demux.recovery_count(), demux.skipped_bytes()), (0, 0));
}

#[test]
fn dom_parse_reader_takes_a_lower_cap() {
    let doc = b".a = 1; .b = 2;";
    assert!(DomDoc::parse_reader_capped(&doc[..], doc.len() as u64).is_ok());
    assert!(DomDoc::parse_reader_capped(&doc[..], 0).is_ok());
    assert_eq!(
        code(DomDoc::parse_reader_capped(&doc[..], 5)),
        Some(ErrorCode::FileTooLong)
    );
    let policy = bovnar::UnitPolicy::default();
    assert_eq!(
        code(DomDoc::parse_reader_capped_with_policy(
            &doc[..],
            5,
            &policy
        )),
        Some(ErrorCode::FileTooLong)
    );
    // Above the built-in cap is the built-in cap, as in C.
    assert!(DomDoc::parse_reader_capped(&doc[..], u64::MAX).is_ok());
}

fn ratio(n: i64, d: i64) -> BigRational {
    BigRational::new(BigInt::from(n), BigInt::from(d))
}

#[test]
fn convert_rational_is_exact() {
    use bovnar::units::convert_rational;
    let u = |t| Unit::parse(t).unwrap();
    assert_eq!(
        convert_rational(&ratio(3, 1), &u("k~m"), &u("m")).unwrap(),
        (ratio(3000, 1), true)
    );
    // 42 km/h is 35/3 m/s: exact as a rational, endless as a decimal.
    assert_eq!(
        convert_rational(&ratio(42, 1), &u("k~m/h"), &u("m/s")).unwrap(),
        (ratio(35, 3), true)
    );
    let (_, exact) = convert_rational(&ratio(180, 1), &u("°"), &u("rad")).unwrap();
    assert!(!exact, "a π-based factor is not exact");
    assert!(convert_rational(&ratio(1, 1), &u("m"), &u("s")).is_err());
    // A value far past any machine integer keeps every digit.
    let big = BigRational::from_integer(BigInt::from(10).pow(400) + 1);
    let (got, exact) = convert_rational(&big, &u("k~m"), &u("m")).unwrap();
    assert!(exact);
    assert_eq!(got, big * BigInt::from(1000));
    // Negative, and near the 32768-bit capacity (~9860 decimal digits).
    let (got, _) = convert_rational(&ratio(-7, 2), &u("k~m"), &u("m")).unwrap();
    assert_eq!(got, ratio(-3500, 1));
    let huge = BigRational::from_integer(BigInt::from(2).pow(32000));
    let (got, _) = convert_rational(&huge, &u("m"), &u("m")).unwrap();
    assert_eq!(got, huge);
    let past = BigRational::from_integer(BigInt::from(2).pow(40000));
    let err = convert_rational(&past, &u("m"), &u("m")).unwrap_err();
    assert!(err.to_string().contains("32768 bits"), "{err}");
}

#[test]
fn datetime_epoch_index_inverts_epoch_name() {
    assert_eq!(bovnar::datetime_epoch_index(""), Some(0));
    assert_eq!(bovnar::datetime_epoch_index("unix"), Some(0));
    assert_eq!(bovnar::datetime_epoch_index("no such epoch"), None);
    assert_eq!(bovnar::datetime_epoch_index("gps\0"), None);
    for name in ["tai", "gps", "galileo", "y2000"] {
        let idx = bovnar::datetime_epoch_index(name).unwrap();
        let Value::Struct(entries) =
            bovnar::parse_typed(format!("#!bovnar 1.1\n.t = <datetime:64,{name}> 5;").as_bytes())
                .unwrap()
        else {
            unreachable!()
        };
        let Value::Quantity(q) = &entries[0].1 else {
            panic!("{entries:?}")
        };
        assert_eq!(q.vtype.base, idx, "{name}");
        assert_eq!(q.epoch_name().as_deref(), Some(name));
    }
}

#[test]
fn version_number_and_error_names() {
    let n = bovnar::version_number();
    let v: Vec<u32> = bovnar::version()
        .split('.')
        .map(|p| p.parse().unwrap())
        .collect();
    assert_eq!(n, v[0] * 10000 + v[1] * 100 + v[2]);
    assert_eq!(ErrorCode::StringTooLong.name(), "string_too_long");
    assert_eq!(ErrorCode::None.name(), "none");
}
