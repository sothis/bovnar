# Bovnar — Jupyter / matplotlib Notebooks

**Format version:** 1.3  
**Module:** `bovnar.analytics`  
**Notebook directory:** `notebooks/`

---

## Overview

The `bovnar.analytics` module provides three purpose-built Jupyter workflows on top
of the existing Python bindings.  Each workflow targets a distinct use case:

| Workflow | Notebook | Library required | Primary tools |
|---|---|---|---|
| A — Benchmark analytics | `A_benchmark_analytics.ipynb` | No (binary only) | `run_benchmark`, `benchmark_df`, plot functions |
| B — Unit-aware pipeline | `B_unit_pipeline.ipynb` | Yes | `doc_to_dataframe`, `filter_by_dim_name`, `plot_scalar_series` |
| C — DOM introspection | `C_dom_introspection.ipynb` | Yes | `assert_node`, `dom_summary`, `check_schema` |

---

## Installation

```bash
pip install bovnar[dev]          # bovnar package + dev deps (pytest)
pip install matplotlib pandas    # notebook runtime deps
pip install notebook             # or jupyterlab
```

For workflows B and C, the native library must be on the search path:

```bash
export LIBBOVNAR_PATH=/path/to/build/libbvnr_shared.so
jupyter notebook
```

---

## Workflow A — Benchmark analytics

**File:** `notebooks/A_benchmark_analytics.ipynb`  
**Library dependency:** none — only the `bvnr_bench` binary is needed.

### What it does

Drives the `bvnr_bench` CLI tool with `--json` output, loads the newline-delimited
JSON into a `pandas.DataFrame`, and produces four complementary visualisations:

1. **Throughput bar chart** — MB/s per profile, grouped by payload size, log scale.
2. **Throughput scaling** — line chart on a log₂ x-axis; cache-saturation "knees"
   are visible as inflection points.
3. **Wall vs. CPU scatter** — points on the diagonal are CPU-bound; points above
   indicate scheduling or memory-bus pressure.  Point size scales with payload.
4. **Latency heatmap** — per-iteration wall time in µs, indexed by profile × size.

### Key API

```python
from bovnar.analytics import run_benchmark, benchmark_df, plot_throughput_bars

records = run_benchmark(
    './build/bvnr_bench',
    profiles=('scalars', 'typed', 'units'),
    sizes=(1024, 4096, 16384, 65536),
    iterations=100,
    warmup=10,
)

df = benchmark_df(records)      # ordered Categorical profile column
fig = plot_throughput_bars(df)
```

`run_benchmark` raises `FileNotFoundError` when the binary is absent,
`subprocess.CalledProcessError` on non-zero exit, and `subprocess.TimeoutExpired`
when the `timeout` parameter (default 300 s) elapses.

### Minimum-overhead mode

Pass `min_overhead=True` to `run_benchmark` to add `--min-overhead` to the
invocation; this disables the `on_verified` callback, giving pure lexer
throughput.  Comparing two DataFrames — one with and one without — shows what
fraction of total parse time is spent inside the callback dispatch path.

---

## Workflow B — Unit-aware data pipeline

**File:** `notebooks/B_unit_pipeline.ipynb`  
**Library dependency:** `libbvnr_shared.so`

### What it does

Parses a `.bvnr` document (from a file, bytes, or the `Writer` API), flattens
all numeric leaf nodes into a `pandas.DataFrame` with SI base-unit values and
7-element dimension vectors, then filters and plots measurements by physical
dimension.

This workflow exploits the one feature BVNR offers that JSON, Protobuf, and YAML
do not: **self-describing physical units** that travel with every value.  The
`value_in_base_units()` method normalises e.g. `<float:32,M-Hz> 868.1` to
`868_100_000.0` (Hz) automatically; the axis label comes from `node.unit_str`.

### Key API

```python
import bovnar
from bovnar.analytics import doc_to_dataframe, filter_by_dim_name, plot_scalar_series

doc = bovnar.dom_parse(open('sensor_run.bvnr', 'rb').read())
df  = doc_to_dataframe(doc)

# df columns: path, dom_type, value_si, unit_str, dims
# dims is the 7-element [m, kg, s, A, K, mol, cd] exponent vector

freq_df = filter_by_dim_name(df, 'frequency')   # selects all Hz-compatible fields
fig     = plot_scalar_series(df, group_col='unit_str')
```

### DataFrame schema

| Column | Type | Description |
|---|---|---|
| `path` | `str` | Dot-separated key path, e.g. `'radio.frequency'` |
| `dom_type` | `str` | `'INT'` or `'FLOAT'` |
| `value_si` | `float` | `node.value_in_base_units()` — SI base unit, no prefix |
| `unit_str` | `str` | Canonical unit string, e.g. `'M-Hz'`, `'k-g·m/s²'`, `''` |
| `dims` | `list[int]` | 7-element SI dimension exponent vector |

### Recognised dimension names for `filter_by_dim_name`

`length`, `mass`, `time`, `electric current`, `temperature`,
`amount of substance`, `luminous intensity`, `velocity`, `acceleration`,
`force`, `energy`, `power`, `pressure`, `frequency`, `voltage`, `dimensionless`.

### Unit conversion

```python
from bovnar.units import convert_value

celsius_unit = bovnar.parse_unit("°C")
kelvin_val   = doc["temperature"].value_in_base_units()
celsius_val  = convert_value(kelvin_val, doc["temperature"].unit, celsius_unit)
```

`convert_value` handles both multiplicative and affine conversions (e.g.
Celsius → Kelvin) without manual formula lookup.

---

## Workflow C — DOM introspection

**File:** `notebooks/C_dom_introspection.ipynb`  
**Library dependency:** `libbvnr_shared.so`

### What it does

Provides structural and semantic assertions on parsed documents, a tabular
summary of top-level keys, and a declarative schema-validation function.  All
three are usable in both Jupyter cells and pytest test functions.

### `assert_node`

```python
from bovnar.analytics import assert_node, NodeAssertionError

assert_node(
    doc['frequency'],
    expected_type='float',
    expected_unit='M-Hz',
    expected_si=868_100_000.0,
    tol=1.0,
    path='frequency',           # appears in error messages
)
```

`NodeAssertionError` is a subclass of `AssertionError`; pytest catches it
without any special configuration.

### `dom_summary`

```python
from bovnar.analytics import dom_summary

df = dom_summary(doc)
# columns: key, dom_type, unit_str, value_si (NaN for non-numeric), value_py
```

This is the fastest way to check whether a freshly parsed document looks sane.

### `check_schema`

```python
from bovnar.analytics import check_schema

schema = {
    'node_id':   {'type': 'INT',   'min_si': 0.0, 'max_si': 255.0},
    'frequency': {'type': 'FLOAT', 'unit_str': 'M-Hz',
                  'min_si': 863e6, 'max_si': 870e6},
    'firmware':  {'type': 'STRING'},
    'humidity':  {'required': False},   # optional
}

errors = check_schema(doc, schema)
assert errors == [], f"Schema violations: {errors}"
```

| Schema rule | Key | Description |
|---|---|---|
| `required` | bool | Key must be present (default `True`) |
| `type` | str | `dom_type.name`, case-insensitive |
| `unit_str` | str | Exact unit string match |
| `min_si` | float | Minimum SI base-unit value, inclusive |
| `max_si` | float | Maximum SI base-unit value, inclusive |

---

## Tests

All analytics helpers are covered by `python/tests/test_analytics.py`.

Tests that do **not** require `libbvnr_shared.so` (use mock objects):

- `TestDimLabel` — `dim_label` with known and algebraic vectors
- `TestFilterByDimName` — filtering and KeyError behaviour
- `TestBenchmarkDf` — DataFrame shape, categorical ordering, edge cases
- `TestHumanBytes` — byte formatting
- `TestAssertNode` — all check types, tolerance boundary, path in message
- `TestCheckSchema` — all rule types, multi-error reporting, case insensitivity
- `TestDomSummary` — column names, NaN for non-numeric, unit_str for strings

Integration tests (require `libbvnr_shared.so`, marked `@needs_lib`):

- `TestDocToDataframeIntegration` — real DOM traversal and dimension vectors
- `TestAssertNodeIntegration` — real DomNode type/unit/SI assertions
- `TestDomSummaryIntegration` — real DomDoc summary
- `TestCheckSchemaIntegration` — real DomDoc schema validation

Run only the pure-Python tests:

```bash
pytest python/tests/test_analytics.py -m "not needs_lib"
```

Run everything including integration tests:

```bash
LIBBOVNAR_PATH=/path/to/build/libbvnr_shared.so pytest python/tests/test_analytics.py -v
```

---

## Module reference

```
bovnar.analytics
├── SI_DIM_NAMES            tuple[str, ...] — ('m', 'kg', 's', 'A', 'K', 'mol', 'cd')
├── ALL_PROFILES            tuple[str, ...] — canonical benchmark profile order
│
├── dim_label(dims)         str   — name or algebraic expression for a dim vector
├── filter_by_dim_name(df, name)  pd.DataFrame — filter by named dimension
│
├── run_benchmark(binary, …)      list[dict]   — drive bvnr_bench --json
├── benchmark_df(records)         pd.DataFrame — tidy benchmark DataFrame
├── plot_throughput_bars(df, …)   Figure
├── plot_size_scaling(df, …)      Figure
├── plot_wall_vs_cpu(df, …)       Figure
├── plot_latency_heatmap(df, …)   Figure
│
├── doc_to_dataframe(doc)         pd.DataFrame — flatten DomDoc numeric leaves
├── plot_scalar_series(df, …)     Figure
│
├── NodeAssertionError      AssertionError subclass
├── assert_node(node, …)    void — raises NodeAssertionError on failure
├── dom_summary(doc)        pd.DataFrame — one row per top-level key
└── check_schema(doc, schema)  list[str] — empty = valid
```
