# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Janos Sonntag
"""A <float:W,_16> carrier is a hex-float literal, not a decimal one.

bovnar.loads() decoded it with float(), so "1.8" came back as decimal 1.8 where
the value is 1.5, and the binary-exponent form "1.0p+0" came back as a *string*
because float() cannot parse it. Quantity.decimal() already honoured the base, so
the two Python paths disagreed with each other as well as with the C reader.

The base field means something different per family: for float_fix it carries the
Q parameter and for float_dec it is unused -- both are always decimal, which is
what bvn_effective_base() decides in C. Reading Q=16 as base 16 turned 1.5 into
1.3125, so those two families are pinned here as well.
"""

import pytest

from conftest import needs_lib

import bovnar

DOC = """#!bovnar 1.1
.a = <float:64,_16> "1.8";
.b = <float:64,_16> "10.4";
.c = <float:64,_16> "1.0p+0";
.d = <float:64,_16> "-1.8";
.e = <float:64> 1.8;
.f = <float_fix:32,q16> 1.5;
.g = <float_dec:64> 2.75;
"""

EXPECT = {"a": 1.5, "b": 16.25, "c": 1.0, "d": -1.5,
          "e": 1.8, "f": 1.5, "g": 2.75}


@needs_lib
def test_hex_float_carriers_decode_in_base_16():
    got = bovnar.loads(DOC)
    for k, want in EXPECT.items():
        assert isinstance(got[k], float), f"{k} decoded as {type(got[k])}"
        assert abs(got[k] - want) < 1e-12, f"{k}: got {got[k]}, want {want}"


@needs_lib
def test_loads_agrees_with_quantity_decimal():
    """The two Python decode paths must not disagree."""
    typed = bovnar.loads(DOC, typed=True)
    plain = bovnar.loads(DOC)
    for k in EXPECT:
        assert abs(float(typed[k].decimal()) - plain[k]) < 1e-12, k
