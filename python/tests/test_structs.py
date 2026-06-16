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



import ctypes
import pytest

from bovnar.enums import (
    ValueTypeFamily, PrefixSystem, SIPrefix, IECPrefix,
    BaseUnit, Exponent,
)
from bovnar.structs import (
    ValueUnitComponent, ValueUnit, ValueTypeSpec, BvnrData,
    BvnrSource, BvnrSink, BvnrReadFlags, BvnrWriteFlags,
    MAX_UNIT_COMPONENTS, OPAQUE_BYTES,
    make_type_spec, make_unit_dimensionless, make_unit_none,
    make_unit_si, make_unit_iec, make_data_key, make_data_typed,
)

class TestConstants:
    def test_max_unit_components(self):
        assert MAX_UNIT_COMPONENTS == 8

    def test_opaque_bytes(self):
        assert OPAQUE_BYTES >= 64

class TestValueUnitComponent:
    def test_size_reasonable(self):

        assert ctypes.sizeof(ValueUnitComponent) >= 12

    def test_zero_init(self):
        c = ValueUnitComponent()
        assert c.base     == 0
        assert c.exponent == 0
        assert c.prefix.system == 0
        assert c.prefix.id.si  == 0

    def test_set_fields(self):
        c = ValueUnitComponent()
        c.base     = int(BaseUnit.METER)
        c.exponent = int(Exponent.SQUARE)
        c.prefix.system  = int(PrefixSystem.SI)
        c.prefix.id.si   = int(SIPrefix.KILO)

        assert c.base_unit    == BaseUnit.METER
        assert c.exp          == Exponent.SQUARE
        assert c.prefix_system == PrefixSystem.SI
        assert c.si_prefix     == SIPrefix.KILO

    def test_iec_prefix(self):
        c = ValueUnitComponent()
        c.base    = int(BaseUnit.BYTE)
        c.prefix.system  = int(PrefixSystem.IEC)
        c.prefix.id.iec  = int(IECPrefix.GIBI)
        assert c.prefix_system == PrefixSystem.IEC
        assert c.iec_prefix    == IECPrefix.GIBI

    def test_union_overlap(self):

        c = ValueUnitComponent()
        c.prefix.id.si  = 42
        assert c.prefix.id.iec == 42

    def test_repr(self):
        c = ValueUnitComponent()
        c.base = int(BaseUnit.SECOND)
        r = repr(c)
        assert 'SECOND' in r

class TestValueUnit:
    def test_size(self):

        comp_size = ctypes.sizeof(ValueUnitComponent)
        expected  = 4 + comp_size * 8
        assert ctypes.sizeof(ValueUnit) >= expected

    def test_zero_init(self):
        vu = ValueUnit()
        assert vu.num_components == 0
        assert vu.is_dimensionless

    def test_active_components(self):
        vu                = ValueUnit()
        vu.num_components = 2
        comps             = vu.active_components()
        assert len(comps) == 2

    def test_dimensionless_single_none(self):
        vu = ValueUnit()
        vu.num_components = 1
        vu.components[0].base = int(BaseUnit.NONE)
        assert vu.is_dimensionless

    def test_not_dimensionless(self):
        vu = ValueUnit()
        vu.num_components = 1
        vu.components[0].base = int(BaseUnit.METER)
        assert not vu.is_dimensionless

class TestValueTypeSpec:
    def test_zero_init(self):
        vt = ValueTypeSpec()
        assert vt.family == 0
        assert vt.width  == 0
        assert vt.base   == 0

    def test_set_fields(self):
        vt = ValueTypeSpec()
        vt.family = int(ValueTypeFamily.UINT)
        vt.width  = 32
        vt.base   = 10
        assert vt.type_family == ValueTypeFamily.UINT
        assert vt.width == 32

    def test_repr(self):
        vt = make_type_spec(ValueTypeFamily.FLOAT, 64, 10)
        r  = repr(vt)
        assert 'FLOAT' in r
        assert '64'    in r

class TestBvnrData:
    def test_zero_init(self):
        d = BvnrData()
        assert d.data   == 0 or d.data is None
        assert d.length == 0
        assert d.raw_bytes() == b''

    def test_raw_bytes(self):
        payload = b'hello'
        buf     = (ctypes.c_char * len(payload)).from_buffer_copy(payload)
        d       = BvnrData()
        d.data   = ctypes.cast(buf, ctypes.c_void_p)
        d.length = len(payload)
        assert d.raw_bytes() == b'hello'
        assert d.raw_str()   == 'hello'

    def test_raw_bytes_empty(self):
        d = BvnrData()
        assert d.raw_bytes() == b''

    def test_repr(self):
        d = BvnrData()
        assert 'BvnrData' in repr(d)

    def test_has_spec_1_1_frac_fields(self):
        # The C bvnr_data_t (spec 1.1) ends with frac_data/frac_length. The
        # writer path passes &BvnrData to bvnr_write_event, so a missing field
        # makes the struct short and C reads frac_data out of bounds when
        # (de)serialising a datetime — a real crash. Guard the layout.
        names = [n for n, _ in BvnrData._fields_]
        assert names[-2:] == ['frac_data', 'frac_length'], names
        d = BvnrData()
        assert (d.frac_data == 0 or d.frac_data is None)
        assert d.frac_length == 0
        assert d.frac_str() is None
        # frac_data must sit AFTER data/length (appended, not inserted)
        assert BvnrData.frac_data.offset > BvnrData.length.offset
        assert BvnrData.frac_length.offset > BvnrData.frac_data.offset

class TestOpaqueTypes:
    def test_source_size(self):
        assert ctypes.sizeof(BvnrSource) == OPAQUE_BYTES

    def test_sink_size(self):
        assert ctypes.sizeof(BvnrSink) == OPAQUE_BYTES

    def test_source_zero_init(self):
        src = BvnrSource()
        assert all(b == 0 for b in src._opaque)

    def test_sink_zero_init(self):
        sink = BvnrSink()
        assert all(b == 0 for b in sink._opaque)

class TestBvnrReadFlags:
    def test_zero_init(self):
        f = BvnrReadFlags()
        assert f.max_file_size     == 0
        assert f.max_struct_nesting == 0
        assert not f.continue_on_error

    def test_set_max_file_size(self):
        f = BvnrReadFlags()
        f.max_file_size = 16 * 1024 * 1024
        assert f.max_file_size == 16 * 1024 * 1024

    def test_size_reasonable(self):

        assert ctypes.sizeof(BvnrReadFlags) >= 32

class TestBvnrWriteFlags:
    def test_zero_init(self):
        f = BvnrWriteFlags()
        assert f.max_file_size == 0
        assert not f.continue_on_error

class TestMakeTypeSpec:
    def test_uint(self):
        vt = make_type_spec(ValueTypeFamily.UINT, 16, 10)
        assert vt.type_family == ValueTypeFamily.UINT
        assert vt.width == 16
        assert vt.base  == 10

    def test_float(self):
        vt = make_type_spec(ValueTypeFamily.FLOAT, 64, 0)
        assert vt.type_family == ValueTypeFamily.FLOAT

    def test_defaults(self):
        vt = make_type_spec(ValueTypeFamily.UTF8)
        assert vt.width == 0
        assert vt.base  == 0

class TestMakeUnitHelpers:
    def test_dimensionless(self):
        vu = make_unit_dimensionless()
        assert vu.num_components == 1
        assert vu.components[0].base == int(BaseUnit.NONE)
        assert vu.is_dimensionless

    def test_unit_none(self):
        vu = make_unit_none()
        assert vu.num_components == 0
        assert vu.is_dimensionless

    def test_unit_si_no_prefix(self):
        vu = make_unit_si(BaseUnit.METER)
        assert vu.num_components == 1
        assert vu.components[0].base_unit   == BaseUnit.METER
        assert vu.components[0].prefix_system == PrefixSystem.SI
        assert vu.components[0].si_prefix    == SIPrefix.NONE

    def test_unit_si_with_prefix(self):
        vu = make_unit_si(BaseUnit.GRAM, SIPrefix.KILO)
        assert vu.components[0].si_prefix == SIPrefix.KILO
        assert vu.components[0].base_unit  == BaseUnit.GRAM

    def test_unit_si_with_exponent(self):
        vu = make_unit_si(BaseUnit.METER, SIPrefix.NONE, Exponent.SQUARE)
        assert vu.components[0].exp == Exponent.SQUARE

    def test_unit_iec(self):
        vu = make_unit_iec(BaseUnit.BYTE, IECPrefix.GIBI)
        assert vu.num_components == 1
        assert vu.components[0].base_unit    == BaseUnit.BYTE
        assert vu.components[0].prefix_system == PrefixSystem.IEC
        assert vu.components[0].iec_prefix    == IECPrefix.GIBI

class TestMakeDataKey:
    def test_ascii(self):
        d, raw = make_data_key("port")
        assert d.length == 4
        assert raw == b'port'

    def test_unicode(self):
        d, raw = make_data_key("tëst")
        assert raw == "tëst".encode('utf-8')
        assert d.length == len(raw)

    def test_data_pointer_valid(self):
        d, raw = make_data_key("foo")

        assert d.data != 0

class TestMakeDataTyped:
    def test_returns_bvnr_data(self):
        vt = make_type_spec(ValueTypeFamily.UINT, 32, 10)
        vu = make_unit_dimensionless()
        d  = make_data_typed(vt, vu)
        assert isinstance(d, BvnrData)
        assert d.value_type.family == int(ValueTypeFamily.UINT)

from bovnar.structs import make_unit_compound

class TestMakeUnitCompound:
    def test_single_component_si(self):
        vu = make_unit_compound([
            {'base': BaseUnit.METER},
        ])
        assert vu.num_components == 1
        assert vu.components[0].base_unit   == BaseUnit.METER
        assert vu.components[0].exp         == Exponent.LINEAR
        assert vu.components[0].si_prefix   == SIPrefix.NONE

    def test_single_component_with_prefix(self):
        vu = make_unit_compound([
            {'base': BaseUnit.METER, 'si_prefix': SIPrefix.KILO},
        ])
        assert vu.components[0].si_prefix == SIPrefix.KILO

    def test_two_components_m_per_s(self):
        vu = make_unit_compound([
            {'base': BaseUnit.METER,  'exp': Exponent.LINEAR},
            {'base': BaseUnit.SECOND, 'exp': Exponent.NEG_LINEAR},
        ])
        assert vu.num_components == 2
        assert vu.components[0].base_unit == BaseUnit.METER
        assert vu.components[1].base_unit == BaseUnit.SECOND
        assert vu.components[1].exp       == Exponent.NEG_LINEAR

    def test_three_components_force(self):
        vu = make_unit_compound([
            {'base': BaseUnit.GRAM,   'exp': Exponent.LINEAR,     'si_prefix': SIPrefix.KILO},
            {'base': BaseUnit.METER,  'exp': Exponent.LINEAR},
            {'base': BaseUnit.SECOND, 'exp': Exponent.NEG_SQUARE},
        ])
        assert vu.num_components == 3
        assert vu.components[0].si_prefix == SIPrefix.KILO
        assert vu.components[2].exp       == Exponent.NEG_SQUARE

    def test_iec_prefix_component(self):
        vu = make_unit_compound([
            {'base': BaseUnit.BYTE, 'iec_prefix': IECPrefix.GIBI},
        ])
        c = vu.components[0]
        assert c.prefix_system == PrefixSystem.IEC
        assert c.iec_prefix    == IECPrefix.GIBI

    def test_default_exp_is_linear(self):
        vu = make_unit_compound([{'base': BaseUnit.METER}])
        assert vu.components[0].exp == Exponent.LINEAR

    def test_default_si_prefix_is_none(self):
        vu = make_unit_compound([{'base': BaseUnit.SECOND}])
        assert vu.components[0].si_prefix == SIPrefix.NONE

    def test_max_components(self):
        vu = make_unit_compound([
            {'base': BaseUnit.METER} for _ in range(MAX_UNIT_COMPONENTS)
        ])
        assert vu.num_components == MAX_UNIT_COMPONENTS

    def test_too_many_components_raises(self):
        with pytest.raises(ValueError):
            make_unit_compound([
                {'base': BaseUnit.METER} for _ in range(MAX_UNIT_COMPONENTS + 1)
            ])

    def test_empty_list(self):
        vu = make_unit_compound([])
        assert vu.num_components == 0

    def test_active_components_matches_num(self):
        vu = make_unit_compound([
            {'base': BaseUnit.METER},
            {'base': BaseUnit.SECOND, 'exp': Exponent.NEG_LINEAR},
        ])
        assert len(vu.active_components()) == 2
