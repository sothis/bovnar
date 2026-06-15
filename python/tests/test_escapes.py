# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Janos Sonntag
"""Spec 1.1 richer string escapes (\\u{...} and \\xHH) via the bindings."""

import pytest

from conftest import needs_lib

import bovnar
from bovnar import ErrorCode, BovnarParseError


@needs_lib
def test_unicode_escape():
    assert bovnar.loads('#!bovnar 1.1\n.s = "\\u{41}";') == {"s": "A"}
    assert bovnar.loads('#!bovnar 1.1\n.s = "\\u{1F600}";') == {"s": "\U0001F600"}


@needs_lib
def test_byte_escape_forms_utf8():
    # \xC3\xA9 is the UTF-8 encoding of é
    assert bovnar.loads('#!bovnar 1.1\n.s = "caf\\xC3\\xA9";') == {"s": "café"}


@needs_lib
def test_lone_xff_is_invalid_utf8():
    with pytest.raises(BovnarParseError) as ei:
        bovnar.loads('#!bovnar 1.1\n.s = "\\xFF";')
    assert ei.value.code == ErrorCode.INVALID_UTF8_BYTE


@needs_lib
@pytest.mark.parametrize("doc", [
    '#!bovnar 1.1\n.s = "\\u{D800}";',     # surrogate
    '#!bovnar 1.1\n.s = "\\u{110000}";',   # above U+10FFFF
])
def test_invalid_codepoint(doc):
    with pytest.raises(BovnarParseError) as ei:
        bovnar.loads(doc)
    assert ei.value.code == ErrorCode.INVALID_CODEPOINT


@needs_lib
@pytest.mark.parametrize("doc", [
    '.s = "\\u{41}";',   # \u without a 1.1 declaration
    '.s = "\\x41";',     # \x without a 1.1 declaration
])
def test_gated_on_version(doc):
    with pytest.raises(BovnarParseError) as ei:
        bovnar.loads(doc)
    assert ei.value.code == ErrorCode.ILLEGAL_ESCAPE_SEQUENCE


@needs_lib
@pytest.mark.parametrize("doc", [
    '#!bovnar 1.1\n.s = "\\u{0}";',   # NUL
    '#!bovnar 1.1\n.s = "\\x01";',    # control byte
    '#!bovnar 1.1\n.s = "\\x7f";',    # DEL
])
def test_control_bytes_rejected(doc):
    # a control byte may not be smuggled into a string via an escape
    with pytest.raises(BovnarParseError) as ei:
        bovnar.loads(doc)
    assert ei.value.code == ErrorCode.UNEXPECTED_INPUT_BYTE


@needs_lib
def test_whitespace_control_allowed():
    assert bovnar.loads('#!bovnar 1.1\n.s = "a\\x09b";') == {"s": "a\tb"}
