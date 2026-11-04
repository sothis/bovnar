# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Janos Sonntag
"""dumps(loads(x, typed=True)) must preserve every value AND its declared type.

A property test over generated documents rather than hand-picked cases. It found
two defects the targeted tests did not, both from the annotation being omitted
when it looked redundant:

  * "<sint:64> 127" came back as "<uint:64> 127". Default synthesis reads a bare
    NON-NEGATIVE integer as uint, so a sint that happens to be >= 0 needs its
    annotation; a negative one does not.
  * A sint array of MIXED sign lost it too: the decision was taken from whichever
    leaf was seen first, and a negative one answers "no annotation needed" for
    the whole array -- after which its non-negative elements re-read as uint.

The comparison is on (family, width, unit, literal), not on the emitted text:
unit spelling is canonicalised (m*s vs m·s) and the version directive is dropped
when nothing needs it, neither of which changes a value.
"""
import os
import random
import subprocess
import tempfile

import pytest

from conftest import needs_lib

import bovnar

CLI = os.path.join(
    os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))),
    "build", "bovnar")

FAMILIES = [
    ("uint",      [8, 16, 32, 64, 128], ["0", "1", "255", "65535",
                                         "12345678901234567890"]),
    ("sint",      [8, 16, 32, 64],      ["0", "-1", "127", "-128",
                                         "-9007199254740993"]),
    ("float",     [16, 32, 64, 128],    ["1.5", "-2.75", "0.0", "3.14159",
                                         "1e10", "nan", "inf"]),
    ("float_dec", [32, 64, 128],        ["1.5", "19.99",
                                         "1.2345678901234567890123456789"]),
]
UNITS = ["", "m", "k~m", "m/s", "k~g*m/s²", "Hz", "$USD", "$JPY", "Gi~B", "°C"]


def _annotation(fam, width, unit):
    return "<%s:%d%s>" % (fam, width, ("," + unit) if unit else "")


def _pick(rnd, arrays):
    fam, widths, values = rnd.choice(FAMILIES)
    width = rnd.choice(widths)
    unit = rnd.choice(UNITS)
    if fam in ("uint", "sint") and unit.startswith("$"):
        unit = ""                      # a currency needs a decimal family
    if arrays:
        values = [v for v in values if v not in ("nan", "inf")]
        body = "[%s]" % ", ".join(rnd.choice(values)
                                  for _ in range(rnd.randint(1, 4)))
    else:
        body = rnd.choice(values)
        if body in ("nan", "inf"):
            unit = ""                  # a special number takes no unit
    return _annotation(fam, width, unit), body


def _signature(q):
    """What must survive: family, width, unit and the exact literal."""
    if q is None:
        return None
    if isinstance(q, list):
        return [_signature(x) for x in q]
    return (int(q.vtype.family), int(q.vtype.width), q.unit_str, q.raw)


@needs_lib
@pytest.mark.skipif(not os.path.exists(CLI), reason="CLI not built")
def test_typed_roundtrip_preserves_value_and_type():
    rnd = random.Random(17)
    checked = skipped = 0
    failures = []
    with tempfile.TemporaryDirectory() as td:
        path = os.path.join(td, "a.bvnr")
        for _ in range(400):
            lines = ["#!bovnar 1.1"]
            for j in range(rnd.randint(1, 5)):
                arrays = rnd.random() >= 0.6
                ann, body = _pick(rnd, arrays)
                lines.append(".%s%d = %s %s;" % ("a" if arrays else "k", j,
                                                 ann, body))
            doc = ("\n".join(lines) + "\n").encode()
            with open(path, "wb") as f:
                f.write(doc)
            # Only documents the reference implementation accepts are in scope.
            if subprocess.run([CLI, "validate", path],
                              capture_output=True).returncode != 0:
                skipped += 1
                continue
            before = bovnar.loads(doc, typed=True)
            after = bovnar.loads(bovnar.dumps(before), typed=True)
            checked += 1
            a = {k: _signature(v) for k, v in before.items()}
            b = {k: _signature(v) for k, v in after.items()}
            if a != b:
                key = next(k for k in a if a.get(k) != b.get(k))
                failures.append("%s: %r -> %r" % (key, a[key], b[key]))
    assert checked >= 200, "only %d documents were in scope" % checked
    assert not failures, "%d of %d round-trips lost information, first: %s" % (
        len(failures), checked, failures[:3])


# A quoted numeric literal under a default annotation is a string to default
# synthesis: dumps() used to drop the annotation (base 10, width 64 say
# nothing) and the writer then refused a float type over a string token. A
# typed null has nothing BUT its annotation: written bare it reloaded as an
# untyped None, and per-element typed nulls in an array all became `null`.
_ANNOTATION_ONLY_DOCS = [
    b'.a = <float:64,_10> "5.7";',
    b'.a = <float> "5.7";',
    b'.a = <uint> "42";',
    b'.a = <uint:64,_10> "42";',
    b'.a = <float:64,_10> ["5.7", "1"];',
    b'.a = <float:64> ;',
    b'.a = <utf8> ;',
    b'.a = <sint:64> ;',
    b'.a = [<uint:32>, <sint:64>, <float:32>];',
    b'.a = [<utf8>, <uint:8>];',
    b'.a = [<uint:8>, 1];',
    b'.a = <utf8> [, "first", ];',
    b'.a = <float:64> [, 1.5];',
    b'.a = [<float:64,m>, <uint:8,m> 1];',
    b'.s = { .x = <float:32> ; };',
    b'.a = <bool> ;',
    b'.a = [<bool>, true];',
]


@needs_lib
@pytest.mark.parametrize("src", _ANNOTATION_ONLY_DOCS)
def test_annotation_only_information_round_trips(src):
    doc = bovnar.loads(src, typed=True)
    out = bovnar.dumps(doc)
    assert bovnar.loads(out, typed=True) == doc, out


@needs_lib
def test_typed_null_keeps_its_family():
    out = bovnar.dumps(bovnar.loads(b'.a = <float:64> ;', typed=True))
    q = bovnar.loads(out, typed=True)["a"]
    assert isinstance(q, bovnar.Quantity) and q.raw is None
    assert int(q.vtype.family) == int(bovnar.ValueTypeFamily.FLOAT)
