# SPDX-License-Identifier: MIT
#
# Copyright (c) 2026 Janos Sonntag
#
# ABI-consistency sweep: every ctypes Structure in the Python binding must
# mirror the C struct it stands in for. The C compiler is the single source of
# truth — tests/bvnr_abi_dump.c prints the real sizeof/offsetof, and this test
# asserts the ctypes layout agrees. A C struct change (e.g. appending a field,
# as the spec-1.1 frac_data/frac_length were) then fails here loudly instead of
# silently desyncing the binding and reading out of bounds at runtime.
#
# Rules:
#   * field-mirrored structs  -> exact size AND every field offset/size
#   * opaque pass-through blobs (bvnr_source_t / bvnr_sink_t) -> the binding may
#     over-allocate but must be >= the C size (else C writes out of bounds).

import ctypes
import os
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

from bovnar import structs as S

_REPO = Path(__file__).resolve().parents[2]

# ctypes Structure  ->  (C typedef name, is_opaque)
_MIRRORS = {
    S.ValueTypeSpec:      ("value_type_spec_t", False),
    S.ValueUnitPrefix:    ("value_unit_prefix_t", False),
    S.ValueUnitComponent: ("value_unit_component_t", False),
    S.ValueUnit:          ("value_unit_t", False),
    S.BvnrConverted:      ("bvnr_converted_t", False),
    S.BvnrData:           ("bvnr_data_t", False),
    S.BvnrReadFlags:      ("bvnr_read_flags_t", False),
    S.BvnrWriteFlags:     ("bvnr_write_flags_t", False),
    S.BvnrSource:         ("bvnr_source_t", True),
    S.BvnrSink:           ("bvnr_sink_t", True),
    S.BvnrDocStreamOpts:  ("bvnr_doc_stream_opts_t", False),
    S.BvnDomEntry:        ("bvn_dom_entry_t", False),
}

# Structs whose ctypes _fields_ are a faithful field-by-field mirror of the C
# struct, so per-field offsets are checked (not just total size). This includes
# the read/write flags structs: they carry the callback function pointers a
# binding installs for C to invoke, so a same-size field reorder there would be
# a serious bug that a size-only check could miss.
_FIELD_CHECKED = {
    "value_type_spec_t", "value_unit_prefix_t", "value_unit_component_t",
    "value_unit_t", "bvnr_data_t", "bvnr_converted_t", "bvnr_doc_stream_opts_t",
    "bvn_dom_entry_t", "bvnr_read_flags_t", "bvnr_write_flags_t",
}


def _c_manifest():
    """Return (sizes, fields) from the C compiler — the source of truth.

    Prefer the CMake-built binary (BVNR_ABI_DUMP); otherwise compile
    tests/bvnr_abi_dump.c on the fly so the test also runs standalone.
    """
    out = None
    env_bin = os.environ.get("BVNR_ABI_DUMP")
    if env_bin and Path(env_bin).exists():
        out = subprocess.run([env_bin], capture_output=True, text=True, check=True).stdout
    else:
        cc = os.environ.get("CC") or shutil.which("cc") or shutil.which("gcc")
        src = _REPO / "tests" / "bvnr_abi_dump.c"
        if not cc or not src.exists():
            pytest.skip("no C compiler / abi_dump source to derive the C ABI")
        import tempfile
        with tempfile.TemporaryDirectory() as td:
            exe = Path(td) / "abi_dump"
            subprocess.run([cc, "-I", str(_REPO / "include"), str(src), "-o", str(exe)],
                           check=True, capture_output=True, text=True)
            out = subprocess.run([str(exe)], capture_output=True, text=True, check=True).stdout

    sizes, fields = {}, {}
    for line in out.splitlines():
        t = line.split()
        if not t:
            continue
        if t[0] == "STRUCT":
            sizes[t[1]] = int(t[2])
        elif t[0] == "FIELD":
            fields.setdefault(t[1], []).append((t[2], int(t[3]), int(t[4])))
    return sizes, fields


@pytest.fixture(scope="module")
def c_abi():
    return _c_manifest()


@pytest.mark.parametrize("cls,cname,opaque",
                         [(c, n, o) for c, (n, o) in _MIRRORS.items()],
                         ids=[n for _, (n, _o) in _MIRRORS.items()])
def test_struct_size_matches_c(cls, cname, opaque, c_abi):
    sizes, _ = c_abi
    assert cname in sizes, f"{cname} missing from the C ABI manifest"
    py = ctypes.sizeof(cls)
    c = sizes[cname]
    if opaque:
        # over-allocation is fine; under-allocation lets C write out of bounds.
        assert py >= c, (f"{cls.__name__} ({py} B) is SMALLER than C {cname} "
                         f"({c} B) — C would write past the Python allocation")
    else:
        assert py == c, (f"{cls.__name__} ({py} B) != C {cname} ({c} B); the "
                         f"ctypes mirror is out of sync with the C struct")


@pytest.mark.parametrize("cls,cname",
                         [(c, n) for c, (n, o) in _MIRRORS.items()
                          if n in _FIELD_CHECKED],
                         ids=[n for _, (n, o) in _MIRRORS.items()
                              if n in _FIELD_CHECKED])
def test_field_offsets_match_c(cls, cname, c_abi):
    _, fields = c_abi
    cfields = fields.get(cname, [])
    pyfields = cls._fields_
    assert len(pyfields) == len(cfields), (
        f"{cls.__name__} has {len(pyfields)} ctypes fields but C {cname} has "
        f"{len(cfields)} — fields added/removed without updating the mirror")
    # Compared in declaration order (names may differ, e.g. a union member).
    # Note: this checks (offset, size), not C type identity — two same-width
    # fields (e.g. a void* and a uint64, both 8 B) are indistinguishable here.
    # Total-size + per-offset agreement catches the realistic desync (a field
    # added/removed/resized, like the frac_* fields); a same-width type-pun is
    # an accepted residual gap.
    for (pyname, _pytype), (cn, coff, csz) in zip(pyfields, cfields):
        fld = getattr(cls, pyname)
        assert fld.offset == coff, (
            f"{cls.__name__}.{pyname} offset {fld.offset} != C {cname}.{cn} {coff}")
        assert fld.size == csz, (
            f"{cls.__name__}.{pyname} size {fld.size} != C {cname}.{cn} {csz}")


def test_all_ctypes_structures_are_covered():
    # Guard against a NEW ctypes Structure being added to the binding without a
    # corresponding entry here (which would leave it unchecked against C). Scans
    # every loaded `bovnar` module, not just structs.py, so one defined elsewhere
    # is caught too. (ctypes.Union mirrors — e.g. the prefix-id union — are not
    # Structure subclasses and are validated transitively via their containers.)
    import inspect
    import sys
    declared = set()
    for modname, mod in list(sys.modules.items()):
        if mod is None or not (modname == "bovnar" or modname.startswith("bovnar.")):
            continue
        for _, obj in inspect.getmembers(mod, inspect.isclass):
            if (issubclass(obj, ctypes.Structure)
                    and str(getattr(obj, "__module__", "")).startswith("bovnar")):
                declared.add(obj)
    missing = declared - set(_MIRRORS)
    assert not missing, (
        f"ctypes Structures not covered by the ABI sweep: "
        f"{sorted(c.__name__ for c in missing)} — add them to _MIRRORS")
