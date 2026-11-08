# SPDX-License-Identifier: MIT
#
# Copyright (c) 2026 Janos Sonntag
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

"""
Per-message cost of reading a multiplexed stream from Python: the batch
stream.mux_load (one ctypes callback per message) next to stream.DemuxFeed
(bvnr_demux_next, no callback), fed the whole document at once and in 1460-byte
pieces (a TCP segment's payload), with payloads as bytes and as memoryviews.
And of writing one: the batch stream.mux_dump next to stream.MuxWriter into a
socket-like object that takes what it is sent (a socket minus the system
call), batched and flushed after every message.

Messages of 150 B, 30 KB and 400 KB (the last spans seven octet chunks), the
same cells as tests/bovnar_stream_bench.c, whose C figures doc/10 §8.4 lists.
Not a test (pytest does not collect python/bench); run it by hand, pinned to one
core, on a library built as Release or RelWithDebInfo:

    export LIBBOVNAR_PATH=$PWD/build/libbvnr.so
    taskset -c 5 python3 python/bench/bench_stream.py [--runs 7] [--budget 0.3]

Each cell runs for at least `budget` seconds per run; the figure printed is the
median over the runs, with the spread (min-max) beside it. The load average is
printed before and after, because on a shared machine it is what moves the
numbers.
"""

import argparse
import os
import statistics
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from bovnar import stream                                   # noqa: E402

SIZES  = (150, 30_000, 400_000)
COUNTS = (20_000, 400, 30)
PIECE  = 1460


def make_messages(size: int, count: int) -> list:
    payload = bytes((i * 2654435761 >> 11) & 0xFF for i in range(size + 64))
    return [(i % 4, payload[i % 64:i % 64 + size]) for i in range(count)]


def run_batch(doc: bytes) -> int:
    return sum(len(p) for _, p in stream.mux_load(doc))


def run_feed(doc: bytes, piece: int, view: bool) -> int:
    total = 0
    with stream.DemuxFeed(view=view) as dm:
        if piece >= len(doc):
            for _channel, payload in dm.feed(doc):
                total += len(payload)
        else:
            for at in range(0, len(doc), piece):
                for _channel, payload in dm.feed(doc[at:at + piece]):
                    total += len(payload)
        for _ in dm.finish():
            pass
    return total


class _Discard:
    """A socket that takes every byte: sendall and nothing else."""

    def sendall(self, data) -> None:
        pass


def run_dump(msgs: list) -> int:
    return len(stream.mux_dump(msgs))


def run_live(msgs: list, flush_each: bool) -> int:
    with stream.MuxWriter(_Discard(), flush_each=flush_each) as mux:
        for channel, payload in msgs:
            mux.send(channel, payload)
        return mux.bytes_written


def per_message(fn, count: int, budget: float) -> float:
    rounds, t0 = 0, time.perf_counter()
    while True:
        fn()
        rounds += 1
        elapsed = time.perf_counter() - t0
        if elapsed >= budget and rounds >= 2:
            return elapsed / (rounds * count)


def fmt(seconds: float) -> str:
    if seconds >= 1e-3:
        return f"{seconds * 1e3:.2f} ms"
    if seconds >= 1e-6:
        return f"{seconds * 1e6:.2f} µs"
    return f"{seconds * 1e9:.0f} ns"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--runs", type=int, default=7)
    ap.add_argument("--budget", type=float, default=0.3,
                    help="seconds per cell and run (default 0.3)")
    args = ap.parse_args()

    cpus = sorted(os.sched_getaffinity(0)) if hasattr(os, "sched_getaffinity") else []
    print(f"Python {sys.version.split()[0]}, cpus {cpus}, "
          f"load average {os.getloadavg()[0]:.2f} at start")
    for size, count in zip(SIZES, COUNTS):
        msgs = make_messages(size, count)
        doc = stream.mux_dump(msgs)
        whole = len(doc)
        cells = [
            ("mux_dump (batch, to bytes)",
             lambda: run_dump(msgs)),
            ("MuxWriter, batched",
             lambda: run_live(msgs, False)),
            ("MuxWriter, flushed per message",
             lambda: run_live(msgs, True)),
            ("mux_load (batch, callback per message)",
             lambda: run_batch(doc)),
            ("DemuxFeed, whole buffer, bytes",
             lambda: run_feed(doc, whole, False)),
            ("DemuxFeed, whole buffer, memoryview",
             lambda: run_feed(doc, whole, True)),
            (f"DemuxFeed, {PIECE} B pieces, bytes",
             lambda: run_feed(doc, PIECE, False)),
            (f"DemuxFeed, {PIECE} B pieces, memoryview",
             lambda: run_feed(doc, PIECE, True)),
        ]
        print(f"{count} messages of {size} B, {whole}-byte document")
        for name, fn in cells:
            runs = [per_message(fn, count, args.budget) for _ in range(args.runs)]
            print(f"  {name:<42} {fmt(statistics.median(runs)):>10}/msg"
                  f"   ({fmt(min(runs))} – {fmt(max(runs))})")
    print(f"load average {os.getloadavg()[0]:.2f} at end")
    return 0


if __name__ == "__main__":
    sys.exit(main())
