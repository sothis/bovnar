# Bovnar — Version 2 Grammar Proposals (Design Note)

> **Spec version:** 2.0 (proposed; every item here is a break the spec reserves for a major revision)
> **Status:** **Proposal.** Nothing here is built, scheduled, or decided. It is a list of the places
> the grammar says one thing twice, or says it in only one of the two places it should.
> **Scope:** Breaking changes to the grammar and lexical structure. Not the unit registry, not the
> C API, not the profiles — those grow additively inside 1.x and need no major revision.

## Table of Contents

1. [What this is](#1-what-this-is)
2. [What stays](#2-what-stays)
3. [One conformance tier](#3-one-conformance-tier)
4. [Remove the `/` dimension row](#4-remove-the--dimension-row)
5. [Quoted keys](#5-quoted-keys)
6. [The inline unit becomes sugar for the annotation, and nothing else](#6-the-inline-unit-becomes-sugar-for-the-annotation-and-nothing-else)
7. [Named type parameters](#7-named-type-parameters)
8. [Close the bare-word namespace](#8-close-the-bare-word-namespace)
9. [Null is written `null`](#9-null-is-written-null)
10. [The version directive stops being a comment](#10-the-version-directive-stops-being-a-comment)
11. [A number in any base is a number](#11-a-number-in-any-base-is-a-number)
12. [Three changes in the unit sub-grammar](#12-three-changes-in-the-unit-sub-grammar)
13. [Octet streams: declared, bounded, checked](#13-octet-streams-declared-bounded-checked)
14. [Priority, and the test a breaking change has to pass](#14-priority-and-the-test-a-breaking-change-has-to-pass)

- [See also](#see-also)

---

## 1. What this is

The spec's own stability rule (spec §17) is that anything which could make a valid 1.x document
invalid, or change how it decodes, waits for a 2.0. That rule has been honoured — the mandatory `$`
sigil, array homogeneity and `float_fix` range validation were all landed *before* 1.0 precisely
because they could not land after. The cost of honouring it is that the warts which remain are
permanent until there is a 2.0, and a 2.0 is worth doing only if the list is complete when it opens.

This note is that list. Each section states what the grammar does today, what it costs, what the
replacement would be, and what it breaks. Every "today" claim was taken from the reference
implementation rather than from the prose; where the two disagree the disagreement is the finding.

Three of the items are not preferences. **One conformance tier** (section 3) and **the inline unit**
(section 6) are correctness defects that happen to be spelled as design, and **the bare-word
namespace** (section 8) is the only item on the list that becomes unfixable once 2.0 ships. If the
list has to be cut, it is cut down to those.

---

## 2. What stays

Nothing below proposes a different-looking language. These are load-bearing and would survive
unchanged:

- the leading `.` key sigil — it is what lets the lexer enter the identifier machine from any
  context, including inside an array of structs, with no lookahead;
- the `;` terminator on every assignment, and with it the resync rule (spec §13.2): "skip to the
  next `;` at this depth" is a recovery story most text formats cannot offer at all;
- `<...>` in prefix position, and the whole-array annotation that inherits into elements;
- the unit living **in the value** rather than in a schema beside it;
- the streaming-first bias — every proposal below is checked against "can a SAX reader still decide
  this from a bounded window", and any that failed that test is not in this note.

---

## 3. One conformance tier

**Today.** Element homogeneity, sibling rectangularity, struct shape and duplicate keys are enforced
by `bvn_dom_parse` and **not** by the streaming reader. The EBNF says so in its own words under
constraint (g) of doc/12_bovnar.ebnf: "only a DOM parse rejects ragged siblings, mixed kinds, and
duplicate keys; the streaming reader accepts them."

**Why it is a problem.** "Is this a valid Bovnar document" has no answer that does not name an API.
A producer can emit a file that `bvnr_reader` blesses and `loads()` refuses, and both are conforming.
For a format whose entire pitch is *the parser validates it*, that is the deepest crack in the
design — deeper than any single rule the two tiers disagree about.

**Proposed.** Push spec §7.4 down into the validator wholesale. The mechanism already exists: the
`/`-row width check is streaming, is scoped per bracket-pair by the context save/restore, and reports
at the earliest offending byte. Kind, unit and sibling shape are the same shape of check one level
deeper — the first non-null element establishes the contract and every later element is compared
against it. Nothing needs the whole array in hand.

Duplicate keys are the one honest exception: detecting them in a stream is unbounded memory in the
width of a struct. So they become an explicit reader **policy** — `reject_duplicate_keys`, defaulting
on — rather than a rule that silently exists in one tier and not the other.

**Cost.** A streaming consumer that today accepts `[1, "two"]` starts failing. That is the point, but
it is a real break for anyone using the SAX API as a permissive front end, and it needs a flag to opt
back out.

---

## 4. Remove the `/` dimension row

**Today.** `[1,2,3]/[4,5,6]` and `[[1,2,3],[4,5,6]]` both exist, and the `/` form is the weaker one:

- it is flat storage plus a row count (`bvn_dom_array_dims`), so it is two-dimensional and cannot
  express a 3-D block at all;
- it is **lossy to JSON**. The repository's own fixture `tests/dim_array.bvnr` records it: `.rows` and
  `.nested` render byte-identical JSON, and only the nested form reads back;
- it forces `&.matrix[0][1]` to address flat-with-rows differently from genuine nesting;
- it carries its own error code and its own streaming width check for a structure the language can
  already express.

**Proposed.** Nested arrays become the only structural form. The case `/` was genuinely good at — a
large rectangular numeric block a consumer wants as one flat buffer — moves into the annotation as a
declared shape:

```text
.field = <float:64, unit=m/s, shape=2x3x4> [ … 24 elements … ];
```

That streams (the expected count is known at the opening `[`), generalises past two dimensions, keeps
the flat buffer, and is a *declaration a reader can check* rather than a structure it must infer. It
also survives the JSON round-trip as metadata instead of vanishing.

**Cost.** Every document using `/` needs rewriting, and the DOM's flat-plus-`dims` representation
becomes a shape-annotation detail rather than a syntax. The transcoder is mechanical (section 14).

---

## 5. Quoted keys

**Today.** A key is an identifier. `a b`, `x.y`, `9lives` and `""` have no representation, and the
JSON converter says so by hand rather than by failing to parse:

```console
$ printf '{"a b": 1}' > k.json && bovnar convert k.json
convert: JSON key 'a b' is not a valid bovnar identifier (must start with a letter
or '_' and contain only letters, digits, '_', '+', '-')
```

**Why it is a problem.** "Some JSON objects have no Bovnar representation" is a larger hole in the
interoperability story than any unit question, and it is the kind of hole a producer meets on day one
with data it does not control — column names out of a CSV, tags out of an MQTT topic, keys out of an
existing config.

**Proposed.** Keep the `.` sigil, reuse the string production behind it:

```text
."sensor 4 (aft)" = <float:64, unit=degC> 21.5;
.deep             = &."sensor 4 (aft)".value;
```

There is no ambiguity to resolve: after `.`, a `"` means quoted key and anything else means
identifier, so the lexer gains one branch and no lookahead. Reference paths take the same form, which
is where most of the implementation work actually lands.

**Cost.** The canonical writer needs a rule for when to quote (quote only when the identifier form is
unavailable), or byte-identical round-tripping breaks. Reference-path resolution becomes a real
parse rather than a split on `.`.

---

## 6. The inline unit becomes sugar for the annotation, and nothing else

**Today.** The inline suffix is a separate mechanism with its own reachability rules, and it disagrees
with the annotation in three directions:

<!-- bovnar-example: rejected -->
```bovnar
.ok    = 1 m;                   # fine
.array = [1 m, 2 m];            # error_unexpected_input_byte
```

<!-- bovnar-example: rejected -->
```bovnar
.sentinel = inf m/s;            # error_unexpected_input_byte — no unit on a special number
```

<!-- bovnar-example: rejected -->
```bovnar
.long = <utf8:,m> "hello";      # error_illegal_value_type — utf8 takes no unit
```

and yet:

```bovnar
.short = <utf8> "hello" m;      # accepted, and pretty-prints back unchanged
```

Both of the last two say *this text is measured in metres*. One is refused because `utf8` is a
parameterless family; the other is accepted because the inline path never consults the family at all.
The format currently admits a string measured in metres, and its own serialiser preserves it.

**Why it is a problem.** The array exclusion is a usability tax — a matrix must hoist to a
whole-array annotation or repeat `<float:64, unit=m>` per element. The special-number exclusion hits
exactly the value where a unit matters most: a sentinel reading in a telemetry stream. The `utf8`
hole is a validation bypass, and a bypass in the one check the format exists to perform.

**Proposed.** Define the inline suffix as *the unit parameter, written elsewhere*: the same
production, the same family validation, permitted in every position a value is permitted — array
elements and special numbers included. One rule, no exception table, and `error_illegal_value_type`
fires from one place.

**Cost.** `[1 m, 2 m]` becomes legal, which is additive; `<utf8> "x" m` becomes an error, which is
not. The lexer's `in_array_element` guard and the `number_outro` / `string_outro` split go away.

---

## 7. Named type parameters

**Today.** Parameters are identified by *shape*, not by name or position: digits are a width, `_N` a
base, `qN` a Q-format, and **everything left over is a unit**. Order is therefore free —
`<float:_10,64>` is accepted — and the consequences are structural:

- `no_unit` is a magic word squatting inside the unit namespace, so a unit may never be spelled that;
- `datetime`'s epoch rides in the unit slot, which doc/13 states plainly: the epoch "shares the unit
  parameter slot because it occupies the same position in the grammar". That is a slot collision
  written up as a design;
- `float_fix` stores its Q in the `base` field of `value_type_spec_t` and forbids a real base
  parameter, because there is one field and two meanings;
- the parameter space is closed. A future class has to be distinguishable from an arbitrary unit
  token by its first byte, and the unit token may begin with almost anything.

**Proposed.** Name them, keeping the width shorthand because a bare integer is unambiguous:

```text
<float:64, unit=m/s>
<float_fix:32, q=8>
<datetime:64, epoch=tai>
<uint:64, base=16>
```

Absence of `unit=` means dimensionless and `no_unit` retires. Each family declares which names it
accepts, which turns a per-family table of shape rules into one lookup, and a new class in 2.1 is
additive with no ambiguity to reason about.

**Cost.** Verbosity, in the annotation a reader meets most often. Worth measuring against the
tutorial's examples before committing; the width shorthand is what keeps `<float:64>` intact, which
is the overwhelmingly common case.

---

## 8. Close the bare-word namespace

**Today.** `null`, `true`, `false`, `on`, `off`, `nan`, `inf` and `ninf` are lexed as ordinary symbol
tokens and reclassified by the validator. Symbols are otherwise an **open namespace owned by the
application** — `.day = Monday;`, `.status = ok;` — and a word that merely starts with a keyword
(`ontology`, `infinity`) stays a symbol.

**Why it is a problem.** The two namespaces are the same namespace. Every keyword a future revision
wants to add silently rewrites the meaning of documents that used that word as an enum, with no
version directive able to save them, because the document was valid under the older grammar and stays
valid under the newer one — it just means something else. This is the one item on the list that
cannot be fixed after 2.0 opens: by then the set is either closed or it never will be.

**Proposed.** Sigil the symbols and leave the keywords bare:

```text
.status = :ok;
.day    = :Monday;
.enabled = true;
```

A leading `:` in value position is unambiguous (the `:` inside an annotation is a different context
entirely), the keyword/symbol lookahead leaves the lexer, and the keyword set becomes extensible
forever.

**Alternative, if the sigil is unacceptable:** declare the keyword set **closed** in spec §17, in the
same normative voice as the rest of the stability promise. That costs nothing today and forecloses
every future keyword, which is a price worth stating out loud rather than discovering later.

**Cost.** Every symbol in every document gains a character. The `bvn_dom_get_symbol` contract is
unchanged; only the spelling moves.

---

## 9. Null is written `null`

**Today.** Null has an empty production. `.a = ;` is null, `[,1,2,]` is four elements of which two
are null, and `[1,,3]` is a sparse array with a hole (spec §7.2).

**Why it is a problem.** It is the JavaScript `[1,,2]` trap. A stray or duplicated comma — the single
most common editing accident in a bracketed list — is a silent change in element count rather than a
parse error, and element count is exactly what the `/`-row and rectangularity rules are built to
police. The format already concedes the short form is not the real one: the canonical writer expands
every hole back to an explicit `null` on output.

**Proposed.** `null` is written `null`. A **single trailing comma** is permitted and ignored, as in
every format written since JSON's was regretted. Every other empty element position is
`error_unexpected_input_byte`. The empty right-hand side `.a = ;` goes with it — `.a = null;` is four
characters longer and unambiguous.

**Cost.** Documents relying on holes need rewriting; the transcoder handles it exactly (holes become
`null`), because that is already what the serialiser does.

---

## 10. The version directive stops being a comment

**Today.** `#!bovnar M.N` is lexically a comment, recognised only as the very first one. That was the
right call for 1.1 — it is precisely what let a 1.0 reader ignore it — and the bill comes due at 2.0.

**Why it is a problem.** The directive *gates syntax*: `datetime`, `\x` and `\u{…}` escapes, reference
indices and profile units are each legal only in a document that declares enough. So the same bytes
parse differently depending on line one, and the grammar is context-free only within a fixed version.
Worse is the consequence the spec already names in §18.3: because an old reader cannot see the
directive at all, **a producer has no way to make an old reader refuse a document it cannot
understand**, and the default is lenient. `strict_version` is a consumer-side patch for a
producer-side problem.

**Proposed.** A real production, required, and the first non-blank thing in the document:

```text
%bovnar 2.0;
.x = 42;
```

A 1.x reader fails on it immediately, which is the correct outcome and the one thing the comment form
cannot deliver. Unknown *future* directives get their handling defined once, here, so the next
extension does not need another comment-shaped hack.

**Cost.** Every 2.0 document carries a mandatory line, and the "a bare file is spec 1.0" convenience
ends. Given the directive already decides what the grammar *is*, that convenience was never free.

---

## 11. A number in any base is a number

**Today.** A non-decimal integer is written as a **quoted string**: `<uint:64,_16> "FF"` is accepted
and the bare `FF` form is `error_type_value_mismatch`. The type annotation says integer; the token is
a string.

**Why it is a problem.** It costs three things at once. Five families must accept "a number **or** a
string" as their value, which is the loosest type/value rule in the format. It is what makes a
string-carrying-a-unit reachable at all, and so is upstream of the `utf8` hole in section 6. And it is
an attack surface the spec documents in §18.1: a 32768-bit base-2 literal is a 32768-character string,
comfortably inside the default `max_string_length`, and base conversion is superlinear.

**Proposed.** Give the base a lexical form — `0x`, `0b`, `0o` for the common cases, and a general
radix form for the exotic ones the registry supports:

```text
.mask  = <uint:64> 0xDEADBEEF;
.flags = <uint:32> 0b1011;
.tag   = <uint:64> 36#zz;
```

A number is then always a number token, `utf8` never carries a unit, and the base parameter becomes
what it should always have been: a *rendering* hint for the writer, not a precondition for the reader.

**Cost.** The lexer gains a radix path, and every base-N document is rewritten. The base parameter
stays, so a document can still say "print this in base 36" independently of how it was written.

---

## 12. Three changes in the unit sub-grammar

**Group exponents.** `<float:64,(m/s)^2>` is `error_unit_illegal` today, and doc/12_bovnar.ebnf says
"a group is not (yet) followed by its own exponent". It should be: variance, power spectral density
and every squared-rate quantity is written that way everywhere else, and the parser already parses
the group.

**Then require parentheses for repeated division.** `a/b/c` means `a·b⁻¹·c⁻¹` today, under a rule the
EBNF states as "the first `/` switches every subsequent factor into the denominator". The rule is
correct, matches UCUM, and is unguessable from the notation. With group exponents available, restrict
each level to **at most one `/`** and let parentheses carry the rest: `a/(b·c)` says what it means and
removes the question instead of documenting the answer.

**Pick one prefix spelling.** Since compact prefixes were accepted, every prefixed unit has two
spellings — `k~g` and `kg` — held apart by the longest-alias-suffix rule plus a hand-maintained
`compact_exceptions` list (`usb`, `kt`) for the tokens where the rule gives the wrong answer. Two
spellings and a patch list is the shape of a rule that will need a third entry. This one is a genuine
trade-off, and either resolution is defensible: compact-only with a normative collision table (my
preference — `~` is the most alien thing in the format to a first-time reader, and it exists only
because the registry has collisions), or `~`-mandatory, which is what the writer already emits. What
is not defensible for another decade is both.

---

## 13. Octet streams: declared, bounded, checked

**Today.** A `text/vnd.bovnar` document may contain arbitrary binary, framed by 16-bit little-endian
length prefixes (with `0x0000` meaning 65536), invisible to every line-oriented tool, and
unrecoverable if any byte in a payload is rewritten. The spec's §18.4 is candid about the failure
mode: desync "is not guaranteed" to be detected, and a length read out of a corrupted field is an
allocation request.

**Why it is a problem.** `--text-only` is a *consumer-side* defence for a hazard only the *producer*
knows about, and the reader that most needs to refuse the document is the one that has already
allocated on a corrupted length.

**Proposed.** Three changes, none of which touch the text layer:

- **declare it**: a document containing an octet stream says so in the header directive (section 10),
  so a text-shaped pipeline refuses at byte zero rather than at the first `0x00`;
- **check it**: a per-stream total length and a checksum, so desync is *detected* rather than
  probably-detected;
- **simplify it**: a 32-bit or varint length, which retires the `0x0000 means 65536` special case
  along with the class of off-by-one it invites.

**Cost.** Wire-incompatible with every 1.x octet stream, and a checksum is real work on a hot path —
so it belongs behind a per-document declaration, not on every chunk unconditionally.

---

## 14. Priority, and the test a breaking change has to pass

**The test.** *Every change in a major revision must be mechanically transcodable from a 1.x parse
tree.* Each item above passes it: quoted keys (quote when needed), holes to `null` (already what the
serialiser does), `/` to nested-or-shape (the row count is in the DOM), base literals (the base is in
the annotation), symbol sigils (a token-level rewrite), named parameters (a shape-to-name lookup).
A proposal that cannot be transcoded is a proposal to abandon the archive, and the archive is the
reason the 1.0 freeze was worth its cost. That test is the entry requirement, not this note's
contents.

**The order.** If the whole list ships, the order that minimises rework is: section 10 (the directive,
because everything else needs a way to be declared), then section 7 (named parameters) and section 6
(the inline unit) together, since the second is defined in terms of the first, then sections 8, 9, 11,
5, 4, 12 and 13 in any order, and section 3 last — it is the only one whose blast radius is other
people's *readers* rather than other people's *documents*.

**If it is cut to three:** sections 3, 6 and 8. The first two are defects, and the third is the only
door that closes for good.

---

## See also

- [Specification](03_bovnar_spec.md) — spec §7.4 (homogeneity), spec §13.2 (recovery), spec §17 (what
  1.0 freezes and what needs a 2.0), spec §18 (the security notes several proposals here answer)
- [EBNF Grammar](12_bovnar.ebnf) — the grammar every "today" claim above was read from
- [Unit & Currency Reference](05_bovnar_unit_system.md) — the notation grammar behind section 12
- [Temperature Difference (Design Note)](temperature_difference.md) — the same shape of argument for
  a change that turned out **not** to need a grammar break
- [Unit Profiles](11_bovnar_unit_profiles.md) — why the profile code is deliberately given no grammar

---

*End of Bovnar — Version 2 Grammar Proposals (Design Note) (Bovnar spec 1.1).*
