#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Janos Sonntag
"""check_doc_examples.py — the documents embedded in the docs must parse.

EVERY ```bovnar fence in README.md, web/index.md and doc/*.md is handed to the
reference reader — 212 of them. Nothing did this before, and the front door was
wrong because of it: README.md's first example — the first bovnar document most
readers ever see — opened with

    #!bovnar 1.1                      # optional spec-version directive

which is `error_invalid_spec_version`. The spec says so four lines above its own
example block: a directive followed by "trailing junk" is malformed, and the
annotation is the junk. Conformance case VER-013 asserts the rejection. So the
reader, the conformance suite and the prose all agreed, and the two most-read
examples in the project disagreed with all three.

That is the class this catches: prose that is right, and an example beside it
that would not survive contact with the parser.

NOT EVERY FENCE IS A DOCUMENT, AND NOT EVERY DOCUMENT IS MEANT TO PARSE

Two kinds of block have to be exempt, and they are exempt by SAYING SO rather
than by being guessed at:

  * an ILLUSTRATION with elided content. README's example writes
    `.payload = \\x00 … binary stream … \\x00` — a placeholder, not syntax. The
    block teaches shape and was never a runnable file.
  * a NEGATIVE example, deliberately showing what is refused. doc/11 has a line
    annotated `# error_unit_mismatch, as always`; the block would be worthless
    if it parsed.

Mark either with an HTML comment on the line before the fence:

    <!-- bovnar-example: illustrative -->   (contains elisions/placeholders)
    <!-- bovnar-example: rejected -->       (must NOT parse)

`rejected` is checked in the negative — a block marked so that starts parsing
cleanly is a failure too, because the example has stopped demonstrating what it
claims. `illustrative` is the only real escape hatch, and it is deliberately
ugly to type so it is not reached for by habit.

WHY THE SPLIT IS NOT "HAS A DIRECTIVE"

This began by checking only fences that opened with `#!bovnar`, which was 24 of
212: the directive is OPTIONAL, so the tutorial and the spec mostly omit it, and
those 188 blocks were skipped while the tool printed an unqualified success. They
are all classified and all checked now.

Classifying them turned up nothing broken, which is the useful result — but two
things are worth recording for whoever adds the next example:

  * A PROFILE UNIT NEEDS THE DIRECTIVE. `<float:64,udunits:m s-1>` is
    `error_unit_illegal` in a document that declares nothing, because spec 1.2 is
    where profile units exist (doc/03 §11.7). Several spec fragments show one
    without a directive; they are `rejected` on that account as much as on the
    deliberate error they also carry. An example meant to PARSE and using a
    profile unit must open with `#!bovnar 1.2`.
  * `\\xNN` IN THE DOCS IS A DEPICTION OF BYTES, not syntax an octet stream
    accepts. Blocks built that way are `illustrative`; they were never files.

Five blocks show a refusal with the offending line COMMENTED OUT (`# .bad = ...
# ERROR: ...`). Those parse, correctly, and stay unmarked — the technique keeps
the block runnable while still showing the reader the bad form, and it is the one
to prefer over a `rejected` marker where it fits.
"""

import os
import re
import subprocess
import sys
import tempfile

REPO = os.path.dirname(os.path.abspath(__file__))

# Where a reader meets an example. web/index.md is included because it is the
# site's front page and is the same example as the README's -- the pair drifted
# together and must be checked together.
SOURCES = ["README.md", "web/index.md"] + sorted(
    os.path.join("doc", f) for f in os.listdir(os.path.join(REPO, "doc"))
    if f.endswith(".md"))

FENCE = re.compile(r"(?:^|\n)([^\n]*)\n```bovnar\n(.*?)```", re.S)

MARK = re.compile(r"<!--\s*bovnar-example:\s*(illustrative|rejected)\s*-->")


def find_cli():
    """The built CLI, or None. Env override first so a packager can point at an
    installed binary rather than a build tree."""
    env = os.environ.get("BOVNAR_CLI")
    if env and os.path.exists(env):
        return env
    for rel in ("build/bovnar", "build/bin/bovnar", "bovnar"):
        p = os.path.join(REPO, rel)
        if os.path.exists(p) and os.access(p, os.X_OK):
            return p
    return None


def parses(cli, text):
    """(ok, message). The CLI's `events` walks the whole document, so a failure
    anywhere in the block is a failure here."""
    fd, path = tempfile.mkstemp(suffix=".bvnr")
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as f:
            f.write(text)
        r = subprocess.run([cli, "events", path],
                           capture_output=True, text=True)
        if r.returncode == 0:
            return True, ""
        out = ((r.stderr or "") + (r.stdout or "")).strip().splitlines()
        # The CLI frames its output; the useful line is the diagnostic, not the
        # box drawing that follows it.
        for ln in out:
            if re.search(r"RECOVERY|error|refus|illegal|invalid", ln, re.I):
                return False, ln.strip()[:160]
        return False, (out[-1][:160] if out else "(no diagnostic)")
    finally:
        os.unlink(path)


def main(argv):
    verbose = "--verbose" in argv[1:]
    cli = find_cli()
    if not cli:
        print("check_doc_examples: SKIPPED — no built CLI (set BOVNAR_CLI)")
        return 0

    problems = []
    checked = illustrative = rejected = undirected = 0

    for rel in SOURCES:
        path = os.path.join(REPO, rel)
        if not os.path.exists(path):
            continue
        text = open(path, encoding="utf-8").read()
        for m in FENCE.finditer(text):
            preceding, body = m.group(1), m.group(2)
            if not body.lstrip().startswith("#!bovnar"):
                # The version directive is OPTIONAL, so most examples carry
                # none. This once skipped every one of them -- 188 blocks
                # against 24 checked -- and printed an unqualified success. They
                # are all classified now and all checked; `undirected` is kept
                # only so the split stays visible in the summary.
                undirected += 1
            line = text[:m.start(2)].count("\n") + 1
            where = "%s:%d" % (rel, line)
            mark = MARK.search(preceding)
            kind = mark.group(1) if mark else None

            # Checked for EVERY fence, including the illustrative ones, because
            # the directive line is the one line an illustration cannot elide
            # and the one a reader copies verbatim. `#!bovnar 1.1  # note` is
            # error_invalid_spec_version -- the annotation is the "trailing
            # junk" the rule forbids -- and that is exactly how README.md and
            # web/index.md both opened before this existed.
            first = body.lstrip("﻿ \t\n").split("\n", 1)[0]
            junk = re.match(r"(#!bovnar\s+\d+\.\d+)(\s*\S.*)$", first)
            if junk:
                problems.append(
                    "%s: the spec-version directive carries trailing text "
                    "(%r) — that is error_invalid_spec_version, not an "
                    "annotated directive. Put the note on the next line."
                    % (where, junk.group(2).strip()[:60]))

            if kind == "illustrative":
                illustrative += 1
                continue

            ok, msg = parses(cli, body)

            if kind == "rejected":
                rejected += 1
                if ok:
                    problems.append(
                        "%s is marked `bovnar-example: rejected` but parses "
                        "cleanly — it no longer demonstrates a refusal" % where)
                elif verbose:
                    print("  ok  %s rejected as intended: %s" % (where, msg))
                continue

            checked += 1
            if not ok:
                problems.append("%s does not parse: %s" % (where, msg))
            elif verbose:
                print("  ok  %s" % where)

    if problems:
        print("check_doc_examples: %d embedded example(s) disagree with the "
              "reference reader:" % len(problems))
        for p in problems:
            print("  %s" % p)
        print("\nIf a block is a sketch rather than a file, mark the line "
              "before its fence with\n    <!-- bovnar-example: illustrative "
              "-->\nand if it is meant to be refused, with\n    "
              "<!-- bovnar-example: rejected -->")
        return 1

    print("check_doc_examples: %d embedded document(s) parse, %d refuse as "
          "marked, %d illustrative" % (checked, rejected, illustrative))
    if undirected:
        print("  (%d of them carry no #!bovnar directive; the directive is "
              "optional and they are checked the same way)" % undirected)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
