"""
The Unit object.

WHY THIS EXISTS. ``Quantity.unit`` used to hand back a raw ``ValueUnit`` -- a
ctypes ``Structure`` whose fields are ``num_components`` and a 32-long array of
``(base, exponent, prefix)`` triples. Everything a caller actually wanted to do
with it lived somewhere else, as a module-level function taking the struct back:

    bovnar.unit_to_str(q.unit)
    bovnar.units_compatible(q.unit, other.unit)
    bovnar.unit_to_ucum(q.unit)

so the obvious question ("what unit is this?") was answered by a function the
caller had to know to look for, and printing a Quantity's unit meant importing
the package root. Worse, the struct compared by ctypes identity, so ``==`` on two
ValueUnits meaning the same thing was False.

``ValueUnit`` now carries the same ``__eq__`` and a matching ``__hash__``, so the
raw struct ``parse_unit`` returns compares correctly too — the wrapper is the
place the operations live, not the only place equality works. The two hash
identically on purpose: ``Unit.__eq__`` accepts a bare ``ValueUnit``, so Python's
contract requires it.

``Unit`` is that struct with its own operations attached. It carries no state
beyond the struct -- ``Unit.raw`` is the same ``ValueUnit``, so anything that
took one still works -- and every operation delegates to the same C entry points
the module-level functions use. There is no second implementation of anything.

EQUALITY IS THE POINT, not the formatting. ``bvn_unit_equal`` compares the
multiset of (base, exponent, prefix) triples, so ``Unit`` gets a ``==`` that means
"the same unit" and a ``__hash__`` consistent with it. Two Quantities read from
``ucum:mm[Hg]`` and from ``mmHg`` now compare equal on their units, which is what
the format promises and what the ctypes default denied.
"""
from .exceptions import BovnarArgumentError
from .structs import ValueUnit, make_unit_none


class Unit:
    """A physical unit: the parsed form, with the operations that belong to it.

    Construct from a unit string with :meth:`parse`, or wrap a ``ValueUnit``
    directly. ``Unit(None)`` and ``Unit()`` are the dimensionless unit.
    """

    __slots__ = ('_vu',)

    def __init__(self, vu: ValueUnit | None = None) -> None:
        if vu is None:
            vu = make_unit_none()
        elif isinstance(vu, Unit):
            vu = vu.raw
        elif not isinstance(vu, ValueUnit):
            raise BovnarArgumentError(
                "Unit() takes a ValueUnit, a Unit, or None; got %s. To build one "
                "from text use Unit.parse('m/s')." % type(vu).__name__)
        self._vu = vu

    # ── construction ───────────────────────────────────────────────────────
    @classmethod
    def parse(cls, unit_str: str) -> 'Unit':
        """Parse a unit string -- native (``'m/s'``, ``'k~g'``) or a profile
        code (``'ucum:mm[Hg]'``). Raises for anything that is not a unit."""
        from . import parse_unit
        return cls(parse_unit(unit_str))

    @property
    def raw(self) -> ValueUnit:
        """The underlying ``ValueUnit`` struct, for the C-level entry points."""
        return self._vu

    # ── identity ───────────────────────────────────────────────────────────
    @property
    def is_dimensionless(self) -> bool:
        """True when this carries no unit at all (the empty unit, or a single
        ``bu_none`` component). Distinct from *dimensionless but real* units
        like ``%`` and ``ppm``, which are units and answer False."""
        from . import _has_real_unit
        return not _has_real_unit(self._vu)

    @property
    def is_profile_only(self) -> bool:
        """True when this unit has no native spelling and can only be written in
        a profile notation -- a UCUM arbitrary unit, a UNECE package code."""
        from . import unit_is_profile_only
        return unit_is_profile_only(self._vu)

    @property
    def factor(self) -> float:
        """Value of one of this unit in coherent SI units. Raises for a unit
        with no SI conversion -- a currency, or a profile-only unit."""
        from . import unit_to_si_factor
        return unit_to_si_factor(self._vu).factor

    # ── relations ──────────────────────────────────────────────────────────
    def compatible_with(self, other) -> bool:
        """True when a value in this unit can be converted to *other*: same
        dimension, and no quantity-kind fence between them."""
        from . import units_compatible
        return units_compatible(self._vu, Unit(other).raw
                                if not isinstance(other, str)
                                else Unit.parse(other).raw)

    def convert_factor(self, other) -> float:
        """Multiply a value in this unit by the result to get *other*. Raises
        when the two do not convert."""
        from . import unit_convert_factor
        tgt = Unit.parse(other) if isinstance(other, str) else Unit(other)
        return unit_convert_factor(self._vu, tgt.raw).factor

    # ── spellings ──────────────────────────────────────────────────────────
    def to_profile(self, namespace: str) -> str:
        """This unit as a code in *namespace* (``'ucum'``, ``'unece'``,
        ``'qudt'``, ``'qudt-qk'``, ``'udunits'``, ``'om'``), without the
        ``ns:`` prefix. ``'cf'`` never answers -- that namespace is read-only.
        Raises when the vocabulary has no code for it -- the reverse tables are
        partial by construction."""
        from . import unit_to_profile
        return unit_to_profile(namespace, self._vu)

    def __str__(self) -> str:
        """The canonical spelling, or ``''`` when dimensionless."""
        if self.is_dimensionless:
            return ''
        from . import unit_to_str
        return unit_to_str(self._vu)

    def __repr__(self) -> str:
        s = str(self)
        return "Unit(%r)" % s if s else "Unit()"

    # ── equality ───────────────────────────────────────────────────────────
    def __eq__(self, other) -> bool:
        if isinstance(other, str):
            try:
                other = Unit.parse(other)
            except BovnarArgumentError:
                return False
        if isinstance(other, ValueUnit):
            other = Unit(other)
        if not isinstance(other, Unit):
            return NotImplemented
        from ._ffi import get_library
        return bool(get_library().bvn_unit_equal(self._vu, other._vu))

    def __ne__(self, other) -> bool:
        eq = self.__eq__(other)
        return eq if eq is NotImplemented else not eq

    def __hash__(self) -> int:
        """Consistent with __eq__: the multiset of (base, exponent, prefix)
        triples, order-independent because bvn_unit_equal is."""
        n = int(self._vu.num_components)
        triples = frozenset(
            (int(self._vu.components[i].base),
             int(self._vu.components[i].exponent),
             int(self._vu.components[i].prefix.system),
             int(self._vu.components[i].prefix.id.si))
            for i in range(n))
        return hash(triples)

    def __bool__(self) -> bool:
        """False for the dimensionless unit, so ``if q.unit:`` reads."""
        return not self.is_dimensionless
