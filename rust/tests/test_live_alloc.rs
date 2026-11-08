// SPDX-License-Identifier: MIT
//! The live readers allocate nothing per message or document once warm, and
//! the `std::io` entry points stream instead of gathering their input or
//! output: a counting global allocator, in a test binary of its own so that
//! no other test's allocations are counted, watches the Rust side. (The C
//! library allocates through its own malloc, which this does not see; its
//! fed paths are held to zero allocations per message by `bvnr_alloc_test`.)
//! The count is per thread, so the test harness's own threads do not disturb
//! it.

use std::alloc::{GlobalAlloc, Layout, System};
use std::cell::Cell;
use std::io::{self, Read};

use bovnar::live::{Demux, DemuxItem, DemuxOptions, DocumentFeed, FramePull};
use bovnar::{ReadOptions, Reader, Writer, stream};

struct Counting;

thread_local! {
    static ALLOCATIONS: Cell<(usize, usize)> = const { Cell::new((0, 0)) };
}

fn count(bytes: usize) {
    // try_with: the allocator may run while the thread's locals are torn down.
    let _ = ALLOCATIONS.try_with(|n| {
        let (calls, total) = n.get();
        n.set((calls + 1, total + bytes));
    });
}

// SAFETY: every call is passed straight to the system allocator; the counter
// is a const-initialised thread local, which itself never allocates.
unsafe impl GlobalAlloc for Counting {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        count(layout.size());
        // SAFETY: the caller's contract is System's.
        unsafe { System.alloc(layout) }
    }

    unsafe fn alloc_zeroed(&self, layout: Layout) -> *mut u8 {
        count(layout.size());
        // SAFETY: as above.
        unsafe { System.alloc_zeroed(layout) }
    }

    unsafe fn realloc(&self, ptr: *mut u8, layout: Layout, new_size: usize) -> *mut u8 {
        count(new_size);
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

/// (allocations, bytes allocated) while `f` runs.
fn allocations_in(f: impl FnOnce()) -> (usize, usize) {
    let (calls, bytes) = ALLOCATIONS.with(Cell::get);
    f();
    let (calls2, bytes2) = ALLOCATIONS.with(Cell::get);
    (calls2 - calls, bytes2 - bytes)
}

const SIZES: [usize; 3] = [150, 30_000, 400_000];

fn mux_stream(rounds: usize) -> (Vec<u8>, usize) {
    let mut messages = Vec::new();
    for r in 0..rounds {
        for (i, &n) in SIZES.iter().enumerate() {
            messages.push(((r * 3 + i) as u64 % 5, vec![0xA5u8; n]));
        }
    }
    let mut doc = b".meta = { .codec = \"av1\"; };\n".to_vec();
    doc.extend(stream::mux_dump(&messages, "media").unwrap());
    (doc, messages.len())
}

/// Drains `doc` through `demux` in pieces of `piece` bytes; the messages.
fn drain_demux(demux: &mut Demux, doc: &[u8], piece: usize) -> usize {
    let mut n = 0;
    for chunk in doc.chunks(piece) {
        let mut input = chunk;
        while let Some(item) = demux.next(&mut input).unwrap() {
            if let DemuxItem::Message { payload, .. } = item {
                n += usize::from(!payload.is_empty());
            }
        }
    }
    demux.finish().unwrap();
    while demux.next(&mut &[][..]).unwrap().is_some() {}
    demux.reset().unwrap();
    n
}

#[test]
fn a_warm_fed_demux_allocates_nothing_per_message() {
    let (doc, messages) = mux_stream(10);
    for deliver_events in [false, true] {
        let options = DemuxOptions {
            deliver_events,
            ..DemuxOptions::default()
        };
        let mut demux = Demux::new(&options).unwrap();
        // Whole, in pieces that split messages (gathered in the channel's
        // buffer), and in pieces smaller than a chunk header.
        for piece in [doc.len(), 4096, 7] {
            assert_eq!(drain_demux(&mut demux, &doc, piece), messages);
            let mut n = 0;
            let (calls, _) = allocations_in(|| n = drain_demux(&mut demux, &doc, piece));
            assert_eq!(n, messages);
            assert_eq!(
                calls, 0,
                "{calls} allocations for {messages} messages, pieces of {piece}"
            );
        }
    }
}

#[test]
fn a_warm_frame_pull_and_document_feed_allocate_nothing_per_document() {
    let docs: Vec<Vec<u8>> = SIZES
        .iter()
        .map(|&n| format!(".s = \"{}\";", "x".repeat(n.min(60_000))).into_bytes())
        .cycle()
        .take(30)
        .collect();
    let frames = stream::frame_documents(&docs).unwrap();
    let mut feed = DocumentFeed::new(&ReadOptions::default()).unwrap();
    let mut events = 0usize;
    let mut drain = |pull: &mut FramePull, piece: usize| {
        let mut n = 0;
        for chunk in frames.chunks(piece) {
            let mut input = chunk;
            while let Some(doc) = pull.next(&mut input).unwrap() {
                feed.reset().unwrap();
                feed.feed(doc, &mut |_| {
                    events += 1;
                    true
                })
                .unwrap();
                feed.finish(&mut |_| true).unwrap();
                n += 1;
            }
        }
        n
    };
    for piece in [frames.len(), 4096, 5] {
        // A frame stream has no end of its own, so the same frames fed
        // again are thirty more documents for the warm reader.
        let mut pull = FramePull::new(0).unwrap();
        assert_eq!(drain(&mut pull, piece), docs.len());
        let mut n = 0;
        let (calls, _) = allocations_in(|| n = drain(&mut pull, piece));
        assert_eq!(n, docs.len());
        assert_eq!(
            calls, 0,
            "{calls} allocations for {n} documents, pieces of {piece}"
        );
    }
    assert!(events > 0);
}

/// An endless-looking input: the same document, `left` bytes of it.
struct Repeat {
    doc: &'static [u8],
    pos: usize,
    left: usize,
}

impl Read for Repeat {
    fn read(&mut self, buf: &mut [u8]) -> io::Result<usize> {
        let mut n = 0;
        while n < buf.len() && self.left > 0 {
            let take = (self.doc.len() - self.pos)
                .min(buf.len() - n)
                .min(self.left);
            buf[n..n + take].copy_from_slice(&self.doc[self.pos..self.pos + take]);
            self.pos = (self.pos + take) % self.doc.len();
            self.left -= take;
            n += take;
        }
        Ok(n)
    }
}

#[test]
fn the_io_entry_points_stream_instead_of_gathering() {
    const TOTAL: usize = 20 << 20;
    let line = b".k = <uint:32> 12345;\n";
    let mut events = 0usize;
    let mut on = |_: &bovnar::EventData<'_>| {
        events += 1;
        true
    };
    let mut reader = Reader::new().unwrap();
    let (_, bytes) = allocations_in(|| {
        let input = Repeat {
            doc: line,
            pos: 0,
            left: TOTAL - TOTAL % line.len(),
        };
        reader
            .read_reader(input, &ReadOptions::default(), Some(&mut on), None)
            .unwrap();
    });
    assert!(events > 1_000_000);
    assert!(bytes < 1 << 20, "{bytes} bytes allocated reading {TOTAL}");

    let payload = vec![7u8; 60_000];
    let (_, bytes) = allocations_in(|| {
        let mut w = Writer::to_writer(io::sink(), false).unwrap();
        for _ in 0..TOTAL / payload.len() {
            w.write_octets("blob", &payload).unwrap();
        }
        w.finish().unwrap();
    });
    assert!(bytes < 1 << 20, "{bytes} bytes allocated writing {TOTAL}");
}
