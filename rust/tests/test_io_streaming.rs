// SPDX-License-Identifier: MIT
//! The `std::io` entry points stream through the C library's callback source
//! and sink. Whatever the reader or writer does within its contract (short
//! reads and writes, `Interrupted`), the results are those of the in-memory
//! functions; a failing one is an `Error::Io` with its own kind, after the
//! events it made possible; a panic in one reaches the caller instead of
//! unwinding through C; and the output reaches the writer while the document
//! is still being written.

use std::io::{self, Read, Write};
use std::sync::{Arc, Mutex};

use bovnar::stream;
use bovnar::{
    Base, Error, ErrorCode, Event, EventData, EventPayload, ReadOptions, Reader, Unit, Value,
    Writer, parse, parse_typed,
};

/// A reader that hands out at most `step` bytes a call, says `Interrupted`
/// every `interrupt`th call, and fails or panics at a given offset.
struct Awkward<'a> {
    data: &'a [u8],
    pos: usize,
    step: usize,
    calls: usize,
    interrupt: usize,
    fail_at: Option<usize>,
    panic_at: Option<usize>,
}

impl<'a> Awkward<'a> {
    fn new(data: &'a [u8], step: usize, interrupt: usize) -> Self {
        Awkward {
            data,
            pos: 0,
            step,
            calls: 0,
            interrupt,
            fail_at: None,
            panic_at: None,
        }
    }
}

impl Read for Awkward<'_> {
    fn read(&mut self, buf: &mut [u8]) -> io::Result<usize> {
        self.calls += 1;
        if self.interrupt != 0 && self.calls.is_multiple_of(self.interrupt) {
            return Err(io::Error::from(io::ErrorKind::Interrupted));
        }
        if self.panic_at.is_some_and(|at| self.pos >= at) {
            panic!("reader blew up");
        }
        if self.fail_at.is_some_and(|at| self.pos >= at) {
            return Err(io::Error::new(
                io::ErrorKind::ConnectionReset,
                "peer went away",
            ));
        }
        let mut end = (self.pos + self.step.min(buf.len())).min(self.data.len());
        for limit in [self.fail_at, self.panic_at].into_iter().flatten() {
            if self.pos < limit {
                end = end.min(limit);
            }
        }
        let n = end - self.pos;
        buf[..n].copy_from_slice(&self.data[self.pos..end]);
        self.pos = end;
        Ok(n)
    }
}

/// A writer that takes at most `step` bytes a call, says `Interrupted` every
/// `interrupt`th call, and fails or panics once it holds a given number of
/// bytes. Its output is shared, to be looked at while a writer holds it.
#[derive(Clone)]
struct Partial {
    out: Arc<Mutex<Vec<u8>>>,
    step: usize,
    calls: usize,
    interrupt: usize,
    fail_at: Option<usize>,
    panic_at: Option<usize>,
    flushes: Arc<Mutex<usize>>,
}

impl Partial {
    fn new(step: usize, interrupt: usize) -> Self {
        Partial {
            out: Arc::default(),
            step,
            calls: 0,
            interrupt,
            fail_at: None,
            panic_at: None,
            flushes: Arc::default(),
        }
    }

    fn bytes(&self) -> Vec<u8> {
        self.out.lock().unwrap().clone()
    }
}

impl Write for Partial {
    fn write(&mut self, buf: &[u8]) -> io::Result<usize> {
        self.calls += 1;
        if self.interrupt != 0 && self.calls.is_multiple_of(self.interrupt) {
            return Err(io::Error::from(io::ErrorKind::Interrupted));
        }
        let have = self.out.lock().unwrap().len();
        if self.panic_at.is_some_and(|at| have >= at) {
            panic!("writer blew up");
        }
        if self.fail_at.is_some_and(|at| have >= at) {
            return Err(io::Error::new(io::ErrorKind::BrokenPipe, "peer went away"));
        }
        let n = buf.len().min(self.step);
        self.out.lock().unwrap().extend_from_slice(&buf[..n]);
        Ok(n)
    }

    fn flush(&mut self) -> io::Result<()> {
        *self.flushes.lock().unwrap() += 1;
        Ok(())
    }
}

/// The ways of reading that test the source: byte by byte, small pieces
/// with interruptions, and everything at once.
const READERS: [(usize, usize); 4] = [(1, 0), (3, 2), (7, 5), (usize::MAX, 0)];
/// The same for writers.
const WRITERS: [(usize, usize); 4] = [(1, 3), (5, 2), (4096, 0), (usize::MAX, 0)];

fn obj(fields: &[(&str, Value)]) -> Value {
    Value::Struct(
        fields
            .iter()
            .map(|(k, v)| (k.to_string(), v.clone()))
            .collect(),
    )
}

/// Documents good and bad, short and longer than one 64 KiB pull.
fn documents() -> Vec<Vec<u8>> {
    let mut long = Vec::new();
    for i in 0..3000 {
        long.extend_from_slice(format!(".k{i} = <uint:32,m> {i};\n").as_bytes());
    }
    let octets =
        bovnar::to_string(&obj(&[("blob", Value::Bytes(vec![7; 70_000]))]), false).unwrap();
    vec![
        b".a = 1; .b = <uint:8>7; .s = \"x\"; .l = [1, 2, 3] / [4, 5, 6];".to_vec(),
        b".t = <datetime:64> 1700000000; .r = &a.b; .sym = up;".to_vec(),
        b".a = @;".to_vec(),
        b".a = [1;".to_vec(),
        b".a = \"unterminated".to_vec(),
        b"#!bovnar 1.1\n.d = <float:64,k~m> 2.5;".to_vec(),
        long,
        octets,
        Vec::new(),
    ]
}

fn events_of(
    read: impl FnOnce(&mut Reader, &mut dyn FnMut(&EventData<'_>) -> bool) -> bovnar::Result<()>,
) -> (bovnar::Result<()>, Vec<EventPayload>) {
    let mut events = Vec::new();
    let mut on = |d: &EventData<'_>| {
        events.push(EventPayload::from(d));
        true
    };
    let r = read(&mut Reader::new().unwrap(), &mut on);
    (r, events)
}

#[test]
fn reader_twins_read_what_the_in_memory_functions_read() {
    for doc in documents() {
        // Byte by byte only where it is quick; the long documents still go
        // through small pieces.
        let readers = READERS
            .iter()
            .filter(|(step, _)| *step > 1 || doc.len() < 4096);
        for &(step, interrupt) in readers {
            let input = || Awkward::new(&doc, step, interrupt);
            assert_eq!(bovnar::parse_reader(input()), parse(&doc), "step {step}");
            assert_eq!(bovnar::parse_typed_reader(input()), parse_typed(&doc));
            for cap in [0, 10, doc.len() as u64] {
                let options = ReadOptions {
                    max_file_size: cap,
                    ..ReadOptions::default()
                };
                let mem = events_of(|r, on| r.read_mem(&doc, &options, Some(on), None));
                let io = events_of(|r, on| r.read_reader(input(), &options, Some(on), None));
                assert_eq!(io, mem, "step {step} cap {cap}");
            }
            let m = Unit::parse("m").unwrap();
            let mut want = |_: &EventData<'_>| Some((m, 16));
            let mem = events_of(|r, on| {
                r.read_mem_want_unit(&doc, &ReadOptions::default(), &mut want, Some(on))
            });
            let mut want = |_: &EventData<'_>| Some((m, 16));
            let io = events_of(|r, on| {
                r.read_reader_want_unit(input(), &ReadOptions::default(), &mut want, Some(on))
            });
            assert_eq!(io, mem);
            for typed in [false, true] {
                assert_eq!(
                    stream::parse_embedded_reader(input(), typed),
                    stream::parse_embedded(&doc, typed)
                );
            }
        }
    }
}

#[test]
fn stream_reader_twins_read_what_the_in_memory_functions_read() {
    let big = vec![0x5Au8; 200_000];
    let muxed = stream::mux_dump(
        &[
            (0, b"hello".to_vec()),
            (7, big.clone()),
            (7, Vec::new()),
            (3, big[..65_517].to_vec()),
        ],
        "mux",
    )
    .unwrap();
    let values = [
        obj(&[("a", Value::Int(1))]),
        obj(&[("blob", Value::Bytes(big.clone()))]),
    ];
    let framed = stream::dump_documents(&values, false).unwrap();
    let bad = stream::frame_documents(&[&b".n = 1;"[..], b".n = [1;", b".n = 3;"]).unwrap();
    for (step, interrupt) in READERS {
        for key in [None, Some("mux"), Some("other")] {
            for max in [0, 100] {
                assert_eq!(
                    stream::mux_load_reader(Awkward::new(&muxed, step, interrupt), max, key),
                    stream::mux_load(&muxed, max, key)
                );
            }
        }
        for blob in [&framed, &bad, &framed[..framed.len() - 1].to_vec()] {
            for (typed, cont) in [(false, false), (true, false), (false, true)] {
                assert_eq!(
                    stream::load_documents_reader(
                        Awkward::new(blob, step, interrupt),
                        typed,
                        cont,
                        0
                    ),
                    stream::load_documents(blob, typed, cont, 0)
                );
            }
        }
    }
}

#[test]
fn a_failing_reader_is_an_io_error_after_the_events_it_allowed() {
    let doc = &documents()[6];
    let mut input = Awkward::new(doc, usize::MAX, 0);
    input.fail_at = Some(70_000);
    let (r, events) =
        events_of(|r, on| r.read_reader(input, &ReadOptions::default(), Some(on), None));
    let e = r.unwrap_err();
    assert_eq!(e.code(), Some(ErrorCode::ReadingFromSourceFd));
    assert_eq!(
        e,
        Error::Io {
            code: ErrorCode::ReadingFromSourceFd,
            io_kind: io::ErrorKind::ConnectionReset,
            message: "peer went away".into(),
        }
    );
    // The input was parsed as it came: the pulls before the one that failed
    // gave their events.
    let keys = events
        .iter()
        .filter(|e| e.event == Event::AssignmentStart)
        .count();
    assert!(keys > 1000, "{keys} assignments before the failure");

    let muxed = stream::mux_dump(&[(1, vec![0; 100_000])], "mux").unwrap();
    let framed = stream::frame_documents(&[&doc[..], &doc[..]]).unwrap();
    for at in [0, 3, 70_000] {
        let reader = |data| {
            let mut r = Awkward::new(data, 4096, 3);
            r.fail_at = Some(at);
            r
        };
        let io = |e: Error| e.code() == Some(ErrorCode::ReadingFromSourceFd);
        assert!(bovnar::parse_reader(reader(doc)).is_err_and(io));
        assert!(stream::parse_embedded_reader(reader(doc), false).is_err_and(io));
        assert!(stream::mux_load_reader(reader(&muxed), 0, None).is_err_and(io));
        assert!(stream::load_documents_reader(reader(&framed), false, true, 0).is_err_and(io));
    }
}

#[test]
fn a_reader_that_claims_more_than_it_was_given_is_refused() {
    struct Liar;
    impl Read for Liar {
        fn read(&mut self, buf: &mut [u8]) -> io::Result<usize> {
            Ok(buf.len() + 1)
        }
    }
    let e = bovnar::parse_reader(Liar).unwrap_err();
    assert!(
        matches!(
            e,
            Error::Io {
                io_kind: io::ErrorKind::InvalidData,
                code: ErrorCode::ReadingFromSourceFd,
                ..
            }
        ),
        "{e:?}"
    );
}

#[test]
#[cfg_attr(panic = "abort", ignore = "a panic cannot be caught where it aborts")]
fn a_panic_in_the_reader_reaches_the_caller() {
    let doc = &documents()[6];
    let muxed = stream::mux_dump(&[(1, vec![0; 100_000])], "mux").unwrap();
    let framed = stream::frame_documents(&[&doc[..]]).unwrap();
    let panicky = |data| {
        let mut r = Awkward::new(data, 1000, 0);
        r.panic_at = Some(5000);
        r
    };
    let calls: [Box<dyn Fn()>; 5] = [
        Box::new(|| drop(bovnar::parse_reader(panicky(doc)))),
        Box::new(|| {
            let _ = Reader::new().unwrap().read_reader(
                panicky(doc),
                &ReadOptions::default(),
                None,
                None,
            );
        }),
        Box::new(|| drop(stream::parse_embedded_reader(panicky(doc), true))),
        Box::new(|| drop(stream::mux_load_reader(panicky(&muxed), 0, None))),
        Box::new(|| {
            drop(stream::load_documents_reader(
                panicky(&framed),
                false,
                false,
                0,
            ))
        }),
    ];
    for call in calls {
        let payload = std::panic::catch_unwind(std::panic::AssertUnwindSafe(call)).unwrap_err();
        assert_eq!(payload.downcast_ref::<&str>(), Some(&"reader blew up"));
    }
    // Nothing was left behind: the next read is an ordinary one.
    assert_eq!(bovnar::parse_reader(&doc[..]), parse(doc));
}

/// Values whose documents span several 64 KiB blocks.
fn big_value() -> Value {
    let rows: Vec<Value> = (0..20_000).map(Value::Int).collect();
    obj(&[
        ("s", Value::String("x".repeat(60_000))),
        ("blob", Value::Bytes(vec![1; 150_000])),
        ("rows", Value::Array(rows)),
        ("small", Value::Bool(false)),
    ])
}

#[test]
fn writer_twins_write_what_the_in_memory_functions_return() {
    let value = big_value();
    let small = obj(&[("a", Value::Int(1))]);
    let docs = [
        bovnar::to_string(&value, false).unwrap(),
        b".b = 2;".to_vec(),
    ];
    let msgs = [
        (0u64, vec![9u8; 300_000]),
        (5, b"hi".to_vec()),
        (0, Vec::new()),
    ];
    for (step, interrupt) in WRITERS {
        let out = || Partial::new(step, interrupt);
        for pretty in [false, true] {
            for v in [&value, &small] {
                let o = out();
                bovnar::write_to(v, pretty, o.clone()).unwrap();
                assert_eq!(o.bytes(), bovnar::to_string(v, pretty).unwrap());
                assert_eq!(*o.flushes.lock().unwrap(), 1, "flushed once, at the end");
            }
            let o = out();
            stream::dump_documents_to(&[small.clone(), value.clone()], pretty, o.clone()).unwrap();
            assert_eq!(
                o.bytes(),
                stream::dump_documents(&[small.clone(), value.clone()], pretty).unwrap()
            );
            let o = out();
            stream::embed_document_to(&docs[0], "payload", pretty, o.clone()).unwrap();
            assert_eq!(
                o.bytes(),
                stream::embed_document(&docs[0], "payload", pretty).unwrap()
            );
        }
        let o = out();
        stream::frame_documents_to(&docs, o.clone()).unwrap();
        assert_eq!(o.bytes(), stream::frame_documents(&docs).unwrap());
        let o = out();
        stream::mux_dump_to(&msgs, "mux", o.clone()).unwrap();
        assert_eq!(o.bytes(), stream::mux_dump(&msgs, "mux").unwrap());

        let o = out();
        let mut w = Writer::to_writer(o.clone(), true).unwrap();
        w.write_uint("a", 255, 64, Base::HEX).unwrap();
        w.write_octets("b", &[3; 100_000]).unwrap();
        w.finish().unwrap();
        let mut m = Writer::to_mem(1 << 20, true).unwrap();
        m.write_uint("a", 255, 64, Base::HEX).unwrap();
        m.write_octets("b", &[3; 100_000]).unwrap();
        m.finish().unwrap();
        assert_eq!(o.bytes(), m.get_output());
        assert!(w.get_output().is_empty());
        assert_eq!(w.bytes_written(), m.bytes_written());
    }
}

#[test]
fn a_writer_receives_the_document_while_it_is_written() {
    let o = Partial::new(usize::MAX, 0);
    let mut w = Writer::to_writer(o.clone(), false).unwrap();
    w.write_string("a", "x").unwrap();
    assert!(o.bytes().is_empty(), "a short document waits for finish");
    w.write_octets("blob", &[0; 200_000]).unwrap();
    let early = o.bytes().len();
    assert!(early >= 65_536, "{early} bytes before finish");
    w.finish().unwrap();
    assert!(o.bytes().len() > early);
    assert_eq!(*o.flushes.lock().unwrap(), 1);
}

#[test]
fn to_writer_has_no_cap_and_to_writer_with_keeps_its_own() {
    // Past the 4 MiB DEFAULT_MEM_CAP that to_writer used to buffer in.
    let o = Partial::new(usize::MAX, 0);
    let mut w = Writer::to_writer(o.clone(), false).unwrap();
    w.write_octets("blob", &vec![1; bovnar::writer::DEFAULT_MEM_CAP + 1000])
        .unwrap();
    w.finish().unwrap();
    assert!(o.bytes().len() > bovnar::writer::DEFAULT_MEM_CAP);

    let o = Partial::new(usize::MAX, 0);
    let mut w = Writer::to_writer_with(o.clone(), 100_000, false, 0, false).unwrap();
    let e = w
        .write_octets("blob", &[1; 200_000])
        .and_then(|()| w.finish())
        .unwrap_err();
    assert_eq!(e.code(), Some(ErrorCode::SinkBufferExhausted), "{e:?}");
    assert!(o.bytes().len() <= 100_000);
    let mut fits = Writer::to_writer_with(Partial::new(9, 0), 100_000, false, 0, false).unwrap();
    fits.write_octets("blob", &[1; 50_000]).unwrap();
    fits.finish().unwrap();
}

#[test]
fn a_failing_writer_is_an_io_error_from_then_on() {
    let broken = |at| {
        let mut o = Partial::new(1000, 0);
        o.fail_at = Some(at);
        o
    };
    let is_broken = |e: &Error| {
        matches!(
            e,
            Error::Io {
                code: ErrorCode::WritingToSink,
                io_kind: io::ErrorKind::BrokenPipe,
                ..
            }
        )
    };
    let value = big_value();
    for at in [0, 10, 70_000] {
        assert!(is_broken(
            &bovnar::write_to(&value, false, broken(at)).unwrap_err()
        ));
        assert!(is_broken(
            &stream::mux_dump_to(&[(1, vec![0; 200_000])], "mux", broken(at)).unwrap_err()
        ));
        assert!(is_broken(
            &stream::frame_documents_to(&[vec![0; 100_000]], broken(at)).unwrap_err()
        ));
        assert!(is_broken(
            &stream::dump_documents_to(std::slice::from_ref(&value), false, broken(at))
                .unwrap_err()
        ));
        assert!(is_broken(
            &stream::embed_document_to(&[b'x'; 100_000], "p", false, broken(at)).unwrap_err()
        ));
    }
    let mut w = Writer::to_writer(broken(10), false).unwrap();
    let first = w.write_octets("blob", &[0; 100_000]).unwrap_err();
    assert!(is_broken(&first), "{first:?}");
    assert_eq!(w.write_bool("b", true).unwrap_err(), first);
    assert_eq!(w.finish().unwrap_err(), first);
    assert_eq!(w.finish().unwrap_err(), first);
}

#[test]
#[cfg_attr(panic = "abort", ignore = "a panic cannot be caught where it aborts")]
fn a_panic_in_the_writer_reaches_the_caller() {
    let panicky = || {
        let mut o = Partial::new(usize::MAX, 0);
        o.panic_at = Some(0);
        o
    };
    let value = big_value();
    let calls: [Box<dyn Fn()>; 4] = [
        Box::new(|| drop(bovnar::write_to(&value, false, panicky()))),
        Box::new(|| drop(stream::mux_dump_to(&[(1, b"x")], "mux", panicky()))),
        Box::new(|| drop(stream::frame_documents_to(&[b"x"], panicky()))),
        Box::new(|| {
            let mut w = Writer::to_writer(panicky(), false).unwrap();
            let _ = w.write_octets("blob", &[0; 100_000]);
        }),
    ];
    for call in calls {
        let payload = std::panic::catch_unwind(std::panic::AssertUnwindSafe(call)).unwrap_err();
        assert_eq!(payload.downcast_ref::<&str>(), Some(&"writer blew up"));
    }
    // The writer that panicked is failed, not corrupted.
    let mut w = Writer::to_writer(panicky(), false).unwrap();
    let r = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        let _ = w.write_octets("blob", &[0; 100_000]);
    }));
    assert!(r.is_err());
    assert!(w.finish().is_err());
    let mut out = Vec::new();
    bovnar::write_to(&value, false, &mut out).unwrap();
    assert_eq!(out, bovnar::to_string(&value, false).unwrap());
}

#[test]
fn a_live_writer_writes_mux_streams_and_embedded_documents() {
    let o = Partial::new(777, 4);
    let mut w = Writer::to_writer(o.clone(), false).unwrap();
    w.write_string("codec", "av1").unwrap();
    w.mux_begin("media").unwrap();
    w.mux_send(1, &[5; 100_000]).unwrap();
    w.mux_send(2, b"two").unwrap();
    w.mux_end().unwrap();
    w.embed_document("payload", b".n = 1;").unwrap();
    w.finish().unwrap();
    let doc = o.bytes();
    assert_eq!(
        stream::mux_load(&doc, 0, Some("media")).unwrap(),
        [(1, vec![5; 100_000]), (2, b"two".to_vec())]
    );
    let Value::Struct(fields) = parse(&doc).unwrap() else {
        unreachable!()
    };
    assert_eq!(fields[0], ("codec".into(), Value::String("av1".into())));
    let Value::Bytes(inner) = &fields[2].1 else {
        panic!("{fields:?}")
    };
    assert_eq!(
        stream::parse_embedded(inner, false).unwrap(),
        obj(&[("n", Value::Int(1))])
    );
    let mut w = Writer::to_mem(64, false).unwrap();
    assert!(matches!(w.mux_begin("a\0b"), Err(Error::Argument { .. })));
    assert!(matches!(
        w.embed_document("a\0b", b""),
        Err(Error::Argument { .. })
    ));
}
