/* ════════════════════════════════════════════════════════════════════════
   bovnar_highlight.js — self-contained Bovnar (.bvnr) syntax highlighter.

   One file, no dependencies. Include it on any page and call:

       BovnarHighlight.highlight(sourceText)   → HTML (preserves newlines)
       BovnarHighlight.highlightLine(oneLine)  → HTML for a single line

   The returned HTML uses <span class="bvh-…"> tokens; the matching colours are
   injected once into the document <head>. Token classification and palette are
   kept in lock-step with highlighter/vim/bovnar.vim so the web demos, the live
   playground and the documentation viewer all match the editor highlighters.
   ════════════════════════════════════════════════════════════════════════ */
(function (global) {
  'use strict';

  /* ── Palette (mirrors highlighter/vim/bovnar.vim guifg values) ─────────── */
  const CSS = `
.bvh-comment{color:#6e7a86}
.bvh-sigil{color:#c084fc}
.bvh-key{color:#67d1f4}
.bvh-eq{color:#f47067}
.bvh-semi{color:#5a6270}
.bvh-str{color:#87d88c}
.bvh-num{color:#f0c64e}
.bvh-neg{color:#f47067}
.bvh-unit{color:#5eead4}
.bvh-unitsep{color:#6e7a86}
.bvh-sym{color:#67d1f4}
.bvh-bool{color:#c084fc;font-weight:600}
.bvh-null{color:#4c5661;font-style:italic}
.bvh-special{color:#c084fc}
.bvh-sep{color:#6e7a86}
.bvh-adelim{color:#a09040}
.bvh-type{color:#e8e8e8;font-weight:600}
.bvh-tsep{color:#6e7a86}
.bvh-width{color:#ff5faf}
.bvh-base{color:#afffd7}
.bvh-param{color:#da844c}
.bvh-refop{color:#f47067;font-weight:600}
.bvh-refpath{color:#d06070}
.bvh-d1{color:#da844c}.bvh-d2{color:#c084fc}.bvh-d3{color:#ff2a7d}.bvh-d4{color:#ffa500}
.bvh-d5{color:#87d88c}.bvh-d6{color:#5eead4}.bvh-d7{color:#f0c64e}.bvh-d8{color:#67d1f4}
`;
  if (typeof document !== 'undefined' && !document.getElementById('bvh-style')) {
    const s = document.createElement('style');
    s.id = 'bvh-style';
    s.textContent = CSS;
    (document.head || document.documentElement).appendChild(s);
  }

  function esc(s) {
    return String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
  }
  function span(cls, t) { return '<span class="bvh-' + cls + '">' + esc(t) + '</span>'; }

  // Token char classes shared by the body scanner and the annotation scanner.
  const ID  = /^[A-Za-z_$µμΩÅ°%‰‱][\w·~µμΩÅ°⁰¹²³⁴⁵⁶⁷⁸⁹⁺⁻+\-*\/^$]*/;
  const NUM = /^-?(?:\d[\d_]*(?:\.\d*)?(?:[eE][+-]?\d+)?|\.\d+(?:[eE][+-]?\d+)?)/;

  // Split a unit so '~', '*' and '/' read as separators (vim BovnarUnitTilde /
  // UnitSep) and the rest as the unit body (BovnarTypeUnit / UnitExp).
  function unit(u) {
    return u.replace(/[~*\/]+|[^~*\/]+/g, (s) => span(/[~*\/]/.test(s[0]) ? 'unitsep' : 'unit', s));
  }

  // Tokenise the interior of a <…> type annotation as vim does: olive delimiters,
  // bold type family, pink bit-width, mint base, orange q-param, teal unit.
  function ann(s) {
    let out = '', i = 0, m;
    while (i < s.length) {
      const c = s[i], r = s.slice(i);
      if (c === ' ' || c === '\t') { m = /^[ \t]+/.exec(r); out += esc(m[0]); i += m[0].length; continue; }
      if (c === '<' || c === '>') { out += span('adelim', c); i++; continue; }
      if (c === ':') { out += span('tsep', ':'); i++; continue; }
      if (c === ',') { out += span('sep', ','); i++; continue; }
      if ((m = /^(?:float_fix|float_dec|float|uint|sint|utf8|bool|no_unit)\b/.exec(r))) { out += span(m[0] === 'no_unit' ? 'unit' : 'type', m[0]); i += m[0].length; continue; }
      if ((m = /^q\d+/.exec(r))) { out += span('param', m[0]); i += m[0].length; continue; }
      if ((m = /^_\d+/.exec(r))) { out += span('base', m[0]); i += m[0].length; continue; }
      if ((m = /^\d+/.exec(r))) { out += span('width', m[0]); i += m[0].length; continue; }
      if ((m = ID.exec(r))) { out += unit(m[0]); i += m[0].length; continue; }
      out += esc(c); i++;
    }
    return out;
  }

  // Highlight Bovnar source. Handles multi-line input (newlines preserved; bracket
  // depth persists across lines) so it serves both whole-document code blocks and
  // a single editor line. Matches highlighter/vim/bovnar.vim classification.
  function tokenize(src) {
    src = String(src);
    const DLV = (d) => 'd' + (((Math.max(1, d) - 1) % 8) + 1);   // span() adds the 'bvh-' prefix
    let out = '', i = 0, sd = 0, ad = 0, afterNum = false, m;
    while (i < src.length) {
      const c = src[i], r = src.slice(i);
      if (c === '\n' || c === '\r') { out += c; i++; afterNum = false; continue; }
      if (c === ' ' || c === '\t') { m = /^[ \t]+/.exec(r); out += esc(m[0]); i += m[0].length; continue; } // keep afterNum across spaces
      if (c === '#') { let j = i; while (j < src.length && src[j] !== '\n' && src[j] !== '\r') j++; out += span('comment', src.slice(i, j)); i = j; afterNum = false; continue; }
      if (c === '<') {                                  // type annotation (single line)
        const nl = src.indexOf('\n', i); const lim = nl < 0 ? src.length : nl;
        let j = src.indexOf('>', i);
        if (j < 0 || j > lim) { out += esc('<'); i++; afterNum = false; continue; }
        out += ann(src.slice(i, j + 1)); i = j + 1; afterNum = false; continue;
      }
      if (c === '"') {                                  // string literal (unescaped newlines are legal, so it may span lines)
        let j = i + 1;
        while (j < src.length) { if (src[j] === '\\') { j += 2; continue; } if (src[j] === '"') { j++; break; } j++; }
        // Wrap each physical line of the string in its own span so the emitted HTML
        // stays balanced when split on '\n' (see highlightLines / gutter renderers).
        out += src.slice(i, j).split('\n').map((seg) => span('str', seg)).join('\n');
        i = j; afterNum = false; continue;
      }
      if (c === '&' && (m = /^&(?:\.[A-Za-z_][\w+\-]*)+/.exec(r))) { out += span('refop', '&') + span('refpath', m[0].slice(1)); i += m[0].length; afterNum = false; continue; }
      if (c === '$' && (m = /^\$(?:-?inf|nan)\b/.exec(r))) { out += span('special', m[0]); i += m[0].length; afterNum = false; continue; }
      if (c === '.' && (m = /^\.[A-Za-z_][\w+\-]*/.exec(r))) { out += span('sigil', '.') + span('key', m[0].slice(1)); i += m[0].length; afterNum = false; continue; }
      if ((m = NUM.exec(r)) && /\d/.test(m[0])) { let s = m[0]; if (s[0] === '-') { out += span('neg', '-'); s = s.slice(1); } out += span('num', s); i += m[0].length; afterNum = true; continue; }
      if ((m = ID.exec(r))) {
        const w = m[0];
        if (/^(?:true|false|on|off)$/.test(w)) out += span('bool', w);
        else if (w === 'null') out += span('null', w);
        else if (afterNum) out += unit(w);            // an inline unit follows a number
        else out += span('sym', w);
        i += w.length; afterNum = false; continue;
      }
      if (c === '=') { out += span('eq', '='); i++; afterNum = false; continue; }
      if (c === ';') { out += span('semi', ';'); i++; afterNum = false; continue; }
      if (c === '{') { sd++; out += span(DLV(sd), '{'); i++; afterNum = false; continue; }
      if (c === '}') { out += span(DLV(sd), '}'); if (sd > 0) sd--; i++; afterNum = false; continue; }
      if (c === '[') { ad++; out += span(DLV(ad), '['); i++; afterNum = false; continue; }
      if (c === ']') { out += span(DLV(ad), ']'); if (ad > 0) ad--; i++; afterNum = false; continue; }
      if (c === ',' || c === '/') { out += span('sep', c); i++; afterNum = false; continue; }
      out += esc(c); i++; afterNum = false;
    }
    return out;
  }

  // Per-line HTML for gutter / line-number views. Highlighting the whole document
  // at once threads cross-line state (open strings, bracket depth), and the string
  // scanner closes/reopens its span at each newline, so every returned line is a
  // self-contained, balanced fragment. Use this instead of mapping highlightLine
  // over pre-split lines, which cannot see a string left open on a previous line.
  function highlightLines(src) { return tokenize(src).split('\n'); }

  global.BovnarHighlight = { highlight: tokenize, highlightLine: tokenize, highlightLines: highlightLines, esc: esc };
})(typeof window !== 'undefined' ? window : this);
