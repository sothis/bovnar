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
//! preamble included, and `stream::mux_load` (the C demultiplexer) must read
//! what the codec wrote. The frame vectors are read by `codec::FrameReader`
//! and must come out of `codec::write_frame` byte for byte.

use std::path::{Path, PathBuf};

use bovnar::codec::{self, FrameReader, MuxReader, MuxWriter};
use bovnar::live::{Demux, DemuxItem, DemuxOptions};
use bovnar::{Value, stream};

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
    digest: u64,
    error: String,
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
    assert!(entries.len() >= 6, "the manifest lists the vectors");
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
            let items = rows
                .iter()
                .map(|r| {
                    let Value::Struct(r) = r else {
                        panic!("{name}: a row is not a struct")
                    };
                    let ch = if is_mux { int(r, "channel") } else { 0 };
                    (ch, int(r, "length"), hex(r, "fnv"))
                })
                .collect();
            let data = std::fs::read(dir().join(text(&s, "file"))).unwrap();
            assert_eq!(data.len() as u64, int(&s, "size"), "{name}: size");
            Vector {
                is_mux,
                max: if is_mux { int(&s, "max_message") } else { 0 },
                items,
                digest: if is_mux { hex(&s, "digest") } else { 0 },
                error: text(&s, "error"),
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

/// The ways a vector is cut: the end of every piece.
fn cuts(n: usize) -> Vec<(String, Vec<usize>)> {
    let mut out = vec![
        ("whole".to_string(), vec![n]),
        ("a byte at a time".to_string(), (1..=n.max(1)).collect()),
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
                assert_eq!(got, want, "{}: codec::MuxReader ({how})", v.name);
                if want.error == "none" {
                    assert_eq!(preamble.as_deref(), Some(&v.preamble[..]), "{}", v.name);
                }
                let got = read_demux(&v, &parts);
                assert_eq!(got, want, "{}: live::Demux ({how})", v.name);
            } else {
                let got = read_frames(&v, &parts);
                assert_eq!(got, want, "{}: codec::FrameReader ({how})", v.name);
            }
        }
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
