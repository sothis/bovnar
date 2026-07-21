# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Janos Sonntag
"""A typed load/dump cycle must not quietly drop an array's unit or precision.

An array carries ONE annotation for all its elements, and dumps() derived it only
for datetimes and wide integers. Everything else fell through to a bare array, so

    <float:64,k~m> [1.5, 2.5]   ->   [1.5, 2.5]

turning 1.5 km into a dimensionless 1.5. Separately, each element was re-emitted
from its decoded scalar, i.e. through a C double, so a float_dec:128 element with
29 significant digits came back with 17. Quantity's own docstring promises that
"numeric precision, integer base, and physical units are all preserved across a
load/dump cycle".

An array that needs no annotation must still get none -- a plain [1, 2, 3] gaining
a redundant <uint:64> would be its own regression.
"""

import pytest

from conftest import needs_lib

import bovnar

DOC = (
    "#!bovnar 1.1\n"
    ".dist  = <float:64,k~m> [1.5, 2.5];\n"
    ".exact = <float_dec:128> [1.2345678901234567890123456789];\n"
    ".big   = <uint:128> [340282366920938463463374607431768211455];\n"
    ".small = <uint:8> [1, 2, 3];\n"
    ".hole  = <float:64,m> [1.0, , 3.0];\n"
    ".plain = [1, 2, 3];\n"
    ".mixed = [1, 2.5, 3];\n"
)


@needs_lib
def test_array_unit_survives_a_typed_roundtrip():
    back = bovnar.loads(bovnar.dumps(bovnar.loads(DOC, typed=True)), typed=True)
    assert back["dist"][0].unit_str() == "k~m"
    assert back["dist"][0].value == 1.5
    assert back["hole"][0].unit_str() == "m"


@needs_lib
def test_array_element_precision_survives():
    back = bovnar.loads(bovnar.dumps(bovnar.loads(DOC, typed=True)), typed=True)
    assert str(back["exact"][0].decimal()) == "1.2345678901234567890123456789"
    assert back["big"][0].value == 340282366920938463463374607431768211455


@needs_lib
def test_declared_width_survives():
    back = bovnar.loads(bovnar.dumps(bovnar.loads(DOC, typed=True)), typed=True)
    assert int(back["small"][0].vtype.width) == 8
    assert int(back["big"][0].vtype.width) == 128


@needs_lib
def test_null_hole_survives():
    back = bovnar.loads(bovnar.dumps(bovnar.loads(DOC, typed=True)), typed=True)
    assert [None if q is None else q.value for q in back["hole"]] == [1.0, None, 3.0]


@needs_lib
def test_arrays_needing_no_annotation_get_none():
    """A redundant annotation would be its own regression."""
    out = bovnar.dumps(bovnar.loads(DOC, typed=True)).decode()
    for line in out.splitlines():
        if line.startswith(".plain") or line.startswith(".mixed"):
            assert "<" not in line, line
