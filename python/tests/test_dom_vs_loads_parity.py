# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Janos Sonntag
"""The DOM tier and the streaming tier must produce the same Python value.

Two independent paths to the same data: DomDoc.parse(...).to_dict() walks the
materialised C DOM, bovnar.loads(...) consumes the streaming event pipe. They
disagreed on /-separated dimension rows.

The C DOM stores those FLAT and records the row count separately -- for
"[1,2,3]/[4,5,6]" it holds six elements with array_dims() == 2 -- and to_python()
ignored the row count. So two DIFFERENT documents produced the same Python value:

    .a = [1,2,3]/[4,5,6];   ->  [1, 2, 3, 4, 5, 6]
    .a = [1,2,3,4,5,6];     ->  [1, 2, 3, 4, 5, 6]

while the streaming path correctly gives [[1,2,3],[4,5,6]]. README calls
"[1,2,3]/[4,5,6] is a native 2D structure" a headline feature, so losing the
shape at one tier is a real gap. Row sizes are equal by construction -- an uneven
one is error_array_row_size_mismatch -- so the reshape is exact.
"""
import pytest

from conftest import needs_lib

import bovnar
from bovnar.dom import DomDoc


def _norm(o):
    """NaN != NaN, and 1.0 == 1 only by luck."""
    if isinstance(o, dict):
        return {k: _norm(v) for k, v in o.items()}
    if isinstance(o, list):
        return [_norm(x) for x in o]
    if isinstance(o, float):
        if o != o:
            return "NAN"
        if o.is_integer():
            return int(o)
    return o


DIMENSION_CASES = {
    ".a = [1,2,3]/[4,5,6];\n":      [[1, 2, 3], [4, 5, 6]],
    ".a = [1,2]/[3,4]/[5,6];\n":    [[1, 2], [3, 4], [5, 6]],
    ".a = [1]/[2];\n":              [[1], [2]],
    ".a = [[1,2]/[3,4]];\n":        [[[1, 2], [3, 4]]],
    ".a = [1,2,3,4,5,6];\n":        [1, 2, 3, 4, 5, 6],   # must stay flat
    ".a = [[1,2],[3,4]];\n":        [[1, 2], [3, 4]],
}


@needs_lib
@pytest.mark.parametrize("body,expected", list(DIMENSION_CASES.items()))
def test_dimension_rows_keep_their_shape(body, expected):
    doc = ("#!bovnar 1.1\n" + body).encode()
    assert _norm(DomDoc.parse(doc).to_dict()["a"]) == expected
    assert _norm(bovnar.loads(doc)["a"]) == expected


RICH = [
    ".a = <float:64,k~m/h> 88.5;\n.b = 9.81 m/s;\n.c = <uint:64,Gi~B> 4;\n",
    ".a = <float_dec:64,$USD> 19.99;\n.b = <uint:64,$BTC> 547820000;\n",
    ".a = <datetime:64,tai> 2016-12-31T23:59:60.5Z;\n.b = 2026-06-15;\n",
    ".a = 1;\n.r = &.a;\n.m = [[1,2],[3,4]];\n.i = &.m[0][1];\n",
    ".a = <uint:128> 340282366920938463463374607431768211455;\n",
    ".a = <float_dec:128> 1.2345678901234567890123456789;\n",
    '.a = <uint:32,_16> "deadbeef";\n.b = <float:64,_16> "1.8p3";\n',
    ".a = nan;\n.b = inf;\n.c = ninf;\n",
    '.a = "tab\\there";\n.b = "caf\\u{e9}";\n.c = "\\x41";\n',
    ".a = {.b = [{.c = 1;}, {.c = 2;}];};\n",
    ".a = [1, , 3];\n.b = ;\n",
]


@needs_lib
@pytest.mark.parametrize("body", RICH)
def test_the_two_tiers_agree_on_rich_documents(body):
    doc = ("#!bovnar 1.1\n" + body).encode()
    assert _norm(DomDoc.parse(doc).to_dict()) == _norm(bovnar.loads(doc))
