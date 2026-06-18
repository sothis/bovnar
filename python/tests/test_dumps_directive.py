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

import bovnar
from bovnar import Quantity


def test_uses_spec_1_1_tolerates_non_spec_vtype():
    """The Quantity constructor does not enforce that vtype is a ValueTypeSpec.
    A Quantity carrying a bare-string vtype must make the directive-detection
    pass report 'not spec 1.1' rather than raise AttributeError on .family."""
    q = Quantity("1", "not-a-valuetypespec")
    assert bovnar._uses_spec_1_1(q) is False
    # The recursive container paths must stay safe too.
    assert bovnar._uses_spec_1_1({"k": q}) is False
    assert bovnar._uses_spec_1_1([q]) is False


def test_uses_spec_1_1_tolerates_none_vtype():
    """A None vtype must also degrade to 'not a datetime', not crash."""
    assert bovnar._uses_spec_1_1(Quantity("1", None)) is False


def test_uses_spec_1_1_false_for_plain_values():
    """Plain Python values never require the directive."""
    assert bovnar._uses_spec_1_1({"a": 1, "b": [1, 2, 3], "c": "x"}) is False
