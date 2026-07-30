#!/usr/bin/env python3
"""strip_lang_comments.py -- strip comments, in every language this tree uses.

C, Bovnar, Python, CMake, HTML, XML, JavaScript, CSS and Markdown. merge.sh runs
it over a throwaway staging copy to build the comment-free half of the bundle;
it is not meant to be pointed at the working tree.

It replaces two scripts. strip_comments.sh did C only, with no verification, and
removed the licence header along with everything else -- so both of its callers
carried an awk to cut that block out first and paste it back after. strip.sh was
the second of those callers: it rewrote src/ and include/ in the working tree in
place, which is why it needed a clean-tree guard and a --force. The C path here
is byte-for-byte compatible with strip_comments.sh, checked over all 82 C files
in the tree before that script was deleted.

Why not a regex per language
----------------------------
Every one of these formats has a way to write the comment marker without
starting a comment, and this repository uses most of them:

    C       char *s = "/* not a comment */";
    CMake   string(REGEX MATCHALL "# Subtest: [a-z_]+" _groups "${_tap}")
            "#!bovnar 1.1\\n"                     -- a hash inside a quoted or a
            [[literal #hash]]                        bracket argument is data
    Python  sep = "#"                             -- and inside any string
    Bovnar  .k = "a#b";  and an octet stream, whose length-prefixed chunks
            carry arbitrary bytes: '#', LF, NUL, anything
    HTML    <script>if (a<!--b) ...</script>      -- script/style content is
                                                     raw text, not markup
    XML     <a b="<!--">, <![CDATA[ <!-- ]]>
    JS      url = "http://x"; re = /[/*]/;        -- 19 of the '//' in the
                                                     vendored highlighter are
                                                     of this kind
    CSS     content: "/*";
    MD      a fenced block containing <!-- -->    -- literal example text

So each language is scanned with something that knows its literals: Python's
own `tokenize`, `html.parser` (which implements the HTML5 raw-text rules), and
hand-written scanners for the rest, each following that language's spec --
cmake-language(7), doc/12_bovnar.ebnf, CommonMark's fence rules.

Verification
------------
Stripping may only remove comments, so every file is checked before it is
written, and one that fails is left alone and reported rather than shipped:

    Python  ast.dump(ast.parse(...)) identical before and after, which makes
            the two files the same program by construction
    C       identical token stream, with comments reduced to separators on both
            sides -- so a/**/b staying two tokens is checked, not assumed. This
            is the weakest of the checks, because it shares its scanner with the
            strip; what actually proves the C path is building the stripped tree
            and running the suite, which merge.sh documents how to do
    XML     ElementTree canonicalisation identical (comments are not part of
            it, so equality proves nothing else moved)
    Bovnar  identical token stream, identical line count, and every octet
            stream byte-identical
    CMake   identical token stream (arguments, parens, nesting)
    JS      identical token stream, plus `node --check` in the same dialect
            (script or module) the original passed in, where node exists
    CSS     identical token stream
    HTML    identical parse events -- tags, attributes, text with
            insignificant whitespace collapsed, exact text inside <pre>
    MD      identical block structure: fences, headings and the code inside
            fenced/indented blocks byte-for-byte

A file whose ORIGINAL cannot be verified (XML that does not parse, JS that node
rejects) is skipped with a warning and counted, not stripped: unverifiable is a
reason to leave a file alone, never a reason to fail the bundle. A file whose
STRIPPED form fails verification is a hard error.

What is kept
------------
Comments that are not documentation but interface:

    * an SPDX/copyright header block, in every language that has one -- the MIT
      licence requires the notice to be retained -- and a vendored
      /*! ... @license ... */ banner, which is the same obligation wearing a
      minifier's convention
    * the FIRST comment of a Bovnar document, always. `#!bovnar 1.1` is a
      version directive only as the very first comment (doc/12_bovnar.ebnf), so
      dropping an ordinary comment ahead of one would PROMOTE it and change the
      spec version the document targets -- and the leading comment is also where
      the BOM rules apply
    * `<!-- bovnar:... -->` markers in Markdown and HTML: check_doc_layout.py,
      check_doc_refs.py and gen_html_docs.py read bovnar:retired-path out of
      them, so they are configuration that happens to look like a comment
    * a `#!` shebang on line 1 and a PEP 263 coding cookie on line 1 or 2:
      Python reads both
    * IE conditional comments, which are markup in a comment's clothing

Lint, typing and coverage pragmas (`# noqa`, `# type:`, `# pragma: no cover`)
are NOT kept. They address tools that never run against this bundle, and the
verification above proves they do not change the program.

C keeps one behaviour of strip_comments.sh that goes beyond removing comments:
it also normalises line endings to LF, strips trailing horizontal whitespace and
drops every blank line. That is not tidiness for its own sake -- the amalgamation
and every bundle built so far contain exactly that output, and changing it would
rewrite them all. The other languages only remove what a comment leaves behind.

Left alone deliberately: `*.min.js`, which has no comments beyond the licence
banner that must be retained anyway, and where a tokenizer would be taking a
real risk with regex-versus-division in minified code for no gain. JSON has no
comments to strip.

Usage
-----
    strip_lang_comments.py FILE                 strip to stdout
    strip_lang_comments.py -i FILE [FILE ...]   strip in place
    strip_lang_comments.py -c FILE [FILE ...]   verify only, write nothing
    strip_lang_comments.py -v ...               name each file on stderr

Exit status
    0   every file handled (an unknown or skipped type is a no-op)
    1   usage error
    3   one or more files failed; the rest were still processed
"""

import ast
import io
import os
import re
import shutil
import subprocess
import sys
import tempfile
import tokenize
import xml.etree.ElementTree as ET
from html.parser import HTMLParser

PROG = os.path.basename(sys.argv[0])

# How much of the line a cut may take with it.
DROP = "drop"     # the comment, the whitespace it leaves at end of line, and
                  # the whole line if the comment was all that was on it
KEEP = "keep"     # trim what the comment leaves behind but never delete the
                  # line -- for Bovnar, where a deleted line moves every
                  # diagnostic after it, and Markdown, where a blank line is
                  # a block separator and trailing spaces are a <br>
EXACT = "exact"   # the comment span and not one byte more -- inside <pre>, and
                  # in XML, where the whitespace around it is text
NL = "nl"         # replace the span with one newline: a multi-line comment is
                  # a line terminator for JavaScript's semicolon insertion, so
                  # closing it up could change what the program means

# Comments that carry a licence are kept whatever else goes.
LICENCE = re.compile(r"SPDX-License-Identifier|Copyright|\(c\)\s*\d{4}"
                     r"|@license|@preserve|\bLicen[cs]e:", re.I)
# Comments that are configuration read by the doc tooling.
DIRECTIVE = re.compile(r"bovnar:[a-z-]+", re.I)


class StripError(Exception):
    """A file could not be stripped, or could not be proven unchanged."""


class Unverifiable(Exception):
    """The original could not be checked, so it is left untouched."""


# --------------------------------------------------------------------------
# shared


def apply_cuts(src, cuts):
    """Remove (start, end, mode) spans from src, working backwards."""
    nl = b"\n" if isinstance(src, bytes) else "\n"
    text = src
    for start, end, mode in sorted(cuts, key=lambda c: c[0], reverse=True):
        line_start = text.rfind(nl, 0, start) + 1
        line_end = text.find(nl, end)
        if line_end == -1:
            line_end = len(text)
        before, after = text[line_start:start], text[end:line_end]
        if mode == EXACT:
            text = text[:start] + text[end:]
            continue
        if mode == NL:
            if not before.strip() and not after.strip():
                # Alone on its lines: the newline that ended the line BEFORE it
                # is still there, so ASI is safe without adding one, and the
                # comment leaves no blank line behind.
                tail = line_end + 1 if line_end < len(text) else line_end
                text = text[:line_start] + text[tail:]
            else:
                # Code on one side or both: the line break the comment provided
                # has to be replaced, or `a = b /*\n*/ c = d` becomes one line
                # and means something else.
                text = text[:line_start] + before.rstrip() + nl + after \
                    + text[line_end:]
            continue
        if mode == DROP and not before.strip() and not after.strip():
            # The comment was the whole line: take the newline with it, rather
            # than leaving a blank line behind for every comment removed.
            tail = line_end + 1 if line_end < len(text) else line_end
            text = text[:line_start] + text[tail:]
            continue
        line = before + after
        if not after.strip():
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
# C
#
# Ported from strip_comments.sh, which this replaces, and deliberately
# byte-for-byte compatible with it: the amalgamation and every bundle built so
# far contain its output, so a "cleanup" here would silently rewrite them all.
# ISO C99 5.1.1.2 phase 3, as a state machine over the source:
#
#   * a block comment becomes ONE SPACE, because it separates tokens: a/**/b is
#     two tokens, not one
#   * a line comment's terminating newline is kept -- it is not part of the
#     comment -- and newlines inside a block comment are kept too, so that the
#     cleanup pass below can count those lines as empty and drop them
#   * a string or character literal is never touched, which is the whole reason
#     this is a state machine and not a regex
#
# Not handled, exactly as strip_comments.sh did not handle it: phase 2 line
# splicing, where a backslash-newline at the end of a // comment continues that
# comment onto the next line. No file in the tree does that, and the token check
# below would catch it if one started.

C_LEADING_BLOCK = re.compile(r"\A/\*.*?\*/", re.S)
C_TRAILING_HWS = re.compile(r"[^\S\n]+$", re.M)

(C_NORMAL, C_SLASH, C_LINE, C_BLOCK, C_BLOCK_STAR,
 C_STR, C_STR_ESC, C_CHR, C_CHR_ESC) = range(9)


def c_uncomment(src):
    """src with every comment replaced by whitespace, literals untouched."""
    out = []
    state = C_NORMAL
    for c in src:
        if state == C_NORMAL:
            if c == "/":
                state = C_SLASH
            elif c == '"':
                out.append(c)
                state = C_STR
            elif c == "'":
                out.append(c)
                state = C_CHR
            else:
                out.append(c)
        elif state == C_SLASH:
            if c == "/":
                state = C_LINE
            elif c == "*":
                state = C_BLOCK
            else:
                out.append("/")
                out.append(c)
                state = C_NORMAL
        elif state == C_LINE:
            if c == "\n":
                out.append("\n")
                state = C_NORMAL
        elif state == C_BLOCK:
            if c == "*":
                state = C_BLOCK_STAR
            elif c == "\n":
                out.append("\n")
        elif state == C_BLOCK_STAR:
            if c == "/":
                out.append(" ")
                state = C_NORMAL
            elif c != "*":
                if c == "\n":
                    out.append("\n")
                state = C_BLOCK
        elif state == C_STR:
            out.append(c)
            if c == "\\":
                state = C_STR_ESC
            elif c == '"':
                state = C_NORMAL
        elif state == C_STR_ESC:
            out.append(c)
            state = C_STR
        elif state == C_CHR:
            out.append(c)
            if c == "\\":
                state = C_CHR_ESC
            elif c == "'":
                state = C_NORMAL
        elif state == C_CHR_ESC:
            out.append(c)
            state = C_CHR
    if state in (C_BLOCK, C_BLOCK_STAR):
        out.append(" ")
    if state == C_SLASH:
        out.append("/")
    return "".join(out)


def c_tokens(src):
    """The code of a translation unit, comments reduced to separators."""
    return c_uncomment(src).split()


def strip_c(src):
    src = src.replace("\r\n", "\n").replace("\r", "\n")
    # The licence block has to survive, and it IS a comment.  strip_comments.sh
    # could not do this itself, so strip.sh and merge.sh each carried their own
    # awk to cut the block out beforehand and paste it back after; doing it here
    # means one copy of the rule for all nine languages.
    header = ""
    m = C_LEADING_BLOCK.match(src)
    if m and "SPDX-License-Identifier" in m.group(0):
        header = m.group(0).rstrip("\n") + "\n"
    else:
        # Every licence header in the tree is a leading /* block, and
        # strip_comments.sh handled no other kind.  A // block carries the same
        # obligation, though, and this is now the only copy of the rule.
        lines = src.splitlines(keepends=True)
        rows = spdx_header_rows(lines, lambda l: l.lstrip().startswith("//"))
        if rows:
            header = "".join(lines[:max(rows)]).rstrip("\n") + "\n"

    body = c_uncomment(src)
    body = C_TRAILING_HWS.sub("", body)
    lines = [l for l in body.split("\n") if l]
    out = ("\n".join(lines) + "\n") if lines else ""
    out = header + out
    if c_tokens(src) != c_tokens(out):
        raise StripError("stripping changed the C token stream")
    return out


# --------------------------------------------------------------------------
# Python

CODING_COOKIE = re.compile(r"^[ \t\f]*#.*?coding[:=][ \t]*([-\w.]+)")


def strip_python(src):
    try:
        tokens = list(tokenize.generate_tokens(io.StringIO(src).readline))
    except (tokenize.TokenError, IndentationError, SyntaxError) as exc:
        raise Unverifiable("cannot tokenize as Python: %s" % exc)

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
        raise Unverifiable("does not parse as Python: %s" % exc)
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
    unquoted argument text), 'punct' for ( and ), or 'space'.  Per
    cmake-language(7) a # begins a comment unless it is escaped, inside a
    quoted argument or inside a bracket argument, and #[[ ]] is a bracket
    comment.
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
    try:
        spans = scan_cmake(src)
    except StripError as exc:
        raise Unverifiable("cannot scan as CMake: %s" % exc)
    lines = src.splitlines(keepends=True)
    keep_rows = spdx_header_rows(lines, lambda l: l.lstrip().startswith("#"))

    cuts = []
    for kind, start, end in spans:
        if kind != "comment":
            continue
        row = src.count("\n", 0, start) + 1
        if row in keep_rows or LICENCE.search(src[start:end]):
            continue
        cuts.append((start, end, DROP))
    out = apply_cuts(src, cuts)
    verify_cmake(src, out)
    return out


def verify_cmake(old, new):
    if cmake_tokens(old) != cmake_tokens(new):
        raise StripError("stripping changed the CMake token stream")


# --------------------------------------------------------------------------
# Bovnar
#
# doc/12_bovnar.ebnf:
#   comment      = "#" , {comment-char} , ("\x0D" | "\x0A" | eof)
#   string-literal = '"' , {string-char} , '"'   (backslash escapes)
#   octet-stream = 0x00 { 0x01 uint16le data } 0x00   (0x0000 = 65536 bytes)
# A NUL where a value is expected switches to binary chunk mode, and those
# chunks carry arbitrary bytes -- so the scanner has to follow the framing
# rather than look for a delimiter. Everything here is bytes, not str: a
# document with an octet stream is not text and need not be valid UTF-8.


def scan_bvnr(src):
    """Split a Bovnar document into (kind, start, end) spans.

    kind is 'comment', 'string', 'binary' (a whole octet stream) or 'data'.
    """
    spans = []
    i, n = 0, len(src)
    while i < n:
        c = src[i]
        if c == 0x23:                                   # '#'
            j = i
            while j < n and src[j] not in (0x0A, 0x0D):
                j += 1
            spans.append(("comment", i, j))
            i = j
        elif c == 0x22:                                 # '"'
            j = i + 1
            while j < n:
                if src[j] == 0x5C:                      # backslash
                    j += 2
                    continue
                if src[j] == 0x22:
                    j += 1
                    break
                j += 1
            else:
                raise StripError("unterminated string at offset %d" % i)
            spans.append(("string", i, j))
            i = j
        elif c == 0x00:                                 # octet stream
            j = i + 1
            while True:
                if j >= n:
                    raise StripError("EOF inside an octet stream at offset %d"
                                     % i)
                tag = src[j]
                if tag == 0x00:                         # end-of-stream marker
                    j += 1
                    break
                if tag != 0x01:
                    raise StripError(
                        "octet stream out of sync at offset %d (tag 0x%02x)"
                        % (j, tag))
                if j + 3 > n:
                    raise StripError("truncated chunk length at offset %d" % j)
                length = src[j + 1] | (src[j + 2] << 8)
                if length == 0:
                    length = 65536                      # 0x0000 encodes 65536
                j += 3 + length
                if j > n:
                    raise StripError("chunk runs past EOF at offset %d" % i)
            spans.append(("binary", i, j))
            i = j
        else:
            j = i
            while j < n and src[j] not in (0x23, 0x22, 0x00):
                j += 1
            spans.append(("data", i, j))
            i = j
    return spans


def bvnr_tokens(src):
    """Everything the parser looks at, and the octet streams on their own.

    Data runs are split on whitespace so that two runs left adjacent by a
    removed comment still compare equal token for token -- while a comment
    removal that ran two tokens together would not.
    """
    toks, binaries = [], []
    for kind, start, end in scan_bvnr(src):
        chunk = src[start:end]
        if kind == "comment":
            continue
        if kind == "data":
            toks.extend(chunk.split())
        else:
            toks.append(chunk)
            if kind == "binary":
                binaries.append(chunk)
    return toks, binaries


def strip_bvnr(src):
    try:
        spans = scan_bvnr(src)
    except StripError as exc:
        raise Unverifiable("cannot scan as Bovnar: %s" % exc)
    cuts, first = [], True
    for kind, start, end in spans:
        if kind != "comment":
            continue
        if first:
            # The version directive is the very first comment or nothing, and
            # removing an ordinary comment ahead of one would promote it.
            first = False
            continue
        if LICENCE.search(src[start:end].decode("utf-8", "replace")):
            continue
        # KEEP, not DROP: a deleted line renumbers every diagnostic below it,
        # and this tree's conformance corpus is the thing those diagnostics are
        # measured against.
        cuts.append((start, end, KEEP))
    out = apply_cuts(src, cuts)
    verify_bvnr(src, out)
    return out


def verify_bvnr(old, new):
    old_toks, old_bin = bvnr_tokens(old)
    try:
        new_toks, new_bin = bvnr_tokens(new)
    except StripError as exc:
        raise StripError("stripping produced an unscannable document: %s" % exc)
    if old_toks != new_toks:
        raise StripError("stripping changed the Bovnar token stream")
    if old_bin != new_bin:
        raise StripError("stripping changed an octet stream")
    if old.count(b"\n") != new.count(b"\n"):
        raise StripError("stripping changed the line count")


# --------------------------------------------------------------------------
# XML


def scan_xml_comments(src):
    """Comment spans in an XML document, skipping CDATA, PIs and tag text."""
    spans = []
    i, n = 0, len(src)
    while i < n:
        c = src[i]
        if c == "<":
            if src.startswith("<!--", i):
                end = src.find("-->", i)
                if end == -1:
                    raise StripError("unterminated comment at offset %d" % i)
                spans.append((i, end + 3))
                i = end + 3
                continue
            if src.startswith("<![CDATA[", i):
                end = src.find("]]>", i)
                if end == -1:
                    raise StripError("unterminated CDATA at offset %d" % i)
                i = end + 3
                continue
            if src.startswith("<?", i):
                end = src.find("?>", i)
                i = n if end == -1 else end + 2
                continue
            # A tag or a declaration: skip it, honouring quoted attribute
            # values, so a "<!--" inside one is not mistaken for a comment.
            j, quote = i + 1, None
            while j < n:
                if quote:
                    if src[j] == quote:
                        quote = None
                elif src[j] in "\"'":
                    quote = src[j]
                elif src[j] == ">":
                    j += 1
                    break
                j += 1
            i = j
        else:
            i += 1
    return spans


def strip_xml(src):
    try:
        before = ET.canonicalize(src)
    except (ET.ParseError, ValueError) as exc:
        raise Unverifiable("does not parse as XML: %s" % exc)
    try:
        comments = scan_xml_comments(src)
    except StripError as exc:
        raise Unverifiable("cannot scan as XML: %s" % exc)

    cuts = []
    for start, end in comments:
        body = src[start + 4:end - 3]
        if LICENCE.search(body) or DIRECTIVE.search(body):
            continue
        # EXACT: the whitespace around an XML comment is text, and xml2rfc
        # renders <artwork> verbatim.
        cuts.append((start, end, EXACT))
    out = apply_cuts(src, cuts)
    try:
        after = ET.canonicalize(out)
    except (ET.ParseError, ValueError) as exc:
        raise StripError("stripping produced invalid XML: %s" % exc)
    if before != after:
        raise StripError("stripping changed the XML canonical form")
    return out


# --------------------------------------------------------------------------
# JavaScript
#
# The hard part is that '/' is division or the start of a regular expression
# depending on what came before it, and a regex body can contain '//' or '/*'.
# The rule below is the usual one: a regex may start only where an expression
# may start.
#
# That rule is not decidable from the previous token alone, and this is where it
# gives up: after ')' or '}' it assumes division, so `if (x) /a\/\/b/.test(s)`
# -- a regex containing '//', in a position only a real parser can tell from a
# division -- would be misread, and the '//' inside it taken for a comment. That
# is why node --check runs over the RESULT: deleting the tail of a line like that
# leaves code node refuses, so the file fails loudly instead of shipping broken.
# No file in this tree does it, and the check would say so if one started.

JS_REGEX_OK_AFTER = {
    "return", "typeof", "instanceof", "in", "of", "new", "delete", "void",
    "throw", "case", "do", "else", "yield", "await",
}
JS_ID = re.compile(r"[A-Za-z_$][A-Za-z0-9_$]*")
JS_NUM = re.compile(r"(?:0[xXbBoO])?[0-9][0-9a-fA-F_.eExXpP+-]*n?")
JS_WS = "\t\n\v\f\r \u00a0\u2028\u2029\ufeff"   # ECMA-262 white space
                                              # and line terminators


def scan_js(src):
    """Split JavaScript into (kind, start, end) spans.

    kind is 'comment', 'space' or 'tok' (anything the parser looks at:
    identifiers, numbers, strings, template literals, regexes, punctuation).
    """
    spans = []
    i, n = 0, len(src)
    prev = None            # the last 'tok' text, for the regex/division call
    while i < n:
        c = src[i]
        if c in JS_WS:
            j = i
            while j < n and src[j] in JS_WS:
                j += 1
            spans.append(("space", i, j))
            i = j
        elif src.startswith("//", i):
            j = src.find("\n", i)
            j = n if j == -1 else j
            spans.append(("comment", i, j))
            i = j
        elif src.startswith("/*", i):
            j = src.find("*/", i + 2)
            if j == -1:
                raise StripError("unterminated block comment at offset %d" % i)
            spans.append(("comment", i, j + 2))
            i = j + 2
        elif c in "'\"":
            j = i + 1
            while j < n:
                if src[j] == "\\":
                    j += 2
                    continue
                if src[j] == c:
                    j += 1
                    break
                if src[j] == "\n":
                    raise StripError("unterminated string at offset %d" % i)
                j += 1
            else:
                raise StripError("unterminated string at offset %d" % i)
            spans.append(("tok", i, j))
            prev = src[i:j]
            i = j
        elif c == "`":
            # Inside ${...} the braces have to be counted, or `${ {a:1}.a }`
            # ends the substitution at the inner one and the rest of the
            # template gets scanned as code.  Nothing inside a substitution is
            # treated as a comment, which errs towards leaving one in place.
            j, subst, braces = i + 1, False, 0
            while j < n:
                if src[j] == "\\":
                    j += 2
                    continue
                if subst:
                    if src[j] == "{":
                        braces += 1
                    elif src[j] == "}":
                        if braces:
                            braces -= 1
                        else:
                            subst = False
                    j += 1
                    continue
                if src.startswith("${", j):
                    subst, braces = True, 0
                    j += 2
                    continue
                if src[j] == "`":
                    j += 1
                    break
                j += 1
            else:
                raise StripError("unterminated template at offset %d" % i)
            spans.append(("tok", i, j))
            prev = "`"
            i = j
        elif c == "/" and js_regex_allowed(prev):
            j, in_class = i + 1, False
            while j < n:
                if src[j] == "\\":
                    j += 2
                    continue
                if src[j] == "[":
                    in_class = True
                elif src[j] == "]":
                    in_class = False
                elif src[j] == "/" and not in_class:
                    j += 1
                    while j < n and src[j].isalpha():   # flags
                        j += 1
                    break
                elif src[j] == "\n":
                    raise StripError("unterminated regex at offset %d" % i)
                j += 1
            else:
                raise StripError("unterminated regex at offset %d" % i)
            spans.append(("tok", i, j))
            prev = "/re/"
            i = j
        else:
            m = JS_ID.match(src, i) or JS_NUM.match(src, i)
            j = m.end() if m else i + 1
            spans.append(("tok", i, j))
            prev = src[i:j]
            i = j
    return spans


def js_regex_allowed(prev):
    """Could a '/' here start a regex rather than be a division sign?"""
    if prev is None:
        return True
    if prev in JS_REGEX_OK_AFTER:
        return True
    if JS_ID.fullmatch(prev) or JS_NUM.fullmatch(prev):
        return False                        # an identifier or number: division
    if prev in (")", "]", "}", "`", "/re/") or prev[:1] in "'\"":
        return False                        # end of an expression: division
    return True                             # an operator or punctuation


def js_tokens(src):
    return [src[s:e] for kind, s, e in scan_js(src) if kind == "tok"]


def strip_js(src):
    try:
        spans = scan_js(src)
    except StripError as exc:
        raise Unverifiable("cannot scan as JavaScript: %s" % exc)
    dialect = js_dialect(src)               # raises Unverifiable if node says
                                            # the original is already broken
    cuts = []
    for kind, start, end in spans:
        if kind != "comment":
            continue
        body = src[start:end]
        if body.startswith("/*!") or LICENCE.search(body):
            continue
        # A multi-line comment is a line terminator for automatic semicolon
        # insertion, so it collapses to a newline rather than to nothing.
        cuts.append((start, end, NL if "\n" in body else DROP))
    out = apply_cuts(src, cuts)
    if js_tokens(src) != js_tokens(out):
        raise StripError("stripping changed the JavaScript token stream")
    if dialect and not node_check(out, dialect):
        raise StripError("stripping produced JavaScript node rejects")
    return out


def js_dialect(src):
    """'js', 'mjs' or None: how node parses this file, if node is here."""
    if not shutil.which("node"):
        return None
    for suffix in ("js", "mjs"):
        if node_check(src, suffix):
            return suffix
    raise Unverifiable("node rejects the file as both script and module")


def node_check(src, suffix):
    with tempfile.NamedTemporaryFile("w", suffix="." + suffix,
                                     encoding="utf-8", delete=False) as fh:
        tmp = fh.name                   # named before the write, so a failing
        fh.write(src)                   # write cannot leak the file
    try:
        return subprocess.run(["node", "--check", tmp],
                              stdout=subprocess.DEVNULL,
                              stderr=subprocess.DEVNULL).returncode == 0
    finally:
        os.unlink(tmp)


# --------------------------------------------------------------------------
# CSS


def scan_css(src):
    """Split CSS into (kind, start, end) spans: 'comment', 'space' or 'tok'."""
    spans = []
    i, n = 0, len(src)
    while i < n:
        c = src[i]
        if c in " \t\r\n\f":
            j = i
            while j < n and src[j] in " \t\r\n\f":
                j += 1
            spans.append(("space", i, j))
            i = j
        elif src.startswith("/*", i):
            j = src.find("*/", i + 2)
            if j == -1:
                raise StripError("unterminated comment at offset %d" % i)
            spans.append(("comment", i, j + 2))
            i = j + 2
        elif c in "'\"":
            j = i + 1
            while j < n:
                if src[j] == "\\":
                    j += 2
                    continue
                if src[j] == c:
                    j += 1
                    break
                j += 1
            else:
                raise StripError("unterminated string at offset %d" % i)
            spans.append(("tok", i, j))
            i = j
        else:
            j = i
            while j < n and src[j] not in " \t\r\n\f'\"" \
                    and not src.startswith("/*", j):
                j += 1
            spans.append(("tok", i, max(j, i + 1)))
            i = max(j, i + 1)
    return spans


def css_tokens(src):
    return [src[s:e] for kind, s, e in scan_css(src) if kind == "tok"]


def strip_css(src):
    try:
        spans = scan_css(src)
    except StripError as exc:
        raise Unverifiable("cannot scan as CSS: %s" % exc)
    cuts = []
    for kind, start, end in spans:
        if kind != "comment":
            continue
        body = src[start:end]
        if body.startswith("/*!") or LICENCE.search(body):
            continue
        cuts.append((start, end, NL if "\n" in body else DROP))
    out = apply_cuts(src, cuts)
    if css_tokens(src) != css_tokens(out):
        raise StripError("stripping changed the CSS token stream")
    return out


# --------------------------------------------------------------------------
# Markdown

FENCE = re.compile(r"^ {0,3}(`{3,}|~{3,})")


def md_code_lines(src):
    """The 1-based rows of every fenced or indented code line.

    A <!-- --> inside a fence is example text, not a comment; doc/ shows HTML
    and Bovnar in fences throughout.  A fence closes only on the same character
    repeated at least as many times as it was opened (CommonMark), so a ``` line
    inside a ~~~~ block does not end it.
    """
    rows, fence, prev_blank, indented = set(), None, True, False
    for n, line in enumerate(src.splitlines(), 1):
        stripped = line.strip()
        if fence:
            rows.add(n)
            if stripped and set(stripped) == {fence[0]} \
                    and len(stripped) >= len(fence):
                fence = None
            continue
        m = FENCE.match(line)
        if m:
            fence = m.group(1)
            rows.add(n)
            prev_blank = False
            continue
        is_indented = line.startswith("    ") or line.startswith("\t")
        if indented:
            # An indented block runs on through every indented line and any
            # blank line between them, not just the first line -- marking only
            # the first left a <!-- --> two lines into a block strippable.
            if is_indented and stripped:
                rows.add(n)
                continue
            if not stripped:
                continue
            indented = False
        elif prev_blank and is_indented and stripped:
            indented = True
            rows.add(n)
            continue
        prev_blank = not stripped
    return rows


def md_structure(src):
    """Fences, headings and the exact text of every code line."""
    code = md_code_lines(src)
    lines = src.splitlines()
    return (
        [l for n, l in enumerate(lines, 1) if n in code],
        [l for n, l in enumerate(lines, 1)
         if n not in code and re.match(r"^ {0,3}#{1,6} ", l)],
        len(lines),
    )


def strip_markdown(src):
    code = md_code_lines(src)
    cuts = []
    i = 0
    while True:
        start = src.find("<!--", i)
        if start == -1:
            break
        end = src.find("-->", start)
        if end == -1:
            break
        end += 3
        i = end
        row = src.count("\n", 0, start) + 1
        if row in code:
            continue                        # inside a code block: example text
        body = src[start + 4:end - 3]
        if LICENCE.search(body) or DIRECTIVE.search(body):
            continue
        line_start = src.rfind("\n", 0, start) + 1
        line_end = src.find("\n", start)
        if "`" in src[line_start:line_end if line_end != -1 else len(src)]:
            continue                        # possibly an inline code span
        # KEEP: an HTML block is a block separator, so the line stays (blank);
        # and trailing whitespace has to go, because two spaces are a <br>.
        cuts.append((start, end, KEEP))
    out = apply_cuts(src, cuts)
    if md_structure(src) != md_structure(out):
        raise StripError("stripping changed the Markdown block structure")
    return out


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
        raise Unverifiable("does not parse as HTML: %s" % exc)

    cuts = []
    for start, end, in_raw, data in loc.comments:
        if CONDITIONAL.match(data):
            continue                          # downlevel-revealed markup
        if LICENCE.search(data) or DIRECTIVE.search(data):
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

# suffix (or exact name) -> (stripper, binary?)
BY_SUFFIX = [
    (".c", strip_c, False),
    (".h", strip_c, False),
    (".py", strip_python, False),
    (".cmake", strip_cmake, False),
    (".cmake.in", strip_cmake, False),
    (".bvnr", strip_bvnr, True),
    (".xml", strip_xml, False),
    (".min.js", None, False),           # skipped: see the module docstring
    (".js", strip_js, False),
    (".mjs", strip_js, False),           # .mjs matters: wasm/index.mjs is the
    (".cjs", strip_js, False),           # source web/bovnar_wasm.js must match,
                                         # and a ctest gate compares the two
    (".css", strip_css, False),
    (".md", strip_markdown, False),
    (".html", strip_html, False),
    (".htm", strip_html, False),
]


def stripper_for(path):
    """(function, binary) for a path, (None, False) if nothing handles it."""
    lower = os.path.basename(path).lower()
    if re.match(r"^cmakelists.*\.txt$", lower):
        return strip_cmake, False
    for suffix, func, binary in BY_SUFFIX:
        if lower.endswith(suffix):
            return func, binary
    return None, False


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

    failed = skipped = 0
    for path in files:
        strip, binary = stripper_for(path)
        if strip is None:
            if verbose:
                sys.stderr.write("%s: not handled\n" % path)
            continue
        if verbose:
            sys.stderr.write("%s\n" % path)
        try:
            if binary:
                with open(path, "rb") as fh:
                    src = fh.read()
            else:
                # surrogateescape, as strip_comments.sh did: a stray non-UTF-8
                # byte in a comment must not stop the run, and writing back the
                # same way restores it byte for byte.
                with open(path, "r", encoding="utf-8",
                          errors="surrogateescape") as fh:
                    src = fh.read()
        except (OSError, UnicodeDecodeError) as exc:
            sys.stderr.write("%s: error: %s: %s\n" % (PROG, path, exc))
            failed += 1
            continue
        try:
            out = strip(src)
        except Unverifiable as exc:
            sys.stderr.write("%s: left alone, cannot verify: %s: %s\n"
                             % (PROG, path, exc))
            skipped += 1
            continue
        except StripError as exc:
            sys.stderr.write("%s: error: %s: %s\n" % (PROG, path, exc))
            failed += 1
            continue
        if check_only:
            continue
        if in_place:
            if out != src:
                tmp = path + ".strip.tmp"
                if binary:
                    with open(tmp, "wb") as fh:
                        fh.write(out)
                else:
                    with open(tmp, "w", encoding="utf-8",
                              errors="surrogateescape") as fh:
                        fh.write(out)
                os.replace(tmp, path)
        else:
            if binary:
                sys.stdout.buffer.write(out)
            else:
                sys.stdout.write(out)
    if skipped:
        sys.stderr.write("%s: %d file(s) left alone as unverifiable\n"
                         % (PROG, skipped))
    return 3 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
