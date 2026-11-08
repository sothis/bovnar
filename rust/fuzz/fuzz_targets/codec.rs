// SPDX-License-Identifier: MIT
//! Arbitrary bytes through the pure-Rust codec (`bovnar::codec`), held to
//! itself and to the C library: the readers may refuse the input but must
//! not crash; fed in pieces they reach the verdict they reach on the whole
//! input; a multiplexed stream `MuxReader` accepts is one the C
//! demultiplexer accepts with the same messages, and so are the chunks it
//! forwards; and `FrameReader` accepts exactly the frame streams
//! `bvnr_doc_stream_read` does.
//!
//! The first byte picks the input's shape: a raw document, an octet region
//! behind `.mux=`, chunks framed from the bytes (so the demultiplexer's
//! varint and reassembly paths are reached), or a frame stream.
#![no_main]

use bovnar::codec::{FrameReader, Item, MuxReader};
use libfuzzer_sys::fuzz_target;

type Msgs = Vec<(u64, Vec<u8>)>;

fuzz_target!(|data: &[u8]| {
    let Some((&mode, rest)) = data.split_first() else {
        return;
    };
    match mode % 4 {
        0 => mux(rest, mode),
        1 => mux(&[&b".mux=\0"[..], rest].concat(), mode),
        2 => mux(&chunked(rest), mode),
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

fn read(parts: &[&[u8]]) -> Result<Msgs, Option<bovnar::ErrorCode>> {
    let mut reader = MuxReader::new();
    let mut got = Vec::new();
    for part in parts {
        let mut input = *part;
        while let Some(m) = reader.read(&mut input).map_err(|e| e.kind())? {
            got.push((m.channel, m.payload.to_vec()));
        }
    }
    reader.finish().map_err(|e| e.kind())?;
    Ok(got)
}

/// Routes every message: received, skipped or forwarded by its channel.
fn route(parts: &[&[u8]]) -> Result<(Msgs, Vec<u8>), Option<bovnar::ErrorCode>> {
    let mut reader = MuxReader::new();
    let mut kept = Vec::new();
    let mut wire = Vec::new();
    for part in parts {
        let mut input = *part;
        while let Some(item) = reader.next_item(&mut input, 3).map_err(|e| e.kind())? {
            match item {
                Item::Peek(p) => match p.channel % 3 {
                    0 => {}
                    1 => p.skip(),
                    _ => p.forward(),
                },
                Item::Message(m) => kept.push((m.channel, m.payload.to_vec())),
                Item::Wire(w) => wire.extend_from_slice(w.bytes),
            }
        }
    }
    reader.finish().map_err(|e| e.kind())?;
    Ok((kept, wire))
}

fn mux(doc: &[u8], seed: u8) {
    let whole = read(&[doc]);
    assert_eq!(read(&cut(doc, seed)), whole, "pieces");
    let routed = route(&cut(doc, seed.wrapping_add(1)));
    assert_eq!(routed.is_ok(), whole.is_ok(), "route");
    let Ok(messages) = whole else {
        return;
    };
    // What the codec accepts, C accepts, and reads the same.
    let c = bovnar::stream::mux_load(doc, 0, None).expect("C refused a stream the codec read");
    assert_eq!(c, messages);
    let (kept, wire) = routed.expect("checked above");
    let expect_kept: Msgs = messages.iter().filter(|(c, _)| c % 3 == 0).cloned().collect();
    assert_eq!(kept, expect_kept);
    let forwarded = bovnar::stream::mux_load(&[&b".m=\0"[..], &wire, b"\0;"].concat(), 0, None)
        .expect("C refused the forwarded chunks");
    let expect_wire: Msgs = messages.into_iter().filter(|(c, _)| c % 3 == 2).collect();
    assert_eq!(forwarded, expect_wire);
}

fn frames(data: &[u8], seed: u8) {
    let read = |parts: &[&[u8]]| -> Result<Vec<Vec<u8>>, Option<bovnar::ErrorCode>> {
        let mut reader = FrameReader::with_max_document_size(1 << 16);
        let mut docs = Vec::new();
        for part in parts {
            let mut input = *part;
            while let Some(doc) = reader.read(&mut input).map_err(|e| e.kind())? {
                docs.push(doc.to_vec());
            }
        }
        reader.finish().map_err(|e| e.kind())?;
        Ok(docs)
    };
    let whole = read(&[data]);
    assert_eq!(read(&cut(data, seed)), whole, "pieces");
    // continue_past_failed: only the framing can fail the C read.
    let c = bovnar::stream::load_documents(data, false, true, 1 << 16);
    assert_eq!(c.as_ref().ok().map(Vec::len), whole.as_ref().ok().map(Vec::len));
}
