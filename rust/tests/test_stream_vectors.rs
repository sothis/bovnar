// SPDX-License-Identifier: MIT
//! The shared streaming vectors (`tests/stream_vectors/`, doc/10 §9) against
//! the crate: the bytes `tests/bovnar_stream_vectors_test.c` holds the C
//! library to, read here by the pure-Rust codec and by the C-backed
//! [`bovnar::live::Demux`].
//!
//! Every multiplexed vector is read by `codec::MuxReader` and `live::Demux`,
//! fed whole, a byte at a time, cut in two at every position (sampled
//! positions for the large vectors) and in seeded random pieces; each must
//! hand out the messages the manifest lists and end with its error. Every
//! canonical vector must come out of `codec::MuxWriter` byte for byte, its
//! preamble included, and so must every one without a preamble out of the
//! C-backed `Writer::mux_send`; `stream::mux_load` (the C demultiplexer) must
//! read what the codec wrote. The frame vectors are read by
//! `codec::FrameReader` and `live::FramePull`, cut the same ways, and by
//! `live::FrameParser` and `stream::load_documents` with and without
//! `continue_past_failed`, which must report each document's own outcome; a
//! frame vector that ends cleanly must come out of `codec::write_frame` byte
//! for byte.

use std::path::{Path, PathBuf};

use bovnar::codec::{self, FrameReader, MuxReader, MuxWriter};
use bovnar::live::{Demux, DemuxItem, DemuxOptions, FrameParser, FramePull};
use bovnar::{ReadOptions, Value, Writer, stream};

/// Vectors up to this size are cut in two at every position.
const EVERY_CUT_MAX: usize = 4096;

fn dir() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR"))
        .parent()
        .unwrap()
        .join("tests/stream_vectors")
}

fn fnv(data: &[u8], mut h: u64) -> u64 {
    for &b in data {
        h ^= u64::from(b);
        h = h.wrapping_mul(0x0100_0000_01b3);
    }
    h
}
const FNV_INIT: u64 = 0xcbf2_9ce4_8422_2325;

/// One message or document: channel (0 for a document), length, FNV-1a.
type Item = (u64, u64, u64);

struct Vector {
    name: String,
    data: Vec<u8>,
    is_mux: bool,
    max: u64,
    items: Vec<Item>,
    /// Each document's outcome when parsed on its own (frame vectors).
    doc_errors: Vec<String>,
    digest: u64,
    error: String,
    /// What the codec makes of a document shape it does not read, and how
    /// many of the messages it hands out first.
    codec_error: String,
    codec_messages: usize,
    canonical: bool,
    preamble: Vec<u8>,
}

fn field<'a>(s: &'a [(String, Value)], key: &str) -> &'a Value {
    &s.iter()
        .find(|(k, _)| k == key)
        .unwrap_or_else(|| panic!("manifest: no .{key}"))
        .1
}

fn text(s: &[(String, Value)], key: &str) -> String {
    match field(s, key) {
        Value::String(t) => t.clone(),
        v => panic!("manifest: .{key} is {v:?}"),
    }
}

fn int(s: &[(String, Value)], key: &str) -> u64 {
    match field(s, key) {
        Value::Int(n) => u64::try_from(*n).unwrap(),
        v => panic!("manifest: .{key} is {v:?}"),
    }
}

fn hex(s: &[(String, Value)], key: &str) -> u64 {
    u64::from_str_radix(&text(s, key), 16).unwrap()
}

fn vectors() -> Vec<Vector> {
    let manifest = std::fs::read(dir().join("manifest.bvnr")).unwrap();
    let Value::Struct(entries) = bovnar::parse(&manifest).unwrap() else {
        panic!("the manifest is not a document")
    };
    assert!(entries.len() >= 25, "the manifest lists the vectors");
    entries
        .into_iter()
        .map(|(name, v)| {
            let Value::Struct(s) = v else {
                panic!("{name} is not a struct")
            };
            let is_mux = text(&s, "kind") == "mux";
            let list = if is_mux { "messages" } else { "documents" };
            let Value::Array(rows) = field(&s, list) else {
                panic!("{name}: .{list} is not an array")
            };
            let mut doc_errors = Vec::new();
            let items = rows
                .iter()
                .map(|r| {
                    let Value::Struct(r) = r else {
                        panic!("{name}: a row is not a struct")
                    };
                    let ch = if is_mux { int(r, "channel") } else { 0 };
                    if !is_mux {
                        doc_errors.push(text(r, "error"));
                    }
                    (ch, int(r, "length"), hex(r, "fnv"))
                })
                .collect();
            let data = std::fs::read(dir().join(text(&s, "file"))).unwrap();
            assert_eq!(data.len() as u64, int(&s, "size"), "{name}: size");
            Vector {
                is_mux,
                max: int(
                    &s,
                    if is_mux {
                        "max_message"
                    } else {
                        "max_document_size"
                    },
                ),
                items,
                doc_errors,
                digest: if is_mux { hex(&s, "digest") } else { 0 },
                error: text(&s, "error"),
                codec_error: if is_mux {
                    text(&s, "codec_error")
                } else {
                    text(&s, "error")
                },
                codec_messages: if is_mux {
                    int(&s, "codec_messages") as usize
                } else {
                    0
                },
                canonical: is_mux && matches!(field(&s, "canonical"), Value::Bool(true)),
                preamble: if is_mux {
                    text(&s, "preamble").into_bytes()
                } else {
                    Vec::new()
                },
                data,
                name,
            }
        })
        .collect()
}

/// What a reader handed out, and how it ended.
#[derive(Debug, Default, PartialEq)]
struct Got {
    items: Vec<Item>,
    digest: u64,
    error: String,
}

impl Got {
    fn new() -> Self {
        Got {
            items: Vec::new(),
            digest: FNV_INIT,
            error: "none".into(),
        }
    }

    fn add(&mut self, channel: u64, payload: &[u8]) {
        let len = payload.len() as u64;
        self.items.push((channel, len, fnv(payload, FNV_INIT)));
        let mut le = [0u8; 16];
        le[..8].copy_from_slice(&channel.to_le_bytes());
        le[8..].copy_from_slice(&len.to_le_bytes());
        self.digest = fnv(payload, fnv(&le, self.digest));
    }

    fn fail(&mut self, e: &bovnar::Error) {
        self.error = e.name().unwrap_or("no code").to_string();
    }
}

fn expected(v: &Vector) -> Got {
    Got {
        items: v.items.clone(),
        digest: if v.is_mux { v.digest } else { FNV_INIT },
        error: v.error.clone(),
    }
}

/// What the codec hands out: the first `codec_messages`, then its error.
fn codec_expected(v: &Vector) -> (Vec<Item>, String) {
    (v.items[..v.codec_messages].to_vec(), v.codec_error.clone())
}

/// The ways a vector is cut: the end of every piece.
fn cuts(n: usize) -> Vec<(String, Vec<usize>)> {
    let mut out = vec![
        ("whole".to_string(), vec![n]),
        (
            "a byte at a time".to_string(),
            (1..=n).chain((n == 0).then_some(0)).collect(),
        ),
    ];
    for k in 0..=n {
        let near_chunk = k % 65539 <= 8 || k % 65539 >= 65531;
        if n <= EVERY_CUT_MAX || k % 16381 == 0 || near_chunk {
            out.push((format!("cut at {k}"), vec![k, n]));
        }
    }
    let mut seed: u64 = 0x9E37_79B9_7F4A_7C15;
    for r in 0..8 {
        let max = if r % 2 == 1 { 97 } else { 9000 };
        let mut ends = Vec::new();
        let mut at = 0;
        while at < n {
            seed ^= seed << 13;
            seed ^= seed >> 7;
            seed ^= seed << 17;
            at = (at + 1 + (seed % max) as usize).min(n);
            ends.push(at);
        }
        if ends.is_empty() {
            ends.push(n);
        }
        out.push((format!("random {r}"), ends));
    }
    out
}

fn pieces<'a>(data: &'a [u8], ends: &[usize]) -> Vec<&'a [u8]> {
    let mut at = 0;
    ends.iter()
        .map(|&e| {
            let p = &data[at..e];
            at = e;
            p
        })
        .collect()
}

fn read_codec(v: &Vector, parts: &[&[u8]]) -> (Got, Option<Vec<u8>>) {
    let mut got = Got::new();
    let mut reader = MuxReader::with_options(v.max, Some("mux")).unwrap();
    for part in parts {
        let mut input = *part;
        loop {
            match reader.read(&mut input) {
                Ok(Some(m)) => got.add(m.channel, m.payload),
                Ok(None) => break,
                Err(e) => {
                    got.fail(&e);
                    return (got, None);
                }
            }
        }
    }
    if let Err(e) = reader.finish() {
        got.fail(&e);
    }
    (got, reader.preamble().map(<[u8]>::to_vec))
}

fn read_demux(v: &Vector, parts: &[&[u8]]) -> Got {
    let mut got = Got::new();
    let options = DemuxOptions {
        max_message: v.max,
        key: Some("mux".into()),
        ..DemuxOptions::default()
    };
    let mut demux = Demux::new(&options).unwrap();
    for part in parts {
        let mut input = *part;
        loop {
            match demux.next(&mut input) {
                Ok(Some(DemuxItem::Message { channel, payload })) => got.add(channel, payload),
                Ok(Some(DemuxItem::Event(_))) => {}
                Ok(None) => break,
                Err(e) => {
                    got.fail(&e);
                    return got;
                }
            }
        }
    }
    if let Err(e) = demux.finish() {
        got.fail(&e);
        return got;
    }
    while let Some(item) = demux.next(&mut &[][..]).unwrap() {
        if let DemuxItem::Message { channel, payload } = item {
            got.add(channel, payload);
        }
    }
    got
}

fn read_frames(v: &Vector, parts: &[&[u8]]) -> Got {
    let mut got = Got::new();
    let mut reader = FrameReader::with_max_document_size(v.max);
    for part in parts {
        let mut input = *part;
        loop {
            match reader.read(&mut input) {
                Ok(Some(doc)) => got.add(0, doc),
                Ok(None) => break,
                Err(e) => {
                    got.fail(&e);
                    got.digest = FNV_INIT;
                    return got;
                }
            }
        }
    }
    if let Err(e) = reader.finish() {
        got.fail(&e);
    }
    got.digest = FNV_INIT;
    got
}

fn read_frame_pull(v: &Vector, parts: &[&[u8]]) -> Got {
    let mut got = Got::new();
    let mut reader = FramePull::new(v.max).unwrap();
    for part in parts {
        let mut input = *part;
        loop {
            match reader.next(&mut input) {
                Ok(Some(doc)) => got.add(0, doc),
                Ok(None) => break,
                Err(e) => {
                    got.fail(&e);
                    got.digest = FNV_INIT;
                    return got;
                }
            }
        }
    }
    if let Err(e) = reader.finish() {
        got.fail(&e);
    }
    got.digest = FNV_INIT;
    got
}

/// How a parsing reader ends: the stream's error going on past a failed
/// document; stopping at one, that document's error, after the documents up
/// to and including it. (Per document: its outcome.)
fn parse_expected(v: &Vector, go_on: bool) -> (Vec<String>, String) {
    match v.doc_errors.iter().position(|e| e != "none") {
        Some(i) if !go_on => (v.doc_errors[..=i].to_vec(), v.doc_errors[i].clone()),
        _ => (v.doc_errors.clone(), v.error.clone()),
    }
}

fn outcome(r: &bovnar::Result<()>) -> String {
    match r {
        Ok(()) => "none".into(),
        Err(e) => e.name().unwrap_or("no code").into(),
    }
}

fn read_frame_parser(v: &Vector, go_on: bool, parts: &[&[u8]]) -> (Vec<String>, String) {
    let mut docs = Vec::new();
    let mut parser = FrameParser::new(&ReadOptions::default(), go_on, v.max).unwrap();
    for part in parts {
        let r = parser.feed(part, &mut |_| true, &mut |_, r| {
            docs.push(outcome(&r));
            true
        });
        if r.is_err() {
            return (docs, outcome(&r));
        }
    }
    let end = outcome(&parser.finish());
    (docs, end)
}

/// The messages of a well-read vector, for the writers.
fn messages(v: &Vector) -> Vec<(u64, Vec<u8>)> {
    let mut out = Vec::new();
    let mut reader = MuxReader::new();
    let mut input = &v.data[..];
    while let Some(m) = reader.read(&mut input).unwrap() {
        out.push((m.channel, m.payload.to_vec()));
    }
    reader.finish().unwrap();
    out
}

#[test]
fn every_vector_reads_as_the_manifest_says() {
    for v in vectors() {
        let want = expected(&v);
        for (how, ends) in cuts(v.data.len()) {
            let parts = pieces(&v.data, &ends);
            if v.is_mux {
                let (got, preamble) = read_codec(&v, &parts);
                // The digest is over all the messages: compared where the
                // codec reads them all.
                if v.codec_messages == v.items.len() && v.codec_error == v.error {
                    assert_eq!(got, want, "{}: codec::MuxReader ({how})", v.name);
                } else {
                    assert_eq!(
                        (got.items, got.error),
                        codec_expected(&v),
                        "{}: codec::MuxReader ({how})",
                        v.name
                    );
                }
                if v.codec_error == "none" {
                    assert_eq!(preamble.as_deref(), Some(&v.preamble[..]), "{}", v.name);
                }
                let got = read_demux(&v, &parts);
                assert_eq!(got, want, "{}: live::Demux ({how})", v.name);
            } else {
                let got = read_frames(&v, &parts);
                assert_eq!(got, want, "{}: codec::FrameReader ({how})", v.name);
                let got = read_frame_pull(&v, &parts);
                assert_eq!(got, want, "{}: live::FramePull ({how})", v.name);
                for go_on in [false, true] {
                    assert_eq!(
                        read_frame_parser(&v, go_on, &parts),
                        parse_expected(&v, go_on),
                        "{}: live::FrameParser, continue_past_failed {go_on} ({how})",
                        v.name
                    );
                }
            }
        }
        if !v.is_mux {
            for go_on in [false, true] {
                let (docs, end) = parse_expected(&v, go_on);
                let got = stream::load_documents(&v.data, false, go_on, v.max);
                let want: Vec<bool> = docs.iter().map(|e| e == "none").collect();
                match got {
                    Ok(values) => {
                        assert_eq!(end, "none", "{}: load_documents {go_on}", v.name);
                        let ok: Vec<bool> = values.iter().map(Option::is_some).collect();
                        assert_eq!(ok, want, "{}: load_documents {go_on}", v.name);
                    }
                    Err(e) => assert_eq!(
                        e.name(),
                        Some(&end[..]),
                        "{}: load_documents {go_on}",
                        v.name
                    ),
                }
            }
        }
    }
}

#[test]
fn the_c_backed_writer_writes_the_canonical_vectors_byte_for_byte() {
    for v in vectors()
        .into_iter()
        .filter(|v| v.canonical && v.preamble.is_empty())
    {
        let mut w = Writer::to_mem(v.data.len() + 64, false).unwrap();
        w.mux_begin("mux").unwrap();
        for (c, p) in messages(&v) {
            w.mux_send(c, &p).unwrap();
        }
        w.mux_end().unwrap();
        w.finish().unwrap();
        assert!(w.get_output() == v.data, "{}: Writer::mux_send", v.name);
    }
}

#[test]
fn the_codec_writes_the_canonical_vectors_byte_for_byte() {
    let mut canonical = 0;
    for v in vectors().into_iter().filter(|v| v.canonical) {
        canonical += 1;
        let msgs = messages(&v);
        let mux = MuxWriter::with_preamble(&v.preamble, "mux").unwrap();
        let mut out = Vec::new();
        mux.begin(&mut out);
        for (c, p) in &msgs {
            mux.write_message(&mut out, *c, p);
        }
        mux.finish(&mut out);
        assert!(out == v.data, "{}: MuxWriter::write_message", v.name);

        // The vectored parts and the io::Write form give the same bytes.
        let mut parts = mux.opener().to_vec();
        let mut to = mux.opener().to_vec();
        let mut head = Vec::new();
        for (c, p) in &msgs {
            for part in mux.encode(&mut head, *c, p) {
                parts.extend_from_slice(part);
            }
            mux.write_message_to(&mut to, *c, p).unwrap();
        }
        parts.extend_from_slice(mux.terminator());
        to.extend_from_slice(mux.terminator());
        assert!(parts == v.data, "{}: MuxWriter::encode", v.name);
        assert!(to == v.data, "{}: MuxWriter::write_message_to", v.name);

        // The C demultiplexer reads what the codec wrote.
        assert_eq!(
            stream::mux_load(&out, 0, Some("mux")).unwrap(),
            msgs,
            "{}",
            v.name
        );
    }
    assert!(canonical >= 2, "the canonical vectors are there");
}

#[test]
fn the_codec_writes_the_frame_vectors_byte_for_byte() {
    for v in vectors()
        .into_iter()
        .filter(|v| !v.is_mux && v.error == "none")
    {
        let mut docs = Vec::new();
        let mut reader = FrameReader::new();
        let mut input = &v.data[..];
        while let Some(doc) = reader.read(&mut input).unwrap() {
            docs.push(doc.to_vec());
        }
        let mut out = Vec::new();
        for d in &docs {
            codec::write_frame(&mut out, d);
        }
        assert!(out == v.data, "{}: codec::write_frame", v.name);
        assert_eq!(
            stream::frame_documents(&docs).unwrap(),
            out,
            "{}: bvnr_frame_write",
            v.name
        );
    }
}
