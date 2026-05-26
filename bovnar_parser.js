/* bovnar_parser.js — JavaScript parser for the Bovnar format
 * Emits events matching the reference C implementation's event stream.
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

    /* Read $nan$, $infinity$, $-infinity$ — cur is '$'. */
    function readSpecialNumber() {
      advance(); // skip opening $
      let kw = '';
      while (!eof() && cur() !== '$') { kw += cur(); advance(); }
      if (!eof()) advance(); // skip closing $
      return kw;
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
        /* Unit starts with a letter, °, µ, Ω, or a high byte */
        if (/[A-Za-z_°µΩ]/.test(c) || code > 0x7F) {
          let unit = '';
          while (!eof() && !/[ \t\r\n;,\[\]{}#]/.test(cur())) {
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
          if (!raw) continue;

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

      /* Special number */
      if (cur() === '$') {
        const kw = readSpecialNumber();
        emit(EV.DATA, { kind: 'special', value: kw, text: '$' + kw + '$' });
        return;
      }

      /* Reference */
      if (cur() === '&') {
        advance();
        let ref = '';
        while (!eof() && !/[ \t\r\n;,\[\]{}#]/.test(cur())) { ref += cur(); advance(); }
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

      /* Symbol (bare word) */
      if (/[A-Za-z_]/.test(cur()) || cur().charCodeAt(0) > 0x7F) {
        let sym = '';
        while (!eof() && !/[ \t\r\n;,\[\]{}#<>=]/.test(cur())) {
          sym += cur(); advance();
        }
        emit(EV.DATA, { kind: 'symbol', value: sym, text: sym });
        return;
      }

      emit(EV.ERROR, { msg: `Unexpected character: ${JSON.stringify(cur())}` });
      advance();
    }

    /* Parse one array (opening '[' not yet consumed). */
    function readArray() {
      advance(); // skip '['
      emit(EV.ARRAY_ROW_START, {});
      skipWsComments();

      if (!eof() && cur() !== ']') {
        /* First element (may be null if cur is ',' or ']') */
        readArrayElement();

        /* Remaining elements separated by ',' */
        for (;;) {
          skipWsComments();
          if (eof() || cur() !== ',') break;
          advance(); // consume ','
          readArrayElement();
        }
      }

      skipWsComments();
      if (!eof() && cur() === ']') advance(); // skip ']'
      else emit(EV.ERROR, { msg: 'Unclosed array (expected \']\')' });
      emit(EV.ARRAY_ROW_END, {});

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
          readArray();
        } else {
          pos = sp; line = sl; col = sc; // not a dimension separator
        }
      } else {
        pos = sp; line = sl; col = sc;
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
