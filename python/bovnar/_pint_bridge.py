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
"""bovnar <-> pint conversion, built on the verified tables in _pint_units.

    to_pint(value, vu)      bovnar ValueUnit + magnitude -> pint Quantity
    to_pint_unit(vu)        bovnar ValueUnit             -> pint Unit
    from_pint(qty)          pint Quantity                -> (magnitude, ValueUnit)
    from_pint_unit(unit)    pint Unit / str              -> ValueUnit

Prefixes are preserved in the pint unit *name* (bovnar ``k~m`` -> pint
``kilometer``), never folded into the magnitude, so a wrapped numpy array is
left untouched.  Offset/affine temperature units (degC, degF, …) may not carry
a prefix or exponent: pint forbids it and bovnar never emits it, so such a
combination raises BovnarArgumentError rather than a cryptic pint error.

pint is an optional dependency; it is imported lazily, only when a registry is
actually needed.  Pass your own ``ureg`` (e.g. from ``build_registry``) to share
one registry across calls; otherwise a module-level default is built on first use.
"""
from __future__ import annotations

import weakref

from .enums import BaseUnit, SIPrefix, IECPrefix, Exponent, PrefixSystem
from .structs import ValueUnit, make_unit_dimensionless, make_unit_compound
from .exceptions import BovnarArgumentError
from ._pint_units import BASE_UNIT_TO_PINT, CURRENCY_TOKENS, build_registry

__all__ = ['to_pint', 'to_pint_unit', 'from_pint', 'from_pint_unit']

# bovnar prefix -> pint prefix name (pint accepts <prefix_name><unit_name>)
_SI_PREFIX_NAME = {
    SIPrefix.QUECTO: 'quecto', SIPrefix.RONTO: 'ronto', SIPrefix.YOCTO: 'yocto',
    SIPrefix.ZEPTO:  'zepto',  SIPrefix.ATTO:  'atto',  SIPrefix.FEMTO: 'femto',
    SIPrefix.PICO:   'pico',   SIPrefix.NANO:  'nano',  SIPrefix.MICRO: 'micro',
    SIPrefix.MILLI:  'milli',  SIPrefix.CENTI: 'centi', SIPrefix.DECI:  'deci',
    SIPrefix.DECA:   'deca',   SIPrefix.HECTO: 'hecto', SIPrefix.KILO:  'kilo',
    SIPrefix.MEGA:   'mega',   SIPrefix.GIGA:  'giga',  SIPrefix.TERA:  'tera',
    SIPrefix.PETA:   'peta',   SIPrefix.EXA:   'exa',   SIPrefix.ZETTA: 'zetta',
    SIPrefix.YOTTA:  'yotta',  SIPrefix.RONNA: 'ronna', SIPrefix.QUETTA: 'quetta',
}
_IEC_PREFIX_NAME = {
    IECPrefix.KIBI: 'kibi', IECPrefix.MEBI: 'mebi', IECPrefix.GIBI: 'gibi',
    IECPrefix.TEBI: 'tebi', IECPrefix.PEBI: 'pebi', IECPrefix.EXBI: 'exbi',
    IECPrefix.ZEBI: 'zebi', IECPrefix.YOBI: 'yobi', IECPrefix.ROBI: 'robi',
    IECPrefix.QUEBI: 'quebi',
}
# affine temperature scales: pint cannot prefix or exponentiate these
_AFFINE_BASES = frozenset(int(b) for b in (
    BaseUnit.CELSIUS, BaseUnit.FAHRENHEIT, BaseUnit.REAUMUR,
    BaseUnit.DELISLE, BaseUnit.NEWTON_TEMP, BaseUnit.ROMER))

_default_reg = None
# Keyed on the registry object itself (not id(reg)): a WeakKeyDictionary auto-
# evicts when a registry is garbage-collected, so a transient registry can never
# leak its map, nor have a recycled id() alias a stale map onto an unrelated new
# registry (id() is reused after GC). The cached map holds only str->int, so it
# keeps no strong reference back to the registry.
_reverse_cache: "weakref.WeakKeyDictionary" = weakref.WeakKeyDictionary()


def _registry(ureg):
    global _default_reg
    if ureg is not None:
        return ureg
    if _default_reg is None:
        _default_reg = build_registry()
    return _default_reg


# --------------------------------------------------------------------------- #
#  bovnar -> pint
# --------------------------------------------------------------------------- #
def _component_token(comp) -> tuple[str, int]:
    """Return (pint_name_with_prefix, exponent) for one bovnar unit component."""
    base = int(comp.base)
    tok = BASE_UNIT_TO_PINT.get(base) or CURRENCY_TOKENS.get(base)
    if tok is None:
        raise BovnarArgumentError(f"no pint mapping for bovnar base unit code {base}")
    if int(comp.prefix.system) == int(PrefixSystem.IEC):
        pfx = _IEC_PREFIX_NAME.get(IECPrefix(comp.prefix.id.iec), '')
    else:
        pfx = _SI_PREFIX_NAME.get(SIPrefix(comp.prefix.id.si), '')
    exp = int(comp.exponent)
    if base in _AFFINE_BASES and (pfx or exp != 1):
        raise BovnarArgumentError(
            f"affine unit {tok!r} cannot carry a prefix or exponent")
    return pfx + tok, exp


def _unit_string(vu) -> str:
    """Build a pint-parseable unit string for a bovnar ValueUnit ('' = dimensionless)."""
    # An affine base inside a PRODUCT is refused. bovnar allows it at exponent 1
    # (see doc/2 §12.4, "An affine unit has an SI value only alone, at exponent
    # 1") and applies the offset, so bvn_unit_convert_value(20, °C/h, K/h) is
    # 983360. pint forbids offset units in compounds by construction and
    # silently rewrites degC to delta_degree_Celsius, which drops the offset
    # and yields 20 -- a different number, with no diagnostic, and from_pint()
    # then cannot map delta_degree_Celsius back at all. Refusing beats
    # answering differently from the reference implementation; the standalone
    # case (20 °C -> 293.15 K) is unaffected and still agrees exactly.
    live = [i for i in range(int(vu.num_components))
            if int(vu.components[i].base) != int(BaseUnit.NONE)]
    if len(live) > 1:
        for i in live:
            if int(vu.components[i].base) in _AFFINE_BASES:
                tok = (BASE_UNIT_TO_PINT.get(int(vu.components[i].base))
                       or str(int(vu.components[i].base)))
                raise BovnarArgumentError(
                    f"affine unit {tok!r} cannot appear in a compound unit: "
                    "pint has no offset-unit product, and dropping the offset "
                    "would disagree with bvn_unit_convert_value")
    parts = []
    for i in live:
        name, exp = _component_token(vu.components[i])
        parts.append(name if exp == 1 else f"{name}**{exp}")
    return " * ".join(parts)


def to_pint_unit(vu: ValueUnit, *, ureg=None):
    """Convert a bovnar ValueUnit to a pint Unit (dimensionless if no real unit)."""
    reg = _registry(ureg)
    s = _unit_string(vu)
    return reg.Unit(s) if s else reg.dimensionless


def to_pint(value, vu: ValueUnit, *, ureg=None):
    """Wrap *value* (scalar or ndarray) and a bovnar ValueUnit in a pint Quantity.

    The prefix lives in the unit, so *value* is returned unscaled.
    """
    reg = _registry(ureg)
    return reg.Quantity(value, _unit_string(vu))


# --------------------------------------------------------------------------- #
#  pint -> bovnar
# --------------------------------------------------------------------------- #
def _reverse_map(reg) -> dict:
    """canonical-pint-name -> bovnar base-unit code (cached per registry)."""
    m = _reverse_cache.get(reg)
    if m is not None:
        return m
    m = {}
    for code, tok in list(BASE_UNIT_TO_PINT.items()) + list(CURRENCY_TOKENS.items()):
        m[tok] = code                      # the token we chose
        try:
            m[reg.get_name(tok)] = code     # pint's canonical spelling (e.g. degree_Celsius)
        except Exception:
            pass
    _reverse_cache[reg] = m
    return m


# pint prefix name -> (system, prefix-enum-int); longest names first for safe stripping
_PREFIX_LOOKUP = sorted(
    [(n, (PrefixSystem.SI, int(p))) for p, n in _SI_PREFIX_NAME.items()] +
    [(n, (PrefixSystem.IEC, int(p))) for p, n in _IEC_PREFIX_NAME.items()],
    key=lambda kv: -len(kv[0]))


def _resolve_name(name: str, revmap: dict):
    """pint atomic unit name -> (base_code, system, prefix_int)."""
    if name in revmap:                      # full name first (so 'decibel' != deci+bel)
        return revmap[name], PrefixSystem.SI, int(SIPrefix.NONE)
    for pfxname, (sysid, pfxint) in _PREFIX_LOOKUP:
        if name.startswith(pfxname):
            rest = name[len(pfxname):]
            if rest in revmap:
                return revmap[rest], sysid, pfxint
    raise BovnarArgumentError(f"no bovnar unit for pint unit {name!r}")


def from_pint_unit(unit, *, ureg=None, validate: bool = True) -> ValueUnit:
    """Convert a pint Unit/Quantity/str to a bovnar ValueUnit."""
    reg = _registry(ureg)
    if hasattr(unit, 'units'):              # a Quantity
        unit = unit.units
    if isinstance(unit, str):
        unit = reg.Unit(unit)
    if not hasattr(unit, '_units'):         # not a pint Unit/Quantity/str
        raise BovnarArgumentError(
            f"expected a pint Unit/Quantity or unit string, "
            f"not {type(unit).__name__}")
    items = list(unit._units.items())       # {canonical_name: exponent}
    if not items:
        return make_unit_dimensionless()
    if len(items) > 8:
        raise BovnarArgumentError(
            f"pint unit has {len(items)} components; bovnar allows at most 8")
    revmap = _reverse_map(reg)
    comps = []
    for name, exp in items:
        if exp != int(exp):
            raise BovnarArgumentError(
                f"non-integer exponent {exp} on {name!r} is not representable in bovnar")
        exp = int(exp)
        if exp == 0 or abs(exp) > 9:
            raise BovnarArgumentError(
                f"exponent {exp} on {name!r} is outside bovnar's range [-9, 9]")
        code, sysid, pfxint = _resolve_name(name, revmap)
        d = {'base': BaseUnit(code), 'exp': Exponent(exp)}
        if sysid == PrefixSystem.IEC:
            d['iec_prefix'] = IECPrefix(pfxint)
        elif pfxint != int(SIPrefix.NONE):
            d['si_prefix'] = SIPrefix(pfxint)
        comps.append(d)
    vu = make_unit_compound(comps)
    if validate:
        from .units import unit_valid
        if not unit_valid(vu):
            raise BovnarArgumentError(
                f"pint unit {unit} maps to an invalid bovnar unit "
                f"(e.g. a prefix not permitted on that base)")
    return vu


def from_pint(qty, *, ureg=None, validate: bool = True):
    """Split a pint Quantity into (magnitude, bovnar ValueUnit).

    *magnitude* is returned unchanged (scalar or ndarray); the unit's prefix is
    represented inside the ValueUnit, not folded into the magnitude.
    """
    mag = qty.magnitude if hasattr(qty, 'magnitude') else qty
    return mag, from_pint_unit(qty, ureg=ureg, validate=validate)
