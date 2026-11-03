# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Janos Sonntag
"""A unit symbol must survive Unicode normalisation.

Three of the symbols are characters Unicode declares equivalent to another:

    U+2126 OHM SIGN      -> U+03A9 GREEK CAPITAL LETTER OMEGA   (NFC/NFD/NFKC/NFKD)
    U+212B ANGSTROM SIGN -> U+00C5, and NFD to U+0041 U+030A
    U+00B5 MICRO SIGN    -> U+03BC GREEK SMALL LETTER MU        (NFKC/NFKD)

Only the sign forms were accepted. So a valid document passed through any
normalising step -- and editors, web forms, databases and search indexes
normalise routinely -- came back invalid:

    .r = <float:64,Ω> 100.0;   ->  NFC  ->  error_unit_illegal

i.e. the format accepted only the form that normalisation destroys, and rejected
the one it produces. The equivalent forms are now aliases. This is purely
additive: strictly more documents parse, and no existing document changes
meaning, since the canonical output symbol is unchanged.

The sweep is over EVERY symbol, not the three that were reported -- a new one
with a compatibility decomposition would set the same trap.
"""
import re
import unicodedata

import pytest

from conftest import needs_lib

import bovnar

FORMS = ("NFC", "NFD", "NFKC", "NFKD")

DOC = ("#!bovnar 1.1\n"
       ".r = <float:64,Ω> 100.0;\n"
       ".d = <float:64,µ~m> 2.5;\n"
       ".a = <float:64,Å> 1.0;\n")


@needs_lib
@pytest.mark.parametrize("form", FORMS)
def test_document_survives_normalisation(form):
    normalised = unicodedata.normalize(form, DOC)
    values = bovnar.loads(normalised.encode())
    assert values["r"] == 100.0 and values["d"] == 2.5 and values["a"] == 1.0


@needs_lib
@pytest.mark.parametrize("form", FORMS)
def test_every_unit_symbol_survives_normalisation(form):
    """Sweep the whole gendata table, not just the reported three."""
    import os
    root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    text = open(os.path.join(root, "src", "gendata", "units.bvnr"),
                encoding="utf-8").read()
    symbols = {s for s in re.findall(r'"([^"]+)"', text)
               if s and not s.isascii()}
    assert len(symbols) >= 5, "found only %d non-ASCII symbols" % len(symbols)

    broken = []
    for sym in sorted(symbols):
        try:
            bovnar.parse_unit(sym)
        except Exception:
            continue                       # not a standalone unit symbol
        variant = unicodedata.normalize(form, sym)
        if variant == sym:
            continue
        try:
            bovnar.parse_unit(variant)
        except Exception:
            broken.append("%s (%s) -> %s" % (
                sym, " ".join("U+%04X" % ord(c) for c in sym),
                " ".join("U+%04X" % ord(c) for c in variant)))
    assert not broken, "%s destroys these symbols: %s" % (form, broken)


# Exponents are a different matter. A superscript digit has a COMPATIBILITY
# decomposition only (U+00B2 SUPERSCRIPT TWO -> U+0032 DIGIT TWO), so the
# canonical forms keep it and the compatibility forms fold it to a plain digit:
# `m/s²` becomes `m/s2`, which is not a unit (error_unit_illegal). That is not
# repaired by an alias -- `s2` would be a guess at what was meant -- so a
# pipeline that applies NFKC/NFKD must write the caret form, which every form
# leaves alone.
SUPERSCRIPT_DOC = ".a = <float:64,m/s²> 9.81;\n"
CARET_DOC = ".a = <float:64,m/s^2> 9.81;\n"


@needs_lib
@pytest.mark.parametrize("form", ("NFC", "NFD"))
def test_canonical_forms_keep_a_superscript_exponent(form):
    normalised = unicodedata.normalize(form, SUPERSCRIPT_DOC)
    assert normalised == SUPERSCRIPT_DOC
    assert bovnar.loads(normalised.encode())["a"] == 9.81


@needs_lib
@pytest.mark.parametrize("form", ("NFKC", "NFKD"))
def test_compatibility_forms_destroy_a_superscript_exponent(form):
    normalised = unicodedata.normalize(form, SUPERSCRIPT_DOC)
    assert "m/s2" in normalised
    with pytest.raises(bovnar.BovnarParseError) as ei:
        bovnar.loads(normalised.encode())
    assert ei.value.code == bovnar.ErrorCode.UNIT_ILLEGAL


@needs_lib
@pytest.mark.parametrize("form", FORMS)
def test_caret_exponent_survives_every_form(form):
    normalised = unicodedata.normalize(form, CARET_DOC)
    assert normalised == CARET_DOC
    q = bovnar.loads(normalised.encode(), typed=True)["a"]
    assert q.value == 9.81
    assert bovnar.parse_unit("m/s^2") == bovnar.parse_unit("m/s²")
