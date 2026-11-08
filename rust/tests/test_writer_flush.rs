// SPDX-License-Identifier: MIT
//! `Writer::flush` (`bvnr_write_flush`): a writer into a `std::io::Write`
//! hands over what it has written when asked, without changing a byte of the
//! document, and a live reader at the other end of a socket has each message
//! before the next is sent.

use std::io::{self, Write};
use std::sync::{Arc, Mutex};

use bovnar::live::{Demux, DemuxItem, DemuxOptions, ValueBuilder};
use bovnar::units::UnitFlags;
use bovnar::writer::{EmitSpec, Signedness};
use bovnar::{
    Base, Error, ErrorCode, Event, Quantity, Unit, ValueTypeFamily, ValueTypeSpec, Writer,
};

/// A destination that keeps what it is given and counts its flushes, and
/// can be told to refuse writes or flushes.
#[derive(Clone, Default)]
struct Shared(Arc<Mutex<State>>);

#[derive(Default)]
struct State {
    out: Vec<u8>,
    flushes: usize,
    refuse_writes: bool,
    refuse_flush: bool,
}

impl Shared {
    fn bytes(&self) -> Vec<u8> {
        self.0.lock().unwrap().out.clone()
    }

    fn len(&self) -> usize {
        self.0.lock().unwrap().out.len()
    }

    fn flushes(&self) -> usize {
        self.0.lock().unwrap().flushes
    }

    fn set(&self, f: impl FnOnce(&mut State)) {
        f(&mut self.0.lock().unwrap());
    }
}

impl Write for Shared {
    fn write(&mut self, buf: &[u8]) -> io::Result<usize> {
        let mut s = self.0.lock().unwrap();
        if s.refuse_writes {
            return Err(io::Error::new(io::ErrorKind::BrokenPipe, "peer gone"));
        }
        // Short writes on purpose: the push must continue them.
        let n = buf.len().min(10_000);
        s.out.extend_from_slice(&buf[..n]);
        Ok(n)
    }

    fn flush(&mut self) -> io::Result<()> {
        let mut s = self.0.lock().unwrap();
        if s.refuse_flush {
            return Err(io::Error::new(io::ErrorKind::TimedOut, "flush refused"));
        }
        s.flushes += 1;
        Ok(())
    }
}

fn vt(family: ValueTypeFamily, width: u32, base: u32) -> ValueTypeSpec {
    ValueTypeSpec {
        family: family as i32,
        width,
        base,
    }
}

type Step = Box<dyn Fn(&mut Writer) -> bovnar::Result<()>>;

/// One public writer call per step, of every kind: a version directive,
/// scalars of every family, a struct, arrays of one and two dimensions, octets,
/// an embedded document, a multiplexed stream with values around it.
fn script() -> Vec<Step> {
    let u8 = vt(ValueTypeFamily::Uint, 8, 0);
    let km = Unit::parse("k~m").unwrap();
    let q = Quantity::new("2.5", vt(ValueTypeFamily::Float, 64, 0), km);
    let big = vec![0x5Au8; 150_000];
    let frame = vec![0xC3u8; 70_000];
    vec![
        Box::new(|w| w.write_version(1, 2)),
        Box::new(|w| w.write_uint("u", 255, 64, Base::HEX)),
        Box::new(|w| w.write_sint("i", -3, 32, Base::DECIMAL)),
        Box::new(|w| w.write_float("f", 1.5, 64)),
        Box::new(|w| w.write_float_fix("fx", 1.5, 32, 16)),
        Box::new(|w| w.write_float_dec("fd", 0.1, 64)),
        Box::new(|w| w.write_bvni("bi", -(1i128 << 100), 102, Base::DECIMAL, Signedness::Auto)),
        Box::new(|w| w.write_bvnf_base("hx", "1.8p1", 64, Base::HEX)),
        Box::new(move |w| w.write_quantity("q", &q)),
        Box::new(|w| w.write_string("s", "caf\u{e9}")),
        Box::new(|w| w.write_bool("b", true)),
        Box::new(|w| w.write_null("n")),
        Box::new(|w| w.begin_struct("meta")),
        Box::new(|w| w.write_string("codec", "av1")),
        Box::new(|w| w.begin_struct("inner")),
        Box::new(|w| w.write_uint("depth", 2, 8, Base::DECIMAL)),
        Box::new(|w| w.end_struct()),
        Box::new(|w| w.end_struct()),
        // `.row = [1, 2, 3];`, one call per event.
        Box::new(|w| w.emit(Event::AssignmentStart, EmitSpec::key("row"))),
        Box::new(|w| w.begin_array_row()),
        Box::new(move |w| w.emit(Event::Data, EmitSpec::value("1", u8))),
        Box::new(move |w| w.emit(Event::Data, EmitSpec::value("2", u8))),
        Box::new(move |w| w.emit(Event::Data, EmitSpec::value("3", u8))),
        Box::new(|w| w.end_array_row()),
        // `.mat = <uint:8,k~m> [1, 2] / [3, 4];`
        Box::new(|w| w.emit(Event::AssignmentStart, EmitSpec::key("mat"))),
        Box::new(move |w| w.annotation(u8, &km, None)),
        Box::new(|w| w.begin_array_row()),
        Box::new(move |w| w.emit(Event::Data, EmitSpec::value("1", u8))),
        Box::new(move |w| w.emit(Event::Data, EmitSpec::value("2", u8))),
        Box::new(|w| w.end_array_row()),
        Box::new(|w| w.new_array_dim()),
        Box::new(|w| w.begin_array_row()),
        Box::new(move |w| w.emit(Event::Data, EmitSpec::value("3", u8))),
        Box::new(move |w| w.emit(Event::Data, EmitSpec::value("4", u8))),
        Box::new(|w| w.end_array_row()),
        Box::new(move |w| w.write_octets("blob", &big)),
        Box::new(|w| w.embed_document("doc", b".n = 1; .s = { .t = \"x\"; };")),
        Box::new(|w| w.mux_begin("media")),
        Box::new(|w| w.mux_send(1, b"key frame")),
        Box::new(|w| w.mux_send(2, b"")),
        Box::new(move |w| w.mux_send(1, &frame)),
        Box::new(|w| w.mux_send(4097, b"audio")),
        Box::new(|w| w.mux_end()),
        Box::new(|w| w.write_bool("complete", true)),
    ]
}

/// The script written into `w`, flushed after each step `flush_after` picks;
/// every flush must leave nothing behind in a `Shared` destination.
fn run(mut w: Writer, out: Option<&Shared>, flush_after: impl Fn(usize) -> bool) -> Writer {
    for (i, step) in script().iter().enumerate() {
        step(&mut w).unwrap();
        if flush_after(i) {
            w.flush().unwrap();
            if let Some(o) = out {
                assert_eq!(o.len() as u64, w.bytes_written(), "held back at step {i}");
            }
        }
    }
    w.finish().unwrap();
    w
}

fn to_shared(pretty: bool) -> (Writer, Shared) {
    let o = Shared::default();
    let w =
        Writer::to_writer_with(o.clone(), usize::MAX, pretty, UnitFlags::ASCII_EXP, false).unwrap();
    (w, o)
}

#[test]
fn flushing_anywhere_changes_no_byte() {
    let steps = script().len();
    for pretty in [false, true] {
        let mem = Writer::to_mem_with(1 << 20, pretty, UnitFlags::ASCII_EXP, false).unwrap();
        let reference = run(mem, None, |_| false).get_output().to_vec();
        assert!(reference.len() > 200_000);

        let flushed = Writer::to_mem_with(1 << 20, pretty, UnitFlags::ASCII_EXP, false).unwrap();
        assert_eq!(run(flushed, None, |_| true).get_output(), reference);

        let (w, never) = to_shared(pretty);
        run(w, Some(&never), |_| false);
        assert_eq!(never.bytes(), reference);
        assert_eq!(never.flushes(), 1, "finish flushes once");

        let (w, every) = to_shared(pretty);
        run(w, Some(&every), |_| true);
        assert_eq!(every.bytes(), reference);
        assert_eq!(
            every.flushes(),
            steps + 1,
            "one flush per call, one at finish"
        );

        for k in 0..steps {
            let (w, one) = to_shared(pretty);
            run(w, Some(&one), |i| i == k);
            assert_eq!(one.bytes(), reference, "flushed after step {k}");
            assert_eq!(one.flushes(), 2);
        }
    }
}

#[test]
fn a_flush_before_any_output_and_after_finish() {
    let (mut w, o) = to_shared(false);
    w.flush().unwrap();
    assert_eq!((o.len(), o.flushes()), (0, 1), "nothing to write; flushed");
    w.write_uint("a", 1, 8, Base::DECIMAL).unwrap();
    w.flush().unwrap();
    w.flush().unwrap();
    assert_eq!(o.bytes(), b".a=<uint:8>1");
    assert_eq!(o.flushes(), 3, "io::Write::flush once per call");
    w.finish().unwrap();
    assert_eq!(o.bytes(), b".a=<uint:8>1;");
    let flushes = o.flushes();
    w.flush().unwrap();
    assert_eq!(o.flushes(), flushes, "after finish a flush does nothing");
}

#[test]
fn a_memory_writer_flush_is_a_no_op() {
    let mut w = Writer::to_mem(4096, false).unwrap();
    w.flush().unwrap();
    w.write_string("a", "x").unwrap();
    let before = (w.bytes_written(), w.get_output().to_vec());
    w.flush().unwrap();
    assert_eq!((w.bytes_written(), w.get_output().to_vec()), before);
    w.begin_struct("s").unwrap();
    w.flush().unwrap();
    w.end_struct().unwrap();
    w.finish().unwrap();
    w.flush().unwrap();
    assert_eq!(w.get_output(), b".a=\"x\";.s={};");

    // A full memory writer: the flush reports the write's error.
    let mut w = Writer::to_mem(4, false).unwrap();
    assert!(w.write_string("key", "too long").is_err());
    let e = w.flush().unwrap_err();
    assert_eq!(e.code(), Some(ErrorCode::SinkBufferExhausted), "{e:?}");
}

#[test]
fn a_refused_write_is_an_io_error_from_flush() {
    let (mut w, o) = to_shared(false);
    w.write_string("a", "x").unwrap();
    o.set(|s| s.refuse_writes = true);
    let e = w.flush().unwrap_err();
    assert!(
        matches!(
            &e,
            Error::Io { code: ErrorCode::WritingToSink, io_kind: io::ErrorKind::BrokenPipe, message }
                if message == "peer gone"
        ),
        "{e:?}"
    );
    assert_eq!(o.flushes(), 0, "no io::Write::flush after a failed write");
    // The error latches: every later call reports it, and nothing is written.
    o.set(|s| s.refuse_writes = false);
    assert_eq!(w.flush().unwrap_err(), e);
    assert_eq!(w.write_bool("b", true).unwrap_err(), e);
    assert_eq!(w.finish().unwrap_err(), e);
    assert!(o.bytes().is_empty());
}

#[test]
fn a_refused_flush_is_an_io_error_from_flush() {
    let (mut w, o) = to_shared(false);
    w.write_string("a", "x").unwrap();
    o.set(|s| s.refuse_flush = true);
    let e = w.flush().unwrap_err();
    assert!(
        matches!(
            &e,
            Error::Io { code: ErrorCode::WritingToSink, io_kind: io::ErrorKind::TimedOut, message }
                if message == "flush refused"
        ),
        "{e:?}"
    );
    assert_eq!(
        o.bytes(),
        b".a=\"x\"",
        "the bytes were written before the flush"
    );
    o.set(|s| s.refuse_flush = false);
    assert_eq!(w.flush().unwrap_err(), e);
    assert_eq!(w.finish().unwrap_err(), e);
}

#[test]
fn a_flush_past_the_cap_is_sink_buffer_exhausted() {
    let o = Shared::default();
    let mut w = Writer::to_writer_with(o.clone(), 10, false, 0, false).unwrap();
    w.write_string("a", "a value longer than ten bytes")
        .unwrap();
    let e = w.flush().unwrap_err();
    assert_eq!(e.code(), Some(ErrorCode::SinkBufferExhausted), "{e:?}");
    assert!(o.bytes().is_empty());
}

#[cfg(unix)]
#[test]
fn a_demux_across_a_socket_has_message_n_before_n_plus_1_is_sent() {
    use std::io::Read;
    use std::os::unix::net::UnixStream;

    /// Reads what has arrived on `rx` without waiting for more.
    fn drain(rx: &mut UnixStream, into: &mut Vec<u8>) {
        let mut buf = [0u8; 8192];
        loop {
            match rx.read(&mut buf) {
                Ok(0) => return,
                Ok(n) => into.extend_from_slice(&buf[..n]),
                Err(e) if e.kind() == io::ErrorKind::WouldBlock => return,
                Err(e) => panic!("{e}"),
            }
        }
    }

    const LENS: [usize; 10] = [150, 0, 1, 3000, 65_516, 65_536, 70_000, 150, 120_000, 7];
    let payload: Vec<u8> = (0..120_016u32)
        .map(|i| (i.wrapping_mul(2_654_435_761) >> 9) as u8)
        .collect();
    for pretty in [false, true] {
        let (tx, mut rx) = UnixStream::pair().unwrap();
        rx.set_nonblocking(true).unwrap();
        let mut w = Writer::to_writer(tx, pretty).unwrap();
        let options = DemuxOptions {
            key: Some("media".into()),
            deliver_events: true,
            ..DemuxOptions::default()
        };
        let mut demux = Demux::new(&options).unwrap();
        let mut meta = ValueBuilder::new(false);
        let mut wire = Vec::new();
        let mut got: Vec<(u64, Vec<u8>)> = Vec::new();
        let mut pump =
            |wire: &mut Vec<u8>, got: &mut Vec<(u64, Vec<u8>)>, meta: &mut ValueBuilder| {
                wire.clear();
                drain(&mut rx, wire);
                let mut input = &wire[..];
                while let Some(item) = demux.next(&mut input).unwrap() {
                    match item {
                        DemuxItem::Event(e) => meta.push(&e.data()),
                        DemuxItem::Message { channel, payload } => {
                            got.push((channel, payload.to_vec()));
                        }
                    }
                }
            };

        w.begin_struct("meta").unwrap();
        w.write_string("codec", "av1").unwrap();
        w.end_struct().unwrap();
        w.mux_begin("media").unwrap();
        pump(&mut wire, &mut got, &mut meta);
        assert!(wire.is_empty(), "nothing leaves before the flush");
        w.flush().unwrap();
        pump(&mut wire, &mut got, &mut meta);
        let keys: Vec<String> = meta.drain().map(|(k, _)| k).collect();
        assert_eq!(
            keys,
            ["meta"],
            "the metadata is there before the first message"
        );

        for (m, &len) in LENS.iter().enumerate() {
            let sent = &payload[m % 13..m % 13 + len];
            w.mux_send(m as u64 % 3, sent).unwrap();
            if m == 0 {
                pump(&mut wire, &mut got, &mut meta);
                assert!(got.is_empty(), "a small message waits for the flush");
            }
            w.flush().unwrap();
            pump(&mut wire, &mut got, &mut meta);
            assert_eq!(
                got.len(),
                m + 1,
                "message {m} arrived before the next is sent"
            );
            assert_eq!(
                got[m],
                (m as u64 % 3, sent.to_vec()),
                "message {m} unchanged"
            );
        }
        w.mux_end().unwrap();
        w.finish().unwrap();
        drop(w);
        pump(&mut wire, &mut got, &mut meta);
        demux.finish().unwrap();
        assert_eq!(got.len(), LENS.len());
    }
}
