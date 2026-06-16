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
from typing import Callable, Generator, IO

from ._ffi import get_library
from .enums import Event, ErrorCode
from .exceptions import BovnarParseError, BovnarArgumentError
from .structs import (
    BvnrSource, BvnrSink, BvnrReadFlags, BvnrData, ValueUnit,
    EVENT_CALLBACK_FUNC,
    make_unit_dimensionless,
)

MAX_FILESIZE_BYTES = 16 * 1024 * 1024

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


    __slots__ = ('event', 'raw', 'value_type', 'value_unit')

    def __init__(self,
                 event: Event,
                 raw: bytes,
                 value_type,
                 value_unit) -> None:
        self.event      = event
        self.raw        = raw
        self.value_type = value_type
        self.value_unit = value_unit

    @property
    def text(self) -> str:
        return self.raw.decode('utf-8', errors='replace') if self.raw else ''

    def __repr__(self) -> str:
        return f"EventPayload(event={self.event.name}, text={self.text!r})"

class Reader:


    def __init__(self) -> None:
        self._lib  = get_library()
        self._ptr  = None
        self._open()

    def _open(self) -> None:
        ptr = self._lib.bvnr_reader_create()
        if not ptr:
            raise MemoryError("bvnr_reader_create() returned NULL")
        self._ptr = ptr

    def close(self) -> None:

        if self._ptr is not None:
            self._lib.bvnr_reader_destroy(self._ptr)
            self._ptr = None

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()

    def __del__(self):
        self.close()

    def _check_open(self) -> None:
        if self._ptr is None:
            raise BovnarArgumentError("Reader has been closed")

    def _build_flags(self,
                     on_verified,
                     on_unverified,
                     max_file_size: int,
                     continue_on_error: bool,
                     strict_version: bool = False) -> tuple:

        flags    = BvnrReadFlags()
        cb_refs  = []

        flags.max_file_size = max_file_size
        # Match the nesting limit used by the CLI, the DOM builder and stream.py
        # (the uint8_t hard cap, 255) rather than the reader's bare 64 default,
        # so loads()/read_mem accept the same documents those paths do. Leaving
        # these at 0 would default to 64 and surprise users with a rejection at
        # depth 65 for a document the CLI and dom_parse() accept.
        flags.max_struct_nesting = 255
        flags.max_array_nesting  = 255

        if on_verified is not None:
            cb = self._wrap_callback(on_verified)
            cb_refs.append(cb)
            flags.on_verified = cb

        if on_unverified is not None:
            cb = self._wrap_callback(on_unverified)
            cb_refs.append(cb)
            flags.on_unverified = cb

        flags.continue_on_error = continue_on_error
        flags.strict_version    = strict_version
        return flags, cb_refs

    @staticmethod
    def _wrap_callback(py_fn: Callable):

        state = {'exc': None}
        unit_size = ctypes.sizeof(ValueUnit)
        # Each event gets a fresh BvnrData (callers may retain sub-struct
        # references like d.value_type), but we skip the eager bytes copy
        # for d.data — d.data references the lexer's internal buffer and
        # is valid for the duration of the callback only.  Call
        # raw_bytes() / raw_str() within the callback to capture data.

        def _c_cb(userdata_void, event_int: int, data_ptr) -> bool:
            try:
                ev = Event(event_int)
                data = None
                if data_ptr:
                    src = data_ptr.contents
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
                    # Like d.data, frac_data references the lexer's internal
                    # buffer (valid only during the callback); read it now via
                    # snap.frac_str() if you need to retain it.
                    snap.frac_data   = src.frac_data
                    snap.frac_length = src.frac_length
                    data = snap
                result = py_fn(ev, data)
                return bool(result) if result is not None else True
            except BaseException as exc:
                if state['exc'] is None:
                    state['exc'] = exc
                return False

        cb = EVENT_CALLBACK_FUNC(_c_cb)
        cb._bvnr_state = state
        return cb

    def _raise_error(self) -> None:

        code   = ErrorCode(self._lib.bvnr_reader_get_error(self._ptr))
        line   = self._lib.bvnr_reader_get_error_line(self._ptr)
        column = self._lib.bvnr_reader_get_error_column(self._ptr)
        offset = self._lib.bvnr_reader_get_error_offset(self._ptr)
        byte   = self._lib.bvnr_reader_get_error_byte(self._ptr)
        msg_b  = self._lib.bvn_error_to_string(int(code))
        msg    = msg_b.decode('utf-8') if msg_b else ''
        raise BovnarParseError(code, line, column, offset, byte, msg)

    def read_mem(self,
                 data: bytes | bytearray | memoryview,
                 *,
                 on_verified: Callable | None = None,
                 on_unverified: Callable | None = None,
                 max_file_size: int = 0,
                 continue_on_error: bool = False,
                 strict_version: bool = False) -> None:

        self._check_open()
        if isinstance(data, memoryview):
            data = bytes(data)
        if not isinstance(data, (bytes, bytearray)):
            raise BovnarArgumentError("data must be bytes, bytearray, or memoryview")

        flags, cb_refs = self._build_flags(
            on_verified, on_unverified, max_file_size, continue_on_error,
            strict_version
        )

        buf = (ctypes.c_char * len(data)).from_buffer_copy(data)
        ok  = self._lib.bvnr_open_read_mem(
            self._ptr,
            buf, len(data),
            None, 0,
            ctypes.byref(flags),
        )
        if not ok:
            self._raise_error()

        ok = self._lib.bvnr_read(self._ptr)

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
                max_file_size: int = 0,
                continue_on_error: bool = False,
                strict_version: bool = False) -> None:

        self._check_open()
        if not isinstance(fd, int) or fd < 0:
            raise BovnarArgumentError(f"Invalid file descriptor: {fd!r}")

        flags, cb_refs = self._build_flags(
            on_verified, on_unverified, max_file_size, continue_on_error,
            strict_version
        )

        src = BvnrSource()
        self._lib.bvnr_source_from_fd(ctypes.byref(src), fd)

        ok = self._lib.bvnr_open_read_source(
            self._ptr,
            ctypes.byref(src),
            None,
            ctypes.byref(flags),
        )
        if not ok:
            self._raise_error()

        ok = self._lib.bvnr_read(self._ptr)

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

    def read_file(self,
                  path: str,
                  *,
                  on_verified: Callable | None = None,
                  on_unverified: Callable | None = None,
                  max_file_size: int = MAX_FILESIZE_BYTES,
                  continue_on_error: bool = False) -> None:

        import os
        fd = os.open(path, os.O_RDONLY)
        try:
            self.read_fd(
                fd,
                on_verified=on_verified,
                on_unverified=on_unverified,
                max_file_size=max_file_size,
                continue_on_error=continue_on_error,
            )
        finally:
            os.close(fd)

    def iter_mem(self,
                 data: bytes | bytearray | memoryview,
                 *,
                 verified_only: bool = True,
                 max_file_size: int = 0) -> Generator[EventPayload, None, None]:

        events: list[EventPayload] = []

        def collector(ev: Event, d):
            raw = d.raw_bytes() if d else b''
            vt  = d.value_type  if d else None
            vu  = d.value_unit  if d else None
            events.append(EventPayload(ev, raw, vt, vu))
            return True

        kwargs = dict(max_file_size=max_file_size)
        if verified_only:
            self.read_mem(data, on_verified=collector, **kwargs)
        else:
            self.read_mem(data,
                          on_verified=collector,
                          on_unverified=collector,
                          **kwargs)

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
    def recovery_count(self) -> int:
        self._check_open()
        return self._lib.bvnr_reader_get_recovery_count(self._ptr)

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


