// SPDX-License-Identifier: MIT
//! The codec allocates nothing per message once its buffers are warm: a
//! counting global allocator, in a test binary of its own so that no other
//! test's allocations are counted, watches the writer, the reader (whole
//! input and input in pieces, so messages are gathered), peek and skip, and
//! forwarding. The count is per thread, so the test harness's own threads do
//! not disturb it.

use std::alloc::{GlobalAlloc, Layout, System};
use std::cell::Cell;

use bovnar::codec::{FrameReader, Item, MuxReader, MuxWriter, write_frame};

struct Counting;

thread_local! {
    static ALLOCATIONS: Cell<usize> = const { Cell::new(0) };
}

fn count() {
    // try_with: the allocator may run while the thread's locals are torn down.
    let _ = ALLOCATIONS.try_with(|n| n.set(n.get() + 1));
}

// SAFETY: every call is passed straight to the system allocator; the counter
// is a const-initialised thread local, which itself never allocates.
unsafe impl GlobalAlloc for Counting {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        count();
        // SAFETY: the caller's contract is System's.
        unsafe { System.alloc(layout) }
    }

    unsafe fn alloc_zeroed(&self, layout: Layout) -> *mut u8 {
        count();
        // SAFETY: as above.
        unsafe { System.alloc_zeroed(layout) }
    }

    unsafe fn realloc(&self, ptr: *mut u8, layout: Layout, new_size: usize) -> *mut u8 {
        count();
        // SAFETY: as above.
        unsafe { System.realloc(ptr, layout, new_size) }
    }

    unsafe fn dealloc(&self, ptr: *mut u8, layout: Layout) {
        // SAFETY: as above.
        unsafe { System.dealloc(ptr, layout) }
    }
}

#[global_allocator]
static GLOBAL: Counting = Counting;

fn allocations_in(f: impl FnOnce()) -> usize {
    let before = ALLOCATIONS.with(Cell::get);
    f();
    ALLOCATIONS.with(Cell::get) - before
}

const SIZES: [usize; 3] = [150, 30_000, 400_000];

fn stream(mux: &MuxWriter, rounds: usize) -> Vec<u8> {
    let payloads: Vec<Vec<u8>> = SIZES.iter().map(|&n| vec![0xA5; n]).collect();
    let mut out = Vec::new();
    mux.begin(&mut out);
    for r in 0..rounds {
        for (i, p) in payloads.iter().enumerate() {
            mux.write_message(&mut out, (r * 3 + i) as u64 % 5, p);
        }
    }
    mux.finish(&mut out);
    out
}

#[test]
fn no_allocation_per_message_once_warm() {
    let mux = MuxWriter::new("mux").unwrap();
    let payloads: Vec<Vec<u8>> = SIZES.iter().map(|&n| vec![0x5A; n]).collect();

    // Writing: headers into a reused buffer, and whole messages into a
    // reused Vec and through io::Write.
    let mut head = Vec::new();
    let mut out = Vec::new();
    let mut sink = std::io::sink();
    let mut write = || {
        for (i, p) in payloads.iter().enumerate() {
            let parts = mux.encode(&mut head, i as u64, p);
            std::hint::black_box(parts.count());
            mux.write_message(&mut out, i as u64, p);
            mux.write_message_to(&mut sink, i as u64, p).unwrap();
        }
        out.clear();
    };
    write();
    assert_eq!(allocations_in(&mut write), 0, "writing");

    // Reading: one stream, measured after its first rounds have warmed
    // every channel's buffer to every size.
    let messages = stream(&mux, 40);
    for piece in [usize::MAX, 4096, 333] {
        let mut reader = MuxReader::new();
        let split = messages.len() / 4;
        let (first, rest) = messages.split_at(split);
        for part in first.chunks(piece) {
            let mut input = part;
            while let Some(m) = reader.read(&mut input).unwrap() {
                std::hint::black_box(m.payload.len());
            }
        }
        let n = allocations_in(|| {
            for part in rest.chunks(piece) {
                let mut input = part;
                while let Some(m) = reader.read(&mut input).unwrap() {
                    std::hint::black_box(m.payload.len());
                }
            }
        });
        reader.finish().unwrap();
        assert_eq!(n, 0, "reading in pieces of {piece}");

        // A reset keeps the buffers: the next stream reads without warming
        // them again.
        reader.reset();
        let n = allocations_in(|| {
            for part in messages.chunks(piece) {
                let mut input = part;
                while let Some(m) = reader.read(&mut input).unwrap() {
                    std::hint::black_box(m.payload.len());
                }
            }
        });
        reader.finish().unwrap();
        assert_eq!(n, 0, "reading again after reset, in pieces of {piece}");

        // Peek and skip, and peek and forward.
        for forward in [false, true] {
            let mut reader = MuxReader::new();
            let route = |reader: &mut MuxReader, data: &[u8], piece: usize| {
                for part in data.chunks(piece) {
                    let mut input = part;
                    while let Some(item) = reader.next_item(&mut input, 4).unwrap() {
                        match item {
                            Item::Peek(p) if forward => p.forward(),
                            Item::Peek(p) => p.skip(),
                            Item::Wire(w) => {
                                std::hint::black_box(w.bytes.len());
                            }
                            Item::Message(_) => unreachable!(),
                        }
                    }
                }
            };
            // Warm up in small pieces, so that forwarding also takes the path
            // that gathers and rewrites a message, not only the zero-copy one.
            // The cut at byte 13 falls inside the first message's head
            // (opener 6, chunk header 3, channel 1, length 2), so the head
            // buffer has been used too.
            route(&mut reader, &first[..13], 1);
            route(&mut reader, &first[13..], piece.min(4096));
            let n = allocations_in(|| route(&mut reader, rest, piece));
            reader.finish().unwrap();
            assert_eq!(n, 0, "peek, forward {forward}, pieces of {piece}");
        }
    }

    // Frames: documents gathered across pieces into the reused buffer.
    let mut framed = Vec::new();
    for i in 0..60 {
        write_frame(&mut framed, &vec![b' '; SIZES[i % 3]]);
    }
    let (first, rest) = framed.split_at(framed.len() / 4);
    let mut frames = FrameReader::new();
    let feed = |frames: &mut FrameReader, data: &[u8]| {
        for part in data.chunks(4096) {
            let mut input = part;
            while let Some(doc) = frames.read(&mut input).unwrap() {
                std::hint::black_box(doc.len());
            }
        }
    };
    feed(&mut frames, first);
    assert_eq!(allocations_in(|| feed(&mut frames, rest)), 0, "frames");
    frames.finish().unwrap();
}
