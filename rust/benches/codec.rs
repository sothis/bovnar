// SPDX-License-Identifier: MIT
//! Per-message cost of the pure-Rust codec (`bovnar::codec`) next to the
//! batch functions of `bovnar::stream`, for messages of about 150 B, 30 KB
//! and 400 KB (the last spans seven chunks).
//!
//! `cargo bench --bench codec`, ideally pinned to one core
//! (`taskset -c 2 cargo bench --bench codec`). Each figure is the median of
//! nine passes, each over at least 64 MiB of messages, divided by the number
//! of messages. The stream is in memory, so no I/O is counted: once a few
//! MiB that stay in the cache, as the bytes of a socket read are, and once a
//! stream far larger than the cache.
//!
//! - write: `MuxWriter::encode`, the chunk headers into a reused buffer and
//!   the parts handed out for a vectored write (the payload is not touched);
//! - write+copy: `MuxWriter::write_message` into a reused `Vec`;
//! - peek+skip: `MuxReader::next_item`, the peek, and `Peek::skip`;
//! - forward: the same with `Peek::forward`, the wire bytes borrowed;
//! - read: `MuxReader::read`; a message that spans chunks is gathered (copied);
//! - mux_dump, mux_load: the batch functions, through the C library.

use std::hint::black_box;
use std::time::Instant;

use bovnar::codec::{Item, MuxReader, MuxWriter};
use bovnar::stream;

const SIZES: [usize; 3] = [150, 30_000, 400_000];
const PASSES: usize = 9;
/// Each timed pass runs over at least this many bytes, so that a short
/// stream is timed over many repetitions.
const PASS_BYTES: usize = 64 << 20;

/// The median over [`PASSES`] of `pass` run `reps` times, in nanoseconds
/// per message.
fn median(messages: usize, reps: usize, mut pass: impl FnMut()) -> f64 {
    pass();
    let mut times: Vec<f64> = (0..PASSES)
        .map(|_| {
            let t = Instant::now();
            for _ in 0..reps {
                pass();
            }
            t.elapsed().as_nanos() as f64 / (messages * reps) as f64
        })
        .collect();
    times.sort_by(f64::total_cmp);
    times[PASSES / 2]
}

fn main() {
    // `cargo test` runs benches with --bench only when asked; a plain
    // `cargo bench -- --list` or a filter that is not ours does nothing.
    if std::env::args().any(|a| a == "--list") {
        println!("codec: bench");
        return;
    }
    // In cache: a stream of a few MiB, as the bytes of a socket read are
    // when the reader sees them. From memory: a stream far larger than the
    // cache, so that every chunk header is a cache miss (a 400 KB message
    // has seven, 64 KiB apart).
    table("in cache", 3 << 20, true);
    table("from memory", 256 << 20, false);
}

fn table(label: &str, stream_bytes: usize, batch: bool) {
    let mux = MuxWriter::new("mux").expect("a valid key");
    println!("{label}:");
    println!(
        "{:>10} {:>9} {:>11} {:>11} {:>11} {:>11} {:>11} {:>11} {:>11}",
        "size",
        "messages",
        "write",
        "write+copy",
        "peek+skip",
        "forward",
        "read",
        "mux_dump",
        "mux_load"
    );
    for size in SIZES {
        let n = (stream_bytes / size).max(8);
        let reps = (PASS_BYTES / (n * size)).max(1);
        let payloads: Vec<Vec<u8>> = (0..4).map(|i| vec![i as u8; size]).collect();
        let channel = |i: usize| (i % 4) as u64;

        let mut head = Vec::new();
        let write = median(n, reps, || {
            for i in 0..n {
                for part in mux.encode(&mut head, channel(i), &payloads[i % 4]) {
                    black_box(part);
                }
            }
        });

        let mut out = Vec::new();
        mux.begin(&mut out);
        for i in 0..n {
            mux.write_message(&mut out, channel(i), &payloads[i % 4]);
        }
        mux.finish(&mut out);
        let data = out;
        let mut scratch = Vec::with_capacity(data.len());
        let write_copy = median(n, reps, || {
            scratch.clear();
            for i in 0..n {
                mux.write_message(&mut scratch, channel(i), &payloads[i % 4]);
            }
            black_box(&scratch);
        });

        let mut reader = MuxReader::new();
        let mut route = |forward: bool| {
            reader.reset();
            let mut input = &data[..];
            let mut count = 0usize;
            while let Some(item) = reader.next_item(&mut input, 4).expect("a valid stream") {
                match item {
                    Item::Peek(p) if forward => p.forward(),
                    Item::Peek(p) => {
                        black_box(p.head);
                        p.skip();
                    }
                    Item::Wire(w) => {
                        black_box(w.bytes);
                        count += 1;
                    }
                    Item::Message(_) => unreachable!("every message is decided"),
                }
            }
            assert!(reader.is_done());
            assert_eq!(count, if forward { n } else { 0 });
        };
        let peek = median(n, reps, || route(false));
        let forward = median(n, reps, || route(true));

        let mut reader = MuxReader::new();
        let read = median(n, reps, || {
            reader.reset();
            let mut input = &data[..];
            let mut count = 0usize;
            while let Some(m) = reader.read(&mut input).expect("a valid stream") {
                black_box(m.payload);
                count += 1;
            }
            assert_eq!(count, n);
        });

        // The batch functions build or return everything at once: fewer
        // passes over a smaller stream keep the run short.
        if !batch {
            println!(
                "{size:>10} {n:>9} {write:>9.1}ns {write_copy:>9.1}ns {peek:>9.1}ns {forward:>9.1}ns \
                 {read:>9.1}ns"
            );
            continue;
        }
        let m = n.min(4096);
        let msgs: Vec<(u64, &[u8])> = (0..m).map(|i| (channel(i), &payloads[i % 4][..])).collect();
        let dump = median(m, 1, || {
            black_box(stream::mux_dump(&msgs, "mux").expect("valid messages"));
        });
        let batch = stream::mux_dump(&msgs, "mux").expect("valid messages");
        let load = median(m, 1, || {
            black_box(stream::mux_load(&batch, 0, None).expect("a valid stream"));
        });

        println!(
            "{size:>10} {n:>9} {write:>9.1}ns {write_copy:>9.1}ns {peek:>9.1}ns {forward:>9.1}ns \
             {read:>9.1}ns {dump:>9.1}ns {load:>9.1}ns"
        );
    }
}
