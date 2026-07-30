# Third-party notices

The unit-profile tables in `src/gendata/` — and the C tables generated from them
into the library, the amalgamation, the WebAssembly build and the Python wheel —
carry **identifier strings from six external vocabularies**. Those strings belong
to their publishers. This file records, for each of them, what was taken, from
which published version, under which licence, and what remains open.

The MIT grant in [`LICENSE`](LICENSE) clause 1 covers **this project's own
contribution**: the native unit registry, the `.bovnar` translation targets, the
refusal rationales, the generators, and every line of C, Python and CMake around
them. It does not, and cannot, grant rights in the third-party identifiers. See
`LICENSE` clause 4.

## What is taken, and what is not

**Taken:** identifier strings only — a UCUM atom code, a QUDT local name, an OM
local name, a UN/ECE common code, a UDUNITS spelling, a CF standard name — and,
for CF alone, the `canonical_units` field each name states.

**Not taken:** no upstream description, definition, label, comment or annotation
appears anywhere in this repository. Where a `.bvnr` row carries prose, that
prose was written here.

**Not redistributed:** no upstream artefact ships with this project. The
publishers' machine-readable definitions are fetched by
`check_profile_factors.py --fetch` into `build/vocab/`, which is git-ignored,
used to verify the tables against their sources, and never packaged. A release
tarball, a wheel, a `.wasm` and the amalgamation contain none of them.

**Adapted, in every case.** No table here reproduces a vocabulary. Each one
extracts that vocabulary's identifiers, maps them onto Bovnar's own unit
registry, and omits every code whose value this project cannot state exactly —
`doc/11_bovnar_unit_profiles.md` §6.4 and §18 record which, and why. Where the
licence below requires a modification notice, this paragraph is it.

---

## UCUM — Unified Code for Units of Measure

| | |
|---|---|
| **Used in** | `src/gendata/ucum.bvnr`, the `ucum:` profile |
| **Version** | UCUM 2.2, revision date 2024-06-17 (`ucum-essence.xml`) |
| **Retrieved** | 2026-07-30, from <https://github.com/ucum-org/ucum> |
| **Home** | <https://ucum.org/> |
| **Extracted** | atom codes, prefix spellings, and the codes named in the refusal list |

> Copyright ©1999-2024, Regenstrief Institute, Inc. All rights reserved.
>
> This product includes material from the Unified Code for Units of Measure
> (UCUM), used under the UCUM Copyright Notice and License, Version 1.1
> (June 2024). The full licence is at <https://ucum.org/license>.
>
> THE WORK IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF
> ANY KIND, either express or implied.
>
> UCUM is a registered trademark of the Regenstrief Institute, Inc.

**Status: permission has not yet been obtained, and this is the one entry here
that needs it.** The UCUM licence is revocable, and its §3(a) conditions the
grant on not creating derivative works of the Work and not adding to, deleting
from or modifying its content. `ucum.bvnr` extracts a subset of the atom table
and maps it onto a different unit model, which is a derivative work on the
licence's own definition (§1.3), whatever its purpose. §2 permits developing
"Software Applications that will communicate with and interoperate with the
Work", which is exactly what the `ucum:` namespace is for — but that grant reads
to a program that consumes UCUM, not obviously to a re-encoded table shipped
alongside one.

Two things are true and worth stating plainly, because §3(a) also bars using the
Work to develop or promulgate a different standard for identifying units of
measure, or to dilute UCUM's purpose:

* Bovnar's native unit notation was designed and specified independently of
  UCUM, is not offered as a replacement for it, and is not derived from it. The
  `ucum:` namespace exists so that a producer holding a UCUM code can write it
  down unchanged rather than translating it by hand first.
* A UCUM code is never silently reinterpreted. A code this project cannot carry
  exactly is refused — `error_unit_profile_unsupported` if UCUM defines it and
  Bovnar cannot represent it, `error_unit_illegal` if UCUM does not define it —
  and never approximated onto a nearby unit.

**Open action:** request written permission from Regenstrief Institute to
distribute this derived mapping table. Until that is granted, the `ucum:`
profile can be excluded from any build with `-DBVNR_WITH_UCUM_PROFILE=OFF`
(`doc/11` §9.4), which drops the table from the library entirely.

---

## QUDT — Quantities, Units, Dimensions and Data Types

| | |
|---|---|
| **Used in** | `src/gendata/qudt.bvnr`, `src/gendata/qudt-qk.bvnr`, `src/gendata/unece.bvnr` |
| **Version** | QUDT 3.1.0 (`owl:versionIRI <http://qudt.org/3.1.0/vocab/unit>`) |
| **Retrieved** | 2026-07-30, from <https://qudt.org/3.1.0/vocab/unit> and <https://qudt.org/3.1.0/vocab/quantitykind> |
| **Home** | <https://qudt.org/> |
| **Extracted** | unit local names, quantity-kind local names, and the `qudt:uneceCommonCode` cross-reference |

> Contains information from the QUDT Ontologies, which is made available under
> the Creative Commons Attribution 4.0 International License (CC BY 4.0),
> <https://creativecommons.org/licenses/by/4.0/>. Attribution is made to
> QUDT.org.
>
> **Modified.** Unit and quantity-kind local names were extracted and mapped
> onto Bovnar's native unit registry; names whose value this project cannot
> state exactly were omitted. QUDT.org does not endorse this project or its use
> of the material.

CC BY 4.0 §2(a)(1) licenses the sui generis database rights as well as the
copyright, so the extraction is covered whichever right applies. There is no
ShareAlike term: the attribution above is the whole of the obligation, and it
imposes nothing on this project's own MIT-licensed code.

---

## OM 2 — Ontology of units of Measure

| | |
|---|---|
| **Used in** | `src/gendata/om.bvnr`, the `om:` profile |
| **Version** | OM 2.0 (`om-2.0.rdf`) |
| **Retrieved** | 2026-07-30, from <https://github.com/HajoRijgersberg/OM> |
| **Home** | <http://www.ontology-of-units-of-measure.org/> |
| **Extracted** | unit local names, and the unit compositions (prefix/base, numerator/denominator, term/term, base/exponent) from which each translation target was derived |

> (c) 2005-2025 Hajo Rijgersberg, Don Willems, Jan Top, Wageningen University
> and Research Centre, The Netherlands. Used under the Creative Commons
> Attribution 4.0 International License (CC BY 4.0),
> <https://creativecommons.org/licenses/by/4.0/>.
>
> **Modified.** Local names were extracted and their translation targets built
> from OM's own stated composition; units whose value this project cannot state
> exactly were omitted. The authors do not endorse this project or its use of
> the material.

OM is published by an EEA maker, so the EU sui generis database right
(§§ 87a ff. UrhG) is the relevant right alongside copyright. CC BY 4.0 §2(a)(1)
licenses it expressly, so the extraction is covered.

---

## UDUNITS-2 — Unidata units library

| | |
|---|---|
| **Used in** | `src/gendata/udunits.bvnr` (the `udunits:` profile), and `src/gendata/cf.bvnr` indirectly — CF states its `canonical_units` in UDUNITS syntax |
| **Version** | `master` branch (`udunits2-base.xml`, `-derived.xml`, `-accepted.xml`, `-common.xml`) |
| **Retrieved** | 2026-07-30, from <https://github.com/Unidata/UDUNITS-2> |
| **Home** | <https://www.unidata.ucar.edu/software/udunits/> |
| **Extracted** | unit symbols, singular and plural spellings, and prefix spellings |

The UDUNITS-2 licence requires this notice to be reproduced in source
distributions and in the documentation accompanying binary distributions. It is
reproduced here in full:

> Copyright 2025 University Corporation for Atmospheric Research and contributors.
> All rights reserved.
>
> This software was developed by the Unidata Program Center of the
> University Corporation for Atmospheric Research (UCAR)
> <http://www.unidata.ucar.edu>.
>
> Redistribution and use in source and binary forms, with or without modification,
> are permitted provided that the following conditions are met:
>
>    1) Redistributions of source code must retain the above copyright notice,
>       this list of conditions and the following disclaimer.
>    2) Redistributions in binary form must reproduce the above copyright notice,
>       this list of conditions and the following disclaimer in the documentation
>       and/or other materials provided with the distribution.
>    3) Neither the names of the development group, the copyright holders, nor the
>       names of contributors may be used to endorse or promote products derived
>       from this software without specific prior written permission.
>    4) This license shall terminate automatically and you may no longer exercise
>       any of the rights granted to you by this license as of the date you
>       commence an action, including a cross-claim or counterclaim, against
>       the copyright holders or any contributor alleging that this software
>       infringes a patent. This termination provision shall not apply for an
>       action alleging patent infringement by combinations of this software with
>       other software or hardware.
>
> THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
> IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
> FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE CONTRIBUTORS
> OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
> WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
> CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS WITH THE SOFTWARE.

Clause 3 is why nothing in this project claims Unidata's or UCAR's endorsement.

---

## CF — Climate and Forecast standard names

| | |
|---|---|
| **Used in** | `src/gendata/cf.bvnr`, the `cf:` profile |
| **Version** | CF standard name table v94, last modified 2026-06-09 |
| **Retrieved** | 2026-07-30, from <https://cfconventions.org/Data/cf-standard-names/94/src/cf-standard-name-table.xml> |
| **Home** | <https://cfconventions.org/> |
| **Extracted** | standard names, and the `canonical_units` each name states |

> Contains standard names from the CF standard name table, maintained on behalf
> of the CF community by the Centre for Environmental Data Analysis (CEDA).
>
> **Modified.** Standard names were extracted and each translated to the unit
> its `canonical_units` field states; names whose canonical units this project
> cannot represent were omitted. The profile is read-only — a unit is never
> written back as a standard name, because a standard name asserts what the
> quantity *is* and a unit does not know that.

**Status: no licence is stated by the publisher.** The CF *conventions document*
is dedicated to the public domain under CC0 1.0
(<https://github.com/cf-convention/cf-conventions>), but the standard name table
is maintained separately, in `cf-convention/vocabularies`, which carries no
`LICENSE` file and no in-band rights statement. Nothing suggests the CF
community intends the table to be restricted — it exists to be embedded in data
files worldwide — but there is no grant to point at.

**Open action:** ask the CF community to declare an explicit licence for the
standard name table, proposing CC0 1.0 to match the conventions document. Until
then this attribution stands in its place, and the `cf:` profile can be excluded
from any build with `-DBVNR_WITH_CF_PROFILE=OFF`.

---

## UN/ECE — Recommendations 20 and 21

| | |
|---|---|
| **Used in** | `src/gendata/unece.bvnr`, the `unece:` profile |
| **Version** | Recommendation No. 20, *Codes for Units of Measure Used in International Trade*, and Recommendation No. 21, *Codes for Passengers, Types of Cargo, Packages and Packaging Materials* |
| **Reached via** | QUDT 3.1.0's `qudt:uneceCommonCode` cross-reference — see below |
| **Home** | <https://unece.org/trade/uncefact/cl-recommendations> |
| **Extracted** | UN/ECE common codes |

> Contains UN/ECE common codes from UN/CEFACT Recommendations 20 and 21,
> published by the United Nations Economic Commission for Europe. Neither UNECE
> nor UN/CEFACT endorses this project or its use of the codes.

**No UNECE artefact was read.** Recommendation 20 states its conversion factors
in prose, so there is no machine-readable publication to resolve against; every
code in `unece.bvnr` was reached through QUDT's published `uneceCommonCode`
cross-reference, and is verified against it (`check_profile_factors.py`, class
`Unece`). The extraction is therefore from QUDT, under CC BY 4.0 as recorded
above; what is UNECE's here is the codes themselves.

**Status: formally unresolved, practically low risk.** The generic United
Nations website terms permit only personal, non-commercial download, without a
right to redistribute or to create derivative works. Those terms plainly are not
aimed at the code lists: Rec 20 exists to be implemented, and is implemented in
UN/EDIFACT, UBL, Peppol/EN 16931, ISO 20022, OPC UA and QUDT among many others.
The operative instrument is the UN/CEFACT Intellectual Property Rights Policy,
under which UN/CEFACT deliverables are made available for implementation
royalty-free.

**Open action:** obtain written confirmation of the reuse terms from
<cefact@unece.org>, and record it here.

---

## Other reference data

Two further external sources are used, neither of them a unit vocabulary and
neither raising the questions above:

* **ISO 4217** currency codes, numeric codes and minor-unit digits, in
  `src/gendata/currencies.bvnr`. The three-letter code list is published by the
  ISO 4217 Maintenance Agency (SIX Group) for free use, and codes and numeric
  identifiers are facts rather than expression. Cryptocurrency tickers are not
  ISO 4217 and are conventional.
* **CODATA 2022** for the one measured constant in `src/gendata/units.bvnr` —
  the atomic mass constant behind `Da`/`u`/`amu`. CODATA recommended values are
  published for unrestricted use; the edition is named in that file's header
  because a value without its edition cannot be checked. Every other factor in
  the registry is exact by definition or by convention.

---

## Keeping this file true

* Each `src/gendata/*.bvnr` profile file repeats its own provenance block in its
  header, so the version and licence travel with the data rather than only with
  this file.
* `check_profile_factors.py` pins the exact upstream URI it verifies against,
  including the CF table version — never a `current/` alias, which would make
  the provenance recorded here unreproducible.
* Bumping a vocabulary to a new upstream version means updating the version and
  retrieval date here and in the `.bvnr` header in the same commit.
