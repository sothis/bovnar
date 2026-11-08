# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Janos Sonntag
"""The shared streaming vectors (tests/stream_vectors/, doc/10 §9) through the
Python bindings: mux_load and DemuxFeed hand out the messages the manifest
lists and end with its error, mux_dump writes the canonical vectors byte for
byte, and load_documents and FrameFeed read the frame vectors."""

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
        return _rows(got), e.code.name.lower()
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
        got = (rows, e.code.name.lower())
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


@pytest.mark.parametrize("name", _vectors("frames"))
def test_frame_vectors(name):
    v = _manifest()[name]
    data = (_DIR / v["file"]).read_bytes()
    rows, error = _expected(v)
    assert error == "none"
    feed = stream.FrameFeed()
    docs = [d for i in range(len(data)) for d in feed.feed(data[i:i + 1])]
    feed.finish()
    assert _rows((0, d) for d in docs) == rows
    assert stream.frame_documents(docs) == data
    assert len(stream.load_documents(data)) == len(rows)
