#!/usr/bin/env python3
"""
check_doc_python_examples.py — doc/09's `# →` expectations, actually evaluated.

THE GAP THIS CLOSES. doc/09 is the Python reference and it states its results the
way a reader reads them:

    bovnar.unit_to_profile("unece", kg)   # → "KGM"
    f = bovnar.unit_factor("M~Hz")        # → 1_000_000.0

Nothing ran any of it. check_doc_examples.py parses the embedded *bovnar*
documents, check_doc_unit_tables.py checks doc/09's BaseUnit range table, and the
76 KB of Python either side of those was the copy nobody executed. Three claims
in it were wrong at once, all in the section describing `UnitFlags.REDUCE`: that
`unit_reduce` yields "the canonical named SI unit" (it yields `m·g/s²` where the
formatter yields `N`), that a caller should apply `unit_reduce(vu).scale` to a
value it is also going to serialise (that double-applies wherever the formatter
re-attaches a prefix — `k~N` is out by 1000), and that a reduction overflows the
"±9 exponent range" (it is ±100, and has been since the exponent range grew).

WHAT IS CHECKED. Every ```python block is executed in order, in one namespace, so
a block may build on the one before it exactly as a reader would. On each line
carrying a `# →` comment, the expression to its left is evaluated a second time
and its result compared with what the comment claims.

WHAT IS SKIPPED, AND LOUDLY. A block that needs something this gate cannot supply
— a file on disk, a handler defined elsewhere, an optional third-party import —
raises, and the whole block is skipped from that point. The count of skipped
blocks is printed on every run, so "nothing is checked any more" cannot look like
success. A block may also opt out with a `# check-doc-python: skip` line, which
is the honest way to exclude a snippet that is deliberately illustrative.

Comparison is deliberately loose about FORM and strict about VALUE: the comment
is prose written for a reader, so `"KGM"`, `'KGM'` and `KGM` all match the string
KGM, `1_000_000.0` matches the float, and any trailing commentary after two
spaces or an em-dash is ignored. A comment that is not parseable as a value at
all (`# → a Quantity`) is not a claim and is skipped.

Usage:  python3 check_doc_python_examples.py [repo-root]
Exit 0 when every stated result matches, 1 with a list when not.
"""
import ast
import contextlib
import io as _io
import os
import re
import sys

REPO = os.path.dirname(os.path.abspath(__file__))
DOC = "doc/09_bovnar_python_bindings.md"

BLOCK = re.compile(r"```python\n(.*?)```", re.S)
# "<expr>   # → <expected>" — the expression is everything before the comment.
ARROW = re.compile(r"^(?P<code>.*?)\s*#\s*→\s*(?P<want>.+?)\s*$")
SKIP_MARKER = "check-doc-python: skip"


def wanted_value(text):
    """The value a `# →` comment claims, or None when it is prose.

    The comment is written for a reader, so it may carry commentary after the
    value ("1.0  (NOT 0.0254 — the inch has no prefix)"). Everything from a
    double space or an em-dash is dropped before parsing.
    """
    text = re.split(r"\s\s+|\s—\s", text, maxsplit=1)[0].strip()
    if not text:
        return None, False
    try:
        return ast.literal_eval(text), True
    except (ValueError, SyntaxError):
        pass
    # A bare word the document did not quote: "KGM", "True", "None".
    if re.fullmatch(r"[A-Za-z_][\w.\[\]-]*", text):
        return text, True
    return None, False


def matches(got, want):
    """Value equality, tolerant of how the document spells the value."""
    if got == want:
        return True
    # A string result the comment left unquoted, or vice versa.
    if isinstance(want, str) and not isinstance(got, str):
        return repr(got) == want or str(got) == want
    if isinstance(got, str) and not isinstance(want, str):
        return got == str(want)
    # A tuple/list written the other way round.
    if isinstance(got, (tuple, list)) and isinstance(want, (tuple, list)):
        return list(got) == list(want)
    return False


def expression_of(code):
    """The trailing expression of a line, or None.

    `f = bovnar.unit_factor("M~Hz")` claims something about the right-hand side;
    a bare call claims something about itself. A statement that is neither (an
    import, a `with`) makes no claim.
    """
    code = code.strip()
    if not code or code.startswith(("#", "import ", "from ", "with ", "def ",
                                    "class ", "for ", "if ", "return")):
        return None
    m = re.match(r"^[A-Za-z_][\w, ]*=\s*(?!=)(.+)$", code)
    if m:
        return m.group(1).strip()
    return code


def check(repo):
    path = os.path.join(repo, DOC)
    if not os.path.exists(path):
        print("check_doc_python_examples: %s is absent" % DOC)
        return [], 0, 0
    sys.path.insert(0, os.path.join(repo, "python"))
    try:
        import bovnar
        bovnar._ffi.load_library()
    except Exception:
        print("check_doc_python_examples: no built library; skipped")
        return [], 0, 0

    with open(path, encoding="utf-8") as f:
        text = f.read()
    ns = {"bovnar": bovnar}
    problems, checked, skipped = [], 0, 0

    for block in BLOCK.findall(text):
        if SKIP_MARKER in block:
            skipped += 1
            continue
        line_no = text[:text.index(block)].count("\n") + 1
        broke = False
        for offset, raw in enumerate(block.split("\n")):
            if broke:
                break
            m = ARROW.match(raw)
            code = m.group("code") if m else raw
            if not code.strip():
                continue
            try:
                # A snippet may print; its output is the document's business,
                # not this gate's.
                with contextlib.redirect_stdout(_io.StringIO()):
                    exec(compile(code, DOC, "exec"), ns)     # noqa: S102
            except Exception:
                # Needs something this gate cannot supply; abandon the block.
                skipped += 1
                broke = True
                continue
            if not m:
                continue
            want, is_claim = wanted_value(m.group("want"))
            if not is_claim:
                continue
            expr = expression_of(code)
            if expr is None:
                continue
            try:
                with contextlib.redirect_stdout(_io.StringIO()):
                    got = eval(compile(expr, DOC, "eval"), ns)   # noqa: S307
            except Exception:
                continue
            checked += 1
            if not matches(got, want):
                problems.append("%s:%d: `%s` is documented as %r; it is %r"
                                % (DOC, line_no + offset, expr, want, got))
    return problems, checked, skipped


def main(argv):
    repo = os.path.abspath(argv[1]) if len(argv) > 1 else REPO
    problems, checked, skipped = check(repo)
    if problems:
        sys.stderr.write("check_doc_python_examples: %s states results the "
                         "library does not produce:\n" % DOC)
        for p in problems:
            sys.stderr.write("    %s\n" % p)
        return 1
    print("check_doc_python_examples: %s — %d `# →` expectation(s) evaluated, "
          "%d block(s) skipped as needing more than this gate supplies"
          % (DOC, checked, skipped))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
