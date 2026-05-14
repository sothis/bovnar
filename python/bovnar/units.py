import ctypes
from dataclasses import dataclass

from ._ffi import get_library
from .enums import BaseUnit, SIPrefix, IECPrefix, Exponent
from .structs import ValueUnit, make_unit_compound
from .exceptions import BovnarArgumentError

_SI_DIM_COUNT = 7
SI_DIM_NAMES  = ('m', 'kg', 's', 'A', 'K', 'mol', 'cd')

@dataclass(frozen=True)
class SIConversion:
    """
    Result of bvn_unit_to_si_factor.

    factor        : multiplicative scale to the SI base unit
    is_affine     : True when an additive offset applies (e.g. Celsius → Kelvin)
    affine_offset : the additive offset in SI base units (add *after* scaling)
    """
    factor:        float
    is_affine:     bool
    affine_offset: float

@dataclass(frozen=True)
class UnitConversion:
    """
    Result of unit_convert_factor.

    factor         : multiply the source value by this to get the target value
    requires_affine: True when a simple multiply is not sufficient (e.g. °C → °F)
    """
    factor:          float
    requires_affine: bool

@dataclass(frozen=True)
class ReducedUnit:
    """
    Result of unit_reduce.

    unit  : the simplified ValueUnit (e.g. kg·m·s⁻² → N)
    scale : multiplicative scale accumulated during reduction
    """
    unit:  ValueUnit
    scale: float


def unit_to_si_factor(unit: ValueUnit) -> SIConversion:
    """
    Return the full SI conversion for *unit*, including affine terms.

    For purely multiplicative units (most SI units), is_affine is False
    and affine_offset is 0.  For Celsius (and similar), is_affine is True
    and the correct conversion is:

        si_value = value * factor + affine_offset
    """
    lib          = get_library()
    is_affine    = ctypes.c_bool(False)
    affine_off   = ctypes.c_double(0.0)
    ok           = ctypes.c_bool(True)
    factor       = lib.bvn_unit_to_si_factor(unit,
                                              ctypes.byref(is_affine),
                                              ctypes.byref(affine_off),
                                              ctypes.byref(ok))
    if not ok.value:
        raise BovnarArgumentError("bvn_unit_to_si_factor: invalid unit")
    return SIConversion(float(factor), bool(is_affine.value),
                        float(affine_off.value))


def units_compatible(a: ValueUnit, b: ValueUnit) -> bool:
    """Return True when *a* and *b* have the same SI dimension vector."""
    return bool(get_library().bvn_units_compatible(a, b))


def unit_convert_factor(from_unit: ValueUnit,
                        to_unit: ValueUnit) -> UnitConversion:
    """
    Return the conversion factor to go from *from_unit* to *to_unit*.

    Raises BovnarArgumentError when the units are dimensionally incompatible.

    When requires_affine is True (e.g. Celsius ↔ Fahrenheit) a plain
    multiply by factor is not sufficient; use unit_to_si_factor for both
    units and perform the two-step affine conversion yourself.
    """
    lib            = get_library()
    ok             = ctypes.c_bool(True)
    requires_affine = ctypes.c_bool(False)
    factor         = lib.bvn_unit_convert_factor(from_unit, to_unit,
                                                  ctypes.byref(ok),
                                                  ctypes.byref(requires_affine))
    if not ok.value:
        raise BovnarArgumentError(
            "unit_convert_factor: incompatible or invalid units")
    return UnitConversion(float(factor), bool(requires_affine.value))


def unit_dimension_vector(unit: ValueUnit) -> list[int]:
    """
    Return the 7-element SI dimensional exponent vector [m, kg, s, A, K, mol, cd].

    Example – velocity (m/s): [1, 0, -1, 0, 0, 0, 0]
    Example – force (N = kg·m·s⁻²): [1, 1, -2, 0, 0, 0, 0]
    """
    lib  = get_library()
    dims = (ctypes.c_int32 * _SI_DIM_COUNT)()
    if not lib.bvn_unit_dimension_vector(unit, dims):
        raise BovnarArgumentError(
            "bvn_unit_dimension_vector: invalid or unrepresentable unit")
    return list(dims)


def unit_reduce(unit: ValueUnit) -> ReducedUnit:
    """
    Simplify *unit* to its canonical named SI unit where possible.

    Example – kg·m·s⁻² reduces to N (newton) with scale 1.0.

    OverflowError is raised when the accumulated scale factor exceeds float range.
    """
    lib      = get_library()
    scale    = ctypes.c_double(1.0)
    overflow = ctypes.c_bool(False)
    reduced  = lib.bvn_unit_reduce(unit,
                                    ctypes.byref(scale),
                                    ctypes.byref(overflow))
    if overflow.value:
        raise OverflowError("unit_reduce: scale factor out of float range")
    return ReducedUnit(reduced, float(scale.value))


def convert_value(value: float,
                  from_unit: ValueUnit,
                  to_unit: ValueUnit) -> float:
    """
    Convert *value* (expressed in *from_unit*) to *to_unit*.

    Handles both multiplicative and affine conversions correctly.
    Raises BovnarArgumentError for dimensionally incompatible units.
    """
    conv = unit_convert_factor(from_unit, to_unit)
    if not conv.requires_affine:
        return value * conv.factor

    src = unit_to_si_factor(from_unit)
    dst = unit_to_si_factor(to_unit)
    si_value = value * src.factor + src.affine_offset
    return (si_value - dst.affine_offset) / dst.factor


__all__ = [
    'SIConversion', 'UnitConversion', 'ReducedUnit', 'SI_DIM_NAMES',
    'unit_to_si_factor', 'units_compatible', 'unit_convert_factor',
    'unit_dimension_vector', 'unit_reduce', 'convert_value',
    'make_unit_compound',
]
