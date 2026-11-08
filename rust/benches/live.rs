// SPDX-License-Identifier: MIT
//! Cost of the fed readers of `bovnar::live` next to the batch functions and
//! the pure-Rust codec: per message for the demux (messages of about 150 B,
//! 30 KB and 400 KB, the last over seven chunks), per document for the feed
//! parser (documents of about the same sizes).
//!
//! `cargo bench --bench live`, ideally pinned to one core
//! (`taskset -c 2 cargo bench --bench live`). Each figure is the median of
//! nine passes, each over at least 32 MiB of messages or 8 MiB of documents,
//! divided by the number of messages or documents. The input is in memory (a few MiB, which stay in the cache
//! as the bytes of a socket read do), so no I/O is counted. "whole" feeds the
//! whole stream at once; "pieces" feeds it in 1460-byte pieces, a TCP
//! segment's payload, so that most messages and documents straddle a feed
//! and are gathered.
//!
//! Demux, per message: `live::Demux` drained with `next` (whole, pieces, and
//! whole with `deliver_events`); `stream::mux_load`, the batch function,
//! which copies every message into a `Vec`; `codec::MuxReader::read`, pure
//! Rust.
//!
//! Feed parser, per document: `live::DocumentFeed` (reset, feed, finish)
//! with a handler that does nothing, whole and in pieces; `Reader::read_mem`
//! with the same handler; `live::ValueFeed`, which builds the values, next to
//! `bovnar::parse`.

use std::hint::black_box;
use std::time::Instant;

use bovnar::codec::MuxReader;
use bovnar::live::{Demux, DemuxItem, DemuxOptions, DocumentFeed, ValueFeed};
use bovnar::{EventData, ReadOptions, Reader, stream};

const SIZES: [usize; 3] = [150, 30_000, 400_000];
const PASSES: usize = 9;
/// Each timed pass runs over at least this many bytes.
const PASS_BYTES: usize = 32 << 20;
/// The input of a pass: a few MiB, in the cache.
const STREAM_BYTES: usize = 3 << 20;
const PIECE: usize = 1460;
/// The feed parser's passes are shorter: a document costs far more per byte
/// than a message.
const FEED_PASS_BYTES: usize = 8 << 20;

/// The median over [`PASSES`] of `pass` run `reps` times, in nanoseconds
/// per item.
fn median(items: usize, reps: usize, mut pass: impl FnMut()) -> f64 {
    pass();
    let mut times: Vec<f64> = (0..PASSES)
        .map(|_| {
            let t = Instant::now();
            for _ in 0..reps {
                pass();
            }
            t.elapsed().as_nanos() as f64 / (items * reps) as f64
        })
        .collect();
    times.sort_by(f64::total_cmp);
    times[PASSES / 2]
}

fn main() {
    if std::env::args().any(|a| a == "--list") {
        println!("live: bench");
        return;
    }
    demux_table();
    println!();
    feed_table();
}

/// Drains `data` through `demux`, fed in pieces of `piece` bytes; the
/// number of messages.
fn drain(demux: &mut Demux, data: &[u8], piece: usize) -> usize {
    let mut n = 0;
    for chunk in data.chunks(piece) {
        let mut input = chunk;
        while let Some(item) = demux.next(&mut input).expect("a valid stream") {
            if let DemuxItem::Message { payload, .. } = item {
                black_box(payload);
                n += 1;
            }
        }
    }
    demux.finish().expect("a complete stream");
    while demux.next(&mut &[][..]).expect("closing events").is_some() {}
    demux.reset().expect("the next document");
    n
}

fn demux_table() {
    println!("demux, ns per message:");
    println!(
        "{:>10} {:>9} {:>11} {:>11} {:>11} {:>11} {:>11} {:>11}",
        "size", "messages", "fed whole", "fed pieces", "+events", "mux_load", "codec", "codec pcs"
    );
    let plain = DemuxOptions::default();
    let events = DemuxOptions {
        deliver_events: true,
        ..DemuxOptions::default()
    };
    for size in SIZES {
        let n = (STREAM_BYTES / size).max(8);
        let reps = (PASS_BYTES / (n * size)).max(1);
        let messages: Vec<(u64, Vec<u8>)> = (0..n)
            .map(|i| ((i % 4) as u64, vec![i as u8; size]))
            .collect();
        let mut data = b".meta = { .codec = \"av1\"; .rate = 30; };\n".to_vec();
        data.extend(stream::mux_dump(&messages, "mux").expect("a valid key"));

        let mut demux = Demux::new(&plain).expect("a demux");
        let whole = median(n, reps, || {
            assert_eq!(drain(&mut demux, &data, data.len()), n)
        });
        let pieces = median(n, reps, || assert_eq!(drain(&mut demux, &data, PIECE), n));
        let mut with_events = Demux::new(&events).expect("a demux");
        let evented = median(n, reps, || {
            assert_eq!(drain(&mut with_events, &data, data.len()), n)
        });
        let batch = median(n, reps, || {
            black_box(stream::mux_load(&data, 0, None).expect("a valid stream"));
        });
        // The codec reads a dedicated stream: the same messages without the
        // metadata, which it would check with the full parser once.
        let bare = stream::mux_dump(&messages, "mux").expect("a valid key");
        let mut reader = MuxReader::new();
        let mut codec = |piece: usize| {
            reader.reset();
            let mut count = 0;
            for chunk in bare.chunks(piece) {
                let mut input = chunk;
                while let Some(m) = reader.read(&mut input).expect("a valid stream") {
                    black_box(m.payload);
                    count += 1;
                }
            }
            assert_eq!(count, n);
        };
        let codec_whole = median(n, reps, || codec(bare.len()));
        let codec_pieces = median(n, reps, || codec(PIECE));
        println!(
            "{size:>10} {n:>9} {whole:>11.1} {pieces:>11.1} {evented:>11.1} {batch:>11.1} \
             {codec_whole:>11.1} {codec_pieces:>11.1}"
        );
    }
}

/// A document of about `size` bytes: a struct of integers and strings per
/// line, and a 4 KiB octet stream after every 512 lines.
fn document(size: usize) -> Vec<u8> {
    let mut doc = Vec::new();
    let mut i = 0;
    while doc.len() < size {
        doc.extend_from_slice(
            format!(".r{i} = {{ .n = {i}; .s = \"frame {i}\"; .flag = true; }};\n").as_bytes(),
        );
        if i % 512 == 511 {
            doc.extend_from_slice(format!(".b{i} = ").as_bytes());
            doc.extend_from_slice(&[0x00, 0x01, 0x00, 0x10]);
            doc.extend(std::iter::repeat_n(0x5A, 4096));
            doc.extend_from_slice(b"\x00;\n");
        }
        i += 1;
    }
    doc
}

fn feed_table() {
    println!("feed parser, ns per document (MB/s):");
    println!(
        "{:>10} {:>7} {:>17} {:>17} {:>17} {:>17} {:>17}",
        "size", "docs", "fed whole", "fed pieces", "read_mem", "ValueFeed", "parse"
    );
    let options = ReadOptions::default();
    for size in SIZES {
        let doc = document(size);
        let docs = (STREAM_BYTES / doc.len()).max(4);
        let reps = (FEED_PASS_BYTES / (docs * doc.len())).max(1);
        let mut nothing = |d: &EventData<'_>| {
            black_box(d.raw);
            true
        };
        let mut feed = DocumentFeed::new(&options).expect("a feed");
        let mut fed = |piece: usize| {
            for _ in 0..docs {
                feed.reset().expect("the next document");
                for chunk in doc.chunks(piece) {
                    feed.feed(chunk, &mut nothing).expect("a valid document");
                }
                feed.finish(&mut nothing).expect("a complete document");
            }
        };
        let whole = median(docs, reps, || fed(doc.len()));
        let pieces = median(docs, reps, || fed(PIECE));
        let mut reader = Reader::new().expect("a reader");
        let mut nothing = |d: &EventData<'_>| {
            black_box(d.raw);
            true
        };
        let batch = median(docs, reps, || {
            for _ in 0..docs {
                reader
                    .read_mem(&doc, &options, Some(&mut nothing), None)
                    .expect("a valid document");
            }
        });
        let mut values = ValueFeed::new(false, &options).expect("a feed");
        let mut out = Vec::new();
        let valued = median(docs, reps, || {
            for _ in 0..docs {
                values.reset().expect("the next document");
                values.feed(&doc, &mut out).expect("a valid document");
                values.finish(&mut out).expect("a complete document");
                black_box(&out);
                out.clear();
            }
        });
        let parsed = median(docs, reps, || {
            for _ in 0..docs {
                black_box(bovnar::parse(&doc).expect("a valid document"));
            }
        });
        let cell = |ns: f64| format!("{ns:.0} ({:.0})", doc.len() as f64 / ns * 1e3);
        println!(
            "{:>10} {docs:>7} {:>17} {:>17} {:>17} {:>17} {:>17}",
            doc.len(),
            cell(whole),
            cell(pieces),
            cell(batch),
            cell(valued),
            cell(parsed)
        );
    }
}
