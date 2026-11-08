#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Janos Sonntag
"""Generate the shared streaming conformance vectors in tests/stream_vectors/.

One set of wire vectors on disk for the readers and writers of the streaming
layer (doc/10 §2 and §3), so that the C library and the Rust crate's
pure-Rust codec are held to the same bytes instead of each to its own
hand-written vectors in its own test code. This is the minimal set: the
multiplexed streams and frames a live consumer relies on, and three malformed
streams. The full set (every reader of every layer, the remaining wire
shapes and limits) is still to come (TODO.md, streaming item 5).

The bytes are built here from the wire conventions alone -- the octet chunk of
spec §9, the two LEB128 varints of doc/10 §3.1, the BVF1 frame of doc/10 §2.1
-- never by the library under test, so a defect in it cannot write itself
into its own expectations. The expected outcome of each vector is known from
how it was built.

Outputs, all deterministic:

  tests/stream_vectors/*.bvnr        multiplexed documents
  tests/stream_vectors/*.bvf         BVF1 frame streams
  tests/stream_vectors/manifest.bvnr every vector and its expected outcome, a
                                     Bovnar document each test reads with its
                                     own layer's parser

Usage:
  python3 tests/gen_stream_vectors.py           write the files
  python3 tests/gen_stream_vectors.py --check   exit 1 if a committed file
                                                differs from a fresh generation

Hashes. A message or a framed document is identified by its length and its
64-bit FNV-1a, as 16 hex digits: the C and Rust tests carry no hash library,
FNV-1a is five lines in each, and the vectors are not adversarial. A vector's
`.digest` is the FNV-1a over all its messages in order, each as channel
(u64 LE), length (u64 LE), payload.
"""

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "stream_vectors")

CHUNK = 65536


def leb(v: int) -> bytes:
    """Unsigned LEB128, as doc/10 §3.1 writes channel ids and lengths."""
    out = bytearray()
    while True:
        b = v & 0x7F
        v >>= 7
        if not v:
            out.append(b)
            return bytes(out)
        out.append(b | 0x80)


def chunk(body: bytes) -> bytes:
    """One octet chunk (spec §9): tag 0x01, u16-LE length (0 means 65536)."""
    assert 1 <= len(body) <= CHUNK
    return b"\x01" + (len(body) & 0xFFFF).to_bytes(2, "little") + body


def mux_send(channel: int, payload: bytes) -> bytes:
    """The chunks bvnr_mux_send writes for one message: the channel varint
    leads every chunk, the length varint follows it in the first, and each
    chunk is filled to 65536 bytes."""
    ch, ln = leb(channel), leb(len(payload))
    out, pos, first = bytearray(), 0, True
    while first or pos < len(payload):
        head = ch + (ln if first else b"")
        take = min(CHUNK - len(head), len(payload) - pos)
        out += chunk(head + payload[pos:pos + take])
        pos += take
        first = False
    return bytes(out)


def mux_doc(octets: bytes, pre: bytes = b"") -> bytes:
    """The preamble, then `.mux=` holding one octet stream, as the compact C
    writer writes it."""
    return pre + b".mux=\x00" + octets + b"\x00;"


def frame(doc: bytes) -> bytes:
    return b"BVF1" + len(doc).to_bytes(8, "little") + doc


def payload(n: int, seed: int) -> bytes:
    return bytes((((i + seed * 7919) * 2654435761) >> 13) & 0xFF
                 for i in range(n))


def fnv(data: bytes) -> str:
    h = 0xCBF29CE484222325
    for b in data:
        h = ((h ^ b) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return "%016x" % h


def digest(messages) -> str:
    acc = bytearray()
    for ch, p in messages:
        acc += ch.to_bytes(8, "little") + len(p).to_bytes(8, "little") + p
    return fnv(bytes(acc))


# A struct with a unit and a string, byte for byte as the compact C writer
# writes them from its typed calls (bvnr_write_float_unit prints the shortest
# exponent form, 2.997e+1), so every writer can reproduce the vector.
PREAMBLE = (b'.meta={.codec="av1";.rate=<float:64,Hz>2.997e+1;};'
            b'.title="caf\xc3\xa9";')

# Two channels interleaved message by message; 150 B, 30 KB, a 400 KB
# message over seven chunks, an empty one, and 1 + 3 + 65532 = 65536: a
# message filling one chunk exactly, whose length field reads 0x0000.
MESSAGES = [(1, payload(150, 1)), (2, payload(30000, 2)),
            (1, payload(400000, 3)), (2, b""), (1, payload(65532, 4)),
            (2, b"last")]

DOCUMENTS = [b'.id = 1; .name = "doc1";', b"",
             b".s = { .x = [1, 2]/[3, 4]; .u = 9.81 m/s; };"]


def vectors():
    """(name, kind, data, fields): fields are the manifest entries."""
    octets = b"".join(mux_send(c, p) for c, p in MESSAGES)
    ok = mux_send(1, b"ok")
    return [
        ("mux_preamble", "mux", mux_doc(octets, PREAMBLE),
         dict(messages=MESSAGES, preamble=PREAMBLE, canonical=True,
              note="text values (a struct with a unit, a string), then the "
                   "stream: 150 B, 30 KB, 400 KB over seven chunks, an empty "
                   "message, a message filling a 65536-byte chunk, two "
                   "channels interleaved message by message")),
        ("mux_plain", "mux", mux_doc(octets),
         dict(messages=MESSAGES, canonical=True,
              note="the same messages without a preamble")),
        ("frames_three", "frames", b"".join(frame(d) for d in DOCUMENTS),
         dict(documents=DOCUMENTS, note="three small documents, one empty")),
        ("mux_truncated_message", "mux",
         mux_doc(ok + chunk(leb(4) + leb(1000) + b"0123456789")),
         dict(messages=[(1, b"ok")], error="octet_stream_truncated",
              note="the stream closes inside a message, after one good one")),
        ("mux_bad_tag", "mux", b".mux=\x00" + ok + b"\x02\x01\x00\x00\x00;",
         dict(messages=[(1, b"ok")], error="octet_stream_out_of_sync",
              note="a chunk tag other than 0x01 or the terminator 0x00")),
        ("mux_oversize", "mux",
         mux_doc(mux_send(1, b"z" * 100) + mux_send(1, b"z" * 101)),
         dict(messages=[(1, b"z" * 100)], max_message=100,
              error="value_out_of_range",
              note="a declared length over max_message (100), after a "
                   "message at the cap")),
    ]


def bstr(b: bytes) -> str:
    s = b.decode("utf-8").replace("\\", "\\\\").replace('"', '\\"')
    assert all(ord(c) >= 0x20 for c in s)
    return '"' + s + '"'


def u64(n: int) -> str:
    return "<uint:64> %d" % n


def manifest(vs) -> bytes:
    out = ["""\
# DO NOT EDIT -- generated by tests/gen_stream_vectors.py.
#
# The shared streaming vectors (doc/10 §9). A multiplexed vector (.kind = mux),
# read with the demux scoped to the key "mux" and with .max_message (0: the
# 64 MiB default), hands out the .messages listed (all of them, or those before
# the error), whose FNV-1a over channel, length and payload is .digest, and
# ends with .error: the outcome of the reference reader, the C demultiplexer.
# .preamble is the text before the stream's assignment. A .canonical vector is
# what bvnr_mux_send writes after that preamble, so every writer must
# reproduce it byte for byte. A frame vector (.kind = frames) holds the
# .documents listed, each with the error it gives on its own.
"""]
    for name, kind, data, f in vs:
        i = "    "
        out.append("\n# %s\n.%s = {\n" % (f["note"], name))
        ext = ".bvnr" if kind == "mux" else ".bvf"
        out.append(i + ".file = %s;\n" % bstr((name + ext).encode()))
        out.append(i + ".kind = %s;\n" % kind)
        out.append(i + ".size = %s;\n" % u64(len(data)))
        if kind == "mux":
            msgs = f["messages"]
            out.append(i + ".max_message = %s;\n" % u64(f.get("max_message", 0)))
            out.append(i + ".digest = \"%s\";\n" % digest(msgs))
            rows = [i + "    { .channel = %s; .length = %s; .fnv = \"%s\"; }"
                    % (u64(c), u64(len(p)), fnv(p)) for c, p in msgs]
            out.append(i + ".messages = [\n" + ",\n".join(rows) + "\n" + i
                       + "];\n")
            out.append(i + ".error = %s;\n" % f.get("error", "none"))
            out.append(i + ".canonical = %s;\n"
                       % ("true" if f.get("canonical") else "false"))
            out.append(i + ".preamble = %s;\n" % bstr(f.get("preamble", b"")))
        else:
            rows = [i + "    { .length = %s; .fnv = \"%s\"; .error = none; }"
                    % (u64(len(d)), fnv(d)) for d in f["documents"]]
            out.append(i + ".documents = [\n" + ",\n".join(rows) + "\n" + i
                       + "];\n")
            out.append(i + ".error = none;\n")
        out.append("};\n")
    return "".join(out).encode("utf-8")


def outputs():
    vs = vectors()
    files = {name + (".bvnr" if kind == "mux" else ".bvf"): data
             for name, kind, data, _ in vs}
    files["manifest.bvnr"] = manifest(vs)
    return files


def main(argv):
    files = outputs()
    if "--check" in argv[1:]:
        bad = []
        for name, data in sorted(files.items()):
            try:
                with open(os.path.join(OUT, name), "rb") as f:
                    if f.read() != data:
                        bad.append(name + " differs from a fresh generation")
            except OSError:
                bad.append(name + " is missing")
        present = set(os.listdir(OUT)) if os.path.isdir(OUT) else set()
        bad += [n + " is not generated" for n in sorted(present - set(files))]
        for b in bad:
            print("tests/stream_vectors/" + b)
        if bad:
            print("run: python3 tests/gen_stream_vectors.py")
            return 1
        print("%d stream vector files match a fresh generation" % len(files))
        return 0
    os.makedirs(OUT, exist_ok=True)
    for name, data in sorted(files.items()):
        with open(os.path.join(OUT, name), "wb") as f:
            f.write(data)
    print("wrote %d files to tests/stream_vectors/" % len(files))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
