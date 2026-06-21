#!/usr/bin/env python3
"""
bvnr_data.py — a tiny standalone reader for the codegen data documents.

The *.bvnr source-of-truth files use only a fixed, simple subset of the bovnar
grammar: a document is a sequence of `.key = value;` assignments whose values are
strings, numbers, booleans, bare symbols, structs `{ … }`, and arrays `[ … ]`.
Inline `<type>` annotations are skipped (codegen only needs the value).

Parsing them here — instead of through the compiled libbvnr — means the
generators have NO dependency on a built bovnar, so a clean checkout regenerates
the tables with plain Python and there is no bootstrap cycle.

`load(text)` returns the same nested dict/list/scalar structure that
`bovnar.loads` returns for these documents (verified by comparison), so it is a
drop-in replacement for the generators' purposes.
"""
import re

_NUMBER = re.compile(r"[+-]?(?:\d+\.\d*|\.\d+|\d+)(?:[eE][+-]?\d+)?$")
_IDENT = re.compile(r"[A-Za-z_][A-Za-z0-9_\-]*")
# token terminators for a bare scalar (number / symbol)
_SCALAR_END = set(" \t\r\n;,]}#")


class _Parser:
    def __init__(self, text):
        self.s = text
        self.i = 0
        self.n = len(text)

    def _err(self, msg):
        line = self.s.count("\n", 0, self.i) + 1
        raise ValueError("bvnr parse error at line %d: %s" % (line, msg))

    def _ws(self):
        """Skip whitespace and `#` comments (this also skips the `#!bovnar …`
        version directive, which begins with `#`)."""
        while self.i < self.n:
            c = self.s[self.i]
            if c in " \t\r\n":
                self.i += 1
            elif c == "#":
                while self.i < self.n and self.s[self.i] != "\n":
                    self.i += 1
            else:
                break

    def _peek(self):
        return self.s[self.i] if self.i < self.n else ""

    def _skip_annotation(self):
        """Skip a leading `<…>` type annotation, if present."""
        self._ws()
        if self._peek() == "<":
            while self.i < self.n and self.s[self.i] != ">":
                self.i += 1
            if self.i < self.n:
                self.i += 1  # consume '>'

    def document(self):
        d = {}
        while True:
            self._ws()
            if self.i >= self.n:
                return d
            if self._peek() != ".":
                self._err("expected `.key` at top level, got %r" % self._peek())
            k, v = self._assignment()
            d[k] = v

    def _assignment(self):
        self.i += 1  # consume '.'
        m = _IDENT.match(self.s, self.i)
        if not m:
            self._err("expected identifier after `.`")
        key = m.group(0)
        self.i = m.end()
        self._ws()
        if self._peek() != "=":
            self._err("expected `=` after .%s" % key)
        self.i += 1  # consume '='
        val = self._value()
        self._ws()
        if self._peek() == ";":
            self.i += 1
        return key, val

    def _value(self):
        self._skip_annotation()
        self._ws()
        c = self._peek()
        if c == '"':
            return self._string()
        if c == "{":
            return self._struct()
        if c == "[":
            return self._array()
        if c == "":
            self._err("unexpected end of input where a value was expected")
        return self._scalar()

    def _string(self):
        self.i += 1  # consume opening '"'
        out = []
        while self.i < self.n:
            c = self.s[self.i]
            self.i += 1
            if c == '"':
                return "".join(out)
            if c == "\\":
                e = self.s[self.i] if self.i < self.n else ""
                self.i += 1
                out.append({'"': '"', "\\": "\\", "n": "\n",
                            "t": "\t", "r": "\r"}.get(e, e))
            else:
                out.append(c)
        self._err("unterminated string")

    def _struct(self):
        self.i += 1  # consume '{'
        d = {}
        while True:
            self._ws()
            c = self._peek()
            if c == "}":
                self.i += 1
                return d
            if c != ".":
                self._err("expected `.key` or `}` in struct, got %r" % c)
            k, v = self._assignment()
            d[k] = v

    def _array(self):
        self.i += 1  # consume '['
        lst = []
        while True:
            self._ws()
            if self._peek() == "]":
                self.i += 1
                return lst
            lst.append(self._value())
            self._ws()
            if self._peek() == ",":
                self.i += 1

    def _scalar(self):
        start = self.i
        while self.i < self.n and self.s[self.i] not in _SCALAR_END:
            self.i += 1
        tok = self.s[start:self.i]
        if tok == "true":
            return True
        if tok == "false":
            return False
        if _NUMBER.match(tok):
            if any(ch in tok for ch in ".eE"):
                return float(tok)
            return int(tok)
        return tok  # bare symbol (e.g. `default`, `info`, `kilo`)


def load(text):
    """Parse a bvnr data document (str) into a nested dict/list/scalar value."""
    if isinstance(text, bytes):
        text = text.decode("utf-8")
    return _Parser(text).document()
