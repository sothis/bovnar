# Bovnar — Conformance Test Tool

> **Spec version:** 1.1
> **Status:** Normative — conformance protocol `bvnr-conformance-v1`
> **Scope:** The conformance corpus, the IUT protocol, and how to validate a third-party implementation.

---

## Table of Contents

1. [Purpose](#1-purpose)
2. [Quick Start](#2-quick-start)
    - 2.1 [Self-test (verify the reference implementation)](#21-self-test-verify-the-reference-implementation)
    - 2.2 [Testing an external implementation](#22-testing-an-external-implementation)
    - 2.3 [Filtering by group](#23-filtering-by-group)
3. [Architecture](#3-architecture)
    - 3.1 [Validation tiers](#31-validation-tiers)
4. [Building](#4-building)
5. [Running](#5-running)
    - 5.1 [Command-line options](#51-command-line-options)
    - 5.2 [Examples](#52-examples)
    - 5.3 [Test groups](#53-test-groups)
6. [IUT Protocol](#6-iut-protocol)
    - 6.1 [Communication model](#61-communication-model)
    - 6.2 [Success response](#62-success-response)
    - 6.3 [Error response](#63-error-response)
7. [Writing a Compliant IUT Adapter](#7-writing-a-compliant-iut-adapter)
    - 7.1 [Event log format reference](#71-event-log-format-reference)
    - 7.2 [Field details](#72-field-details)
    - 7.3 [TOKEN_TYPE values for DATA lines](#73-token_type-values-for-data-lines)
    - 7.4 [Effective width and base](#74-effective-width-and-base)
    - 7.5 [Example traces](#75-example-traces)
8. [Test Case Corpus](#8-test-case-corpus)
    - 8.1 [Coverage summary](#81-coverage-summary)
9. [Output Format (TAP)](#9-output-format-tap)
10. [Extending the Corpus](#10-extending-the-corpus)
11. [CMake Integration](#11-cmake-integration)

- [See also](#see-also)

---

## 1. Purpose

The Bovnar Conformance Test Tool (`bvnr_conformance`) verifies that any
implementation of the Bovnar serialization format produces correct, spec-
compliant behaviour.  The reference implementation (this repository) is
used both as the test driver and as the oracle against which candidate
implementations are judged.

Two use modes are supported:

| Mode | Description |
|------|-------------|
| **Self-test** (default) | Runs the corpus against the reference libbvnr directly |
| **IUT test** (`--iut`) | Invokes an external binary and compares its output to the reference |

---

## 2. Quick Start

### 2.1 Self-test (verify the reference implementation)

```sh
cd build
cmake --build . --target bvnr_conformance
ctest -R bvnr_conformance_self --output-on-failure
```

### 2.2 Testing an external implementation

```sh
# Build your adapter (see Section 7)
cc -o my_impl_adapter my_adapter.c -lmy_bovnar

# Run the conformance suite against it
./tests/bvnr_conformance --iut ./my_impl_adapter
```

### 2.3 Filtering by group

```sh
./tests/bvnr_conformance --filter units
./tests/bvnr_conformance --iut ./my_impl_adapter --filter arrays
```

---

## 3. Architecture

```
┌───────────────────────────────────────────────────────────────────┐
│                       bvnr_conformance                            │
│                                                                   │
│  Test corpus (419 cases) ──→ for each test case:                │
│                                                                   │
│  Self-test mode:                    IUT mode:                     │
│  ┌─────────────────────┐            ┌──────────────────────────┐  │
│  │ ref_parse()         │            │ ref_parse() → event log  │  │
│  │  (uses libbvnr)     │            │ fork/exec IUT binary     │  │
│  │ verify error codes  │            │ compare IUT stdout to    │  │
│  │ verify key presence │            │ reference event log      │  │
│  └─────────────────────┘            └──────────────────────────┘  │
│                                                                   │
│  Output: TAP v14 stream                                           │
└───────────────────────────────────────────────────────────────────┘
```

The reference implementation is the single authoritative oracle.  An
implementation is conformant when its IUT adapter produces output
byte-for-byte identical to the reference for every test case.

### 3.1 Validation tiers

Most cases exercise the **streaming reader** (`bvnr_read`): the lexer,
validator, and the `on_verified` event stream the IUT protocol mirrors. A
smaller set — the `homogeneity` group — exercises the **materialised-document
(DOM) tier** (`bvn_dom_parse`), because the spec-1.0 array-homogeneity (§7.4),
struct-shape, and duplicate-key (§8.1) rules are enforced *above* the lexer and
are therefore unreachable through the streaming `on_verified` callback. These
DOM-tier cases run in **self-test mode only**; under `--iut` they are reported
as `# SKIP`, since IUT protocol v1 is streaming-only and cannot express a
DOM-tier check. An implementation that targets full spec-1.0 conformance must
still enforce these rules in its document/tree API; the self-test cases pin the
reference behaviour and its frozen error codes (39, 40, 41).

---

## 4. Building

The conformance tool is built automatically as part of the standard CMake
build.  Both the main driver and the reference IUT adapter are compiled:

```sh
mkdir build && cd build
cmake ..
cmake --build .
```

Targets produced:

| Target | Binary | Purpose |
|--------|--------|---------|
| `bvnr_conformance` | `tests/bvnr_conformance` | Main conformance driver |
| `bvnr_conformance_iut` | `tests/bvnr_conformance_iut` | Reference IUT adapter |

The conformance tool links against `bvnr_static`.  No external
dependencies beyond libc and POSIX are required.

---

## 5. Running

### 5.1 Command-line options

```
bvnr_conformance [OPTIONS]

Options:
  --iut <binary>   Path to the IUT binary to test
  --filter <group> Run only cases in the specified group
  --list           List all test case IDs and descriptions
  --verbose        Print additional diagnostic information
  --help           Show this help and exit
```

### 5.2 Examples

```sh
# Run self-test (reference vs. reference)
./tests/bvnr_conformance

# Run with verbose output
./tests/bvnr_conformance --verbose

# List all test cases
./tests/bvnr_conformance --list

# Run only unit-related tests
./tests/bvnr_conformance --filter units

# Test an external IUT adapter
./tests/bvnr_conformance --iut ./my_adapter

# Test external adapter, units group only
./tests/bvnr_conformance --iut ./my_adapter --filter units
```

### 5.3 Test groups

| Group | Description |
|-------|-------------|
| `encoding` | UTF-8, BOM handling, byte class enforcement, truncated streams |
| `limits` | Lexer token-length caps |
| `version` | `#!bovnar M.N` directive parsing and strictness |
| `identifiers` | Key syntax, length limits |
| `strings` | Escape sequences, concatenation, length limits |
| `datetime` | Timestamp family: epochs, range, version gating, ISO-8601 literals (spec 1.1) |
| `numbers` | Integer and float literals, scientific notation |
| `types` | All type families, widths, bases |
| `default_synthesis` | Automatic type annotation inference |
| `symbols` | Bare-word values |
| `references` | `&.path` syntax |
| `null_values` | Null scalars and null array elements |
| `structs` | Nesting, empty structs, struct arrays |
| `arrays` | 1D, 2D, nested, typed, null elements |
| `octet_streams` | Binary chunk protocol |
| `units` | SI, IEC, compound, inline suffix, mismatch errors |
| `special_numbers` | `nan`, `inf`, `ninf` |
| `roundtrip` | Multi-assignment sequences |
| `recovery` | Error-resync behaviour |
| `comments` | Comment parsing |
| `whitespace` | Whitespace tolerance |
| `homogeneity` | DOM-tier: array homogeneity, struct shape, key uniqueness (self-test only) |

---

## 6. IUT Protocol

The **IUT (Implementation Under Test) Protocol** version 1
(`bvnr-conformance-v1`) defines the interface between the conformance
driver and a candidate implementation's adapter binary.

### 6.1 Communication model

```
conformance driver                    IUT adapter
       │                                   │
       │  fork, exec IUT binary            │
       │ ─────────────────────────────────→│
       │                                   │
       │  Bovnar text (stdin)              │
       │ ─────────────────────────────────→│
       │                                   │
       │       closes stdin (EOF)          │
       │ ─────────────────────────────────→│
       │                                   │
       │  event log or error (stdout)     │
       │ ←─────────────────────────────── │
       │                                   │
       │  exit(0) or exit(1)              │
       │ ←─────────────────────────────── │
```

### 6.2 Success response

The IUT must:

1. Exit with code **0**.
2. Write the conformance event log to stdout with **no trailing garbage**.

The event log is a sequence of lines, one event per line, in the order
the events were received from the `on_verified` callback.

### 6.3 Error response

The IUT must:

1. Exit with code **non-zero** (typically 1).
2. Write exactly one line to stdout: `ERROR <code_name>` followed by `\n`.

Where `<code_name>` is the value returned by `bvn_error_to_string()` for
the first error encountered, e.g. `value_out_of_range`.

---

## 7. Writing a Compliant IUT Adapter

The file `tests/bvnr_conformance_iut.c` is the reference IUT adapter.
It uses the reference libbvnr and is intended both for self-testing and
as a template for third-party implementors.

A minimal conforming adapter must:

1. **Read all of stdin** into a buffer.
2. **Parse the buffer** using the implementation under test.
3. **Collect events** from the `on_verified` callback.
4. **Emit the event log** to stdout on success.
5. **Emit** `ERROR <code_name>\n` to stdout and exit non-zero on failure.

### 7.1 Event log format reference

Each event maps to one line.  Text fields use `\xNN` escaping for any
byte outside the printable ASCII range `0x20–0x7E` or for the backslash
character `0x5C`.

```
STREAM_START
ASSIGNMENT_START <key>
TYPE_ANN_START <family>
TYPE_FAMILY <family>
TYPE_PARAM_WIDTH <N>
TYPE_PARAM_BASE <N>
TYPE_PARAM_Q <N>
TYPE_PARAM_UNIT <unit>
TYPE_ANN_END <family>
DATA <token_type> <value>
STRUCT_START
STRUCT_END
ARRAY_ROW_START
ARRAY_ROW_END
ARRAY_DIM_START
OCTET_STREAM_START
OCTET_STREAM_END
```

### 7.2 Field details

| Line | Fields | Notes |
|------|--------|-------|
| `STREAM_START` | — | Always first |
| `ASSIGNMENT_START <key>` | key: raw key bytes, safe-escaped | |
| `TYPE_ANN_START <ann>` | ann: the **whole annotation body**, safe-escaped — the text between `<` and `>` verbatim, e.g. `float:64,m/s`, not `float`. It is the bare family keyword only when the annotation was *synthesised* for an untyped value (see below) | |
| `TYPE_FAMILY <ann>` | Same bytes as TYPE_ANN_START | |
| `TYPE_PARAM_WIDTH <N>` | N: effective width (0 → 64) | Emitted when the annotation carries a width, and for a synthesised one |
| `TYPE_PARAM_BASE <N>` | N: effective base (0 → 10) | Emitted only when the annotation **states** a base (`_16`), or was synthesised. `<float:64,m/s>` names no base and produces no such line — the events mirror the parameters actually present |
| `TYPE_PARAM_Q <N>` | N: Q parameter | Only for `float_fix` |
| `TYPE_PARAM_UNIT <unit>` | unit: unit string, safe-escaped | Emitted when the annotation carries a unit — and also for a `datetime`, whose **epoch name** travels in this line (`TYPE_PARAM_UNIT tai`). A datetime is not a numeric type; the epoch shares the unit parameter slot because it occupies the same position in the grammar |
| `TYPE_ANN_END <ann>` | Same bytes as TYPE_ANN_START | |
| `DATA <token_type> <value>` | token_type: see below; value: safe-escaped | |
| `STRUCT_START` | — | |
| `STRUCT_END` | — | |
| `ARRAY_ROW_START` | — | |
| `ARRAY_ROW_END` | — | |
| `ARRAY_DIM_START` | — | Emitted between `/`-separated rows |
| `OCTET_STREAM_START` | — | |
| `OCTET_STREAM_END` | — | |

### 7.3 TOKEN_TYPE values for DATA lines

| Token type | String |
|------------|--------|
| `token_is_number` | `number` |
| `token_is_string` | `string` |
| `token_is_symbol` | `symbol` |
| `token_is_reference` | `reference` |
| `token_is_array_number` | `array_number` |
| `token_is_array_string` | `array_string` |
| `token_is_null_value` | `null` |
| `token_is_octet_stream` | `octets` |
| `token_is_bool` | `bool` |

For `octets` token type, the value field is `<N> bytes` (decimal byte
count, then a space, then the literal string `bytes`), not the raw
binary data.

### 7.4 Effective width and base

- **Effective width**: if the stored width is 0, emit `64`.
  Use `bvn_effective_width(value_type)`.
- **Effective base**: if the stored base is 0, emit `10`.
  Use `bvn_effective_base(value_type)`.

These rules ensure that untyped values synthesised to `uint:64,_10`
produce `TYPE_PARAM_WIDTH 64` and `TYPE_PARAM_BASE 10`, matching the
reference output exactly.

### 7.5 Example traces

**Input:** `.x = 42;`

```
STREAM_START
ASSIGNMENT_START x
TYPE_ANN_START uint
TYPE_FAMILY uint
TYPE_PARAM_WIDTH 64
TYPE_PARAM_BASE 10
TYPE_PARAM_UNIT no_unit
TYPE_ANN_END uint
DATA number 42
```

**Input:** `.s = "hello";`

```
STREAM_START
ASSIGNMENT_START s
TYPE_ANN_START utf8
TYPE_FAMILY utf8
TYPE_ANN_END utf8
DATA string hello
```

**Input:** `.v = <float:64,m/s> 9.81;`

```
STREAM_START
ASSIGNMENT_START v
TYPE_ANN_START float:64,m/s
TYPE_FAMILY float:64,m/s
TYPE_PARAM_WIDTH 64
TYPE_PARAM_UNIT m/s
TYPE_ANN_END float:64,m/s
DATA number 9.81
```

Note what an **explicit** annotation does differently from the synthesised one
above, because this is where an adapter is most likely to diverge: the three
`TYPE_ANN_*`/`TYPE_FAMILY` lines carry the annotation body verbatim rather than
the family keyword, and there is no `TYPE_PARAM_BASE` line at all — the source
named no base, and the event stream mirrors the parameters that are there.

**Input:** `#!bovnar 1.1` + `.t = <datetime:64,tai> 1000;` (the datetime family is spec-1.1 gated, so the directive is required)

```
STREAM_START
ASSIGNMENT_START t
TYPE_ANN_START datetime:64,tai
TYPE_FAMILY datetime:64,tai
TYPE_PARAM_WIDTH 64
TYPE_PARAM_UNIT tai
TYPE_ANN_END datetime:64,tai
DATA number 1000
```

The epoch arrives as `TYPE_PARAM_UNIT`, not as a parameter of its own.

**Input:** `.x = ok;` (symbol)

```
STREAM_START
ASSIGNMENT_START x
DATA symbol ok
```

**Input:** `.a = [1, 2];`

```
STREAM_START
ASSIGNMENT_START a
ARRAY_ROW_START
TYPE_ANN_START uint
TYPE_FAMILY uint
TYPE_PARAM_WIDTH 64
TYPE_PARAM_BASE 10
TYPE_PARAM_UNIT no_unit
TYPE_ANN_END uint
DATA array_number 1
DATA array_number 2
ARRAY_ROW_END
```

The element annotation is emitted **once per row**, not once per element: the
array's element type is established at the first element and the rest inherit
it.

**Input:** `.x = <uint:8> 999;` (error — value out of range)

```
ERROR value_out_of_range
```
(exit code 1)

---

## 8. Test Case Corpus

The corpus is embedded in `tests/bvnr_conformance.c`.  Each case
specifies:

| Field | Description |
|-------|-------------|
| `id` | Unique identifier, e.g. `TYP-019` |
| `group` | Group name for `--filter` |
| `description` | Human-readable description |
| `input` | Bovnar text (or binary) to parse |
| `expect` | `CF_VALID` or `CF_ERROR` |
| `expected_error` | Error code for `CF_ERROR` cases |
| `continue_on_error` | Whether the reader should resync |
| `max_*` | Limit overrides (0 = use defaults) |
| `expect_key` | Optional: key name expected in event log |

### 8.1 Coverage summary

| Group | Cases | What is tested |
|-------|-------|---------------|
| `encoding` | 14 | UTF-8 validity, BOM placement, byte classes, truncated streams |
| `limits` | 4 | Lexer token-length caps at both sides of the boundary |
| `version` | 13 | `#!bovnar M.N` directive: valid, malformed, strictness (spec 1.1) |
| `identifiers` | 11 | Syntax, body characters, length limits |
| `strings` | 33 | Escapes, concatenation, UTF-8, limits |
| `datetime` | 54 | Timestamp family: epochs, signed range, gating, ISO-8601 literals (spec 1.1) |
| `numbers` | 16 | Integer, float, scientific, special numbers |
| `types` | 52 | All seven type families, widths, bases, errors |
| `default_synthesis` | 8 | Auto-type inference rules |
| `symbols` | 6 | Bare-word values and limits |
| `references` | 10 | Dotted paths, array indexing (spec 1.1), limits |
| `null_values` | 5 | Null in all positions |
| `structs` | 7 | Nesting, empty, unmatched braces |
| `arrays` | 19 | 1D, 2D, nested, typed, null, limits, /-row size consistency |
| `octet_streams` | 4 | Single/multi-chunk, sync errors |
| `units` | 78 | SI/IEC prefixes, compact prefix form, compound, inline, multi-digit exponents and their bounds, errors |
| `unit_profile` | 47 | The five profile notations: the three outcomes and their error codes, annotations, the decade fold, the one native unit whose error code the profile moved, and cross-vocabulary agreement — each agreement case is an annotation in one notation against an inline unit in another, so it passes only if both spellings produced the same unit |
| `special_numbers` | 5 | `nan`, `inf`, `ninf` |
| `roundtrip` | 5 | Multi-assignment correctness |
| `recovery` | 2 | Error-resync: valid data after error |
| `comments` | 6 | Comment styles |
| `whitespace` | 4 | Whitespace tolerance |
| `homogeneity` | 16 | DOM-tier: array homogeneity (§7.4), struct shape, key uniqueness (§8.1) — self-test only |
| **Total** | **419** | |

---

## 9. Output Format (TAP)

The tool emits **TAP version 14** (Test Anything Protocol), which is consumed
natively by CTest and many CI systems. Each case **group** is a TAP 14 *subtest*:
a 4-space-indented child stream of the individual cases, a trailing child plan,
and a leading `# Subtest:` comment, rolled up into one parent test point. The
parent plan therefore counts the groups (currently 23), not the 419 cases.

```
TAP version 14
1..23
# Subtest: encoding
    ok 1 - [ENC-001] empty stream
    ok 2 - [ENC-002] UTF-8 BOM at byte 0
    not ok 3 - [ENC-003] UTF-8 BOM after first comment
      ---
      message: expected error invalid_byte_order_mark but got none
      ...
    1..14
not ok 1 - encoding
# Subtest: version
    ok 1 - [VER-001] directive declaring the current spec version
    ...
    1..13
ok 2 - version
```

A group's parent point is `not ok` if any of its cases failed. Exit code is **0**
when all tests pass, **1** when any test fails. (`--filter <group>` restricts the
run to a single group; the parent plan is then `1..1`.)

---

## 10. Extending the Corpus

To add a new conformance test, add an entry to the `g_cases[]` array in
`tests/bvnr_conformance.c` using one of the provided macros:

```c
/* Valid input — no expected error */
VALID("GRP-NNN", "group_name", "description of the test",
      ".input = value;"),

/* Valid input — verify a specific key and value appear */
VALID_KEY("GRP-NNN", "group_name", "description",
          ".key = value;", "key", "value"),

/* Error — no special limits */
ERROR_CASE("GRP-NNN", "group_name", "description",
           ".bad = <uint:8> 999;",
           error_value_out_of_range),

/* Error — with continue_on_error (resync mode) */
ERROR_CONT("GRP-NNN", "group_name", "description",
           ".a = 1; .bad = <uint:8> 999; .b = 2;",
           error_value_out_of_range),

/* Error — with custom limits (max_id, max_str, max_num,
                               max_struct_nesting, max_array_nesting,
                               max_array_items) */
ERROR_LIM("GRP-NNN", "group_name", "description",
          ".long_name = 1;",
          error_identifier_too_long,
          4 /*max_id*/, 0, 0, 0, 0, 0),

/* Materialised-document (DOM) tier — validated via bvn_dom_parse instead of
   the streaming reader; runs in self-test mode only (skipped under --iut).
   Use for the spec-1.0 homogeneity / struct-shape / duplicate-key rules. */
DOM_VALID("GRP-NNN", "group_name", "description",
          ".a = [1, 2.5, 3];"),
DOM_ERROR("GRP-NNN", "group_name", "description",
          ".a = [1, \"two\"];",
          error_array_element_type_mismatch),
```

After editing, rebuild:

```sh
cmake --build build --target bvnr_conformance
```

---

## 11. CMake Integration

Two CTest tests are registered:

| CTest name | What it runs |
|------------|-------------|
| `bvnr_conformance_self` | Conformance suite against the reference implementation |
| `bvnr_conformance_iut_self` | Conformance suite using the IUT adapter as the external binary |

Both are in the `conformance` label group and can be run with:

```sh
ctest -L conformance --output-on-failure
```

The `bvnr_conformance_iut_self` test validates the IUT adapter itself:
it must produce bit-for-bit identical output to the internal reference
path for every valid test case.

To run all tests including conformance:

```sh
ctest --output-on-failure
```

---

## See also

- [Specification](03_bovnar_spec.md) — the behaviour the corpus tests
- [EBNF Grammar](12_bovnar.ebnf) — the grammar an implementation under test must accept
- [Read & Write API](08_bovnar_readwrite_api.md) — the reference reader and writer the adapter drives
- [Unit & Currency Reference](05_bovnar_unit_system.md) — the unit table a conforming implementation needs

---

*End of Bovnar — Conformance Test Tool (Bovnar spec 1.1).*
