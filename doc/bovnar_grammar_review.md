# Bovnar grammar review and v2 proposals

Scope: `doc/5_bovnar.ebnf` (the canonical grammar; the spec §14 defers to it),
cross-checked against `src/lexer/` (state table + `bvn_escape_lut`),
`src/validator/bovnar_validator.c`, `src/utils/bovnar_si_units.c`, and the spec.
Everything below is verified against the implementation, not just the EBNF text.

> **Update — several "v2 proposals" below shipped in spec 1.1.** This review was
> written against 1.0. Spec 1.1 (now drafted) implemented three of the proposals
> as 1.1-gated, opt-in features, so their "currently a hard error" framing is no
> longer accurate for a `#!bovnar 1.1` document:
> - **P3 (richer escapes)** — `\xHH` and `\u{…}` are now valid in 1.1 strings.
> - **P5 (time family)** — the `datetime` family (`<datetime:width,epoch>`) plus
>   ISO-8601 datetime literals now exist.
> - **P10 (reference array indexing)** — `&.matrix[0][1]` now resolves at the DOM
>   layer.
> See `doc/1_bovnar_spec.md` / `doc/5_bovnar.ebnf` for the shipped syntax.

---

## 0. TL;DR

The grammar is unusually good: it is a faithful, single-pass, effectively LL(1)
reflection of a byte-class-driven state machine, with sigil-led disambiguation
and an honest "constraints not expressible in CFG" appendix. It is *correct*
about the implementation in every place I spot-checked.

The weaknesses are not bugs; they are **scope and layering** issues:

1. The formal grammar **over-generates** and offloads real syntactic rules to
   the validator, even where those rules are perfectly context-free.
2. The **unit sub-grammar is written in comments**, so the most distinctive part
   of the language is not machine-checkable and cannot drive a generated parser.
3. A handful of **ergonomic gaps** (escapes, radix literals, quoted keys,
   top-level scalars, exponent range) make lossless interchange — including your
   own JSON round-trip — awkward or impossible.

The good news for v2: **almost every extension I propose occupies syntactic
space that is currently a hard error.** That means v2 can be a strict superset —
every valid 1.0 document stays valid and keeps its meaning. The only proposals
that touch existing meaning are flagged explicitly.

A second recurring theme, given that this is an embedded/POSIX target: every
proposal is checked against "does this stay implementable as a single-pass,
table-driven, no-backtrack lexer with O(1) state?" I note where a proposal costs
a counter or a small sub-state machine versus where it's free.

---

## 1. What the existing grammar does well (so v2 doesn't regress it)

These are load-bearing properties. Treat them as invariants for v2.

- **Sigil-led, single-token lookahead.** Every construct is disambiguated by its
  first byte from value position: `.`=key, `<`=annotation, `[`=array,
  `{`=struct, `"`=string, `&`=reference, `\x00`=octet stream, `-`/`.`/digit =
  number, alpha/`_`/UTF-8-leader = symbol-or-keyword. No backtracking, no
  two-token lookahead. This is why the lexer is a flat table.

- **Enumerated EOF-accept states.** The grammar names the four lexer states in
  which EOF is legal (`undefined`, `first_comment_intro`, `first_bom`,
  `value_outro`) plus `struct_nesting_level == 0`. That is exactly the
  information a re-implementer needs and almost no hand-written grammar provides.

- **Honest §13 appendix.** The "not expressible in context-free EBNF" section is
  the right way to document UTF-8 validity, nesting caps, homogeneity, and the
  recovery state machine. Keep it.

- **Keyword-as-symbol reclassification.** `null`/`true`/`false`/`on`/`off`/
  `nan`/`inf`/`ninf` are lexed as ordinary symbols and reclassified by the
  validator, so `truthy` and `infinity` stay symbols with zero special-casing in
  the lexer. This is the correct factoring and the grammar documents it clearly.

Keep all four.

---

## 2. Weaknesses in the grammar *as it stands* (1.0)

### 2.1 The grammar over-generates; several of those rules are context-free

`value` and `array-elem` are defined identically:

```ebnf
value      = [type-annotation , ws] , raw-value ;
array-elem = [type-annotation , ws] , raw-value ;
raw-value  = null-value | bool-value | special-number
           | ( number | string ) , [ ws-mandatory , inline-unit ]
           | symbol | reference | array | struct | octet-stream ;
```

Two real constraints are pushed down to the action layer and labelled
"not expressible in context-free EBNF" — but **they are expressible**:

- **Inline units are forbidden inside array elements.** §6.5 and §13(o) say this
  is enforced by the `in_array_element` flag. But it is trivially CF: give
  `array-elem` a `raw-value` variant *without* the `[ws-mandatory, inline-unit]`
  option.

- **A type annotation before a symbol / reference / struct / octet-stream is a
  semantic error** (`error_illegal_value_type`). The grammar lets you write
  `<uint:8> &.foo` or `<uint:8> {…}`; only the validator rejects it. Also CF.

Why it matters: the formal grammar is the artifact people generate tooling from
(syntax highlighters, the JS playground, fuzz grammars). When it accepts strings
the language rejects, every downstream tool either re-implements the validator's
rules or quietly disagrees with the reference parser. Your own
`web/bovnar_parser.js` is already documented as lenient *because* of exactly this
gap.

**v2 fix (pure refactor, no language change):** split annotatable vs
non-annotatable values and scalar-with-unit vs element forms.

```ebnf
(* Only numbers, strings, arrays, and the keyword values can carry an
   annotation; symbol / reference / struct / octet-stream cannot.        *)
value          = annotated-value | bare-value ;
annotated-value = type-annotation , ws , annotatable ;
bare-value     = annotatable | symbol | reference | octet-stream ;
annotatable    = null-value | bool-value | special-number
               | scalar-with-unit | array | struct ;
scalar-with-unit = ( number | string ) , [ ws-mandatory , inline-unit ] ;

(* Inside an array, the inline-unit option is simply absent.            *)
array-elem     = [type-annotation , ws] , array-raw-value ;
array-raw-value = null-value | bool-value | special-number
               | number | string         (* no inline-unit here *)
               | symbol | reference | array | struct | octet-stream ;
```

This removes two of the items from §13 and makes the grammar match the language.
(Annotation-on-`struct` is intentionally kept in `annotatable` only if you want
whole-array/whole-struct annotation; if structs never take annotations, drop it
from `annotatable` too.)

### 2.2 The unit sub-grammar is in comments, not productions

The single most distinctive feature of Bovnar — the inline unit algebra — is not
in the grammar. `unit-param` is a flat blob:

```ebnf
unit-param = unit-char , {unit-char} ;
```

and the actual structure (prefix `~`, base unit, exponent, `* · /` separators,
parens, the negation-after-first-`/` rule, the 8-component / depth-16 caps) lives
entirely inside `(* … *)` comments, enforced by `bvn_parse_unit`.

Consequence: you cannot generate a unit validator, a unit-aware highlighter, or a
fuzz grammar from the EBNF. The comment block is also where the spec/EBNF have
already drifted (see 2.5).

**v2 fix:** promote it to real, named productions (P7 below gives the full
sketch). Even if `bvn_parse_unit` stays the authority for *semantic* validity
(which base units are real, which prefixes a unit accepts), the *shape* should be
grammar.

### 2.3 `type-spec` is vestigial

```ebnf
type-spec = param-type ;
```

A one-alternative rule. Either it anticipated non-parameterised type-specs that
never shipped, or it's a leftover. Inline it into `type-annotation` in v2.

### 2.4 Two structurally distinct "dimensionless" states

Spec §11.8: omitting the unit yields `BVN_UNIT_NO_PREFIX(bu_none)` with
`num_components == 1`; an explicit `no_unit` yields `BVN_UNIT_NONE` with
`num_components == 0`. They compare equal and both serialise to `"no_unit"`, but
they are distinct in-memory states. That is a canonicalisation smell: two
representations of one concept, kept apart only by `bvn_units_compatible`
papering over the difference. It will bite anyone who does a raw `memcmp` on
`value_unit_t` (which is exactly what the inline-vs-annotation unit check does,
per §13(o) — it works today only because both sides happen to take the same
path). v2 should pick one canonical dimensionless representation and normalise to
it at parse time.

### 2.5 Documentation drift

- ~~The EBNF header still says `Updated: 2026-05-29`~~ — **resolved**: the EBNF
  banner now reads `Updated: 2026-06-15 — spec 1.1`.
- Exponent notation disagreed between layers: spec §11.5 table said the caret
  form is `^[+-]?[0-9]` (which would admit `^0`), while the EBNF comment says
  `"^" ["-"|"+"] ASCII-digit` *and* "`^0` is not a valid exponent." The
  implementation (the `exp_*` enum) confirms the EBNF: magnitude 1–9, no zero.
  **Resolved**: the spec §11.5 table now reads `^[+-]?[1-9]`.

### 2.6 Octet chunk length encoding wart

```ebnf
os-length = byte , byte ;   (* little-endian uint16; 0x0000 → 65536 *)
```

`0x0000` meaning 65536 (not 0) is a classic off-by-one footgun: a zero-length
chunk is unrepresentable, and every implementer must remember the special case.
This is wire-format, not grammar proper, but if v2 ever revisits the octet frame,
prefer an explicit large-chunk tag over magic-zero (P6/§3 note).

---

## 3. v2 proposals

Each proposal: motivation → EBNF sketch → example → tradeoffs → lexer/wire
impact. Ordered roughly by value-to-effort.

Legend for compatibility:
- **(superset)** = occupies syntactic space that is a hard error in 1.0; old docs
  unaffected.
- **(meaning change)** = could change how an existing-but-currently-legal byte
  sequence is interpreted; needs a version gate.

---

### P1 — Quoted keys and a bare top-level value (superset) — highest interop value

**Motivation.** Two restrictions block lossless interchange, including your own
`convert_json_roundtrip` path:

- Keys must be identifiers: `key = id-start , {id-body-char}`. A JSON object key
  like `"first name"`, `"2024"`, or `""` has no Bovnar spelling. JSON→Bovnar is
  therefore lossy for any non-identifier key.
- The document root must be a sequence of assignments
  (`stream = [utf8-bom] , ws , {assignment , ws}`). A JSON document whose root is
  `[1,2,3]` or `42` or `"hi"` cannot be represented at all.

**EBNF sketch.**

```ebnf
(* key gains a quoted form; the bare form is unchanged. *)
key            = bare-key | string-literal ;
bare-key       = id-start , {id-body-char} ;

(* root may be a single value document OR the classic assignment map. *)
stream         = [utf8-bom] , ws , ( document-map | document-value ) ;
document-map   = {assignment , ws} ;
document-value = value , ws ;     (* note: no trailing ";" for a root value *)
```

**Example.**

```bovnar
# v2: quoted keys round-trip arbitrary JSON object names
."first name" = "Ada";
."2024"       = <uint:16> 42;
.""           = "empty key is legal in JSON, now legal here";

# v2: a document whose root is a bare value
[1, 2, 3]/[4, 5, 6]
```

**Tradeoffs.** The leading-`.` sigil disambiguates a key from a value at struct
scope; with quoted keys you write `."x" = …`, keeping the sigil. References then
need a quoted segment form to point at quoted keys (couples with P10). Root-value
documents need a rule for the terminator: a root *map* ends at EOF after the last
`;`; a root *value* ends at EOF with no `;` (mirrors how `value_outro` already
accepts EOF). Decide whether a trailing `;` after a root value is required,
optional, or forbidden — I'd make it forbidden so the two root forms can't be
confused.

**Lexer impact.** `."…"` adds one transition: after `.` in key position, `"`
enters the existing `string_intro` machinery but tagged as key-context. Root
value: `stream` already starts by dispatching on the first non-ws byte; you add
the value-start bytes to the top-level dispatch and suppress the mandatory
trailing `;` in that one case. Both are table-local. No backtracking.

---

### P2 — Numeric literal ergonomics: radix prefixes, digit separators, leading `+`

**Motivation.** Today a non-decimal integer must be written as a quoted string
with a base parameter:

```bovnar
.mask = <uint:32,_16> "DEADBEEF";   # 1.0: hex only via quoted string
```

That is verbose, defeats the "the number is a number" intuition, and means the
hex digits live in a string the lexer treats as opaque. Bare literals also reject
`+` and have no digit grouping. For an embedded/register-oriented audience this
is the single most-requested ergonomic in formats like this.

**EBNF sketch (superset for radix/grouping; leading `+` is a tiny meaning
addition).**

```ebnf
number        = [sign] , ( radix-int | dec-number ) ;
sign          = "+" | "-" ;                         (* "+" is new *)

radix-int     = "0" , ("x"|"X") , hex-digit  , {hex-digit  | "_"}
              | "0" , ("b"|"B") , bin-digit  , {bin-digit  | "_"}
              | "0" , ("o"|"O") , oct-digit  , {oct-digit  | "_"} ;

dec-number    = (int-led | dot-led) , [dec-exponent] ;
int-led       = DIGIT , {DIGIT | "_"} , ["." , {DIGIT | "_"}] ;
dot-led       = "." , DIGIT , {DIGIT | "_"} ;
(* "_" is a separator: not leading, not trailing, not doubled — that
   last constraint is the one part you enforce in the action, like base
   digit validity already is. *)
```

**Example.**

```bovnar
.mask      = <uint:32> 0xDEAD_BEEF;     # hex literal, grouped
.flags     = <uint:8>  0b1010_0101;     # binary literal
.perm      = <uint:16> 0o755;           # octal literal
.population = 8_100_000_000;            # decimal grouping
.delta      = +3;                        # explicit positive sign
```

**Tradeoffs / meaning changes.**
- Radix prefixes and `_` separators are **(superset)**: `0xFF`, `0b1`, `0o7`, and
  `1_000` are all hard errors in 1.0 (`x`/`b`/`o`/`_` are not accepted in
  `zero_intro` / `copy_number_byte`), so adding them breaks nothing.
- Leading `+` is **(meaning change)** only in the narrow sense that `+3` is
  currently `error` and becomes `3`. No existing valid document changes. But note
  `+` already means "positive exponent sign" inside `dec-exponent` and "no-op
  positive" in unit exponents, so the precedent exists; just decide whether `+0`
  and `+nan`-style oddities are normalised.
- **Base parameter interaction:** if a literal carries its own radix prefix
  *and* an explicit `_base` annotation, define the rule. Cleanest: a prefixed
  literal fixes the base, and a conflicting `<…,_N>` is `error_illegal_value_type`
  (or must agree). Document it; don't leave it to chance.
- Octal `0o` vs the existing leading-zero rule: 1.0 explicitly allows `007` as
  decimal 7. Keep that — `0o7` is the octal form; `007` stays decimal. No C-style
  "leading zero is octal" trap.

**Lexer impact.** `zero_intro` already exists as a distinct state precisely
because a leading `0` needs special handling. Add `x/X`, `b/B`, `o/O`
transitions out of it into three short digit-accumulation states. `_` becomes an
accepted-and-skipped byte in the number-body states (don't push it to the
accumulator; let `bvn_acc_parse_number` see clean digits). All O(1), table-driven,
no lookahead.

---

### P3 — Richer string escapes and multi-line / raw strings (superset)

**Motivation.** The entire escape set is `\t \n \v \f \r \" \\`. There is **no
way to encode an arbitrary code point or control byte in a string**: `\u`, `\x`,
`\0`, `\a`, `\b` are all `error_illegal_escape_sequence`, and raw control bytes
`0x00–0x08`, `0x0E–0x1F`, `0x7F` are hard errors even inside quotes. So a string
containing U+0007 (BEL) or U+200B (zero-width space, which you'd never want to
paste literally) is impossible without dropping to an octet stream. Of the seven
escapes, only `\"` and `\\` are non-redundant — the rest duplicate bytes you can
type raw.

**EBNF sketch.**

```ebnf
escape-seq = "\" , ( "t" | "n" | "v" | "f" | "r" | "\"" | "\\"
                   | "0" | "a" | "b"                          (* new C escapes *)
                   | "x" , hex-digit , hex-digit              (* \xHH byte    *)
                   | "u" , hex-digit , hex-digit , hex-digit , hex-digit  (* \uHHHH *)
                   | "u" , "{" , hex-digit , {hex-digit} , "}" (* \u{1F600}   *)
                   ) ;

(* Raw / multi-line string: no escapes processed, no UTF-8-only control ban
   beyond NUL; ends at the closing triple-quote. *)
raw-string = '"""' , { raw-string-byte } , '"""' ;
```

**Example.**

```bovnar
.bell     = "ring\x07now";
.emoji    = "score: \u{1F600}";
.bmp      = "\u00B5 is micro";       # µ via escape instead of raw bytes
.banner   = """
line one
line two with "quotes" and \no escaping
""";
```

**Tradeoffs.**
- `\u{…}` must reject surrogates and > U+10FFFF (same checks `bvn_utf8_feed`
  already encodes); `\xHH` injects a *byte*, so two `\x` halves of a multibyte
  sequence must still validate as UTF-8 in the text layer — or you explicitly
  scope `\x` to "only inside the safe ASCII range" to avoid producing invalid
  UTF-8. I'd restrict `\x` to `\x00–\x7F` and route everything multibyte through
  `\u`.
- Triple-quote raw strings interact with your existing **adjacent-literal
  concatenation** (`"a" "b"` → `"ab"`). Decide whether a raw string participates
  in concatenation (I'd say yes, uniformly).
- `\0` lets a NUL into a *string* value, which today only an octet stream can
  carry. That's a feature, but it means string length is no longer a safe C
  `strlen`; your API already carries explicit lengths, so confirm every consumer
  is length-clean before enabling `\0`.

**Lexer impact.** The escape LUT (`bvn_escape_lut[256]`) stays for the
single-char escapes. `\x`/`\u` need a tiny hex-accumulation sub-state (2 or 4
iterations, or until `}`). Triple-quote needs a 2-byte opener lookahead handled
as states `string_intro → maybe_triple → raw_body`. All bounded, all table-driven.

---

### P4 — Multi-digit and grouped unit exponents (superset, but needs a wire/repr change)

**Motivation.** Unit exponents are capped at magnitude 9. The internal
representation is an enum (`exp_linear … exp_nonic`, `exp_neg_linear …
exp_neg_nonic`), so `m^10`, `s⁻¹⁰`, or any reduction that lands outside ±9 is
**unrepresentable**, not merely unwritten. `bvn_int_to_exponent(10)` has no case.
For most SI work ±9 is plenty, but hyper-derived or per-something-cubed-per-…
units, and especially *intermediate* results of `bvn_unit_reduce`, can exceed it.

**EBNF sketch.**

```ebnf
unit-exponent = unicode-exponent | caret-exponent ;
unicode-exponent = [exp-sign] , exp-digit , {exp-digit} ;   (* multi-digit now *)
caret-exponent   = "^" , ["+"|"-"] , DIGIT , {DIGIT} ;       (* ^10, ^-12 ok   *)
```

**Example.**

```bovnar
.weird = <float:64,m^12> 1.0;        # was impossible
.super = <float:64,s⁻¹⁰> 1.0;        # superscript 10 (two glyphs)
```

**Tradeoffs.** The grammar change is a superset (multi-digit superscripts and
`^10` are errors today), **but** it forces a representation change: replace the
`unit_exponent_t` enum with a small signed integer field (`int8_t` covers
±127; pick a documented cap, e.g. ±64, and raise `error_unit_illegal` beyond it).
That ripples through `bvn_exponent_to_int` / `bvn_int_to_exponent` (which become
identity/clamp), `bvn_unit_reduce`, the writer's superscript formatter, and the
`value_unit_t` `memcmp` equality (fine as long as the field is still
zero-padded). This is the one proposal whose cost is mostly *below* the grammar.

---

### P5 — Native temporal type family (superset) — strong fit for the domain

**Motivation.** The format's stated home is "scientific instrumentation,
industrial telemetry, IoT, long-term archival." Every one of those is timestamp-
heavy, and right now a timestamp is either an opaque `utf8` string or a bare
`<sint:64,s>` epoch count with no way to say "this is a wall-clock instant" vs
"this is a duration." Unit-safety is the whole pitch; time is the unit users most
want typed.

You already have `s`, `min`, `h`, `d`, `wk`, `yr` as *units* — so durations are
half-done. What's missing is a calendar instant.

**Two sub-options:**

(a) Lean: keep it in the unit system. Durations are already expressible
(`<float:64,s> 1.5`); just bless an RFC 3339 *instant* as a string subtype:

```bovnar
.captured_at = <utf8> "2026-06-09T12:00:00Z";   # 1.0 today: opaque
```

(b) Native family `time` with a base and an epoch/scale parameter:

```ebnf
param-type = ("uint"|"sint"|"float"|"float_fix"|"float_dec"|"utf8"|"bool"|"time")
           , [ ws , ":" , ws , type-param-list ] ;

(* time params: width (bits of the tick count), a tick unit (any duration
   unit from the existing table), and an epoch keyword. *)
time-epoch = "unix" | "tai" | "gps" | "iso" ;
```

```bovnar
.t_unix = <time:64,s,unix>   1749470400;          # seconds since 1970
.t_ns   = <time:64,ns,unix>  1749470400000000000; # ns ticks
.t_iso  = <time,iso>         "2026-06-09T12:00:00Z";
.uptime = <float:64,s>       42.5;                # duration stays a unit
```

**Tradeoffs.** Option (a) costs nothing and is honest about scope ("Bovnar types
*quantities*; a timestamp is a string"). Option (b) is a genuine new family and a
lot of validator surface (leap seconds, epoch arithmetic, ISO parsing) — arguably
*against* the format's minimalism. My recommendation: ship (a) as a documented
convention in v2, and only add (b) if telemetry users actually need
parser-enforced epoch typing. I include (b) mainly to show the grammar slot is
clean (`time` reuses the existing `param-type` keyword machine — it's one more
`tf_*` path).

---

### P6 — In-band document separator (superset) — fold framing into the grammar

**Motivation.** `doc/10` is explicit: *"The grammar has no in-band document
separator: a reader ends one document at EOF."* So multi-document streams need
the out-of-band `BVF1` length-prefixed frame. That's fine for binary transports,
but a *text* log of concatenated documents (the "mixed text-binary log streams"
use case in the README) has no text-native way to say "document boundary here."

**EBNF sketch.**

```ebnf
stream    = [utf8-bom] , ws , document , { doc-sep , document } ;
doc-sep   = ws , "---" , ( "\x0A" | "\x0D" ) ;   (* a line of exactly --- *)
document  = {assignment , ws} ;                   (* or document-value, see P1 *)
```

(YAML's `---` is the obvious precedent and reads naturally in logs.)

**Example.**

```bovnar
.reading = <float:64,m/s> 9.81;
.t       = <sint:64,s> 1749470400;
---
.reading = <float:64,m/s> 9.79;
.t       = <sint:64,s> 1749470401;
```

**Tradeoffs.** Only legal at top level (nesting must be 0 — same condition as
EOF-accept), so it's a top-level-dispatch addition, not a deep change. It does
*not* replace `BVF1` framing for binary transports (you still want length
prefixes when payloads contain octet streams that could embed `\n---\n`); it's
the text-stream complement. Reusing the BOM offset-0 rule, a `---` separator
resets you to the same "start of a fresh document" state. **Caution:** define
precedence against octet streams clearly — `---` inside an octet payload is data,
not a separator, because the lexer is in binary mode there; that falls out
naturally from the existing mode flag.

---

### P7 — Promote the unit sub-grammar to real productions (no language change)

This is the 2.2 fix, written out. It moves the comment-only algebra into the
formal grammar so tooling can consume it. Semantic validity (which base units /
prefixes exist, prefix-applicability rules, the 8-component cap) stays with
`bvn_parse_unit`; only the *shape* becomes grammar.

```ebnf
resolved-unit  = "no_unit" | unit-expr ;
unit-expr      = unit-factor , { unit-sep , unit-factor } ;
unit-factor    = unit-component | "(" , unit-expr , ")" ;
unit-sep       = "*" | "·" | "/" ;
unit-component = [ prefix , "~" ] , base-unit , [ unit-exponent ] ;

prefix         = si-prefix | iec-prefix ;
base-unit      = (* the enumerated set from §4, kept in the grammar *) ;
unit-exponent  = unicode-exponent | caret-exponent ;   (* see P4 *)

(* currency component: mandatory "$" sigil, optionally prefixed *)
currency-comp  = [ prefix , "~" ] , "$" , currency-code ;
```

Two clarifications the comment block currently only implies, worth making
grammatical:
- A group cannot (yet) carry its own exponent: `(m/s)²` is not accepted. If P4
  lands, consider allowing `unit-factor = … | "(" unit-expr ")" [unit-exponent]`
  so `(m/s)²` becomes legal — that's a real ergonomic win and the reduction logic
  already negates group exponents wholesale.
- The "first `/` flips everything after into the denominator, later `/` never
  flips back" rule is semantic, not shape — keep it in §13, but reference it from
  the `unit-sep` production so re-implementers don't miss it.

**Example of the group-exponent extension (depends on P4):**

```bovnar
.jerk_density = <float:64,(m/s³)²> 1.0;   # ((m·s⁻³))² = m²·s⁻⁶
```

---

### P8 — Block comments and value-attached doc comments (superset)

**Motivation.** Only `# … EOL` line comments exist. Config-heavy users want to
comment out a block, and tooling (doc generators, the highlighters you already
ship) benefits from a comment that's *associated* with a value rather than
floating.

**EBNF sketch.**

```ebnf
comment      = line-comment | block-comment ;
line-comment = "#" , {comment-char} , ("\x0D" | "\x0A" | (* eof *)) ;
block-comment = "#[" , block-body , "]#" ;     (* nesting allowed *)
```

`#[ … ]#` (rather than `/* … */`) keeps `#` as the universal comment sigil, so a
highlighter's "comment starts with `#`" rule still holds, and `/` stays
unambiguously the array-row / unit-division operator.

**Example.**

```bovnar
#[ this whole block is a comment
   #[ and it nests ]#
   .disabled = 1; ]#

.gain = <float:64,no_unit> 2.0;   # inline, value-attached
```

**Tradeoffs.** Nesting needs a depth counter — the first thing in this list that
isn't strictly O(1)-state, but it's a single integer, trivially embeddable.
Decide whether block comments are legal mid-token (I'd forbid them inside a
number/string/unit, allowing them only where `ws` is allowed, matching today's
line comment).

---

### P9 — Formalise annotation placement (the 2.1 refactor as a spec change)

Not a new feature — adopt the tightened productions from 2.1 as *the* grammar, so
the formal grammar stops accepting `<uint:8> &.foo`, `<uint:8> {…}`, and inline
units inside arrays. This shrinks §13 (remove items o-in-array and the
annotation-target constraint) and makes generated tooling correct by
construction. Zero impact on valid documents.

---

### P10 — Reference path indexing and quoted segments (superset; pairs with P1)

**Motivation.** A reference can only name identifier-keyed struct paths
(`&.a.b.c`). It cannot point into an array element, and — once P1 adds quoted
keys — it cannot name a quoted key either.

**EBNF sketch.**

```ebnf
reference   = "&" , ref-segment , { ref-segment } ;
ref-segment = "." , ( bare-key | string-literal )   (* quoted segment, P1 *)
            | "[" , DIGIT , {DIGIT} , "]" ;          (* array index        *)
```

**Example.**

```bovnar
.matrix = [10, 20, 30]/[40, 50, 60];
.row0c1 = &.matrix[0][1];          # → 20  (resolution still app's job)
.weird  = &."first name";          # reference a quoted key
```

**Tradeoffs.** References are stored unresolved and never dereferenced by the
library (§8), so this is purely a *path syntax* extension — no resolver, no cycle
detection added. It only matters once something (your DOM, an app) chooses to
resolve them. Cheap to add to the grammar, defer the resolver.

---

## 4. Suggested priority for v2

If v2 is "conservative superset, ship fast":
**P1 (quoted keys + root value), P2 (radix + separators), P3 (escapes), P9
(formalise placement), P7 (unit productions).** All five are either pure refactors
or strict supersets, none touches the wire format, and together they close the
interchange gap and make the grammar machine-checkable.

Defer **P4** (needs the exponent-representation change), **P5(b)** (large
validator surface; ship P5(a) convention instead), **P6** (nice for text logs but
overlaps `BVF1`), **P8** (counter state), and **P10** (only pays off with a
resolver).

## 5. One thing I would *not* change

The "type the value, not the schema" core and the single-pass table lexer. Every
proposal above is deliberately shaped to keep the lexer flat and single-token.
The moment a proposal needs real backtracking or two-token lookahead, it's the
wrong proposal for this format — the embedded story is the differentiator, and
LL(1)-on-a-byte-table is what makes it true.
