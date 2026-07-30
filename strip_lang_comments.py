#!/usr/bin/env python3
"""strip_lang_comments.py -- strip comments from Python, CMake and HTML files.

The companion to strip_comments.sh, which does the same for C99. merge.sh runs
both over a throwaway staging copy of the tree to build the comment-free half of
the bundle; neither tool is meant to be pointed at the working tree.

Why not a regex per language
----------------------------
Every one of these three formats has a way to write the comment marker without
starting a comment, and this repository uses all three:

    CMake   string(REGEX MATCHALL "# Subtest: [a-z_]+" _groups "${_tap}")
            "#!bovnar 1.1\\n"                     -- a hash inside a quoted
            [[literal #hash]]                        argument or a bracket
                                                     argument is data
    Python  sep = "#"                             -- and a hash inside any
                                                     string literal
    HTML    <script>if (a<!--b) ...</script>      -- script and style content is
                                                     raw text, not markup

A line-oriented `s/#.*//` corrupts the CTest registry in exactly that first way,
so each language is scanned with something that knows its literals: Python's own
`tokenize`, `html.parser` (which implements the HTML5 raw-text rules), and for
CMake a scanner for the four argument forms in cmake-language(7).

Verification
------------
Stripping is only allowed to remove comments, so every file is checked before it
is written, and a file that fails its check is left alone and reported:

    Python  ast.dump(ast.parse(...)) must be identical before and after, which
            makes the two files the same program by construction
    CMake   the token stream (arguments, parens, nesting) minus comments must be
            identical
    HTML    the parse event stream -- tags, attributes, text with insignificant
            whitespace collapsed, exact text inside <pre>/<textarea> -- must be
            identical

What is kept
------------
Comments that are not documentation but interface:

    * an SPDX/copyright header block, in any of the three languages -- the MIT
      licence requires the notice to be retained (same rule strip.sh applies to
      C)
    * a `#!` shebang on line 1, and a PEP 263 `# -*- coding: ... -*-` cookie on
      line 1 or 2: Python reads both
    * IE conditional comments (`<!--[if ...]> ... <![endif]-->`), which are
      markup wearing a comment's clothes

Lint, typing and coverage pragmas (`# noqa`, `# type:`, `# pragma: no cover`)
are NOT kept. They address tools that never run against this bundle, and the
verification above proves they do not change the program.

Formats outside the three named are left untouched, deliberately: TOML, JSON,
Markdown, XML and .bvnr comments are not stripped, and neither are JavaScript or
CSS comments inside an HTML <script>/<style> element.

Usage
-----
    strip_lang_comments.py FILE                 strip to stdout
    strip_lang_comments.py -i FILE [FILE ...]   strip in place
    strip_lang_comments.py -c FILE [FILE ...]   verify only, write nothing
    strip_lang_comments.py -v ...               name each file on stderr

Exit status
    0   every file handled (a file of an unknown type is a no-op)
    1   usage error
    3   one or more files failed; the rest were still processed
"""

import ast
import io
import os
import re
import sys
import tokenize
from html.parser import HTMLParser

PROG = os.path.basename(sys.argv[0])

# How much of the line a cut may take with it.
DROP = "drop"     # the comment, the whitespace it leaves at end of line, and
                  # the whole line if the comment was all that was on it
EXACT = "exact"   # the comment span and not one byte more -- for a comment
                  # inside <pre>, where the surrounding whitespace is content


class StripError(Exception):
    """A file could not be stripped, or could not be proven unchanged."""


# --------------------------------------------------------------------------
# shared


def apply_cuts(src, cuts):
    """Remove (start, end, mode) spans from src, working backwards."""
    text = src
    for start, end, mode in sorted(cuts, key=lambda c: c[0], reverse=True):
        line_start = text.rfind("\n", 0, start) + 1
        line_end = text.find("\n", end)
        if line_end == -1:
            line_end = len(text)
        before, after = text[line_start:start], text[end:line_end]
        if mode == DROP and not before.strip() and not after.strip():
            # The comment was the whole line: take the newline with it, rather
            # than leaving a blank line behind for every comment removed.
            tail = line_end + 1 if line_end < len(text) else line_end
            text = text[:line_start] + text[tail:]
            continue
        line = before + after
        if mode != EXACT and not after.strip():
            line = line.rstrip()
        text = text[:line_start] + line + text[line_end:]
    return text


def spdx_header_rows(lines, is_comment):
    """Rows (1-based) of the leading comment block, if it carries an SPDX tag.

    The block is the run of comment lines at the top of the file, so the
    copyright line and the licence text that follow the tag are kept with it.
    """
    rows, texts = [], []
    for n, line in enumerate(lines, 1):
        if not is_comment(line):
            break
        rows.append(n)
        texts.append(line)
    if "SPDX-License-Identifier" in "".join(texts):
        return set(rows)
    return set()


# --------------------------------------------------------------------------
# Python

CODING_COOKIE = re.compile(r"^[ \t\f]*#.*?coding[:=][ \t]*([-\w.]+)")


def strip_python(src):
    try:
        tokens = list(tokenize.generate_tokens(io.StringIO(src).readline))
    except (tokenize.TokenError, IndentationError, SyntaxError) as exc:
        raise StripError("not tokenizable as Python: %s" % exc)

    lines = src.splitlines(keepends=True)
    keep_rows = spdx_header_rows(lines, lambda l: l.lstrip().startswith("#"))
    if lines and lines[0].startswith("#!"):
        keep_rows.add(1)                       # the kernel reads this one
    for row in (1, 2):                         # PEP 263, first two lines only
        if len(lines) >= row and CODING_COOKIE.match(lines[row - 1]):
            keep_rows.add(row)

    # A line offset table, so a (row, col) token position becomes an index.
    starts, pos = [], 0
    for line in lines:
        starts.append(pos)
        pos += len(line)

    cuts = []
    for tok in tokens:
        if tok.type != tokenize.COMMENT:
            continue
        row = tok.start[0]
        if row in keep_rows:
            continue
        cuts.append((starts[row - 1] + tok.start[1],
                     starts[tok.end[0] - 1] + tok.end[1], DROP))
    out = apply_cuts(src, cuts)
    verify_python(src, out)
    return out


def verify_python(old, new):
    try:
        before = ast.dump(ast.parse(old))
    except SyntaxError as exc:
        raise StripError("does not parse as Python before stripping: %s" % exc)
    try:
        after = ast.dump(ast.parse(new))
    except SyntaxError as exc:
        raise StripError("stripping produced invalid Python: %s" % exc)
    if before != after:
        raise StripError("stripping changed the Python syntax tree")


# --------------------------------------------------------------------------
# CMake

BRACKET_OPEN = re.compile(r"\[(=*)\[")


def scan_cmake(src):
    """Split CMake source into (kind, start, end) spans.

    kind is 'comment', 'arg' (a quoted or bracket argument, or a run of
    unquoted argument text), 'punct' for ( and ), or 'space'.  cmake-language(7):
    a # begins a comment unless it is escaped, inside a quoted argument or
    inside a bracket argument, and #[[ ]] is a bracket comment.
    """
    spans = []
    i, n = 0, len(src)
    arg_start = True   # a bracket argument is only recognised where one begins
    while i < n:
        c = src[i]
        if c in " \t\r\n":
            j = i
            while j < n and src[j] in " \t\r\n":
                j += 1
            spans.append(("space", i, j))
            arg_start = True
            i = j
        elif c == "#":
            m = BRACKET_OPEN.match(src, i + 1)
            if m:
                close = "]" + m.group(1) + "]"
                end = src.find(close, m.end())
                if end == -1:
                    raise StripError("unterminated bracket comment at offset %d"
                                     % i)
                spans.append(("comment", i, end + len(close)))
                i = end + len(close)
            else:
                end = src.find("\n", i)
                end = n if end == -1 else end
                spans.append(("comment", i, end))
                i = end
            arg_start = True
        elif c == '"':
            j = i + 1
            while j < n:
                if src[j] == "\\":
                    j += 2
                    continue
                if src[j] == '"':
                    j += 1
                    break
                j += 1
            else:
                raise StripError("unterminated quoted argument at offset %d" % i)
            spans.append(("arg", i, j))
            arg_start = False
            i = j
        elif c == "[" and arg_start and BRACKET_OPEN.match(src, i):
            m = BRACKET_OPEN.match(src, i)
            close = "]" + m.group(1) + "]"
            end = src.find(close, m.end())
            if end == -1:
                raise StripError("unterminated bracket argument at offset %d"
                                 % i)
            spans.append(("arg", i, end + len(close)))
            arg_start = False
            i = end + len(close)
        elif c in "()":
            spans.append(("punct", i, i + 1))
            arg_start = True
            i += 1
        else:
            # An unquoted argument: runs to whitespace, a paren, or an
            # unescaped # or ".
            j = i
            while j < n:
                if src[j] == "\\":
                    j += 2
                    continue
                if src[j] in ' \t\r\n()#"':
                    break
                j += 1
            spans.append(("arg", i, min(j, n)))
            arg_start = False
            i = min(j, n)
    return spans


def cmake_tokens(src):
    """The significant tokens of src: arguments and parens, comments dropped."""
    return [src[s:e] for kind, s, e in scan_cmake(src)
            if kind in ("arg", "punct")]


def strip_cmake(src):
    spans = scan_cmake(src)
    lines = src.splitlines(keepends=True)
    keep_rows = spdx_header_rows(lines, lambda l: l.lstrip().startswith("#"))

    cuts = []
    for kind, start, end in spans:
        if kind != "comment":
            continue
        row = src.count("\n", 0, start) + 1
        if row in keep_rows:
            continue
        cuts.append((start, end, DROP))
    out = apply_cuts(src, cuts)
    verify_cmake(src, out)
    return out


def verify_cmake(old, new):
    if cmake_tokens(old) != cmake_tokens(new):
        raise StripError("stripping changed the CMake token stream")


# --------------------------------------------------------------------------
# HTML

RAW_TEXT = {"pre", "textarea"}          # whitespace is significant in here
CONDITIONAL = re.compile(r"^\s*\[\s*(if|endif)", re.I)


class CommentLocator(HTMLParser):
    """Record every comment span, and whether it sits inside <pre>/<textarea>.

    html.parser switches to raw-text mode inside <script> and <style>, so a
    '<!--' in a script never reaches handle_comment and is never stripped.
    """

    def __init__(self, src):
        super().__init__(convert_charrefs=False)
        self.src = src
        self.starts = []
        pos = 0
        for line in src.splitlines(keepends=True):
            self.starts.append(pos)
            pos += len(line)
        self.stack = []
        self.comments = []      # (start, end, in_raw_text, data)

    def _offset(self):
        row, col = self.getpos()
        return self.starts[row - 1] + col

    def handle_starttag(self, tag, attrs):
        if tag in RAW_TEXT:
            self.stack.append(tag)

    def handle_endtag(self, tag):
        if tag in self.stack:
            while self.stack and self.stack.pop() != tag:
                pass

    def handle_comment(self, data):
        start = self._offset()
        if not self.src.startswith("<!--", start):
            return                            # not a plain comment; leave it
        end = self.src.find("-->", start)
        if end == -1:
            return
        self.comments.append((start, end + 3, bool(self.stack), data))


def strip_html(src):
    loc = CommentLocator(src)
    try:
        loc.feed(src)
        loc.close()
    except Exception as exc:                  # html.parser is tolerant, but
        raise StripError("not parsable as HTML: %s" % exc)

    cuts = []
    for start, end, in_raw, data in loc.comments:
        if CONDITIONAL.match(data):
            continue                          # downlevel-revealed markup
        if "SPDX-License-Identifier" in data or "Copyright" in data:
            continue
        cuts.append((start, end, EXACT if in_raw else DROP))
    out = apply_cuts(src, cuts)
    verify_html(src, out)
    return out


class EventStream(HTMLParser):
    """The parse events of a document, comments excluded.

    Adjacent text is joined before comparison: removing a comment from between
    two pieces of text merges them into one node, which is the same document.
    Whitespace is collapsed outside <pre>/<textarea> and compared byte for byte
    inside.
    """

    def __init__(self):
        super().__init__(convert_charrefs=False)
        self.events = []
        self.buf = []
        self.stack = []

    def _flush(self):
        if not self.buf:
            return
        text = "".join(self.buf)
        self.buf = []
        if not any(t in RAW_TEXT for t in self.stack):
            text = " ".join(text.split())
            if not text:
                return
        self.events.append(("text", text))

    def handle_starttag(self, tag, attrs):
        self._flush()
        self.events.append(("start", tag, sorted(attrs)))
        if tag in RAW_TEXT:
            self.stack.append(tag)

    def handle_startendtag(self, tag, attrs):
        self._flush()
        self.events.append(("empty", tag, sorted(attrs)))

    def handle_endtag(self, tag):
        self._flush()
        self.events.append(("end", tag))
        if tag in self.stack:
            while self.stack and self.stack.pop() != tag:
                pass

    def handle_data(self, data):
        self.buf.append(data)

    def handle_entityref(self, name):
        self.buf.append("&%s;" % name)

    def handle_charref(self, name):
        self.buf.append("&#%s;" % name)

    def handle_decl(self, decl):
        self._flush()
        self.events.append(("decl", decl))

    def handle_pi(self, data):
        self._flush()
        self.events.append(("pi", data))

    def unknown_decl(self, data):
        self._flush()
        self.events.append(("unknown-decl", data))

    def handle_comment(self, data):
        # Deliberately not flushed: 'a<!--x-->b' is two text nodes before the
        # strip and one after, and those are the same document.  Letting the
        # buffer run across the comment is what makes the two compare equal.
        pass

    def parse(self, src):
        self.feed(src)
        self.close()
        self._flush()
        return self.events


def verify_html(old, new):
    if EventStream().parse(old) != EventStream().parse(new):
        raise StripError("stripping changed the HTML parse events")


# --------------------------------------------------------------------------
# dispatch

def stripper_for(path):
    name = os.path.basename(path)
    lower = name.lower()
    if lower.endswith(".py"):
        return strip_python
    # CMakeLists.txt, and this repo's second registry CMakeLists_tests.txt.
    if lower.endswith((".cmake", ".cmake.in")) \
            or re.match(r"^cmakelists.*\.txt$", lower):
        return strip_cmake
    if lower.endswith((".html", ".htm")):
        return strip_html
    return None


def main(argv):
    in_place = check_only = verbose = False
    files = []
    for arg in argv[1:]:
        if arg in ("-i", "--in-place"):
            in_place = True
        elif arg in ("-c", "--check"):
            check_only = True
        elif arg in ("-v", "--verbose"):
            verbose = True
        elif arg in ("-h", "--help"):
            sys.stdout.write(__doc__)
            return 0
        elif arg.startswith("-") and arg != "-":
            sys.stderr.write("%s: unknown option: %s\n" % (PROG, arg))
            return 1
        else:
            files.append(arg)

    if not files:
        sys.stderr.write("%s: no input files\n" % PROG)
        return 1
    if not (in_place or check_only) and len(files) > 1:
        sys.stderr.write("%s: refusing to write %d files to stdout; "
                         "use -i or -c\n" % (PROG, len(files)))
        return 1

    failed = 0
    for path in files:
        strip = stripper_for(path)
        if strip is None:
            if verbose:
                sys.stderr.write("%s: skipped (unknown type)\n" % path)
            continue
        if verbose:
            sys.stderr.write("%s\n" % path)
        try:
            with open(path, "r", encoding="utf-8") as fh:
                src = fh.read()
        except (OSError, UnicodeDecodeError) as exc:
            sys.stderr.write("%s: error: %s: %s\n" % (PROG, path, exc))
            failed += 1
            continue
        try:
            out = strip(src)
        except StripError as exc:
            sys.stderr.write("%s: error: %s: %s\n" % (PROG, path, exc))
            failed += 1
            continue
        if check_only:
            continue
        if in_place:
            if out != src:
                tmp = path + ".strip.tmp"
                with open(tmp, "w", encoding="utf-8") as fh:
                    fh.write(out)
                os.replace(tmp, path)
        else:
            sys.stdout.write(out)
    return 3 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
