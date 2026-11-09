# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Janos Sonntag
"""The shared streaming vectors (tests/stream_vectors/, doc/10 §9) through the
Python bindings: mux_load and DemuxFeed hand out the messages the manifest
lists and end with its error; mux_dump, and MuxWriter for a vector without a
preamble, write the canonical vectors byte for byte; FrameFeed, pulled and
parsing, and load_documents read the frame vectors, the parsing ones with each
document's own outcome; frame_documents and FrameWriter write the frame
vectors that end cleanly byte for byte."""

import io
import re
from functools import lru_cache
from pathlib import Path

import pytest

from conftest import needs_lib
import bovnar
from bovnar import stream
from bovnar.exceptions import BovnarParseError

pytestmark = needs_lib

_DIR = Path(__file__).resolve().parents[2] / "tests" / "stream_vectors"
_TEXT = (_DIR / "manifest.bvnr").read_text(encoding="utf-8")


@lru_cache(maxsize=None)
def _manifest():
    # Parsed on first use, not at import: collection must not need the
    # library (bvnr_py_pure_all collects every file without it).
    return bovnar.loads(_TEXT.encode())


def _fnv(data: bytes) -> str:
    h = 0xCBF29CE484222325
    for b in data:
        h = ((h ^ b) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return "%016x" % h


def _label(exc):
    """An error as the manifest names it: bvn_error_to_string's label (code
    18 is "incomplete_bvnr_stream", not its enumerator's spelling)."""
    from bovnar._ffi import get_library
    return get_library().bvn_error_to_string(int(exc.code)).decode()


def _rows(items):
    return [(ch, len(p), _fnv(p)) for ch, p in items]


def _expected(v):
    rows = [(m.get("channel", 0), m["length"], m["fnv"])
            for m in v["messages" if v["kind"] == "mux" else "documents"]]
    return rows, v["error"]


def _vectors(kind):
    """The vector names of a kind, read off the manifest's text."""
    return re.findall(r"^\.(\w+) = \{\n.*\n    \.kind = %s;" % kind, _TEXT,
                      re.M)


def _demux(v, pieces):
    got, feed = [], stream.DemuxFeed(key="mux", max_message=v["max_message"])
    try:
        for p in pieces:
            got.extend(feed.feed(p))
        got.extend(feed.finish())
    except BovnarParseError as e:
        return _rows(got), _label(e)
    return _rows(got), "none"


@pytest.mark.parametrize("name", _vectors("mux"))
def test_mux_vectors(name):
    v = _manifest()[name]
    data = (_DIR / v["file"]).read_bytes()
    rows, error = _expected(v)
    try:
        got = (_rows(stream.mux_load(data, key="mux",
                                     max_message=v["max_message"])), "none")
    except BovnarParseError as e:
        # A batch loader returns everything or the error alone.
        got = (rows, _label(e))
    assert got == (rows, error), "mux_load"
    splits = [[data]]
    if len(data) <= 4096:
        splits.append([data[i:i + 1] for i in range(len(data))])
    else:
        splits.append([data[i:i + 1460] for i in range(0, len(data), 1460)])
    for pieces in splits:
        assert _demux(v, pieces) == (rows, error), "DemuxFeed"
    if v["canonical"]:
        msgs = stream.mux_load(data, key="mux")
        pre = bovnar.loads(v["preamble"].encode(), typed=True) or None
        assert stream.mux_dump(msgs, preamble=pre) == data, "mux_dump"
        if not v["preamble"]:
            out = io.BytesIO()
            with stream.MuxWriter(out, flush_each=False) as w:
                for ch, p in msgs:
                    w.send(ch, p)
            assert out.getvalue() == data, "MuxWriter"


def _pieces(data):
    if len(data) <= 4096:
        return [data[i:i + 1] for i in range(len(data))]
    return [data[i:i + 1460] for i in range(0, len(data), 1460)]


def _parse_expected(v, go_on):
    """How a parsing reader ends: the stream's error going on past a failed
    document; stopping at one, that document's error, after the documents up
    to and including it."""
    errs = [d["error"] for d in v["documents"]]
    for i, e in enumerate(errs):
        if e != "none" and not go_on:
            return errs[:i + 1], e
    return errs, v["error"]


@pytest.mark.parametrize("name", _vectors("frames"))
def test_frame_vectors(name):
    v = _manifest()[name]
    data = (_DIR / v["file"]).read_bytes()
    rows, error = _expected(v)
    cap = v["max_document_size"]
    for pieces in ([data], _pieces(data)):
        docs, end = [], "none"
        feed = stream.FrameFeed(max_document_size=cap)
        try:
            for p in pieces:
                docs.extend(bytes(d) for d in feed.feed(p))
            feed.finish()
        except BovnarParseError as e:
            end = _label(e)
        assert (_rows((0, d) for d in docs), end) == (rows, error), "FrameFeed"
    for go_on in (False, True):
        want_docs, want_end = _parse_expected(v, go_on)
        want = ([e == "none" for e in want_docs], want_end)
        for pieces in ([data], _pieces(data)):
            got, end = [], "none"
            feed = stream.FrameFeed(parse=True, continue_past_failed=go_on,
                                    max_document_size=cap)
            try:
                for p in pieces:
                    got.extend(feed.feed(p))
                got.extend(feed.finish())
            except BovnarParseError as e:
                end = _label(e)
            # A document that fails without continue_past_failed is the
            # error, not an item.
            stopped = not go_on and any(e != "none" for e in want_docs)
            oks = [d is not None for d in got] + ([False] if stopped else [])
            assert (oks, end) == want, ("FrameFeed parse", go_on)
        try:
            got = stream.load_documents(data, continue_past_failed=go_on,
                                        max_document_size=cap)
            assert want_end == "none", ("load_documents", go_on)
            assert [d is not None for d in got] == want[0]
        except BovnarParseError as e:
            assert _label(e) == want_end, ("load_documents", go_on)
    if error == "none":
        docs = [data[i + 12:i + 12 + n] for i, n in
                _offsets([r[1] for r in rows])]
        assert stream.frame_documents(docs) == data
        out = io.BytesIO()
        with stream.FrameWriter(out) as w:
            for d in docs:
                w.write(d)
        assert out.getvalue() == data, "FrameWriter"


def _offsets(lengths):
    at = 0
    for n in lengths:
        yield at, n
        at += 12 + n
