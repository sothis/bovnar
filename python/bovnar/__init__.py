

from .enums import (
    Event, ValueTypeFamily, PrefixSystem,
    SIPrefix, IECPrefix, BaseUnit, Exponent, ErrorCode,
)
from .structs import (
    ValueTypeSpec, ValueUnit, ValueUnitComponent, BvnrData,
    BvnrReadFlags, BvnrWriteFlags,
    make_type_spec, make_unit_dimensionless, make_unit_none,
    make_unit_si, make_unit_iec,
)
from .exceptions import (
    BovnarError, BovnarLibraryNotFound,
    BovnarParseError, BovnarWriteError, BovnarArgumentError,
)
from .reader import Reader, EventPayload, MAX_FILESIZE_BYTES
from .writer import Writer

__all__ = [

    'loads', 'dumps', 'unit_factor', 'unit_to_str', 'parse_unit',

    'Reader', 'Writer', 'EventPayload',

    'ValueTypeSpec', 'ValueUnit', 'ValueUnitComponent', 'BvnrData',
    'BvnrReadFlags', 'BvnrWriteFlags',

    'make_type_spec', 'make_unit_dimensionless', 'make_unit_none',
    'make_unit_si', 'make_unit_iec',

    'Event', 'ValueTypeFamily', 'PrefixSystem',
    'SIPrefix', 'IECPrefix', 'BaseUnit', 'Exponent', 'ErrorCode',

    'BovnarError', 'BovnarLibraryNotFound',
    'BovnarParseError', 'BovnarWriteError', 'BovnarArgumentError',

    'MAX_FILESIZE_BYTES',
]

__version__ = '0.1.0'

def loads(data: bytes | bytearray | str | memoryview,
          *,
          max_file_size: int = 0,
          continue_on_error: bool = False) -> dict:

    if isinstance(data, str):
        data = data.encode('utf-8')

    parser = _DictParser()
    with Reader() as r:
        r.read_mem(
            data,
            on_verified=parser.on_event,
            max_file_size=max_file_size,
            continue_on_error=continue_on_error,
        )
    return parser.result()

def dumps(obj: dict,
          *,
          pretty: bool = True,
          cap: int = 256 * 1024) -> bytes:

    if not isinstance(obj, dict):
        raise BovnarArgumentError("dumps() requires a dict at the top level")

    with Writer.to_mem(cap=cap, pretty=pretty) as w:
        _emit_dict(w, obj)
    return w.get_output()

def unit_factor(unit_str: str) -> float:

    import ctypes
    from ._ffi import get_library
    lib = get_library()
    ok  = ctypes.c_bool(True)
    raw = unit_str.encode('utf-8')
    arr = (ctypes.c_uint8 * len(raw)).from_buffer_copy(raw)
    vu  = lib.bvn_parse_unit_n(arr, ctypes.c_uint32(len(raw)), ctypes.byref(ok))
    if not ok.value:
        raise BovnarArgumentError(f"Invalid unit string: {unit_str!r}")
    return lib.bvn_unit_prefix_factor(vu)

def unit_to_str(unit: ValueUnit) -> str:

    import ctypes
    from ._ffi import get_library
    lib = get_library()
    buf = ctypes.create_string_buffer(256)
    n   = lib.bvn_unit_to_string(unit, buf, 256)
    if n < 0:
        raise BovnarArgumentError("unit_to_str: output buffer overflow")
    return buf.raw[:n].decode('utf-8')

def parse_unit(unit_str: str) -> ValueUnit:

    import ctypes
    from ._ffi import get_library
    lib = get_library()
    ok  = ctypes.c_bool(True)
    raw = unit_str.encode('utf-8')
    arr = (ctypes.c_uint8 * len(raw)).from_buffer_copy(raw)
    vu  = lib.bvn_parse_unit_n(arr, ctypes.c_uint32(len(raw)), ctypes.byref(ok))
    if not ok.value:
        raise BovnarArgumentError(f"Invalid unit string: {unit_str!r}")
    return vu

class _DictParser:


    def __init__(self) -> None:

        self._stack: list[tuple[dict, str | None]] = [({}, None)]
        self._current_key: str | None = None

        self._array_stack: list[list] = []
        self._in_array  = False

        self._in_octet  = False
        self._octet_buf = bytearray()

    def on_event(self, ev: Event, data) -> bool:
        raw  = data.raw_bytes() if data else b''
        vt   = data.value_type if data else None
        fam  = ValueTypeFamily(vt.family) if vt else ValueTypeFamily.PLAIN

        if ev == Event.STREAM_START:
            pass

        elif ev == Event.ASSIGNMENT_START:
            self._current_key = raw.decode('utf-8')

        elif ev == Event.STRUCT_START:
            child: dict = {}
            self._push_value(child)
            self._stack.append((child, self._current_key))
            self._current_key = None

        elif ev == Event.STRUCT_END:
            if len(self._stack) > 1:
                self._stack.pop()

        elif ev == Event.ARRAY_ROW_START:
            self._in_array = True
            self._array_stack.append([])

        elif ev == Event.ARRAY_ROW_END:
            row = self._array_stack.pop()
            if self._array_stack:
                self._array_stack[-1].append(row)
            else:
                self._push_value(row)
                self._in_array = False

        elif ev == Event.ARRAY_DIM_START:

            self._array_stack.append([])

        elif ev == Event.OCTET_STREAM_START:
            self._in_octet  = True
            self._octet_buf = bytearray()

        elif ev == Event.OCTET_STREAM_END:
            self._in_octet = False
            self._push_value(bytes(self._octet_buf))
            self._octet_buf = bytearray()

        elif ev == Event.DATA:
            if self._in_octet:
                self._octet_buf.extend(raw)
            else:
                value = self._decode_value(raw, fam, vt)
                if self._in_array and self._array_stack:
                    self._array_stack[-1].append(value)
                else:
                    self._push_value(value)

        return True

    def _push_value(self, value) -> None:
        d, _ = self._stack[-1]
        if self._current_key is not None:
            d[self._current_key] = value
            self._current_key = None

    @staticmethod
    def _decode_value(raw: bytes, fam: ValueTypeFamily, vt) -> object:

        if not raw:
            return None
        text = raw.decode('utf-8', errors='replace')

        if fam in (ValueTypeFamily.UINT, ValueTypeFamily.SINT):
            try:
                return int(text, vt.base if vt and vt.base > 1 else 10)
            except ValueError:
                return text

        if fam in (ValueTypeFamily.FLOAT,
                   ValueTypeFamily.FLOAT_FIX,
                   ValueTypeFamily.FLOAT_DEC):
            if text in ('nan', 'infinity', '-infinity'):
                return {'nan': float('nan'),
                        'infinity': float('inf'),
                        '-infinity': float('-inf')}[text]
            try:
                return float(text)
            except ValueError:
                return text

        return text

    def result(self) -> dict:
        return self._stack[0][0]

def _emit_dict(w: Writer, d: dict) -> None:
    for key, value in d.items():
        _emit_value(w, key, value)

def _emit_value(w: Writer, key: str, value) -> None:
    if value is None:
        w.write_null(key)
    elif isinstance(value, bool):
        w.write_bool(key, value)
    elif isinstance(value, int):
        if value >= 0:
            w.write_uint(key, value)
        else:
            w.write_sint(key, value)
    elif isinstance(value, float):
        w.write_float(key, value)
    elif isinstance(value, str):
        w.write_string(key, value)
    elif isinstance(value, dict):
        w.begin_struct(key)
        _emit_dict(w, value)
        w.end_struct()
    elif isinstance(value, (list, tuple)):

        w.emit(Event.ASSIGNMENT_START, key=key)
        w.begin_array_row()
        for elem in value:
            _emit_array_element(w, elem)
        w.end_array_row()
    else:
        raise BovnarArgumentError(
            f"Cannot serialise value of type {type(value).__name__!r} "
            f"for key {key!r}")

def _emit_array_element(w: Writer, elem) -> None:
    if elem is None:
        vt = make_type_spec(ValueTypeFamily.PLAIN)
        w.emit(Event.DATA, vt=vt, vu=make_unit_none())
    elif isinstance(elem, bool):
        vt = make_type_spec(ValueTypeFamily.PLAIN)
        w.emit(Event.DATA, value='true' if elem else 'false',
               vt=vt, vu=make_unit_none())
    elif isinstance(elem, int):
        vt = make_type_spec(ValueTypeFamily.UINT if elem >= 0
                            else ValueTypeFamily.SINT, 64, 10)
        w.emit(Event.DATA, value=str(elem), vt=vt, vu=make_unit_dimensionless())
    elif isinstance(elem, float):
        vt = make_type_spec(ValueTypeFamily.FLOAT, 64, 0)
        w.emit(Event.DATA, value=repr(elem), vt=vt, vu=make_unit_dimensionless())
    elif isinstance(elem, str):
        vt = make_type_spec(ValueTypeFamily.UTF8)
        w.emit(Event.DATA, value=elem, vt=vt, vu=make_unit_none())
    else:
        raise BovnarArgumentError(
            f"Cannot serialise array element of type {type(elem).__name__!r}")
