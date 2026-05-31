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
"""Contract test for the examples/numpy_roundtrip.bvnr fixture.

Asserts that the shipped example loads to exactly the documented arrays and that
every one survives the numpy bridge: the loads() and DOM read paths agree, the
array re-serialises and reads back identically, and unit-bearing arrays wrap as
pint quantities.  The hardcoded (dtype, shape, unit) table also guards the
fixture itself against accidental regeneration with different contents.
"""
from pathlib import Path

import pytest

np = pytest.importorskip("numpy")
from conftest import needs_lib

from bovnar import loads, dom_parse, to_numpy, from_numpy, to_pint_array, Writer

try:
    import pint  # noqa: F401
    _HAS_PINT = True
except ImportError:
    _HAS_PINT = False
needs_pint = pytest.mark.skipif(not _HAS_PINT, reason="pint not installed")

FIXTURE = Path(__file__).resolve().parents[2] / "examples" / "numpy_roundtrip.bvnr"

# key -> (numpy dtype, shape, bovnar unit string).  '' means dimensionless.
EXPECTED = [
    ('imu_accel',         'float32', (7, 3),    'm/s²'),
    ('imu_gyro',          'float64', (7, 3),    'rad/s'),
    ('magnetometer',      'float32', (7, 3),    'n~T'),
    ('sample_time',       'uint32',  (7,),      'm~s'),
    ('packet_counts',     'uint16',  (6,),      ''),
    ('temperature_field', 'float32', (2, 3, 3), 'K'),
    ('pressure_grid',     'float32', (4, 5),    'k~Pa'),
    ('voltage_samples',   'int16',   (3, 5),    'm~V'),
    ('current_draw',      'float64', (6,),      'A'),
    ('spectral_power',    'float32', (4, 5),    'W'),
    ('rotation_tensor',   'float64', (3, 3, 3), ''),
    ('force_vectors',     'float64', (5, 3),    'k~g·m/s²'),
    ('gain_table',        'float16', (3, 4),    ''),
    ('position_eci',      'float64', (6, 3),    'k~m'),
    ('data_throughput',   'uint64',  (6,),      'Ki~B'),
    ('frequency_bins',    'float32', (6,),      'M~Hz'),
]
_UNIT_KEYS = [k for k, _, _, u in EXPECTED if u]

pytestmark = pytest.mark.skipif(not FIXTURE.exists(),
                                reason=f"fixture missing: {FIXTURE}")


@pytest.fixture(scope="module")
def raw():
    return FIXTURE.read_bytes()


@pytest.fixture(scope="module")
def doc_loads(raw):
    return loads(raw, typed=True)


@pytest.fixture(scope="module")
def doc_dom(raw):
    return dom_parse(raw)


@needs_lib
class TestFixtureContents:
    def test_keys_match_exactly(self, doc_loads):
        assert set(doc_loads) == {k for k, *_ in EXPECTED}

    def test_rank_and_dtype_variety(self, doc_loads):
        ranks = {to_numpy(doc_loads[k]).ndim for k, *_ in EXPECTED}
        dtypes = {str(to_numpy(doc_loads[k]).dtype) for k, *_ in EXPECTED}
        assert ranks == {1, 2, 3}                     # showcases 1-D, 2-D and 3-D
        assert len(dtypes) >= 7                        # spread of int/uint/float widths


@needs_lib
class TestFixtureRoundTrip:
    @pytest.mark.parametrize("key,dtype,shape,unit", EXPECTED,
                             ids=[e[0] for e in EXPECTED])
    def test_array(self, key, dtype, shape, unit, doc_loads, doc_dom):
        # 1) loads() path yields the documented dtype / shape / unit
        arr, got_unit = to_numpy(doc_loads[key], return_unit=True)
        assert str(arr.dtype) == dtype
        assert arr.shape == shape
        assert got_unit == unit

        # 2) the DOM read path agrees element-for-element
        arr_dom = to_numpy(doc_dom[key])
        assert arr_dom.dtype == arr.dtype
        assert np.array_equal(arr_dom, arr)

        # 3) bovnar -> numpy -> bovnar -> numpy is idempotent
        with Writer.to_mem() as w:
            from_numpy(w, key, arr, unit=(unit or None))
        arr_rt = to_numpy(loads(w.get_output(), typed=True)[key])
        assert arr_rt.dtype == arr.dtype
        assert arr_rt.shape == arr.shape
        assert np.array_equal(arr_rt, arr)


@needs_lib
@needs_pint
class TestFixturePintArrays:
    @pytest.mark.parametrize("key", _UNIT_KEYS)
    def test_to_pint_array(self, key, doc_loads):
        q = to_pint_array(doc_loads[key])
        assert np.array_equal(q.magnitude, to_numpy(doc_loads[key]))
