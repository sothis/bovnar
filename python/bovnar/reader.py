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



import ctypes
from dataclasses import dataclass, field
from typing import Callable, Generator, IO, Sequence

from ._ffi import get_library
from ._fileio import PullSource, push_sink, is_stream
from .enums import Event, ErrorCode, event_from_value
from .exceptions import (BovnarParseError, BovnarArgumentError,
                         BovnarWriteError)
from .structs import (
    BvnrSource, BvnrSink, BvnrReadFlags, BvnrData, ValueUnit,
    build_unit_policy, describe_unit_policy_refusal, MAX_UNIT_TARGETS,
    EVENT_CALLBACK_FUNC, WANT_UNIT_FUNC, ON_ERROR_FUNC,
    make_unit_dimensionless,
)
from .unit import _as_vu

MAX_FILESIZE_BYTES = 16 * 1024 * 1024


@dataclass
class UnitRule:
    """A rule about ONE field, named by its key path.

    `path` is the dotted path a value sits at, leading dot included, exactly as
    the document writes its keys: ``".inlet.temperature"``. A path ending in
    ``".*"`` matches everything below that point at any depth. Array elements
    sit at the path of the assignment holding them, so one rule covers a whole
    array.

    Unlike the whole-document `targets` list, a rule is a statement about a
    field the caller NAMED, so a value it cannot be applied to raises
    ``UNIT_MISMATCH`` rather than being passed through — silence would defeat
    the point of naming it. That includes a bare number: ".speed is m/s" is not
    satisfied by a value with no unit.

    With ``convert=False`` the rule asserts only, and the value is delivered
    exactly as the document wrote it.
    """
    path:    str
    unit:    str
    base:    int = 0
    convert: bool = True


@dataclass
class UnitPolicy:
    """What the document must contain, and what unit values arrive in.

    The declarative form of the ``want_unit`` callback: everything is unit
    TEXT, so this is reachable from Python without a per-value trampoline.
    Install it with :meth:`Reader.set_unit_policy`.

    Per-field rules
      ``rules`` is a list of :class:`UnitRule`, matched by key path and
      consulted before everything else — the most specific thing a policy can
      say. First match wins, so order matters: put ``".a.b"`` before ``".a.*"``.

    Conversion
      ``targets`` names the units to convert to. Each entry is either ``"m/s"``
      or ``("m/s", base)``. Each numeric value takes the FIRST target it can
      validly convert to, so order is significant — ``["m", "k~m"]`` never
      selects ``k~m``. A value that matches nothing, or that is already in the
      unit a target names, is delivered untouched; ``data.converted`` is what
      tells the two apart.

      ``normalise_si`` catches whatever the targets did not, delivering it in
      coherent SI base units with prefixes folded out. Currencies and every
      DIMENSIONLESS unit (%, ppm, dB, pH, rad, °) are left as written rather
      than reinterpreted.

      A value with NO unit only ever matches a target that is itself
      ``no_unit``. A bare number is dimensionally compatible with % and ppm, so
      without that fence a policy naming ``"%"`` would deliver 0.25 as 25.

    Exactness
      Nothing approximate is ever delivered. ``leave_inexact`` hands over a
      value this conversion cannot deliver exactly — an irrational factor, a
      non-terminating expansion such as 42 km/h → 35/3 m/s, an exponent past
      the work limit — in its NATIVE unit instead of failing the parse. Without
      it, such a value raises. It applies only to policy-chosen targets; a
      ``want_unit`` hook keeps its strict all-or-nothing contract.

    Validation
      ``require_unit`` rejects a document containing any bare numeric value.
      ``require_dimension_of`` requires every numeric value to be validly
      convertible to at least one of the listed units — "this document is
      lengths, in whatever unit it wrote them". Both are evaluated on the unit
      the DOCUMENT wrote, before any conversion, and both raise
      ``ErrorCode.UNIT_MISMATCH``.
    """
    rules:                Sequence['UnitRule'] = field(default_factory=tuple)
    targets:              Sequence[str | tuple[str, int]] = field(default_factory=tuple)
    normalise_si:         bool = False
    base:                 int = 0
    leave_inexact:        bool = False
    require_unit:         bool = False
    require_dimension_of: Sequence[str] = field(default_factory=tuple)

class _CollectingHandler:


    def __init__(self) -> None:
        self._events: list[tuple[Event, bytes, object, object]] = []

    def __call__(self, ev: Event, data: BvnrData | None) -> bool:
        raw = data.raw_bytes() if data else b''
        vt  = data.value_type if data else None
        vu  = data.value_unit if data else None
        self._events.append((ev, raw, vt, vu))
        return True

class EventPayload:


    __slots__ = ('event', 'raw', 'value_type', 'value_unit',
                 'converted', 'converted_text', 'converted_base')

    def __init__(self,
                 event: Event,
                 raw: bytes,
                 value_type,
                 value_unit,
                 converted: bool = False,
                 converted_text: "str | None" = None,
                 converted_base: int = 0) -> None:
        self.event      = event
        self.raw        = raw
        self.value_type = value_type
        self.value_unit = value_unit
        # Lossless read-time unit/base conversion (see Reader want_unit). When
        # True, converted_text is the EXACT value in the requested unit and base
        # (converted_base); `raw` keeps the original text. See BvnrData.conv.
        # converted_text is None even with converted True when the result has no
        # terminating expansion in that base and the reader allowed it through
        # (want_unit_allow_nonterminating).
        self.converted      = converted
        self.converted_text = converted_text
        self.converted_base = converted_base

    @property
    def text(self) -> str:
        return self.raw.decode('utf-8', errors='replace') if self.raw else ''

    def __repr__(self) -> str:
        return f"EventPayload(event={self.event.name}, text={self.text!r})"

def _read_limit(name: str, value, bits: int = 64) -> int:
    """A read limit: an int that fits its C field of `bits` bits (ctypes
    would wrap a negative one or cut a large one silently)."""
    if type(value) is not int or not 0 <= value < (1 << bits):
        raise BovnarArgumentError(
            f"{name} must be an int from 0 to 2**{bits} - 1, not {value!r}")
    return value


def _read_switch(name: str, value) -> bool:
    """A read switch: a bool (or 0 / 1). Any other value is refused rather
    than taken by its truth, where continue_on_error='no' would turn
    recovery on."""
    if type(value) is bool or (type(value) is int and value in (0, 1)):
        return bool(value)
    raise BovnarArgumentError(f"{name} must be a bool, not {value!r}")


# Every field of bvnr_read_flags_t that is a limit or a switch, with the width
# of its C field (0 for a bool), in the one table each reading entry point takes
# its keywords from -- Reader.read_*, the feeds, loads/load and the stream
# functions -- so a field added in C is added everywhere at once. Every
# default is 0 / False: 0 selects the library's own default for a limit
# (max_file_size 0 is no total-byte cap), except the two nesting limits,
# where 0 is 255, the format's limit and what the DOM and the CLI read at (the
# C reader's own default is 64).
READ_OPTIONS = {
    'max_file_size':                  64,
    'max_text_bytes':                 64,
    'max_array_items':                64,
    'max_identifier_length':          16,
    'max_string_length':              16,
    'max_number_length':              16,
    'max_symbol_length':              16,
    'max_reference_length':           16,
    'max_struct_nesting':             8,
    'max_array_nesting':              8,
    'max_conversion_length':          32,
    'continue_on_error':              0,
    'strict_version':                 0,
    'text_only':                      0,
    'want_unit_allow_nonterminating': 0,
}


def read_flags(where: str, options: dict,
               flags: BvnrReadFlags | None = None) -> BvnrReadFlags:
    """`flags` (a fresh BvnrReadFlags by default) with the READ_OPTIONS in
    `options` set, each checked; an unknown keyword is a TypeError naming
    `where`, as Python reports one for a function."""
    if flags is None:
        flags = BvnrReadFlags()
    for name in options:
        if name not in READ_OPTIONS:
            raise TypeError(
                f"{where}() got an unexpected keyword argument {name!r}")
    for name, bits in READ_OPTIONS.items():
        value = options.get(name, 0)
        if bits:
            value = _read_limit(name, value, bits)
        else:
            value = _read_switch(name, value)
        setattr(flags, name, value)
    if not flags.max_struct_nesting:
        flags.max_struct_nesting = 255
    if not flags.max_array_nesting:
        flags.max_array_nesting = 255
    return flags


def _parse_error(lib, code: int, line: int, column: int, offset: int,
                 byte: int, prefix: str = '') -> BovnarParseError:
    msg_b = lib.bvn_error_to_string(int(code))
    msg   = msg_b.decode('utf-8') if msg_b else ''
    return BovnarParseError(ErrorCode(code), line, column, offset, byte,
                            prefix + msg)


class Reader:


    def __init__(self) -> None:
        # _ptr and _busy first: get_library() can raise BovnarLibraryNotFound,
        # and __del__ then runs on a half-built object.
        self._ptr  = None
        self._busy = False
        self._lib  = get_library()
        self._open()

    def _open(self) -> None:
        ptr = self._lib.bvnr_reader_create()
        if not ptr:
            raise MemoryError("bvnr_reader_create() returned NULL")
        self._ptr = ptr

    def close(self) -> None:

        # Destroying the reader while C is still inside bvnr_read() -- i.e.
        # from one of this reader's own callbacks -- frees the state the parse
        # is running on. Refuse; the callback's exception aborts the read and
        # the caller can close once read_*() has returned.
        self._check_not_busy()
        if self._ptr is not None:
            self._lib.bvnr_reader_destroy(self._ptr)
            self._ptr = None

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()

    def __del__(self):
        # Never destroy mid-read (cannot normally happen: the running read
        # holds a reference to self), and never raise from a finaliser.
        if getattr(self, '_busy', False):
            return
        ptr = getattr(self, '_ptr', None)
        if ptr is not None:
            self._lib.bvnr_reader_destroy(ptr)
            self._ptr = None

    def _check_open(self) -> None:
        if self._ptr is None:
            raise BovnarArgumentError("Reader has been closed")

    def _check_not_busy(self) -> None:
        if getattr(self, '_busy', False):
            raise BovnarArgumentError("Reader is in use by its own callback")

    def _run_read(self, open_fn) -> bool:
        # Open and parse with the busy flag held, so a callback cannot close
        # the reader, re-open it on another input, or swap its unit policy
        # while bvnr_read() is still walking the current one.
        self._busy = True
        try:
            if not open_fn():
                self._raise_error()
            return bool(self._lib.bvnr_read(self._ptr))
        finally:
            self._busy = False

    def _build_flags(self,
                     where: str,
                     on_verified=None,
                     on_unverified=None,
                     want_unit=None,
                     on_error=None,
                     options: dict | None = None,
                     raw: bool = False) -> tuple:
        """The C read flags for `options` (READ_OPTIONS keywords) and the
        callbacks, with the ctypes callbacks to keep alive. The callbacks
        share one state, so the first exception any of them raises stops the
        others and is the one re-raised."""
        flags   = read_flags(where, options or {})
        cb_refs = []
        state   = {'exc': None}

        if on_verified is not None:
            cb = self._wrap_callback(on_verified, state, raw)
            cb_refs.append(cb)
            flags.on_verified = cb

        if on_unverified is not None:
            cb = self._wrap_callback(on_unverified, state)
            cb_refs.append(cb)
            flags.on_unverified = cb

        if want_unit is not None:
            cb = self._wrap_want_unit(want_unit, state)
            cb_refs.append(cb)
            flags.want_unit = cb

        if on_error is not None:
            cb = self._wrap_on_error(on_error, state, self._lib)
            cb_refs.append(cb)
            flags.on_error = cb
        return flags, cb_refs

    @staticmethod
    def _snapshot(src: BvnrData) -> BvnrData:
        # Copy a C-owned BvnrData into a Python-owned one.
        #
        # `data_ptr.contents` is a VIEW over the reader's memory, not a copy:
        # every field read through it dereferences C storage that dies with the
        # reader, so an object a callback stashes away segfaults later. Every
        # callback must therefore hand Python a snapshot, never the view.
        #
        # The by-value fields below become genuinely Python-owned, and so do
        # the pointer fields (`data`, `frac_data`, `unit_source`, `conv.num`,
        # `conv.den`): _own_buffers() replaces each with a copy the snapshot
        # owns. They used to be carried over as raw addresses, and a BvnrData
        # kept past the callback read freed reader memory.
        unit_size = ctypes.sizeof(ValueUnit)
        snap = BvnrData()
        snap.type              = src.type
        snap.value_type.family = src.value_type.family
        snap.value_type.width  = src.value_type.width
        snap.value_type.base   = src.value_type.base
        ctypes.memmove(ctypes.addressof(snap.value_unit),
                       ctypes.addressof(src.value_unit),
                       unit_size)
        snap.length = src.length
        snap.data   = src.data
        # spec 1.1 — an ISO datetime literal's sub-second digits.
        snap.frac_data   = src.frac_data
        snap.frac_length = src.frac_length
        # library 2.0 — the unit's own spelling; reader-owned like the two above.
        snap.unit_source        = src.unit_source
        snap.unit_source_length = src.unit_source_length
        # Lossless read-time conversion result (see want_unit). The exact `text`
        # lives in reader-owned memory valid only during this call, so copy it
        # into the snapshot's own carrier now.
        snap.converted = src.converted
        if src.converted:
            snap.conv.base   = src.conv.base
            snap.conv.length = src.conv.length
            ctypes.memmove(ctypes.addressof(snap.conv.unit),
                           ctypes.addressof(src.conv.unit),
                           unit_size)
            # reading a c_char_p yields a fresh bytes copy; assigning it back
            # lets ctypes keep it alive past the callback. None when the result
            # does not terminate in the output base
            # (want_unit_allow_nonterminating) — the exact value then lives in
            # conv.num/conv.den, copied below.
            snap.conv.text = src.conv.text
            snap.conv.num  = src.conv.num
            snap.conv.den  = src.conv.den
        snap._own_buffers()
        return snap

    @staticmethod
    def _wrap_callback(py_fn: Callable, state: dict | None = None,
                       raw: bool = False):
        """The C callback for `py_fn`.

        Each event normally gets a fresh BvnrData that owns everything it
        points at (see _snapshot), so a callback may keep it -- or a
        sub-struct view like d.value_type -- and use every accessor after the
        reader is gone. ``raw`` passes the reader-owned event block itself
        instead, valid only for the call: the package's own parsers read it
        and keep nothing, and for them the snapshot was a 1136-byte copy plus
        four owned buffers per event.
        """

        if state is None:
            state = {'exc': None}

        def _c_cb(userdata_void, event_int: int, data_ptr) -> bool:
            if state['exc'] is not None:
                return False
            try:
                ev = event_from_value(event_int)
                if raw:
                    result = py_fn(ev, data_ptr.contents if data_ptr else None)
                else:
                    data = Reader._snapshot(data_ptr.contents) if data_ptr else None
                    result = py_fn(ev, data)
                return bool(result) if result is not None else True
            except BaseException as exc:
                if state['exc'] is None:
                    state['exc'] = exc
                return False

        cb = EVENT_CALLBACK_FUNC(_c_cb)
        cb._bvnr_state = state
        return cb

    @staticmethod
    def _wrap_on_error(py_fn: Callable, state: dict, lib):
        # on_error(BovnarParseError) for every error the reader reports, the
        # recovered ones of continue_on_error included; C ignores what it
        # returns. An exception it raises stops the read at the next event.
        def _c_cb(userdata_void, err, line, column, byte, offset) -> None:
            if state['exc'] is not None:
                return
            try:
                py_fn(_parse_error(lib, int(err), int(line), int(column),
                                   int(offset), int(byte)))
            except BaseException as exc:
                state['exc'] = exc

        cb = ON_ERROR_FUNC(_c_cb)
        cb._bvnr_state = state
        return cb

    @staticmethod
    def _wrap_want_unit(py_fn: Callable, state: dict | None = None):
        # Wrap a Python want_unit(data) into the C want_unit contract
        #   bool(*)(void*, const bvnr_data_t*, value_unit_t*, uint32_t*).
        # The Python callback returns one of:
        #   None                       -> decline (value delivered untouched)
        #   ValueUnit                  -> convert to that unit, keep native base
        #   (ValueUnit, base:int)      -> convert to that unit and output base
        #                                 (0 keeps native; otherwise any base bvnr
        #                                  writes: 2..62, 64 or 85)
        # The conversion is lossless. An incompatible unit becomes
        # error_unit_mismatch, an irrational factor error_unit_inexact, an
        # unusable base error_invalid_argument — each aborts the parse. So does a
        # result that does not terminate in the output base, unless the reader was
        # given want_unit_allow_nonterminating=True.
        if state is None:
            state = {'exc': None}
        unit_size = ctypes.sizeof(ValueUnit)

        def _c_cb(userdata_void, data_ptr, want_ptr, want_base_ptr) -> bool:
            if state['exc'] is not None:
                return False
            try:
                # Snapshot, never the raw view: a callback that stashes the
                # object away would otherwise be holding freed reader memory.
                data = Reader._snapshot(data_ptr.contents) if data_ptr else None
                res = py_fn(data)
                if res is None:
                    return False
                if isinstance(res, tuple):
                    want, base = res
                else:
                    want, base = res, 0
                # A Unit is a unit: Quantity.unit and Unit.parse() hand those
                # out, and refusing one here sent callers hunting for `.raw`.
                want = _as_vu(want)
                if not isinstance(want, ValueUnit):
                    raise BovnarArgumentError(
                        "want_unit callback must return a ValueUnit or Unit, "
                        "(unit, base), or None")
                ctypes.memmove(ctypes.addressof(want_ptr.contents),
                               ctypes.addressof(want), unit_size)
                if want_base_ptr:
                    want_base_ptr.contents.value = int(base)
                return True
            except BaseException as exc:
                if state['exc'] is None:
                    state['exc'] = exc
                return False

        cb = WANT_UNIT_FUNC(_c_cb)
        cb._bvnr_state = state
        return cb

    def _raise_error(self) -> None:
        lib = self._lib
        raise _parse_error(lib, lib.bvnr_reader_get_error(self._ptr),
                           lib.bvnr_reader_get_error_line(self._ptr),
                           lib.bvnr_reader_get_error_column(self._ptr),
                           lib.bvnr_reader_get_error_offset(self._ptr),
                           lib.bvnr_reader_get_error_byte(self._ptr))

    def read_mem(self,
                 data: bytes | bytearray | memoryview,
                 *,
                 on_verified: Callable | None = None,
                 on_unverified: Callable | None = None,
                 want_unit: Callable | None = None,
                 on_error: Callable | None = None,
                 mirror=None,
                 _raw: bool = False,
                 **options) -> None:
        """Parse a document held in memory.

        ``on_verified(event, data)`` gets every event the validator accepted,
        ``on_unverified`` every raw lexer event; returning ``False`` stops the
        read. ``want_unit(data)`` picks the unit (and base) a numeric value is
        delivered in. ``on_error(err)`` gets each error the reader reports as
        a :class:`BovnarParseError` with its position -- under
        ``continue_on_error`` the errors it recovers from, which are reported
        nowhere else, and the one that ends a read.

        ``mirror`` -- a writable binary file object, a socket or a
        ``bytearray`` (appended to) -- receives every byte the reader
        consumes: the input mirror of ``bvnr_open_read_source``, a debugging
        aid. The bytes reach it a piece at a time, each once the reader has
        parsed it -- a buffer pulled (up to 64 KiB), a piece fed, an
        octet-stream chunk -- and a read that fails or is stopped has mirrored
        the input up to the byte it stopped at. An exception
        the object raises ends the read and is re-raised from here,
        unchanged.

        The other keywords are the read options of :data:`READ_OPTIONS`, each
        ``0`` / ``False`` by default: the limits ``max_file_size`` (0: no
        total-byte cap), ``max_text_bytes``, ``max_array_items``,
        ``max_identifier_length``, ``max_string_length``,
        ``max_number_length``, ``max_symbol_length``,
        ``max_reference_length``, ``max_struct_nesting`` and
        ``max_array_nesting`` (0: 255), and ``max_conversion_length`` (0
        selects the library's default for each), and the switches
        ``continue_on_error``, ``strict_version``, ``text_only`` and
        ``want_unit_allow_nonterminating``.
        """
        self._check_open()
        self._check_not_busy()
        if isinstance(data, memoryview):
            data = bytes(data)
        if not isinstance(data, (bytes, bytearray)):
            raise BovnarArgumentError("data must be bytes, bytearray, or memoryview")

        flags, cb_refs = self._build_flags(
            'read_mem', on_verified, on_unverified, want_unit, on_error,
            options, _raw)

        buf = (ctypes.c_char * len(data)).from_buffer_copy(data)
        if mirror is not None:
            # bvnr_open_read_mem mirrors into a fixed buffer only; a memory
            # source with a callback sink is the same read.
            src = BvnrSource()
            self._lib.bvnr_source_from_mem(ctypes.byref(src), buf, len(data))
            self._read_source(src, flags, cb_refs, mirror=mirror, keep=buf)
            return
        ok = self._run_read(lambda: self._lib.bvnr_open_read_mem(
            self._ptr,
            buf, len(data),
            None, 0,
            ctypes.byref(flags),
        ))

        cb_exc = None
        for cb in cb_refs:
            st = getattr(cb, '_bvnr_state', None)
            if st is not None and st.get('exc') is not None:
                cb_exc = st['exc']
                break

        del cb_refs
        if cb_exc is not None:
            raise cb_exc
        if not ok:
            self._raise_error()

    def read_fd(self,
                fd: int,
                *,
                on_verified: Callable | None = None,
                on_unverified: Callable | None = None,
                want_unit: Callable | None = None,
                on_error: Callable | None = None,
                mirror=None,
                **options) -> None:
        """Parse a document read from file descriptor ``fd``; the keywords
        are those of :meth:`read_mem`."""
        self._check_open()
        self._check_not_busy()
        if type(fd) is not int or fd < 0 or fd > 0x7FFFFFFF:
            raise BovnarArgumentError(f"Invalid file descriptor: {fd!r}")

        flags, cb_refs = self._build_flags(
            'read_fd', on_verified, on_unverified, want_unit, on_error,
            options)

        src = BvnrSource()
        self._lib.bvnr_source_from_fd(ctypes.byref(src), fd)
        self._read_source(src, flags, cb_refs, mirror=mirror)

    def read_stream(self,
                    stream,
                    *,
                    on_verified: Callable | None = None,
                    on_unverified: Callable | None = None,
                    want_unit: Callable | None = None,
                    on_error: Callable | None = None,
                    mirror=None,
                    _raw: bool = False,
                    **options) -> None:
        """Parse a document pulled from a socket or a binary file object.

        The keyword parameters are those of :meth:`read_mem`. The bytes are
        pulled as the parse needs them (``recv_into``, ``readinto1``,
        ``readinto`` or ``read``), so the document is never held whole, and it
        ends where the object reports end of input. An exception the object
        raises is re-raised from here, unchanged.
        """
        self._check_open()
        self._check_not_busy()
        flags, cb_refs = self._build_flags(
            'read_stream', on_verified, on_unverified, want_unit, on_error,
            options, _raw)
        source = PullSource(stream)
        self._read_source(source.handle, flags, cb_refs, source, mirror=mirror)

    def _read_source(self, src, flags, cb_refs, source=None, mirror=None,
                     keep=None) -> None:
        # `keep`: the memory a memory source reads, alive until the read is.
        sink = push_sink(mirror) if mirror is not None else None
        ok = self._run_read(lambda: self._lib.bvnr_open_read_source(
            self._ptr,
            ctypes.byref(src),
            ctypes.byref(sink.handle) if sink is not None else None,
            ctypes.byref(flags),
        ))
        del keep

        # The transports' own exceptions first: they are what stopped the
        # read.
        cb_exc = source.exc if source is not None else None
        if cb_exc is None and sink is not None:
            cb_exc = sink.exc
        for cb in cb_refs:
            if cb_exc is not None:
                break
            st = getattr(cb, '_bvnr_state', None)
            if st is not None and st.get('exc') is not None:
                cb_exc = st['exc']

        del cb_refs
        if cb_exc is not None:
            raise cb_exc
        if not ok:
            self._raise_error()

    def read_file(self,
                  path: str,
                  *,
                  on_verified: Callable | None = None,
                  on_unverified: Callable | None = None,
                  want_unit: Callable | None = None,
                  on_error: Callable | None = None,
                  mirror=None,
                  max_file_size: int = MAX_FILESIZE_BYTES,
                  **options) -> None:
        """Parse the file at ``path``; the keywords are those of
        :meth:`read_mem`, except that ``max_file_size`` defaults to
        ``MAX_FILESIZE_BYTES`` (16 MiB) here."""
        import os
        # Check the keywords before opening the file.
        read_flags('read_file', options)
        fd = os.open(path, os.O_RDONLY)
        try:
            self.read_fd(
                fd,
                on_verified=on_verified,
                on_unverified=on_unverified,
                want_unit=want_unit,
                on_error=on_error,
                mirror=mirror,
                max_file_size=max_file_size,
                **options,
            )
        finally:
            os.close(fd)

    def iter_mem(self,
                 data: bytes | bytearray | memoryview,
                 *,
                 verified_only: bool = True,
                 **options) -> Generator[EventPayload, None, None]:
        """Read ``data`` and yield its events as :class:`EventPayload`
        objects, verified ones only unless ``verified_only`` is false. The
        other keywords are the read options of :meth:`read_mem`."""

        events: list[EventPayload] = []

        def collector(ev: Event, d):
            raw = d.raw_bytes() if d else b''
            vt  = d.value_type  if d else None
            vu  = d.value_unit  if d else None
            cvd = bool(d.converted) if d else False
            ctxt = d.converted_str() if (d and cvd) else None
            cbase = int(d.conv.base) if (d and cvd) else 0
            events.append(EventPayload(ev, raw, vt, vu, cvd, ctxt, cbase))
            return True

        if verified_only:
            self.read_mem(data, on_verified=collector, **options)
        else:
            self.read_mem(data,
                          on_verified=collector,
                          on_unverified=collector,
                          **options)

        yield from events

    @property
    def error_code(self) -> ErrorCode:
        self._check_open()
        return ErrorCode(self._lib.bvnr_reader_get_error(self._ptr))

    @property
    def error_line(self) -> int:
        self._check_open()
        return self._lib.bvnr_reader_get_error_line(self._ptr)

    @property
    def error_column(self) -> int:
        self._check_open()
        return self._lib.bvnr_reader_get_error_column(self._ptr)

    @property
    def error_offset(self) -> int:
        self._check_open()
        return self._lib.bvnr_reader_get_error_offset(self._ptr)

    @property
    def error_byte(self) -> int:
        """The byte the last error was raised on, or 0 where it is not one
        byte's fault."""
        self._check_open()
        return self._lib.bvnr_reader_get_error_byte(self._ptr)

    @property
    def recovery_count(self) -> int:
        self._check_open()
        return self._lib.bvnr_reader_get_recovery_count(self._ptr)

    def set_unit_policy(self, policy: UnitPolicy | None) -> None:
        """Install (or, with ``None``, clear) this reader's unit policy.

        Every unit string is parsed here, so a typo raises before a byte of the
        document is read; a rejected policy leaves the previous one in force.
        The policy may be set before or after a read and survives re-reading
        the same reader on another document — it describes the consumer, not
        the document.

            r = Reader()
            r.set_unit_policy(UnitPolicy(targets=["m/s", "°C"],
                                         normalise_si=True,
                                         leave_inexact=True))
            r.read_file("sensors.bvnr", on_verified=handler)
        """
        self._check_open()
        self._check_not_busy()
        if policy is None:
            if not self._lib.bvnr_reader_set_unit_policy(self._ptr, None):
                raise BovnarArgumentError("bvnr_reader_set_unit_policy failed")
            return
        try:
            cp, keepalive = build_unit_policy(
                policy.targets, policy.base, policy.normalise_si,
                policy.leave_inexact, policy.require_unit,
                policy.require_dimension_of, policy.rules)
        except ValueError as e:
            raise BovnarArgumentError(str(e)) from None
        # `keepalive` holds the encoded unit strings the struct points at; it
        # must stay referenced until the call returns, which it does by being a
        # live local here.
        if not self._lib.bvnr_reader_set_unit_policy(self._ptr, ctypes.byref(cp)):
            raise BovnarArgumentError(describe_unit_policy_refusal(
                self._lib, policy.targets, policy.base, policy.normalise_si,
                policy.leave_inexact, policy.require_unit,
                policy.require_dimension_of, policy.rules))
        del keepalive

    @property
    def skipped_bytes(self) -> int:
        """Bytes the reader consumed and DISCARDED while recovering.

        ``recovery_count`` says how often the parser had to recover; this says
        what it cost, and it is the only way to learn that: the skipped bytes
        were never parsed, so no callback ever mentions them. A non-zero value
        means the document your callbacks saw is not the whole document.
        """
        self._check_open()
        return self._lib.bvnr_reader_get_skipped_bytes(self._ptr)

    @property
    def declared_version(self):
        """The (major, minor) spec version declared by a leading
        ``#!bovnar M.N`` directive, or ``None`` if the document carried none.
        Valid after a read."""
        import ctypes
        self._check_open()
        maj, mn = ctypes.c_uint16(0), ctypes.c_uint16(0)
        if self._lib.bvnr_reader_get_declared_version(
                self._ptr, ctypes.byref(maj), ctypes.byref(mn)):
            return (maj.value, mn.value)
        return None




# ===========================================================================
# Canonical form
# ===========================================================================

_canon_on_event = None


def _canon_callback(lib):
    """bvnr_canon_observer_on_event itself as the reader's on_verified: a
    ctypes function pointer to the C function, so the observer is fed with no
    Python call per event."""
    global _canon_on_event
    if _canon_on_event is None:
        addr = ctypes.cast(lib.bvnr_canon_observer_on_event, ctypes.c_void_p)
        _canon_on_event = EVENT_CALLBACK_FUNC(addr.value)
    return _canon_on_event


class _VersionSniffer:
    """Finds a leading ``#!bovnar M.N`` in the bytes a stream hands over and
    gives it to the observer before the reader sees them -- the bytes of the
    directive's line all arrive before any value can, so the directive is
    set before output begins. bvnr_peek_version decides, on the bytes up to
    the end of that line."""

    _WS = b' \t\n\r\x0b\x0c'
    _CAP = 1 << 16

    def __init__(self, lib, obs) -> None:
        self._lib = lib
        self._obs = obs
        self._head = bytearray()
        self.done = False

    def on_pull(self, buf: int, n: int) -> None:
        if self.done:
            return
        if n == 0:
            self._decide()
            return
        self._head += ctypes.string_at(buf, n)
        head = self._head
        if len(head) < 3 and b'\xef\xbb\xbf'.startswith(head):
            return                                  # a BOM, perhaps
        i = 3 if head[:3] == b'\xef\xbb\xbf' else 0
        while i < len(head) and head[i] in self._WS:
            i += 1
        if i == len(head):
            if len(head) > self._CAP:
                self._decide()
            return
        if head[i] != 0x23:                         # not '#': no directive
            self.done = True
            return
        if (b'\n' in head[i:] or b'\r' in head[i:]
                or len(head) > self._CAP):
            self._decide()

    def _decide(self) -> None:
        self.done = True
        _set_version(self._lib, self._obs, bytes(self._head))
        self._head = bytearray()


def _set_version(lib, obs, data: bytes) -> None:
    maj, mn = ctypes.c_uint16(0), ctypes.c_uint16(0)
    if lib.bvnr_peek_version(data, len(data), ctypes.byref(maj),
                             ctypes.byref(mn)):
        lib.bvnr_canon_observer_set_version(obs, maj.value, mn.value)


def _canonicalize(where: str, data, out, pretty: bool, options: dict) -> None:
    lib = get_library()
    flags = read_flags(where, options)
    sink = push_sink(out)
    obs = lib.bvnr_canon_observer_create(ctypes.byref(sink.handle),
                                         bool(pretty))
    if not obs:
        raise MemoryError("bvnr_canon_observer_create() returned NULL")
    reader = None
    try:
        # Inside the try: the observer is destroyed if this fails.
        reader = Reader()
        flags.on_verified = _canon_callback(lib)
        flags.userdata = obs
        # The first error a continue_on_error read recovered from: on_error
        # runs only for an error, so it costs nothing on a clean document.
        first = []

        def _c_on_error(_ud, err, line, column, byte, offset) -> None:
            if not first:
                first.append((int(err), int(line), int(column), int(offset),
                              int(byte)))
        on_error = ON_ERROR_FUNC(_c_on_error)
        flags.on_error = on_error

        source = keep = None
        src = BvnrSource()
        if is_stream(data):
            sniffer = _VersionSniffer(lib, obs)
            source = PullSource(data, on_pull=sniffer.on_pull)
            src = source.handle
        else:
            try:
                if isinstance(data, str):
                    raise TypeError
                # Any bytes-like object, in memory order; bytes(5) would be
                # five zero bytes.
                data = memoryview(data).tobytes()
            except TypeError:
                raise BovnarArgumentError(
                    f"{where}() takes a bytes-like object or a binary "
                    f"stream, not {type(data).__name__}") from None
            _set_version(lib, obs, data)
            keep = (ctypes.c_char * len(data)).from_buffer_copy(data)
            lib.bvnr_source_from_mem(ctypes.byref(src), keep, len(data))
        ok = reader._run_read(lambda: lib.bvnr_open_read_source(
            reader._ptr, ctypes.byref(src), None, ctypes.byref(flags)))
        for exc in (source.exc if source is not None else None, sink.exc):
            if exc is not None:
                raise exc
        if not ok:
            reader._raise_error()
        if lib.bvnr_reader_get_recovery_count(reader._ptr):
            # doc/08 §3.6: the observer writes what it is fed, and a
            # recovered error leaves a statement without its value, so the
            # output would not be a document anyone can read. (Every
            # recovery is reported to on_error first; INVALID_ARGUMENT at
            # 0:0 stands in should one ever not be, as in Rust.)
            code, line, column, offset, byte = (
                first[0] if first else (int(ErrorCode.INVALID_ARGUMENT),
                                        0, 0, 0, 0))
            raise _parse_error(
                lib, code, line, column, offset, byte,
                prefix="the document has no canonical form: the read "
                       "recovered from an error, and the output would not "
                       "be readable: ")
        if not lib.bvnr_canon_observer_finish(obs):
            if sink.exc is not None:
                raise sink.exc
            # The output side failed: a write error, as in Rust.
            raise BovnarWriteError(ErrorCode.WRITING_TO_SINK, 0,
                                   "bvnr_canon_observer_finish failed")
    finally:
        lib.bvnr_canon_observer_destroy(obs)
        if reader is not None:
            reader.close()


def canonicalize(data, *, pretty: bool = False, **options) -> bytes:
    """``data`` -- bytes, or a socket or binary file object read to its end
    -- in canonical form: the document re-serialised from its verified
    events by the canonicalising observer (``bvnr_canon_observer_*``), the
    form ``bovnar events -d`` prints (``-p`` with ``pretty=True``). The
    compact form is one line with no spaces outside literals; two documents
    that differ only in layout, comments and the spelling of separators
    canonicalise to the same bytes, and canonicalising the output again
    gives it back unchanged.

    A leading ``#!bovnar M.N`` directive is kept. The keywords are the read
    options of :meth:`Reader.read_mem` (:data:`READ_OPTIONS`); a document
    the reader refuses raises ``BovnarParseError``. Under
    ``continue_on_error`` a document the reader had to recover from raises
    it too, with the first error recovered from: the observer writes what
    it is fed, and the gap a recovery leaves would make the output
    unreadable (doc/08 §3.6)."""
    out = bytearray()
    _canonicalize('canonicalize', data, out, pretty, options)
    return bytes(out)


def canonicalize_to(data, out, *, pretty: bool = False, **options) -> None:
    """:func:`canonicalize` into ``out``: a writable binary file object, a
    socket or a ``bytearray`` (appended to). The output reaches ``out`` in
    64 KiB pieces as it is made, so neither the document nor its canonical
    form is held whole when ``data`` is a stream; after an error ``out`` may
    hold the start of the output."""
    _canonicalize('canonicalize_to', data, out, pretty, options)
