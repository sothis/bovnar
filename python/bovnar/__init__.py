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

from decimal import Decimal
from fractions import Fraction

from .enums import (
    Event, TokenType, ValueTypeFamily, PrefixSystem,
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
from .reader   import Reader, EventPayload, UnitPolicy, UnitRule, MAX_FILESIZE_BYTES
from .writer   import Writer
from .dom      import DomDoc, DomNode, DomType
from .quantity import Quantity
from .units   import (
    UnitFlags,
    SIConversion, UnitConversion, ReducedUnit, SI_DIM_NAMES,
    unit_valid,
    unit_prefix_factor, unit_prefix_exponent,
    prefix_unit_valid,
    unit_to_si_factor, units_compatible, units_convertible,
    unit_si_normal_form, unit_convert_factor,
    unit_dimension_vector, unit_reduce,
    unit_to_str_ex,
    exponent_to_int, int_to_exponent,
    convert_value,
)
from . import currency
from . import stream
# pint bridge: functions only; pint itself is imported lazily on first use, so
# importing bovnar never requires pint to be installed.
from ._pint_bridge import to_pint, to_pint_unit, from_pint, from_pint_unit
# numpy bridge: likewise numpy is imported lazily, only when these are called.
from ._numpy import (to_numpy, to_pint_array, from_numpy, from_pint_array,
                     array_to_bvnr)
from ._bvnfloat import BvnFloat

__all__ = [
    'BvnFloat',
    'loads', 'dumps', 'dom_parse',
    'version', 'spec_version', 'peek_version',
    'currency', 'stream',
    'unit_factor', 'unit_to_str', 'parse_unit',
    'unit_to_ucum', 'unit_is_profile_only', 'unit_error_code',
    'write_array',
    'Quantity',

    'Reader', 'Writer', 'EventPayload', 'UnitPolicy', 'UnitRule',
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
    'unit_to_si_factor', 'units_compatible', 'units_convertible',
    'unit_si_normal_form', 'unit_convert_factor',
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

__version__ = '1.1.0'


def version() -> str:
    """Runtime library version string of the loaded shared library."""
    from ._ffi import get_library
    return get_library().bvnr_version_string().decode('ascii')


def spec_version() -> tuple[int, int]:
    """Highest bovnar spec version (major, minor) this build understands."""
    import ctypes
    from ._ffi import get_library
    maj, mn = ctypes.c_uint16(0), ctypes.c_uint16(0)
    get_library().bvnr_spec_version(ctypes.byref(maj), ctypes.byref(mn))
    return (maj.value, mn.value)


def peek_version(data: bytes | bytearray | str | memoryview):
    """Return the (major, minor) spec version a document declares via a leading
    ``#!bovnar M.N`` directive, or ``None`` if it carries no such directive."""
    import ctypes
    from ._ffi import get_library
    if isinstance(data, str):
        data = data.encode('utf-8')
    buf = bytes(data)
    maj, mn = ctypes.c_uint16(0), ctypes.c_uint16(0)
    ok = get_library().bvnr_peek_version(buf, len(buf),
                                         ctypes.byref(maj), ctypes.byref(mn))
    return (maj.value, mn.value) if ok else None


def loads(data: bytes | bytearray | str | memoryview,
          *,
          typed: bool = False,
          max_file_size: int = 0,
          continue_on_error: bool = False) -> dict:
    """Parse a BVNR document into a dict.

    With ``typed=True`` each scalar is returned as a :class:`Quantity` (its
    type, unit, base, and — for a datetime — epoch and sub-second fraction are
    preserved, so ``dumps()`` round-trips losslessly); otherwise scalars decode
    to native Python values (a symbol or reference becomes a ``str``).

    This is a single-pass *streaming* parse: it enforces the lexical grammar and
    per-value type/unit/range rules, but NOT the whole-document (DOM-tier)
    constraints — duplicate struct keys, cross-sibling array homogeneity, and
    matrix rectangularity. A document that ``bovnar validate`` (the CLI) or
    :func:`dom_parse` would reject for one of those can still load here, mirroring
    the C streaming reader. Use :func:`dom_parse` when you need full validation.
    """
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


def _uses_spec_1_1(obj) -> bool:
    """True if serialising obj would emit a spec-1.1-only construct — a datetime
    Quantity, or a reference whose path indexes an array (&.a[0]) — so dumps()
    must prepend a #!bovnar 1.1 directive for the output to round-trip."""
    if isinstance(obj, Quantity):
        # vtype is normally a ValueTypeSpec, but the constructor does not enforce
        # it; read .family defensively so a Quantity built with an unexpected
        # vtype reports "not a datetime" rather than raising AttributeError mid
        # serialisation (the directive-detection pass must not be the thing that
        # crashes dumps()).
        if getattr(obj.vtype, 'family', None) == int(ValueTypeFamily.DATETIME):
            return True
        # A reference is re-emitted as &.path; array indexing in that path
        # (&.a[0][1]) is spec 1.1, so its '[' requires the directive.
        if obj._tok_type == _TOKEN_IS_REFERENCE and obj.raw and '[' in obj.raw:
            return True
        return False
    if isinstance(obj, dict):
        return any(_uses_spec_1_1(v) for v in obj.values())
    if isinstance(obj, (list, tuple)):
        return any(_uses_spec_1_1(v) for v in obj)
    return False


def dumps(obj: dict, *, pretty: bool = True) -> bytes:
    if not isinstance(obj, dict):
        raise BovnarArgumentError("dumps() requires a dict at the top level")
    need_version = _uses_spec_1_1(obj)
    cap = 4 * 1024 * 1024
    while True:
        try:
            with Writer.to_mem(cap=cap, pretty=pretty) as w:
                if need_version:
                    w.write_version(1, 1)
                _emit_dict(w, obj)
            return w.get_output()
        except BovnarWriteError as e:
            if e.code == ErrorCode.SINK_BUFFER_EXHAUSTED and cap < 256 * 1024 * 1024:
                cap *= 2
            else:
                raise


def dom_parse(data: bytes | bytearray | str | memoryview,
              policy: 'UnitPolicy | None' = None) -> DomDoc:
    """Parse BVNR bytes into a DOM tree (random-access, type-preserving).

    With a :class:`UnitPolicy`, the same rules the streaming reader applies:
    a validation failure raises, and a value the policy converted is STORED
    converted — digits, unit and base.
    """
    if isinstance(data, str):
        data = data.encode('utf-8')
    return DomDoc.parse(bytes(data) if isinstance(data, memoryview) else data,
                        policy)


def unit_factor(unit_str: str) -> float:
    """Return the SI/IEC **prefix** scale of *unit_str* — the prefix alone.

    This is ``bvn_unit_prefix_factor``: it reports what the prefixes contribute
    and nothing about the base unit itself, so a unit with no prefix is 1.0
    however far it is from SI.

      unit_factor("M~Hz")  → 1000000.0
      unit_factor("Gi~B")  → 1073741824.0
      unit_factor("in")    → 1.0          # not 0.0254
      unit_factor("h")     → 1.0          # not 3600.0

    For the full conversion to coherent SI — the one most callers want — use
    :func:`bovnar.units.unit_to_si_factor`, which also reports the affine offset
    and refuses a unit that has no SI mapping.

    Raises BovnarArgumentError if *unit_str* does not parse.
    """
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


def unit_to_ucum(unit: ValueUnit) -> str:
    """
    The UCUM code for *unit*, without the ``ucum:`` prefix.

    Partial by construction (doc/10_bovnar_ucum_profile.md 5.3): a native
    unit outside the transliteration table has no UCUM form, which includes
    the Old German units,
    the water-hardness degrees, the turbidity kinds and every currency. Those
    raise rather than returning an invented code.
    """
    import ctypes as _ct
    from ._ffi import get_library
    lib = get_library()
    buf = _ct.create_string_buffer(256)
    n   = lib.bvn_unit_to_ucum(unit, buf, 256)
    if n < 0:
        raise BovnarArgumentError("unit_to_ucum: this unit has no UCUM code")
    return buf.raw[:n].decode('utf-8')


def unit_is_profile_only(unit: ValueUnit) -> bool:
    """
    True when *unit* has no native spelling and serialises in profile notation.

    That is exactly the units carrying a UCUM arbitrary atom -- ``[IU]``,
    ``[PFU]`` and the rest of that block, which are assay-defined and
    commensurable with nothing, not even with each other.
    """
    from ._ffi import get_library
    return bool(get_library().bvn_unit_is_profile_only(unit))


def unit_error_code(unit_str: str) -> int:
    """
    Why :func:`parse_unit` rejected *unit_str*, as an ``error_code_t`` value.

    Distinguishes malformed input (``error_unit_illegal``) from an unrecognised
    profile namespace (``error_unit_profile_unknown``) and from valid UCUM this
    build cannot represent (``error_unit_profile_unsupported``). Returns
    ``error_none`` (0) for a string that does parse.
    """
    import ctypes as _ct
    from ._ffi import get_library
    lib = get_library()
    raw = unit_str.encode('utf-8')
    arr = (_ct.c_uint8 * len(raw)).from_buffer_copy(raw)
    return int(lib.bvn_unit_error_code(arr, _ct.c_uint32(len(raw))))


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
            int(ValueTypeFamily.DATETIME):  'datetime',
        }
        name = fam_names.get(int(vt.family), 'uint')
        w._emit_annotation(name, vt, vu if vu is not None else make_unit_none())

    if rows and all(isinstance(r, (list, tuple)) for r in rows):
        row_list = rows
    else:
        row_list = [rows]

    # A single explicit row is NESTED, not flattened. The outer list maps to
    # /-separated rows here, but the reader maps NESTING to dimensions, and with
    # exactly one row the two disagree: [[0,1,2,3]] was written as [0,1,2,3] and
    # read back with shape (4,) instead of (1,4). The format can express it --
    # ".x = [[0, 1, 2, 3]];" reads back as (1,4) -- so this is a writer collapse,
    # not a format limit. Two or more rows are unaffected: [[1,2],[3,4]] stays
    # [1,2]/[3,4], which already reads back as (2,2).
    if len(row_list) == 1 and row_list is rows:
        w.begin_array_row()
        _emit_array_element(w, row_list[0])
        w.end_array_row()
        return

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
            # A symbol or reference carries no family, but its KIND (tok_type)
            # must survive a typed round-trip: dumps() re-emits a symbol as a
            # bare word and a reference as &.path, not as a quoted string. Wrap
            # it in a Quantity carrying the tok_type (the writer already honours
            # it). Other PLAIN values (null) decode to a native Python value.
            if tok_type in (_TOKEN_IS_SYMBOL, _TOKEN_IS_REFERENCE):
                text = raw.decode('utf-8', errors='replace') if raw else None
                return Quantity(text, vt, make_unit_none(), tok_type)
            return _decode_value(raw, fam, vt, tok_type)
        text = raw.decode('utf-8', errors='replace') if raw else None
        vu   = data.value_unit if data is not None else make_unit_none()
        # spec 1.1 — a datetime literal's sub-second digits live on the event,
        # not in `raw` (the whole-second carrier); carry them so dumps() can
        # round-trip the fraction losslessly.
        frac = data.frac_str() if data is not None else None
        return Quantity(text, vt, vu, tok_type, frac=frac)


def _seal_array(arr: _ArrScope):
    """
    Collapse an _ArrScope into a Python value.

    A single-row array is unwrapped to a flat list.
    A multi-row array becomes a list of lists.
    """
    if len(arr.rows) == 1:
        return arr.rows[0]
    return arr.rows


# Mirror of C token_type_t; see enums.TokenType (checked against the header
# by test_enums.py) rather than re-declaring the numbers here.
_TOKEN_IS_SYMBOL    = TokenType.SYMBOL
_TOKEN_IS_REFERENCE = TokenType.REFERENCE
_TOKEN_IS_BOOL      = TokenType.BOOL


def _char_to_digit(c: int, base: int) -> int:
    """Mirror of the C bvn_char_to_digit: ordinal -> digit value, or `base`
    to signal "not a digit". Bases 64/85 use the standard Base64 / Ascii85
    alphabets; bases up to 36 are case-insensitive, 37..62 case-sensitive."""
    if base == 64:
        if 0x41 <= c <= 0x5A: return c - 0x41          # A-Z -> 0..25
        if 0x61 <= c <= 0x7A: return c - 0x61 + 26     # a-z -> 26..51
        if 0x30 <= c <= 0x39: return c - 0x30 + 52     # 0-9 -> 52..61
        if c == 0x2B: return 62                         # '+'
        if c == 0x2F: return 63                         # '/'
        return base
    if base == 85:
        if 0x21 <= c <= 0x75: return c - 0x21          # '!'..'u' -> 0..84
        return base
    if 0x30 <= c <= 0x39:   d = c - 0x30
    elif 0x61 <= c <= 0x7A: d = 10 + (c - 0x61)
    elif 0x41 <= c <= 0x5A: d = (36 + (c - 0x41)) if base > 36 else (10 + (c - 0x41))
    else:                   return base
    return d if d < base else base


def _int_from_base(text: str, base: int) -> int:
    """Parse an integer in any bovnar base (2..62, 64, 85). Python's built-in
    int(text, base) only supports bases 2..36, so high bases are decoded here
    with the same alphabet the C reader/writer use. Bases 64/85 are unsigned
    (their '+'/'-' are digits, not signs)."""
    s = text
    neg = False
    i = 0
    if base not in (64, 85) and s[:1] in ('+', '-'):
        neg = s[0] == '-'
        i = 1
    if i >= len(s):
        raise ValueError(f"no digits in {text!r} for base {base}")
    val = 0
    for ch in s[i:]:
        d = _char_to_digit(ord(ch), base)
        if d >= base:
            raise ValueError(f"invalid digit {ch!r} for base {base}")
        val = val * base + d
    return -val if neg else val


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

    if fam == ValueTypeFamily.DATETIME:
        # epoch seconds — a decimal signed integer (base holds the epoch index,
        # not a numeric base). Use bovnar.peek/units helpers or bvn_datetime to
        # convert to civil time.
        try:
            return int(text, 10)
        except ValueError:
            return text

    if fam in (ValueTypeFamily.UINT, ValueTypeFamily.SINT):
        base = vt.base if vt and vt.base > 1 else 10
        try:
            # int() only handles bases 2..36; high bases need the custom decoder.
            return int(text, base) if base <= 36 else _int_from_base(text, base)
        except ValueError:
            return text

    if fam in (ValueTypeFamily.FLOAT,
               ValueTypeFamily.FLOAT_FIX,
               ValueTypeFamily.FLOAT_DEC):
        if text in ('nan', 'inf', 'ninf'):
            return {'nan': float('nan'),
                    'inf': float('inf'),
                    'ninf': float('-inf')}[text]
        # A <float:W,_16> carrier is a hex-float literal, not a decimal one.
        # float(text) read "1.8" as decimal 1.8 where the value is 1.5, and
        # returned the STRING "1.0p+0" for the binary-exponent form because it
        # could not parse it at all. Quantity.decimal() already honours the base
        # via _numeric_base(), so the two Python paths disagreed with each other
        # as well as with the C reader.
        # FLOAT only: for float_fix the base field carries the Q parameter (the
        # fractional-bit count) and for float_dec it is unused -- both are always
        # decimal, exactly as bvn_effective_base() decides in C. Reading Q=16 as
        # "base 16" turned 1.5 into 1.3125.
        base = (vt.base if vt and vt.base > 1 else 10) \
               if fam == ValueTypeFamily.FLOAT else 10
        if base == 16:
            try:
                from ._bvnfloat import BvnFloat
                return float(BvnFloat.from_str(text, 16).to_decimal())
            except Exception:
                return text
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
    int(ValueTypeFamily.DATETIME):  'datetime',
}


def _has_real_unit(vu: ValueUnit) -> bool:
    return any(vu.components[i].base != 0 for i in range(vu.num_components))


def _needs_annotation(vt: ValueTypeSpec, vu: ValueUnit, raw: str | None = None) -> bool:
    """Return True only when the type annotation carries non-default information.

    `raw` matters for exactly one case: default synthesis reads a bare
    NON-NEGATIVE integer as uint, so a sint value that happens to be >= 0 loses
    its family without an annotation -- "<sint:64> 127" came back as
    "<uint:64> 127". A negative one re-reads as sint on its own and needs none.
    """
    fam = int(vt.family)
    if fam == int(ValueTypeFamily.PLAIN):
        return False
    if fam == int(ValueTypeFamily.SINT) and raw is not None \
            and not raw.lstrip().startswith('-'):
        return True
    # datetime is its own family — it must always be annotated (else the value
    # reloads as a plain integer, losing the timestamp semantics), even for the
    # default unix epoch / width 64.
    if fam == int(ValueTypeFamily.DATETIME):
        return True
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
    if fam_name is not None and _needs_annotation(q.vtype, q.unit, q.raw):
        w._emit_annotation(fam_name, q.vtype, q.unit)
    raw_bytes = q.raw.encode('utf-8') if q.raw else b''
    d = BvnrData()
    d.type       = q._tok_type
    d.value_type = q.vtype
    d.value_unit = q.unit
    d.data       = _ct.cast(_ct.c_char_p(raw_bytes), _ct.c_void_p)
    d.length     = len(raw_bytes)
    # spec 1.1 — pass a datetime's verbatim sub-second digits so the C writer
    # re-emits the ISO literal (else the whole-second carrier alone would
    # silently drop the fraction). frac_bytes must outlive the write call.
    frac_bytes = q._frac.encode('ascii') if q._frac else None
    if frac_bytes:
        d.frac_data   = _ct.cast(_ct.c_char_p(frac_bytes), _ct.c_void_p)
        d.frac_length = len(frac_bytes)
    _write_event_data(w, d)


def _array_quantity_annotation(rows):
    """(ValueTypeSpec, ValueUnit) for an array whose leaves are all Quantities
    that agree on type and unit, else (None, None).

    An array carries ONE annotation for all its elements, and dumps() derived it
    only for datetimes and wide integers. Everything else fell through to a bare
    array, so a typed load/dump cycle silently dropped the unit and the width:
    <float:64,k~m> [1.5, 2.5] came back as [1.5, 2.5], turning 1.5 km into a
    dimensionless 1.5 -- while Quantity's own docstring promises that "numeric
    precision, integer base, and physical units are all preserved across a
    load/dump cycle".
    """
    first = None
    needed = False       # does ANY leaf need the annotation?
    stack = [rows]
    while stack:
        cur = stack.pop()
        for e in cur:
            if isinstance(e, (list, tuple)):
                stack.append(e)
                continue
            if e is None:
                continue                      # a null hole constrains nothing
            if not isinstance(e, Quantity):
                return None, None
            if int(e.vtype.family) == int(ValueTypeFamily.DATETIME):
                return None, None             # handled by the datetime path
            key = (int(e.vtype.family), int(e.vtype.width), int(e.vtype.base),
                   e.unit_str())
            # Ask EVERY leaf, not just the first. A sint array of mixed sign
            # needs the annotation because its non-negative elements would
            # otherwise re-read as uint, but the first element it happened to see
            # was negative and answered "no annotation needed" for the lot.
            if _needs_annotation(e.vtype, e.unit, e.raw):
                needed = True
            if first is None:
                first = (key, e)
            elif key != first[0]:
                return None, None             # mixed: no single annotation fits
    if first is None:
        return None, None
    (fam, width, base, _us), sample = first
    if fam == int(ValueTypeFamily.PLAIN):
        return None, None
    vt = ValueTypeSpec()
    vt.family = fam
    vt.width  = width
    vt.base   = base
    unit = sample.unit if _has_real_unit(sample.unit) else None
    # Only annotate when the annotation says something the defaults do not --
    # otherwise a plain [1, 2, 3] would gain a redundant <uint:64> where it
    # previously had none.
    if not needed:
        return None, None
    return vt, unit


def _array_int_annotation(rows):
    """
    For an array whose leaves are ALL plain ints, return a ValueTypeSpec giving a
    width wide enough to hold the largest element — but only when that width
    exceeds 64 bits (otherwise the default needs no annotation). Returns None for
    empty / non-integer / mixed arrays, or when 64 bits already suffice, so normal
    arrays are written exactly as before. The annotation is emitted once at the
    array level (per-element widths are not serialised), so all elements share it;
    a single negative element promotes the whole array to sint.
    """
    saw_int   = False
    any_neg   = False
    max_pos_b = 0
    max_neg_b = 0
    stack = [rows]
    while stack:
        cur = stack.pop()
        for e in cur:
            if isinstance(e, bool):
                return None
            if isinstance(e, (list, tuple)):
                stack.append(e)
            elif isinstance(e, int):
                saw_int = True
                if e < 0:
                    any_neg = True
                    if (-e).bit_length() > max_neg_b:
                        max_neg_b = (-e).bit_length()
                elif e.bit_length() > max_pos_b:
                    max_pos_b = e.bit_length()
            else:
                return None
    if not saw_int:
        return None
    if any_neg:
        width = max(max_pos_b + 1, max_neg_b + 1)
        if width <= 64:
            return None
        return make_type_spec(ValueTypeFamily.SINT, width, 10)
    if max_pos_b <= 64:
        return None
    return make_type_spec(ValueTypeFamily.UINT, max_pos_b, 10)


def _array_datetime_annotation(rows):
    """
    If every leaf of *rows* is a datetime Quantity, return a ValueTypeSpec that
    annotates the whole array as datetime so the family (and epoch) survive a
    dumps() round-trip; otherwise return None so the caller falls back to the
    plain-integer path. Array elements carry no per-element annotation, so the
    datetime type is emitted once at the array level — every leaf must therefore
    share one epoch; a mixed-epoch array has no valid single annotation and is
    rejected. The widest element width wins (all elements are range-checked
    against it; their own value always fits).
    """
    saw_dt = False
    width  = 0
    base   = None
    stack  = [rows]
    while stack:
        cur = stack.pop()
        for e in cur:
            if isinstance(e, (list, tuple)):
                stack.append(e)
            elif (isinstance(e, Quantity)
                  and e.vtype.family == int(ValueTypeFamily.DATETIME)):
                saw_dt = True
                if e.vtype.width > width:
                    width = e.vtype.width
                if base is None:
                    base = e.vtype.base
                elif e.vtype.base != base:
                    raise BovnarArgumentError(
                        "Cannot serialise a datetime array whose elements use "
                        "different epochs; split it into one array per epoch.")
            else:
                return None
    if not saw_dt:
        return None
    return make_type_spec(ValueTypeFamily.DATETIME, width or 64, base or 0)


def _emit_value(w: Writer, key: str, value) -> None:
    if value is None:
        w.write_null(key)
    elif isinstance(value, bool):
        w.write_bool(key, value)
    elif isinstance(value, Quantity):
        _emit_quantity(w, key, value)
    elif isinstance(value, int):
        # write_uint/write_sint pass the value through a 64-bit ctypes argument,
        # which SILENTLY truncates a Python int that exceeds 64 bits (e.g. 2**100
        # would be written as 0). Route anything outside the 64-bit range to the
        # arbitrary-precision writer with a width wide enough to hold it.
        if 0 <= value <= 0xFFFFFFFFFFFFFFFF:
            w.write_uint(key, value)
        elif -0x8000000000000000 <= value < 0:
            w.write_sint(key, value)
        else:
            width = (value.bit_length() if value >= 0
                     else (-value).bit_length() + 1)
            w.write_bvni(key, value, width=width)
    elif isinstance(value, float):
        w.write_float(key, value)
    elif isinstance(value, Decimal):
        # exact -> a decimal-float (float_dec:64) literal, never via a C double
        _emit_quantity(w, key, Quantity.from_number(value))
    elif isinstance(value, Fraction):
        _emit_quantity(w, key, Quantity.from_number(value))
    elif isinstance(value, str):
        w.write_string(key, value)
    elif isinstance(value, dict):
        w.begin_struct(key)
        _emit_dict(w, value)
        w.end_struct()
    elif isinstance(value, (list, tuple)):
        vt = _array_datetime_annotation(value)
        vu = None
        if vt is None:
            vt, vu = _array_quantity_annotation(value)
        if vt is None:
            vt = _array_int_annotation(value)
        write_array(w, key, value, vt=vt, vu=vu)
    else:
        raise BovnarArgumentError(
            f"Cannot serialise value of type {type(value).__name__!r} "
            f"for key {key!r}")


def _emit_array_element(w: Writer, elem) -> None:
    import ctypes as _ct

    _TOKEN_IS_ARRAY_NUMBER = TokenType.ARRAY_NUMBER
    _TOKEN_IS_ARRAY_STRING = TokenType.ARRAY_STRING
    _TOKEN_IS_BOOL         = TokenType.BOOL
    _TOKEN_IS_NULL_VALUE   = TokenType.NULL_VALUE

    # A typed load (loads(typed=True)) yields Quantity elements. Array elements
    # are written bare (bovnar arrays carry no per-element type annotation), so
    # emit the decoded scalar; the element's own annotation is not preserved.
    if isinstance(elem, Quantity):
        # A datetime element carries its sub-second fraction on the Quantity, not
        # in .value (the whole-second carrier). Emit it directly with the fraction
        # so the C writer reconstructs the ISO literal (the array-number path
        # supports it) — otherwise an array would drop fractions the scalar path
        # keeps. The array-level <datetime> annotation supplies width/epoch.
        if (int(elem.vtype.family) == int(ValueTypeFamily.DATETIME)
                and elem._frac):
            raw  = elem.raw.encode('utf-8') if elem.raw else b''
            frac = elem._frac.encode('ascii')
            d = BvnrData()
            d.type        = _TOKEN_IS_ARRAY_NUMBER
            d.value_type  = elem.vtype
            d.value_unit  = make_unit_none()
            d.data        = _ct.cast(_ct.c_char_p(raw), _ct.c_void_p)
            d.length      = len(raw)
            d.frac_data   = _ct.cast(_ct.c_char_p(frac), _ct.c_void_p)
            d.frac_length = len(frac)
            _write_event_data(w, d)
            return
        # Write the ORIGINAL literal, not the decoded scalar. Going through
        # elem.value routes the number through a C double, so a float_dec:128
        # element with 29 significant digits came back with 17.
        if elem.raw and int(elem.vtype.family) in (
                int(ValueTypeFamily.FLOAT), int(ValueTypeFamily.FLOAT_DEC),
                int(ValueTypeFamily.FLOAT_FIX), int(ValueTypeFamily.UINT),
                int(ValueTypeFamily.SINT)):
            raw = elem.raw.encode('utf-8')
            d = BvnrData()
            d.type       = _TOKEN_IS_ARRAY_NUMBER
            d.value_type = elem.vtype
            d.value_unit = make_unit_none()
            d.data       = _ct.cast(_ct.c_char_p(raw), _ct.c_void_p)
            d.length     = len(raw)
            _write_event_data(w, d)
            return
        _emit_array_element(w, elem.value)
        return

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
        # The per-element value_type is NOT serialised (array elements are written
        # bare), but the writer DOES range-check the value text against it, so the
        # width must be wide enough for this element or the write is rejected. The
        # element's own bit length always fits. Re-parse width comes from the
        # array-level annotation (_array_int_annotation) chosen in _emit_value.
        if elem >= 0:
            family = ValueTypeFamily.UINT
            width  = max(64, elem.bit_length())
        else:
            family = ValueTypeFamily.SINT
            width  = max(64, (-elem).bit_length() + 1)
        vt  = make_type_spec(family, width, 10)
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
        elem = float(elem)                  # normalise numpy float scalars
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

    elif isinstance(elem, (Decimal, Fraction)):
        # Exact decimal/fraction element: emit the verbatim literal as a number.
        # The array-level annotation (set by from_numpy) supplies the real
        # family/width; FLOAT_DEC:64 here is only the lenient per-element
        # range-check carrier (magnitude, not digit count).
        from .quantity import _exact_number_text
        raw = _exact_number_text(elem).encode('ascii')
        d = BvnrData()
        d.type       = _TOKEN_IS_ARRAY_NUMBER
        d.value_type = make_type_spec(ValueTypeFamily.FLOAT_DEC, 64, 0)
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
        import numbers
        if isinstance(elem, (numbers.Integral, numbers.Real)):
            # any other real numeric scalar (e.g. np.float32, np.int64) reaching
            # an object array: format it exactly and emit as a bare number. The
            # array-level annotation supplies the real family/width.
            from .quantity import _exact_number_text
            raw = _exact_number_text(elem).encode('ascii')
            d = BvnrData()
            d.type       = _TOKEN_IS_ARRAY_NUMBER
            d.value_type = make_type_spec(ValueTypeFamily.FLOAT_DEC, 64, 0)
            d.value_unit = make_unit_dimensionless()
            d.data       = _ct.cast(_ct.c_char_p(raw), _ct.c_void_p)
            d.length     = len(raw)
            _write_event_data(w, d)
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
