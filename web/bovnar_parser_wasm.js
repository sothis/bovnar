/*
 * bovnar_parser_wasm.js — the parser behind window.BovnarParser.
 *
 * Loads the real C reference parser (compiled to WASM) and makes it the sole
 * authority behind BovnarParser.parseFaithful / parseBovnar, producing the exact
 * output shapes the playground and the live demos consume with true
 * type/unit/value validation.
 *
 * Both surfaces are assembled from the reference *verified event stream*
 * (bvnr_wasm_events, run in resync mode) plus the resync error list
 * (bvnr_wasm_errors). Resync means a malformed assignment is skipped, not fatal,
 * so the tree and the demos recover and show every well-formed assignment past
 * an error.
 *
 * The WASM module loads asynchronously, so until it is ready parseFaithful/
 * parseBovnar return empty results (an empty tree / no events); a
 * 'bovnar-wasm-ready' event fires once the module is live so the playground and
 * demos can re-render against the real parser. The dispatcher functions have
 * stable identity, so consumers that captured `const { parseBovnar } =
 * BovnarParser` at load time pick up the parser transparently once it arrives.
 *
 * The C event stream carries no per-token position, so line numbers (demo
 * gutters) and the synthesised-vs-explicit annotation flag (a cosmetic tree tag)
 * are recovered from the source text by a forward scan over assignments in
 * document order.
 */
(function () {
  'use strict';

  var EV = {
    STREAM_START: 'ev_stream_start', STREAM_END: 'ev_stream_end',
    ASSIGNMENT_START: 'ev_assignment_start', ASSIGNMENT_END: 'ev_assignment_end',
    TYPE_ANN_START: 'ev_type_annotation_start',
    TYPE_ANN_FAMILY: 'ev_type_annotation_type_family',
    TYPE_ANN_PARAM: 'ev_type_annotation_type_family_parameter',
    TYPE_ANN_END: 'ev_type_annotation_end',
    STRUCT_START: 'ev_struct_start', STRUCT_END: 'ev_struct_end',
    ARRAY_ROW_START: 'ev_array_row_start', ARRAY_ROW_END: 'ev_array_row_end',
    ARRAY_DIM_START: 'ev_array_dim_start',
    DATA: 'ev_data',
    OCTET_STREAM_START: 'ev_octet_stream_start',
    OCTET_STREAM_END: 'ev_octet_stream_end',
    ERROR: 'ev_error',
  };

  var wasm = null;
  import('./bovnar_wasm.js?v=7945170b613c')
    .then(function (m) { return m.loadBovnar(); })
    .then(function (b) {
      wasm = b;
      try { window.dispatchEvent(new Event('bovnar-wasm-ready')); } catch (_) {}
    })
    .catch(function (e) {
      if (window.console) console.warn('bovnar: WASM parser failed to load', e);
    });

  /* Blank out string literals ("…") and '#' comments — replacing their bytes with
   * spaces while preserving offsets and newlines — so the key scanner below can
   * never match a '.key =' that merely appears inside a string or a comment.
   * (Octet-stream content is base64/hex, which can't contain '.', '#' or '"', so
   * it needs no masking.) */
  function maskStringsAndComments(text) {
    var out = '', n = text.length, mode = 0;   // 0 = code, 1 = string, 2 = comment
    for (var i = 0; i < n; i++) {
      var c = text[i];
      if (mode === 2) { if (c === '\n') { mode = 0; out += '\n'; } else out += ' '; continue; }
      if (mode === 1) {
        if (c === '\\') { out += ' '; if (i + 1 < n) { out += (text[i + 1] === '\n' ? '\n' : ' '); i++; } continue; }
        if (c === '"') { mode = 0; out += ' '; continue; }
        out += (c === '\n') ? '\n' : ' '; continue;
      }
      if (c === '#') { mode = 2; out += ' '; continue; }
      if (c === '"') { mode = 1; out += ' '; continue; }
      out += c;
    }
    return out;
  }

  /* ── source line / explicit-annotation scanner ───────────────────────── */
  function makeScan(text) {
    // Structural scanning runs on the masked copy (offsets map 1:1 to the source,
    // so line numbers and the value slice below are still correct).
    var masked = maskStringsAndComments(text);
    var pos = 0;
    // Incremental line counter. This used to rescan from offset 0 on every call,
    // and next() calls it once per assignment -- O(assignments x document), i.e.
    // quadratic: ~46M charCodeAt calls for a 1000-assignment, 46 KB document, on
    // the main thread, every debounce tick. The scan is strictly monotonic (pos
    // only advances and each match index is >= the previous pos), so count
    // newlines only over the span since the last query. lastIdx is reset if a
    // caller ever asks for an earlier offset, so a non-monotonic use stays correct.
    var lastIdx = 0, lastLine = 1;
    function lineAt(idx) {
      if (idx > masked.length) idx = masked.length;
      if (idx < lastIdx) { lastIdx = 0; lastLine = 1; }
      for (var i = lastIdx; i < idx; i++) if (masked.charCodeAt(i) === 10) lastLine++;
      lastIdx = idx;
      return lastLine;
    }
    return function next(key) {
      var needle = '.' + key;
      var i = masked.indexOf(needle, pos);
      while (i >= 0) {
        var before = i === 0 ? '\n' : masked[i - 1];
        var j = i + needle.length;
        while (j < masked.length && (masked[j] === ' ' || masked[j] === '\t')) j++;
        var after = masked[j];
        if (/[\s{};]/.test(before) && (after === '=' || after === '{')) {
          pos = j;
          var semi = masked.indexOf(';', j); if (semi < 0) semi = masked.length;
          return { line: lineAt(i),
                   synthesized: after === '=' ? !/^\s*=\s*</.test(masked.slice(j, semi)) : false };
        }
        i = masked.indexOf(needle, i + 1);
      }
      return { line: lineAt(pos), synthesized: false };
    };
  }

  /* ── event → value helpers ───────────────────────────────────────────── */
  var NUMERIC = { uint: 1, sint: 1, float: 1, float_fix: 1, float_dec: 1, datetime: 1 };
  function isSpecial(t) { return /^(inf|ninf|-inf|nan)$/i.test(t || ''); }
  function dataKind(e) {
    if (e.family === 'datetime') return 'datetime';
    switch (e.tok) {
      case 'string': case 'array_string': return 'string';
      case 'symbol': return 'symbol';
      case 'reference': return 'reference';
      case 'null': return 'null';
      case 'bool': return 'bool';
      default: return isSpecial(e.text) ? 'special' : 'number';
    }
  }
  function dataValue(e) {
    if (e.tok === 'bool') return e.text === 'true';
    if (e.tok === 'null') return null;
    return e.text != null ? e.text : '';   // numbers kept as text; demos parseFloat()
  }
  function scalarNode(e) {
    var num = NUMERIC[e.family] ? true : false;
    var width = num && ('width' in e) ? e.width : null;
    var base = num && ('base' in e) ? e.base : null;
    var vt = { familyName: e.family, family: 'vt_' + e.family, width: width, base: base, epoch: e.epoch };
    var type = e.tok === 'reference' ? 'reference' : (e.tok === 'null' ? 'null' : 'scalar');
    return { type: type, kind: dataKind(e), text: e.text != null ? e.text : '',
             value: dataValue(e), familyName: e.family, width: width, base: base,
             unit: e.unit || null, epoch: e.epoch || null,
             valueType: vt, valueUnit: e.unit || null };
  }
  function annFromValue(v, synthesized) {
    var params = [];
    if (v.width != null) params.push({ kind: 'width', text: String(v.width), value: v.width });
    if (v.base != null)  params.push({ kind: 'base',  text: '_' + v.base,    value: v.base });
    if (v.unit)          params.push({ kind: 'unit',  text: v.unit });
    if (v.epoch)         params.push({ kind: 'epoch', text: v.epoch });
    return { synthesized: synthesized, familyName: v.familyName,
             family: 'vt_' + v.familyName, params: params };
  }

  /* ── parseFaithful: assemble the tree from the verified event stream ─────
   *
   * Arrays live on the SAME container stack as structs, because in bovnar an
   * array element is a value like any other — including a struct or another
   * array. A flat "one array being built" variable cannot express that: it made
   * a struct inside an array open a keyed assignment in the enclosing scope
   * (stealing the array's key), committed the array — empty — at that struct's
   * first field, and dropped every following element on the floor. `.a =
   * [{.b=3;},{.c=4;}];` rendered as two top-level structs, one of them keyed
   * `null`, around a phantom 1×0 array.
   *
   * The stack has to be lazy about closing an array because the C stream has no
   * "array end" event: a row ends with ev_array_row_end, and only the NEXT event
   * says whether the array continues with a '/'-dimension row (announced by
   * ev_array_dim_start), whether a sibling sub-array begins, or whether the
   * array is over. So settle() pops arrays whose last row has closed, and runs
   * before any event that proves the array cannot continue.
   *
   * Elements land in the innermost OPEN row; that single rule is what makes a
   * struct inside an array an element instead of an assignment. The resulting
   * shape matches bvnr_wasm_doc's DOM projection node for node.
   */
  function buildTree(events, scan) {
    var root = { type: 'stream', children: [] };
    var stack = [{ kind: 'struct', node: root }];
    var key = null, line = 1, synth = false;
    var oct = null;       // the octet node currently collecting chunks
    function top() { return stack[stack.length - 1]; }
    /* the row currently accepting elements, or null between rows / in a struct */
    function openRow() { var f = top(); return f.kind === 'array' ? f.row : null; }
    function closeArray(f) {
      /* bvn_dom_array_dims() is the '/'-row count (src/dom/bovnar_dom_builder.c),
         which is exactly the number of rows opened here. */
      f.node.dims = f.node.rows.length;
      /* The C emits ONE type annotation for an array — the element type — and it
         belongs to the array's assignment. Reconstruct it from the first element
         that actually carries one, exactly as the scalar path does: the
         annotation precedes the first TYPED element, which in `[null, 1]` is not
         the first element. */
      if (!f.assign || f.assign.ann) return;
      for (var r = 0; r < f.node.rows.length; r++) {
        var els = f.node.rows[r].elements;
        for (var c = 0; c < els.length; c++) {
          if (els[c].type === 'scalar' && NUMERIC[els[c].familyName]) {
            f.assign.ann = annFromValue(els[c], f.synth);
            return;
          }
        }
      }
    }
    function settle() {
      while (top().kind === 'array' && !top().row) closeArray(stack.pop());
    }
    /* An element of the open row, or a keyed assignment in the enclosing scope.
       Returns the assignment when it made one (null for an array element). */
    function attach(value) {
      var row = openRow();
      if (row) { row.elements.push(value); return null; }
      var assign = { type: 'assignment', key: key, line: line, col: 1,
                     ann: null, value: value };
      top().node.children.push(assign);
      key = null;
      return assign;
    }
    for (var i = 0; i < events.length; i++) {
      var e = events[i];
      switch (e.ev) {
        case 'assign_start': {
          settle();
          var s = scan(e.text); key = e.text; line = s.line; synth = s.synthesized;
          break;
        }
        case 'struct_open': {
          settle();
          var snode = { type: 'struct', children: [] };
          attach(snode);
          stack.push({ kind: 'struct', node: snode });
          key = null;
          break;
        }
        // Close anything still open inside the struct before popping it: the C
        // stream emits array_row_end → struct_close, so an array that is the
        // struct's last field is still on the stack here.
        case 'struct_close':
          while (stack.length > 1 && top().kind === 'array') closeArray(stack.pop());
          if (stack.length > 1) stack.pop();
          break;
        case 'array_row_start': {
          /* Pop finished sub-arrays — but never one waiting for its next
             '/'-dimension row, which this event is about to open. */
          while (top().kind === 'array' && !top().row && !top().pendingDim)
            closeArray(stack.pop());
          if (top().kind === 'array' && top().pendingDim) {
            top().pendingDim = false;
            top().row = { elements: [] };
            top().node.rows.push(top().row);
            break;
          }
          /* A new array: an element of the open row (a nested sub-array), or the
             value of the pending assignment. A sub-array shares its parent's
             assignment so the element annotation the C emitted in the innermost
             first row still lands on the array that was assigned. */
          var anode = { type: 'array', dims: 0, rows: [] };
          var parent = top();
          var assign = attach(anode);
          if (!assign && parent.kind === 'array') assign = parent.assign;
          var frame = { kind: 'array', node: anode, row: { elements: [] },
                        assign: assign, synth: synth, pendingDim: false };
          anode.rows.push(frame.row);
          stack.push(frame);
          break;
        }
        case 'array_row_end':
          settle();                    /* the innermost sub-array closed first */
          if (top().kind === 'array') top().row = null;
          break;
        case 'array_dim_start':
          /* The next row continues THIS array rather than starting a new one. */
          if (top().kind === 'array') top().pendingDim = true;
          break;
        case 'octet_start': {
          settle();
          /* attached here, not at octet_end, so its chunks accumulate into a node
             that is already parented — an octet stream in an array row is an
             element like any other value */
          oct = { type: 'octet', chunks: [], total: 0 };
          attach(oct);
          break;
        }
        case 'octet_end':
          oct = null;
          break;
        case 'data': {
          if (e.tok === 'octet_stream') {
            if (oct) { oct.chunks.push({ length: e.bytes || 0 }); oct.total += (e.bytes || 0); }
            break;
          }
          settle();
          var node = scalarNode(e);
          var a = attach(node);
          // Only numeric families carry a meaningful (synthesised or explicit)
          // type annotation; reference/symbol/null/string/bool values have no
          // family, so emitting annFromValue for them would render a bogus
          // "synthesised → family vt_undefined" node in the playground tree.
          if (a && NUMERIC[node.familyName]) a.ann = annFromValue(node, synth);
          break;
        }
        case 'stream_end': settle(); break;
        default: break;   /* type_* events are folded into the data node's resolved type */
      }
    }
    /* Resync can truncate a document mid-container. Every node is already
       attached where it belongs, so this only finishes the arrays' bookkeeping. */
    while (stack.length > 1) {
      var f = stack.pop();
      if (f.kind === 'array') closeArray(f);
    }
    return root;
  }
  function mapErrors(errObj) {
    return (errObj.errors || []).map(function (e) {
      return { code: e.error_name, codeNum: e.error, message: e.error_name,
               line: e.line, col: e.column, offset: e.offset, tier: 'stream' };
    });
  }
  /*
   * The streaming pass alone is not the whole of validity: array homogeneity,
   * struct shape and duplicate keys are document-tier rules (spec §7.4, §12.4),
   * checked when the document is materialised. bvnr_read() never sees them, so
   * the playground reported a green "ok · no errors" for documents `bovnar
   * validate` rejects -- `.a = [{.b=3;},{.c=4;}];` (struct_shape_mismatch),
   * `.x=1; .x=2;` (duplicate_struct_key), `[1,"two"]` (array_element_type_mismatch).
   * src/bovnar.c had exactly this hole in `bovnar events` and closed it the same
   * way: run the DOM pass too, and only when the streaming tier came back clean
   * (a resynced document is already reported, and its DOM is not the one the
   * author wrote). The error carries no position -- bvn_dom_doc_get_parse_error
   * is a code, not a site -- so it is flagged as document-tier and rendered
   * without a line:column jump target.
   */
  function docTierError(text) {
    try {
      var d = wasm.toJSON(text);
      if (!d || d.ok || !d.error) return null;
      return { code: d.error_name, codeNum: d.error, message: d.error_name,
               line: 0, col: 0, offset: 0, tier: 'document' };
    } catch (_) { return null; }
  }
  /* Single source of both streams, shared by parseFaithful and parseBovnar.
     bvnr_wasm_parse runs ONE reader carrying both callbacks; the two older
     exports each construct their own reader and walk the whole document, so
     using them together cost two passes and two JSON round-trips. The fallback
     is kept for a module that predates the combined export -- note the probe is
     on wasm.parse, which index.mjs defines only when the compiled core actually
     exports it, so a current wrapper paired with a stale core still falls back
     rather than throwing.

     One-entry memo on the input string: the same document is parsed more than
     once (the bovnar-wasm-ready re-render, an example button re-clicked, and
     every demo step re-parses its just-encoded frame). Keyed on string identity,
     so any real edit misses immediately; one entry, so it cannot grow. */
  var lastText = null, lastRaw = null;
  function rawParse(text) {
    if (text === lastText && lastRaw) return lastRaw;
    var events, errObj;
    if (typeof wasm.parse === 'function') {
      var both = wasm.parse(text);
      events = both.events || [];
      errObj = both;
    } else {
      events = wasm.events(text).events || [];
      errObj = wasm.errors(text);
    }
    var errors = mapErrors(errObj);
    if (!errors.length) {
      var docErr = docTierError(text);
      if (docErr) errors.push(docErr);
    }
    lastRaw = { events: events, errors: errors };
    lastText = text;
    return lastRaw;
  }

  function wasmFaithful(text) {
    var raw = rawParse(text);
    if (!raw.tree) raw.tree = buildTree(raw.events, makeScan(text));
    return raw;
  }

  /* ── parseBovnar: translate the verified event stream to the live cb ─────
   *
   * ev_assignment_end is this shim's own event — bvnr_event_t has no such
   * member — so it is on us to close each assignment exactly once, when its
   * VALUE ends. That needs the same container stack buildTree uses, and for the
   * same reason: a struct or an array inside an array row is an element, not an
   * assignment, and must not close one. A single "an array is open" flag emitted
   * an assignment_end at the first field of a struct inside an array, one more
   * after each element struct, and never closed the assignment that actually
   * owned the array. */
  function wasmBovnar(text, cb) {
    var raw = rawParse(text);
    var events = raw.events;
    var scan = makeScan(text);
    var line = 0;
    function E(type, data) { cb({ type: type, data: data || {}, line: line }); }
    var stack = [{ kind: 'struct' }];
    function top() { return stack[stack.length - 1]; }
    /* true while the innermost container is an array row accepting elements —
       i.e. a value ending right now is an element and closes no assignment */
    function inRow() { var f = top(); return f.kind === 'array' && !!f.row; }
    function endValue() {
      if (inRow()) return;              /* an element closes no assignment */
      E(EV.ASSIGNMENT_END);
      top().pending = false;
    }
    /* In resync mode a malformed assignment is skipped after its
       ev_assignment_start, so its value events never arrive. Close it when the
       next event proves it is over, or the stream would carry an
       ev_assignment_start with no end and a consumer's nesting would drift for
       the rest of the document. */
    function flushPending() { if (!inRow() && top().pending) endValue(); }
    /* close every array whose last row has ended (see buildTree: the C stream
       has no array-end event, so this is deferred until an event proves it) */
    function settle() {
      while (top().kind === 'array' && !top().row) { stack.pop(); endValue(); }
    }
    for (var i = 0; i < events.length; i++) {
      var e = events[i];
      switch (e.ev) {
        case 'stream_start': E(EV.STREAM_START); break;
        case 'assign_start':
          settle();
          flushPending();              /* the previous assignment was resynced away */
          line = scan(e.text).line;
          E(EV.ASSIGNMENT_START, { key: e.text });
          top().pending = true;
          break;
        case 'type_start': E(EV.TYPE_ANN_START); break;
        case 'type_family': E(EV.TYPE_ANN_FAMILY, { family: e.family }); break;
        case 'type_param': {
          var d;
          if (e.tok === 'type_width')      d = { kind: 'width', value: e.width, text: String(e.width) };
          else if (e.tok === 'type_base')  d = { kind: 'base',  value: e.base,  text: '_' + e.base };
          else if (e.tok === 'unit')     { if (e.text === 'no_unit') break;   // synthesized "no unit" sentinel — not a real unit
                                           d = { kind: 'unit', text: e.text }; }
          else if (e.tok === 'type_q')     d = { kind: 'q',     text: e.text };
          else                             d = { kind: 'param', text: e.text };
          E(EV.TYPE_ANN_PARAM, d);
          break;
        }
        case 'type_end': E(EV.TYPE_ANN_END); break;
        case 'struct_open':
          settle();
          E(EV.STRUCT_START);
          stack.push({ kind: 'struct' });
          break;
        // Close a struct-trailing array (array_row_end → struct_close) before
        // closing the struct, so it stays parented to this struct rather than
        // the outer scope.
        case 'struct_close':
          while (stack.length > 1 && top().kind === 'array') { stack.pop(); endValue(); }
          flushPending();      /* last field resynced away */
          E(EV.STRUCT_END);
          if (stack.length > 1) stack.pop();
          endValue();          /* a struct that is an array element closes nothing */
          break;
        case 'array_row_start':
          /* pop finished sub-arrays, but never one awaiting its '/'-dimension row */
          while (top().kind === 'array' && !top().row && !top().pendingDim) {
            stack.pop(); endValue();
          }
          if (top().kind === 'array' && top().pendingDim) {
            top().pendingDim = false;
            top().row = true;
          } else {
            stack.push({ kind: 'array', row: true, pendingDim: false });
          }
          E(EV.ARRAY_ROW_START);
          break;
        case 'array_row_end':
          settle();                      /* the innermost sub-array closed first */
          E(EV.ARRAY_ROW_END);
          if (top().kind === 'array') top().row = false;
          break;
        case 'array_dim_start':
          if (top().kind === 'array') top().pendingDim = true;
          E(EV.ARRAY_DIM_START);
          break;
        case 'octet_start': settle(); E(EV.OCTET_STREAM_START); break;
        case 'octet_end': E(EV.OCTET_STREAM_END); endValue(); break;
        case 'data':
          if (e.tok === 'octet_stream') {
            E(EV.DATA, { kind: 'octet', value: null, text: '', length: e.bytes });
          } else {
            settle();
            E(EV.DATA, { kind: dataKind(e), value: dataValue(e),
                         text: e.text != null ? e.text : '', unit: e.unit || null });
            endValue();
          }
          break;
        case 'stream_end':
          settle();
          flushPending();
          E(EV.STREAM_END);
          break;
        default: break;
      }
    }
    // Same pass as the events above -- this was a second full walk of the
    // document. mapErrors already normalised error_name onto .code/.message.
    var errs = raw.errors;
    for (var k = 0; k < errs.length; k++)
      cb({ type: EV.ERROR, data: { msg: errs[k].message, code: errs[k].code }, line: errs[k].line });
  }

  function peekVersion(text) {
    var m = /^﻿?\s*#!bovnar[ \t]+(\d+)\.(\d+)[ \t]*$/m.exec(text || '');
    if (!m || /^0\d/.test(m[1]) || /^0\d/.test(m[2])) return null;
    return { major: parseInt(m[1], 10), minor: parseInt(m[2], 10) };
  }

  /* ── dispatcher: WASM once ready, empty results until then ───────────────── */
  function parseFaithful(text) {
    // A throw inside the shim used to return the same empty result as "WASM not
    // loaded yet", so the playground reported a green "ok - 0 events - no errors"
    // for a document it had actually failed on. Flag it so the caller can tell.
    var failed = false;
    if (wasm) {
      try { return wasmFaithful(text); }
      catch (e) { failed = true; if (window.console) console.warn('bovnar wasm parseFaithful failed', e); }
    }
    return { events: [], errors: [], tree: { type: 'stream', children: [] }, failed: failed };
  }
  function parseBovnar(text, cb) {
    if (wasm) { try { return wasmBovnar(text, cb); } catch (e) { if (window.console) console.warn('bovnar wasm parseBovnar failed', e); } }
  }

  window.BovnarParser = { parseFaithful: parseFaithful, parseBovnar: parseBovnar,
                          peekVersion: peekVersion, EV: EV,
                          isWasmReady: function () { return !!wasm; } };
})();
