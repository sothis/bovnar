from .structs import ValueTypeSpec, ValueUnit, make_unit_none
from .enums   import ValueTypeFamily


class Quantity:
    """
    A typed, unit-annotated scalar value with its original text representation.

    Returned by ``loads(..., typed=True)`` and accepted by ``dumps()`` to
    enable exact round-trips: numeric precision, integer base, and physical
    units are all preserved across a load/dump cycle.
    """
    __slots__ = ('raw', 'vtype', 'unit', '_tok_type')

    def __init__(self,
                 raw:      str | None,
                 vtype:    ValueTypeSpec,
                 unit:     ValueUnit | None = None,
                 tok_type: int = 2) -> None:
        self.raw       = raw
        self.vtype     = vtype
        self.unit      = unit if unit is not None else make_unit_none()
        self._tok_type = tok_type

    @property
    def value(self):
        """Decode the stored text to the closest native Python scalar."""
        from . import _decode_value
        raw_bytes = self.raw.encode('utf-8') if self.raw else b''
        return _decode_value(raw_bytes, ValueTypeFamily(self.vtype.family),
                             self.vtype, self._tok_type)

    def unit_str(self) -> str:
        """Return the canonical unit string, or '' if dimensionless."""
        if not any(self.unit.components[i].base != 0
                   for i in range(self.unit.num_components)):
            return ''
        from . import unit_to_str
        return unit_to_str(self.unit)

    def __repr__(self) -> str:
        fam = ValueTypeFamily(self.vtype.family).name
        u   = self.unit_str()
        u_part = f' [{u}]' if u else ''
        return f'Quantity({self.raw!r}, {fam}{u_part})'

    def __eq__(self, other) -> bool:
        if not isinstance(other, Quantity):
            return NotImplemented
        return (self.raw        == other.raw
                and self.vtype.family == other.vtype.family
                and self.vtype.width  == other.vtype.width
                and self.vtype.base   == other.vtype.base
                and self.unit_str()   == other.unit_str())

    def __hash__(self) -> int:
        return hash((self.raw,
                     self.vtype.family,
                     self.vtype.width,
                     self.vtype.base,
                     self.unit_str()))
