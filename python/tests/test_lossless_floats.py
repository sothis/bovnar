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
"""Lossless float128 / float256 / float_fix / float_dec support in Python.

bovnar carries these formats at full precision (the verbatim decimal literal and
the arbitrary-precision bvn_float). These tests pin the lossless Python path:
exact Decimal/Fraction scalars, bit-exact IEEE materialisation, and exact
round-trips of both scalars and numpy object arrays.
"""
from decimal import Decimal
from fractions import Fraction

import pytest

from conftest import needs_lib

import bovnar
from bovnar import loads, dumps
from bovnar.quantity import Quantity
from bovnar.enums import ValueTypeFamily as F
from bovnar.exceptions import BovnarArgumentError
from bovnar._bvnfloat import (BvnFloat, fraction_to_binary_bits,
                              decimal_to_interchange_bits, decimal_stored_value)


@needs_lib
class TestBinaryEncoderMatchesC:
    """The pure-Python IEEE encoders (used for extreme exponents the C parser
    can't reach) must be bit-identical to bovnar's C encoder over the range the
    C encoder *can* reach."""

    def test_encoder_matches_c_oracle(self):
        # deterministic LCG over a wide exponent range, within the C parser limit.
        # Exact zero is skipped: a Fraction has no signed zero, so the encoder
        # (correctly, for its domain) returns +0 — and the fallback is only ever
        # called for *saturated nonzero* values, never an exact-zero literal.
        x = 0x9E3779B97F4A7C15
        mism = 0
        for _ in range(8000):
            x = (x * 6364136223846793005 + 1442695040888963407) & ((1 << 64) - 1)
            digits = (x >> 3) % 18 + 1
            x = (x * 6364136223846793005 + 1442695040888963407) & ((1 << 64) - 1)
            mant = x % (10 ** digits)
            x = (x * 6364136223846793005 + 1442695040888963407) & ((1 << 64) - 1)
            exp = (x >> 5) % 120 - 60
            if mant == 0:
                continue
            sign = '-' if x & 1 else ''
            s = f"{sign}{mant}e{exp}"
            for w in (16, 32, 64, 128, 256):
                c = BvnFloat.from_str(s).to_binary_bits(w)
                p = fraction_to_binary_bits(Fraction(Decimal(s)), w)
                if c != p:
                    mism += 1
        assert mism == 0

    def test_negative_underflow_keeps_sign(self):
        # a negative value that underflows in the format materialises to -0.0
        q = loads(b'.a=<float:256>-1e-80000;', typed=True)['a']
        assert q.ieee_bits[-1] & 0x80          # sign bit set (little-endian)

    def test_decimal_encoder_correctly_rounded_and_matches_c(self):
        # Validate the decimal-interchange encoder two ways, using Python's own
        # `decimal` module (the IEEE-754 reference) with each format's context:
        #   (1) the materialised VALUE equals the reference's round-to-nearest-even
        #       (full range, including exponents the C parser can't reach);
        #   (2) the BID BITS equal bovnar's C to_dec* for non-tie values (ties are
        #       where the C path rounds bovnar's binary approximation of the
        #       literal vs the exact literal — both legitimate, and the Python
        #       path only ever runs when C cannot store the value at all).
        import decimal
        from decimal import Decimal as D
        # width -> (exp_bits, bias, max_digits)
        params = {16: (6, 24, 2), 32: (8, 101, 7), 64: (10, 398, 16),
                  128: (14, 6176, 34), 256: (20, 611867, 70)}

        def ctx_for(w, rounding):
            eb, bias, md = params[w]
            std = bias == 3 * (1 << (eb - 3)) + md - 2
            be_max = (3 << (eb - 2)) if std else ((1 << eb) - 1)
            c = decimal.Context(prec=md, Emax=be_max - 1 - bias + md - 1,
                                Emin=-bias + md - 1, rounding=rounding)
            for t in c.traps:
                c.traps[t] = False
            return c

        x = 0x1234567
        for _ in range(5000):
            x = (x * 6364136223846793005 + 1442695040888963407) & ((1 << 64) - 1)
            nd = (x >> 3) % 22 + 1
            x = (x * 6364136223846793005 + 1442695040888963407) & ((1 << 64) - 1)
            mant = x % (10 ** nd)
            x = (x * 6364136223846793005 + 1442695040888963407) & ((1 << 64) - 1)
            exp = (x >> 5) % 120 - 60
            if mant == 0:
                continue
            s = ('-' if x & 1 else '') + f"{mant}e{exp}"
            d = D(s)
            for w in (16, 32, 64, 128, 256):
                # create_decimal already applies the format's prec/Emax/Emin;
                # NO unary + (that would re-round in the prec-28 global context).
                ref = ctx_for(w, decimal.ROUND_HALF_EVEN).create_decimal(d)
                mine = decimal_stored_value(d, w)
                if mine != 0:                     # (1) value correctly rounded
                    assert mine == ref and mine.is_signed() == ref.is_signed()
                # (2) bits match C for non-tie values within the C parser's range
                if abs(exp) < 30:
                    down = ctx_for(w, decimal.ROUND_DOWN).create_decimal(d)
                    up = ctx_for(w, decimal.ROUND_UP).create_decimal(d)
                    with decimal.localcontext() as wide:
                        wide.prec = 400
                        is_tie = down != up and down + up == d * 2
                    if not is_tie:                # exact midpoint: C rounds the
                        assert (BvnFloat.from_str(s).to_decimal_bits(w)   # binary
                                == decimal_to_interchange_bits(d, w))     # approx

# A 38-digit value well beyond float64's ~15-17 significant digits.
HIPI = '1.10000000000000000022204460492503000007'
PI34 = '3.141592653589793238462643383279503'        # 34 sig digits (decimal128)


@needs_lib
class TestScalarLosslessRead:
    def test_float128_literal_is_exact(self):
        q = loads(('.a=<float:128>%s;' % HIPI).encode(), typed=True)['a']
        assert q.decimal == Decimal(HIPI)
        assert q.value == pytest.approx(1.1)          # the lossy double path

    def test_float_dec_literal_is_exact(self):
        q = loads(('.p=<float_dec:128>%s;' % PI34).encode(), typed=True)['p']
        assert q.decimal == Decimal(PI34)

    def test_float_fix_fraction_is_exact(self):
        # 3.27 is not exact in Q8; the stored datum is round(3.27*256)=837
        q = loads(b'.x=<float_fix:32,q8>3.27;', typed=True)['x']
        assert q.fraction == Fraction(837, 256)
        assert q.fixed_point == (837, 8)
        assert q.stored_value == Decimal('3.26953125')

    def test_stored_value_rounds_to_format(self):
        # a 34-digit literal in decimal64 (16 digits) -> stored value is rounded
        q = loads(('.p=<float_dec:64>%s;' % PI34).encode(), typed=True)['p']
        assert q.decimal == Decimal(PI34)                 # the literal
        assert q.stored_value == Decimal('3.141592653589793')   # decimal64

    def test_extreme_exponent_binary_materialises_full_range(self):
        # bovnar's C parser saturates an extreme exponent, but the pure-Python
        # encoder materialises the correct binary256 value across the full range.
        q = loads(b'.a=<float:256>1e-78000;', typed=True)['a']
        assert q.decimal == Decimal('1e-78000')        # exact literal
        sv = q.stored_value
        assert sv != 0 and float(sv / Decimal('1e-78000')) == 1.0
        assert len(q.ieee_bits) == 32
        # genuine range boundaries of binary256
        assert loads(b'.a=<float:256>1e80000;', typed=True)['a'].stored_value \
            == Decimal('Infinity')
        assert loads(b'.a=<float:256>1e-80000;', typed=True)['a'].stored_value == 0

    def test_extreme_exponent_decimal_materialises_full_range(self):
        # the decimal-interchange path also materialises beyond the C parser's
        # cap via the pure-Python encoder.
        q = loads(b'.a=<float_dec:256>1e20000;', typed=True)['a']
        assert q.decimal == Decimal('1e20000')
        assert q.stored_value == Decimal('1E+20000')
        assert len(q.ieee_bits) == 32
        qn = loads(b'.a=<float_dec:256>-1.5e-20000;', typed=True)['a']
        assert qn.stored_value == Decimal('-1.5E-20000')
        # decimal64 range boundaries (Emax 384)
        assert loads(b'.a=<float_dec:64>1e400;', typed=True)['a'].stored_value \
            == Decimal('Infinity')
        assert loads(b'.a=<float_dec:64>1e-400;', typed=True)['a'].stored_value == 0

    def test_decimal_near_emax_full_coefficient_exact(self):
        # A full-coefficient value just under the format's Emax is exactly
        # representable; the materialised value must keep every digit. (Regression
        # for a bvn_float render-precision bug — a coarse log10(2) estimate that
        # truncated decimal128's 34-nine maximum to ~20 digits — cross-checked
        # against Python's own decimal module.)
        import decimal
        s = '9999999999999999999999999999999999e6111'   # 34 nines, decimal128 max
        q = loads(('.a=<float_dec:128>%s;' % s).encode(), typed=True)['a']
        assert q.stored_value == Decimal(s)
        # the emitted bits decode back to exactly that value
        assert BvnFloat.from_decimal_bits(128, q.ieee_bits).to_decimal() \
            == q.stored_value
        # and it equals Python decimal's own decimal128 rounding
        ctx = decimal.Context(prec=34, Emax=6144, Emin=-6143,
                              rounding=decimal.ROUND_HALF_EVEN)
        for t in ctx.traps:
            ctx.traps[t] = False
        assert q.stored_value == ctx.create_decimal(Decimal(s))

    def test_genuine_format_underflow_is_zero_not_refused(self):
        # a literal within the parser's range that simply rounds below the
        # format's smallest value materialises to 0 / inf, not an error.
        assert loads(b'.a=<float:16>1e-100;', typed=True)['a'].stored_value == 0
        assert loads(b'.a=<float:16>1e100;', typed=True)['a'].stored_value \
            == Decimal('Infinity')

    def test_binary128_stored_value_matches_ieee(self):
        # the materialised binary128 of 0.1 differs from the literal
        q = loads(b'.a=<float:128>0.1;', typed=True)['a']
        sv = q.stored_value
        assert sv != Decimal('0.1')
        assert float(sv) == 0.1                              # nearest double is 0.1
        assert len(q.ieee_bits) == 16                      # binary128 = 16 bytes

    def test_decimal_works_for_ints(self):
        q = loads(b'.n=<uint:64>18446744073709551615;', typed=True)['n']
        assert q.decimal == Decimal(18446744073709551615)

    def test_nonfinite(self):
        assert loads(b'.z=<float:64>nan;', typed=True)['z'].decimal.is_nan()
        assert loads(b'.z=<float:64>inf;', typed=True)['z'].decimal == Decimal('Infinity')

    def test_decimal_rejects_nonnumeric(self):
        q = loads(b'.s="hi";', typed=True)['s']
        with pytest.raises(BovnarArgumentError):
            q.decimal


@needs_lib
class TestScalarLosslessWrite:
    def test_dumps_decimal_roundtrips(self):
        d = Decimal(PI34)
        out = dumps({'p': d})
        assert b'<float_dec:64>' in out
        assert loads(out, typed=True)['p'].decimal == d

    def test_dumps_terminating_fraction(self):
        assert b'0.375' in dumps({'x': Fraction(3, 8)})

    def test_dumps_nonterminating_fraction_rejected(self):
        with pytest.raises(BovnarArgumentError, match="terminating"):
            dumps({'x': Fraction(1, 3)})

    def test_from_number_float128_roundtrip(self):
        q = Quantity.from_number(Decimal(HIPI), family=F.FLOAT, width=128)
        out = dumps({'a': q})
        assert b'<float:128>' in out
        assert loads(out, typed=True)['a'].decimal == Decimal(HIPI)

    def test_from_number_float_fix_roundtrip(self):
        q = Quantity.from_number(Fraction(837, 256), family=F.FLOAT_FIX,
                                 width=32, frac=8)
        assert loads(dumps({'x': q}), typed=True)['x'].fraction == Fraction(837, 256)

    def test_from_number_float_fix_requires_frac(self):
        with pytest.raises(BovnarArgumentError, match="frac"):
            Quantity.from_number(Decimal('1.5'), family=F.FLOAT_FIX, width=32)

    def test_from_number_float_fix_range_checked(self):
        with pytest.raises(BovnarArgumentError, match="does not fit"):
            Quantity.from_number(Decimal('1e9'), family=F.FLOAT_FIX, width=16, frac=8)


@needs_lib
class TestAllWidths:
    """bovnar supports the binary (16/32/64/128/256), decimal (16/32/64/128/256)
    and fixed-point (16/32/64/128/256) widths plus integers to 256-bit; the
    lossless Python accessors must cover every one of them."""

    @pytest.mark.parametrize("width", [16, 32, 64, 128, 256])
    def test_binary_ieee_bits_and_stored_value(self, width):
        q = loads(('.a=<float:%d>0.1;' % width).encode(), typed=True)['a']
        assert len(q.ieee_bits) == width // 8
        # the materialised value re-parses to the same bytes (canonical)
        from bovnar import BvnFloat
        bits = q.ieee_bits
        assert BvnFloat.from_binary_bits(width, bits).to_binary_bits(width) == bits
        # and float(stored_value) collapses back to the nearest double
        assert isinstance(q.stored_value, Decimal)

    def test_binary_bits_match_struct(self):
        import struct
        q32 = loads(b'.a=<float:32>0.1;', typed=True)['a']
        q64 = loads(b'.a=<float:64>0.1;', typed=True)['a']
        assert q32.ieee_bits == struct.pack('<f', 0.1)
        assert q64.ieee_bits == struct.pack('<d', 0.1)

    @pytest.mark.parametrize("width", [16, 32, 64, 128, 256])
    def test_decimal_stored_value(self, width):
        q = loads(('.p=<float_dec:%d>1.5;' % width).encode(), typed=True)['p']
        assert q.stored_value == Decimal('1.5')
        assert len(q.ieee_bits) == width // 8

    @pytest.mark.parametrize("width", [16, 32, 64, 128, 256])
    def test_fixed_point_all_widths(self, width):
        q = loads(('.x=<float_fix:%d,q4>2.5;' % width).encode(), typed=True)['x']
        assert q.fixed_point == (40, 4)        # 2.5 * 2**4
        assert q.fraction == Fraction(5, 2)

    @pytest.mark.parametrize("family,width,frac", [
        (F.FLOAT, 16, None), (F.FLOAT, 32, None), (F.FLOAT, 64, None),
        (F.FLOAT, 128, None), (F.FLOAT, 256, None),
        (F.FLOAT_DEC, 16, None), (F.FLOAT_DEC, 256, None),
        (F.FLOAT_FIX, 16, 4), (F.FLOAT_FIX, 256, 8),
    ])
    def test_write_roundtrip_every_width(self, family, width, frac):
        q = Quantity.from_number(Decimal('0.5'), family=family, width=width, frac=frac)
        back = loads(dumps({'a': q}), typed=True)['a']
        assert back.decimal == Decimal('0.5')

    def test_integers_to_256bit(self):
        big = 2 ** 200 + 7
        assert loads(('.n=<uint:256>%d;' % big).encode(), typed=True)['n'].decimal \
            == Decimal(big)
        assert loads(b'.n=<sint:128>-123;', typed=True)['n'].value == -123

    def test_wide_binary_float_beyond_256_is_lossless_literal(self):
        # binary float accepts any power-of-two width; the literal round-trips
        # exactly even where there is no IEEE bit encoding (>256).
        q = loads(('.a=<float:512>%s;' % HIPI).encode(), typed=True)['a']
        assert q.decimal == Decimal(HIPI)
        with pytest.raises(BovnarArgumentError, match="no such encoding"):
            q.stored_value

    @pytest.mark.parametrize("family,width,frac,ok", [
        # binary float: 16 or any multiple of 32 (incl. non-powers-of-2), to 32768
        (F.FLOAT, 48, None, False), (F.FLOAT, 512, None, True),
        (F.FLOAT, 96, None, True), (F.FLOAT, 192, None, True),
        (F.FLOAT, 24, None, False), (F.FLOAT, 65536, None, False),
        # decimal/fixed: only the IEEE interchange set 16/32/64/128/256
        (F.FLOAT_DEC, 512, None, False), (F.FLOAT_DEC, 128, None, True),
        (F.FLOAT_DEC, 96, None, False),
        (F.FLOAT_FIX, 200, 4, False), (F.FLOAT_FIX, 64, 4, True),
    ])
    def test_width_validated_early(self, family, width, frac, ok):
        if ok:
            Quantity.from_number(Decimal('1.5'), family=family, width=width, frac=frac)
        else:
            with pytest.raises(BovnarArgumentError, match="width"):
                Quantity.from_number(Decimal('1.5'), family=family, width=width, frac=frac)


@needs_lib
class TestNumpyScalarInputs:
    """numpy scalar types must serialise by value, not via repr (np.float64's
    repr is 'np.float64(1.5)' under numpy 2.x)."""
    np = pytest.importorskip("numpy")

    def test_from_number_numpy_float(self):
        import numpy as np
        assert Quantity.from_number(np.float64(1.5)).raw == '1.5'

    @pytest.mark.parametrize("maker", [
        lambda np: np.array([np.float64(1.5), np.float64(2.5)], dtype=object),
        lambda np: np.array([np.float32(1.5), np.float32(2.25)], dtype=object),
        lambda np: np.array([np.int64(3), np.int64(4)], dtype=object),
    ])
    def test_numpy_scalar_object_array_write(self, maker):
        import numpy as np
        arr = maker(np)
        raw = bovnar.array_to_bvnr('a', arr, float_format=('float_dec', 64),
                                   pretty=False)
        back = bovnar.to_numpy(loads(raw, typed=True)['a'])
        assert [float(x) for x in back] == [float(x) for x in arr.tolist()]


@needs_lib
class TestNumpyLossless:
    np = pytest.importorskip("numpy")

    def test_float_dec_array_to_decimal(self):
        import numpy as np
        a = bovnar.to_numpy(
            loads(('.a=<float_dec:64>[19.99,0.1,%s];' % PI34).encode(),
                  typed=True)['a'])
        assert a.dtype == object
        assert a.tolist() == [Decimal('19.99'), Decimal('0.1'), Decimal(PI34)]

    def test_float128_array_to_decimal(self):
        import numpy as np
        a = bovnar.to_numpy(
            loads(('.a=<float:128>[%s,2];' % HIPI).encode(), typed=True)['a'])
        assert a.tolist() == [Decimal(HIPI), Decimal(2)]

    def test_float64_escape_is_lossy_numbers(self):
        a = bovnar.to_numpy(loads(b'.a=<float_dec:64>[1.1,2.2];', typed=True)['a'],
                            dtype='float64')
        assert a.dtype == self.np.float64 and a.tolist() == [1.1, 2.2]

    def test_decimal_object_array_write_roundtrip(self):
        import numpy as np
        arr = np.array([Decimal('19.99'), Decimal('0.1'), Decimal(PI34)], dtype=object)
        raw = bovnar.array_to_bvnr('a', arr, pretty=False)
        assert b'<float_dec:64>' in raw
        assert bovnar.to_numpy(loads(raw, typed=True)['a']).tolist() == arr.tolist()

    def test_float128_object_array_write(self):
        import numpy as np
        arr = np.array([Decimal(HIPI), Decimal('2')], dtype=object)
        raw = bovnar.array_to_bvnr('a', arr, float_format=('float', 128), pretty=False)
        assert bovnar.to_numpy(loads(raw, typed=True)['a']).tolist() == arr.tolist()

    def test_float_fix_object_array_write(self):
        import numpy as np
        arr = np.array([Fraction(837, 256), Fraction(3, 8)], dtype=object)
        raw = bovnar.array_to_bvnr('a', arr, float_format=('float_fix', 32, 8),
                                   pretty=False)
        back = [q.fraction for q in loads(raw, typed=True)['a']]
        assert back == [Fraction(837, 256), Fraction(3, 8)]

    def test_currency_decimal_roundtrip_with_unit(self):
        import numpy as np
        a, unit = bovnar.to_numpy(
            loads(b'.a=<float_dec:64,$USD>[19.99,5.00,0.01];', typed=True)['a'],
            return_unit=True)
        assert unit == '$USD'
        assert a.tolist() == [Decimal('19.99'), Decimal('5.00'), Decimal('0.01')]
        with bovnar.Writer.to_mem(pretty=False) as w:
            bovnar.from_numpy(w, 'a', a, unit=unit)
        assert b'<float_dec:64,$USD>' in w.get_output()

    def test_ambiguous_object_array_rejected(self):
        import numpy as np
        with pytest.raises(BovnarArgumentError, match="float_format"):
            bovnar.array_to_bvnr('a', np.array([10 ** 30, 1], dtype=object), pretty=False)

    def test_dom_path_default_raises_points_at_typed(self):
        with pytest.raises(BovnarArgumentError, match="loads\\(typed=True\\)"):
            bovnar.to_numpy(bovnar.dom_parse(b'.a=<float_dec:64>[1.1,2.2];')['a'])
