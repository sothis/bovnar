// SPDX-License-Identifier: MIT
//! Arbitrary bytes through the fed readers of `bovnar::live`, held to the
//! batch functions: fed in pieces, a document gives the events and the error
//! `Reader::read_mem` gives for the whole of it, a multiplexed document the
//! messages and the error of `stream::mux_load`, and a frame stream the
//! documents of `codec::FrameReader` and the verdict of
//! `stream::load_documents`. None of them may crash, whatever the input.
//!
//! The first byte picks the input's shape and seeds the cuts: a raw
//! document, an octet region behind `.mux=` (with a value before it and one
//! after it), chunks framed from the bytes (so the demultiplexer's varint and
//! reassembly paths are reached), or a frame stream.
#![no_main]

use bovnar::codec::FrameReader;
use bovnar::live::{
    Demux, DemuxItem, DemuxOptions, DocumentFeed, FrameParser, FramePull, ValueFeed,
};
use bovnar::{EventData, EventPayload, ReadOptions, Reader};
use libfuzzer_sys::fuzz_target;

type Msgs = Vec<(u64, Vec<u8>)>;

fuzz_target!(|data: &[u8]| {
    let Some((&mode, rest)) = data.split_first() else {
        return;
    };
    match mode % 4 {
        0 => document(rest, mode),
        1 => demux(
            &[&b".meta = 1;\n.mux=\0"[..], rest, b"\0;\n.after = 2;"].concat(),
            mode,
        ),
        2 => demux(&chunked(rest), mode),
        _ => frames(rest, mode),
    }
});

/// `.mux=` and an octet stream whose chunks are cut from `bytes`: each
/// chunk's length is taken from the byte before it.
fn chunked(mut bytes: &[u8]) -> Vec<u8> {
    let mut doc = b".mux=\0".to_vec();
    while let Some((&n, rest)) = bytes.split_first() {
        let n = (usize::from(n) % 48 + 1).min(rest.len().max(1));
        let (chunk, rest) = rest.split_at(n.min(rest.len()));
        if chunk.is_empty() {
            break;
        }
        doc.push(1);
        doc.extend_from_slice(&(chunk.len() as u16).to_le_bytes());
        doc.extend_from_slice(chunk);
        bytes = rest;
    }
    doc.extend_from_slice(b"\0;");
    doc
}

/// Piece lengths from 1 to 16, varied by `seed`.
fn cut(data: &[u8], seed: u8) -> Vec<&[u8]> {
    let mut out = Vec::new();
    let mut rest = data;
    let mut x = u32::from(seed) | 1;
    while !rest.is_empty() {
        x = x.wrapping_mul(1_103_515_245).wrapping_add(12_345);
        let n = ((x >> 16) as usize % 16 + 1).min(rest.len());
        out.push(&rest[..n]);
        rest = &rest[n..];
    }
    out
}

fn options(seed: u8) -> ReadOptions {
    ReadOptions {
        continue_on_error: seed & 0x10 != 0,
        max_file_size: if seed & 0x20 != 0 { 64 } else { 0 },
        ..ReadOptions::default()
    }
}

fn document(doc: &[u8], seed: u8) {
    let options = options(seed);
    let mut whole = Vec::new();
    let mut on = |d: &EventData<'_>| {
        whole.push(EventPayload::from(d));
        true
    };
    let want = Reader::new()
        .unwrap()
        .read_mem(doc, &options, Some(&mut on), None);

    let mut feed = DocumentFeed::new(&options).unwrap();
    let mut events = Vec::new();
    let mut on = |d: &EventData<'_>| {
        events.push(EventPayload::from(d));
        true
    };
    let mut got = Ok(());
    for part in cut(doc, seed) {
        got = feed.feed(part, &mut on);
        if got.is_err() {
            break;
        }
    }
    if got.is_ok() {
        got = feed.finish(&mut on);
    }
    assert_eq!(got, want, "result");
    assert_eq!(events, whole, "events");

    // The values, assignment by assignment: whatever they are, the verdict is
    // the reader's.
    let mut values = ValueFeed::new(seed & 1 != 0, &options).unwrap();
    let mut out = Vec::new();
    let mut r = Ok(());
    for part in cut(doc, seed.wrapping_add(1)) {
        r = values.feed(part, &mut out);
        if r.is_err() {
            break;
        }
    }
    if r.is_ok() {
        r = values.finish(&mut out);
    }
    assert_eq!(r.is_ok(), want.is_ok(), "values");
}

fn demux(doc: &[u8], seed: u8) {
    let options = DemuxOptions {
        max_message: if seed & 0x40 != 0 { 32 } else { 0 },
        deliver_events: seed & 0x80 != 0,
        ..DemuxOptions::default()
    };
    let want = bovnar::stream::mux_load(doc, options.max_message, None);
    let mut dm = Demux::new(&options).unwrap();
    let got = (|| -> bovnar::Result<Msgs> {
        let mut msgs = Vec::new();
        for part in cut(doc, seed) {
            let mut input = part;
            while let Some(item) = dm.next(&mut input)? {
                if let DemuxItem::Message { channel, payload } = item {
                    msgs.push((channel, payload.to_vec()));
                }
            }
        }
        dm.finish()?;
        while dm.next(&mut &[][..])?.is_some() {}
        Ok(msgs)
    })();
    assert_eq!(got, want);
}

fn frames(data: &[u8], seed: u8) {
    const MAX: u64 = 1 << 16;
    let mut codec = FrameReader::with_max_document_size(MAX);
    let mut pull = FramePull::new(MAX).unwrap();
    let mut from_codec: Result<Vec<Vec<u8>>, _> = Ok(Vec::new());
    let mut from_pull: bovnar::Result<Vec<Vec<u8>>> = Ok(Vec::new());
    for part in cut(data, seed) {
        if let Ok(docs) = &mut from_codec {
            let mut input = part;
            loop {
                match codec.read(&mut input) {
                    Ok(Some(d)) => docs.push(d.to_vec()),
                    Ok(None) => break,
                    Err(e) => {
                        from_codec = Err(e);
                        break;
                    }
                }
            }
        }
        if let Ok(docs) = &mut from_pull {
            let mut input = part;
            loop {
                match pull.next(&mut input) {
                    Ok(Some(d)) => docs.push(d.to_vec()),
                    Ok(None) => break,
                    Err(e) => {
                        from_pull = Err(e);
                        break;
                    }
                }
            }
        }
    }
    let from_codec = from_codec.and_then(|d| codec.finish().map(|()| d));
    let from_pull = from_pull.and_then(|d| pull.finish().map(|()| d));
    assert_eq!(
        from_pull.as_ref().map_err(|e| e.kind()),
        from_codec.as_ref().map_err(|e| e.kind())
    );

    // Parsed as it arrives, past failed documents: only the framing can
    // fail, as for load_documents.
    let batch = bovnar::stream::load_documents(data, false, true, MAX);
    let mut parser = FrameParser::new(&ReadOptions::default(), true, MAX).unwrap();
    let mut seen = 0u64;
    let mut ok = Ok(());
    for part in cut(data, seed.wrapping_add(1)) {
        ok = parser.feed(part, &mut |_| true, &mut |_, _| {
            seen += 1;
            true
        });
        if ok.is_err() {
            break;
        }
    }
    if ok.is_ok() {
        ok = parser.finish();
    }
    assert_eq!(ok.is_ok(), batch.is_ok());
    if let Ok(docs) = batch {
        assert_eq!(seen, docs.len() as u64);
    }
}
