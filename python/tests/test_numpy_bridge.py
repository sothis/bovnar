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
"""numpy <-> bovnar bridge tests (to_numpy / from_numpy / array_to_bvnr / pint arrays)."""
import pytest

np = pytest.importorskip("numpy")
from conftest import needs_lib

import bovnar
from bovnar import (
    loads, dom_parse, Writer,
    to_numpy, from_numpy, to_pint_array, from_pint_array, array_to_bvnr,
    parse_unit,
)
from bovnar.exceptions import BovnarArgumentError

try:
    import pint  # noqa: F401
    _HAS_PINT = True
except ImportError:
    _HAS_PINT = False

needs_pint = pytest.mark.skipif(not _HAS_PINT, reason="pint not installed")


@needs_lib
class TestToNumpy:
    def test_uint8_1d(self):
        a = to_numpy(loads(b'.a=<uint:8>[1,2,3];', typed=True)['a'])
        assert a.dtype == np.uint8 and a.tolist() == [1, 2, 3]

    def test_float32_2d_with_unit(self):
        a, unit = to_numpy(loads(b'.a=<float:32,m/s>[1,2,3]/[4,5,6];', typed=True)['a'],
                           return_unit=True)
        assert a.dtype == np.float32 and a.shape == (2, 3) and unit == 'm/s'

    def test_nested_and_slash_same_shape(self):
        nested = to_numpy(loads(b'.a=[[1,2,3],[4,5,6]];', typed=True)['a'])
        slash = to_numpy(loads(b'.a=[1,2,3]/[4,5,6];', typed=True)['a'])
        assert nested.shape == slash.shape == (2, 3)

    def test_3d(self):
        a = to_numpy(loads(b'.a=[[1,2]/[3,4],[5,6]/[7,8]];', typed=True)['a'])
        assert a.shape == (2, 2, 2)

    def test_dom_source(self):
        a = to_numpy(dom_parse(b'.a=<sint:16>[10,-20,30]/[40,-50,60];')['a'])
        assert a.dtype == np.int16 and a.shape == (2, 3) and a[1, 1] == -50

    def test_slash_matrix_more_than_8_rows(self):
        # Regression: the DOM previously capped the recorded /-row count at 8,
        # so matrices with >8 rows lost (or mis-grouped) their shape. A 10x2
        # matrix must reshape to (10, 2), not flatten or fold into (8, ...).
        rows = '/'.join('[%d,%d]' % (i, i + 1) for i in range(10))
        src = ('.a=<uint:16>' + rows + ';').encode()
        a = to_numpy(loads(src, typed=True)['a'])
        assert a.shape == (10, 2)
        assert a[9].tolist() == [9, 10]

    def test_null_in_float_becomes_nan(self):
        a = to_numpy(loads(b'.a=<float:32>[1,,3];', typed=True)['a'])
        assert a.dtype == np.float32 and np.isnan(a[1]) and a[0] == 1.0

    def test_dtype_coercion_of_mixed(self):
        a = to_numpy(loads(b'.a=[<uint:8>1,<float:32>2.5];', typed=True)['a'],
                     dtype='float64')
        assert a.dtype == np.float64 and a.tolist() == [1.0, 2.5]


@needs_lib
class TestUnmappableWidthEscapeHatch:
    """A width with no native numpy dtype (>64-bit integer, or a float width
    other than 16/32/64) must be rejected only under strict inference
    (dtype=None) — an explicit dtype= must still coerce it. Regression: the
    inference probe used to raise before the caller's dtype was consulted, so
    the advertised dtype=object / dtype=float remedies did not work."""

    _BIG = 340282366920938463463374607431768211455  # 2**128 - 1

    def test_bigint_default_raises(self):
        with pytest.raises(BovnarArgumentError):
            to_numpy(dom_parse(b'.a=<uint:128>[%d,1];' % self._BIG)['a'])

    def test_bigint_object_exact(self):
        a = to_numpy(dom_parse(b'.a=<uint:128>[%d,1];' % self._BIG)['a'],
                     dtype=object)
        assert a.dtype == object and a[0] == self._BIG and a[1] == 1

    def test_bigint_float_coerces(self):
        a = to_numpy(dom_parse(b'.a=<uint:128>[%d,1];' % self._BIG)['a'],
                     dtype=float)
        assert a.dtype == np.float64 and a[1] == 1.0

    def test_float256_default_raises(self):
        with pytest.raises(BovnarArgumentError):
            to_numpy(dom_parse(b'.a=<float:256>[1.5,2.5];')['a'])

    def test_float256_coerces_to_float64(self):
        a = to_numpy(dom_parse(b'.a=<float:256>[1.5,2.5];')['a'], dtype='float64')
        assert a.dtype == np.float64 and a.tolist() == [1.5, 2.5]

    def test_float128_dom_default_raises(self):
        # The DOM path has only a lossy C double for <float:128>; it must point
        # at the lossless typed path or the explicit float64 opt-in, not map to
        # the (mismatched, 80-bit) np.float128.
        with pytest.raises(BovnarArgumentError, match="lossless numpy dtype"):
            to_numpy(dom_parse(b'.a=<float:128>[1.5,2.5];')['a'])

    def test_float128_coerces_to_float64(self):
        a = to_numpy(dom_parse(b'.a=<float:128>[1.5,2.5];')['a'], dtype='float64')
        assert a.dtype == np.float64 and a.tolist() == [1.5, 2.5]

    @pytest.mark.skipif(not hasattr(np, 'float128'),
                        reason="platform has no float128/longdouble")
    def test_float128_array_write_rejected(self):
        # a user-built np.float128 (x86 80-bit, not IEEE binary128) array must
        # give a clear message, not a cryptic "cannot serialise longdouble".
        with pytest.raises(BovnarArgumentError, match="no faithful bovnar mapping"):
            array_to_bvnr('a', np.array([1.5, 2.5], dtype=np.float128))

    @pytest.mark.skipif(not hasattr(np, 'float128'),
                        reason="platform has no float128/longdouble")
    def test_float128_array_write_after_cast(self):
        raw = array_to_bvnr('a', np.array([1.5, 2.5], dtype=np.float128).astype('float64'),
                            pretty=False)
        assert to_numpy(loads(raw, typed=True)['a']).tolist() == [1.5, 2.5]


@needs_lib
class TestRawPythonIntRange:
    """Raw Python lists are an accepted source. An int past 2**63-1 must not
    raise a bare numpy OverflowError: it is widened to uint64 when it fits and
    nothing is negative (bovnar's own default for a bare non-negative integer),
    routed to the object escape when it is a true bigint, and a genuine
    signed/unsigned conflict gives a clean, actionable message."""

    _U = 2 ** 63 + 5            # past int64, fits uint64
    _BIG = 10 ** 30             # past uint64

    def test_uint64_band_widens(self):
        a = to_numpy([self._U, 1])
        assert a.dtype == np.uint64 and a.tolist() == [self._U, 1]

    def test_signed_unsigned_conflict_is_clean(self):
        with pytest.raises(BovnarArgumentError, match="mixes element dtypes"):
            to_numpy([-1, self._U])

    def test_conflict_object_escape(self):
        a = to_numpy([-1, self._U], dtype=object)
        assert a.tolist() == [-1, self._U]

    def test_bigint_clean_error(self):
        with pytest.raises(BovnarArgumentError, match="64-bit range"):
            to_numpy([self._BIG, 1])

    def test_bigint_object_exact(self):
        a = to_numpy([self._BIG, 1], dtype=object)
        assert a[0] == self._BIG and a[1] == 1

    def test_explicit_narrow_dtype_overflow_is_clean(self):
        with pytest.raises(BovnarArgumentError, match="out of range"):
            to_numpy([self._U, 1], dtype='int64')

    def test_plain_small_ints_stay_int64(self):
        assert to_numpy([1, 2, 3]).dtype == np.int64

    def test_typed_signed_unsigned_nonnegative_reconciles(self):
        # mixed <sint:64>/<uint:64> leaves, all non-negative -> uint64
        a = to_numpy(loads(
            b'.a=[<sint:64>5,<uint:64>9223372036854775813];', typed=True)['a'])
        assert a.dtype == np.uint64 and a.tolist() == [5, 9223372036854775813]


@needs_lib
class TestToNumpyStrict:
    @pytest.mark.parametrize("payload", [
        b'.a=[<uint:8>1,<float:32>2.0];',     # mixed dtype
        b'.a=[<float:32,m>1,<float:32,s>2];',  # mixed unit
        b'.a=[[1,2],[3,4,5]];',                # ragged
        b'.a=<uint:8>[1,,3];',                 # null into an int array
    ])
    def test_rejected(self, payload):
        with pytest.raises(BovnarArgumentError):
            to_numpy(loads(payload, typed=True)['a'])


@needs_lib
class TestNullCoercionStrict:
    """A null in a dtype with no null slot must raise, not be coerced. numpy
    silently turns None into False (bool) or the literal string 'None' (str),
    which is data corruption; only the integer case was guarded before."""

    @pytest.mark.parametrize("payload", [
        b'.a=<bool>[true,,false];',  # None -> False
        b'.a=["x",,"y"];',           # None -> 'None'
        b'.a=<sint:16>[1,,3];',      # None -> TypeError historically
    ])
    def test_inferred_null_rejected(self, payload):
        with pytest.raises(BovnarArgumentError, match="null"):
            to_numpy(loads(payload, typed=True)['a'])

    @pytest.mark.parametrize("payload,dt", [
        (b'.a=<bool>[true,,false];', 'bool'),
        (b'.a=["x",,"y"];', 'str'),
    ])
    def test_explicit_lossy_dtype_still_rejected(self, payload, dt):
        # An explicit but null-incapable dtype must not silently coerce either.
        with pytest.raises(BovnarArgumentError, match="null"):
            to_numpy(loads(payload, typed=True)['a'], dtype=dt)

    def test_object_dtype_preserves_null(self):
        a = to_numpy(loads(b'.a=["x",,"y"];', typed=True)['a'], dtype=object)
        assert a.tolist() == ['x', None, 'y']

    def test_bool_null_object_dtype_preserves_null(self):
        a = to_numpy(loads(b'.a=<bool>[true,,false];', typed=True)['a'],
                     dtype=object)
        assert a.tolist() == [True, None, False]


@needs_lib
class TestMixedUnitDimensionless:
    """A numpy array carries one whole-array unit. Mixing a dimensioned numeric
    element with a dimensionless one must raise — silently labelling the bare
    value with the other's unit invents a dimension it does not have (the same
    conflict class as two distinct units, which is already rejected)."""

    @pytest.mark.parametrize("payload", [
        b'.a=[<float:32,m>1,<float:32>2];',   # unit then dimensionless
        b'.a=[<float:32>1,<float:32,m>2];',   # dimensionless then unit
    ])
    def test_inferred_conflict_rejected(self, payload):
        with pytest.raises(BovnarArgumentError, match="dimensionless"):
            to_numpy(loads(payload, typed=True)['a'])

    def test_conflict_rejected_even_with_explicit_dtype(self):
        # the unit ambiguity is independent of the element dtype coercion.
        with pytest.raises(BovnarArgumentError, match="dimensionless"):
            to_numpy(loads(b'.a=[<float:32,m>1,<float:32>2];', typed=True)['a'],
                     dtype='float64')

    def test_uniform_unit_still_ok_typed(self):
        a, u = to_numpy(loads(b'.a=<float:32,m/s>[1,2,3];', typed=True)['a'],
                        return_unit=True)
        assert u == 'm/s' and a.tolist() == [1.0, 2.0, 3.0]

    def test_uniform_unit_still_ok_dom(self):
        a, u = to_numpy(dom_parse(b'.a=<float:32,m/s>[1,2,3];')['a'],
                        return_unit=True)
        assert u == 'm/s'

    def test_all_dimensionless_ok(self):
        a, u = to_numpy(loads(b'.a=[1,2,3];', typed=True)['a'], return_unit=True)
        assert u == '' and a.tolist() == [1, 2, 3]


@needs_lib
class TestFromNumpy:
    @pytest.mark.parametrize("arr,unit", [
        (np.array([0, 127, 255], dtype=np.uint8), None),
        (np.array([[1, -2], [3, -4]], dtype=np.int32), None),
        (np.array([1.5, 2.5], dtype=np.float32), 'm/s'),
        (np.arange(8, dtype=np.float64).reshape(2, 2, 2), None),
        (np.array([1.5, 2.5, -3.25], dtype=np.float16), None),
        (np.array([True, False, True]), None),
        (np.array(['a', 'bb', 'ccc']), None),
    ], ids=['uint8', 'int32_2d', 'f32_unit', 'f64_3d', 'f16', 'bool', 'str'])
    def test_roundtrip(self, arr, unit):
        raw = array_to_bvnr('a', arr, unit=unit, pretty=False)
        back = to_numpy(loads(raw, typed=True)['a'])
        assert back.shape == arr.shape
        assert np.array_equal(back, arr)
        if arr.dtype.kind != 'U':
            assert back.dtype == arr.dtype

    def test_uint64_max_exact(self):
        arr = np.array([0, 18446744073709551615], dtype=np.uint64)
        back = to_numpy(loads(array_to_bvnr('a', arr, pretty=False), typed=True)['a'])
        assert back.dtype == np.uint64 and back[1] == np.uint64(18446744073709551615)

    def test_int64_min_exact(self):
        arr = np.array([-9223372036854775808, 7], dtype=np.int64)
        back = to_numpy(loads(array_to_bvnr('a', arr, pretty=False), typed=True)['a'])
        assert back.tolist() == [-9223372036854775808, 7]

    def test_into_writer(self):
        with Writer.to_mem(pretty=False) as w:
            from_numpy(w, 'a', np.array([1, 2, 3], dtype=np.uint16), unit='Hz')
        assert b'<uint:16,Hz>' in w.get_output()

    def test_complex_rejected(self):
        with pytest.raises(BovnarArgumentError):
            array_to_bvnr('a', np.array([1 + 2j]))

    def test_zero_d_rejected(self):
        with pytest.raises(BovnarArgumentError):
            array_to_bvnr('a', np.array(5))

    def test_unit_on_bool_rejected(self):
        with pytest.raises(BovnarArgumentError):
            array_to_bvnr('a', np.array([True, False]), unit='m')

    def test_masked_array_with_masked_entries_rejected(self):
        # np.asarray drops the mask and exposes the underlying value, so a masked
        # entry must not be silently serialized.
        m = np.ma.array([1.0, 2.0, 3.0], mask=[0, 1, 0])
        with pytest.raises(BovnarArgumentError, match="masked"):
            array_to_bvnr('a', m)

    def test_masked_array_without_masked_entries_ok(self):
        m = np.ma.array([1.0, 2.0, 3.0], mask=[0, 0, 0])
        raw = array_to_bvnr('a', m, pretty=False)
        assert to_numpy(loads(raw, typed=True)['a']).tolist() == [1.0, 2.0, 3.0]

    def test_masked_array_filled_escape(self):
        m = np.ma.array([1.0, 2.0, 3.0], mask=[0, 1, 0])
        raw = array_to_bvnr('a', m.filled(0.0), pretty=False)
        assert to_numpy(loads(raw, typed=True)['a']).tolist() == [1.0, 0.0, 3.0]

    @pytest.mark.parametrize("bad", [5, ['m'], 3.5, object()])
    def test_invalid_unit_type_rejected(self, bad):
        with pytest.raises(BovnarArgumentError, match="unit must be"):
            array_to_bvnr('a', np.array([1.0, 2.0]), unit=bad)


@needs_lib
@needs_pint
class TestPintArray:
    def test_to_pint_array_preserves_data_and_unit(self):
        q = to_pint_array(loads(b'.a=<float:64,k~m>[1,2,3];', typed=True)['a'])
        assert str(q.units) == 'kilometer'
        assert np.array_equal(q.magnitude, [1.0, 2.0, 3.0])
        assert np.allclose(q.to('meter').magnitude, [1000, 2000, 3000])

    def test_from_pint_array_roundtrip(self):
        from bovnar import to_pint
        q = to_pint(np.array([2.0, 4.0, 6.0]), parse_unit('m~s'))   # milliseconds
        with Writer.to_mem(pretty=False) as w:
            from_pint_array(w, 'a', q)
        a, unit = to_numpy(loads(w.get_output(), typed=True)['a'], return_unit=True)
        assert np.array_equal(a, [2, 4, 6]) and unit == 'm~s'

    def test_datetime_array_to_pint_rejected(self):
        # pint has no absolute-timestamp concept; wrapping datetime64 would
        # silently present it as a meaningless dimensionless quantity.
        src = loads(b'#!bovnar 1.1\n.t = [2026-01-01, 2026-01-02];', typed=True)['t']
        with pytest.raises(BovnarArgumentError, match="datetime"):
            to_pint_array(src)

    def test_datetime_array_to_pint_int64_escape(self):
        # explicit dtype='int64' yields the raw epoch seconds as a dimensionless
        # count — a meaningful result, so it stays allowed.
        src = loads(b'#!bovnar 1.1\n.t = [<datetime:64,tai> 100, 200];',
                    typed=True)['t']
        q = to_pint_array(src, dtype='int64')
        assert q.magnitude.tolist() == [100, 200]
        assert str(q.units) == 'dimensionless'


# --------------------------------------------------------------------------- #
#  datetime64 bridge (spec 1.1)
# --------------------------------------------------------------------------- #
@needs_lib
class TestDatetime64:
    _V = b'#!bovnar 1.1\n'

    def test_to_numpy_unix_array_is_datetime64(self):
        # bovnar unix-epoch datetime carrier -> datetime64[s]
        a = to_numpy(loads(self._V + b'.t = [2026-01-01, 2026-01-02, 2026-01-03];',
                           typed=True)['t'])
        assert a.dtype == np.dtype('datetime64[s]')
        assert a[0] == np.datetime64('2026-01-01T00:00:00')
        # carrier seconds match
        assert a.astype('int64').tolist() == [1767225600, 1767312000, 1767398400]

    def test_to_numpy_integer_carrier_array(self):
        a = to_numpy(loads(self._V + b'.t = [<datetime:64> 100, 200, 300];',
                           typed=True)['t'])
        assert a.dtype == np.dtype('datetime64[s]')
        assert a.astype('int64').tolist() == [100, 200, 300]

    def test_to_numpy_2d_datetime(self):
        a = to_numpy(loads(self._V + b'.t = [2026-01-01,2026-01-02]/[2026-01-03,2026-01-04];',
                           typed=True)['t'])
        assert a.dtype == np.dtype('datetime64[s]') and a.shape == (2, 2)

    def test_to_numpy_pre_1970_via_domnode(self):
        # Regression: the DomNode read path decoded a negative (pre-1970)
        # datetime as a huge uint64, which then overflowed datetime64[s].
        a = to_numpy(dom_parse(self._V + b'.t = [1960-01-01, 2020-01-01];')['t'])
        assert a.dtype == np.dtype('datetime64[s]')
        assert a.astype('int64').tolist() == [-315619200, 1577836800]

    def test_to_numpy_non_unix_epoch_refused_by_default(self):
        # tai/gps/... are not unix-relative; default inference refuses
        with pytest.raises(BovnarArgumentError) as ei:
            to_numpy(loads(self._V + b'.t = [<datetime:64,tai> 100, 200];',
                           typed=True)['t'])
        assert 'unix' in str(ei.value)

    def test_to_numpy_non_unix_epoch_explicit_int64(self):
        # explicit dtype='int64' yields the raw carrier seconds on that scale
        a = to_numpy(loads(self._V + b'.t = [<datetime:64,tai> 100, 200];',
                           typed=True)['t'], dtype='int64')
        assert a.tolist() == [100, 200]

    def test_from_numpy_into_writer_emits_datetime(self):
        with Writer.to_mem(pretty=False) as w:
            w.write_version(1, 1)
            from_numpy(w, 't', np.array(['2026-01-01', '2026-01-02'],
                                        dtype='datetime64[s]'))
        out = w.get_output()
        assert b'<datetime' in out
        assert loads(out)['t'] == [1767225600, 1767312000]

    def test_from_numpy_rejects_unit(self):
        with pytest.raises(BovnarArgumentError):
            with Writer.to_mem(pretty=False) as w:
                from_numpy(w, 't', np.array(['2026-01-01'], dtype='datetime64[s]'),
                           unit='s')

    def test_from_numpy_rejects_nat(self):
        with pytest.raises(BovnarArgumentError) as ei:
            array_to_bvnr('t', np.array(['2026-01-01', 'NaT'], dtype='datetime64[s]'))
        assert 'NaT' in str(ei.value)

    def test_array_to_bvnr_is_self_contained_and_reparses(self):
        # datetime64 -> array_to_bvnr must prepend the #!bovnar 1.1 directive
        raw = array_to_bvnr('t', np.array(['2026-06-15T12:00:00', '1970-01-01T00:00:00'],
                                          dtype='datetime64[s]'), pretty=False)
        assert raw.startswith(b'#!bovnar 1.1')
        assert b'<datetime' in raw
        assert loads(raw)['t'] == [1781524800, 0]

    def test_roundtrip_resolution_coarsened_to_seconds(self):
        # finer resolutions truncate to whole seconds (the bovnar carrier)
        arr = np.array(['2026-06-15T12:00:00.123456789'], dtype='datetime64[ns]')
        raw = array_to_bvnr('t', arr, pretty=False)
        assert loads(raw)['t'] == [1781524800]

    def test_roundtrip_bovnar_to_numpy_to_bovnar(self):
        src = self._V + b'.t = [2026-01-01, 2026-06-15, 1970-01-01];'
        a = to_numpy(loads(src, typed=True)['t'])
        raw = array_to_bvnr('t', a, pretty=False)
        assert loads(raw)['t'] == loads(src)['t']
