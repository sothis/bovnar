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

"""Pure-Python regression tests for dumps()'s spec-1.1 directive detection
(_uses_spec_1_1). These exercise only Python attribute/type logic and need no
native library."""

import pytest

import bovnar
from bovnar import Quantity
from bovnar.exceptions import BovnarArgumentError


def test_quantity_rejects_non_spec_vtype():
    """The Quantity constructor VALIDATES vtype (since 1.2).

    It used to accept anything and store it, so Quantity("1", "not-a-vtype")
    constructed happily and every consumer of .vtype had to defend itself
    against a value that could never be right -- this file's own predecessor
    tested exactly that defence. Rejecting at construction is strictly better:
    the traceback names the caller that built the bad object rather than some
    reader of it three frames later.
    """
    with pytest.raises(BovnarArgumentError, match="ValueTypeSpec"):
        Quantity("1", "not-a-valuetypespec")


def test_quantity_rejects_none_vtype():
    """None is not a ValueTypeSpec either."""
    with pytest.raises(BovnarArgumentError, match="ValueTypeSpec"):
        Quantity("1", None)


def test_uses_spec_1_1_stays_safe_on_containers():
    """The directive-detection pass still walks containers without raising, on
    Quantities that are now necessarily well-formed."""
    vt = bovnar.make_type_spec(bovnar.ValueTypeFamily.UINT, 64, 0)
    q = Quantity("1", vt)
    assert bovnar._uses_spec_1_1(q) is False
    assert bovnar._uses_spec_1_1({"k": q}) is False
    assert bovnar._uses_spec_1_1([q]) is False


def test_uses_spec_1_1_false_for_plain_values():
    """Plain Python values never require the directive."""
    assert bovnar._uses_spec_1_1({"a": 1, "b": [1, 2, 3], "c": "x"}) is False


class TestUnitOnAStringRoundTrips:
    """`.s = "x" m;` is legal, and dumps() emitted it in a form the reader refused.

    The format lets a string carry a unit -- the grammar says so outright
    (`scalar-with-unit = (number | string), ws, inline-unit`) -- but only as an
    INLINE suffix. `<utf8,m>` is error_illegal_value_type, because a unit is not
    one of the parameters that family takes.

    dumps() put it in the annotation anyway, producing `<utf8:m> "x"`, so
    loads(typed=True) -> dumps -> loads failed on a document the library had
    just read. Only utf8 was affected; every numeric family, including a quoted
    non-decimal number carrying a unit, was already right. The C writer emits
    `<utf8> "x" m;` and always round-tripped, which is what this now matches.
    """

    def test_string_with_a_unit_survives_a_round_trip(self):
        import bovnar
        doc = bovnar.loads(b'.s = "x" m;', typed=True)
        out = bovnar.dumps(doc)
        assert b'<utf8:m>' not in out
        assert bovnar.loads(out, typed=True) == doc

    def test_the_emitted_form_is_the_one_the_c_writer_uses(self):
        import bovnar
        out = bovnar.dumps(bovnar.loads(b'.s = "x" m;', typed=True))
        assert out.decode().strip() == '.s = <utf8> "x" m;'

    def test_the_unit_is_still_carried(self):
        import bovnar
        q = bovnar.loads(b'.s = "x" m;', typed=True)["s"]
        assert q.unit_str == "m"
        assert q.raw == "x"

    def test_numeric_families_are_unchanged(self):
        import bovnar
        for src, want in ((b'.n = 5 m;', '.n = <uint:64,m> 5;'),
                          (b'.f = 1.5 m;', '.f = <float:64,m> 1.5;'),
                          (b'.h = <uint:8,_16,m> "FF";',
                           '.h = <uint:8,_16,m> "FF";')):
            doc = bovnar.loads(src, typed=True)
            out = bovnar.dumps(doc)
            assert out.decode().strip() == want
            assert bovnar.loads(out, typed=True) == doc
