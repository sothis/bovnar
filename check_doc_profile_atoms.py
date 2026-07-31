#!/usr/bin/env python3
"""
check_doc_profile_atoms.py — doc/11 §6.1's UCUM atom tables, against
src/gendata/ucum.bvnr.

THE GAP THIS CLOSES. §6.1 opens with "What follows is the whole mapped list",
and it was not: 188 codes are mapped in `ucum.bvnr` and 155 appeared in the
tables, so a third of the profile was invisible to a reader consulting the one
place that promises completeness. Worse than an omission, the same section
carried a sentence saying the US survey series "is **refused**" while §6.3, a
hundred lines down, correctly listed all nine of them as mapped — the document
contradicting itself about a capability the library has had for some time.

Nothing caught it because the existing gates ask different questions.
check_profile_factors.py's `check_doc_error_claims` keys on an `error_unit_*`
token, and a sentence that says "is refused" in plain English carries none;
`check_doc_profile_spellings` reads what the WRITER emits. Neither asks the
completeness question, which is the one §6.1's own opening sentence makes.

WHAT IS CHECKED, per row of §6.1:

  membership  the code is one `ucum.bvnr` maps (an opaque/arbitrary unit is
              allowed too — §6.1 lists a few beside the mapped ones);
  target      the Bovnar column is what the reference implementation actually
              produces for `ucum:<code>` — not merely a unit that parses;
  factor      the third column is that unit's coherent-SI factor;
  coverage    every mapped code appears in some §6.1 table.

The whole-code table at the end of §6.1 is read the same way, except that its
first column holds a UCUM EXPRESSION rather than an atom, so it is checked by
parsing rather than by table membership.

Needs the built library (it asks the reader, rather than restating its rules),
so it skips rather than fails when there is none — the same rule
check_profile_factors.py follows.

Usage:  python3 check_doc_profile_atoms.py [repo-root]
Exit 0 when §6.1 agrees with src/gendata and the library, 1 with a list when not.
"""
import os
import re
import sys

REPO = os.path.dirname(os.path.abspath(__file__))
DOC = "doc/11_bovnar_unit_profiles.md"
SECTION_START = "### 6.1"
SECTION_END = "### 6.2"

TICK = re.compile(r"`([^`\n]+)`")
SEPARATOR = re.compile(r"^\|[\s:|-]+\|$")


def load_reader(repo):
    """(parse_ucum, unit_to_str, si_factor), or None when there is no library."""
    sys.path.insert(0, os.path.join(repo, "python"))
    try:
        import bovnar
        bovnar._ffi.load_library()
    except Exception:
        return None

    def parse(expr):
        try:
            return bovnar.parse_unit(expr)
        except Exception:
            return None

    def to_str(u):
        return bovnar.unit_to_str(u)

    def factor(u):
        # An opaque (arbitrary) unit has no SI factor at all and the binding
        # raises rather than reporting one, which is the right answer for a
        # row whose factor cell says so in words.
        try:
            return bovnar.unit_to_si_factor(u).factor
        except Exception:
            return None

    return parse, to_str, factor


def rows(path):
    """(line number, cells) for every table row in §6.1."""
    with open(path, encoding="utf-8") as f:
        lines = f.read().split("\n")
    inside = False
    header = None
    for n, line in enumerate(lines, 1):
        if line.startswith(SECTION_START):
            inside = True
            continue
        if line.startswith(SECTION_END):
            return
        if not inside:
            continue
        if not line.startswith("|"):
            header = None
            continue
        if SEPARATOR.match(line.strip()):
            continue
        cells = [c.strip() for c in line.strip().strip("|").split("|")]
        if header is None:
            header = cells
            continue
        if header[:1] != ["UCUM"] or len(cells) < 3:
            continue
        yield n, cells


def main(argv):
    repo = os.path.abspath(argv[1]) if len(argv) > 1 else REPO
    sys.path.insert(0, repo)
    import bvnr_data

    path = os.path.join(repo, DOC)
    if not os.path.exists(path):
        print("check_doc_profile_atoms: %s is absent; nothing to check" % DOC)
        return 0

    reader = load_reader(repo)
    if reader is None:
        print("check_doc_profile_atoms: no built library; skipped")
        return 0
    parse, to_str, factor = reader

    with open(os.path.join(repo, "src", "gendata", "ucum.bvnr"), "rb") as f:
        data = bvnr_data.load(f.read())
    mapped = {m["code"] for m in data.get("mapped", [])}
    opaque = {o["code"] for o in data.get("opaque", [])}

    bad = []
    seen = set()
    checked = 0
    for lineno, cells in rows(path):
        codes = TICK.findall(cells[0])
        if not codes:
            continue
        code = codes[0]
        want_target = TICK.findall(cells[1])
        want_factor = TICK.findall(cells[2])
        where = "%s:%d  %-14s" % (DOC, lineno, code)

        # An ATOM row names a code the table declares; a WHOLE-CODE row names an
        # expression built from several. Both must read through the profile, so
        # both are parsed -- the distinction only decides whether membership in
        # `mapped` is required.
        is_atom = code in mapped or code in opaque
        if is_atom:
            seen.add(code)
        u = parse("ucum:" + code)
        if u is None:
            bad.append("%s does not parse through the ucum profile" % where)
            continue
        checked += 1
        if want_target:
            got = to_str(u)
            if got != want_target[0]:
                bad.append("%s documented target %r, the library writes %r"
                           % (where, want_target[0], got))
        if want_factor:
            got_f = factor(u)
            try:
                doc_f = float(want_factor[0])
            except ValueError:
                continue          # "no SI factor", an opaque unit's row
            if got_f is None:
                bad.append("%s documents a factor %s; the unit has none"
                           % (where, want_factor[0]))
            elif got_f == 0.0 or abs(got_f - doc_f) > 1e-12 * abs(got_f):
                bad.append("%s documented factor %s, the library computes %.17g"
                           % (where, want_factor[0], got_f))

    missing = sorted(mapped - seen)
    if missing:
        bad.append("%s §6.1 says it is \"the whole mapped list\" and omits %d "
                   "of the %d mapped codes: %s"
                   % (DOC, len(missing), len(mapped), ", ".join(missing)))

    if bad:
        sys.stderr.write("check_doc_profile_atoms: doc/11 §6.1 disagrees with "
                         "src/gendata/ucum.bvnr:\n")
        for line in bad:
            sys.stderr.write("    %s\n" % line)
        sys.stderr.write("\nsrc/gendata is the source of truth; fix the "
                         "document.\n")
        return 1
    print("check_doc_profile_atoms: %s §6.1 — %d row(s) checked against the "
          "reader, all %d mapped UCUM codes present"
          % (DOC, checked, len(mapped)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
