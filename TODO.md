# TODO — spec review round of 2026-10-12

Fixes 1–4 of this round are committed as `759cd49` (spec §5.1 twice, §16.4,
the grammar's escape comment, the IETF draft and its renderings, regenerated
web pages). Full build + `ctest` 182/182 before the commit. Not pushed. No
CHANGELOG entry was added (wording only, no behaviour change) and no
ASan/UBSan pass was run (no C sources changed).

## Open findings, not fixed

1. **`[]` is formally ambiguous in the grammar** (`doc/12_bovnar.ebnf`, `array-row`
   / `row-content` / `array-elem` / `null-value`): `[]` derives both as an absent
   `row-content` (zero elements) and as one `array-elem` with the empty
   `null-value` (one null). Only the comment above `row-content` settles it;
   §7.2 says zero elements. Fix in the productions, then mirror in the ABNF
   copies in the IETF draft.
2. **`bvnr_reader_get_error` after a recovered error** (§13, §16.7): the spec does
   not say what it returns under `continue_on_error`. Library behaviour not
   tested yet.
3. **Appendix C** does not list the five length limits (they are in the §12
   table); consider adding them.
4. `doc/02_bovnar_faq.md:305–309` still has the short wording on TAI injectivity
   and the datetime error split. Not contradictory; left alone.

## Review coverage still missing

- Sections 12–15 got only a light read ("no findings" after five file reads).
- No reviewer saw the whole spec at once, so nothing crossing sections was
  looked for (each run covered one part plus the grammar).
- `google/gemini-3.8-flash` produced one partial finding in four runs
  (three aborts, one step-limit); its findings 1–7 on §16–18 and its finding 9
  on `strict_version` were lost to truncation. Rerun §16–18 if wanted.

## How the review was run

crosscheck `delegate`, read-only, one part of the spec plus the full EBNF per
run, at most 400 lines per read, and the instruction to stop reading and answer.
The whole-spec run in one task returned an empty answer from both models.
Cost so far: about $0.57 (gpt-6-luna about $0.07, gemini-3.8-flash about $0.50).
