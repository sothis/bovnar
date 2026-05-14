"""
bovnar.analytics
================
Jupyter / matplotlib helpers covering the three notebook workflows:

  A  Benchmark analytics       — drive bvnr_bench --json, plot results
  B  Unit-aware data pipeline  — flatten a DomDoc to a DataFrame, plot SI values
  C  DOM introspection         — structural assertions, summary tables, schema checks

All pandas / matplotlib imports are deferred to function bodies so that
``import bovnar`` remains clean when those packages are absent.  DOM-dependent
functions (doc_to_dataframe, dom_summary, check_schema) require libbvnr_shared.so
at call time; the rest have no native-library dependency.
"""

from __future__ import annotations

import json
import subprocess
from pathlib import Path
from typing import TYPE_CHECKING, Any, Sequence

if TYPE_CHECKING:
    import matplotlib.figure
    import pandas as pd
    from .dom import DomDoc, DomNode


# ---------------------------------------------------------------------------
# Shared SI dimension helpers (no library dependency)
# ---------------------------------------------------------------------------

SI_DIM_NAMES: tuple[str, ...] = ('m', 'kg', 's', 'A', 'K', 'mol', 'cd')

_SUP = str.maketrans('0123456789-', '⁰¹²³⁴⁵⁶⁷⁸⁹⁻')

_KNOWN_DIMS: dict[tuple[int, ...], str] = {
    ( 1,  0,  0,  0, 0, 0, 0): 'length',
    ( 0,  1,  0,  0, 0, 0, 0): 'mass',
    ( 0,  0,  1,  0, 0, 0, 0): 'time',
    ( 0,  0,  0,  1, 0, 0, 0): 'electric current',
    ( 0,  0,  0,  0, 1, 0, 0): 'temperature',
    ( 0,  0,  0,  0, 0, 1, 0): 'amount of substance',
    ( 0,  0,  0,  0, 0, 0, 1): 'luminous intensity',
    ( 1,  0, -1,  0, 0, 0, 0): 'velocity',
    ( 1,  0, -2,  0, 0, 0, 0): 'acceleration',
    ( 1,  1, -2,  0, 0, 0, 0): 'force',
    ( 2,  1, -2,  0, 0, 0, 0): 'energy',
    ( 2,  1, -3,  0, 0, 0, 0): 'power',
    (-1,  1, -2,  0, 0, 0, 0): 'pressure',
    ( 0,  0, -1,  0, 0, 0, 0): 'frequency',
    ( 2,  1, -3, -1, 0, 0, 0): 'voltage',
    ( 0,  0,  0,  0, 0, 0, 0): 'dimensionless',
}

_DIM_BY_NAME: dict[str, tuple[int, ...]] = {v: k for k, v in _KNOWN_DIMS.items()}


def dim_label(dims: Sequence[int]) -> str:
    """Return a human-readable name or algebraic expression for a dimension vector.

    *dims* must be a 7-element sequence [m, kg, s, A, K, mol, cd].

    Named dimensions (e.g. 'velocity', 'force') are returned for known vectors;
    unknown combinations produce a Unicode algebraic string such as ``'m²·s⁻²'``.

    >>> dim_label([1, 0, -1, 0, 0, 0, 0])
    'velocity'
    >>> dim_label([2, 0, -2, 0, 0, 0, 0])
    'm²·s⁻²'
    >>> dim_label([0, 0, 0, 0, 0, 0, 0])
    'dimensionless'
    """
    key = tuple(int(d) for d in dims)
    if key in _KNOWN_DIMS:
        return _KNOWN_DIMS[key]
    parts: list[str] = []
    for sym, exp in zip(SI_DIM_NAMES, key):
        if exp == 0:
            continue
        sup = '' if exp == 1 else str(exp).translate(_SUP)
        parts.append(f'{sym}{sup}')
    return '·'.join(parts) if parts else 'dimensionless'


def filter_by_dim_name(df: 'pd.DataFrame', dim_name: str) -> 'pd.DataFrame':
    """Filter a :func:`doc_to_dataframe` result to rows matching *dim_name*.

    Raises ``KeyError`` for unknown dimension names.  For a full list of
    recognised names see ``_DIM_BY_NAME`` or the analytics module docstring.
    """
    if dim_name not in _DIM_BY_NAME:
        known = ', '.join(sorted(_DIM_BY_NAME))
        raise KeyError(
            f"Unknown dimension name {dim_name!r}.  Known: {known}"
        )
    target = list(_DIM_BY_NAME[dim_name])
    return df[df['dims'].apply(lambda d: d == target)].copy()


# ---------------------------------------------------------------------------
# Option A — Benchmark analytics
# ---------------------------------------------------------------------------

ALL_PROFILES: tuple[str, ...] = (
    'scalars', 'typed', 'structs', 'arrays', 'units', 'mixed'
)

_DEFAULT_SIZES: tuple[int, ...] = (1024, 4096, 16384, 65536)

_METRIC_LABELS: dict[str, str] = {
    'mb_per_sec':        'Throughput (MB/s)',
    'ass_per_sec':       'Assignments/s',
    'ev_per_sec':        'Events/s',
    'wall_ms':           'Wall time (ms)',
    'cpu_ms':            'CPU time (ms)',
    'wall_per_iter_us':  'Latency (µs/iter)',
}


def run_benchmark(
    binary: str | Path,
    profiles: Sequence[str] = ALL_PROFILES,
    sizes: Sequence[int] = _DEFAULT_SIZES,
    iterations: int = 100,
    warmup: int = 10,
    min_overhead: bool = False,
    timeout: float = 300.0,
) -> list[dict[str, Any]]:
    """Execute *binary* (the ``bvnr_bench`` CLI tool) and return JSON records.

    The binary is invoked with ``--json``; each ``{...}`` line on stdout
    becomes one dict.  The surrounding ``{"config":...,"results":[...]}``
    wrapper is ignored — only the per-result lines are returned.

    Raises
    ------
    FileNotFoundError
        When *binary* does not exist on the filesystem.
    subprocess.CalledProcessError
        On a non-zero exit code.
    subprocess.TimeoutExpired
        When execution exceeds *timeout* seconds.
    """
    cmd: list[str] = [
        str(binary),
        '--json',
        '--profile', ','.join(profiles),
        '--size',    ','.join(str(s) for s in sizes),
        '--iterations', str(iterations),
        '--warmup',     str(warmup),
    ]
    if min_overhead:
        cmd.append('--min-overhead')

    proc = subprocess.run(cmd, capture_output=True, check=True, timeout=timeout)

    records: list[dict[str, Any]] = []
    for raw_line in proc.stdout.splitlines():
        line = raw_line.strip()
        if line.startswith(b'{') and line.endswith(b'}'):
            records.append(json.loads(line))
    return records


def benchmark_df(records: list[dict[str, Any]]) -> 'pd.DataFrame':
    """Convert :func:`run_benchmark` output to a tidy ``pandas.DataFrame``.

    The ``'profile'`` column is an ordered ``Categorical`` following the
    canonical profile order so that plots respect the natural ordering.
    """
    import pandas as pd

    df = pd.DataFrame(records)
    if df.empty:
        return df
    present = [p for p in ALL_PROFILES if p in df['profile'].unique()]
    df['profile'] = pd.Categorical(df['profile'], categories=present, ordered=True)
    return df.sort_values(['profile', 'bytes']).reset_index(drop=True)


def plot_throughput_bars(
    df: 'pd.DataFrame',
    metric: str = 'mb_per_sec',
    log_scale: bool = True,
    figsize: tuple[int, int] = (12, 5),
) -> 'matplotlib.figure.Figure':
    """Grouped bar chart: *metric* per profile, one group per payload size."""
    import matplotlib.pyplot as plt

    pivot = df.pivot_table(
        index='bytes', columns='profile', values=metric, aggfunc='mean'
    )
    fig, ax = plt.subplots(figsize=figsize)
    pivot.plot(kind='bar', ax=ax, width=0.75)
    ax.set_xlabel('Payload size')
    ax.set_ylabel(_METRIC_LABELS.get(metric, metric))
    ax.set_title(f'BVNR parse benchmark — {_METRIC_LABELS.get(metric, metric)}')
    ax.legend(title='Profile', bbox_to_anchor=(1.02, 1), loc='upper left')
    if log_scale:
        ax.set_yscale('log')
    sizes = sorted(df['bytes'].unique())
    ax.set_xticklabels([_human_bytes(s) for s in sizes], rotation=0)
    fig.tight_layout()
    return fig


def plot_size_scaling(
    df: 'pd.DataFrame',
    metric: str = 'mb_per_sec',
    figsize: tuple[int, int] = (10, 5),
) -> 'matplotlib.figure.Figure':
    """Line chart: *metric* vs. payload size, one line per profile."""
    import matplotlib.pyplot as plt

    fig, ax = plt.subplots(figsize=figsize)
    for profile, group in df.groupby('profile', observed=True):
        g = group.sort_values('bytes')
        ax.plot(g['bytes'], g[metric], marker='o', label=profile)
    ax.set_xscale('log', base=2)
    ax.set_xlabel('Payload size')
    ax.set_ylabel(_METRIC_LABELS.get(metric, metric))
    ax.set_title(f'BVNR throughput scaling — {_METRIC_LABELS.get(metric, metric)}')
    ax.legend(title='Profile')
    ax.xaxis.set_major_formatter(
        plt.FuncFormatter(lambda x, _: _human_bytes(int(x)))
    )
    fig.tight_layout()
    return fig


def plot_wall_vs_cpu(
    df: 'pd.DataFrame',
    figsize: tuple[int, int] = (7, 7),
) -> 'matplotlib.figure.Figure':
    """Scatter: wall time vs. CPU time.  Points on the diagonal are CPU-bound."""
    import matplotlib.pyplot as plt
    import numpy as np

    fig, ax = plt.subplots(figsize=figsize)
    for profile, group in df.groupby('profile', observed=True):
        ax.scatter(
            group['cpu_ms'], group['wall_ms'],
            s=group['bytes'] / 40,
            label=profile, alpha=0.75,
        )
    xlim = ax.get_xlim()
    diag = np.linspace(0, xlim[1], 100)
    ax.plot(diag, diag, 'k--', linewidth=0.8, label='wall = CPU')
    ax.set_xlabel('CPU time (ms)')
    ax.set_ylabel('Wall time (ms)')
    ax.set_title('Wall time vs. CPU time\n(point size ∝ payload)')
    ax.legend(title='Profile', bbox_to_anchor=(1.02, 1), loc='upper left')
    fig.tight_layout()
    return fig


def plot_latency_heatmap(
    df: 'pd.DataFrame',
    metric: str = 'wall_per_iter_us',
    figsize: tuple[int, int] = (9, 4),
) -> 'matplotlib.figure.Figure':
    """Heatmap of per-iteration latency indexed by profile × payload size."""
    import matplotlib.pyplot as plt
    import numpy as np

    pivot = df.pivot_table(
        index='profile', columns='bytes', values=metric, aggfunc='mean'
    )
    fig, ax = plt.subplots(figsize=figsize)
    im = ax.imshow(pivot.values, aspect='auto', cmap='YlOrRd')
    fig.colorbar(im, ax=ax, label=_METRIC_LABELS.get(metric, metric))
    ax.set_xticks(range(len(pivot.columns)))
    ax.set_xticklabels([_human_bytes(c) for c in pivot.columns])
    ax.set_yticks(range(len(pivot.index)))
    ax.set_yticklabels(list(pivot.index))
    ax.set_title('Per-iteration latency heatmap (µs)')
    for r in range(pivot.shape[0]):
        for c in range(pivot.shape[1]):
            v = pivot.values[r, c]
            if not np.isnan(v):
                ax.text(c, r, f'{v:.1f}', ha='center', va='center',
                        fontsize=8, color='black')
    fig.tight_layout()
    return fig


# ---------------------------------------------------------------------------
# Option B — Unit-aware data pipeline
# ---------------------------------------------------------------------------

def doc_to_dataframe(doc: 'DomDoc') -> 'pd.DataFrame':
    """Flatten all numeric leaf nodes of *doc* into a ``pandas.DataFrame``.

    Struct nesting is reflected in the ``'path'`` column as a dot-separated
    key chain (e.g. ``'radio.frequency'``).  Array elements are skipped;
    only named scalar assignments of type INT or FLOAT are included.

    Columns
    -------
    path      : str   — dot-separated key path
    dom_type  : str   — ``'INT'`` or ``'FLOAT'``
    value_si  : float — ``node.value_in_base_units()``
    unit_str  : str   — canonical unit string (empty when dimensionless)
    dims      : list[int] — 7-element SI dimension exponent vector
    """
    import pandas as pd
    from .units import unit_dimension_vector

    rows: list[dict[str, Any]] = []
    _walk_doc(doc, '', rows, unit_dimension_vector)
    return pd.DataFrame(rows, columns=['path', 'dom_type', 'value_si',
                                        'unit_str', 'dims'])


def _walk_doc(node_or_doc: Any, prefix: str, rows: list,
              udv: Any) -> None:
    from .dom import DomNode
    from .units import unit_dimension_vector as _udv

    if isinstance(node_or_doc, DomNode):
        dt_name = node_or_doc.dom_type.name
        if dt_name in ('INT', 'FLOAT'):
            try:
                dims = udv(node_or_doc.unit)
            except Exception:
                dims = [0] * 7
            rows.append({
                'path':     prefix,
                'dom_type': dt_name,
                'value_si': node_or_doc.value_in_base_units(),
                'unit_str': node_or_doc.unit_str,
                'dims':     dims,
            })
        elif dt_name == 'STRUCT':
            for key, child in node_or_doc.entries():
                child_path = f'{prefix}.{key}' if prefix else key
                _walk_doc(child, child_path, rows, udv)
    else:
        for key, child in node_or_doc:
            _walk_doc(child, key, rows, udv)


def plot_scalar_series(
    df: 'pd.DataFrame',
    x_col: str = 'path',
    y_col: str = 'value_si',
    group_col: str | None = 'unit_str',
    figsize: tuple[int, int] = (10, 5),
    title: str = 'BVNR scalar measurements',
) -> 'matplotlib.figure.Figure':
    """Bar chart of SI values from a :func:`doc_to_dataframe` result.

    When *group_col* is given, one subplot per distinct group value is produced
    so that measurements in different units sit on separate axes.
    """
    import matplotlib.pyplot as plt

    if group_col and group_col in df.columns:
        groups = list(df[group_col].unique())
        n = len(groups)
        fig, axes = plt.subplots(n, 1,
                                  figsize=(figsize[0], figsize[1] * max(n, 1)),
                                  squeeze=False)
        for ax, grp in zip(axes[:, 0], groups):
            sub = df[df[group_col] == grp]
            labels = sub[x_col].str.replace('_', ' ', regex=False)
            ax.bar(labels, sub[y_col])
            ax.set_ylabel(f'SI value [{grp}]' if grp else 'SI value')
            ax.set_title(str(grp) if grp else 'no unit')
            ax.tick_params(axis='x', rotation=30)
    else:
        fig, ax = plt.subplots(figsize=figsize)
        labels = df[x_col].str.replace('_', ' ', regex=False)
        ax.bar(labels, df[y_col])
        ax.set_ylabel('SI value')
        ax.tick_params(axis='x', rotation=30)

    fig.suptitle(title)
    fig.tight_layout()
    return fig


# ---------------------------------------------------------------------------
# Option C — DOM introspection
# ---------------------------------------------------------------------------

class NodeAssertionError(AssertionError):
    """Raised by :func:`assert_node` on a failed structural or semantic check."""


def assert_node(
    node: 'DomNode',
    *,
    expected_type: str | None = None,
    expected_unit: str | None = None,
    expected_si: float | None = None,
    tol: float = 1e-9,
    path: str = '',
) -> None:
    """Assert structural and semantic properties of a DOM node.

    Parameters
    ----------
    node:
        The ``DomNode`` to inspect.
    expected_type:
        When given, ``node.dom_type.name`` must match (case-insensitive).
    expected_unit:
        When given, ``node.unit_str`` must match exactly.
    expected_si:
        When given, ``node.value_in_base_units()`` must be within *tol*.
    tol:
        Absolute tolerance for *expected_si* (default ``1e-9``).
    path:
        Human-readable location for error messages, e.g. ``'radio.frequency'``.

    Raises :class:`NodeAssertionError` on the first failed check.
    """
    loc = f' at {path!r}' if path else ''

    if expected_type is not None:
        actual = node.dom_type.name.lower()
        if actual != expected_type.lower():
            raise NodeAssertionError(
                f'Expected dom_type {expected_type!r}{loc}, got {actual!r}'
            )

    if expected_unit is not None:
        actual_u = node.unit_str
        if actual_u != expected_unit:
            raise NodeAssertionError(
                f'Expected unit {expected_unit!r}{loc}, got {actual_u!r}'
            )

    if expected_si is not None:
        actual_si = node.value_in_base_units()
        if abs(actual_si - expected_si) > tol:
            raise NodeAssertionError(
                f'Expected SI value {expected_si} ± {tol}{loc}, '
                f'got {actual_si}'
            )


def dom_summary(doc: 'DomDoc') -> 'pd.DataFrame':
    """Return a DataFrame with one row per top-level key of *doc*.

    Columns: ``key``, ``dom_type``, ``unit_str``, ``value_si`` (NaN for
    non-numeric nodes), ``value_py`` (Python-native via ``to_python()``).
    """
    import math
    import pandas as pd

    rows: list[dict[str, Any]] = []
    for key, node in doc:
        dt_name = node.dom_type.name
        numeric = dt_name in ('INT', 'FLOAT')
        try:
            vsi: float = node.value_in_base_units() if numeric else math.nan
        except Exception:
            vsi = math.nan
        try:
            vpy: Any = node.to_python()
        except Exception:
            vpy = None
        rows.append({
            'key':      key,
            'dom_type': dt_name,
            'unit_str': node.unit_str if numeric else '',
            'value_si': vsi,
            'value_py': vpy,
        })
    return pd.DataFrame(rows, columns=['key', 'dom_type', 'unit_str',
                                        'value_si', 'value_py'])


_SchemaEntry = dict[str, Any]


def check_schema(
    doc: 'DomDoc',
    schema: dict[str, _SchemaEntry],
) -> list[str]:
    """Validate *doc* against *schema*; return a list of error strings.

    An empty list means the document satisfies every rule.

    Schema entry keys
    -----------------
    ``'required'``  : bool (default ``True``) — key must be present.
    ``'type'``      : str — expected ``dom_type.name`` (case-insensitive).
    ``'unit_str'``  : str — exact expected unit string.
    ``'min_si'``    : float — minimum SI base-unit value (inclusive).
    ``'max_si'``    : float — maximum SI base-unit value (inclusive).
    """
    errors: list[str] = []

    for key, rules in schema.items():
        required = rules.get('required', True)
        if key not in doc:
            if required:
                errors.append(f'Missing required key {key!r}')
            continue

        node = doc[key]
        dt_name = node.dom_type.name

        if 'type' in rules:
            if dt_name.lower() != rules['type'].lower():
                errors.append(
                    f'{key!r}: expected type {rules["type"]!r}, '
                    f'got {dt_name!r}'
                )

        if 'unit_str' in rules:
            if node.unit_str != rules['unit_str']:
                errors.append(
                    f'{key!r}: expected unit {rules["unit_str"]!r}, '
                    f'got {node.unit_str!r}'
                )

        if dt_name in ('INT', 'FLOAT'):
            try:
                vsi = node.value_in_base_units()
            except Exception:
                vsi = None
            if vsi is not None:
                if 'min_si' in rules and vsi < rules['min_si']:
                    errors.append(
                        f'{key!r}: SI value {vsi} below minimum {rules["min_si"]}'
                    )
                if 'max_si' in rules and vsi > rules['max_si']:
                    errors.append(
                        f'{key!r}: SI value {vsi} above maximum {rules["max_si"]}'
                    )

    return errors


# ---------------------------------------------------------------------------
# Internal helpers
# ---------------------------------------------------------------------------

def _human_bytes(n: int) -> str:
    for suffix, threshold in (('GiB', 1 << 30), ('MiB', 1 << 20), ('KiB', 1 << 10)):
        if n >= threshold:
            v = n / threshold
            return f'{v:.0f} {suffix}' if v == int(v) else f'{v:.1f} {suffix}'
    return f'{n} B'


__all__ = [
    'SI_DIM_NAMES',
    'dim_label',
    'filter_by_dim_name',
    'ALL_PROFILES',
    'run_benchmark',
    'benchmark_df',
    'plot_throughput_bars',
    'plot_size_scaling',
    'plot_wall_vs_cpu',
    'plot_latency_heatmap',
    'doc_to_dataframe',
    'plot_scalar_series',
    'NodeAssertionError',
    'assert_node',
    'dom_summary',
    'check_schema',
]
