# Bovnar 1.1 — Implementation Plan

Status: Draft
Author: planning pass, 2026-06-15
Scope: the four items in the repo-root `TODO` file.

> All four items are **additive** to the frozen v1.0 grammar (old documents
> still parse unchanged), so this is a **semver-minor bump to spec 1.1**.
> Item 1 (an in-document version declaration) is the linchpin: it lets a
> document opt in to 1.1 syntax and lets a parser refuse a document that needs
> features it does not implement. **Build item 1 first**; items 2–4 ship as
> 1.1-gated features that declare against it.

## Locked decisions

These were decided during planning and are assumed throughout:

1. **`\x` stays valid UTF-8.** `\xHH` writes a byte, but the string as a whole
   must still form valid UTF-8 (`\xC3\xA9` = `é` ok; `\xFF` alone errors). The
   `utf8` family keeps its "contents are valid UTF-8" guarantee; Python `str`,
   the JS parser, and JSON conversion are untouched. Octet streams remain the
   mechanism for carrying arbitrary, non-textual bytes.
2. **Time family = annotation over a number** (e.g. `<datetime:64,unix> 1750000000`).
   No new value-literal grammar in 1.1. Reuses the already-built
   `bvn_datetime` / `bvn_gregorian_date` utilities. First-class ISO-8601
   literals are explicitly deferred to a later milestone (see Item 3 §Deferred).

## Standard blast radius (SBR)

Every item touches most of this list; it is stated once and referenced per item:

- Lexer state table + actions — `src/lexer/bovnar_state_table.c`, `bovnar_lexer.c`, `bvn_lexer_impl.h`
- Validator — `src/validator/bovnar_validator.c`
- Writer — `src/writer/bovnar_writer.c`, `bovnar_write_utils.c`, `bovnar_canon_observer.c`
- DOM — `src/dom/bovnar_dom.c`, `bovnar_dom_builder.c`, `bvn_dom_impl.h`
- Public API — `include/bovnar.h` (event/family/error enums are API surface)
- Python bindings — `python/bovnar/` (`enums.py`, `reader.py`, `writer.py`, `dom.py`, `quantity.py`)
- Lenient JS playground parser — `web/bovnar_parser.js` (not conformant, but must not break on new syntax)
- Spec & grammar — `doc/1_bovnar_spec.md`, `doc/5_bovnar.ebnf`
- FAQ / tutorial / cheatsheet — `doc/6_bovnar_faq.md`, `doc/0_bovnar_tutorial.md`, `doc/8_unit_cheatsheet.md`
- Conformance suite — `tests/bvnr_conformance.c` (the **207-case** count is quoted in README and docs and will move)
- Syntax highlighters — `highlighter/{vscode,sublime,geany,vim,clion}`
- Amalgamation — regenerate `dist/bovnar.c` / `dist/bovnar.h` via `amalgamate.py`

---

## Item 1 — Spec version in files  *(foundation — do first)*

### Current state
- A document is `[BOM] (ws|comment)* assignment*`. No header/preamble/directive
  concept exists (`doc/5_bovnar.ebnf:67` `stream = [utf8-bom], ws, {assignment, ws}`).
- Version lives only in compile-time macros: `BVNR_VERSION_*` (`include/bovnar.h:50`),
  Python `__version__` (`python/bovnar/__init__.py:101`), CMake `project(... VERSION 1.0.0)`.
- **No runtime `bvnr_version()`** exists.
- The lexer already has `first_bom` / `first_comment_*` states (today they watch
  for a mid-comment BOM) — the hook point for a first-line directive.
- The only existing envelope is the optional `BVF1` stream-framing magic
  (`bovnar_stream.h`), which lives beside the grammar, not in it.

### Design — first-line comment-form directive
A v1.0 parser must not choke on the marker, so it must be ignorable as a comment
by v1.0 and recognized by v1.1+:

```bovnar
#!bovnar 1.1
.key = value;
```

Rejected alternatives: a reserved data key (`.__bovnar_spec__`) pollutes the
data namespace; a `<bovnar_spec:1.1>` pseudo-type is a non-backward-compatible
grammar change. Both are worse than the comment form.

### Work
- Extend the `first_comment_*` lexer states to recognize the `#!bovnar <ver>`
  pattern on byte 0 (or immediately after the BOM) and surface it.
- **New event** `ev_version` (or a reader field) carrying the declared version →
  ripples to every event consumer (validator passthrough, DOM builder, writer,
  Python event enum, JS).
- **New runtime API**: `bvnr_version()`; a reader flag for *max supported
  version* / strict mode.
- **Version policy (must be specified, not improvised):** unknown **major** →
  reject; newer **minor** → reject in strict mode, accept-lenient otherwise.
- **Writer**: option to emit the directive; canonical pretty-print emits it when
  the document uses 1.1 features.
- **Semantics to pin down**: `major.minor` vs full semver; the directive must be
  byte 0 (after optional BOM); appearing twice or mid-document is an error.
- **JSON round-trip**: `bvnr→json` drops it; `json→bvnr` emits a default.

### Consequences / risk
- Carving a *semantically load-bearing* comment breaks the current invariant
  that comments are inert — document this deliberately in the FAQ.
- SBR: spec/EBNF gain a `version-directive` production; conformance gains cases;
  add an example file that declares a version.

---

## Item 2 — Richer string escapes (`\u`, `\x`)

### Current state
- Exactly 7 single-byte escapes (`\t \n \v \f \r \" \\`) via a 256-entry LUT
  (`src/lexer/bovnar_lexer.c:239`). `\x` is currently **illegal**
  (`error_illegal_escape_sequence`).
- The escape decoder writes **one byte** to `str_data` (`bovnar_lexer.c:300`).
- String contents are validated as **well-formed UTF-8** (`bvn_utf8_feed`); a raw
  `0xFF` yields `error_invalid_utf8_byte`.
- **No octet-stream collision:** octet streams use raw `0x00` at *value* level;
  a string literal can never contain a raw `0x00`, so `\x00` *inside a string*
  is unambiguously the escape.

### Design (per locked decision 1)
- `\u{1–6 hex}` → Unicode code point → encoded to UTF-8. Recommend the **braced**
  form to reach astral planes; reject code points `> 0x10FFFF` and **lone
  surrogates**. (Decide whether to also accept fixed `\uXXXX`; if so, surrogate
  *pairs* must be handled or astral chars require the braced form.)
- `\xHH` → one byte, but the surrounding string must still validate as UTF-8 as
  a whole. `\xC3\xA9` → `é` (ok); `\xFF` alone → error.

### Work
- **Lexer**: new multi-byte escape states (`\x` consumes 2 hex digits; `\u`
  consumes a braced hex run). The decoder must now push **multiple bytes** to
  `str_data` — update `str_len` / `max_string_length` accounting and the
  `error_string_too_long` path.
- **New error codes** + matching resync variants (existing `resync_string_escape`
  actions): `error_invalid_escape_hex`, `error_invalid_codepoint`,
  `error_invalid_surrogate`.
- **Writer**: round-trips for free — decoded UTF-8 bytes pass through unescaped
  and re-read identically; only the *spelling* is lost (consistent with floats:
  `\u{41}` → `A`). No writer change required under the conservative `\x`.
- **JS parser** decodes escapes independently → add `\u`/`\x`. **Python read
  path** uses the C lexer via FFI → inherits automatically.
- SBR: spec escape table (`doc/1_bovnar_spec.md:231`), EBNF `escape-seq`,
  conformance (valid + error cases; keep `STR-013`), highlighters (5 grammars
  match escape patterns).

### Gating
1.1 feature, declared via Item 1.

---

## Item 3 — Native time family

### Current state — utilities already built, unwired
- `src/utils/bvn_datetime.c` (epochs MJD/NTP/TAI/Unix/GPS/Galileo, full IERS
  leap-second table, UTC↔TAI, timezone/DST) and `src/utils/bvn_gregorian_date.c`
  (Hinnant era algorithm, ±2.5×10¹⁶ yr) exist, are compiled, and are tested
  (`tests/bvn_datetime_test.c`) — but are **completely unwired** into the format.
- Family enum has no time member (`include/bovnar.h:88`):
  `vt_plain, vt_utf8, vt_sint, vt_uint, vt_float, vt_float_fix, vt_float_dec, vt_bool, vt_illegal`.
- The spec explicitly enumerates exactly **7 families** and says extra params are
  errors — adding the 8th is a genuine spec edit.

### Design (per locked decision 2) — annotation over a number
`<datetime:64,unix> 1750000000`. The carrier value is an integer (or fixed/float
for sub-second); the annotation supplies semantics, validation, and epoch
metadata. Mirrors how units already attach to numbers. No new literal grammar.

### Work
- **Enum** `vt_datetime` → every `switch` over families: writer dispatch
  (`bovnar_writer.c:770`), the `memcmp` family-name dispatch in
  `src/utils/bovnar_utils.c:~2214` (place `datetime` ahead of any shorter
  prefix), validator, DOM, Python `enums.py`, JS.
- **Parameters**: define + validate — width (default 64), **epoch**
  (`unix`/`tai`/`gps`/`mjd`…, reusing `bvn_epoch_t`), sub-second scale, optional
  timezone. New annotation-param syntax + validation rules.
- **Link** `bvn_datetime.c` / `bvn_gregorian_date.c` into the validator/writer
  link targets (currently compiled — verify they actually link into those TUs).
- **Default-type semantics**: define a default epoch + width for a bare
  `<datetime>`.
- **Writer**: `bvnr_write_datetime[_unit]()` helpers + formatting.
- **Python**: typed `Quantity` / `loads(typed=True)` mapping to/from
  `datetime.datetime`; enum additions.
- **JSON conversion**: new rule (datetime → ISO string, or number).

### Consequences / risk
- **Units-vs-time footgun:** a *duration* is `<float:64,s>`; a *timestamp* is
  `<datetime>`. Must be documented prominently.
- Largest API surface of the four. SBR + highlighter `datetime` keyword +
  conformance cases.

### Deferred to a later milestone
First-class ISO-8601 literals (`2026-06-15T12:00:00Z`): requires new lexer
scanning and ambiguity work — notably the date `/` clashes with the array
row separator, and `-` / `:` are overloaded. Out of scope for 1.1.

---

## Item 4 — Reference path indexing for arrays  *(DOM-layer resolution)*

Target:
```bovnar
.matrix  = [10, 20, 30]/[40, 50, 60];
.row0c1  = &.matrix[0][1];   # → 20
```

### Current state
- References tokenize as an **unresolved path string** (`&.a.b.c`), stored
  verbatim; the streaming layer never dereferences. Reference body chars are
  identifier chars + `.`; **`[` and `]` are not accepted** (`ACT_NONE` in
  `reference_segment_body`, `src/lexer/bovnar_state_table.c:543`).
- Resolution happens **only** in the DOM via `bvn_dom_lookup`
  (`src/dom/bovnar_dom.c:257`), which splits on `.` and walks structs.
- **Array DOM model**: a slash-array `[10,20,30]/[40,50,60]` is stored **flat** —
  `items[] = {10,20,30,40,50,60}`, `count=6`, `num_dims=2`, `rows_per_dim={3,3}`
  (`src/dom/bvn_dom_impl.h:49`). Comma-nested arrays `[[1,2],[3,4]]` are genuine
  nested nodes.

### Two subtleties
1. **The lexer still needs a small change.** "Resolution at DOM only" is true for
   *resolution*, but to *store* `.matrix[0][1]` as the path string the lexer's
   `reference_segment_body` must accept `[` and `]` (digits are already allowed).
   Make the lexer **lenient** — capture the bytes, validate nothing (consistent
   with "references are stored unresolved; the target need not exist"). All
   structural validation moves to DOM resolution. The JS parser tokenizes
   references too and must mirror this (it currently errors on `[`).
2. **Flat vs nested indexing semantics.** `&.matrix[0][1]` on the flat slash-array
   must map (row 0, col 1) → flat offset `0*3+1` = `items[1]` = **20**. But
   `bvn_dom_array_at(node,0)` returns the *scalar* `items[0]` = 10 — there is no
   per-row sub-node to return for a partial index. So the resolver must handle
   two layouts:
   - **flat multi-row**: accumulate all indices, require index count == `num_dims`,
     compute flat offset from `rows_per_dim`, return `items[offset]`. A *partial*
     index (`&.matrix[0]`) has no node → **error in 1.1** (a "row view" is future
     work).
   - **nested**: descend `items[i]` node-by-node.
   - mixed (flat rows whose elements are nested arrays) needs a precisely
     specified rule.

### Work
- **DOM**: rewrite `bvn_dom_lookup` into a tokenizer over `.seg` *and* `[N]`,
  walking structs + both array layouts, with bounds checking (out-of-range →
  `NULL`), index overflow handling, and a leading-zero/format policy.
- **CLI bonus**: same function backs `bovnar query`, so `bovnar query .matrix[0][1]`
  works for free.
- **Writer**: essentially unchanged — references serialize by copying the stored
  path string, so `[0][1]` round-trips as text.
- **Lexer**: lenient bracket/digit capture in reference body (+ JS mirror).
- SBR: reference grammar in spec/EBNF, DOM + reference tests, FAQ.

### Risk
The flat-vs-nested semantic split is the one genuinely tricky design point — it
**must** be specified in the spec, or third-party conformance implementations
will diverge.

### Gating
1.1 feature, declared via Item 1.

---

## Recommended sequencing

1. **Item 1** — version directive + `bvnr_version()` + strict-mode policy (foundation).
2. **Item 2** — `\u`/`\x`; smallest, lexer-local once `\x` semantics are fixed.
3. **Item 4** — reference indexing; DOM-local + a tiny lexer tweak; spec must nail flat-vs-nested.
4. **Item 3** — time family; largest surface, but utilities are already built.

Ship each item's spec/EBNF/conformance/highlighter/amalgamation updates *with*
that item, not batched at the end. Bump the conformance case count and every
quoted "207" as cases are added.
