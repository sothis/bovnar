#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Janos Sonntag
"""Generate the shared streaming conformance vectors in tests/stream_vectors/.

One set of wire vectors on disk for the readers and writers of the streaming
layer (doc/10 §2 and §3), so that the C library and the Rust crate's
pure-Rust codec are held to the same bytes instead of each to its own
hand-written vectors in its own test code: every wire shape a producer may
write (packed and interleaved chunks, a length varint split across chunks,
the widest channel ids, a message filling a chunk exactly and one spilling a
byte), the limits (4096 channels and the 4097th, max_message,
max_document_size), and the ways a stream fails (a cut inside the octet
stream or a frame, a desync, a failed document among good ones).

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

# A frame stream's documents as (bytes, the error each gives on its own). The
# failing ones fail in a way the grammar fixes: '@' starts no value, and a
# struct left open is an incomplete document.
GOOD = b'.ok = 1;'
FAILED = [(GOOD, "none"), (b".a = @;", "unexpected_input_byte"), (b"", "none"),
          (b".b = { .c = 1;", "incomplete_bvnr_stream"), (GOOD, "none")]

MAX_U64 = (1 << 64) - 1


def wide_channels():
    """Channel ids of every varint width, and the one-chunk boundary for the
    widest: 10 + 3 varint bytes leave 65523 for the payload, so 65523 fills
    the chunk exactly and 65524 spills one byte into a second chunk."""
    return [(MAX_U64, payload(65523, 5)), (MAX_U64, payload(65524, 6)),
            (1 << 63, b""), (127, b"a"), (128, b"bc"), (16383, b"d"),
            (16384, payload(300, 7)), (0, b"zero")]


def packed():
    """Chunks a producer may pack: several messages in one chunk, and a
    chunk that ends one message, holds another whole and starts a third."""
    a, b, c = payload(40, 8), payload(500, 9), payload(70, 10)
    octets = (chunk(leb(3) + leb(2) + b"p1" + leb(0) + leb(3) + b"p22")
              + chunk(leb(4) + leb(len(a)) + a[:10])
              + chunk(leb(4) + a[10:] + leb(len(b)) + b + leb(len(c)) + c[:5])
              + chunk(leb(4) + c[5:]))
    msgs = [(3, b"p1"), (3, b""), (3, b"p22"), (4, a), (4, b), (4, c)]
    return octets, msgs


def interleaved():
    """Two messages of 100 KB whose chunks alternate, and a small one on a
    third channel between them: messages complete in the order their last
    chunk arrives, not the order they were begun."""
    x, y = payload(100000, 11), payload(100000, 12)
    hx, hy = leb(len(x)), leb(len(y))
    out = bytearray()
    step = 30000
    for k in range(0, 100000, step):
        out += chunk(leb(1) + (hx if k == 0 else b"") + x[k:k + step])
        if k == step:
            out += chunk(leb(9) + leb(5) + b"small")
        out += chunk(leb(2) + (hy if k == 0 else b"") + y[k:k + step])
    return bytes(out), [(9, b"small"), (1, x), (2, y)]


def split_lengths():
    """A length varint split across chunks: two bytes split 1 | 1 with
    another channel's chunk between them, and three bytes split 1 | 1 | 1."""
    p, q = payload(300, 13), payload(20000, 14)
    lp, lq = leb(len(p)), leb(len(q))
    assert len(lp) == 2 and len(lq) == 3
    octets = (chunk(leb(5) + lp[:1]) + chunk(leb(6) + leb(4) + b"mid!")
              + chunk(leb(5) + lp[1:] + p)
              + chunk(leb(7) + lq[:1]) + chunk(leb(7) + lq[1:2])
              + chunk(leb(7) + lq[2:]))
    pos = 0
    while pos < len(q):
        take = min(CHUNK - 1, len(q) - pos)
        octets += chunk(leb(7) + q[pos:pos + take])
        pos += take
    return octets, [(6, b"mid!"), (5, p), (7, q)]


def channels(n):
    """n distinct channels, each with an empty message in a chunk of its own."""
    msgs = [(c, b"") for c in range(n)]
    return b"".join(mux_send(c, p) for c, p in msgs), msgs


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
        ("mux_empty", "mux", mux_doc(b""),
         dict(messages=[], canonical=True,
              note="a stream that carries no message")),
        ("mux_wide_channels", "mux",
         mux_doc(b"".join(mux_send(c, p) for c, p in wide_channels())),
         dict(messages=wide_channels(), canonical=True,
              note="channel ids of every varint width up to 2^64-1, and for "
                   "the widest a message filling a chunk exactly (65523 "
                   "bytes) and one spilling a byte into a second chunk")),
        ("mux_packed", "mux", mux_doc(packed()[0]),
         dict(messages=packed()[1],
              note="several messages packed into one chunk, and a chunk that "
                   "ends a message, holds one whole and starts a third")),
        ("mux_interleaved", "mux", mux_doc(interleaved()[0]),
         dict(messages=interleaved()[1],
              note="the chunks of two 100 KB messages alternating, and a "
                   "small message between them, which completes first")),
        ("mux_split_length", "mux", mux_doc(split_lengths()[0]),
         dict(messages=split_lengths()[1],
              note="a length varint split across chunks: two bytes with "
                   "another channel's chunk between them, three bytes over "
                   "three chunks")),
        ("mux_4096_channels", "mux", mux_doc(channels(4096)[0]),
         dict(messages=channels(4096)[1], canonical=True,
              note="4096 distinct channels, the most one stream may use")),
        ("mux_4097th_channel", "mux",
         mux_doc(channels(4096)[0] + mux_send(4096, b"")),
         dict(messages=channels(4096)[1], error="octet_stream_out_of_sync",
              note="a 4097th distinct channel, which cannot be attributed")),
        ("mux_channel_cut", "mux", mux_doc(ok + chunk(b"\x80") + ok),
         dict(messages=[(1, b"ok")], error="octet_stream_out_of_sync",
              note="a chunk that ends inside its channel varint")),
        ("mux_overlong_length", "mux",
         mux_doc(ok + chunk(leb(1) + b"\xff" * 10 + b"\x01")),
         dict(messages=[(1, b"ok")], error="octet_stream_out_of_sync",
              note="a length varint of eleven bytes")),
        ("mux_channel_over_64_bits", "mux",
         mux_doc(ok + chunk(b"\xff" * 9 + b"\x02" + leb(0))),
         dict(messages=[(1, b"ok")], error="octet_stream_out_of_sync",
              note="a channel varint whose tenth byte carries bits past 64")),
        ("mux_ends_in_chunk", "mux",
         b".mux=\x00" + ok + chunk(leb(1) + leb(10) + b"0123456789")[:8],
         dict(messages=[(1, b"ok")], error="read_complete_chunk_failed",
              note="the input ends inside a chunk, a message in progress")),
        ("mux_ends_before_terminator", "mux", b".mux=\x00" + ok,
         dict(messages=[(1, b"ok")], error="read_complete_chunk_failed",
              note="the input ends after a chunk, before the stream's "
                   "closing 0x00")),
        ("mux_terminator", "mux",
         b".mux = \x00" + ok + b"\x00 # closed\n ; # done\n\t\n",
         dict(messages=[(1, b"ok")],
              note="the closing 0x00, then whitespace and comments before "
                   "and after the ;")),
        ("mux_after_stream", "mux", mux_doc(ok) + b".after = 1;",
         dict(messages=[(1, b"ok")], codec_error="unexpected_input_byte",
              note="a statement after the stream: the C demux reads the "
                   "document on, the codec reads only a document that ends "
                   "with the stream")),
        ("mux_other_key", "mux", b".media=\x00" + ok + b"\x00;",
         dict(messages=[], codec_error="unexpected_input_byte",
              note="a stream under another key: the C demux scoped to mux "
                   "passes it over, the codec refuses it")),
        ("mux_binary_preamble", "mux",
         b".bin=\x00" + chunk(b"\x00\x01\x02") + b"\x00;" + mux_doc(ok),
         dict(messages=[(1, b"ok")], codec_error="unexpected_input_byte",
              codec_messages=0,
              note="an octet stream before the mux stream: the C demux "
                   "scoped to mux passes it over, the codec takes the first "
                   "0x00 for the stream's opener")),
        ("frames_failed", "frames", b"".join(frame(d) for d, _ in FAILED),
         dict(documents=FAILED,
              note="a document that does not parse and one left incomplete, "
                   "among good ones (and an empty one)")),
        ("frames_bad_magic", "frames", frame(GOOD) + b"BVF2" + bytes(8),
         dict(documents=[(GOOD, "none")], error="unexpected_input_byte",
              note="a header without the BVF1 magic, after a good frame")),
        ("frames_oversize", "frames",
         frame(b"#" + b"x" * 62 + b"\n") + frame(b"#" + b"y" * 63 + b"\n"),
         dict(documents=[(b"#" + b"x" * 62 + b"\n", "none")],
              max_document_size=64, error="value_out_of_range",
              note="a frame over max_document_size (64), after one at it")),
        ("frames_cut_payload", "frames", (frame(GOOD) * 2)[:-3],
         dict(documents=[(GOOD, "none")],
              error="incomplete_bvnr_stream",
              note="the input ends inside a frame's document")),
        ("frames_cut_header", "frames", frame(GOOD) + frame(GOOD)[:7],
         dict(documents=[(GOOD, "none")],
              error="incomplete_bvnr_stream",
              note="the input ends inside a frame header")),
        ("frames_none", "frames", b"",
         dict(documents=[], note="a frame stream of no frames")),
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
# .codec_error is the outcome of the pure-Rust codec (bovnar::codec), which
# reads one shape of document only and refuses the others where the C demux
# reads on, after the first .codec_messages of the messages.
# .preamble is the text before the stream's assignment. A .canonical vector is
# what bvnr_mux_send writes after that preamble, so every writer must
# reproduce it byte for byte. A frame vector (.kind = frames) holds the
# .documents listed, each with the error it gives when parsed on its own, read
# under .max_document_size (0: the 256 MiB default); .error is how the stream
# ends for a reader that does not parse the documents, or that goes on past a
# failed one. One that stops at a failed document ends with that document's
# error instead. Errors are named as bvn_error_to_string names them.
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
            out.append(i + ".messages = [" + ("\n" + ",\n".join(rows) + "\n"
                                             + i if rows else "") + "];\n")
            out.append(i + ".error = %s;\n" % f.get("error", "none"))
            out.append(i + ".codec_error = %s;\n"
                       % f.get("codec_error", f.get("error", "none")))
            out.append(i + ".codec_messages = %s;\n"
                       % u64(f.get("codec_messages", len(msgs))))
            out.append(i + ".canonical = %s;\n"
                       % ("true" if f.get("canonical") else "false"))
            out.append(i + ".preamble = %s;\n" % bstr(f.get("preamble", b"")))
        else:
            docs = [d if isinstance(d, tuple) else (d, "none")
                    for d in f["documents"]]
            out.append(i + ".max_document_size = %s;\n"
                       % u64(f.get("max_document_size", 0)))
            rows = [i + "    { .length = %s; .fnv = \"%s\"; .error = %s; }"
                    % (u64(len(d)), fnv(d), e) for d, e in docs]
            out.append(i + ".documents = [" + ("\n" + ",\n".join(rows) + "\n"
                                              + i if rows else "") + "];\n")
            out.append(i + ".error = %s;\n" % f.get("error", "none"))
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
