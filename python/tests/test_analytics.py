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

"""
Tests for bovnar.analytics.

Pure-Python tests (no libbvnr.so dependency):
  - dim_label
  - filter_by_dim_name
  - benchmark_df / run_benchmark error paths
  - assert_node with mock objects
  - check_schema with mock DomDoc
  - dom_summary with mock DomDoc
  - _human_bytes formatting (via benchmark_df indirectly)

Integration tests (require libbvnr.so, marked needs_lib):
  - doc_to_dataframe with a real parsed document
  - assert_node against real DomNode objects
  - dom_summary against a real DomDoc
  - check_schema against a real DomDoc
  - filter_by_dim_name on a real doc_to_dataframe result
"""

from __future__ import annotations

import math
import subprocess
from pathlib import Path
from unittest.mock import MagicMock, patch

import pytest

# bovnar.analytics is an optional module that may not ship in every build.
# Skip the whole file rather than failing collection when it's absent.
analytics = pytest.importorskip("bovnar.analytics")

SI_DIM_NAMES        = analytics.SI_DIM_NAMES
ALL_PROFILES        = analytics.ALL_PROFILES
NodeAssertionError  = analytics.NodeAssertionError
_human_bytes        = analytics._human_bytes
assert_node         = analytics.assert_node
benchmark_df        = analytics.benchmark_df
check_schema        = analytics.check_schema
dim_label           = analytics.dim_label
dom_summary         = analytics.dom_summary
filter_by_dim_name  = analytics.filter_by_dim_name


# ---------------------------------------------------------------------------
# Mock infrastructure for library-free tests
# ---------------------------------------------------------------------------

class _MockDomType:
    def __init__(self, name: str) -> None:
        self.name = name

    def __eq__(self, other: object) -> bool:
        return isinstance(other, _MockDomType) and self.name == other.name

    def __hash__(self) -> int:
        return hash(self.name)


class _MockNode:
    """Minimal DomNode stand-in for unit tests."""

    def __init__(
        self,
        type_name: str,
        unit_str: str = '',
        value_si: float | None = None,
    ) -> None:
        self.dom_type = _MockDomType(type_name)
        self.unit_str = unit_str
        self._value_si = value_si

    def value_in_base_units(self) -> float:
        if self._value_si is None:
            raise ValueError('No SI value on this mock node')
        return self._value_si

    def to_python(self) -> object:
        return self._value_si


class _MockDoc:
    """Minimal DomDoc stand-in for unit tests."""

    def __init__(self, entries: dict[str, _MockNode]) -> None:
        self._entries = entries

    def __contains__(self, key: str) -> bool:
        return key in self._entries

    def __getitem__(self, key: str) -> _MockNode:
        return self._entries[key]

    def __iter__(self):
        return iter(self._entries.items())


# ---------------------------------------------------------------------------
# dim_label
# ---------------------------------------------------------------------------

class TestDimLabel:
    def test_named_velocity(self):
        assert dim_label([1, 0, -1, 0, 0, 0, 0]) == 'velocity'

    def test_named_force(self):
        assert dim_label([1, 1, -2, 0, 0, 0, 0]) == 'force'

    def test_named_temperature(self):
        assert dim_label([0, 0, 0, 0, 1, 0, 0]) == 'temperature'

    def test_named_dimensionless(self):
        assert dim_label([0, 0, 0, 0, 0, 0, 0]) == 'dimensionless'

    def test_named_frequency(self):
        assert dim_label([0, 0, -1, 0, 0, 0, 0]) == 'frequency'

    def test_named_pressure(self):
        assert dim_label([-1, 1, -2, 0, 0, 0, 0]) == 'pressure'

    def test_unknown_produces_algebraic(self):
        result = dim_label([2, 0, -2, 0, 0, 0, 0])
        assert 'm' in result
        assert 's' in result
        assert '²' in result or '^' in result or '-' in result

    def test_single_dim(self):
        result = dim_label([1, 0, 0, 0, 0, 0, 0])
        assert result == 'length'

    def test_accepts_float_input(self):
        result = dim_label([1.0, 0.0, -1.0, 0.0, 0.0, 0.0, 0.0])
        assert result == 'velocity'

    def test_si_dim_names_length(self):
        assert len(SI_DIM_NAMES) == 7


# ---------------------------------------------------------------------------
# filter_by_dim_name
# ---------------------------------------------------------------------------

class TestFilterByDimName:
    @pytest.fixture
    def sample_df(self):
        import pandas as pd
        return pd.DataFrame([
            {'path': 'speed',    'dims': [1, 0, -1, 0, 0, 0, 0], 'value_si': 9.81},
            {'path': 'temp',     'dims': [0, 0, 0, 0, 1, 0, 0],  'value_si': 300.0},
            {'path': 'force',    'dims': [1, 1, -2, 0, 0, 0, 0], 'value_si': 98.0},
            {'path': 'velocity2','dims': [1, 0, -1, 0, 0, 0, 0], 'value_si': 3.0},
        ])

    def test_filter_velocity(self, sample_df):
        result = filter_by_dim_name(sample_df, 'velocity')
        assert len(result) == 2
        assert set(result['path']) == {'speed', 'velocity2'}

    def test_filter_temperature(self, sample_df):
        result = filter_by_dim_name(sample_df, 'temperature')
        assert len(result) == 1
        assert result.iloc[0]['path'] == 'temp'

    def test_filter_empty_result(self, sample_df):
        result = filter_by_dim_name(sample_df, 'energy')
        assert len(result) == 0

    def test_unknown_dim_raises_key_error(self, sample_df):
        with pytest.raises(KeyError, match='Unknown dimension'):
            filter_by_dim_name(sample_df, 'flux_capacitance')

    def test_returns_copy(self, sample_df):
        result = filter_by_dim_name(sample_df, 'velocity')
        result.loc[result.index[0], 'value_si'] = 999.0
        assert sample_df.loc[0, 'value_si'] == 9.81


# ---------------------------------------------------------------------------
# benchmark_df
# ---------------------------------------------------------------------------

class TestBenchmarkDf:
    @pytest.fixture
    def raw_records(self):
        return [
            {
                'profile': 'scalars', 'bytes': 1024,  'assignments': 50,
                'events': 200, 'iterations': 100,
                'wall_ms': 5.0, 'cpu_ms': 4.8,
                'mb_per_sec': 200.0, 'ass_per_sec': 1_000_000.0,
                'ev_per_sec': 4_000_000.0, 'wall_per_iter_us': 50.0,
            },
            {
                'profile': 'arrays', 'bytes': 4096,   'assignments': 10,
                'events': 80,  'iterations': 100,
                'wall_ms': 10.0, 'cpu_ms': 9.5,
                'mb_per_sec': 400.0, 'ass_per_sec': 100_000.0,
                'ev_per_sec': 800_000.0, 'wall_per_iter_us': 100.0,
            },
            {
                'profile': 'typed', 'bytes': 1024,   'assignments': 30,
                'events': 150, 'iterations': 100,
                'wall_ms': 4.0, 'cpu_ms': 3.9,
                'mb_per_sec': 250.0, 'ass_per_sec': 750_000.0,
                'ev_per_sec': 3_750_000.0, 'wall_per_iter_us': 40.0,
            },
        ]

    def test_columns_present(self, raw_records):
        df = benchmark_df(raw_records)
        for col in ('profile', 'bytes', 'wall_ms', 'mb_per_sec'):
            assert col in df.columns

    def test_profile_categorical(self, raw_records):
        import pandas as pd
        df = benchmark_df(raw_records)
        assert isinstance(df['profile'].dtype, pd.CategoricalDtype)

    def test_canonical_profile_order(self, raw_records):
        df = benchmark_df(raw_records)
        cats = list(df['profile'].cat.categories)
        assert cats.index('scalars') < cats.index('typed')
        assert cats.index('typed')   < cats.index('arrays')

    def test_sorted_by_profile_then_bytes(self, raw_records):
        df = benchmark_df(raw_records)
        profiles = list(df['profile'])
        assert profiles == sorted(profiles, key=lambda p: ALL_PROFILES.index(p))

    def test_empty_input(self):
        df = benchmark_df([])
        assert df.empty

    def test_single_record(self, raw_records):
        df = benchmark_df([raw_records[0]])
        assert len(df) == 1


# ---------------------------------------------------------------------------
# _human_bytes (internal, tested via the function directly)
# ---------------------------------------------------------------------------

class TestHumanBytes:
    def test_bytes(self):
        assert _human_bytes(512) == '512 B'

    def test_kibibytes_exact(self):
        assert _human_bytes(1024) == '1 KiB'

    def test_kibibytes_fraction(self):
        result = _human_bytes(1536)
        assert 'KiB' in result
        assert '1.5' in result

    def test_mebibytes_exact(self):
        assert _human_bytes(4 * 1024 * 1024) == '4 MiB'

    def test_gibibytes(self):
        assert _human_bytes(1024 ** 3) == '1 GiB'


# ---------------------------------------------------------------------------
# assert_node (mock-based)
# ---------------------------------------------------------------------------

class TestAssertNode:
    def test_passes_on_matching_type(self):
        node = _MockNode('FLOAT', value_si=9.81)
        assert_node(node, expected_type='float')

    def test_passes_case_insensitive(self):
        node = _MockNode('INT', value_si=42)
        assert_node(node, expected_type='INT')
        assert_node(node, expected_type='int')

    def test_fails_wrong_type(self):
        node = _MockNode('FLOAT', value_si=1.0)
        with pytest.raises(NodeAssertionError, match='Expected dom_type'):
            assert_node(node, expected_type='int')

    def test_passes_matching_unit(self):
        node = _MockNode('FLOAT', unit_str='m/s', value_si=9.81)
        assert_node(node, expected_unit='m/s')

    def test_fails_wrong_unit(self):
        node = _MockNode('FLOAT', unit_str='m/s', value_si=9.81)
        with pytest.raises(NodeAssertionError, match='Expected unit'):
            assert_node(node, expected_unit='m/s²')

    def test_passes_si_within_tolerance(self):
        node = _MockNode('FLOAT', value_si=9.81)
        assert_node(node, expected_si=9.81, tol=1e-9)

    def test_passes_si_at_exact_tolerance_boundary(self):
        node = _MockNode('FLOAT', value_si=9.81 + 1e-10)
        assert_node(node, expected_si=9.81, tol=1e-9)

    def test_fails_si_outside_tolerance(self):
        node = _MockNode('FLOAT', value_si=9.82)
        with pytest.raises(NodeAssertionError, match='Expected SI value'):
            assert_node(node, expected_si=9.81, tol=1e-4)

    def test_path_appears_in_error_message(self):
        node = _MockNode('FLOAT', value_si=0.0)
        with pytest.raises(NodeAssertionError, match="'radio.freq'"):
            assert_node(node, expected_type='int', path='radio.freq')

    def test_all_checks_combined_pass(self):
        node = _MockNode('FLOAT', unit_str='m/s', value_si=9.81)
        assert_node(node, expected_type='float', expected_unit='m/s',
                    expected_si=9.81)

    def test_no_checks_always_passes(self):
        node = _MockNode('STRUCT')
        assert_node(node)


# ---------------------------------------------------------------------------
# check_schema (mock DomDoc)
# ---------------------------------------------------------------------------

class TestCheckSchema:
    @pytest.fixture
    def doc(self):
        return _MockDoc({
            'temperature': _MockNode('FLOAT', unit_str='°C', value_si=300.0),
            'pressure':    _MockNode('FLOAT', unit_str='Pa', value_si=101325.0),
            'label':       _MockNode('STRING'),
            'sensor_id':   _MockNode('INT', value_si=7.0),
        })

    def test_valid_doc_returns_empty_list(self, doc):
        schema = {
            'temperature': {'type': 'FLOAT', 'unit_str': '°C'},
            'pressure':    {'type': 'FLOAT'},
        }
        assert check_schema(doc, schema) == []

    def test_missing_required_key(self, doc):
        schema = {'missing_key': {'required': True}}
        errs = check_schema(doc, schema)
        assert any('missing_key' in e for e in errs)

    def test_optional_missing_key_no_error(self, doc):
        schema = {'nonexistent': {'required': False}}
        assert check_schema(doc, schema) == []

    def test_wrong_type_reported(self, doc):
        schema = {'temperature': {'type': 'INT'}}
        errs = check_schema(doc, schema)
        assert any('temperature' in e and 'INT' in e for e in errs)

    def test_wrong_unit_reported(self, doc):
        schema = {'pressure': {'unit_str': 'bar'}}
        errs = check_schema(doc, schema)
        assert any('pressure' in e and 'bar' in e for e in errs)

    def test_min_si_violation(self, doc):
        schema = {'temperature': {'min_si': 400.0}}
        errs = check_schema(doc, schema)
        assert any('temperature' in e and 'below minimum' in e for e in errs)

    def test_max_si_violation(self, doc):
        schema = {'temperature': {'max_si': 200.0}}
        errs = check_schema(doc, schema)
        assert any('temperature' in e and 'above maximum' in e for e in errs)

    def test_si_within_range_no_error(self, doc):
        schema = {'temperature': {'min_si': 250.0, 'max_si': 350.0}}
        assert check_schema(doc, schema) == []

    def test_multiple_errors_all_reported(self, doc):
        schema = {
            'missing_key':  {'required': True},
            'temperature':  {'type': 'INT'},
        }
        errs = check_schema(doc, schema)
        assert len(errs) >= 2

    def test_type_check_case_insensitive(self, doc):
        schema = {'temperature': {'type': 'float'}}
        assert check_schema(doc, schema) == []


# ---------------------------------------------------------------------------
# dom_summary (mock DomDoc)
# ---------------------------------------------------------------------------

class TestDomSummary:
    @pytest.fixture
    def doc(self):
        return _MockDoc({
            'freq':  _MockNode('FLOAT', unit_str='Hz',  value_si=868_100_000.0),
            'count': _MockNode('INT',   unit_str='',    value_si=42.0),
            'label': _MockNode('STRING'),
        })

    def test_row_count(self, doc):
        df = dom_summary(doc)
        assert len(df) == 3

    def test_columns(self, doc):
        df = dom_summary(doc)
        assert list(df.columns) == ['key', 'dom_type', 'unit_str', 'value_si', 'value_py']

    def test_keys_preserved(self, doc):
        df = dom_summary(doc)
        assert set(df['key']) == {'freq', 'count', 'label'}

    def test_numeric_value_si(self, doc):
        df = dom_summary(doc)
        row = df[df['key'] == 'freq'].iloc[0]
        assert row['value_si'] == pytest.approx(868_100_000.0)

    def test_non_numeric_value_si_is_nan(self, doc):
        df = dom_summary(doc)
        row = df[df['key'] == 'label'].iloc[0]
        assert math.isnan(row['value_si'])

    def test_unit_str_empty_for_string_node(self, doc):
        df = dom_summary(doc)
        row = df[df['key'] == 'label'].iloc[0]
        assert row['unit_str'] == ''


# ---------------------------------------------------------------------------
# Integration tests — require libbvnr.so
# ---------------------------------------------------------------------------

try:
    from conftest import needs_lib
except ImportError:
    needs_lib = pytest.mark.skip(reason='conftest not importable outside pytest run')


SENSOR_DOC = (
    b".node_id   = <uint:8>         7;\n"
    b".frequency = <float:64,M~Hz>  868.1;\n"
    b".tx_power  = <sint:8,dBm>    14;\n"
    b".interval  = <uint:32,s>       60;\n"
    b".offset    = <float_fix:32,q8,\xc2\xb0\x43> -0.5;\n"
    b".config = {\n"
    b"    .host    = \"sensor.local\";\n"
    b"    .timeout = <float:64,s>   2.5;\n"
    b"};\n"
)

TEMP_DOC = (
    b".temperature = <float:64,K>     300.0;\n"
    b".pressure    = <float:64,k~Pa>  101.325;\n"
    b".velocity    = <float:64,m/s>   9.81;\n"
    b".energy      = <float:64,k~J>   5400.0;\n"
    b".label       = \"run-001\";\n"
)


@needs_lib
class TestDocToDataframeIntegration:
    @pytest.fixture
    def df(self):
        import bovnar
        from bovnar.analytics import doc_to_dataframe
        doc = bovnar.dom_parse(TEMP_DOC)
        return doc_to_dataframe(doc)

    def test_only_numeric_nodes(self, df):
        assert set(df['dom_type']).issubset({'INT', 'FLOAT'})

    def test_expected_paths(self, df):
        assert set(df['path']) >= {'temperature', 'pressure', 'velocity', 'energy'}

    def test_velocity_dims(self, df):
        row = df[df['path'] == 'velocity'].iloc[0]
        assert row['dims'] == [1, 0, -1, 0, 0, 0, 0]

    def test_temperature_dims(self, df):
        row = df[df['path'] == 'temperature'].iloc[0]
        assert row['dims'] == [0, 0, 0, 0, 1, 0, 0]

    def test_pressure_si_in_pascals(self, df):
        row = df[df['path'] == 'pressure'].iloc[0]
        assert row['value_si'] == pytest.approx(101_325.0, rel=1e-5)

    def test_filter_velocity(self, df):
        result = filter_by_dim_name(df, 'velocity')
        assert len(result) == 1
        assert result.iloc[0]['path'] == 'velocity'

    def test_filter_temperature(self, df):
        result = filter_by_dim_name(df, 'temperature')
        assert len(result) == 1


@needs_lib
class TestAssertNodeIntegration:
    @pytest.fixture
    def doc(self):
        import bovnar
        return bovnar.dom_parse(SENSOR_DOC)

    def test_frequency_type_and_unit(self, doc):
        node = doc['frequency']
        assert_node(node, expected_type='float', expected_unit='M~Hz',
                    path='frequency')

    def test_frequency_si_value(self, doc):
        node = doc['frequency']
        assert_node(node, expected_si=868_100_000.0, tol=1.0,
                    path='frequency')

    def test_node_id_type(self, doc):
        node = doc['node_id']
        assert_node(node, expected_type='int', path='node_id')

    def test_wrong_type_raises(self, doc):
        with pytest.raises(NodeAssertionError):
            assert_node(doc['frequency'], expected_type='int')

    def test_wrong_unit_raises(self, doc):
        with pytest.raises(NodeAssertionError):
            assert_node(doc['frequency'], expected_unit='Hz')


@needs_lib
class TestDomSummaryIntegration:
    @pytest.fixture
    def doc(self):
        import bovnar
        return bovnar.dom_parse(TEMP_DOC)

    def test_row_count_matches_top_level_keys(self, doc):
        from bovnar.analytics import dom_summary
        df = dom_summary(doc)
        assert len(df) == 5

    def test_numeric_rows_have_si_values(self, doc):
        from bovnar.analytics import dom_summary
        df = dom_summary(doc)
        numeric = df[df['dom_type'].isin(['INT', 'FLOAT'])]
        assert not numeric['value_si'].isna().any()

    def test_string_row_has_nan_si(self, doc):
        from bovnar.analytics import dom_summary
        df = dom_summary(doc)
        row = df[df['key'] == 'label'].iloc[0]
        assert math.isnan(row['value_si'])


@needs_lib
class TestCheckSchemaIntegration:
    @pytest.fixture
    def doc(self):
        import bovnar
        return bovnar.dom_parse(TEMP_DOC)

    def test_valid_schema_passes(self, doc):
        schema = {
            'temperature': {'type': 'FLOAT', 'min_si': 0.0, 'max_si': 1000.0},
            'pressure':    {'type': 'FLOAT', 'min_si': 80_000.0},
            'label':       {'type': 'STRING'},
        }
        assert check_schema(doc, schema) == []

    def test_missing_required_key(self, doc):
        schema = {'humidity': {'required': True}}
        errs = check_schema(doc, schema)
        assert len(errs) == 1
        assert 'humidity' in errs[0]

    def test_si_range_violation(self, doc):
        schema = {'temperature': {'max_si': 200.0}}
        errs = check_schema(doc, schema)
        assert len(errs) == 1
        assert 'above maximum' in errs[0]
