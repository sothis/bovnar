#!/usr/bin/env python3
"""
check_doc_counts.py — every "180 physical units" in the tree, against gendata.

THE GAP THIS CLOSES. src/gendata is the source of truth for the catalogue, and
this repo gates the tables against it thoroughly: check_doc_unit_tables.py
compares roughly 1150 documented ROWS, test_unit_factors_derived.py compares the
generated C factors, check_profile_factors.py compares the profiles against
their publishers. What none of them compares is the SENTENCE — the headline
number a reader meets first, in the README, in the cheatsheet's scope line, in
the IETF draft, in the JOSS paper, in the EBNF's commentary, on the website.

Those had drifted, and drifted quietly, because a count in prose is invisible to
every table check. The registry had grown past 180 units and fifteen files still
said 180; the spelling total was stated as 529 against an actual 586. A reader
implementing from the draft, or citing the paper, got a number this repository
could have contradicted from its own data.

WHAT IS CHECKED. Only tightly anchored phrases — "N physical units", "N accepted
spellings", "N fiat currencies" and the handful of others in CLAIMS below. Each
is a phrase whose subject is unambiguous, so a number that happens to sit near
the word "unit" ("180 square Ruten", "216 bytes") is not swept up. A file with
no such phrase is not a failure: this proves the numbers that ARE stated, it
does not require any to be.

WHAT IS DELIBERATELY NOT CHECKED. CHANGELOG.md and the release notes, which are
a record of what was true at a version and must not be rewritten; web/ and dist/,
which are generated from the documents checked here.

Usage:  python3 check_doc_counts.py [repo-root]
Exit 0 when every stated count matches src/gendata, 1 with a list when not.
"""
import os
import re
import sys

REPO = os.path.dirname(os.path.abspath(__file__))


def counts(repo):
    """The numbers src/gendata actually states."""
    sys.path.insert(0, repo)
    import bvnr_data

    def load(name):
        with open(os.path.join(repo, "src", "gendata", name), "rb") as f:
            return bvnr_data.load(f.read())

    units = load("units.bvnr")["units"]
    curr = load("currencies.bvnr")["currencies"]
    pfx = load("prefixes.bvnr")
    profiles = ("ucum", "unece", "qudt", "qudt-qk", "udunits", "om", "cf")
    mapped = refused = 0
    for ns in profiles:
        d = load(ns + ".bvnr")
        mapped += len(d.get("mapped", [])) + len(d.get("opaque", []))
        refused += len(d.get("unsupported", []))
    _ = profiles
    return {
        "profiles": len(profiles),
        "mapped_codes": mapped,
        "refusals": refused,
        "units": len(units),
        "spellings": sum(len(u["aliases"]) for u in units),
        "currencies": len(curr),
        "fiat": sum(1 for c in curr if not c["is_crypto"]),
        "crypto": sum(1 for c in curr if c["is_crypto"]),
        "si_prefixes": len(pfx["si_prefixes"]),
        "iec_prefixes": len(pfx["iec_prefixes"]),
    }


# The gap between two words of a phrase: ONE space, or a line break and whatever
# a wrapped line opens with (Markdown quoting, a comment marker, indentation).
# Not \s+ — a run of spaces is a TABLE COLUMN, not a sentence, and matching one
# turned "900000..909999   currencies" in the id-space table of bovnar.h into a
# claim that the catalogue holds 909 999 of them.
_G = r"(?:[ ]|\n[ \t>#*(]*)"


def _phrase(*words):
    return _G.join(words)


# (regex, key) — the regex must capture exactly one number, and the phrase must
# leave no doubt what that number counts. Add a phrasing here rather than
# loosening one that already works.
# The namespace count, stated as a WORD in four documents. doc/11 managed to say
# "seven" in its own header and "five" 160 lines later, and doc/09 said "five"
# long after om and cf shipped -- a count nothing gated because it is spelled out
# rather than written as a numeral.
_WORD = {"one": 1, "two": 2, "three": 3, "four": 4, "five": 5, "six": 6,
         "seven": 7, "eight": 8, "nine": 9, "ten": 10}

CLAIMS = [
    (_phrase(r"\b(\d+)", "physical", r"units\b"),               "units"),
    (_phrase(r"\b(\d+)", "physical", "base", r"units\b"),       "units"),
    (_phrase(r"\b(\d+)", "named", r"units\b"),                  "units"),
    (_phrase(r"\b(\d+)", "named", "base", r"units\b"),          "units"),
    (_phrase(r"\b(\d+)", "base", r"units\b"),                   "units"),
    (_phrase(r"registry(?:'s)?", r"(\d+)", r"units\b"),         "units"),
    (_phrase(r"\b(\d+)", "units", "and", r"\d+", r"currencies\b"), "units"),
    (_phrase(r"\b(\d+)", "accepted", r"spellings\b"),           "spellings"),
    (_phrase(r"\b(\d+)", "spellings", "in", r"total\b"),        "spellings"),
    (_phrase(r"\b(\d+)", "registered", r"spellings\b"),         "spellings"),
    # Two words, always on one line: the wrapping form of this one matched the
    # BLOCK-TAG TABLES in the .bvnr headers, where "90" is an id block and
    # "currencies" the next comment line's first word.
    (r"\b(\d+) currencies\b",                                   "currencies"),
    (_phrase(r"\b(\d+)", "currency", r"codes\b"),               "currencies"),
    # "ISO 4217 fiat currencies" names the STANDARD, not a count, so the
    # lookbehind keeps 4217 out of the comparison.
    (_phrase(r"(?<!ISO )\b(\d+)", "fiat", r"currencies\b"),     "fiat"),
    (_phrase(r"\b(\d+)", r"cryptocurrencies\b"),                "crypto"),
    (_phrase(r"\b(\d+)", "SI", r"\+", r"\d+", "IEC", r"prefixes\b"),
                                                                "si_prefixes"),
    (_phrase(r"\bSI", r"\+", r"(\d+)", "IEC", r"prefixes\b"),   "iec_prefixes"),
    # The profile tables, as a WHOLE. Anchored on the full "N mapped codes and M
    # named refusals" phrasing rather than on "N mapped codes" alone: doc/11
    # narrates how each vocabulary's own table grew ("from 100 mapped codes to
    # 201"), which is history and must not be rewritten to today's total. The
    # digits carry a thin space in the documents, so a separator is allowed
    # inside the number and stripped before comparing.
    (_phrase(r"\b([\d\u202f\u00a0 ]+?)", "mapped", "codes", "and",
             r"[\d\u202f\u00a0 ]+?", "named", r"refusals\b"),   "mapped_codes"),
    (_phrase(r"\b[\d\u202f\u00a0 ]+?", "mapped", "codes", "and",
             r"([\d\u202f\u00a0 ]+?)", "named", r"refusals\b"), "refusals"),
]

# Everything under these is either a historical record or generated from a file
# that IS checked. Rewriting a changelog to match today's catalogue would be a
# lie about what shipped.
SKIP_DIRS = {".git", "build", "dist", "web", "__pycache__", ".pytest_cache",
             ".cache", ".idea", "node_modules"}
SKIP_FILES = {"CHANGELOG.md", "check_doc_counts.py"}
SKIP_PREFIXES = ("RELEASE_NOTES_",)
SUFFIXES = (".md", ".py", ".txt", ".bvnr", ".h", ".c", ".ebnf", ".cff", ".xml")


def files(repo):
    for root, dirs, names in os.walk(repo):
        dirs[:] = [d for d in dirs if d not in SKIP_DIRS]
        for n in sorted(names):
            if n in SKIP_FILES or n.startswith(SKIP_PREFIXES):
                continue
            if not n.endswith(SUFFIXES):
                continue
            yield os.path.join(root, n)


def main(argv):
    repo = os.path.abspath(argv[1]) if len(argv) > 1 else REPO
    want = counts(repo)
    bad, checked = [], 0
    for path in files(repo):
        try:
            with open(path, encoding="utf-8") as f:
                text = f.read()
        except (UnicodeDecodeError, OSError):
            continue
        rel = os.path.relpath(path, repo)
        # The word-form namespace claim, checked before the numeral claims
        # because its value is a word and the shared loop parses an integer.
        for m in re.finditer(
                r"\b([Oo]ne|[Tt]wo|[Tt]hree|[Ff]our|[Ff]ive|[Ss]ix|[Ss]even|"
                r"[Ee]ight|[Nn]ine|[Tt]en)" + _G + r"namespaces?" + _G +
                r"(?:are|is)" + _G + r"(?:defined|accepted)", text):
            checked += 1
            got = _WORD[m.group(1).lower()]
            if got != want["profiles"]:
                line = text.count("\n", 0, m.start()) + 1
                bad.append("%s:%d: says %d namespaces, src/gendata defines %d  (%r)"
                           % (rel, line, got, want["profiles"],
                              " ".join(m.group(0).split())))
        for pattern, key in CLAIMS:
            for m in re.finditer(pattern, text):
                checked += 1
                got = int(re.sub(r"[\s\u202f\u00a0]", "", m.group(1)))
                if got != want[key]:
                    line = text.count("\n", 0, m.start()) + 1
                    bad.append("%s:%d: says %d %s, src/gendata has %d  (%r)"
                               % (rel, line, got, key, want[key],
                                  m.group(0)))
    if bad:
        print("check_doc_counts: stated counts disagree with src/gendata:",
              file=sys.stderr)
        for line in bad:
            print("    " + line, file=sys.stderr)
        print("\nsrc/gendata is the source of truth; fix the sentence.",
              file=sys.stderr)
        return 1
    print("check_doc_counts: %d stated count(s) checked, all matching "
          "src/gendata (%d units, %d spellings, %d currencies)"
          % (checked, want["units"], want["spellings"], want["currencies"]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
