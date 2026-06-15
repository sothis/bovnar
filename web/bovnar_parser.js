/* bovnar_parser.js — JavaScript parser for the Bovnar format, used by the
 * web visualizers.
 *
 * Two entry points are exported:
 *
 *   parseBovnar(text, cb)  — the original lenient streaming lexer.  It is a
 *     loose approximation of the C event stream (it adds an ev_assignment_end
 *     delimiter, surfaces errors as ev_error events, and does NOT synthesise
 *     default type annotations).  Kept unchanged because the Mission-Control
 *     demo and other consumers build JS objects from it.
 *
 *   parseFaithful(text)    — reconstructs the reference C reader's on_verified
 *     event stream for the live-parser playground:
 *       · no ev_assignment_end (the C core emits none);
 *       · errors are NOT events — they surface through a separate on_error
 *         list carrying the error_code_t name + number + position, matching the
 *         C bvnr_on_error_fn callback (verified-layer errors are reported at the
 *         delimiter that terminates the value, exactly as the C core does);
 *       · the default type-annotation sequence the validator synthesises for
 *         unannotated number/string/bool values is reproduced before ev_data; and
 *       · it performs the type/value, integer-range, digit-in-base and unit
 *         checks the C validator performs (incl. synthesised defaults and array
 *         elements), and resyncs on errors like the C reader.
 *     Returns { events, errors, tree }.
 *
 *     This is a faithful port of the C *streaming* reader's checks: the digit
 *     alphabet (bvn_char_to_digit, incl. base-64/85), number-in-base parsing,
 *     arbitrary-precision integer range (BigInt, any width), the float_fix
 *     Q-format range, the per-family type-spec validity (width sets, q-only-on-
 *     float_fix, base/param rules → illegal_value_type), per-element array
 *     validation (each un-annotated element typed on its own; stop at the first
 *     bad element like the C resync), the exact type/dot/exponent/range check
 *     order, and the identifier/symbol/unit/string length caps. Validated
 *     against build/bovnar over large fuzz corpora: with ASCII text the first
 *     error's code AND position match the C core ~100% (a few deep multi-error
 *     resync cascades on malformed input still differ in their trailing errors).
 *     Unit checking uses the ported unit database (bovnar_units.js): unit_illegal
 *     for a unit string that does not parse (e.g. "kg" must be "k~g") and the
 *     structural unit_mismatch (m/s ≡ m·s⁻¹) exactly as bvn_unit_equal does.
 *
 *     One divergence remains, intrinsic: positions are character (UTF-16)
 *     columns — what the editor overlay needs — whereas the C core reports byte
 *     columns, so they differ on lines with multibyte content before the error.
 *     (DOM-level checks — duplicate key, array-element homogeneity, struct shape
 *     — are intentionally absent: the C *streaming* layer this mirrors does not
 *     emit them either; they live in the DOM builder.)
 *
 * Exported as window.BovnarParser = { parseBovnar, parseFaithful, EV }
 */
(function (global) {
  'use strict';

  /* The ported unit database (bovnar_units.js): parseUnit() / unitEqual(), used
     for unit_illegal (a unit string that does not parse) and unit_mismatch (an
     inline unit that differs structurally from the annotation unit). Loaded via
     a <script> tag in the browser, or require() under Node; null ⇒ unit checks
     are skipped (graceful degradation). */
  var BVNUNITS = (typeof window !== 'undefined' && window.BovnarUnits) ? window.BovnarUnits
    : (typeof require !== 'undefined'
        ? (function () { try { return require('./bovnar_units.js'); } catch (e) { return null; } })()
        : null);

  /* ── Event type constants ──────────────────────────────────────────── */
  const EV = {
    STREAM_START:        'ev_stream_start',
    STREAM_END:          'ev_stream_end',
    ASSIGNMENT_START:    'ev_assignment_start',
    ASSIGNMENT_END:      'ev_assignment_end',
    TYPE_ANN_START:      'ev_type_annotation_start',
    TYPE_ANN_FAMILY:     'ev_type_annotation_type_family',
    TYPE_ANN_PARAM:      'ev_type_annotation_type_family_parameter',
    TYPE_ANN_END:        'ev_type_annotation_end',
    STRUCT_START:        'ev_struct_start',
    STRUCT_END:          'ev_struct_end',
    ARRAY_ROW_START:     'ev_array_row_start',
    ARRAY_ROW_END:       'ev_array_row_end',
    ARRAY_DIM_START:     'ev_array_dim_start',
    DATA:                'ev_data',
    OCTET_STREAM_START:  'ev_octet_stream_start',
    OCTET_STREAM_END:    'ev_octet_stream_end',
    ERROR:               'ev_error',
  };

  /* ── Main parser function ──────────────────────────────────────────── */
  function parseBovnar(text, cb) {
    let pos  = 0;
    let line = 1;
    let col  = 1;
    let inlineUnitStart = null;   /* start position of the last inline unit read */
    let halted = false;           /* a hard (non-recoverable) error — e.g. an octet
                                     framing error — stops the parse, as the C core does */
    let valResync = false;        /* a recoverable error inside a value (e.g. a stray
                                     byte where an array separator/closer was expected):
                                     the C reader emits ONE error and resyncs to the
                                     enclosing statement ';', so we unwind without
                                     emitting further cascade errors */
    /* The C lexer caps struct and array nesting independently at 64 levels
       (bvn_val_impl.h max_struct_nesting/max_array_nesting); past that it emits
       error_struct_nesting_too_high / error_array_nesting_too_high. Without a
       counter the recursive descent below would throw an uncaught RangeError on
       deeply nested input (a pasteable string of '[' or '{'). */
    const MAX_NESTING = 64;
    let arrayDepth = 0, structDepth = 0;

    function emit(type, data, posOverride) {
      const p = posOverride || { line, col, offset: pos };
      cb({ type, data: data || {}, line: p.line, col: p.col, offset: p.offset });
    }

    /* Emit an on_error with an explicit error_code_e name (preferred over the
       message-string heuristic) and an optional explicit position. */
    function emitErr(code, posOverride, msg) {
      emit(EV.ERROR, { code, msg: msg || code }, posOverride);
    }

    function eof()      { return pos >= text.length; }
    function cur()      { return text[pos]; }
    function peek(n)    { return text[pos + (n || 1)]; }
    function here()     { return { line, col, offset: pos }; }
    /* The C reader reports an incomplete stream at the last byte it read (the
       NUL sentinel sits on the final column), not one column past it. */
    function eofPos()   { return { line, col: col - 1, offset: pos }; }

    function advance() {
      const ch = text[pos++];
      if (ch === '\n') { line++; col = 1; }
      /* The C reader advances the column to the next tab stop on a TAB (stops
         every 4 columns), and the playground editor renders tabs at tab-size:4,
         so the reported column matches both the C core and the on-screen caret. */
      else if (ch === '\t') { col += 4 - ((col - 1) % 4); }
      else { col++; }
      return ch;
    }

    /* Position of the delimiter that terminates the current value — the byte the
       C lexer reads to complete a token and hand it to the validator, which is
       where the validator reports type/range/unit errors. Skips trailing
       whitespace/comments without consuming them; flags EOF (an unterminated
       value, which the C core reports as incomplete_bvnr_stream instead). */
    function peekTermPos() {
      const sp = pos, sl = line, sc = col;
      skipWsComments();
      const p = { line, col, offset: pos, eof: eof(), delim: eof() ? '' : cur() };
      pos = sp; line = sl; col = sc;
      return p;
    }

    function skipWs() {
      while (!eof() && /[ \t\r\n\v\f]/.test(cur())) advance();
    }

    function skipComment() {
      while (!eof() && cur() !== '\n' && cur() !== '\r') advance();
    }

    function skipWsComments() {
      for (;;) {
        skipWs();
        if (!eof() && cur() === '#') { skipComment(); continue; }
        break;
      }
    }

    /* Read an identifier.
       id-start = A–Z | a–z | _ | codepoint ≥ 0xC0 (UTF-8 leader ≥ 0xC3)
       id-body  = id-start | + | - | digit | continuation bytes (> 0x7F) */
    function readIdent() {
      let s = '', tooLong = false;
      while (!eof()) {
        const c = cur();
        const code = c.charCodeAt(0);
        const ok = s.length === 0
          ? (/[A-Za-z_]/.test(c) || code >= 0xC0)
          : (/[A-Za-z0-9_+\-]/.test(c) || code > 0x7F);
        if (!ok) break;
        /* identifier length cap (max_identifier_length = 255) */
        if (!tooLong && s.length >= 255) { emitErr('error_identifier_too_long', here()); tooLong = true; }
        s += c; advance();
      }
      return s;
    }

    /* Parse a quoted string body (opening " already consumed). */
    function readStringBody() {
      let s = '', strTooLong = false;
      while (!eof()) {
        /* string length cap (max_string_length = 65535): the C reader errors
           only when it would push the 65536th CONTENT byte — a string of exactly
           65535 bytes is valid — so defer the check when the next byte is the
           closing quote (which is not stored). */
        if (!strTooLong && s.length >= 65535 && cur() !== '"') {
          emitErr('error_string_too_long', here()); strTooLong = true;
        }
        const c = advance();
        if (c === '"') return { closed: true, value: s };
        if (c === '\\') {
          if (eof()) { emitErr('error_got_incomplete_bvnr_stream', eofPos()); break; }
          const escPos = here();      /* the escape letter itself (\< here >) */
          const e = advance();
          const ESC = { t:'\t', n:'\n', v:'\v', f:'\f', r:'\r', '"':'"', '\\':'\\' };
          if (ESC[e] !== undefined) s += ESC[e];
          else { emitErr('error_illegal_escape_sequence', escPos); s += e; }
        } else {
          s += c;
        }
      }
      emitErr('error_got_incomplete_bvnr_stream', eofPos());
      return { closed: false, value: s };
    }

    /* Read a number literal (leading - already peeked). */
    function readNumber() {
      let s = '', mantDigits = 0;
      if (!eof() && cur() === '-') { s += '-'; advance(); }
      while (!eof() && /\d/.test(cur())) { s += cur(); advance(); mantDigits++; }
      if (!eof() && cur() === '.') {
        s += '.'; advance();
        while (!eof() && /\d/.test(cur())) { s += cur(); advance(); mantDigits++; }
      }
      /* a number must have at least one digit in its mantissa: `-`, `.`, `-.`,
         `.e5` etc. are malformed. The C lexer commits to number mode on the
         sign/dot/digit and then rejects the byte where a digit was required
         (at EOF the value is merely unterminated → incomplete, handled by the
         caller, so only flag the non-EOF case). */
      if (mantDigits === 0) {
        if (!eof()) emitErr('error_unexpected_input_byte', here());
        return s;
      }
      if (!eof() && (cur() === 'e' || cur() === 'E')) {
        s += cur(); advance();
        if (!eof() && (cur() === '+' || cur() === '-')) { s += cur(); advance(); }
        let expDigits = 0;
        while (!eof() && /\d/.test(cur())) { s += cur(); advance(); expDigits++; }
        /* an exponent with no digit (`1e`, `1e+`) is likewise malformed */
        if (expDigits === 0 && !eof()) emitErr('error_unexpected_input_byte', here());
      }
      return s;
    }

    /* Try to read an optional inline unit suffix after a scalar.
       Requires at least one whitespace before the unit.
       NOT allowed inside arrays. */
    function tryInlineUnit() {
      inlineUnitStart = null;
      const sp = pos, sl = line, sc = col;
      let hadWs = false;
      while (!eof() && /[ \t]/.test(cur())) { advance(); hadWs = true; }
      if (hadWs && !eof()) {
        const c = cur();
        const code = c.charCodeAt(0);
        /* Unit starts with a letter, _, $ (currency sigil), %, ( (a leading
           parenthesised group), °, µ, Ω, or a high byte */
        if (/[A-Za-z_$%(°µΩΩ]/.test(c) || code > 0x7F) {
          inlineUnitStart = { line, col, offset: pos };
          let unit = '', unitTooLong = false;
          while (!eof() && !/[ \t\r\n\v\f;,\[\]{}#]/.test(cur())) {
            /* inline-unit length cap (max unit length = 255) */
            if (!unitTooLong && unit.length >= 255) { emitErr('error_unit_too_long', here()); unitTooLong = true; }
            unit += cur(); advance();
          }
          return unit;
        }
      }
      pos = sp; line = sl; col = sc;
      return null;
    }

    /* The seven type families, as a DFA: the C lexer accepts only a prefix that
       can still complete one of these, rejecting the first deviating byte. */
    const FAMILIES = ['uint', 'sint', 'float', 'float_fix', 'float_dec', 'utf8', 'bool'];
    const isFamPrefix = (s) => FAMILIES.some((f) => f.indexOf(s) === 0);

    /* Parse a type annotation body (opening '<' already consumed). */
    function readTypeAnnotation() {
      emit(EV.TYPE_ANN_START, {});
      skipWsComments();

      /* ── family keyword (matched char-by-char against the family DFA) ── */
      let family = '', famBad = null;
      while (!eof() && /[a-z_0-9]/.test(cur())) {
        if (!isFamPrefix(family + cur())) { famBad = here(); break; }
        family += cur(); advance();
      }
      if (famBad || FAMILIES.indexOf(family) < 0) {
        /* empty / incomplete / unknown family — the C type-family DFA rejects
           the offending byte with unexpected_input_byte */
        emitErr('error_unexpected_input_byte', famBad || here());
        while (!eof() && cur() !== '>' && cur() !== ';') advance();
        if (!eof() && cur() === '>') advance();
        emit(EV.TYPE_ANN_END, { errored: true });
        return;
      }
      emit(EV.TYPE_ANN_FAMILY, { family });

      /* ── parameters: width | _base | qN | unit, comma-separated ── */
      let emptyParam = false, unitCount = 0, commaFirst = false, unitText = null;
      let hasWidth = false, width = 0, hasBase = false, base = 0, hasQ = false, q = 0;
      skipWsComments();
      if (!eof() && (cur() === ':' || cur() === ',')) {
        /* the first param separator must be ':'; a leading ',' (<uint,8>,
           <utf8,_16>) is an illegal type spec in the C core */
        if (cur() === ',') commaFirst = true;
        advance(); // skip ':' or ','
        for (;;) {
          skipWsComments();
          let raw = '';
          while (!eof() && cur() !== ',' && cur() !== '>' &&
                 cur() !== ';' && cur() !== '\n' && cur() !== '\r') {
            raw += cur(); advance();
          }
          raw = raw.trim();
          /* empty / trailing components (<uint:8,>, <uint:8,,>) are illegal */
          if (!raw) {
            emptyParam = true;
          } else if (/^\d+$/.test(raw)) {
            hasWidth = true; width = parseInt(raw, 10);
            emit(EV.TYPE_ANN_PARAM, { kind: 'width', value: width, text: raw });
          } else if (/^_\d+$/.test(raw)) {
            hasBase = true; base = parseInt(raw.slice(1), 10);
            emit(EV.TYPE_ANN_PARAM, { kind: 'base', value: base, text: raw });
          } else if (/^q\d+$/.test(raw)) {
            hasQ = true; q = parseInt(raw.slice(1), 10);
            emit(EV.TYPE_ANN_PARAM, { kind: 'q', value: q, text: raw });
          } else {
            emit(EV.TYPE_ANN_PARAM, { kind: 'unit', text: raw });
            unitCount++; unitText = raw;
          }
          skipWsComments();
          if (!eof() && cur() === ',') { advance(); continue; }
          break;
        }
      }
      /* type-spec validity (mirrors bvn_parse_type_annotation's type_ok, incl.
         the parameter-range checks in bovnar_utils.c):
         · utf8/bool take no width/base/q/unit
         · q is valid only on float_fix (and must be < effective width, ≤ 256)
         · base is valid only on uint/sint/float (NOT float_fix or float_dec) and
           the value must be 2..62, 64 or 85
         · uint/sint width ≤ BVN_MAX_INT_WIDTH (32768)
         · float (binary) base is 10 or 16 only; width is 0(default), 16, or a
           multiple of 32 up to BVN_FLOAT_MAX_PREC (32768)
         · float_fix/float_dec width is one of 16/32/64/128/256; float_fix q < width */
      let specOk = !commaFirst;
      if (!specOk) { /* leading ',' separator → illegal */ }
      else if (family === 'utf8' || family === 'bool')
        specOk = !hasWidth && !hasBase && !hasQ && unitCount === 0;
      else if (hasQ && family !== 'float_fix') specOk = false;
      else if (hasBase && (family === 'float_fix' || family === 'float_dec')) specOk = false;
      else if (hasBase && !((base >= 2 && base <= 62) || base === 64 || base === 85)) specOk = false;
      else if (family === 'uint' || family === 'sint') {
        if (hasWidth && width > 32768) specOk = false;
        /* bases 64/85 use '+'/'-' as digits (no sign char) so they are
           unsigned-only: a signed integer in base 64/85 is illegal. */
        else if (family === 'sint' && hasBase && (base === 64 || base === 85)) specOk = false;
      } else if (family === 'float') {
        if (hasBase && base !== 10 && base !== 16) specOk = false;
        else if (hasWidth && !((width === 16 || width % 32 === 0) && width <= 32768)) specOk = false;
      } else if (family === 'float_fix' || family === 'float_dec') {
        const wEff = (hasWidth && width > 0) ? width : 64;
        if (hasWidth && width !== 0 && [16, 32, 64, 128, 256].indexOf(width) < 0) specOk = false;
        else if (hasQ && (q >= wEff || q > 256)) specOk = false;
      }

      let annErr = false;
      skipWsComments();
      if (!eof() && cur() === '>') {
        /* type-level (validator) errors are reported at the closing '>' */
        const gt = here();
        /* the annotation unit must parse to a known unit; >1 unit is also illegal */
        const unitBad = unitCount > 1 ||
          (unitText != null && BVNUNITS && !BVNUNITS.parseUnit(unitText).ok);
        if (emptyParam || !specOk) { emitErr('error_illegal_value_type', gt); annErr = true; }
        else if (unitBad) { emitErr('error_unit_illegal', gt); annErr = true; }
        advance(); // skip '>'
      } else if (eof()) {
        emitErr('error_got_incomplete_bvnr_stream', eofPos()); annErr = true;
      } else {
        emitErr('error_unexpected_input_byte', here()); annErr = true;
      }
      /* a bad annotation aborts value validation in the C core — flag it so the
         verified layer does not also report a type/range/unit error */
      emit(EV.TYPE_ANN_END, { errored: annErr });
    }

    /* Parse one value (type annotation + actual token).
       insideArray = true disables inline unit suffix. */
    function readValue(insideArray) {
      skipWsComments();

      /* Optional type annotation */
      if (!eof() && cur() === '<') {
        advance(); // skip '<'
        readTypeAnnotation();
        skipWsComments();
      }

      /* Octet stream — a NUL byte where a value is expected switches the
         text layer into binary chunk mode (§11). */
      if (!eof() && text.charCodeAt(pos) === 0) {
        readOctetStream();
        return;
      }

      /* Null value — nothing between annotation/= and ;/,/] */
      if (eof() ||
          cur() === ';' ||
          (insideArray && (cur() === ',' || cur() === ']')) ||
          cur() === '}') {
        emit(EV.DATA, { kind: 'null', value: null, text: '', endPos: here() });
        return;
      }

      /* Struct */
      if (cur() === '{') {
        if (structDepth >= MAX_NESTING) {
          emitErr('error_struct_nesting_too_high', here());
          halted = true;
          return;
        }
        advance();
        emit(EV.STRUCT_START, {});
        structDepth++;
        skipWsComments();
        while (!eof() && cur() !== '}') {
          if (!parseAssignment(true)) break;
          if (halted) break;
          skipWsComments();
        }
        structDepth--;
        if (!halted && !eof() && cur() === '}') advance();
        emit(EV.STRUCT_END, {});
        return;
      }

      /* Array */
      if (cur() === '[') {
        if (arrayDepth >= MAX_NESTING) {
          emitErr('error_array_nesting_too_high', here());
          halted = true;
          return;
        }
        arrayDepth++;
        readArray();
        arrayDepth--;
        return;
      }

      /* String (with optional concatenation) */
      if (cur() === '"') {
        const st = { line, col, offset: pos };
        advance();
        let fullVal = '';
        for (;;) {
          const r = readStringBody();
          fullVal += r.value;
          /* look for concatenation */
          const sp = pos, sl = line, sc = col;
          skipWsComments();
          if (!eof() && cur() === '"') { advance(); continue; }
          pos = sp; line = sl; col = sc;
          break;
        }
        const unit = insideArray ? null : tryInlineUnit();
        emit(EV.DATA, { kind: 'string', value: fullVal, text: fullVal, unit,
          start: st, unitStart: unit ? inlineUnitStart : null, endPos: peekTermPos() });
        return;
      }

      /* Reference — '&' must be followed by one or more '.segment' path parts. */
      if (cur() === '&') {
        const st = { line, col, offset: pos };
        advance();
        if (eof() || cur() !== '.') {
          /* '&' not followed by a path: the next byte (or EOF) is rejected */
          if (eof()) emitErr('error_got_incomplete_bvnr_stream', eofPos());
          else emitErr('error_unexpected_input_byte', here());
        }
        let ref = '', refTooLong = false;
        while (!eof() && !/[ \t\r\n\v\f;,\[\]{}#]/.test(cur())) {
          /* reference path cap (max_reference_length = 65535): the C reader
             errors when it would push the 65536th path byte */
          if (!refTooLong && ref.length >= 65535) { emitErr('error_reference_too_long', here()); refTooLong = true; }
          ref += cur(); advance();
        }
        emit(EV.DATA, { kind: 'reference', value: ref, text: '&' + ref, start: st, endPos: peekTermPos() });
        return;
      }

      /* Number (integer-led, dot-led, or negative). The C lexer commits to
         number mode on a leading '-' or '.' even when no digit follows (it then
         reports the malformed token), so a bare '.' enters here too. */
      if (cur() === '-' || cur() === '.' || /\d/.test(cur())) {
        const st = { line, col, offset: pos };
        const numStr = readNumber();
        const unit   = insideArray ? null : tryInlineUnit();
        emit(EV.DATA, { kind: 'number', value: numStr, text: numStr, unit,
          start: st, unitStart: unit ? inlineUnitStart : null, endPos: peekTermPos() });
        return;
      }

      /* Symbol (bare word) — or a reserved keyword (null / true / false /
         on / off), which the validator reclassifies out of the symbol space. */
      if (/[A-Za-z_]/.test(cur()) || cur().charCodeAt(0) > 0x7F) {
        const st = { line, col, offset: pos };
        let sym = '';
        let flagged = false, symTooLong = false;
        while (!eof() && !/[ \t\r\n\v\f;,\[\]{}#<>=]/.test(cur())) {
          const c = cur();
          /* symbol length cap (max_symbol_length = 255) */
          if (!symTooLong && sym.length >= 255) { emitErr('error_symbol_too_long', here()); symTooLong = true; }
          /* Spec symbol/identifier body chars are A-Za-z0-9_+- plus non-ASCII
             (codepoint > U+007F); the C core lexer rejects anything else
             (e.g. . : / * @ % !) with unexpected_input_byte. Flag the first
             offending char at its own position, but keep consuming the run so
             the rest of the line still parses (error recovery). */
          if (!flagged && !/[A-Za-z0-9_+\-]/.test(c) && c.charCodeAt(0) <= 0x7F) {
            emitErr('error_unexpected_input_byte', here());
            flagged = true;
          }
          sym += c; advance();
        }
        const ep = peekTermPos();
        if (sym === 'null') {
          emit(EV.DATA, { kind: 'null', value: null, text: sym, start: st, endPos: ep });
        } else if (sym === 'true' || sym === 'on') {
          emit(EV.DATA, { kind: 'bool', value: true, text: 'true', start: st, endPos: ep });
        } else if (sym === 'false' || sym === 'off') {
          emit(EV.DATA, { kind: 'bool', value: false, text: 'false', start: st, endPos: ep });
        } else if (sym === 'nan' || sym === 'inf' || sym === 'ninf') {
          /* Special floats are bare reserved keywords (no sigil, no inline
             unit), reclassified out of the symbol space like null/bool. */
          emit(EV.DATA, { kind: 'special', value: sym, text: sym, start: st, endPos: ep });
        } else {
          emit(EV.DATA, { kind: 'symbol', value: sym, text: sym, start: st, endPos: ep });
        }
        return;
      }

      /* an unrecognised byte where a value was expected (a leading '+', a stray
         punctuation): the C reader reports it once and resyncs to the enclosing
         statement ';' rather than consuming the single byte and then flagging a
         missing terminator on the remainder */
      emitErr('error_unexpected_input_byte', here());
      valResync = true;
    }

    /* Parse an octet stream (the entering NUL byte is cur, not consumed).
       Binary protocol (§11):
         tag 0x01 → chunk: 2-byte LE length (0x0000 encodes 65536) + data
         tag 0x00 → end of stream
         any other tag → out of sync (hard error) */
    function readOctetStream() {
      /* binary framing errors are non-recoverable in the C reader and are all
         reported at the octet stream's entry position (the NUL), then the parse
         halts — captured here before consuming the NUL. */
      const startPos = here();
      const fail = (code) => { emitErr(code, startPos); halted = true; };
      advance(); // consume the NUL that enters binary mode
      emit(EV.OCTET_STREAM_START, {});
      for (;;) {
        /* EOF anywhere in the binary framing is a failed byte read in the C
           reader (bvn_os_read_exact), reported as read_complete_chunk_failed */
        if (eof()) { fail('error_read_complete_chunk_failed'); return; }
        const tag = text.charCodeAt(pos);
        advance();
        if (tag === 0x00) { emit(EV.OCTET_STREAM_END, {}); return; }
        if (tag !== 0x01) { fail('error_octet_stream_out_of_sync'); return; }
        /* 16-bit little-endian chunk length; 0x0000 → 65536 */
        if (pos + 1 >= text.length) { fail('error_read_complete_chunk_failed'); return; }
        const lo = text.charCodeAt(pos); advance();
        const hi = text.charCodeAt(pos); advance();
        let len = lo | (hi << 8);
        if (len === 0) len = 65536;

        let data = '';
        let i = 0;
        for (; i < len && !eof(); i++) { data += advance(); }
        if (i < len) { fail('error_read_complete_chunk_failed'); return; }
        emit(EV.DATA, { kind: 'octet', value: data, text: data, length: len });
      }
    }

    /* Parse one bracket row '[...]' (opening '[' not yet consumed) and return
       its element-position count. A row always has at least one position (an
       empty '[]' is one null element, '[,]' is two), so the count equals
       commas + 1 — matching the C lexer's curr_row_size. */
    function readArrayRow(expected) {
      advance(); // skip '['
      emit(EV.ARRAY_ROW_START, {});
      skipWsComments();

      let count = 1; // every row holds at least one (possibly null) position
      let flagged = false;
      if (!eof() && cur() !== ']') {
        /* First element (may be null if cur is ',' or ']') */
        readArrayElement();

        /* Remaining elements separated by ',' */
        for (;;) {
          skipWsComments();
          if (valResync || halted || eof() || cur() !== ',') break;
          /* over-count: the C reader flags the mismatch at the ',' that pushes
             this row past the width established by the first row */
          if (expected >= 0 && !flagged && count + 1 > expected) {
            emitErr('error_array_row_size_mismatch', here()); flagged = true;
          }
          advance(); // consume ','
          readArrayElement();
          count++;
        }
      }

      skipWsComments();
      if (valResync || halted) {
        /* an inner value already errored (or a hard nesting-limit halt) and we
           are unwinding — emit no further errors, just close the row */
      } else if (!eof() && cur() === ']') {
        /* under-count: flagged at the ']' that closes a too-narrow row */
        if (expected >= 0 && !flagged && count < expected) {
          emitErr('error_array_row_size_mismatch', here()); flagged = true;
        }
        advance(); // skip ']'
      } else if (eof()) {
        emitErr('error_got_incomplete_bvnr_stream', eofPos());
      } else {
        /* a stray byte where ',' or ']' was expected: report it once, then
           unwind to the enclosing statement ';' (the C reader's recovery) */
        emitErr('error_unexpected_input_byte', here());
        valResync = true;
      }
      emit(EV.ARRAY_ROW_END, {});
      return count;
    }

    /* Parse one array (opening '[' not yet consumed).

       Row-size consistency is per-array and scoped to '/'-dimension rows only:
       all '/'-separated rows of *this* array must have the same element count
       (error_array_row_size_mismatch in the C reader). Comma-separated elements
       — including nested arrays parsed by their own readArray invocation — are
       independent, so a sibling array's width never constrains this one. */
    function readArray() {
      let firstCount = -1;
      for (;;) {
        /* the row reports its own size mismatch (at the precise byte) against
           the width established by the first row */
        const count = readArrayRow(firstCount);
        if (firstCount < 0) firstCount = count;
        if (valResync || halted) break;   /* unwinding to the statement ';' */

        /* Additional dimension rows separated by '/'.
           Tentatively consume '/' then skip ws+comments; backtrack fully
           if the next non-whitespace token is not '['. */
        const sp = pos, sl = line, sc = col;
        skipWsComments();
        if (!eof() && cur() === '/') {
          advance(); // tentatively skip '/'
          skipWsComments();
          if (!eof() && cur() === '[') {
            emit(EV.ARRAY_DIM_START, {});
            continue; // next dimension row of the SAME array
          }
          pos = sp; line = sl; col = sc; // not a dimension separator
          break;
        }
        pos = sp; line = sl; col = sc;
        break;
      }
    }

    /* Parse one array element (including null). */
    function readArrayElement() {
      skipWsComments();
      if (eof() || cur() === ',' || cur() === ']') {
        /* Null element — emit without consuming the delimiter */
        emit(EV.DATA, { kind: 'null', value: null, text: '' });
        return;
      }
      readValue(true);
    }

    /* Discard input up to and including the next ';' at depth 0 — the C reader's
       resync after an error, which drops the rest of the broken assignment (and
       does not validate it) before restarting at the following statement. */
    function resyncToSemicolon(inStruct) {
      let depth = 0;
      while (!eof()) {
        const c = cur();
        if (c === '[' || c === '{') depth++;
        else if (c === ']' || c === '}') {
          if (depth === 0) {
            /* Leave '}' unconsumed inside a struct so the struct handler's
               closing-brace check sees it; consume ']' to avoid an infinite loop. */
            if (c !== '}' || !inStruct) advance();
            return;
          }
          depth--;
        }
        else if (c === ';' && depth === 0) { advance(); return; }
        advance();
      }
      /* the resync ran off the end without finding a ';' — the stream ended
         mid-assignment (got_incomplete_bvnr_stream in the C reader) */
      emitErr('error_got_incomplete_bvnr_stream', eofPos());
    }

    /* Resync after a value-level error (valResync). Unlike resyncToSemicolon
       this is invoked while still nested *inside* one or more arrays/structs
       the value belonged to, so closers of those ENCLOSING containers (']' / '}'
       at non-positive relative depth) are skipped over rather than treated as a
       stop. It halts at the statement ';' (consumed) or, inside a struct, at the
       container '}' (left for the struct handler) — matching the C reader, which
       reports one error then unwinds to the enclosing statement boundary. */
    function resyncValue(inStruct) {
      let depth = 0;
      while (!eof()) {
        const c = cur();
        if (c === '[' || c === '{') depth++;
        else if (c === ']') { if (depth > 0) depth--; /* else enclosing ] — skip */ }
        else if (c === '}') {
          if (depth > 0) depth--;
          else { if (inStruct) return; advance(); return; }   /* enclosing struct close */
        } else if (c === ';' && depth === 0) { advance(); return; }
        advance();
      }
      emitErr('error_got_incomplete_bvnr_stream', eofPos());
    }

    /* Parse one full assignment (.key = value ;).
       Returns false when nothing more to parse at this level. */
    function parseAssignment(inStruct) {
      skipWsComments();
      if (eof()) return false;
      if (inStruct && cur() === '}') return false;

      if (cur() !== '.') {
        emitErr('error_unexpected_input_byte', here());
        resyncToSemicolon(inStruct);
        return true;
      }
      advance(); // skip '.'

      const key = readIdent();
      if (!key) {
        /* '.' not followed by an identifier byte: the C identifier-intro state
           rejects the offending byte (or reports incomplete at EOF) */
        if (eof()) emitErr('error_got_incomplete_bvnr_stream', eofPos());
        else emitErr('error_unexpected_input_byte', here());
      }

      emit(EV.ASSIGNMENT_START, { key });

      skipWsComments();
      if (!eof() && cur() === '=') advance();
      else {
        /* a missing '=' after the key (an illegal byte in/after the key, or a
           bare value): the C reader reports the offending byte once (or
           incomplete at EOF) and resyncs to the next ';', discarding this broken
           assignment — it does NOT then also read a value and flag a missing ';' */
        if (eof()) emitErr('error_got_incomplete_bvnr_stream', eofPos());
        else { emitErr('error_unexpected_input_byte', here()); resyncToSemicolon(inStruct); }
        emit(EV.ASSIGNMENT_END, { key });
        return true;
      }

      readValue(false);

      /* a hard error inside the value (octet framing) already reported itself and
         stops the parse — don't also flag the missing terminator */
      if (halted) { emit(EV.ASSIGNMENT_END, { key }); return true; }

      /* a recoverable value-level error (a stray byte inside an array) already
         reported one error — unwind to the enclosing statement ';' without
         emitting the missing-';' cascade the loose resync would produce */
      if (valResync) {
        emit(EV.ASSIGNMENT_END, { key });
        resyncValue(inStruct);
        valResync = false;
        return true;
      }

      skipWsComments();
      if (!eof() && cur() === ';') advance();
      else if (eof()) emitErr('error_got_incomplete_bvnr_stream', eofPos());
      else {
        /* missing ';' — the C reader reports the offending byte then resyncs,
           discarding the remainder of this (broken) assignment */
        emitErr('error_unexpected_input_byte', here());
        emit(EV.ASSIGNMENT_END, { key });
        resyncToSemicolon(inStruct);
        return true;
      }

      emit(EV.ASSIGNMENT_END, { key });
      return true;
    }

    /* ── Top-level parse loop ──────────────────────────────────────── */
    emit(EV.STREAM_START, {});

    /* UTF-8 BOM: legal only as the very first bytes of the stream.
       Accept both the decoded form (U+FEFF) and the raw three-byte
       sequence 0xEF 0xBB 0xBF when the input was loaded as a byte string. */
    if (text.charCodeAt(0) === 0xFEFF) {
      advance();
    } else if (text.charCodeAt(0) === 0xEF &&
               text.charCodeAt(1) === 0xBB &&
               text.charCodeAt(2) === 0xBF) {
      advance(); advance(); advance();
    }

    while (!eof()) {
      skipWsComments();
      if (eof()) break;
      parseAssignment(false);
      if (halted) break;          /* hard error → stop, like the C reader */
    }
    emit(EV.STREAM_END, {});
  }

  /* ══════════════════════════════════════════════════════════════════════
     Faithful C-event reconstruction (parseFaithful)
     ══════════════════════════════════════════════════════════════════════ */

  /* value_type_family_e → enum spelling used in the C public header */
  const FAMILY_ENUM = {
    uint: 'vt_uint', sint: 'vt_sint', float: 'vt_float',
    float_fix: 'vt_float_fix', float_dec: 'vt_float_dec',
    utf8: 'vt_utf8', bool: 'vt_bool', plain: 'vt_plain',
  };
  /* error_code_e names → numeric code (subset the JS layer can detect) */
  const ERR_CODE = {
    error_unknown_token_type: 1, error_array_row_size_mismatch: 2,
    error_struct_nesting_too_high: 5, error_array_nesting_too_high: 6,
    error_identifier_too_long: 3, error_empty_identifier: 4, error_string_too_long: 8,
    error_illegal_escape_sequence: 9, error_number_too_long: 10,
    error_symbol_too_long: 11, error_reference_too_long: 12,
    error_read_complete_chunk_failed: 13, error_octet_stream_out_of_sync: 14,
    error_unexpected_input_byte: 15, error_got_incomplete_bvnr_stream: 18,
    error_type_too_long: 21, error_unit_too_long: 22,
    error_illegal_value_type: 25, error_unit_illegal: 32,
    error_type_value_mismatch: 34, error_value_out_of_range: 35,
    error_digit_not_in_base: 36, error_unit_mismatch: 38,
  };
  const NUMERIC_FAMILIES = ['uint', 'sint', 'float', 'float_fix', 'float_dec'];

  function mapErrorCode(msg) {
    const m = (msg || '').toLowerCase();
    if (m.includes('escape at eof') || m.includes('unterminated string'))
      return 'error_got_incomplete_bvnr_stream';
    if (m.includes('escape')) return 'error_illegal_escape_sequence';
    if (m.includes('unclosed type') || m.includes('incomplete'))
      return 'error_got_incomplete_bvnr_stream';
    if (m.includes('empty type') || m.includes('type parameter'))
      return 'error_illegal_value_type';
    if (m.includes('empty identifier')) return 'error_empty_identifier';
    if (m.includes('octet') && m.includes('sync'))
      return 'error_octet_stream_out_of_sync';
    if (m.includes('octet')) return 'error_read_complete_chunk_failed';
    if (m.includes('row_size') || m.includes('row size'))
      return 'error_array_row_size_mismatch';
    return 'error_unexpected_input_byte';
  }

  function defaultWidth(fam) {
    return NUMERIC_FAMILIES.indexOf(fam) >= 0 ? 64 : null;
  }
  function normUnit(u) { return (u || '').trim(); }
  /* Port of bvn_char_to_digit (src/utils/bovnar_utils.c) — the exact per-base
     digit alphabet, including base-64 (A-Za-z0-9+/) and base-85 (Ascii85 !..u),
     and the >36 rule where uppercase continues after lowercase. Returns >= base
     for a non-digit. */
  function charToDigit(c, base) {
    const k = c.charCodeAt(0);
    const A = 65, Z = 90, a = 97, z = 122, d0 = 48, d9 = 57;
    if (base === 64) {
      if (k >= A && k <= Z) return k - A;
      if (k >= a && k <= z) return k - a + 26;
      if (k >= d0 && k <= d9) return k - d0 + 52;
      if (c === '+') return 62;
      if (c === '/') return 63;
      return base;
    }
    if (base === 85) return (k >= 33 && k <= 117) ? k - 33 : base;  /* '!'..'u' */
    let d;
    if (k >= d0 && k <= d9) d = k - d0;
    else if (k >= a && k <= z) d = 10 + (k - a);
    else if (k >= A && k <= Z) d = base > 36 ? 36 + (k - A) : 10 + (k - A);
    else return base;
    return d < base ? d : base;
  }
  /* Port of bvn_validate_number_in_base — a full numeric literal (sign, mantissa
     with optional dot, optional base-aware exponent) is parseable in the base. */
  function validateNumberInBase(s, base) {
    if (!s) return false;
    /* Bases 64/85 are unsigned fixed-alphabet integer bases (standard Base64,
       Ascii85) in which '+','-','.' are ordinary digits, not sign/dot/exponent
       markers. Validate them as a pure unsigned digit string, matching the C
       reference (bvn_validate_number_in_base delegates to digits-for-base). */
    if (base === 64 || base === 85) return digitsValidForBase(s, base);
    let i = 0;
    if (s[0] === '-' || s[0] === '+') i = 1;
    if (i >= s.length) return false;
    let hasDot = false, hasExp = false, hasMant = false, hasExpDigit = false;
    for (; i < s.length; i++) {
      const c = s[i];
      if (c === '.') { if (hasDot || hasExp) return false; hasDot = true; continue; }
      if (((c === 'e' || c === 'E') && base <= 14) ||
          ((c === 'p' || c === 'P') && (base & (base - 1)) === 0 && base <= 16)) {
        if (hasExp || !hasMant) return false;
        hasExp = true;
        if (s[i + 1] === '+' || s[i + 1] === '-') i++;
        continue;
      }
      if (c === '+' || c === '-') return false;
      if (hasExp) { if (charToDigit(c, 10) >= 10) return false; hasExpDigit = true; }
      else { if (charToDigit(c, base) >= base) return false; hasMant = true; }
    }
    return hasMant && (!hasExp || hasExpDigit);
  }
  function digitsValidForBase(t, base) {
    /* In bases 64/85 '+'/'-' are digits, not a leading sign (uint-only). */
    const signed = base !== 64 && base !== 85;
    const s = (signed && (t[0] === '-' || t[0] === '+')) ? t.slice(1) : t;
    if (!s.length) return false;
    for (const ch of s) if (charToDigit(ch, base) >= base) return false;
    return true;
  }
  function toBigInt(t, base) {
    /* Bases 64/85 are unsigned; their '+'/'-' are digits, not a sign. */
    const signed = base !== 64 && base !== 85;
    const neg = signed && t[0] === '-';
    const s = (signed && (neg || t[0] === '+')) ? t.slice(1) : t;
    let v = 0n; const B = BigInt(base);
    for (const ch of s) v = v * B + BigInt(charToDigit(ch, base));
    return neg ? -v : v;
  }
  /* Does a decimal value fit a signed Q-format fixed-point (total_bits W,
     frac_bits q)? The raw is a signed W-bit integer scaled by 2^-q, so the
     value rounds into [-2^(W-1), 2^(W-1)-1] raw counts. Approximated with JS
     doubles — exact for the small widths float_fix uses. */
  function floatFitsFix(text, W, q) {
    const v = parseFloat(text);
    if (!isFinite(v)) return true;            /* nan/inf are range-exempt */
    const raw = Math.round(v * Math.pow(2, q));
    return raw >= -Math.pow(2, W - 1) && raw <= Math.pow(2, W - 1) - 1;
  }
  function intInRange(v, fam, W) {
    if (W < 1) return true;   /* width 0 / unspecified is unconstrained (matches the C core) */
    if (fam === 'uint') { if (v < 0n) return false; return v <= (2n ** BigInt(W)) - 1n; }
    const lim = 2n ** BigInt(W - 1);
    return v >= -lim && v <= lim - 1n;
  }
  function scalarTok(v) {
    switch (v.type === 'scalar' ? v.kind : v.type) {
      case 'string':  return 'token_is_string';
      case 'bool':    return 'token_is_bool';
      case 'number':
      case 'special': return 'token_is_number';
      case 'reference': return 'token_is_reference';
      case 'symbol':  return 'token_is_symbol';
      default:        return 'token_is_null_value';
    }
  }
  function arrTok(v) {
    if (v.kind === 'string') return 'token_is_array_string';
    if (v.kind === 'number' || v.kind === 'special') return 'token_is_array_number';
    return 'token_is_null_value';
  }

  function parseFaithful(text) {
    const raw = [];
    parseBovnar(text, (e) => raw.push(e));

    /* errors are pulled out of the stream; the structural skeleton (which
       always carries matching *_end events, even after recovery) remains */
    const errors = [];
    const clean = [];
    for (const e of raw) {
      if (e.type === EV.ERROR) {
        const code = e.data.code || mapErrorCode(e.data.msg);
        errors.push({ code, codeNum: ERR_CODE[code] || 0,
          message: e.data.msg || code, line: e.line, col: e.col, offset: e.offset });
      } else clean.push(e);
    }

    /* ── reshape the lenient stream into a tree ── */
    let i = 0;
    const at = (t) => clean[i] && clean[i].type === t;
    const eat = () => clean[i++];

    function parseAnnotation() {
      if (!at(EV.TYPE_ANN_START)) return null;
      eat();
      const ann = { synthesized: false, familyName: '', family: 'vt_illegal', params: [] };
      if (at(EV.TYPE_ANN_FAMILY)) {
        const f = eat();
        ann.familyName = f.data.family;
        ann.family = FAMILY_ENUM[f.data.family] || 'vt_illegal';
      }
      while (at(EV.TYPE_ANN_PARAM)) {
        const p = eat();
        ann.params.push({ kind: p.data.kind, text: p.data.text, value: p.data.value });
      }
      if (at(EV.TYPE_ANN_END)) { ann.errored = !!eat().data.errored; }
      return ann;
    }
    function scalarNode(dat, ev) {
      const start = dat.start || { line: ev.line, col: ev.col };
      const unitStart = dat.unitStart || null;
      const endPos = dat.endPos || null;
      if (dat.kind === 'null')
        return { type: 'null', text: '', line: ev.line, col: ev.col, start: start, unitStart: unitStart, endPos: endPos };
      if (dat.kind === 'reference')
        return { type: 'reference', kind: 'reference', text: dat.text,
          line: ev.line, col: ev.col, start: start, unitStart: unitStart, endPos: endPos };
      return { type: 'scalar', kind: dat.kind,
        text: dat.text != null ? dat.text : String(dat.value),
        value: dat.value, unit: dat.unit || null,
        line: ev.line, col: ev.col, start: start, unitStart: unitStart, endPos: endPos };
    }
    function parseArray() {
      const node = { type: 'array', dims: 1, rows: [] };
      for (;;) {
        if (!at(EV.ARRAY_ROW_START)) break;
        eat();
        const row = { elements: [] };
        for (;;) {
          /* an element may carry its OWN inline type annotation (e.g.
             `[<uint:8> 5, <utf8> "x"]`); the C reader applies it to that one
             element only (it does not carry to the next). Read it first, then
             the value it annotates. */
          const elAnn = at(EV.TYPE_ANN_START) ? parseAnnotation() : null;
          /* an element may itself be a nested array, a struct, an octet stream
             or a scalar — parseValue handles each; only ARRAY_ROW_END /
             ARRAY_DIM_START (or anything else) ends the row */
          if (at(EV.ARRAY_ROW_START) || at(EV.STRUCT_START) ||
              at(EV.OCTET_STREAM_START) || at(EV.DATA)) {
            const el = parseValue();
            if (elAnn) el.ann = elAnn;
            row.elements.push(el);
          } else if (elAnn) {
            /* an annotation with no following value (e.g. `[<uint:8>]`) — the C
               reader still validates the dangling annotation; carry it on a null */
            row.elements.push({ type: 'null', text: '', ann: elAnn });
            break;
          } else break;
        }
        if (at(EV.ARRAY_ROW_END)) eat();
        node.rows.push(row);
        if (at(EV.ARRAY_DIM_START)) { eat(); node.dims++; continue; }
        break;
      }
      return node;
    }
    function parseValue() {
      if (at(EV.STRUCT_START)) {
        eat();
        const node = { type: 'struct', children: [] };
        while (at(EV.ASSIGNMENT_START)) node.children.push(parseAssignment());
        if (at(EV.STRUCT_END)) eat();
        return node;
      }
      if (at(EV.ARRAY_ROW_START)) return parseArray();
      if (at(EV.OCTET_STREAM_START)) {
        eat();
        const node = { type: 'octet', chunks: [], total: 0 };
        while (at(EV.DATA)) {
          const d = eat();
          const len = d.data.length || (d.data.text ? d.data.text.length : 0);
          node.chunks.push({ length: len }); node.total += len;
        }
        if (at(EV.OCTET_STREAM_END)) eat();
        return node;
      }
      if (at(EV.DATA)) { const d = eat(); return scalarNode(d.data, d); }
      return { type: 'null', text: '' };
    }
    function parseAssignment() {
      const a = eat(); /* ASSIGNMENT_START */
      const node = { type: 'assignment', key: a.data.key, line: a.line, col: a.col,
        ann: null, value: null };
      node.ann = parseAnnotation();
      node.value = parseValue();
      /* the lenient stream closes the scope with ev_assignment_end — skip it */
      if (at(EV.ASSIGNMENT_END)) eat();
      return node;
    }

    const tree = { type: 'stream', children: [] };
    if (at(EV.STREAM_START)) eat();
    while (at(EV.ASSIGNMENT_START)) tree.children.push(parseAssignment());

    /* ── synthesise default annotations, resolve types, validate ── */
    function synthScalarAnn(v) {
      if (v.kind === 'string')
        return { synthesized: true, familyName: 'utf8', family: 'vt_utf8', params: [] };
      if (v.kind === 'bool')
        return { synthesized: true, familyName: 'bool', family: 'vt_bool', params: [] };
      let fam = 'uint';
      if (v.kind === 'special') fam = 'float';
      else {
        const t = v.text || '';
        const isNeg = t[0] === '-';
        const dot = t.indexOf('.') >= 0;
        const exp = /[eE]/.test(t);
        fam = (dot || exp) ? 'float' : (isNeg ? 'sint' : 'uint');
      }
      return { synthesized: true, familyName: fam, family: FAMILY_ENUM[fam],
        params: [
          { kind: 'width', value: 64, text: '64' },
          { kind: 'base', value: 10, text: '_10' },
          { kind: 'unit', text: v.unit || 'no_unit' },
        ] };
    }
    function firstScalar(arr) {
      for (const row of arr.rows)
        for (const el of row.elements) {
          if (el.type === 'array') { const f = firstScalar(el); if (f) return f; }
          else if (el.type === 'scalar') return el;
        }
      return null;
    }
    function err(code, pos) {
      errors.push({ code, codeNum: ERR_CODE[code] || 0, message: code,
        line: (pos && pos.line) || 0, col: (pos && pos.col) || 0,
        offset: (pos && pos.offset) || 0 });
    }
    /* Faithful port of the C validator's per-value checks (bvn_acc_parse_number
       + bvn_validate_type_value_compat + bvn_check_acc_range), in the same order
       so the *first* failing check — and thus the reported error_code — matches. */
    function validateExplicit(ann, v) {
      const fam = ann.familyName;
      /* every verified-layer error is reported at the delimiter that terminated
         the value (the ; , ] } the lexer read to complete the token) = endPos */
      const at = v.endPos || v.start || v;
      /* a value the lexer never completed (EOF, or a missing-';' byte after it)
         is a lexer error, not a verified-layer one */
      if (v.endPos && (v.endPos.eof || ';,]}'.indexOf(v.endPos.delim) < 0)) return;
      if (!FAMILY_ENUM[fam] || fam === 'plain') return;  /* bad family: lexer already reported it */

      const numeric = NUMERIC_FAMILIES.indexOf(fam) >= 0;
      const isFloat = fam === 'float' || fam === 'float_fix' || fam === 'float_dec';
      const wp = ann.params.find((p) => p.kind === 'width');
      const bp = ann.params.find((p) => p.kind === 'base');
      const up = ann.params.find((p) => p.kind === 'unit');
      const base = bp ? bp.value : 10;
      const W = wp ? wp.value : 64;
      const kind = v.kind;                          /* number|string|bool|special|symbol|reference */
      const text = v.text || '';
      const isNumberTok = kind === 'number', isStringTok = kind === 'string', isSpecial = kind === 'special';
      /* In bases 64/85 a leading '-' is a digit, not a sign (uint-only bases). */
      const isNeg = (base !== 64 && base !== 85) && text[0] === '-';

      /* ── phase 1: bvn_acc_parse_number (number/string tokens only) ── */
      let accDot = false, accExp = false;
      if (numeric && (isNumberTok || isStringTok)) {
        if (fam === 'uint' && isNeg) { err('error_value_out_of_range', at); return; }
        const start = isNeg ? 1 : 0;
        if (isStringTok) {
          /* only the integer families digit-check the string here */
          if (fam === 'uint' || fam === 'sint')
            for (let i = start; i < text.length; i++)
              if (charToDigit(text[i], base) >= base) { err('error_digit_not_in_base', at); return; }
        } else {                                    /* bare number literal */
          let expState = 0;
          for (let i = start; i < text.length; i++) {
            const c = text[i];
            if (c === '.') { accDot = true; continue; }
            if ((c === 'e' || c === 'E') && (base === 10 || base <= 14)) { accExp = true; expState = 1; continue; }
            if (expState > 0) { expState = 2; continue; }   /* inside exponent */
            if (charToDigit(c, base) >= base) { err('error_digit_not_in_base', at); return; }
          }
        }
      }

      /* ── phase 2: bvn_validate_type_value_compat ── */
      if (fam === 'utf8') {                          /* utf8 demands a string */
        if (!isStringTok) err('error_type_value_mismatch', at);
        return;
      }
      if (fam === 'bool') {                          /* bool demands a bool */
        if (kind !== 'bool') err('error_type_value_mismatch', at);
        return;
      }
      /* numeric family: needs a number / special / string-that-parses */
      if (!(isNumberTok || isStringTok || isSpecial)) { err('error_type_value_mismatch', at); return; }
      if (isSpecial) { checkInlineUnit(v, up, at); return; }  /* nan/inf/ninf — range-exempt */
      if (isStringTok && !validateNumberInBase(text, base)) { err('error_digit_not_in_base', at); return; }
      if (accExp && !isFloat) { err('error_type_value_mismatch', at); return; }   /* exponent on an integer type */
      if (accDot && !isFloat) { err('error_type_value_mismatch', at); return; }   /* dot on an integer type */
      /* fixed-point Q-format range (e.g. 1e6 overflows float_fix:16,q8) */
      if (fam === 'float_fix') {
        const q = (ann.params.find((p) => p.kind === 'q') || {}).value || 0;
        if (!floatFitsFix(text, W, q)) { err('error_value_out_of_range', at); return; }
      }
      /* integer range (arbitrary width via BigInt; plain/decimal floats unbounded) */
      if ((fam === 'uint' || fam === 'sint') && digitsValidForBase(text, base)) {
        if (!intInRange(toBigInt(text, base), fam, W)) { err('error_value_out_of_range', at); return; }
      }
      checkInlineUnit(v, up, at);
    }
    /* inline-unit validation (C order: a unit that does not parse → unit_illegal;
       one that parses but differs structurally from the annotation unit →
       unit_mismatch). Applies to both explicit and synthesised annotations. */
    function checkInlineUnit(v, up, at) {
      if (!v.unit || !BVNUNITS) return;
      /* an over-length inline unit was already reported as unit_too_long by the
         lexer; the C reader stops there and never runs the unit parser, so don't
         also emit unit_illegal */
      if (v.unit.length > 255) return;
      const iu = BVNUNITS.parseUnit(v.unit);
      if (!iu.ok) { err('error_unit_illegal', at); return; }
      if (up && up.text) {
        const au = BVNUNITS.parseUnit(up.text);
        if (au.ok && !BVNUNITS.unitEqual(au, iu)) err('error_unit_mismatch', at);
      }
    }
    function resolve(ann, v) {
      if (ann) {
        const numeric = NUMERIC_FAMILIES.indexOf(ann.familyName) >= 0;
        const wp = ann.params.find((p) => p.kind === 'width');
        const bp = ann.params.find((p) => p.kind === 'base');
        const up = ann.params.find((p) => p.kind === 'unit');
        /* only numeric families carry width/base/unit; bool & utf8 have none */
        v.valueType = { familyName: ann.familyName, family: ann.family,
          width: wp ? wp.value : defaultWidth(ann.familyName),
          base: numeric ? (bp ? bp.value : 10) : null };
        v.valueUnit = (up && up.text !== 'no_unit') ? up.text : (v.unit || null);
      } else {
        v.valueType = { familyName: 'plain', family: 'vt_plain', width: null, base: null };
        v.valueUnit = v.unit || null;
      }
    }
    /* Array element validation stops at the FIRST failing element — the C
       streaming validator returns false on the first bad token and resyncs, so
       it never reports a second element error in the same array. Returns true
       once an error has been emitted so the stop propagates up nested rows.

       Each element's EFFECTIVE type is: its own inline annotation if it carries
       one (which applies to that element only — it does NOT carry to the next);
       otherwise the array's outer annotation `outerAnn`; otherwise the element's
       own synthesised default (un-annotated array). A nested array inherits the
       same outerAnn; a struct element is validated as its own assignments. */
    function validateArrayEls(arr, outerAnn) {
      for (const row of arr.rows)
        for (const el of row.elements) {
          if (el.type === 'array') { if (validateArrayEls(el, outerAnn)) return true; }
          else if (el.type === 'struct') { for (const c of el.children) processAssign(c); }
          else if (el.type === 'scalar' || el.type === 'reference') {
            /* effective type = own inline annotation, else the array's outer
               annotation, else (scalars only) the synthesised default.
               · a null element is universal (skipped above by type !== these).
               · a BARE reference (no annotation) is fine, but a reference that
                 carries a concrete annotation — its own or the array's — is a
                 type_value_mismatch (a reference can't be a uint/utf8/…), exactly
                 as the C streaming validator reports for `[<utf8> &.x]`. */
            let ea = el.ann || outerAnn;
            if (!ea) {
              if (el.type === 'reference') continue;
              if (['number', 'string', 'bool', 'special'].indexOf(el.kind) < 0) continue;
              ea = synthScalarAnn(el);
            }
            /* a parse-time error already stands for this element's annotation
               (e.g. an illegal unit) — the C reader aborts the whole array at
               that point, so stop here too rather than validating later siblings */
            if (ea.errored) return true;
            const n = errors.length; validateExplicit(ea, el);
            if (errors.length > n) return true;
          }
        }
      return false;
    }
    function processAssign(a) {
      const v = a.value;
      if (v.type === 'struct') { for (const c of v.children) processAssign(c); return; }
      if (v.type === 'scalar') {
        /* synthesise the default annotation first, then validate — the C
           validator range-checks synthesised uint64/sint64 defaults too. A
           malformed annotation aborts validation (its error already stands). */
        if (!a.ann && ['number', 'string', 'bool', 'special'].indexOf(v.kind) >= 0)
          a.ann = synthScalarAnn(v);
        if (a.ann && !a.ann.errored) validateExplicit(a.ann, v);
        resolve(a.ann, v);
      } else if (v.type === 'array') {
        if (a.ann) {
          /* explicit annotation: every element is validated against it (the C
             streaming validator checks type, base and range per element) unless
             the element overrides with its own inline annotation */
          if (!a.ann.errored) validateArrayEls(v, a.ann);
        } else {
          /* no annotation: the C reader synthesises a default for EACH element
             from its own kind and validates against that — there is no
             cross-element homogeneity check at the streaming layer (that lives
             in the DOM builder), so a number element is range-checked as uint64/
             sint64/float64 while a string/bool element passes independently. */
          validateArrayEls(v, null);
          const fe = firstScalar(v);          /* a representative type for the tree */
          if (fe && ['number', 'string', 'bool', 'special'].indexOf(fe.kind) >= 0) {
            a.ann = synthScalarAnn(fe);
            /* the C reader keeps the no_unit param on a synthesised array type */
          }
        }
        v.elementType = a.ann ? { familyName: a.ann.familyName, family: a.ann.family } : null;
      } else if (v.type === 'reference') {
        /* a reference cannot carry a concrete type annotation — the C validator
           reports type_value_mismatch for any explicit family on one */
        if (a.ann && !a.ann.errored) validateExplicit(a.ann, v);
        resolve(a.ann, v);
      } else if (v.type === 'null') {
        resolve(a.ann, v);   /* null is universal — never a type mismatch */
      }
    }
    for (const a of tree.children) processAssign(a);

    /* ── flatten the tree into the faithful flat event stream ── */
    const events = [];
    const ev = (type, data) => events.push({ type, data: data || {} });
    function flattenAnn(ann) {
      ev(EV.TYPE_ANN_START, { synthesized: ann.synthesized });
      ev(EV.TYPE_ANN_FAMILY, { family: ann.family, familyName: ann.familyName });
      for (const p of ann.params)
        ev(EV.TYPE_ANN_PARAM, { paramKind: p.kind, text: p.text, value: p.value });
      ev(EV.TYPE_ANN_END, {});
    }
    /* When the array type is synthesised (no explicit annotation) the C reader
       emits a fresh default annotation at the first element AND whenever the next
       element's inferred family differs (default-annotation suppression keeps it
       silent for runs of the same family). prevFam threads that state across rows
       so a homogeneous array yields exactly one annotation. */
    /* The full resolved-type signature the C reader compares for default-
       annotation suppression: it re-emits a type_family only when the next
       value's resolved <family:width,_base,unit> differs from the last emitted
       one — EXCEPT an explicit inline annotation is always emitted (even if it
       repeats the current type). */
    function annSig(ann) {
      const numeric = NUMERIC_FAMILIES.indexOf(ann.familyName) >= 0;
      const wp = ann.params.find((p) => p.kind === 'width');
      const bp = ann.params.find((p) => p.kind === 'base');
      const up = ann.params.find((p) => p.kind === 'unit');
      const w = wp ? wp.value : (numeric ? 64 : null);
      const b = numeric ? (bp ? bp.value : 10) : null;
      const u = up ? up.text : 'no_unit';
      return ann.familyName + '|' + w + '|' + b + '|' + u;
    }
    /* `defaultAnn` is the array's outer annotation (applies to every element that
       does not override with its own inline annotation), or null for an
       un-annotated array (each scalar synthesises its own default type). st.lastSig
       threads the last-emitted resolved type across rows and nested arrays so a
       homogeneous run yields exactly one annotation. */
    function flattenArray(v, defaultAnn, st) {
      st = st || { lastSig: defaultAnn ? annSig(defaultAnn) : null };
      let first = true;
      for (const row of v.rows) {
        if (!first) ev(EV.ARRAY_DIM_START, {});
        first = false;
        ev(EV.ARRAY_ROW_START, {});
        for (const el of row.elements) {
          if (el.type === 'array') { flattenArray(el, defaultAnn, st); continue; }
          /* a struct/octet element resets suppression — the C reader re-emits a
             fresh annotation for the next scalar after one (a nested array, by
             contrast, carries the suppression state through) */
          if (el.type === 'struct' || el.type === 'octet') { flattenValue(el, null); st.lastSig = null; continue; }
          if (el.type === 'null' || el.type === 'reference') {
            ev(EV.DATA, { tokenType: scalarTok(el), text: el.text || '', valueType: v.elementType, valueUnit: null });
            continue;
          }
          /* an element's effective type: its own inline annotation, else the
             array's outer annotation, else its synthesised default */
          let effAnn = null, explicit = false;
          if (el.ann) { effAnn = el.ann; explicit = true; }
          else if (defaultAnn) effAnn = defaultAnn;
          else if (['number', 'string', 'bool', 'special'].indexOf(el.kind) >= 0) effAnn = synthScalarAnn(el);
          if (effAnn) {
            const s = annSig(effAnn);
            if (explicit || s !== st.lastSig) { flattenAnn(effAnn); st.lastSig = s; }
            ev(EV.DATA, { tokenType: arrTok(el), text: el.text || '',
              valueType: { familyName: effAnn.familyName, family: effAnn.family }, valueUnit: null });
          } else {
            ev(EV.DATA, { tokenType: arrTok(el), text: el.text || '',
              valueType: v.elementType, valueUnit: null });
          }
        }
        ev(EV.ARRAY_ROW_END, {});
      }
    }
    function flattenValue(v, defaultAnn) {
      if (v.type === 'scalar' || v.type === 'null' || v.type === 'reference')
        return ev(EV.DATA, { tokenType: scalarTok(v), text: v.text || '',
          valueType: v.valueType, valueUnit: v.valueUnit });
      if (v.type === 'struct') {
        ev(EV.STRUCT_START, {});
        for (const c of v.children) flattenAssign(c);
        return ev(EV.STRUCT_END, {});
      }
      if (v.type === 'array') return flattenArray(v, defaultAnn || null);
      if (v.type === 'octet') {
        ev(EV.OCTET_STREAM_START, {});
        for (const c of v.chunks) ev(EV.DATA, { tokenType: 'token_is_octet_stream', length: c.length });
        return ev(EV.OCTET_STREAM_END, {});
      }
    }
    function flattenAssign(a) {
      ev(EV.ASSIGNMENT_START, { key: a.key });
      if (a.value.type === 'array') {
        /* an explicit (non-synthesised) array annotation is emitted BEFORE the
           array and applies to every element; a synthesised array type is emitted
           INSIDE the first row, per element, by flattenArray */
        const outer = (a.ann && !a.ann.synthesized) ? a.ann : null;
        if (outer) flattenAnn(outer);
        flattenValue(a.value, outer);
      } else {
        /* a scalar/struct/octet value: its annotation (explicit or synthesised)
           always precedes it */
        if (a.ann) flattenAnn(a.ann);
        flattenValue(a.value, null);
      }
    }
    ev(EV.STREAM_START, {});
    for (const a of tree.children) flattenAssign(a);
    ev(EV.STREAM_END, {});

    /* the C reader surfaces errors as it streams — present them in source order
       (lexer errors are already ordered; validator errors are merged in by
       byte offset so a verified-layer error never trails a later lexer one) */
    errors.sort((a, b) => (a.offset - b.offset) || (a.line - b.line) || (a.col - b.col));
    /* collapse duplicate reports of the same code at the same byte (an
       unterminated value can be flagged by both the value reader and the
       enclosing assignment) */
    const deduped = errors.filter((e, k) =>
      k === 0 || e.code !== errors[k - 1].code ||
      e.line !== errors[k - 1].line || e.col !== errors[k - 1].col);

    return { events, errors: deduped, tree };
  }

  /* ── Export ────────────────────────────────────────────────────────── */
  if (typeof module !== 'undefined' && module.exports) {
    module.exports = { parseBovnar, parseFaithful, EV };
  } else {
    global.BovnarParser = { parseBovnar, parseFaithful, EV };
  }

}(typeof window !== 'undefined' ? window : this));
