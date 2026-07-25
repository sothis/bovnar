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
"""Conversion factors, re-derived from each unit's DEFINITION.

The pint bridge already checks bovnar's factors against pint — but only where
pint has its own definition. For the ~40 units bovnar defines itself
(`bvnr_furlong`, `bvnr_gauss`, the German historical units, the water-hardness
scales …) the pint definition is generated FROM bovnar's factor, so that check
is circular: a wrong number would agree with itself.

This module closes that gap from the other side. Every factor below is written
out from the unit's defining relation — a furlong is 660 international feet, an
acre is 43560 square feet, an oersted is 1000/4π A/m, a Prussian Zoll is a
twelfth of a Prussian Fuß — and compared against the shipped table. It reads
src/gendata/units.bvnr directly with bvnr_data, so it needs no built library and
fails the moment a factor drifts from its own definition.
"""
import math
import os
import sys

import pytest

_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, _ROOT)
import bvnr_data  # noqa: E402


@pytest.fixture(scope="module")
def factors():
    with open(os.path.join(_ROOT, "src", "gendata", "units.bvnr"),
              encoding="utf-8") as f:
        doc = bvnr_data.load(f.read())
    return {a: u["factor"] for u in doc["units"] for a in u["aliases"]}


# --- the defining constants everything else is built from -------------------
G0 = 9.80665                     # standard gravity, CGPM 1901 (exact)
LB = 0.45359237                  # international pound, 1959 (exact)
FT = 0.3048                      # international foot, 1959 (exact)
IN = FT / 12
GAL = 231 * IN ** 3              # US liquid gallon = 231 in³ (exact)
GAL_UK = 4.54609e-3              # imperial gallon, 1985 (exact)
FL_OZ = GAL / 128
OZ_T = 31.1034768e-3             # troy ounce (exact)
NMI = 1852.0                     # nautical mile, 1929 (exact)
BUSHEL = 2150.42 * IN ** 3       # US bushel (exact)
PR_FUSS = 0.313853               # Prussian foot, 1816 — the historical datum

# unit symbol -> (value in coherent SI, how it is defined)
DERIVED = {
    # length
    'in':      (IN,                 "foot / 12"),
    'yd':      (3 * FT,             "3 ft"),
    'mi':      (5280 * FT,          "5280 ft"),
    'fath':    (6 * FT,             "6 ft"),
    'ch':      (66 * FT,            "66 ft (Gunter's chain)"),
    'rd':      (16.5 * FT,          "16.5 ft"),
    'fur':     (660 * FT,           "660 ft = 1/8 mile"),
    'lea':     (3 * 5280 * FT,      "3 miles"),
    'nmi':     (NMI,                "1852 m exactly"),
    'cbl':     (NMI / 10,           "1/10 nautical mile"),
    'thou':    (IN / 1000,          "inch / 1000"),
    'hand':    (4 * IN,             "4 in"),
    'ftUS':    (1200 / 3937,        "US survey foot"),
    'Å':       (1e-10,              "10⁻¹⁰ m"),
    'au':      (149597870700.0,     "IAU 2012 (exact)"),
    'ly':      (9460730472580800.0, "c × Julian year (exact)"),
    'pc':      (149597870700.0 * 648000 / math.pi, "au × 648000/π"),
    # area
    'ac':      (43560 * FT ** 2,    "43560 ft²"),
    'ha':      (1e4,                "10⁴ m²"),
    # volume
    'gal':     (GAL,                "231 in³"),
    'qt':      (GAL / 4,            "gallon / 4"),
    'pt':      (GAL / 8,            "gallon / 8"),
    'cup':     (GAL / 16,           "gallon / 16"),
    'fl_oz':   (FL_OZ,              "gallon / 128"),
    'tbsp':    (FL_OZ / 2,          "fl oz / 2"),
    'tsp':     (FL_OZ / 6,          "fl oz / 6"),
    'fl_dr':   (FL_OZ / 8,          "fl oz / 8"),
    'minim':   (FL_OZ / 480,        "fl oz / 480"),
    'bbl':     (42 * GAL,           "42 US gallons (petroleum)"),
    'gal_uk':  (GAL_UK,             "4.54609 L exactly"),
    'qt_uk':   (GAL_UK / 4,         "imperial gallon / 4"),
    'pt_uk':   (GAL_UK / 8,         "imperial gallon / 8"),
    'fl_oz_uk': (GAL_UK / 160,      "imperial gallon / 160"),
    'gi_uk':   (GAL_UK / 32,        "imperial gallon / 32"),
    'bsh':     (BUSHEL,             "2150.42 in³"),
    'pk':      (BUSHEL / 4,         "bushel / 4"),
    # mass
    'lb':      (LB,                 "0.45359237 kg exactly"),
    'oz':      (LB / 16,            "pound / 16"),
    'dr':      (LB / 256,           "pound / 256"),
    'gr':      (LB / 7000,          "pound / 7000"),
    'st':      (14 * LB,            "14 lb"),
    'tn_sh':   (2000 * LB,          "2000 lb"),
    'tn_l':    (2240 * LB,          "2240 lb"),
    'oz_t':    (OZ_T,               "troy ounce"),
    'dwt':     (OZ_T / 20,          "troy ounce / 20"),
    'ct':      (0.2e-3,             "200 mg exactly"),
    'qntl':    (100.0,              "100 kg"),
    't':       (1000.0,             "1000 kg"),
    # force, pressure, energy, power
    'lbf':     (LB * G0,            "pound × g₀"),
    'kip':     (1000 * LB * G0,     "1000 lbf"),
    'kgf':     (G0,                 "kilogram × g₀"),
    'gn':      (G0,                 "standard gravity"),
    'dyn':     (1e-5,               "10⁻⁵ N"),
    'psi':     (LB * G0 / IN ** 2,  "lbf / in²"),
    'atm':     (101325.0,           "101325 Pa exactly"),
    'at':      (98066.5,            "1 kgf/cm²"),
    'bar':     (1e5,                "10⁵ Pa"),
    'Torr':    (101325 / 760,       "atm / 760"),
    'mmHg':    (133.322387415,      "conventional mmHg"),
    'cal':     (4.184,              "thermochemical calorie"),
    'erg':     (1e-7,               "10⁻⁷ J"),
    'eV':      (1.602176634e-19,    "elementary charge × volt (exact, SI 2019)"),
    'thm':     (1e5 * 1054.804,     "10⁵ BTU(59 °F) — US therm"),
    'hp':      (550 * FT * LB * G0, "550 ft·lbf/s"),
    'PS':      (75 * G0,            "75 kgf·m/s"),
    # speed
    'kn':      (NMI / 3600,         "nautical mile per hour"),
    'mph':     (5280 * FT / 3600,   "mile per hour"),
    'kph':     (1000 / 3600,        "km per hour"),
    'rpm':     (1 / 60,             "revolution per minute (as a cycle rate)"),
    # CGS and photometry
    'Gal':     (0.01,               "cm/s²"),
    'G':       (1e-4,               "10⁻⁴ T"),
    'Mx':      (1e-8,               "10⁻⁸ Wb"),
    'ph':      (1e4,                "10⁴ lx"),
    'sb':      (1e4,                "10⁴ cd/m²"),
    'Oe':      (1000 / (4 * math.pi), "1000/4π A/m"),
    'Ci':      (3.7e10,             "3.7×10¹⁰ Bq exactly"),
    'R':       (2.58e-4,            "2.58×10⁻⁴ C/kg exactly"),
    # time and angle
    'min':     (60.0,               "60 s"),
    'h':       (3600.0,             "3600 s"),
    'd':       (86400.0,            "86400 s"),
    'wk':      (7 * 86400.0,        "7 d"),
    'fn':      (14 * 86400.0,       "14 d"),
    'yr':      (365.25 * 86400.0,   "Julian year"),
    'mo':      (365.25 * 86400.0 / 12, "Julian year / 12"),
    '°':       (math.pi / 180,      "π/180 rad"),
    'grad':    (math.pi / 200,      "π/200 rad"),
    'rev':     (2 * math.pi,        "2π rad"),
    'arcmin':  (math.pi / 10800,    "degree / 60"),
    'arcsec':  (math.pi / 648000,   "degree / 3600"),
    # ratios and dimensionless scales
    '%':       (0.01,               "10⁻²"),
    '‰':       (1e-3,               "10⁻³"),
    '‱':       (1e-4,               "10⁻⁴"),
    'pcm':     (1e-5,               "10⁻⁵"),
    'ppm':     (1e-6,               "10⁻⁶"),
    'ppb':     (1e-9,               "10⁻⁹"),
    'L':       (1e-3,               "10⁻³ m³"),
    'Da':      (1.66053906660e-27,  "unified atomic mass unit (CODATA 2018)"),
    # German historical: everything derives from the 1816 Prussian foot
    'Pfd':     (0.5,                "Zollverein Pfund = 500 g"),
    'Ztr':     (50.0,               "100 Pfund"),
    'dz':      (100.0,              "200 Pfund"),
    'lot':     (0.5 / 32,           "Pfund / 32"),
    'prf':     (PR_FUSS,            "Prussian foot, 1816"),
    'prz':     (PR_FUSS / 12,       "Fuß / 12"),
    'prln':    (PR_FUSS / 144,      "Zoll / 12"),
    'rute':    (12 * PR_FUSS,       "12 Fuß", 5e-6),
    'klafter': (6 * PR_FUSS,        "6 Fuß", 5e-6),
    'elle':    (25.5 * PR_FUSS / 12, "25½ Zoll"),
    'morgen':  (180 * (12 * PR_FUSS) ** 2, "180 square Ruten", 5e-6),
    'schffl':  (0.0549615,          "published Prussian Scheffel, 54.9615 L "
                                    "(the 3072-Kubikzoll derivation gives 54.967 L, "
                                    "the era's own conversion having used a slightly "
                                    "different Fuß)", 5e-5),
    'dt_mi':   (7420.4386,          "geographische Meile = 1/15 of an equatorial degree", 5e-6),
    # water analysis: molar masses are IUPAC 2021
    '°dH':     (10 / (40.078 + 15.999),                       "10 mg CaO/L ÷ M(CaO)"),
    '°fH':     (10 / (40.078 + 12.011 + 3 * 15.999),          "10 mg CaCO₃/L ÷ M(CaCO₃)"),
    '°aH':     (1 / (40.078 + 12.011 + 3 * 15.999),           "1 mg CaCO₃/L ÷ M(CaCO₃)"),
    '°rH':     (1 / 40.078,                                   "1 mg Ca/L ÷ M(Ca)"),
    '°e':      ((64.79891e-3 * 1000 / 4.54609)
                / (40.078 + 12.011 + 3 * 15.999),  "grain CaCO₃ per imperial gallon"),
    'gpg':     ((64.79891e-3 * 1000 / 3.785411784)
                / (40.078 + 12.011 + 3 * 15.999),  "grain CaCO₃ per US gallon"),
    'val':     (0.5,                "equivalent of a divalent ion = ½ mol"),
    'CF':      (0.01,               "EC in mS/cm × 10 = 0.1 mS/cm"),
}


# A unit defined by arithmetic on exact constants must match to the last bit.
# A historical one is published to about six significant figures, and demanding
# more of it would assert a precision its source never had; those entries carry
# their own tolerance.
_DEFAULT_TOL = 1e-9


def test_generated_tables_match_the_source(factors):
    """The C tables are generated from units.bvnr; check the generated text
    against the document rather than trusting the generator that wrote both.
    Skipped when the build directory has not produced them yet."""
    import re
    from decimal import Decimal
    from fractions import Fraction
    gen = os.path.join(_ROOT, "build", "generated")
    conv_path = os.path.join(gen, "bovnar_si_conv_table.gen.inc")
    if not os.path.exists(conv_path):
        pytest.skip("generated tables not built")
    with open(os.path.join(_ROOT, "src", "gendata", "units.bvnr"),
              encoding="utf-8") as f:
        units = bvnr_data.load(f.read())["units"]
    dim_names = ["length", "mass", "time", "current", "temperature",
                 "amount", "luminosity"]
    rows = {}
    for line in open(conv_path, encoding="utf-8"):
        m = re.match(r'\s*\[(bu_\w+)\s*\]\s*=\s*\{\s*bu_\w+,\s*([^,]+),'
                     r'\s*\{([^}]*)\},\s*(true|false),\s*([^,]+),'
                     r'\s*"(-?\d+)",\s*"(\d+)",\s*"(-?\d+)",\s*"(\d+)",'
                     r'\s*(true|false)', line)
        if m:
            rows[m.group(1)] = m.groups()[1:]
    assert len(rows) == len(units), (
        f"si_conv_table has {len(rows)} rows for {len(units)} units")
    for u in units:
        key = "bu_" + u["name"]
        factor, dims, affine, offset, fn, fd, on, od, exact = rows[key]
        assert float(factor) == float(u["factor"]), key
        want = [0] * 7
        if isinstance(u["dims"], dict):
            for name, value in u["dims"].items():
                want[dim_names.index(name)] = value
        assert [int(x) for x in dims.split(",")] == want, f"{key} dims"
        assert (affine == "true") == bool(u["affine"]), f"{key} affine"
        assert float(offset) == float(u["offset"]), f"{key} offset"
        assert (exact == "true") == bool(u.get("exact", True)), f"{key} exact"
        # the generator reduces the rational, so compare as fractions
        stated = (Fraction(int(u["factor_num"]), int(u["factor_den"]))
                  if u.get("factor_num") is not None
                  else Fraction(Decimal(repr(float(u["factor"])))))
        assert Fraction(int(fn), int(fd)) == stated, f"{key} rational"


def test_the_parse_table_is_ordered_longest_first():
    """The suffix matcher takes the FIRST match, so a shorter alias appearing
    before a longer one silently changes what a token parses as."""
    import re
    path = os.path.join(_ROOT, "build", "generated", "bovnar_bu_table.gen.inc")
    if not os.path.exists(path):
        pytest.skip("generated tables not built")
    lengths = [int(m.group(1)) for line in open(path, encoding="utf-8")
               for m in [re.match(r'\s*\{\s*.+?,\s*(\d+),\s*bu_\w+\s*\},', line)]
               if m]
    assert lengths, "no rows parsed out of bovnar_bu_table.gen.inc"
    assert lengths == sorted(lengths, reverse=True), \
        "bu_table is not sorted longest-first"
    assert max(lengths) < 32, "an alias exceeds BU_LEN_INDEX_SIZE"


@pytest.mark.parametrize("symbol", sorted(DERIVED), ids=str)
def test_factor_matches_its_definition(symbol, factors):
    entry = DERIVED[symbol]
    want, how = entry[0], entry[1]
    tol = entry[2] if len(entry) > 2 else _DEFAULT_TOL
    assert symbol in factors, f"{symbol} is no longer in the unit table"
    got = factors[symbol]
    assert math.isclose(got, want, rel_tol=tol), (
        f"{symbol}: table says {got!r}, but {how} gives {want!r} "
        f"(relative difference {abs(got - want) / want:.3g}, tolerance {tol:g})")


def test_the_derivation_set_covers_the_self_defined_units(factors):
    """The point of this module is the units pint cannot check independently.
    If a new one is added without a derivation here, say so."""
    unchecked = {
        # primitive or instrument-defined, not derivable by arithmetic
        'b', 'B', 'Bd', 'pH', 'NTU', 'FNU', 'FTU', 'FAU', 'JTU', 'PSU',
        'var', 'VA', 'dB', 'Np', 'slug', 'tex', 'den', 'sc', 'gi',
    }
    self_defined = {'fur', 'fath', 'thm', 'ac', 'G', 'Mx', 'Oe', 'ph', 'var',
                    'rpm', 'ch', 'rd', 'Pfd', 'Ztr', 'dz', 'lot', 'prln',
                    'prz', 'prf', 'elle', 'rute', 'klafter', 'dt_mi', 'morgen',
                    'schffl', 'lea', 'cbl', 'qntl', '‱', 'pcm', 'ppb',
                    '°dH', '°e', '°fH', '°rH', '°aH', 'val', 'gpg', 'CF'}
    missing = sorted(self_defined - set(DERIVED) - unchecked)
    assert not missing, (
        "these units are validated against a pint definition derived from "
        "bovnar itself, and have no independent derivation here: %s" % missing)
