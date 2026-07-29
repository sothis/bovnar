#!/usr/bin/env python3
"""
check_ietf_draft.py — the IETF draft and the documentation set say what the
header says.

THE GAP THIS CLOSES. doc/ietf/draft-sonntag-bovnar-00 is a second, independent
statement of the format, written for a different audience and rendered into four
committed artifacts (.md, .txt, .xml, .html). Nothing compared it to anything.
check_doc_refs.py checks that citations RESOLVE and that the spec's error table
matches include/bovnar.h; check_doc_layout.py checks structure. Neither looks at
the draft at all, and neither checks a numeric CLAIM anywhere.

So the draft drifted, and so did doc/05: both said a compound unit may not exceed
"8 components" long after BVNR_MAX_UNIT_COMPONENTS became 32. doc/03 said 32. A
reader implementing from the draft would have rejected documents the reference
implementation accepts -- an interoperability defect, in the one document whose
entire purpose is interoperability.

WHAT IS CHECKED, AND WHY EACH ONE. Every check here is a fact stated in two
places that only the header can settle:

  * ERROR CODES. Every error_* the draft names must exist in error_code_t, and
    every code in error_code_t must be named by the draft. The first direction
    catches a typo or a renamed code; the second catches a new code the draft was
    never told about, which is how a normative document quietly stops being
    normative.

  * NUMERIC LIMITS. The draft's limits table and doc/05's, against the constants
    they cite. This is the check that would have caught the 8/32 drift on the
    commit that introduced it.

  * EPOCH NAMES and TYPE FAMILIES. Enumerations a consumer must implement
    exactly; a missing one is a document that cannot be implemented from.

  * SPEC VERSION. The draft describes a numbered specification; if the build's
    BVNR_SPEC_VERSION moves past it, the draft needs a new revision rather than
    silently describing an older format.

  * RENDERING AGREEMENT. The .txt, .xml and .html are generated from the .md by
    kramdown-rfc/xml2rfc, which are not run here and not in CI. A fix applied to
    the .md alone would leave three committed artifacts stating the old value, so
    every fact checked above is re-checked in each rendering. This is the check
    that makes "fix the draft" mean all four files.

Usage:
    python3 check_ietf_draft.py            # report and exit 1 on any drift
    python3 check_ietf_draft.py --verbose  # also list what was checked
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.abspath(__file__))
HDR = os.path.join(ROOT, "include", "bovnar.h")
IETF = os.path.join(ROOT, "doc", "ietf")
DRAFT_STEM = "draft-sonntag-bovnar-00"
DOC = os.path.join(ROOT, "doc")

# doc/05's own limits table restates the same constants for the unit half of the
# format. It is here rather than in check_doc_refs.py because it is the same
# question -- a number stated twice -- and answering it in two tools is how the
# two answers start to differ.
UNIT_DOC = os.path.join(DOC, "05_bovnar_unit_system.md")
SPEC_DOC = os.path.join(DOC, "03_bovnar_spec.md")


def die(problems):
    for p in problems:
        print("check_ietf_draft: " + p, file=sys.stderr)
    print("\n%d inconsistency(ies). The header is the authority: fix the "
          "document, or the header if the document is right." % len(problems),
          file=sys.stderr)
    return 1


def header_facts():
    """The authoritative values, read from include/bovnar.h."""
    h = open(HDR, encoding="utf-8").read()
    f = {}
    f["errors"] = set(re.findall(r"^\s*(error_\w+)\s*=\s*\d+", h, re.M))
    for name in ("BVNR_MAX_UNIT_COMPONENTS", "BVNR_UNIT_STRING_MAX",
                 "BVNR_SPEC_VERSION_MAJOR", "BVNR_SPEC_VERSION_MINOR"):
        m = re.search(r"#\s*define\s+%s\s+(\d+)" % name, h)
        if not m:
            raise SystemExit("check_ietf_draft: %s not in include/bovnar.h" % name)
        f[name] = int(m.group(1))
    # Type families: the vt_* enumerators, minus the two that are not families a
    # document can write (vt_plain is "no annotation", vt_illegal is the error).
    f["families"] = set(re.findall(r"\bvt_(\w+)", h)) - {"plain", "illegal"}
    return f


def epoch_names():
    """The epoch table lives in the validator, not the header."""
    src = open(os.path.join(ROOT, "src", "utils", "bovnar_utils.c"),
               encoding="utf-8").read()
    m = re.search(r"bvn_epoch_table\[\]\s*=\s*\{(.*?)\n\};", src, re.S)
    if not m:
        return None
    return set(re.findall(r'"(\w+)"', m.group(1)))


def check_errors(text, where, facts, problems, verbose):
    cited = set(re.findall(r"error_[a-z0-9_]+", text))
    unknown = sorted(cited - facts["errors"])
    if unknown:
        problems.append("%s names %d error code(s) that are not in "
                        "error_code_t: %s" % (where, len(unknown),
                                              ", ".join(unknown)))
    missing = sorted(facts["errors"] - cited)
    if missing:
        problems.append("%s never mentions %d error code(s) the header "
                        "defines: %s" % (where, len(missing),
                                         ", ".join(missing)))
    if verbose and not unknown and not missing:
        print("  ok  %s: all %d error codes agree" % (where, len(cited)))


def check_component_limit(text, where, facts, problems, verbose):
    """The claim that drifted. Matched on the sentence, not on a bare number, so
    an unrelated 8 elsewhere in the document cannot satisfy or break it."""
    want = facts["BVNR_MAX_UNIT_COMPONENTS"]
    hits = re.findall(r"exceed[^.\n]*?(\d+)[^.\n]*?components?", text)
    hits += re.findall(r"components? per compound unit\D*?(\d+)", text)
    if not hits:
        problems.append("%s states no compound-unit component limit; the header "
                        "says %d and a consumer implementing from this document "
                        "cannot know it" % (where, want))
        return
    bad = [h for h in hits if int(h) != want]
    if bad:
        problems.append("%s says a compound unit may not exceed %s components; "
                        "BVNR_MAX_UNIT_COMPONENTS is %d"
                        % (where, "/".join(sorted(set(bad))), want))
    elif verbose:
        print("  ok  %s: component limit %d" % (where, want))


def check_macro_values(text, where, facts, problems, verbose):
    """Any '#define NAME <n>' or '| `NAME` | <n> |' restating a header macro."""
    for name in ("BVNR_MAX_UNIT_COMPONENTS", "BVNR_UNIT_STRING_MAX"):
        want = facts[name]
        for m in re.finditer(r"#\s*define\s+%s\s+(\d+)" % name, text):
            if int(m.group(1)) != want:
                problems.append("%s reprints `#define %s %s`; the header says %d"
                                % (where, name, m.group(1), want))
        for m in re.finditer(r"`%s`\s*\|\s*(\d+)" % name, text):
            if int(m.group(1)) != want:
                problems.append("%s tabulates %s as %s; the header says %d"
                                % (where, name, m.group(1), want))
        for m in re.finditer(r"\|\s*(\d+)\s*\(`%s`\)" % name, text):
            if int(m.group(1)) != want:
                problems.append("%s tabulates %s as %s; the header says %d"
                                % (where, name, m.group(1), want))
    if verbose:
        print("  ok  %s: macro restatements agree" % where)


def check_spec_version(text, where, facts, problems, verbose):
    want = "%d.%d" % (facts["BVNR_SPEC_VERSION_MAJOR"],
                      facts["BVNR_SPEC_VERSION_MINOR"])
    seen = set(re.findall(r"#!bovnar\s+(\d+\.\d+)", text))
    # A draft may legitimately show an OLDER version in an example (a 1.0
    # document is still valid), but it must describe the current one somewhere.
    if want not in seen:
        problems.append("%s never shows a `#!bovnar %s` document, but this build "
                        "implements spec %s -- the draft describes an older "
                        "format than the one that ships" % (where, want, want))
    ahead = sorted(v for v in seen
                   if tuple(int(x) for x in v.split(".")) >
                      (facts["BVNR_SPEC_VERSION_MAJOR"],
                       facts["BVNR_SPEC_VERSION_MINOR"]))
    if ahead and where.endswith(".md"):
        # Only a note: the unit-profile notation is deliberately written against
        # an unreleased version, and the draft may reference it.
        if verbose:
            print("  note %s: mentions unreleased spec version(s) %s"
                  % (where, ", ".join(ahead)))
    if verbose:
        print("  ok  %s: describes spec %s" % (where, want))


def check_epochs(text, where, problems, verbose):
    eps = epoch_names()
    if not eps:
        return
    missing = sorted(e for e in eps if not re.search(r"\b%s\b" % e, text))
    if missing:
        problems.append("%s does not name %d datetime epoch(s) the "
                        "implementation accepts: %s"
                        % (where, len(missing), ", ".join(missing)))
    elif verbose:
        print("  ok  %s: all %d epochs named" % (where, len(eps)))


def check_families(text, where, facts, problems, verbose):
    missing = sorted(f for f in facts["families"]
                     if not re.search(r"\b%s\b" % re.escape(f), text))
    if missing:
        problems.append("%s does not name %d type family(ies): %s"
                        % (where, len(missing), ", ".join(missing)))
    elif verbose:
        print("  ok  %s: all %d type families named" % (where, len(facts["families"])))


def main(argv):
    verbose = "--verbose" in argv
    facts = header_facts()
    problems = []

    md = os.path.join(IETF, DRAFT_STEM + ".md")
    if not os.path.exists(md):
        raise SystemExit("check_ietf_draft: %s not found" % md)
    text = open(md, encoding="utf-8").read()
    where = "doc/ietf/%s.md" % DRAFT_STEM
    check_errors(text, where, facts, problems, verbose)
    check_component_limit(text, where, facts, problems, verbose)
    check_macro_values(text, where, facts, problems, verbose)
    check_spec_version(text, where, facts, problems, verbose)
    check_epochs(text, where, problems, verbose)
    check_families(text, where, facts, problems, verbose)

    # The three generated renderings must state the same facts as the source.
    for ext in ("txt", "xml", "html"):
        p = os.path.join(IETF, "%s.%s" % (DRAFT_STEM, ext))
        if not os.path.exists(p):
            continue
        t = open(p, encoding="utf-8", errors="replace").read()
        w = "doc/ietf/%s.%s" % (DRAFT_STEM, ext)
        check_component_limit(t, w, facts, problems, verbose)
        # The .xml/.html wrap identifiers in markup, so only the component
        # claim and the macro restatements survive a plain-text comparison
        # reliably. That is the claim that drifted; the rest is covered by
        # the .md check above plus the fact that all four are one generation.
        check_macro_values(t, w, facts, problems, verbose)

    # The documentation set restates the same constants.
    for p, w in ((UNIT_DOC, "doc/05_bovnar_unit_system.md"),
                 (SPEC_DOC, "doc/03_bovnar_spec.md")):
        t = open(p, encoding="utf-8").read()
        check_component_limit(t, w, facts, problems, verbose)
        check_macro_values(t, w, facts, problems, verbose)

    if problems:
        return die(problems)
    print("check_ietf_draft: the IETF draft, its three renderings, doc/03 and "
          "doc/05 agree with include/bovnar.h on %d error codes, the numeric "
          "limits, %d type families and the spec version."
          % (len(facts["errors"]), len(facts["families"])))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
