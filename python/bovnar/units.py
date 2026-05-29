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
from dataclasses import dataclass
from enum import IntFlag

from ._ffi import get_library
from .enums import BaseUnit, SIPrefix, IECPrefix, Exponent
from .structs import ValueUnit, ValueUnitPrefix, make_unit_compound
from .exceptions import BovnarArgumentError

_SI_DIM_COUNT = 7
SI_DIM_NAMES  = ('m', 'kg', 's', 'A', 'K', 'mol', 'cd')


class UnitFlags(IntFlag):
    NONE      = 0
    REDUCE    = 1 << 0
    ASCII_EXP = 1 << 1


@dataclass(frozen=True)
class SIConversion:
    """
    Result of unit_to_si_factor.

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


def unit_valid(unit: ValueUnit) -> bool:
    """
    Return True when *unit* is structurally valid.

    A zero-component ValueUnit (BVN_UNIT_NONE) is valid. A unit whose
    components reference out-of-range base-unit or prefix values is not.
    """
    return bool(get_library().bvn_unit_valid(unit))


def unit_prefix_factor(unit: ValueUnit) -> float:
    """
    Return the combined SI/IEC prefix scale factor for *unit*.

    Only prefix contributions are included; the intrinsic SI base-unit
    factor (e.g. gram = 1e-3 kg) is not.  For a compound unit the
    factors of individual components are multiplied together, taking
    exponents into account.

    Examples:
      unit_prefix_factor(parse_unit("k~m"))   → 1000.0
      unit_prefix_factor(parse_unit("m~s"))   → 1e-3
      unit_prefix_factor(parse_unit("k~m/s")) → 1000.0
      unit_prefix_factor(parse_unit("Gi~B"))  → 2**30
    """
    return float(get_library().bvn_unit_prefix_factor(unit))


def unit_prefix_exponent(unit: ValueUnit) -> int:
    """
    Return the effective prefix exponent for *unit*.

    For SI prefixes this is the base-10 exponent (e.g. 3 for kilo,
    -3 for milli).  For IEC prefixes this is the base-2 exponent
    (e.g. 10 for kibi, 30 for gibi).  For compound units the
    individual component exponents are summed after sign adjustment.
    Returns 0 when no prefix is applied.
    """
    return int(get_library().bvn_unit_prefix_exponent(unit))


def prefix_unit_valid(prefix: ValueUnitPrefix, base: BaseUnit | int) -> bool:
    """
    Return True when *prefix* is a valid prefix for *base*.

    IEC prefixes (kibi, mebi, …) are only valid on bit and byte base
    units; all other combinations are rejected.  SI prefix NONE is
    always valid.

    Example:
      prefix_unit_valid(ValueUnitPrefix.make_iec(IECPrefix.GIBI), BaseUnit.BYTE)  → True
      prefix_unit_valid(ValueUnitPrefix.make_iec(IECPrefix.GIBI), BaseUnit.METER) → False
    """
    return bool(get_library().bvn_prefix_unit_valid(prefix, int(base)))


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

    Raises BovnarArgumentError when the units are dimensionally incompatible
    or either unit is structurally invalid.

    The C function signals two distinct outcomes via the pair (ok, requires_affine):

      ok=True,  requires_affine=False → straightforward multiplicative factor
      ok=False, requires_affine=True  → affine conversion required (e.g. °C ↔ K);
                                        the returned factor is still valid but a plain
                                        multiply is not sufficient — use convert_value
      ok=False, requires_affine=False → units are dimensionally incompatible; raises

    When requires_affine is True, use convert_value which handles the two-step
    affine conversion automatically, or call unit_to_si_factor on both units and
    perform the conversion manually.
    """
    lib             = get_library()
    ok              = ctypes.c_bool(True)
    requires_affine = ctypes.c_bool(False)
    factor          = lib.bvn_unit_convert_factor(from_unit, to_unit,
                                                   ctypes.byref(ok),
                                                   ctypes.byref(requires_affine))
    if not ok.value and not requires_affine.value:
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


def unit_to_str_ex(unit: ValueUnit,
                   flags: UnitFlags | int = UnitFlags.NONE) -> str:
    """
    Serialise *unit* to its string representation with formatting options.

    flags may combine:
      UnitFlags.REDUCE    — reduce to a canonical named SI unit before serialising
                            (e.g. kg·m·s⁻² → N)
      UnitFlags.ASCII_EXP — use ^N exponent notation instead of Unicode
                            superscripts (e.g. m/s^2 instead of m/s²)

    Raises BovnarArgumentError when the output buffer overflows (unit has an
    unusually long representation — this should not occur in practice).
    """
    lib = get_library()
    buf = ctypes.create_string_buffer(256)
    n   = lib.bvn_unit_to_string_ex(unit, buf, 256, int(flags))
    if n < 0:
        raise BovnarArgumentError("unit_to_str_ex: output buffer overflow")
    return buf.raw[:n].decode('utf-8')


def exponent_to_int(exp: Exponent | int) -> int:
    """
    Convert a unit_exponent_t enum value to its signed integer exponent.

    Returns values in the range -9..9. Exponent.INVALID (0) maps to 0.

    Example:
      exponent_to_int(Exponent.LINEAR)     → 1
      exponent_to_int(Exponent.NEG_SQUARE) → -2
      exponent_to_int(Exponent.INVALID)    → 0
    """
    return int(get_library().bvn_exponent_to_int(int(exp)))


def int_to_exponent(n: int) -> Exponent:
    """
    Convert a signed integer exponent to the corresponding Exponent enum value.

    Valid inputs are -9..9 and 0.  Values outside this range return
    Exponent.INVALID.

    Example:
      int_to_exponent(1)  → Exponent.LINEAR
      int_to_exponent(-2) → Exponent.NEG_SQUARE
      int_to_exponent(0)  → Exponent.INVALID
    """
    result = get_library().bvn_int_to_exponent(ctypes.c_int32(n))
    return Exponent(result)


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
    'make_unit_compound',
]
