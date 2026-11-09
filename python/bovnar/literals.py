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

"""The C library's validators and literal helpers (``bovnar.h``).

The checks the writer applies to keys, symbols, references and numbers, the
integer and float literal parsers and formatters it uses, and the parser of a
type annotation. Each function is the C function of the same name without the
``bvn_`` prefix and answers exactly as it does; the docstrings say where that
is not what the name suggests. Text holding NUL is none of the things these
check: a validator answers ``False`` for it, a parser raises.
"""

import ctypes
from dataclasses import dataclass

from ._ffi import get_library
from .enums import ErrorCode
from .exceptions import BovnarArgumentError, BovnarParseError
from .structs import ValueTypeSpec, ValueUnit

# The unit parameter's buffer: up to 255 bytes and the NUL (bovnar.h).
_UNIT_BUF = 256
# More than any bvn_format_* writes: 64 digits, a sign and the NUL for the
# integers, a 53-bit significand and its exponent for a double.
_FORMAT_BUF = 256


@dataclass(frozen=True)
class TypeAnnotation:
    """A parsed type annotation: what :func:`parse_type_annotation` returns.

    value_type  : family, width and base (a ``datetime`` holds its epoch index
                  in ``base``, a ``float_fix`` its q)
    unit        : the unit; no components when the annotation has no unit
                  parameter or spells it ``no_unit``
    unit_source : the unit parameter exactly as written, ``''`` when there is
                  none (a ``datetime``'s epoch name is no unit and is not here)
    """
    value_type:  ValueTypeSpec
    unit:        ValueUnit
    unit_source: str


def _text(s, what: str) -> bytes | None:
    """``s`` as the bytes C reads, or None when it holds NUL."""
    if not isinstance(s, str):
        raise TypeError(f"{what} takes a str, not {type(s).__name__}")
    if '\x00' in s:
        return None
    try:
        return s.encode('utf-8')
    except UnicodeEncodeError:
        return None      # a lone surrogate has no UTF-8 form: no literal


def _check(fn, s, *args) -> bool:
    raw = _text(s, fn.__name__)
    return raw is not None and bool(fn(raw, *args))


def parse_type_annotation(text: str) -> TypeAnnotation:
    """``bvn_parse_type_annotation``: the text between the angle brackets of a
    type annotation, ``"uint:32,_16,k~g"`` say, as the reader parses it.

    A refusal is a :class:`BovnarParseError` whose ``code`` is the one the
    reader gives the same annotation in a document: ``ILLEGAL_VALUE_TYPE``
    when the annotation itself is malformed (an unknown family, a parameter
    given twice or not allowed for the family, a width or base out of range),
    ``UNIT_TOO_LONG`` for a unit parameter over 255 bytes, and for one that is
    not a unit this build knows the code :func:`bovnar.unit_error_code` gives
    it (``UNIT_ILLEGAL``, or a profile code). The reader's version gates are
    not applied: ``datetime`` (spec 1.1) and profile units (spec 1.2) parse
    here whatever a document declares.
    """
    if not isinstance(text, str):
        raise TypeError(f"parse_type_annotation takes a str, not "
                        f"{type(text).__name__}")
    try:
        raw = text.encode('utf-8')
    except UnicodeEncodeError:
        raise BovnarParseError(ErrorCode.ILLEGAL_VALUE_TYPE,
                               message=f"{text!r} has no UTF-8 form") from None
    if len(raw) >= 1 << 32:
        raise BovnarArgumentError("a type annotation is at most 4 GiB")
    lib = get_library()
    type_ok, unit_ok, too_long = ctypes.c_bool(), ctypes.c_bool(), ctypes.c_bool()
    unit = ValueUnit()
    buf = ctypes.create_string_buffer(_UNIT_BUF)
    buf_len = ctypes.c_uint8()
    vt = lib.bvn_parse_type_annotation(
        raw, len(raw), ctypes.byref(type_ok), ctypes.byref(unit_ok),
        ctypes.byref(too_long), ctypes.byref(unit), buf, ctypes.byref(buf_len))
    if not type_ok.value:
        raise BovnarParseError(
            ErrorCode.ILLEGAL_VALUE_TYPE,
            message=f"{text!r} is not a type annotation (ILLEGAL_VALUE_TYPE)")
    # The unit parameter is a run of the input between ASCII commas, so it is
    # UTF-8 whenever the input is.
    source = buf.raw[:buf_len.value].decode('utf-8', errors='replace')
    if not unit_ok.value:
        if too_long.value:
            code = ErrorCode.UNIT_TOO_LONG
        elif not source:
            # Brace or bracket nesting past the bound: no text was kept.
            code = ErrorCode.UNIT_ILLEGAL
        else:
            from . import unit_error_code
            code = ErrorCode(unit_error_code(source))
        raise BovnarParseError(
            code, message=f"the unit of {text!r} is refused ({code.name})")
    return TypeAnnotation(vt, unit, source)


def validate_identifier(s: str) -> bool:
    """``bvn_validate_identifier``: may *s* be a key? It starts with an ASCII
    letter, ``_`` or a character from U+00C0 on, and holds no space, control
    character, U+0080 to U+00BF, or punctuation spec §4.2 forbids in a key
    (``-`` and ``+`` are allowed); and it is valid UTF-8. There is no length
    limit here: the reader's ``max_identifier_length`` (255 by default) is
    separate."""
    return _check(get_library().bvn_validate_identifier, s)


def validate_symbol(s: str) -> bool:
    """``bvn_validate_symbol``: a symbol is spelled as a key is."""
    return _check(get_library().bvn_validate_symbol, s)


def validate_reference(s: str) -> bool:
    """``bvn_validate_reference``: a path such as ``.a.b[0].c``, without the
    ``&`` that marks a reference in a document. Every segment is an
    identifier, each optionally followed by ``[digits]`` indexes."""
    return _check(get_library().bvn_validate_reference, s)


def validate_number(s: str) -> bool:
    """``bvn_validate_number``: a decimal number, ``-``? digits with at most
    one ``.`` and an optional exponent (``e`` or ``E``, an optional sign,
    digits). No leading ``+``, and not ``inf``, ``ninf`` or ``nan`` (see
    :func:`is_special_number_string`)."""
    return _check(get_library().bvn_validate_number, s)


def is_special_number_string(s: str) -> bool:
    """``bvn_is_special_number_string``: exactly ``inf``, ``ninf`` or
    ``nan``."""
    return _check(get_library().bvn_is_special_number_string, s)


def _base(base) -> int:
    if type(base) is not int or not 0 <= base < 1 << 32:
        raise BovnarArgumentError(f"a base is an int from 0 to 2**32 - 1, "
                                  f"not {base!r}")
    return base


def validate_digits_for_base(s: str, base: int) -> bool:
    """``bvn_validate_digits_for_base``: an optional ``-`` and at least one
    digit of *base*, the form of an integer literal. In bases 64 and 85,
    where ``+`` and ``-`` are digits, there is no sign. The base is not
    checked against the ones a type may declare: in base 1 the one digit is
    ``0``, and a base above 62 (other than 64 and 85) has the 62 digits of
    base 62."""
    return _check(get_library().bvn_validate_digits_for_base, s, _base(base))


def validate_number_in_base(s: str, base: int) -> bool:
    """``bvn_validate_number_in_base``: the form of a quoted float literal in
    *base*: either sign, digits of *base* with at most one ``.``, and an
    exponent marked ``e``/``E`` while ``e`` is no digit (bases up to 14) or
    ``p``/``P`` in the power-of-two bases up to 16, its digits decimal. Bases
    64 and 85 take only :func:`validate_digits_for_base`'s digit strings."""
    return _check(get_library().bvn_validate_number_in_base, s, _base(base))


def _width(width) -> int:
    if type(width) is not int or not 0 <= width < 1 << 32:
        raise BovnarArgumentError(f"a width is an int from 0 to 2**32 - 1, "
                                  f"not {width!r}")
    return width


def validate_uint_range(s: str, width: int, base: int = 10) -> bool:
    """``bvn_validate_uint_range``: does the unsigned literal *s* in *base* fit
    *width* bits? A width of 0 means unconstrained, and ``inf``, ``ninf`` and
    ``nan`` pass. This is a range test only, made on text the digit check has
    already passed: the empty string passes it, and so does text with a
    leading space or ``+`` in base 10."""
    return _check(get_library().bvn_validate_uint_range, s, _width(width),
                  _base(base))


def validate_sint_range(s: str, width: int, base: int = 10) -> bool:
    """``bvn_validate_sint_range``: :func:`validate_uint_range` for a signed
    literal, from ``-2**(width-1)`` to ``2**(width-1) - 1``."""
    return _check(get_library().bvn_validate_sint_range, s, _width(width),
                  _base(base))


def validate_string(data) -> bool:
    """``bvn_validate_string``: are the bytes *data* well-formed UTF-8 (no
    overlong form, no surrogate, nothing past U+10FFFF, no sequence cut
    short)? The same test as ``data.decode('utf-8')``. It says nothing about
    the control bytes the writer refuses in a string: NUL passes."""
    if isinstance(data, str):
        raise TypeError("validate_string takes bytes; a str is UTF-8 "
                        "whenever it encodes")
    raw = bytes(data)
    return bool(get_library().bvn_validate_string(raw, len(raw)))


def char_to_digit(ch: str, base: int) -> int | None:
    """``bvn_char_to_digit``: the value of the digit *ch* (one character) in
    *base*, ``None`` when it is no digit of that base. Up to base 36 a
    letter's case does not matter; above it ``a``-``z`` are 10-35 and
    ``A``-``Z`` 36-61. Bases 64 and 85 use the Base64 and Ascii85
    alphabets."""
    if not isinstance(ch, str) or len(ch) != 1:
        raise TypeError(f"char_to_digit takes one character, not {ch!r}")
    base = _base(base)
    d = int(get_library().bvn_char_to_digit(ord(ch), base))
    return d if d < base else None


def min_digits_for_type(vt: ValueTypeSpec) -> int:
    """``bvn_min_digits_for_type``: how many digits of the type's base the
    largest value of an integer type of up to 64 bits takes (``uint:8,_16``
    is 2). 0 for a width of 0 or above 64 and for a type that is not
    numeric; a float type is answered as if it were an unsigned integer of its
    width."""
    return int(get_library().bvn_min_digits_for_type(vt))


def _format(fn, *args) -> str | None:
    buf = ctypes.create_string_buffer(_FORMAT_BUF)
    n = fn(buf, _FORMAT_BUF, *args)
    return None if n < 0 else buf.raw[:n].decode('ascii')


def format_uint64(value: int, base: int = 10, min_digits: int = 0) -> str:
    """``bvn_format_uint64``: *value* in *base*, padded with zero digits to
    *min_digits* (which the C function caps at 64). Raises
    BovnarArgumentError for a base other than 2 to 62, 64 or 85, and for a
    value outside 0 to 2**64 - 1."""
    if type(value) is not int or not 0 <= value < 1 << 64:
        raise BovnarArgumentError(f"format_uint64: {value!r} is not a uint64")
    text = _format(get_library().bvn_format_uint64, value, _base(base),
                   _width(min_digits))
    if text is None:
        raise BovnarArgumentError(
            f"format_uint64: base {base} is not one a literal takes")
    return text


def format_int64(value: int, base: int = 10, min_digits: int = 0) -> str:
    """``bvn_format_int64``: as :func:`format_uint64`, with a ``-`` before a
    negative value. Also raises for a negative value in base 64 or 85, which
    have no sign, and for a value outside the int64 range."""
    if type(value) is not int or not -(1 << 63) <= value < 1 << 63:
        raise BovnarArgumentError(f"format_int64: {value!r} is not an int64")
    text = _format(get_library().bvn_format_int64, value, _base(base),
                   _width(min_digits))
    if text is None:
        raise BovnarArgumentError(
            f"format_int64: {value} cannot be written in base {base}")
    return text


def format_double(value: float, vt: ValueTypeSpec) -> str:
    """``bvn_format_double``: the literal the writer emits for *value* under
    type *vt*: the shortest digits that read back to the same double
    (``0.1`` is ``1e-1``), in the type's base, 10 (also for a base of 0) or 16
    for a hex float; any other base raises BovnarArgumentError. A ``float``
    width below 53 rounds the value first to a significand of that many bits:
    16 for ``float:16`` and 32 for ``float:32``, not the 11 and 24 of IEEE
    binary16 and binary32."""
    text = _format(get_library().bvn_format_double, float(value), vt)
    if text is None:
        raise BovnarArgumentError(f"format_double: {value!r} under {vt!r}")
    return text


def _parse(name: str, fn, out, s, *args):
    raw = _text(s, name)
    if raw is None or not fn(raw, *args, ctypes.byref(out)):
        what = f"{args[0]!r}" if isinstance(args[0], ValueTypeSpec) \
            else f"base {args[0]}"
        raise BovnarArgumentError(f"{name}: {s!r} is not a value of {what}")
    return out.value


def parse_int64(s: str, vt: ValueTypeSpec) -> int:
    """``bvn_parse_int64``: the signed literal *s* in the type's base, refused
    (BovnarArgumentError) when it does not fit the type's width (64 when it
    declares none). No leading ``+`` or space. In bases up to 36 the C
    library reads with ``strtoll``, so in base 16 a ``0x`` prefix is taken
    (``"0x10"`` is 16), which no document can carry."""
    return int(_parse('parse_int64', get_library().bvn_parse_int64,
                      ctypes.c_int64(), s, vt))


def parse_uint64(s: str, vt: ValueTypeSpec) -> int:
    """``bvn_parse_uint64``: :func:`parse_int64` for an unsigned literal;
    there is no sign (in bases 64 and 85 ``+`` and ``-`` are digits)."""
    return int(_parse('parse_uint64', get_library().bvn_parse_uint64,
                      ctypes.c_uint64(), s, vt))


def parse_double(s: str, vt: ValueTypeSpec) -> float:
    """``bvn_parse_double``: :func:`parse_double_in_base` in the type's
    base."""
    return float(_parse('parse_double', get_library().bvn_parse_double,
                        ctypes.c_double(), s, vt))


def parse_double_in_base(s: str, base: int) -> float:
    """``bvn_parse_double_in_base``: the float literal *s* as the nearest
    double, rounded once from its exact value. Base 10 or 16 only (a hex
    float has no ``0x`` and its exponent is ``p``); either sign, and ``inf``,
    ``ninf`` and ``nan``, but not ``-inf``. Raises BovnarArgumentError
    otherwise."""
    return float(_parse('parse_double_in_base',
                        get_library().bvn_parse_double_in_base,
                        ctypes.c_double(), s, _base(base)))


def escape_repl_table() -> dict[str, str]:
    """``bvn_get_escape_repl_table``: each escape letter (the character after
    ``\\`` in a string) mapped to the character it stands for: ``t`` to TAB,
    ``n``, ``v``, ``f``, ``r``, ``"`` and ``\\``. The ``\\x`` and ``\\u``
    escapes of spec 1.1 take digits and are not in it."""
    table = get_library().bvn_get_escape_repl_table()
    return {chr(i): chr(table[i]) for i in range(256) if table[i]}


__all__ = [
    'TypeAnnotation', 'parse_type_annotation',
    'validate_identifier', 'validate_symbol', 'validate_reference',
    'validate_number', 'is_special_number_string',
    'validate_digits_for_base', 'validate_number_in_base',
    'validate_uint_range', 'validate_sint_range', 'validate_string',
    'char_to_digit', 'min_digits_for_type',
    'format_uint64', 'format_int64', 'format_double',
    'parse_int64', 'parse_uint64', 'parse_double', 'parse_double_in_base',
    'escape_repl_table',
]
