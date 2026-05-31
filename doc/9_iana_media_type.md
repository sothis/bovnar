# IANA Media Type Registration — `text/vnd.bovnar`

This is the registration application for the Bovnar media type, prepared per
**RFC 6838** (Media Type Specifications and Registration Procedures). `vnd.`
places it in the **vendor tree**, which requires only IANA Expert Review — no
RFC or IETF standards action.

## How to submit

1. Review the template below; confirm the contact e-mail and specification URLs.
2. Submit it using the IANA application form:
   <https://www.iana.org/form/media-types>
   (or e-mail the completed template to `media-types-requests@iana.org`).
3. Expert Review typically takes ~2 weeks. Address any reviewer feedback.
4. On approval it appears at
   <https://www.iana.org/assignments/media-types/media-types.xhtml> under `text`.

---

## Registration template

**Type name:** text

**Subtype name:** vnd.bovnar

**Required parameters:** N/A

**Optional parameters:**
`charset` — per RFC 6657. Bovnar documents are Unicode text encoded as UTF-8;
`UTF-8` is the only interoperable value and the assumed default. Other charsets
SHOULD NOT be used.

**Encoding considerations:**
8bit. A Bovnar document is UTF-8 text and may contain non-ASCII octets in string
values, identifiers, and unit symbols (e.g. `m/s²`, `µ`). Embedded binary data is
carried as *escaped octet streams* (`\xNN …`), so the byte stream remains text;
no transfer encoding is required for 8-bit-clean transports. Over 7-bit-only
transports, use quoted-printable or base64.

**Security considerations:**
A Bovnar document is passive data. It carries no scripting, macros, external
entity references, or instructions that a conforming parser executes; processing
a document cannot, by itself, cause code execution.

Implementations consuming untrusted documents should nonetheless note:

- *Resource consumption.* Deeply nested structures/arrays and very large octet
  streams or array dimensions can consume memory and CPU. Parsers SHOULD bound
  input size and nesting depth. The reference implementation exposes a
  configurable maximum document size and recovers from malformed input rather
  than aborting.
- *Internal references.* The `&.path` reference syntax names another value within
  the *same* document only; it does not trigger any network or filesystem access,
  so it is not an SSRF/XXE vector. Cyclic or dangling references are a validation
  error, not a fetch.
- *Numeric / unit values.* Values are validated against their declared type,
  bit-width, base, and physical unit. A mismatch is a parse error. Consumers
  should still range-check semantically significant quantities.
- *Binary payloads.* Octet streams are opaque bytes; a consumer that interprets
  or executes them is responsible for its own validation.

The reference implementation is built with standard hardening (bounds checks,
stack protection, FORTIFY) and is exercised by a fuzz suite.

**Interoperability considerations:**
The format is strongly and explicitly typed: each value's type family, bit-width,
numeric base, and physical unit travel inline with the value. Producers SHOULD
emit canonical type annotations; consumers MUST reject documents whose inline
values contradict their annotations (e.g. a unit mismatch). Units outside a
consumer's known set are a validation error rather than silently ignored. The
grammar is specified in EBNF (see published specification) to support
independent, interoperable implementations.

**Published specification:**
- Format specification: <https://github.com/sothis/bovnar/blob/main/doc/1_bovnar_spec.md>
- Grammar (EBNF): <https://github.com/sothis/bovnar/blob/main/doc/5_bovnar.ebnf>
- Unit system: <https://github.com/sothis/bovnar/blob/main/doc/2_bovnar_unit_system.md>
- Project / overview: <https://github.com/sothis/bovnar>

**Applications that use this media type:**
Configuration and data interchange for scientific, engineering, financial, and
industrial systems where values must carry their physical unit and exact numeric
type. Implementations include the Bovnar C99 reference library and command-line
tool, and the `bovnar` Python package on PyPI (<https://pypi.org/project/bovnar/>).
Editor support exists for VS Code, Sublime Text, Vim, and Geany.

**Fragment identifier considerations:** N/A. No fragment identifier syntax is
defined for this media type.

**Additional information:**

- *Deprecated alias names for this type:* `text/x-bovnar` (an unofficial,
  unregistered name used before this registration; consumers MAY accept it as an
  alias).
- *Magic number(s):* None. Bovnar is a text format with no fixed signature; a
  document typically begins with a `#` comment or a `.identifier` assignment.
  Identification relies on the `.bvnr` file extension and the media type label.
- *File extension(s):* `.bvnr`
- *Macintosh file type code(s):* `TEXT`
- *Object Identifiers:* N/A

**Person & email address to contact for further information:**
Janos Sonntag <janos.priv@gmail.com>
<!-- Confirm or replace with a preferred role address before submitting; this
     contact is published in the IANA registry. -->

**Intended usage:** COMMON

**Restrictions on usage:** None.

**Author:** Janos Sonntag

**Change controller:** Janos Sonntag (the Bovnar project maintainer).

**Provisional registration?** No (permanent).
