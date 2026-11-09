// SPDX-License-Identifier: MIT
//! Reader and writer plumbing the crate could not reach: the writer's
//! `on_event` observer and nesting caps, the reader's input mirror, the
//! canonicalising observer, the live feeds' `on_unverified`, `want_unit` and
//! `on_error`, and the error of each failed framed document.

use std::io::Write as _;
use std::panic::{AssertUnwindSafe, catch_unwind};
use std::path::PathBuf;
use std::sync::{Arc, Mutex};

use bovnar::live::{Demux, DemuxOptions, DocumentFeed, FrameParser, ValueFeed};
use bovnar::units::UnitFlags;
use bovnar::{
    Error, ErrorCode, Event, EventData, ReadHandlers, ReadOptions, Reader, Unit, Value,
    WriteOptions, Writer, canonicalize, canonicalize_reader, parse, parse_typed, stream,
};

/// Whether a panic unwinds here. Where it aborts (wasm32-wasip2) a caught
/// panic would end the test binary, so the parts that catch one are skipped.
const UNWINDS: bool = cfg!(panic = "unwind");

fn examples() -> Vec<PathBuf> {
    let dir = PathBuf::from(concat!(env!("CARGO_MANIFEST_DIR"), "/../examples"));
    let mut v: Vec<_> = std::fs::read_dir(dir)
        .unwrap()
        .map(|e| e.unwrap().path())
        .filter(|p| p.extension().is_some_and(|x| x == "bvnr"))
        .collect();
    v.sort();
    v
}

/// The built CLI: `$BOVNAR_CLI`, or `build/bovnar` of the repository.
fn cli() -> Option<PathBuf> {
    // WASI runs no processes; the comparison with the CLI is for the native
    // legs.
    if cfg!(target_os = "wasi") {
        return None;
    }
    let candidates = [
        std::env::var_os("BOVNAR_CLI").map(PathBuf::from),
        Some(PathBuf::from(concat!(
            env!("CARGO_MANIFEST_DIR"),
            "/../build/bovnar"
        ))),
    ];
    candidates.into_iter().flatten().find(|p| p.is_file())
}

fn quantity(doc: &[u8]) -> bovnar::Quantity {
    let Value::Struct(entries) = parse_typed(doc).unwrap() else {
        unreachable!()
    };
    let Value::Quantity(q) = &entries[0].1 else {
        panic!("not a quantity: {:?}", entries[0].1)
    };
    (**q).clone()
}

/// A `Write` whose bytes stay readable through a clone.
#[derive(Clone, Default)]
struct Shared(Arc<Mutex<Vec<u8>>>);

impl std::io::Write for Shared {
    fn write(&mut self, b: &[u8]) -> std::io::Result<usize> {
        self.0.lock().unwrap().extend_from_slice(b);
        Ok(b.len())
    }
    fn flush(&mut self) -> std::io::Result<()> {
        Ok(())
    }
}

struct Failing;

impl std::io::Write for Failing {
    fn write(&mut self, _: &[u8]) -> std::io::Result<usize> {
        Err(std::io::Error::other("full"))
    }
    fn flush(&mut self) -> std::io::Result<()> {
        Ok(())
    }
}

struct Panicking;

impl std::io::Write for Panicking {
    fn write(&mut self, _: &[u8]) -> std::io::Result<usize> {
        panic!("mirror panicked")
    }
    fn flush(&mut self) -> std::io::Result<()> {
        Ok(())
    }
}

fn panic_text(payload: Box<dyn std::any::Any + Send>) -> String {
    payload
        .downcast_ref::<&str>()
        .map(|s| s.to_string())
        .or_else(|| payload.downcast_ref::<String>().cloned())
        .unwrap_or_default()
}

// -- the writer's observer ---------------------------------------------------

#[test]
fn observer_sees_what_reached_the_output() {
    let seen: Arc<Mutex<Vec<(Event, String, String)>>> = Arc::default();
    let log = Arc::clone(&seen);
    let options = WriteOptions {
        unit_flags: UnitFlags::REDUCE,
        on_event: Some(Box::new(move |d: &EventData<'_>| {
            log.lock()
                .unwrap()
                .push((d.event, d.text(), d.value_unit.to_string()));
            true
        })),
        ..WriteOptions::default()
    };
    let mut w = Writer::to_mem_with_options(4096, options).unwrap();
    w.write_quantity("d", &quantity(b".d = <uint:64,k~m> 5;"))
        .unwrap();
    w.write_string("s", "x").unwrap();
    w.finish().unwrap();
    assert_eq!(w.get_output(), b".d=<uint:64,m>5000;.s=\"x\";");
    let seen = seen.lock().unwrap();
    assert_eq!(seen[0].0, Event::AssignmentStart);
    let data: Vec<_> = seen.iter().filter(|e| e.0 == Event::Data).collect();
    // The rescale's digits and unit, not the 5 k~m the writer was handed.
    assert_eq!((data[0].1.as_str(), data[0].2.as_str()), ("5000", "m"));
    assert_eq!(data[1].1, "x");
}

#[test]
fn observer_can_veto_and_its_panic_reaches_the_caller() {
    let options = WriteOptions {
        on_event: Some(Box::new(|d: &EventData<'_>| {
            !(d.event == Event::Data && d.raw == b"2")
        })),
        ..WriteOptions::default()
    };
    let mut w = Writer::to_mem_with_options(4096, options).unwrap();
    w.write_uint("a", 1, 64, bovnar::Base::DECIMAL).unwrap();
    let err = w.write_uint("b", 2, 64, bovnar::Base::DECIMAL).unwrap_err();
    assert_eq!(err.code(), Some(ErrorCode::ScannerCallbackFailed));
    assert!(w.write_null("c").is_err(), "the writer stays failed");

    if UNWINDS {
        let options = WriteOptions {
            on_event: Some(Box::new(|d: &EventData<'_>| {
                if d.event == Event::Data {
                    panic!("observer panicked");
                }
                true
            })),
            ..WriteOptions::default()
        };
        let mut w = Writer::to_writer_with_options(Vec::new(), usize::MAX, options).unwrap();
        let payload = catch_unwind(AssertUnwindSafe(|| w.write_bool("b", true))).unwrap_err();
        assert_eq!(panic_text(payload), "observer panicked");
        let err = w.write_bool("c", true).unwrap_err();
        assert_eq!(err.code(), Some(ErrorCode::ScannerCallbackFailed));
    }
}

// -- nesting caps ------------------------------------------------------------

#[test]
fn nesting_caps_refuse_one_past_them() {
    for cap in [1u8, 3, 255] {
        let options = WriteOptions {
            max_struct_nesting: cap,
            ..WriteOptions::default()
        };
        let mut w = Writer::to_mem_with_options(1 << 16, options).unwrap();
        for _ in 0..cap {
            w.begin_struct("s").unwrap();
        }
        let err = w.begin_struct("s").unwrap_err();
        assert_eq!(err.code(), Some(ErrorCode::StructNestingTooHigh), "{cap}");
    }
    // 0 is 255.
    let mut w = Writer::to_mem_with_options(1 << 16, WriteOptions::default()).unwrap();
    for _ in 0..255 {
        w.begin_struct("s").unwrap();
    }
    assert!(w.begin_struct("s").is_err());

    let options = WriteOptions {
        max_array_nesting: 2,
        ..WriteOptions::default()
    };
    let mut w = Writer::to_mem_with_options(4096, options).unwrap();
    w.emit(Event::AssignmentStart, bovnar::EmitSpec::key("a"))
        .unwrap();
    w.begin_array_row().unwrap();
    w.begin_array_row().unwrap();
    let err = w.begin_array_row().unwrap_err();
    assert_eq!(err.code(), Some(ErrorCode::ArrayNestingTooHigh));
}

// -- the reader's mirror -----------------------------------------------------

fn doc_with_octets() -> Vec<u8> {
    let mut w = Writer::to_mem(1 << 16, false).unwrap();
    w.write_uint("a", 1, 64, bovnar::Base::DECIMAL).unwrap();
    w.write_octets("o", &[0u8, 1, 2, 255].repeat(300)).unwrap();
    w.write_string("s", "x").unwrap();
    w.finish().unwrap();
    let mut doc = b"#!bovnar 1.1\n# a comment\n".to_vec();
    doc.extend_from_slice(w.get_output());
    doc
}

#[test]
fn mirror_receives_exactly_the_input() {
    let doc = doc_with_octets();
    let options = ReadOptions::default();
    let mut out = Vec::new();
    Reader::new()
        .unwrap()
        .read_mem_mirror(&doc, &options, ReadHandlers::default(), &mut out)
        .unwrap();
    assert_eq!(out, doc);
    let mut out = Vec::new();
    Reader::new()
        .unwrap()
        .read_reader_mirror(&doc[..], &options, ReadHandlers::default(), &mut out)
        .unwrap();
    assert_eq!(out, doc);

    for size in [1, 3, 64, doc.len()] {
        let mut feed = DocumentFeed::new(&options).unwrap();
        feed.set_mirror(Vec::<u8>::new()).unwrap();
        for piece in doc.chunks(size) {
            feed.feed(piece, &mut |_| true).unwrap();
        }
        feed.finish(&mut |_| true).unwrap();
        assert!(feed.take_mirror::<String>().is_none(), "not a String");
        assert_eq!(feed.take_mirror::<Vec<u8>>().unwrap(), doc, "{size}");
        // Taken back: the next document is not mirrored.
        feed.reset().unwrap();
        feed.feed(b".z = 1;", &mut |_| true).unwrap();
        assert!(feed.take_mirror::<Vec<u8>>().is_none());
    }

    // The mirror stays across documents, and a ValueFeed has one too.
    let shared = Shared::default();
    let mut values = ValueFeed::new(false, &options).unwrap();
    values.set_mirror(shared.clone()).unwrap();
    let mut got = Vec::new();
    for doc in [&b".a = 1;"[..], b".b = 2;"] {
        values.feed(doc, &mut got).unwrap();
        values.finish(&mut got).unwrap();
        values.reset().unwrap();
    }
    assert_eq!(*shared.0.lock().unwrap(), b".a = 1;.b = 2;");
    assert_eq!(got.len(), 2);
}

#[test]
fn mirror_gets_the_input_through_the_error() {
    let mut feed = DocumentFeed::new(&ReadOptions::default()).unwrap();
    feed.set_mirror(Vec::<u8>::new()).unwrap();
    feed.feed(b".a = 1; ", &mut |_| true).unwrap();
    let err = feed.feed(b".b = @; .c = 2;", &mut |_| true).unwrap_err();
    assert_eq!(err.code(), Some(ErrorCode::UnexpectedInputByte));
    assert_eq!(feed.take_mirror::<Vec<u8>>().unwrap(), b".a = 1; .b = @");

    let mut out = Vec::new();
    let err = Reader::new()
        .unwrap()
        .read_mem_mirror(
            b".a = 1; .b = @; .c = 2;",
            &ReadOptions::default(),
            ReadHandlers::default(),
            &mut out,
        )
        .unwrap_err();
    assert_eq!(err.code(), Some(ErrorCode::UnexpectedInputByte));
    assert_eq!(out, b".a = 1; .b = @");
}

#[test]
fn a_failing_or_panicking_mirror_reaches_the_caller() {
    let options = ReadOptions::default();
    let err = Reader::new()
        .unwrap()
        .read_mem_mirror(b".a = 1;", &options, ReadHandlers::default(), &mut Failing)
        .unwrap_err();
    assert_eq!(err.code(), Some(ErrorCode::WritingToSink));
    assert!(matches!(err, Error::Io { .. }), "{err:?}");

    if UNWINDS {
        let payload = catch_unwind(AssertUnwindSafe(|| {
            Reader::new().unwrap().read_mem_mirror(
                b".a = 1;",
                &options,
                ReadHandlers::default(),
                &mut Panicking,
            )
        }))
        .unwrap_err();
        assert_eq!(panic_text(payload), "mirror panicked");
    }

    let mut feed = DocumentFeed::new(&options).unwrap();
    feed.set_mirror(Failing).unwrap();
    let err = feed.feed(b".a = 1;", &mut |_| true).unwrap_err();
    assert_eq!(err.code(), Some(ErrorCode::WritingToSink));
    // The document has failed; reset() starts a fresh one with the mirror.
    assert_eq!(feed.feed(b"x", &mut |_| true).unwrap_err(), err);

    if UNWINDS {
        let mut feed = DocumentFeed::new(&options).unwrap();
        feed.set_mirror(Panicking).unwrap();
        let payload =
            catch_unwind(AssertUnwindSafe(|| feed.feed(b".a = 1;", &mut |_| true))).unwrap_err();
        assert_eq!(panic_text(payload), "mirror panicked");
        assert!(feed.feed(b".b = 1;", &mut |_| true).is_err());
    }
}

// -- canonical form ----------------------------------------------------------

#[test]
fn canonical_form_of_a_small_document() {
    let doc = b".a = 1 ;  # one\n.b = { .c = \"x\"; };";
    let options = ReadOptions::default();
    assert_eq!(
        canonicalize(doc, false, &options).unwrap(),
        b".a=<uint:64,_10,no_unit>1;.b={.c=<utf8>\"x\";};"
    );
    let pretty = canonicalize(doc, true, &options).unwrap();
    assert!(pretty.contains(&b'\n'));
    assert_eq!(parse(&pretty).unwrap(), parse(doc).unwrap());
}

#[test]
fn canonical_form_matches_the_cli_and_is_idempotent() {
    let options = ReadOptions::default();
    let cli = cli();
    for path in examples() {
        let data = std::fs::read(&path).unwrap();
        for pretty in [false, true] {
            let canon = canonicalize(&data, pretty, &options).unwrap();
            assert_eq!(
                canonicalize(&canon, pretty, &options).unwrap(),
                canon,
                "{path:?} is not idempotent"
            );
            let text = |d: &[u8]| bovnar::to_string(&parse(d).unwrap(), false).unwrap();
            assert_eq!(text(&canon), text(&data), "{path:?}");
            let mut out = Vec::new();
            canonicalize_reader(&data[..], &mut out, pretty, &options).unwrap();
            assert_eq!(out, canon, "{path:?} from a reader");
            if let Some(cli) = &cli {
                let mut cmd = std::process::Command::new(cli);
                cmd.args(["events", "-d"]);
                if pretty {
                    cmd.arg("-p");
                }
                let run = cmd.arg(&path).output().unwrap();
                assert!(run.status.success(), "{path:?}");
                // `bovnar events -d` writes the observer's output to stderr.
                assert_eq!(run.stderr, canon, "{path:?} differs from the CLI");
            }
        }
    }
}

/// A reader handing over one byte per read.
struct Trickle<'a>(&'a [u8]);

impl std::io::Read for Trickle<'_> {
    fn read(&mut self, buf: &mut [u8]) -> std::io::Result<usize> {
        match (self.0.split_first(), buf.first_mut()) {
            (Some((b, rest)), Some(slot)) => {
                *slot = *b;
                self.0 = rest;
                Ok(1)
            }
            _ => Ok(0),
        }
    }
}

#[test]
fn canonical_form_keeps_the_directive() {
    let options = ReadOptions::default();
    let doc = b"\xef\xbb\xbf  \n#!bovnar 1.1\n.a = 1;";
    let canon = canonicalize(doc, false, &options).unwrap();
    assert!(canon.starts_with(b"#!bovnar 1.1\n"), "{canon:?}");
    let mut out = Vec::new();
    canonicalize_reader(Trickle(doc), &mut out, false, &options).unwrap();
    assert_eq!(out, canon);
    let plain = canonicalize(b"# a comment\n.a = 1;", false, &options).unwrap();
    assert!(!plain.starts_with(b"#"));
}

#[test]
fn canonicalize_refuses_a_recovered_document_and_reports_errors() {
    let doc = b".a=[1,2,{.b=@;},3]; .c = 2;";
    let lenient = ReadOptions {
        continue_on_error: true,
        ..ReadOptions::default()
    };
    let err = canonicalize(doc, false, &lenient).unwrap_err();
    assert_eq!(err.code(), Some(ErrorCode::UnexpectedInputByte));
    assert_eq!(err.position().unwrap().line, 1);
    assert!(canonicalize(doc, false, &ReadOptions::default()).is_err());
    assert_eq!(
        canonicalize(b".a=1;", false, &lenient).unwrap(),
        canonicalize(b".a=1;", false, &ReadOptions::default()).unwrap()
    );

    let short = ReadOptions {
        max_string_length: 2,
        ..ReadOptions::default()
    };
    let err = canonicalize(b".a=\"xxxx\";", false, &short).unwrap_err();
    assert_eq!(err.code(), Some(ErrorCode::StringTooLong));

    let err =
        canonicalize_reader(&b".a=1;"[..], Failing, false, &ReadOptions::default()).unwrap_err();
    assert_eq!(err.code(), Some(ErrorCode::WritingToSink));
    if UNWINDS {
        let payload = catch_unwind(AssertUnwindSafe(|| {
            canonicalize_reader(&b".a=1;"[..], Panicking, false, &ReadOptions::default())
        }))
        .unwrap_err();
        assert_eq!(panic_text(payload), "mirror panicked");
    }
    struct BadIn;
    impl std::io::Read for BadIn {
        fn read(&mut self, _: &mut [u8]) -> std::io::Result<usize> {
            Err(std::io::Error::other("gone"))
        }
    }
    let err = canonicalize_reader(BadIn, Vec::new(), false, &ReadOptions::default()).unwrap_err();
    assert_eq!(err.code(), Some(ErrorCode::ReadingFromSourceFd));
}

// -- the live feeds' handlers --------------------------------------------------

fn lexer_events(doc: &[u8]) -> Vec<(Event, Vec<u8>)> {
    let mut want = Vec::new();
    let mut on = |d: &EventData<'_>| {
        want.push((d.event, d.raw.to_vec()));
        true
    };
    Reader::new()
        .unwrap()
        .read_mem(doc, &ReadOptions::default(), None, Some(&mut on))
        .unwrap();
    want
}

#[test]
fn document_feed_takes_every_handler() {
    let doc = b".a = <uint:8> 7; .d = <uint:64,k~m> 3;";
    let want = lexer_events(doc);
    for size in [1, 5, doc.len()] {
        let mut feed = DocumentFeed::new(&ReadOptions::default()).unwrap();
        let mut got = Vec::new();
        let mut converted = Vec::new();
        for piece in doc.chunks(size) {
            let mut on_raw = |d: &EventData<'_>| {
                got.push((d.event, d.raw.to_vec()));
                true
            };
            let mut on_value = |d: &EventData<'_>| {
                if d.converted {
                    converted.push(String::from_utf8_lossy(d.converted_text).into_owned());
                }
                true
            };
            let mut want_unit = |d: &EventData<'_>| {
                (!d.value_unit.is_dimensionless()).then(|| (Unit::parse("m").unwrap(), 0))
            };
            // Every call passes the same set; the first one decides.
            feed.feed_with(
                piece,
                ReadHandlers {
                    on_verified: Some(&mut on_value),
                    on_unverified: Some(&mut on_raw),
                    want_unit: Some(&mut want_unit),
                    on_error: None,
                },
            )
            .unwrap();
        }
        let mut on_raw = |d: &EventData<'_>| {
            got.push((d.event, d.raw.to_vec()));
            true
        };
        feed.finish_with(ReadHandlers {
            on_unverified: Some(&mut on_raw),
            ..ReadHandlers::default()
        })
        .unwrap();
        assert_eq!(got, want, "{size}");
        assert_eq!(converted, ["3000"], "{size}");
    }
}

#[test]
fn a_feed_started_without_a_handler_refuses_it_later() {
    let mut feed = DocumentFeed::new(&ReadOptions::default()).unwrap();
    feed.feed(b".a = 1", &mut |_| true).unwrap();
    let mut on_raw = |_: &EventData<'_>| true;
    let err = feed
        .feed_with(
            b";",
            ReadHandlers {
                on_unverified: Some(&mut on_raw),
                ..ReadHandlers::default()
            },
        )
        .unwrap_err();
    assert!(matches!(err, Error::Argument { .. }), "{err:?}");
    // After a reset the first call decides again.
    feed.reset().unwrap();
    let mut on_raw = |_: &EventData<'_>| true;
    feed.feed_with(
        b".a = 1;",
        ReadHandlers {
            on_unverified: Some(&mut on_raw),
            ..ReadHandlers::default()
        },
    )
    .unwrap();
    // Fewer handlers later are fine.
    feed.finish(&mut |_| true).unwrap();
}

#[test]
fn feed_on_error_sees_recovered_errors_and_panics_propagate() {
    let lenient = ReadOptions {
        continue_on_error: true,
        ..ReadOptions::default()
    };
    let mut feed = DocumentFeed::new(&lenient).unwrap();
    let mut codes = Vec::new();
    let mut on_error = |e: &Error| codes.push(e.code());
    feed.feed_with(
        b".a = @; .b = 2;",
        ReadHandlers {
            on_error: Some(&mut on_error),
            ..ReadHandlers::default()
        },
    )
    .unwrap();
    assert_eq!(codes, [Some(ErrorCode::UnexpectedInputByte)]);

    if UNWINDS {
        for which in 0..3 {
            let mut feed = DocumentFeed::new(&ReadOptions::default()).unwrap();
            let payload = catch_unwind(AssertUnwindSafe(|| {
                let mut boom = |_: &EventData<'_>| -> bool { panic!("handler panicked") };
                let mut boom_unit =
                    |_: &EventData<'_>| -> Option<(Unit, u32)> { panic!("handler panicked") };
                let mut h = ReadHandlers::default();
                match which {
                    0 => h.on_unverified = Some(&mut boom),
                    1 => h.want_unit = Some(&mut boom_unit),
                    _ => h.on_verified = Some(&mut boom),
                }
                feed.feed_with(b".a = 1;", h)
            }))
            .unwrap_err();
            assert_eq!(panic_text(payload), "handler panicked", "{which}");
            assert!(feed.feed(b".b = 1;", &mut |_| true).is_err(), "{which}");
        }
    }
}

#[test]
fn frame_parser_and_demux_take_the_handlers() {
    let frames = stream::frame_documents(&[&b".a = 1;"[..], b".b = <uint:64,k~m> 2;"]).unwrap();
    let mut parser = FrameParser::new(&ReadOptions::default(), false, 0).unwrap();
    let mut raw = 0;
    let mut converted = Vec::new();
    let mut docs = Vec::new();
    for piece in frames.chunks(6) {
        let mut on_raw = |_: &EventData<'_>| {
            raw += 1;
            true
        };
        let mut on_value = |d: &EventData<'_>| {
            if d.converted {
                converted.push(String::from_utf8_lossy(d.converted_text).into_owned());
            }
            true
        };
        let mut want_unit = |d: &EventData<'_>| {
            (!d.value_unit.is_dimensionless()).then(|| (Unit::parse("m").unwrap(), 0))
        };
        parser
            .feed_with(
                piece,
                ReadHandlers {
                    on_verified: Some(&mut on_value),
                    on_unverified: Some(&mut on_raw),
                    want_unit: Some(&mut want_unit),
                    on_error: None,
                },
                &mut |i, r| {
                    docs.push((i, r.is_ok()));
                    true
                },
            )
            .unwrap();
    }
    parser.finish().unwrap();
    assert!(raw > 0);
    assert_eq!(converted, ["2000"]);
    assert_eq!(docs, [(0, true), (1, true)]);

    // The demux's reader: every verified event, the mux stream's included.
    let mut w = Writer::to_mem(4096, false).unwrap();
    w.write_string("codec", "av1").unwrap();
    w.mux_begin("media").unwrap();
    w.mux_send(1, b"frame").unwrap();
    w.mux_end().unwrap();
    w.finish().unwrap();
    let doc = w.get_output().to_vec();
    let mut demux = Demux::new(&DemuxOptions::default()).unwrap();
    let mut verified = Vec::new();
    let mut messages = Vec::new();
    for piece in doc.chunks(4) {
        let mut input = piece;
        loop {
            let mut on = |d: &EventData<'_>| {
                verified.push(d.event);
                true
            };
            let item = demux
                .next_with(
                    &mut input,
                    ReadHandlers {
                        on_verified: Some(&mut on),
                        ..ReadHandlers::default()
                    },
                )
                .unwrap();
            match item {
                Some(bovnar::live::DemuxItem::Message { channel, payload }) => {
                    messages.push((channel, payload.to_vec()))
                }
                Some(_) => {}
                None => break,
            }
        }
    }
    let mut on = |d: &EventData<'_>| {
        verified.push(d.event);
        true
    };
    demux
        .finish_with(ReadHandlers {
            on_verified: Some(&mut on),
            ..ReadHandlers::default()
        })
        .unwrap();
    assert_eq!(messages, [(1, b"frame".to_vec())]);
    assert!(verified.contains(&Event::OctetStreamStart));
    assert_eq!(verified.last(), Some(&Event::StreamEnd));

    if UNWINDS {
        // A panic in a demux handler reaches the caller.
        let mut demux = Demux::new(&DemuxOptions::default()).unwrap();
        let payload = catch_unwind(AssertUnwindSafe(|| {
            let mut boom = |_: &EventData<'_>| -> bool { panic!("demux handler") };
            let mut input = &doc[..];
            demux
                .next_with(
                    &mut input,
                    ReadHandlers {
                        on_unverified: Some(&mut boom),
                        ..ReadHandlers::default()
                    },
                )
                .map(|_| ())
        }))
        .unwrap_err();
        assert_eq!(panic_text(payload), "demux handler");
    }
}

// -- failed framed documents ---------------------------------------------------

#[test]
fn each_failed_document_keeps_its_error() {
    let frames =
        stream::frame_documents(&[&b".a = 1;"[..], b".a = @;", b".b = 2;", b".c = \"x"]).unwrap();
    let options = ReadOptions::default();
    let docs = stream::load_documents_results(&frames, false, 0, &options).unwrap();
    assert_eq!(docs.len(), 4);
    assert!(docs[0].is_ok() && docs[2].is_ok());
    assert_eq!(
        docs[1].as_ref().unwrap_err().code(),
        Some(ErrorCode::UnexpectedInputByte)
    );
    assert_eq!(
        docs[3].as_ref().unwrap_err().code(),
        Some(ErrorCode::GotIncompleteBvnrStream)
    );
    let from_reader =
        stream::load_documents_reader_results(&frames[..], false, 0, &options).unwrap();
    assert_eq!(from_reader, docs);
    // The existing form still gives None for them.
    let plain = stream::load_documents(&frames, false, true, 0).unwrap();
    assert_eq!(plain.iter().filter(|d| d.is_none()).count(), 2);
    // The framing's own failure is the outer error.
    let mut cut = frames.clone();
    cut.truncate(5);
    assert!(stream::load_documents_results(&cut, false, 0, &options).is_err());
}

#[test]
fn writer_into_a_flushed_writer_with_options() {
    let shared = Shared::default();
    let options = WriteOptions {
        pretty: true,
        emit_version: true,
        ..WriteOptions::default()
    };
    let mut w = Writer::to_writer_with_options(shared.clone(), usize::MAX, options).unwrap();
    w.write_null("a").unwrap();
    w.finish().unwrap();
    let out = shared.0.lock().unwrap().clone();
    assert!(out.starts_with(b"#!bovnar "), "{out:?}");
    let mut sink = Vec::new();
    sink.write_all(&out).unwrap();
    assert_eq!(
        parse(&sink).unwrap(),
        Value::Struct(vec![("a".into(), Value::Null)])
    );
}

#[test]
fn a_recovered_document_is_refused_before_any_output_is_flushed() {
    let lenient = ReadOptions {
        continue_on_error: true,
        ..ReadOptions::default()
    };
    let mut out = Vec::new();
    let err = canonicalize_reader(&b".a = @; .b = 1;"[..], &mut out, false, &lenient).unwrap_err();
    assert_eq!(err.code(), Some(ErrorCode::UnexpectedInputByte));
    assert!(out.is_empty(), "{:?}", String::from_utf8_lossy(&out));
}

#[test]
fn a_canonicalize_inside_another_keeps_the_outer_first_error() {
    // The outer read's input runs a whole canonicalisation of its own on the
    // same thread; the outer refusal still names the outer document's error.
    struct Nested<'a>(&'a [u8], bool);
    impl std::io::Read for Nested<'_> {
        fn read(&mut self, buf: &mut [u8]) -> std::io::Result<usize> {
            if !self.1 {
                self.1 = true;
                canonicalize(b".x = 1;", false, &ReadOptions::default()).unwrap();
            }
            self.0.read(buf)
        }
    }
    let lenient = ReadOptions {
        continue_on_error: true,
        ..ReadOptions::default()
    };
    let err = canonicalize_reader(
        Nested(b".a = 1;\n.b = @;", false),
        Vec::new(),
        false,
        &lenient,
    )
    .unwrap_err();
    assert_eq!(err.code(), Some(ErrorCode::UnexpectedInputByte));
    assert_eq!(err.position().unwrap().line, 2);
}

#[test]
fn narrow_float_widths_round_to_ieee_in_the_writer() {
    let mut w = Writer::to_mem(4096, false).unwrap();
    w.write_float("a", 2049.0, 16).unwrap();
    w.write_float("b", 16_777_217.0, 32).unwrap();
    w.write_float("c", 65504.0, 16).unwrap();
    w.finish().unwrap();
    assert_eq!(
        w.get_output(),
        b".a=<float:16>2.048e+3;.b=<float:32>1.6777216e+7;.c=<float:16>6.5504e+4;"
    );
    let mut w = Writer::to_mem(4096, false).unwrap();
    let err = w.write_float("a", 1e5, 16).unwrap_err();
    assert_eq!(err.code(), Some(ErrorCode::ValueOutOfRange));
}
