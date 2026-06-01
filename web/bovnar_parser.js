/* bovnar_parser.js — lenient JavaScript parser for the Bovnar format,
 * used by the web visualizer.  It approximates the reference C
 * implementation's event stream but is deliberately NOT a faithful
 * reproduction of it:
 *   - it adds an ev_assignment_end delimiter that the C core does not emit
 *     (used by the visualizer to close an assignment's render scope);
 *   - it does not synthesise the default type-annotation event sequence
 *     that the C validator emits for unannotated values; and
 *   - it performs no type/value-compatibility validation, so it accepts
 *     some inputs the C validator would reject.
 * Exported as window.BovnarParser = { parseBovnar, EV }
 */
(function (global) {
  'use strict';

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

    function emit(type, data) {
      cb({ type, data: data || {}, line, col, offset: pos });
    }

    function eof()      { return pos >= text.length; }
    function cur()      { return text[pos]; }
    function peek(n)    { return text[pos + (n || 1)]; }

    function advance() {
      const ch = text[pos++];
      if (ch === '\n') { line++; col = 1; } else { col++; }
      return ch;
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
      let s = '';
      while (!eof()) {
        const c = cur();
        const code = c.charCodeAt(0);
        if (s.length === 0) {
          if (/[A-Za-z_]/.test(c) || code >= 0xC0) { s += c; advance(); }
          else break;
        } else {
          if (/[A-Za-z0-9_+\-]/.test(c) || code > 0x7F) { s += c; advance(); }
          else break;
        }
      }
      return s;
    }

    /* Parse a quoted string body (opening " already consumed). */
    function readStringBody() {
      let s = '';
      while (!eof()) {
        const c = advance();
        if (c === '"') return { closed: true, value: s };
        if (c === '\\') {
          if (eof()) { emit(EV.ERROR, { msg: 'Unterminated escape at EOF' }); break; }
          const e = advance();
          const ESC = { t:'\t', n:'\n', v:'\v', f:'\f', r:'\r', '"':'"', '\\':'\\' };
          if (ESC[e] !== undefined) s += ESC[e];
          else { emit(EV.ERROR, { msg: `Illegal escape: \\${e}` }); s += e; }
        } else {
          s += c;
        }
      }
      emit(EV.ERROR, { msg: 'Unterminated string literal' });
      return { closed: false, value: s };
    }

    /* Read a number literal (leading - already peeked). */
    function readNumber() {
      let s = '';
      if (!eof() && cur() === '-') { s += '-'; advance(); }
      while (!eof() && /\d/.test(cur())) { s += cur(); advance(); }
      if (!eof() && cur() === '.') {
        s += '.'; advance();
        while (!eof() && /\d/.test(cur())) { s += cur(); advance(); }
      }
      if (!eof() && (cur() === 'e' || cur() === 'E')) {
        s += cur(); advance();
        if (!eof() && (cur() === '+' || cur() === '-')) { s += cur(); advance(); }
        while (!eof() && /\d/.test(cur())) { s += cur(); advance(); }
      }
      return s;
    }

    /* Try to read an optional inline unit suffix after a scalar.
       Requires at least one whitespace before the unit.
       NOT allowed inside arrays. */
    function tryInlineUnit() {
      const sp = pos, sl = line, sc = col;
      let hadWs = false;
      while (!eof() && /[ \t]/.test(cur())) { advance(); hadWs = true; }
      if (hadWs && !eof()) {
        const c = cur();
        const code = c.charCodeAt(0);
        /* Unit starts with a letter, _, $ (currency sigil), %, ( (a leading
           parenthesised group), °, µ, Ω, or a high byte */
        if (/[A-Za-z_$%(°µΩ]/.test(c) || code > 0x7F) {
          let unit = '';
          while (!eof() && !/[ \t\r\n\v\f;,\[\]{}#]/.test(cur())) {
            unit += cur(); advance();
          }
          return unit;
        }
      }
      pos = sp; line = sl; col = sc;
      return null;
    }

    /* Parse a type annotation body (opening '<' already consumed). */
    function readTypeAnnotation() {
      emit(EV.TYPE_ANN_START, {});
      skipWsComments();

      /* family keyword (utf8 contains a digit — include [0-9]) */
      let family = '';
      while (!eof() && /[a-z_0-9]/.test(cur())) { family += cur(); advance(); }
      if (!family) {
        emit(EV.ERROR, { msg: 'Empty type annotation' });
        while (!eof() && cur() !== '>') advance();
        if (!eof()) advance();
        emit(EV.TYPE_ANN_END, {});
        return;
      }
      emit(EV.TYPE_ANN_FAMILY, { family });

      /* parameters */
      skipWsComments();
      if (!eof() && cur() === ':') {
        advance(); // skip ':'
        let firstParam = true;
        for (;;) {
          skipWsComments();
          if (eof() || cur() === '>') break;
          if (!firstParam) {
            if (cur() === ',') { advance(); skipWsComments(); }
            else break;
          }
          firstParam = false;
          if (eof() || cur() === '>') break;

          /* collect raw param token; stop at ',' '>' ';' or newline (error recovery) */
          let raw = '';
          while (!eof() && cur() !== ',' && cur() !== '>' &&
                 cur() !== ';' && cur() !== '\n' && cur() !== '\r') {
            raw += cur(); advance();
          }
          raw = raw.trim();
          /* A comma/colon must introduce a real parameter: empty or
           * trailing components (<uint:8,>, <uint:8,,>, <uint:,_16>) are
           * rejected, matching the C core's strict type-param-list. */
          if (!raw) { emit(EV.ERROR, { msg: 'Empty type parameter' }); continue; }

          let paramData;
          if (/^\d+$/.test(raw)) {
            paramData = { kind: 'width', value: parseInt(raw, 10), text: raw };
          } else if (/^_\d+$/.test(raw)) {
            paramData = { kind: 'base', value: parseInt(raw.slice(1), 10), text: raw };
          } else if (/^q\d+$/.test(raw)) {
            paramData = { kind: 'q', value: parseInt(raw.slice(1), 10), text: raw };
          } else {
            paramData = { kind: 'unit', text: raw };
          }
          emit(EV.TYPE_ANN_PARAM, paramData);
        }
      }

      skipWsComments();
      if (!eof() && cur() === '>') advance(); // skip '>'
      else emit(EV.ERROR, { msg: 'Unclosed type annotation (expected \'>\')' });
      emit(EV.TYPE_ANN_END, {});
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
        emit(EV.DATA, { kind: 'null', value: null, text: '' });
        return;
      }

      /* Struct */
      if (cur() === '{') {
        advance();
        emit(EV.STRUCT_START, {});
        skipWsComments();
        while (!eof() && cur() !== '}') {
          if (!parseAssignment(true)) break;
          skipWsComments();
        }
        if (!eof() && cur() === '}') advance();
        emit(EV.STRUCT_END, {});
        return;
      }

      /* Array */
      if (cur() === '[') {
        readArray();
        return;
      }

      /* String (with optional concatenation) */
      if (cur() === '"') {
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
        emit(EV.DATA, { kind: 'string', value: fullVal, text: fullVal, unit });
        return;
      }

      /* Reference */
      if (cur() === '&') {
        advance();
        let ref = '';
        while (!eof() && !/[ \t\r\n\v\f;,\[\]{}#]/.test(cur())) { ref += cur(); advance(); }
        emit(EV.DATA, { kind: 'reference', value: ref, text: '&' + ref });
        return;
      }

      /* Number (integer-led, dot-led, or negative) */
      if (cur() === '-' || /\d/.test(cur()) ||
          (cur() === '.' && /\d/.test(peek()))) {
        const numStr = readNumber();
        const unit   = insideArray ? null : tryInlineUnit();
        emit(EV.DATA, { kind: 'number', value: numStr, text: numStr, unit });
        return;
      }

      /* Symbol (bare word) — or a reserved keyword (null / true / false /
         on / off), which the validator reclassifies out of the symbol space. */
      if (/[A-Za-z_]/.test(cur()) || cur().charCodeAt(0) > 0x7F) {
        let sym = '';
        let illegalDot = false;
        while (!eof() && !/[ \t\r\n\v\f;,\[\]{}#<>=]/.test(cur())) {
          if (cur() === '.') illegalDot = true;  // '.' is a hard error in a symbol body
          sym += cur(); advance();
        }
        if (illegalDot) emit(EV.ERROR, { msg: `Illegal '.' in symbol '${sym}'` });
        if (sym === 'null') {
          emit(EV.DATA, { kind: 'null', value: null, text: sym });
        } else if (sym === 'true' || sym === 'on') {
          emit(EV.DATA, { kind: 'bool', value: true, text: 'true' });
        } else if (sym === 'false' || sym === 'off') {
          emit(EV.DATA, { kind: 'bool', value: false, text: 'false' });
        } else if (sym === 'nan' || sym === 'inf' || sym === 'ninf') {
          /* Special floats are bare reserved keywords (no sigil, no inline
             unit), reclassified out of the symbol space like null/bool. */
          emit(EV.DATA, { kind: 'special', value: sym, text: sym });
        } else {
          emit(EV.DATA, { kind: 'symbol', value: sym, text: sym });
        }
        return;
      }

      emit(EV.ERROR, { msg: `Unexpected character: ${JSON.stringify(cur())}` });
      advance();
    }

    /* Parse an octet stream (the entering NUL byte is cur, not consumed).
       Binary protocol (§11):
         tag 0x01 → chunk: 2-byte LE length (0x0000 encodes 65536) + data
         tag 0x00 → end of stream
         any other tag → out of sync (hard error) */
    function readOctetStream() {
      advance(); // consume the NUL that enters binary mode
      emit(EV.OCTET_STREAM_START, {});
      for (;;) {
        if (eof()) {
          emit(EV.ERROR, { msg: 'Unterminated octet stream (expected end tag 0x00)' });
          return;
        }
        const tag = text.charCodeAt(pos);
        advance();
        if (tag === 0x00) { emit(EV.OCTET_STREAM_END, {}); return; }
        if (tag !== 0x01) {
          emit(EV.ERROR, { msg: `Octet stream out of sync (tag 0x${tag.toString(16)})` });
          return;
        }
        /* 16-bit little-endian chunk length; 0x0000 → 65536 */
        if (pos + 1 >= text.length) {
          emit(EV.ERROR, { msg: 'Truncated octet-stream chunk length' });
          return;
        }
        const lo = text.charCodeAt(pos); advance();
        const hi = text.charCodeAt(pos); advance();
        let len = lo | (hi << 8);
        if (len === 0) len = 65536;

        let data = '';
        let i = 0;
        for (; i < len && !eof(); i++) { data += advance(); }
        if (i < len) {
          emit(EV.ERROR, { msg: 'Truncated octet-stream chunk data' });
          return;
        }
        emit(EV.DATA, { kind: 'octet', value: data, text: data, length: len });
      }
    }

    /* Parse one bracket row '[...]' (opening '[' not yet consumed) and return
       its element-position count. A row always has at least one position (an
       empty '[]' is one null element, '[,]' is two), so the count equals
       commas + 1 — matching the C lexer's curr_row_size. */
    function readArrayRow() {
      advance(); // skip '['
      emit(EV.ARRAY_ROW_START, {});
      skipWsComments();

      let count = 1; // every row holds at least one (possibly null) position
      if (!eof() && cur() !== ']') {
        /* First element (may be null if cur is ',' or ']') */
        readArrayElement();

        /* Remaining elements separated by ',' */
        for (;;) {
          skipWsComments();
          if (eof() || cur() !== ',') break;
          advance(); // consume ','
          readArrayElement();
          count++;
        }
      }

      skipWsComments();
      if (!eof() && cur() === ']') advance(); // skip ']'
      else emit(EV.ERROR, { msg: 'Unclosed array (expected \']\')' });
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
        const count = readArrayRow();
        if (firstCount < 0) {
          firstCount = count;
        } else if (count !== firstCount) {
          emit(EV.ERROR, {
            msg: `array_row_size_mismatch: dimension row has ${count} ` +
                 `element(s), expected ${firstCount}`,
          });
        }

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

    /* Parse one full assignment (.key = value ;).
       Returns false when nothing more to parse at this level. */
    function parseAssignment(inStruct) {
      skipWsComments();
      if (eof()) return false;
      if (inStruct && cur() === '}') return false;

      if (cur() !== '.') {
        emit(EV.ERROR, { msg: `Expected '.' to start an assignment, got ${JSON.stringify(cur())}` });
        /* error recovery: skip to next ';' at nesting depth 0 */
        let depth = 0;
        while (!eof()) {
          const c = cur();
          if (c === '[' || c === '{') depth++;
          else if (c === ']' || c === '}') {
            if (depth === 0) {
              /* Leave '}' unconsumed only when we are inside a struct so the
                 struct handler's closing-brace check can see it.  ']' has no
                 such handler and must be consumed to prevent an infinite loop. */
              if (c !== '}' || !inStruct) advance();
              break;
            }
            depth--;
          }
          else if (c === ';' && depth === 0) { advance(); break; }
          advance();
        }
        return true;
      }
      advance(); // skip '.'

      const key = readIdent();
      if (!key) emit(EV.ERROR, { msg: 'Empty identifier after \'.\'' });

      emit(EV.ASSIGNMENT_START, { key });

      skipWsComments();
      if (!eof() && cur() === '=') advance();
      else emit(EV.ERROR, { msg: `Expected '=' after key '${key}'` });

      readValue(false);

      skipWsComments();
      if (!eof() && cur() === ';') advance();
      else if (!eof() && cur() !== '}') {
        emit(EV.ERROR, { msg: `Expected ';' to close assignment '${key}'` });
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
    }
    emit(EV.STREAM_END, {});
  }

  /* ── Export ────────────────────────────────────────────────────────── */
  if (typeof module !== 'undefined' && module.exports) {
    module.exports = { parseBovnar, EV };
  } else {
    global.BovnarParser = { parseBovnar, EV };
  }

}(typeof window !== 'undefined' ? window : this));
