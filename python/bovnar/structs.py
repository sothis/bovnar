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
from .enums import (
    Event, ValueTypeFamily, PrefixSystem, SIPrefix, IECPrefix,
    BaseUnit, Exponent, ErrorCode,
)

# Reserved size for the opaque pass-through structs (BvnrSource / BvnrSink).
# These are black boxes the C side owns entirely: Python only allocates them and
# hands &struct to C (bvnr_source_from_fd, bvnr_open_read_source, ...). C uses
# BVNR_SOURCE_RESERVED_SIZE / BVNR_SINK_RESERVED_SIZE bytes (64 each as of this
# writing). We deliberately over-allocate (256) as a forward-compatibility
# margin: a binding may be LARGER than the C struct (C writes within its own
# bounds) but must never be SMALLER. test_abi.py enforces `python_size >= c_size`
# against the live C layout, so if the C reserve ever grows past 256 the build
# fails loudly here instead of under-allocating and corrupting memory.
OPAQUE_BYTES = 256
# Use 64-bit words so the ctypes struct's alignment matches the C
# struct's (pointer-aligned).  c_uint8 arrays have alignment 1.
_OPAQUE_WORDS = OPAQUE_BYTES // 8

MAX_UNIT_COMPONENTS = 8

ON_ERROR_FUNC = ctypes.CFUNCTYPE(
    None,
    ctypes.c_void_p,
    ctypes.c_int,
    ctypes.c_uint64,
    ctypes.c_uint64,
    ctypes.c_uint32,
    ctypes.c_uint64,
)

class _PrefixId(ctypes.Union):
    _fields_ = [
        ('si',  ctypes.c_int),
        ('iec', ctypes.c_int),
    ]

class ValueUnitPrefix(ctypes.Structure):
    _fields_ = [
        ('system', ctypes.c_int),
        ('id',     _PrefixId),
    ]

    @classmethod
    def make_si(cls, prefix: SIPrefix) -> 'ValueUnitPrefix':
        p = cls()
        p.system = int(PrefixSystem.SI)
        p.id.si  = int(prefix)
        return p

    @classmethod
    def make_iec(cls, prefix: IECPrefix) -> 'ValueUnitPrefix':
        p = cls()
        p.system = int(PrefixSystem.IEC)
        p.id.iec = int(prefix)
        return p

    @property
    def prefix_system(self) -> PrefixSystem:
        return PrefixSystem(self.system)

    @property
    def si_prefix(self) -> SIPrefix:
        return SIPrefix(self.id.si)

    @property
    def iec_prefix(self) -> IECPrefix:
        return IECPrefix(self.id.iec)

    def __repr__(self) -> str:
        if self.prefix_system == PrefixSystem.SI:
            return f"ValueUnitPrefix(si={self.si_prefix.name})"
        return f"ValueUnitPrefix(iec={self.iec_prefix.name})"


class ValueUnitComponent(ctypes.Structure):
    _fields_ = [
        ('base',     ctypes.c_int),
        ('exponent', ctypes.c_int),
        ('prefix',   ValueUnitPrefix),
    ]

    @property
    def base_unit(self) -> BaseUnit:
        return BaseUnit(self.base)

    @property
    def exp(self) -> Exponent:
        return Exponent(self.exponent)

    @property
    def prefix_system(self) -> PrefixSystem:
        return PrefixSystem(self.prefix.system)

    @property
    def si_prefix(self) -> SIPrefix:
        return SIPrefix(self.prefix.id.si)

    @property
    def iec_prefix(self) -> IECPrefix:
        return IECPrefix(self.prefix.id.iec)

    def __repr__(self) -> str:
        sys = self.prefix_system
        if sys == PrefixSystem.SI:
            pfx = f"si={self.si_prefix.name}"
        else:
            pfx = f"iec={self.iec_prefix.name}"
        return (f"ValueUnitComponent(base={self.base_unit.name}, "
                f"exp={self.exp.name}, {pfx})")

class ValueUnit(ctypes.Structure):
    _fields_ = [
        ('num_components', ctypes.c_uint32),
        ('components',     ValueUnitComponent * MAX_UNIT_COMPONENTS),
    ]

    def active_components(self) -> list:
        return list(self.components[:self.num_components])

    @property
    def is_dimensionless(self) -> bool:
        n = self.num_components
        if n == 0:
            return True
        if n == 1 and self.components[0].base == BaseUnit.NONE:
            return True
        return False

    def __repr__(self) -> str:
        return (f"ValueUnit(n={self.num_components}, "
                f"components={self.active_components()!r})")

class ValueTypeSpec(ctypes.Structure):
    _fields_ = [
        ('family', ctypes.c_int),
        ('width',  ctypes.c_uint32),
        ('base',   ctypes.c_uint32),
    ]

    @property
    def type_family(self) -> ValueTypeFamily:
        return ValueTypeFamily(self.family)

    def __repr__(self) -> str:
        f = self.type_family
        return f"ValueTypeSpec(family={f.name}, width={self.width}, base={self.base})"

class BvnrConverted(ctypes.Structure):
    # Mirrors C `bvnr_converted_t`: the exact result of a lossless read-time
    # unit/base conversion. `text` is the value's full positional expansion in
    # `base` (reader-owned, callback lifetime); num/den are opaque bvn_int_t*.
    # `text` is NULL when the exact result has no terminating expansion in `base`
    # and the reader was opened with want_unit_allow_nonterminating — num/den are
    # still exact there. See BvnrData.converted_str().
    _fields_ = [
        ('unit',   ValueUnit),
        ('text',   ctypes.c_char_p),
        ('length', ctypes.c_uint32),
        ('base',   ctypes.c_uint32),
        ('num',    ctypes.c_void_p),
        ('den',    ctypes.c_void_p),
    ]

class BvnrData(ctypes.Structure):
    # Must mirror the C `bvnr_data_t` layout exactly: the writer path allocates
    # this and passes &d to bvnr_write_event(), and the reader path receives a
    # C-allocated pointer. The spec-1.1 frac_* fields and the conversion carrier
    # are appended last, matching the C struct — omitting them makes the struct
    # short, so C would read out of bounds.
    _fields_ = [
        ('type',        ctypes.c_int),
        ('value_type',  ValueTypeSpec),
        ('value_unit',  ValueUnit),
        ('data',        ctypes.c_void_p),
        ('length',      ctypes.c_uint32),
        ('frac_data',   ctypes.c_void_p),    # spec 1.1 — ISO datetime sub-second digits, else NULL
        ('frac_length', ctypes.c_uint32),    # spec 1.1 — length of frac_data, else 0
        ('converted',   ctypes.c_bool),      # lossless read-time conversion applied?
        ('conv',        BvnrConverted),      # exact converted value when converted
    ]

    def frac_str(self) -> "str | None":
        """The verbatim ISO sub-second digits (spec 1.1), or None when absent."""
        if not self.frac_data or self.frac_length == 0:
            return None
        return (ctypes.c_char * self.frac_length).from_address(
            self.frac_data).raw.decode('ascii')

    def converted_str(self) -> "str | None":
        """The exact converted value in the requested base (lossless read-time
        unit/base conversion), or None when no conversion was applied.

        Also None — with `converted` True — when the exact result has no
        terminating expansion in the requested base and the reader was opened
        with want_unit_allow_nonterminating: there is no finite digit string to
        return, only the exact rational in conv.num/conv.den. Check `converted`
        rather than this method to tell the two cases apart."""
        if not self.converted or not self.conv.text:
            return None
        return self.conv.text.decode('ascii')

    def converted_rational(self) -> "tuple[int, int] | None":
        """The exact converted value as a reduced ``(numerator, denominator)``
        pair, or None when no conversion was applied.

        This is the only carrier of the value when ``converted_str()`` returns
        None — the result was exact but had no terminating expansion in the
        requested base (``want_unit_allow_nonterminating``). ``Fraction(*pair)``
        gives you the exact number.

        Valid only while the callback is running: num/den point at reader-owned
        bignums.
        """
        if not self.converted or not self.conv.num or not self.conv.den:
            return None
        from ._ffi import get_library
        lib = get_library()

        def _to_int(handle: int) -> int:
            bits = lib.bvn_int_bitlen(handle) or 1
            size = lib.bvn_int_str_bufsize(bits, 10)
            buf  = ctypes.create_string_buffer(size)
            if lib.bvn_int_to_str(handle, buf, size, 10) < 0:
                raise ValueError("bvn_int_to_str failed on a conversion result")
            return int(buf.value.decode('ascii'))

        return (_to_int(self.conv.num), _to_int(self.conv.den))

    def converted_in_base(self, base: int) -> "str | None":
        """Render the exact converted value in ``base`` (2..62, 64 or 85).

        Returns None when no conversion was applied, or when the exact value has
        no terminating expansion in that base — use converted_rational() there.
        Lets a caller re-render in a base other than the one requested at read
        time. Valid only while the callback is running.
        """
        if not self.converted or not self.conv.num or not self.conv.den:
            return None
        from ._ffi import get_library
        lib   = get_library()
        size  = lib.bvn_rational_str_bufsize(self.conv.num, self.conv.den, base)
        if not size:
            raise ValueError(f"base {base} is not one bovnar can write")
        buf   = ctypes.create_string_buffer(size)
        exact = ctypes.c_bool(False)
        n = lib.bvn_rational_to_str(self.conv.num, self.conv.den, base,
                                    buf, size, ctypes.byref(exact))
        if n < 0 or not exact.value:
            return None                    # non-terminating, or unrepresentable
        return buf.value.decode('ascii')

    def raw_bytes(self) -> bytes:
        if not self.data or self.length == 0:
            return b''
        return (ctypes.c_char * self.length).from_address(self.data).raw

    def raw_str(self, encoding: str = 'utf-8') -> str:
        return self.raw_bytes().decode(encoding)

    def __repr__(self) -> str:
        return (f"BvnrData(type={self.type}, "
                f"vt={self.value_type!r}, "
                f"len={self.length}, "
                f"raw={self.raw_bytes()!r})")

EVENT_CALLBACK_FUNC = ctypes.CFUNCTYPE(
    ctypes.c_bool,
    ctypes.c_void_p,
    ctypes.c_int,
    ctypes.POINTER(BvnrData),
)

# bvnr_read_flags_t.want_unit:
#   bool (*want_unit)(void* ud, const bvnr_data_t* data,
#                     value_unit_t* want, uint32_t* want_base)
WANT_UNIT_FUNC = ctypes.CFUNCTYPE(
    ctypes.c_bool,
    ctypes.c_void_p,
    ctypes.POINTER(BvnrData),
    ctypes.POINTER(ValueUnit),
    ctypes.POINTER(ctypes.c_uint32),
)

# bvnr_stream.h callback signatures.
#   bool (*on_document)(void* ud, uint64_t index, bool ok, error_code_t err)
ON_DOCUMENT_FUNC = ctypes.CFUNCTYPE(
    ctypes.c_bool,
    ctypes.c_void_p,
    ctypes.c_uint64,
    ctypes.c_bool,
    ctypes.c_int,
)
#   bool (*on_message)(void* ud, uint64_t channel, const uint8_t* data, uint64_t len)
MUX_ON_MSG_FUNC = ctypes.CFUNCTYPE(
    ctypes.c_bool,
    ctypes.c_void_p,
    ctypes.c_uint64,
    ctypes.c_void_p,
    ctypes.c_uint64,
)

class BvnrSource(ctypes.Structure):
    _fields_ = [('_opaque', ctypes.c_uint64 * _OPAQUE_WORDS)]

class BvnrSink(ctypes.Structure):
    _fields_ = [('_opaque', ctypes.c_uint64 * _OPAQUE_WORDS)]

class BvnrReadFlags(ctypes.Structure):
    _fields_ = [
        ('max_identifier_length', ctypes.c_uint16),
        ('max_string_length',     ctypes.c_uint16),
        ('max_number_length',     ctypes.c_uint16),
        ('max_symbol_length',     ctypes.c_uint16),
        ('max_reference_length',  ctypes.c_uint16),
        ('max_array_items',       ctypes.c_uint64),
        ('max_text_bytes',        ctypes.c_uint64),
        ('max_file_size',         ctypes.c_uint64),
        ('max_struct_nesting',    ctypes.c_uint8),
        ('max_array_nesting',     ctypes.c_uint8),
        ('userdata',              ctypes.c_void_p),
        ('on_unverified',         EVENT_CALLBACK_FUNC),
        ('on_verified',           EVENT_CALLBACK_FUNC),
        ('continue_on_error',     ctypes.c_bool),
        ('on_error',              ON_ERROR_FUNC),
        ('strict_version',        ctypes.c_bool),
        ('want_unit_allow_nonterminating', ctypes.c_bool),
        ('max_conversion_length', ctypes.c_uint32),
        ('want_unit',             WANT_UNIT_FUNC),
        ('_reserved',             ctypes.c_uint64 * 2),
    ]

class BvnrWriteFlags(ctypes.Structure):
    _fields_ = [
        ('max_identifier_length', ctypes.c_uint16),
        ('max_string_length',     ctypes.c_uint16),
        ('max_number_length',     ctypes.c_uint16),
        ('max_symbol_length',     ctypes.c_uint16),
        ('max_reference_length',  ctypes.c_uint16),
        ('max_array_items',       ctypes.c_uint64),
        ('max_text_bytes',        ctypes.c_uint64),
        ('max_file_size',         ctypes.c_uint64),
        ('max_struct_nesting',    ctypes.c_uint8),
        ('max_array_nesting',     ctypes.c_uint8),
        ('userdata',              ctypes.c_void_p),
        ('on_event',              EVENT_CALLBACK_FUNC),
        ('continue_on_error',     ctypes.c_bool),
        ('on_error',              ON_ERROR_FUNC),
        ('unit_flags',            ctypes.c_uint32),
        ('emit_version',          ctypes.c_bool),
        ('_reserved',             ctypes.c_uint64 * 4),
    ]

class BvnrDocStreamOpts(ctypes.Structure):
    """Mirror of bvnr_doc_stream_opts_t (bovnar_stream.h)."""
    _fields_ = [
        ('flags',                ctypes.POINTER(BvnrReadFlags)),
        ('userdata',             ctypes.c_void_p),
        ('on_document',          ON_DOCUMENT_FUNC),
        ('continue_past_failed', ctypes.c_bool),
        ('max_document_size',    ctypes.c_uint64),
    ]

class BvnDomEntry(ctypes.Structure):
    _fields_ = [
        ('key',   ctypes.c_char_p),
        ('value', ctypes.c_void_p),
    ]

def make_data_key(key: str) -> tuple:
    raw = key.encode('utf-8')
    d = BvnrData()
    d.data = ctypes.cast(ctypes.c_char_p(raw), ctypes.c_void_p)
    d.length = len(raw)
    return d, raw

def make_data_value(value_str: str,
                    vt: ValueTypeSpec,
                    vu: ValueUnit) -> tuple:
    raw = value_str.encode('utf-8')
    d = BvnrData()
    d.value_type = vt
    d.value_unit = vu
    d.data = ctypes.cast(ctypes.c_char_p(raw), ctypes.c_void_p)
    d.length = len(raw)
    return d, raw

def make_data_typed(vt: ValueTypeSpec, vu: ValueUnit) -> BvnrData:
    d = BvnrData()
    d.value_type = vt
    d.value_unit = vu
    return d

def make_type_spec(family: ValueTypeFamily,
                   width: int = 0,
                   base: int = 0) -> ValueTypeSpec:
    vt = ValueTypeSpec()
    vt.family = int(family)
    vt.width  = width
    vt.base   = base
    return vt

def make_unit_dimensionless() -> ValueUnit:
    vu = ValueUnit()
    vu.num_components = 1
    vu.components[0].base              = int(BaseUnit.NONE)
    vu.components[0].exponent          = int(Exponent.LINEAR)
    vu.components[0].prefix.system     = int(PrefixSystem.SI)
    vu.components[0].prefix.id.si      = int(SIPrefix.NONE)
    return vu

def make_unit_none() -> ValueUnit:
    return ValueUnit()

def make_unit_si(base: BaseUnit,
                 prefix: SIPrefix = SIPrefix.NONE,
                 exp: Exponent = Exponent.LINEAR) -> ValueUnit:
    vu = ValueUnit()
    vu.num_components = 1
    c = vu.components[0]
    c.base             = int(base)
    c.exponent         = int(exp)
    c.prefix.system    = int(PrefixSystem.SI)
    c.prefix.id.si     = int(prefix)
    return vu

def make_unit_iec(base: BaseUnit,
                  prefix: IECPrefix = IECPrefix.NONE,
                  exp: Exponent = Exponent.LINEAR) -> ValueUnit:
    vu = ValueUnit()
    vu.num_components = 1
    c = vu.components[0]
    c.base             = int(base)
    c.exponent         = int(exp)
    c.prefix.system    = int(PrefixSystem.IEC)
    c.prefix.id.iec    = int(prefix)
    return vu

def make_unit_compound(components: list[dict]) -> ValueUnit:
    """
    Build a ValueUnit with multiple components (e.g. kg·m/s²).

    Each dict in *components* must contain:
      'base'       : BaseUnit  (required)
      'exp'        : Exponent  (default LINEAR)
      'si_prefix'  : SIPrefix  (optional, default NONE, mutually exclusive with iec_prefix)
      'iec_prefix' : IECPrefix (optional, mutually exclusive with si_prefix)

    Example – Newton (kg·m·s⁻²):
      make_unit_compound([
          {'base': BaseUnit.GRAM,   'exp': Exponent.LINEAR,     'si_prefix': SIPrefix.KILO},
          {'base': BaseUnit.METER,  'exp': Exponent.LINEAR},
          {'base': BaseUnit.SECOND, 'exp': Exponent.NEG_SQUARE},
      ])
    """
    if len(components) > MAX_UNIT_COMPONENTS:
        raise ValueError(
            f"Too many unit components: {len(components)} > {MAX_UNIT_COMPONENTS}")
    vu = ValueUnit()
    vu.num_components = len(components)
    _KEYS = {'base', 'exp', 'si_prefix', 'iec_prefix'}
    for i, comp in enumerate(components):
        # An unrecognised key used to be dropped in silence, which is how
        # {'base': ..., 'prefix': SIPrefix.KILO} built an UNPREFIXED unit and
        # every downstream check agreed it was fine. The keys differ by one word
        # from the obvious guess, so the typo is the likely case, not the rare one.
        unknown = set(comp) - _KEYS
        if unknown:
            raise ValueError(
                f"component {i}: unknown key(s) {sorted(unknown)}; "
                f"expected any of {sorted(_KEYS)}")
        c = vu.components[i]
        c.base     = int(comp['base'])
        c.exponent = int(comp.get('exp', Exponent.LINEAR))
        if 'iec_prefix' in comp:
            c.prefix.system  = int(PrefixSystem.IEC)
            c.prefix.id.iec  = int(comp['iec_prefix'])
        else:
            c.prefix.system  = int(PrefixSystem.SI)
            c.prefix.id.si   = int(comp.get('si_prefix', SIPrefix.NONE))
    return vu
