# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Janos Sonntag
"""Spec version directive (`#!bovnar M.N`) — bindings coverage."""

import pytest

from conftest import needs_lib

import bovnar
from bovnar import Reader, ErrorCode, BovnarParseError


@needs_lib
def test_runtime_versions():
    assert bovnar.version() == "1.1.0"
    assert bovnar.spec_version() == (1, 2)


@needs_lib
def test_peek_version():
    assert bovnar.peek_version("#!bovnar 1.1\n.x = 1;") == (1, 1)
    assert bovnar.peek_version(b"\xef\xbb\xbf#!bovnar 1.0\n.x = 1;") == (1, 0)
    assert bovnar.peek_version(".x = 1;") is None
    assert bovnar.peek_version("#!bovnarish 9.9\n.x = 1;") is None


@needs_lib
def test_reader_declared_version():
    with Reader() as r:
        r.read_mem(b"#!bovnar 1.0\n.x = 1;", on_verified=lambda e, d: True)
        assert r.declared_version == (1, 0)
    with Reader() as r:
        r.read_mem(b".x = 1;", on_verified=lambda e, d: True)
        assert r.declared_version is None


@needs_lib
def test_lenient_accepts_future_version():
    # A newer version is recorded but not rejected by default.
    with Reader() as r:
        r.read_mem(b"#!bovnar 2.5\n.x = 1;", on_verified=lambda e, d: True)
        assert r.declared_version == (2, 5)


@needs_lib
def test_strict_rejects_future_version():
    with pytest.raises(BovnarParseError) as ei:
        with Reader() as r:
            r.read_mem(b"#!bovnar 2.0\n.x = 1;",
                       on_verified=lambda e, d: True, strict_version=True)
    assert ei.value.code == ErrorCode.UNSUPPORTED_SPEC_VERSION


@needs_lib
@pytest.mark.parametrize("doc", [
    b"#!bovnar 1\n.x = 1;",       # missing minor
    b"#!bovnar a.b\n.x = 1;",     # non-numeric
    b"#!bovnar 1.01\n.x = 1;",    # leading zero
    b"#!bovnar 1.1 beta\n.x = 1;",  # trailing junk
])
def test_malformed_directive_is_error(doc):
    with pytest.raises(BovnarParseError) as ei:
        with Reader() as r:
            r.read_mem(doc, on_verified=lambda e, d: True)
    assert ei.value.code == ErrorCode.INVALID_SPEC_VERSION


@needs_lib
def test_loads_ignores_directive():
    assert bovnar.loads("#!bovnar 1.1\n.n = 7;") == {"n": 7}
