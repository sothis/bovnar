import ctypes
from .enums import (
    Event, ValueTypeFamily, PrefixSystem, SIPrefix, IECPrefix,
    BaseUnit, Exponent, ErrorCode,
)

OPAQUE_BYTES = 256

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

class BvnrData(ctypes.Structure):
    _fields_ = [
        ('type',       ctypes.c_int),
        ('value_type', ValueTypeSpec),
        ('value_unit', ValueUnit),
        ('data',       ctypes.c_void_p),
        ('length',     ctypes.c_uint32),
    ]

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

class BvnrSource(ctypes.Structure):
    _fields_ = [('_opaque', ctypes.c_uint8 * OPAQUE_BYTES)]

class BvnrSink(ctypes.Structure):
    _fields_ = [('_opaque', ctypes.c_uint8 * OPAQUE_BYTES)]

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
    ]

class BvnDomEntry(ctypes.Structure):
    _fields_ = [
        ('key',   ctypes.c_char_p),
        ('value', ctypes.c_void_p),
    ]

def make_data_key(key: str) -> BvnrData:
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
    for i, comp in enumerate(components):
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
