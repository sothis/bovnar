

import ctypes
from typing import Callable, Generator, IO

from ._ffi import get_library
from .enums import Event, ErrorCode
from .exceptions import BovnarParseError, BovnarArgumentError
from .structs import (
    BvnrSource, BvnrSink, BvnrReadFlags, BvnrData,
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
                     continue_on_error: bool) -> tuple:

        flags    = BvnrReadFlags()
        cb_refs  = []

        flags.max_file_size = max_file_size

        _need_noop = (on_verified is None and on_unverified is None)

        if on_verified is not None or _need_noop:
            fn = on_verified if on_verified is not None else (lambda ev, d: True)
            cb = self._wrap_callback(fn)
            cb_refs.append(cb)
            flags.on_verified = cb

        if on_unverified is not None:
            cb = self._wrap_callback(on_unverified)
            cb_refs.append(cb)
            flags.on_unverified = cb

        flags.continue_on_error = continue_on_error
        return flags, cb_refs

    @staticmethod
    def _wrap_callback(py_fn: Callable):

        state = {'exc': None}

        def _c_cb(userdata_void, event_int: int, data_ptr) -> bool:
            try:
                ev = Event(event_int)
                data = None
                if data_ptr:
                    src = data_ptr.contents
                    snap = BvnrData()
                    snap.type = src.type
                    snap.value_type.family = src.value_type.family
                    snap.value_type.width  = src.value_type.width
                    snap.value_type.base   = src.value_type.base
                    ctypes.memmove(
                        ctypes.byref(snap.value_unit),
                        ctypes.byref(src.value_unit),
                        ctypes.sizeof(type(snap.value_unit)),
                    )
                    snap.length = src.length
                    if src.data and src.length:
                        raw = (ctypes.c_char * src.length).from_address(
                            src.data).raw
                        buf = ctypes.create_string_buffer(raw, src.length + 1)
                        snap.data = ctypes.cast(buf, ctypes.c_void_p)
                        snap._owned_data = buf
                    else:
                        snap.data = None
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
                 continue_on_error: bool = False) -> None:

        self._check_open()
        if isinstance(data, memoryview):
            data = bytes(data)
        if not isinstance(data, (bytes, bytearray)):
            raise BovnarArgumentError("data must be bytes, bytearray, or memoryview")

        flags, cb_refs = self._build_flags(
            on_verified, on_unverified, max_file_size, continue_on_error
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
                continue_on_error: bool = False) -> None:

        self._check_open()
        if not isinstance(fd, int) or fd < 0:
            raise BovnarArgumentError(f"Invalid file descriptor: {fd!r}")

        flags, cb_refs = self._build_flags(
            on_verified, on_unverified, max_file_size, continue_on_error
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


