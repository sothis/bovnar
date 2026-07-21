#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Janos Sonntag
"""bvnr_data.py must agree with the reference reader.

It is a SECOND implementation of the format -- a dependency-free parser the code
generators use so a clean checkout can rebuild the unit, prefix and currency
tables without a built libbvnr. Its docstring claims load() returns what
bovnar.loads() returns. Where it silently disagreed, the generators shipped wrong
tables:

  * `on` / `off` are boolean synonyms in the format and came back as STRINGS.
    "off" is truthy in Python, so ".exact = off;" -- perfectly legal bovnar --
    would have been read as exact.
  * \\u{...} and \\xHH escapes fell through to the escaped character, so
    "caf\\u{e9}" became "cafu{e9}" -- a corrupted unit or currency name.
  * `null`, `nan`, `inf`, `ninf` came back as the strings "null", "nan", ...
  * An empty slot came back as "" rather than None, so `is None` missed it.
  * A leading '+' was accepted where the reference reader rejects it.

Run standalone: python3 tests/test_bvnr_data_parity.py
"""
import math
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)
sys.path.insert(0, os.path.join(ROOT, "python"))

import bvnr_data                                            # noqa: E402

try:
    import bovnar                                           # noqa: E402
except Exception as exc:                                    # pragma: no cover
    print("SKIP: bovnar bindings unavailable (%s)" % exc)
    sys.exit(0)


def _norm(o):
    """NaN != NaN, and 1.0 == 1 only by luck; normalise both."""
    if isinstance(o, dict):
        return {k: _norm(v) for k, v in o.items()}
    if isinstance(o, list):
        return [_norm(v) for v in o]
    if isinstance(o, float):
        if math.isnan(o):
            return "NAN"
        if o.is_integer():
            return int(o)
    return o


CASES = [
    ("negative number",      ".x = -5;"),
    ("exponent",             ".x = 1e-3;"),
    ("uppercase exponent",   ".x = 2.5E+4;"),
    ("named escapes",        '.x = "a\\tb\\r\\n\\"q\\"";'),
    ("codepoint escape",     '.x = "caf\\u{e9}";'),
    ("byte escape",          '.x = "\\x41";'),
    ("bare symbol",          ".x = ok;"),
    ("true / false",         ".x = true; .y = false;"),
    ("on / off",             ".x = on; .y = off;"),
    ("empty slot",           ".x = ;"),
    ("null keyword",         ".x = null;"),
    ("array hole",           ".x = [1, , 3];"),
    ("special floats",       ".x = nan; .y = inf; .z = ninf;"),
    ("trailing comment",     ".x = 1;   # remark"),
    ("comment with ;",       "# a; b\n.x = 1;"),
    ("type annotation",      ".x = <uint:16> 7;"),
    ("annotation with unit", ".x = <float:64,k~m> 1.5;"),
    ("brace in string",      '.x = "}";'),
    ("hash in string",       '.x = "# not a comment";'),
    ("nested",               ".a = {.b = [1, {.c = 2;}];};"),
    ("empty struct",         ".x = {};"),
    ("empty array",          ".x = [];"),
]


def main():
    failures = []
    for label, body in CASES:
        doc = ("#!bovnar 1.1\n" + body + "\n").encode()
        try:
            mine = _norm(bvnr_data.load(doc))
        except Exception as exc:
            mine = ("raised", type(exc).__name__)
        try:
            ref = _norm(bovnar.loads(doc))
        except Exception as exc:
            ref = ("raised", type(exc).__name__)
        if mine != ref:
            failures.append("%-22s bvnr_data=%r  bovnar=%r" % (label, mine, ref))

    # A leading '+' is not a bovnar number; both must refuse it.
    for parse in (lambda d: bvnr_data.load(d), lambda d: bovnar.loads(d)):
        try:
            parse(b"#!bovnar 1.1\n.x = +5;\n")
            failures.append("a leading '+' was accepted")
        except Exception:
            pass

    # The real inputs, which is what actually gates the generated tables.
    for name in ("units", "prefixes", "currencies"):
        path = os.path.join(ROOT, "src", "gendata", "%s.bvnr" % name)
        raw = open(path, "rb").read()
        if _norm(bvnr_data.load(raw)) != _norm(bovnar.loads(raw)):
            failures.append("gendata/%s.bvnr parses differently" % name)

    if failures:
        print("FAILED %d of %d" % (len(failures), len(CASES) + 4))
        for f in failures:
            print("  " + f)
        return 1
    print("PASSED %d parity cases + 3 gendata documents" % len(CASES))
    return 0


if __name__ == "__main__":
    sys.exit(main())
