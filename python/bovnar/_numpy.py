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
"""numpy <-> bovnar array bridge.

    to_numpy(src)               bovnar array -> numpy ndarray (+ optional unit)
    to_pint_array(src)          bovnar array -> pint Quantity (ndarray + unit)
    from_numpy(writer, key, a)  ndarray -> bovnar (via a Writer)
    from_pint_array(w, key, q)  pint Quantity -> bovnar (via a Writer)
    array_to_bvnr(key, a)       ndarray -> bovnar bytes (convenience)

*src* for the read side is either a DomNode for an ARRAY (random-access, from
``dom_parse``) or the nested list a typed ``loads(..., typed=True)`` produces.
Both ``/``-rows and bracket nesting collapse to the same ndarray shape.

Design notes:
  * Strict by default — an array that mixes element dtypes or units raises
    (pass an explicit ``dtype=`` to coerce); ragged data raises unless
    ``dtype=object``.
  * The unit is a whole-array property (numpy has one dtype per array): it is
    returned alongside the data (``return_unit``/``to_pint_array``), never baked
    into element values.
  * Prefixes ride in the unit, not the data (see _pint_bridge), so a wrapped
    array is never silently rescaled.

numpy (and, for the pint helpers, pint) are optional dependencies imported
lazily; importing bovnar never requires them.
"""
from __future__ import annotations

from .enums import ValueTypeFamily as F
from .dom import DomNode, DomType
from .quantity import Quantity
from .structs import ValueUnit, make_type_spec, make_unit_none
from .exceptions import BovnarArgumentError

__all__ = ['to_numpy', 'to_pint_array', 'from_numpy', 'from_pint_array',
           'array_to_bvnr']


def _np():
    try:
        import numpy as np
    except ImportError as e:                       # pragma: no cover
        raise BovnarArgumentError(
            "numpy is required for the numpy bridge; `pip install numpy`") from e
    return np


# --------------------------------------------------------------------------- #
#  dtype <-> bovnar (family, width)
# --------------------------------------------------------------------------- #
_INT_DTYPE = {
    (int(F.UINT), 8): 'uint8',  (int(F.UINT), 16): 'uint16',
    (int(F.UINT), 32): 'uint32', (int(F.UINT), 64): 'uint64',
    (int(F.SINT), 8): 'int8',   (int(F.SINT), 16): 'int16',
    (int(F.SINT), 32): 'int32',  (int(F.SINT), 64): 'int64',
}
_FLOAT_DTYPE = {16: 'float16', 32: 'float32', 64: 'float64', 128: 'float128'}


def _family_width_dtype(family: int, width: int):
    """bovnar (family,width) -> numpy dtype name, or None to infer (PLAIN)."""
    family = int(family)
    width = int(width) or 64                    # bovnar default width is 64
    if family == int(F.BOOL):
        return 'bool'
    if family == int(F.UTF8):
        return 'str'
    if family in (int(F.UINT), int(F.SINT)):
        dt = _INT_DTYPE.get((family, width))
        if dt is None:
            raise BovnarArgumentError(
                f"integer width {width} has no numpy dtype (bigints need dtype=object)")
        return dt
    if family == int(F.FLOAT):
        dt = _FLOAT_DTYPE.get(width)
        if dt is None:
            raise BovnarArgumentError(f"float width {width} has no numpy dtype")
        return dt
    if family in (int(F.FLOAT_FIX), int(F.FLOAT_DEC)):
        return 'float64'                         # lossy, but the closest native dtype
    if family == int(F.PLAIN):
        return None                              # infer from the value / DOM node type
    raise BovnarArgumentError(f"value family {family} cannot map to a numpy dtype")


def _dtype_to_family_width(dt):
    """numpy dtype -> (ValueTypeFamily, width-in-bits)."""
    kind, bits = dt.kind, dt.itemsize * 8
    if kind == 'u':
        return F.UINT, bits
    if kind == 'i':
        return F.SINT, bits
    if kind == 'f':
        return F.FLOAT, bits
    if kind == 'b':
        return F.BOOL, 0
    if kind in ('U', 'S'):
        return F.UTF8, 0
    raise BovnarArgumentError(
        f"numpy dtype {dt!r} (kind {kind!r}) has no bovnar equivalent")


# --------------------------------------------------------------------------- #
#  read side: bovnar -> ndarray
# --------------------------------------------------------------------------- #
def _dom_rows(node: DomNode):
    """Children of an ARRAY DomNode as a nested list, undoing /-row flattening."""
    kids = [node[i] for i in range(len(node))]
    if not kids:
        return []
    dims = node.array_dims()
    if dims > 1:                                  # /-multirow: split flat children
        rl = len(kids) // dims
        return [kids[r * rl:(r + 1) * rl] for r in range(dims)]
    return kids


def _walk(obj, acc):
    """Recurse the source, recording leaf dtypes/units in *acc*, return values."""
    if isinstance(obj, (list, tuple)):
        return [_walk(x, acc) for x in obj]
    if isinstance(obj, DomNode) and obj.dom_type == DomType.ARRAY:
        return _walk(_dom_rows(obj), acc)
    return _leaf(obj, acc)


def _leaf(obj, acc):
    if obj is None:
        acc['null'] = True
        return None
    if isinstance(obj, Quantity):
        dt = _family_width_dtype(obj.vtype.family, obj.vtype.width)
        if dt:
            acc['dtypes'].add(dt)
        us = obj.unit_str()
        if us:
            acc['units'][us] = obj.unit
        val = obj.value                           # a typed null decodes to None
        if val is None:
            acc['null'] = True
        return val
    if isinstance(obj, DomNode):                  # a non-array leaf node
        dt = _family_width_dtype(obj.value_type.family, obj.value_type.width)
        if dt is None:                            # PLAIN: fall back to the node kind
            dt = {DomType.INT: 'int64', DomType.FLOAT: 'float64',
                  DomType.BOOL: 'bool', DomType.STRING: 'str'}.get(obj.dom_type)
        if dt:
            acc['dtypes'].add(dt)
        us = obj.unit_str
        if us:
            acc['units'][us] = obj.unit
        val = obj.to_python()
        if val is None:
            acc['null'] = True
        return val
    if isinstance(obj, bool):
        acc['dtypes'].add('bool'); return obj
    if isinstance(obj, int):
        acc['dtypes'].add('int64'); return obj
    if isinstance(obj, float):
        acc['dtypes'].add('float64'); return obj
    if isinstance(obj, str):
        acc['dtypes'].add('str'); return obj
    raise BovnarArgumentError(f"unsupported array element type {type(obj).__name__}")


def _extract(src, dtype):
    """Core: src -> (ndarray, ValueUnit, unit_str)."""
    np = _np()
    if not isinstance(src, (DomNode, list, tuple)):
        raise BovnarArgumentError(
            "to_numpy expects a DomNode array or the nested list from "
            "loads(..., typed=True)")
    acc = {'dtypes': set(), 'units': {}, 'null': False}
    values = _walk(src, acc)

    if dtype is None:
        dts = acc['dtypes']
        if not dts:
            resolved = None
        elif len(dts) == 1:
            resolved = next(iter(dts))
        else:
            raise BovnarArgumentError(
                f"array mixes element dtypes {sorted(dts)}; pass dtype= to coerce")
        if acc['null'] and resolved and resolved.startswith(('int', 'uint')):
            raise BovnarArgumentError(
                "null element(s) cannot fill an integer array; "
                "pass dtype=float or dtype=object")
    else:
        resolved = dtype

    np_dtype = None
    if resolved is not None:
        try:
            np_dtype = np.dtype(resolved)
        except TypeError as e:
            raise BovnarArgumentError(f"unknown dtype {resolved!r}: {e}") from e

    try:
        arr = np.array(values, dtype=np_dtype)
    except ValueError as e:                       # ragged / inhomogeneous
        raise BovnarArgumentError(
            f"cannot build a rectangular array (ragged data?): {e}; "
            f"pass dtype=object to keep it ragged") from e
    except TypeError as e:                         # e.g. None into an int array
        raise BovnarArgumentError(
            f"cannot build a {np_dtype} array from these values "
            f"(null element?): {e}") from e

    units = list(acc['units'])
    if len(units) > 1:
        raise BovnarArgumentError(
            f"array mixes units {units}; a numpy array carries a single unit")
    if units:
        return arr, acc['units'][units[0]], units[0]
    return arr, make_unit_none(), ''


def to_numpy(src, *, dtype=None, return_unit: bool = False):
    """Convert a bovnar array (DomNode or typed nested list) to an ndarray.

    With return_unit=True, returns (ndarray, unit_str) where unit_str is the
    bovnar canonical unit ('' if dimensionless).
    """
    arr, _vu, unit_str = _extract(src, dtype)
    return (arr, unit_str) if return_unit else arr


def to_pint_array(src, *, dtype=None, ureg=None):
    """Convert a bovnar array to a pint Quantity (ndarray data + its unit)."""
    arr, vu, _us = _extract(src, dtype)
    from ._pint_bridge import to_pint
    return to_pint(arr, vu, ureg=ureg)


# --------------------------------------------------------------------------- #
#  write side: ndarray -> bovnar
# --------------------------------------------------------------------------- #
def _resolve_unit_arg(unit):
    """unit arg (None|str|ValueUnit|pint Unit/Quantity) -> ValueUnit or None."""
    if unit is None:
        return None
    if isinstance(unit, ValueUnit):
        return unit
    if isinstance(unit, str):
        from . import parse_unit
        return parse_unit(unit)
    from ._pint_bridge import from_pint_unit       # assume a pint Unit/Quantity
    return from_pint_unit(unit)


def from_numpy(writer, key: str, arr, *, unit=None) -> None:
    """Write *arr* (an ndarray) as a bovnar array into *writer* under *key*.

    *unit* may be a bovnar unit string, a ValueUnit, or a pint Unit/Quantity.
    Units apply to numeric arrays only.
    """
    np = _np()
    arr = np.asarray(arr)
    if arr.ndim == 0:
        raise BovnarArgumentError(
            "from_numpy needs a 1-D+ array; write a 0-D value with the scalar API")
    family, width = _dtype_to_family_width(arr.dtype)
    rows = arr.tolist()
    vu = _resolve_unit_arg(unit)

    from . import write_array
    if family in (F.BOOL, F.UTF8):
        if vu is not None:
            raise BovnarArgumentError(
                f"a unit is not applicable to a {family.name.lower()} array")
        vt = make_type_spec(F.UTF8, 0, 0) if family == F.UTF8 else None
        write_array(writer, key, rows, vt=vt, vu=None)
    else:
        write_array(writer, key, rows, vt=make_type_spec(family, width, 0), vu=vu)


def from_pint_array(writer, key: str, qty) -> None:
    """Write a pint Quantity (magnitude + unit) as a bovnar array into *writer*."""
    from ._pint_bridge import from_pint
    mag, vu = from_pint(qty)
    from_numpy(writer, key, mag, unit=vu)


def array_to_bvnr(key: str, arr, *, unit=None, pretty: bool = True) -> bytes:
    """Serialize a single ndarray to bovnar bytes under *key* (convenience)."""
    from .writer import Writer
    from .exceptions import BovnarWriteError
    from .enums import ErrorCode
    cap = 4 * 1024 * 1024
    while True:
        try:
            with Writer.to_mem(cap=cap, pretty=pretty) as w:
                from_numpy(w, key, arr, unit=unit)
            return w.get_output()
        except BovnarWriteError as e:
            if e.code == ErrorCode.SINK_BUFFER_EXHAUSTED and cap < 256 * 1024 * 1024:
                cap *= 2
            else:
                raise
