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

from .enums import (
    Event, ValueTypeFamily, PrefixSystem,
    SIPrefix, IECPrefix, BaseUnit, Exponent, ErrorCode,
)
from .structs import (
    ValueTypeSpec, ValueUnit, ValueUnitComponent, ValueUnitPrefix, BvnrData,
    BvnrReadFlags, BvnrWriteFlags,
    make_type_spec, make_unit_dimensionless, make_unit_none,
    make_unit_si, make_unit_iec, make_unit_compound,
)
from .exceptions import (
    BovnarError, BovnarLibraryNotFound,
    BovnarParseError, BovnarWriteError, BovnarArgumentError,
)
from .reader   import Reader, EventPayload, MAX_FILESIZE_BYTES
from .writer   import Writer
from .dom      import DomDoc, DomNode, DomType
from .quantity import Quantity
from .units   import (
    UnitFlags,
    SIConversion, UnitConversion, ReducedUnit, SI_DIM_NAMES,
    unit_valid,
    unit_prefix_factor, unit_prefix_exponent,
    prefix_unit_valid,
    unit_to_si_factor, units_compatible, unit_convert_factor,
    unit_dimension_vector, unit_reduce,
    unit_to_str_ex,
    exponent_to_int, int_to_exponent,
    convert_value,
)
from . import currency
# pint bridge: functions only; pint itself is imported lazily on first use, so
# importing bovnar never requires pint to be installed.
from ._pint_bridge import to_pint, to_pint_unit, from_pint, from_pint_unit
# numpy bridge: likewise numpy is imported lazily, only when these are called.
from ._numpy import (to_numpy, to_pint_array, from_numpy, from_pint_array,
                     array_to_bvnr)

__all__ = [
    'loads', 'dumps', 'dom_parse',
    'currency',
    'unit_factor', 'unit_to_str', 'parse_unit',
    'write_array',
    'Quantity',

    'Reader', 'Writer', 'EventPayload',
    'DomDoc', 'DomNode', 'DomType',

    'ValueTypeSpec', 'ValueUnit', 'ValueUnitComponent', 'ValueUnitPrefix', 'BvnrData',
    'BvnrReadFlags', 'BvnrWriteFlags',

    'make_type_spec', 'make_unit_dimensionless', 'make_unit_none',
    'make_unit_si', 'make_unit_iec', 'make_unit_compound',

    'Event', 'ValueTypeFamily', 'PrefixSystem',
    'SIPrefix', 'IECPrefix', 'BaseUnit', 'Exponent', 'ErrorCode',

    'UnitFlags',
    'SIConversion', 'UnitConversion', 'ReducedUnit', 'SI_DIM_NAMES',
    'unit_valid',
    'unit_prefix_factor', 'unit_prefix_exponent',
    'prefix_unit_valid',
    'unit_to_si_factor', 'units_compatible', 'unit_convert_factor',
    'unit_dimension_vector', 'unit_reduce',
    'unit_to_str_ex',
    'exponent_to_int', 'int_to_exponent',
    'convert_value',

    'to_pint', 'to_pint_unit', 'from_pint', 'from_pint_unit',
    'to_numpy', 'to_pint_array', 'from_numpy', 'from_pint_array', 'array_to_bvnr',

    'BovnarError', 'BovnarLibraryNotFound',
    'BovnarParseError', 'BovnarWriteError', 'BovnarArgumentError',

    'MAX_FILESIZE_BYTES',
]

__version__ = '1.0.0'


def loads(data: bytes | bytearray | str | memoryview,
          *,
          typed: bool = False,
          max_file_size: int = 0,
          continue_on_error: bool = False) -> dict:
    if isinstance(data, str):
        data = data.encode('utf-8')
    parser = _TypedDictParser() if typed else _DictParser()
    with Reader() as r:
        r.read_mem(
            data,
            on_verified=parser.on_event,
            max_file_size=max_file_size,
            continue_on_error=continue_on_error,
        )
    return parser.result()


def dumps(obj: dict, *, pretty: bool = True) -> bytes:
    if not isinstance(obj, dict):
        raise BovnarArgumentError("dumps() requires a dict at the top level")
    cap = 4 * 1024 * 1024
    while True:
        try:
            with Writer.to_mem(cap=cap, pretty=pretty) as w:
                _emit_dict(w, obj)
            return w.get_output()
        except BovnarWriteError as e:
            if e.code == ErrorCode.SINK_BUFFER_EXHAUSTED and cap < 256 * 1024 * 1024:
                cap *= 2
            else:
                raise


def dom_parse(data: bytes | bytearray | str | memoryview) -> DomDoc:
    """Parse BVNR bytes into a DOM tree (random-access, type-preserving)."""
    if isinstance(data, str):
        data = data.encode('utf-8')
    return DomDoc.parse(bytes(data) if isinstance(data, memoryview) else data)


def unit_factor(unit_str: str) -> float:
    import ctypes as _ct
    from ._ffi import get_library
    lib = get_library()
    ok  = _ct.c_bool(True)
    raw = unit_str.encode('utf-8')
    arr = (_ct.c_uint8 * len(raw)).from_buffer_copy(raw)
    vu  = lib.bvn_parse_unit_n(arr, _ct.c_uint32(len(raw)), _ct.byref(ok))
    if not ok.value:
        raise BovnarArgumentError(f"Invalid unit string: {unit_str!r}")
    return lib.bvn_unit_prefix_factor(vu)


def unit_to_str(unit: ValueUnit) -> str:
    import ctypes as _ct
    from ._ffi import get_library
    lib = get_library()
    buf = _ct.create_string_buffer(256)
    n   = lib.bvn_unit_to_string(unit, buf, 256)
    if n < 0:
        raise BovnarArgumentError("unit_to_str: output buffer overflow")
    return buf.raw[:n].decode('utf-8')


def parse_unit(unit_str: str) -> ValueUnit:
    import ctypes as _ct
    from ._ffi import get_library
    lib = get_library()
    ok  = _ct.c_bool(True)
    raw = unit_str.encode('utf-8')
    arr = (_ct.c_uint8 * len(raw)).from_buffer_copy(raw)
    vu  = lib.bvn_parse_unit_n(arr, _ct.c_uint32(len(raw)), _ct.byref(ok))
    if not ok.value:
        raise BovnarArgumentError(f"Invalid unit string: {unit_str!r}")
    return vu


def write_array(w: Writer,
                key: str,
                rows,
                *,
                vt: ValueTypeSpec | None = None,
                vu: ValueUnit | None = None) -> None:
    """
    High-level typed array writer.

    *rows* may be:
      - a flat list  [1, 2, 3]            → single-row array
      - a list-of-lists [[1,2],[3,4]]     → multi-row array (rows separated by /)

    Elements may be: int, float, str, bool, None, dict (struct), or nested
    list/tuple (nested array).

    *vt* and *vu*, when provided, emit a type annotation before the opening [.
    """
    w.emit(Event.ASSIGNMENT_START, key=key)

    if vt is not None:
        fam_names = {
            int(ValueTypeFamily.UINT):      'uint',
            int(ValueTypeFamily.SINT):      'sint',
            int(ValueTypeFamily.FLOAT):     'float',
            int(ValueTypeFamily.FLOAT_FIX): 'float_fix',
            int(ValueTypeFamily.FLOAT_DEC): 'float_dec',
            int(ValueTypeFamily.UTF8):      'utf8',
        }
        name = fam_names.get(int(vt.family), 'uint')
        w._emit_annotation(name, vt, vu if vu is not None else make_unit_none())

    if rows and all(isinstance(r, (list, tuple)) for r in rows):
        row_list = rows
    else:
        row_list = [rows]

    first = True
    for row in row_list:
        if not first:
            w.new_array_dim()
        first = False
        w.begin_array_row()
        for elem in row:
            _emit_array_element(w, elem)
        w.end_array_row()


class _ArrScope:
    __slots__ = ('rows', 'cur_row', 'dim_continue', 'pending_seal', 'parent_key')

    def __init__(self, parent_key: str | None = None) -> None:
        self.rows:         list      = []
        self.cur_row:      list      = []
        self.dim_continue: bool      = False
        self.pending_seal: bool      = False
        self.parent_key:   str | None = parent_key


class _DictParser:
    """
    SAX-style event consumer that builds a plain Python dict.

    Array state machine design
    ──────────────────────────
    The scope stack entries are ('doc', dict), ('struct', dict), or
    ('array', _ArrScope).  Array scopes are sealed lazily:

    • ARRAY_ROW_START  – push a new _ArrScope *unless* dim_continue is set
                         on the top _ArrScope, in which case we just reset
                         cur_row and continue the existing scope.
                         If the top scope is a *pending* _ArrScope (a
                         completed inner array), seal it first and push its
                         value to the new top scope.
    • DATA / STRUCT    – append to the current scope's cur_row or dict.
    • ARRAY_ROW_END    – first flush any pending inner _ArrScopes (sealing
                         completed nested arrays), then finalize cur_row into
                         the current scope's rows list and mark pending_seal.
    • ARRAY_DIM_START  – clear pending_seal and set dim_continue on the top
                         _ArrScope so the next ARRAY_ROW_START continues it.
    • ASSIGNMENT_START / STRUCT_END / result() – call _maybe_seal_array()
                         which walks the top of the stack sealing any
                         pending _ArrScopes and pushing their values.
    """

    def __init__(self) -> None:
        self._doc:         dict = {}
        self._scope_stack: list = [('doc', self._doc)]
        self._current_key: str | None = None
        self._in_octet:    bool = False
        self._octet_buf:   bytearray = bytearray()

    def on_event(self, ev: Event, data) -> bool:
        raw = data.raw_bytes() if data else b''
        vt  = data.value_type if data else None
        fam = ValueTypeFamily(vt.family) if vt else ValueTypeFamily.PLAIN

        if ev == Event.STREAM_START:
            pass

        elif ev == Event.ASSIGNMENT_START:
            self._maybe_seal_array()
            self._current_key = raw.decode('utf-8')

        elif ev == Event.STRUCT_START:
            child: dict = {}
            self._push_value(child)
            self._scope_stack.append(('struct', child))
            self._current_key = None

        elif ev == Event.STRUCT_END:
            self._maybe_seal_array()
            if len(self._scope_stack) > 1 and self._scope_stack[-1][0] == 'struct':
                self._scope_stack.pop()

        elif ev == Event.ARRAY_DIM_START:
            top = self._scope_stack[-1] if self._scope_stack else None
            if top and top[0] == 'array':
                top[1].pending_seal = False
                top[1].dim_continue = True

        elif ev == Event.ARRAY_ROW_START:
            top = self._scope_stack[-1] if self._scope_stack else None
            if top and top[0] == 'array' and top[1].dim_continue:
                top[1].cur_row      = []
                top[1].dim_continue = False
            else:
                if top and top[0] == 'array' and top[1].pending_seal:
                    arr = self._scope_stack.pop()[1]
                    self._commit_sealed(arr, _seal_array(arr))
                new_scope = _ArrScope(parent_key=self._current_key)
                self._current_key = None
                self._scope_stack.append(('array', new_scope))

        elif ev == Event.ARRAY_ROW_END:
            self._flush_pending_inner_arrays()
            top = self._scope_stack[-1] if self._scope_stack else None
            if top and top[0] == 'array':
                arr = top[1]
                arr.rows.append(list(arr.cur_row))
                arr.cur_row      = []
                arr.pending_seal = True

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
                tok_type = getattr(data, 'type', 0) if data is not None else 0
                value = self._decode_data(raw, fam, vt, tok_type, data)
                self._push_value(value)

        return True

    def _push_value(self, value) -> None:
        top = self._scope_stack[-1] if self._scope_stack else None
        if top is None:
            return
        if top[0] in ('doc', 'struct'):
            if self._current_key is not None:
                top[1][self._current_key] = value
                self._current_key = None
        elif top[0] == 'array':
            top[1].cur_row.append(value)

    def _commit_sealed(self, arr: '_ArrScope', value) -> None:
        """
        Insert a sealed array value into the current top scope, using the
        key saved at array-push time rather than self._current_key (which
        may have been overwritten by intervening struct-start events).
        """
        top = self._scope_stack[-1] if self._scope_stack else None
        if top is None:
            return
        if top[0] in ('doc', 'struct'):
            if arr.parent_key is not None:
                top[1][arr.parent_key] = value
        elif top[0] == 'array':
            top[1].cur_row.append(value)

    def _maybe_seal_array(self) -> None:
        while (self._scope_stack and
               self._scope_stack[-1][0] == 'array' and
               self._scope_stack[-1][1].pending_seal):
            arr = self._scope_stack.pop()[1]
            self._commit_sealed(arr, _seal_array(arr))

    def _flush_pending_inner_arrays(self) -> None:
        """
        Before finalising a row, collapse any completed inner _ArrScope
        sitting on top of the stack into a value in the parent scope.
        """
        while (len(self._scope_stack) >= 2 and
               self._scope_stack[-1][0] == 'array' and
               self._scope_stack[-1][1].pending_seal):
            arr = self._scope_stack.pop()[1]
            self._commit_sealed(arr, _seal_array(arr))

    def _decode_data(self, raw: bytes, fam: ValueTypeFamily, vt,
                     tok_type: int, data) -> object:
        return _decode_value(raw, fam, vt, tok_type)

    def result(self) -> dict:
        self._maybe_seal_array()
        return self._doc


class _TypedDictParser(_DictParser):
    """Like _DictParser but wraps typed values in Quantity for lossless round-trips."""

    def _decode_data(self, raw: bytes, fam: ValueTypeFamily, vt,
                     tok_type: int, data) -> object:
        if fam == ValueTypeFamily.PLAIN:
            return _decode_value(raw, fam, vt, tok_type)
        text = raw.decode('utf-8', errors='replace') if raw else None
        vu   = data.value_unit if data is not None else make_unit_none()
        return Quantity(text, vt, vu, tok_type)


def _seal_array(arr: _ArrScope):
    """
    Collapse an _ArrScope into a Python value.

    A single-row array is unwrapped to a flat list.
    A multi-row array becomes a list of lists.
    """
    if len(arr.rows) == 1:
        return arr.rows[0]
    return arr.rows


_TOKEN_IS_SYMBOL = 3
_TOKEN_IS_BOOL   = 15


def _decode_value(raw: bytes, fam: ValueTypeFamily, vt, tok_type: int = 0) -> object:
    if not raw:
        if tok_type in (1, 6):
            return ''
        return None
    text = raw.decode('utf-8', errors='replace')

    if tok_type == _TOKEN_IS_BOOL:
        # The validator normalises on/off to canonical true/false text
        # before emitting token_is_bool, so only "true"/"false" arrive here.
        return text == 'true'

    if tok_type == _TOKEN_IS_SYMBOL:
        return text

    if fam in (ValueTypeFamily.UINT, ValueTypeFamily.SINT):
        try:
            return int(text, vt.base if vt and vt.base > 1 else 10)
        except ValueError:
            return text

    if fam in (ValueTypeFamily.FLOAT,
               ValueTypeFamily.FLOAT_FIX,
               ValueTypeFamily.FLOAT_DEC):
        if text in ('nan', 'inf', 'ninf'):
            return {'nan': float('nan'),
                    'inf': float('inf'),
                    'ninf': float('-inf')}[text]
        try:
            return float(text)
        except ValueError:
            return text

    return text


def _emit_dict(w: Writer, d: dict) -> None:
    for key, value in d.items():
        _emit_value(w, key, value)


_FAM_NAMES = {
    int(ValueTypeFamily.UTF8):      'utf8',
    int(ValueTypeFamily.SINT):      'sint',
    int(ValueTypeFamily.UINT):      'uint',
    int(ValueTypeFamily.FLOAT):     'float',
    int(ValueTypeFamily.FLOAT_FIX): 'float_fix',
    int(ValueTypeFamily.FLOAT_DEC): 'float_dec',
}


def _has_real_unit(vu: ValueUnit) -> bool:
    return any(vu.components[i].base != 0 for i in range(vu.num_components))


def _needs_annotation(vt: ValueTypeSpec, vu: ValueUnit) -> bool:
    """Return True only when the type annotation carries non-default information."""
    fam = int(vt.family)
    if fam == int(ValueTypeFamily.PLAIN):
        return False
    if fam in (int(ValueTypeFamily.FLOAT_FIX), int(ValueTypeFamily.FLOAT_DEC)):
        return True
    if _has_real_unit(vu):
        return True
    if vt.base not in (0, 10):
        return True
    if vt.width == 0:
        return False
    return vt.width != 64


def _emit_quantity(w: Writer, key: str, q: 'Quantity') -> None:
    import ctypes as _ct
    fam = int(q.vtype.family)
    w.emit(Event.ASSIGNMENT_START, key=key)
    fam_name = _FAM_NAMES.get(fam)
    if fam_name is not None and _needs_annotation(q.vtype, q.unit):
        w._emit_annotation(fam_name, q.vtype, q.unit)
    raw_bytes = q.raw.encode('utf-8') if q.raw else b''
    d = BvnrData()
    d.type       = q._tok_type
    d.value_type = q.vtype
    d.value_unit = q.unit
    d.data       = _ct.cast(_ct.c_char_p(raw_bytes), _ct.c_void_p)
    d.length     = len(raw_bytes)
    _write_event_data(w, d)


def _emit_value(w: Writer, key: str, value) -> None:
    if value is None:
        w.write_null(key)
    elif isinstance(value, bool):
        w.write_bool(key, value)
    elif isinstance(value, Quantity):
        _emit_quantity(w, key, value)
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
        write_array(w, key, value)
    else:
        raise BovnarArgumentError(
            f"Cannot serialise value of type {type(value).__name__!r} "
            f"for key {key!r}")


def _emit_array_element(w: Writer, elem) -> None:
    import ctypes as _ct

    _TOKEN_IS_ARRAY_NUMBER = 5
    _TOKEN_IS_ARRAY_STRING = 6
    _TOKEN_IS_BOOL         = 15
    _TOKEN_IS_NULL_VALUE   = 9

    if elem is None:
        d = BvnrData()
        d.type = _TOKEN_IS_NULL_VALUE
        _write_event_data(w, d)

    elif isinstance(elem, bool):
        sym = b'true' if elem else b'false'
        d = BvnrData()
        d.type = _TOKEN_IS_BOOL
        d.data   = _ct.cast(_ct.c_char_p(sym), _ct.c_void_p)
        d.length = len(sym)
        _write_event_data(w, d)

    elif isinstance(elem, int):
        vt  = make_type_spec(ValueTypeFamily.UINT if elem >= 0
                             else ValueTypeFamily.SINT, 64, 10)
        raw = str(elem).encode('ascii')
        d = BvnrData()
        d.type       = _TOKEN_IS_ARRAY_NUMBER
        d.value_type = vt
        d.value_unit = make_unit_dimensionless()
        d.data       = _ct.cast(_ct.c_char_p(raw), _ct.c_void_p)
        d.length     = len(raw)
        _write_event_data(w, d)

    elif isinstance(elem, float):
        import math as _math
        vt  = make_type_spec(ValueTypeFamily.FLOAT, 64, 0)
        if _math.isinf(elem):
            _s = 'ninf' if elem < 0 else 'inf'
        elif _math.isnan(elem):
            _s = 'nan'
        else:
            _s = repr(elem)
        raw = _s.encode('ascii')
        d = BvnrData()
        d.type       = _TOKEN_IS_ARRAY_NUMBER
        d.value_type = vt
        d.value_unit = make_unit_dimensionless()
        d.data       = _ct.cast(_ct.c_char_p(raw), _ct.c_void_p)
        d.length     = len(raw)
        _write_event_data(w, d)

    elif isinstance(elem, str):
        raw = elem.encode('utf-8')
        d = BvnrData()
        d.type   = _TOKEN_IS_ARRAY_STRING
        d.data   = _ct.cast(_ct.c_char_p(raw), _ct.c_void_p)
        d.length = len(raw)
        _write_event_data(w, d)

    elif isinstance(elem, dict):
        w.emit(Event.STRUCT_START)
        for k, v in elem.items():
            _emit_value(w, k, v)
        w.emit(Event.STRUCT_END)

    elif isinstance(elem, (list, tuple)):
        if elem and all(isinstance(r, (list, tuple)) for r in elem):
            inner_rows = elem
        else:
            inner_rows = [elem]
        first = True
        for row in inner_rows:
            if not first:
                w.new_array_dim()
            first = False
            w.begin_array_row()
            for sub in row:
                _emit_array_element(w, sub)
            w.end_array_row()

    else:
        raise BovnarArgumentError(
            f"Cannot serialise array element of type {type(elem).__name__!r}")


def _write_event_data(w: Writer, d: BvnrData) -> None:
    from ._ffi import get_library
    import ctypes as _ct
    lib = get_library()
    ok  = lib.bvnr_write_event(w._ptr, int(Event.DATA), _ct.byref(d))
    if not ok:
        w._raise_error()
