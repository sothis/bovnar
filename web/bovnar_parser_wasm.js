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
  /*
   * value_type_spec_t.base is three different things depending on the family,
   * and the C says which by the parameter event it emits beside it: type_base
   * for a real radix (uint/sint/float), type_q for float_fix's fixed-point
   * scaling, and nothing at all for float_dec. A datetime puts its epoch there
   * as a small dense index (bovnar.h: 0 = unix, 1 = tai) and reports the name
   * separately. Reconstructing the annotation from the resolved value therefore
   * has to branch the same way the reader does, or `<float_fix:16,q8,°C>` reads
   * as "base 8" and `<datetime:64,tai>` as "base 1" — a radix neither value has.
   *
   * A radix of 0 means "no explicit base", which resolves to decimal: the CLI
   * prints that as <uint:64,_10,no_unit>, and so does this.
   */
  function annFromValue(v, synthesized) {
    var params = [];
    if (v.width != null) params.push({ kind: 'width', text: String(v.width), value: v.width });
    if (v.familyName === 'float_fix') {
      if (v.base != null) params.push({ kind: 'q', text: 'q' + v.base, value: v.base });
    } else if (v.familyName !== 'datetime' && v.familyName !== 'float_dec' && v.base != null) {
      var base = v.base || 10;
      params.push({ kind: 'base', text: '_' + base, value: base });
    }
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
    /* The unit exactly as the current annotation spells it. bvn_unit_to_string
       canonicalises (`m·s⁻¹` and `deg` come back as `m/s` and `°`), and a reader
       echoing a document back — the demos print the decoded unit beside the
       value — has to be able to show the spelling that was written. Only ever
       attached to a value that HAS a unit: a datetime's epoch arrives in the
       same parameter slot, and `<datetime:64,tai>` does not make `tai` a unit. */
    var annUnit = null;
    var oct = null;       // the octet node currently collecting chunks
    /* struct_close events already spent by a resynced array_row_end (below) */
    var swallowStructClose = 0;
    function top() { return stack[stack.length - 1]; }
    /* the row currently accepting elements, or null between rows / in a struct */
    function openRow() { var f = top(); return f.kind === 'array' ? f.row : null; }
    function closeArray(f) {
      /* bvn_dom_array_dims() is the '/'-row count (src/dom/bovnar_dom_builder.c),
         which is exactly the number of rows opened here. */
      f.node.dims = f.node.rows.length;
      var e0 = f.node.rows[0] && f.node.rows[0].elements[0];
      if (f.assign && f.unitText && !f.assign.unitText && e0 && e0.unit)
        f.assign.unitText = f.unitText;
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
    function haveArray() {
      for (var k = 0; k < stack.length; k++) if (stack[k].kind === 'array') return true;
      return false;
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
          annUnit = null;
          break;
        }
        case 'type_start': annUnit = null; break;
        case 'type_param':
          if (e.tok === 'unit' && e.text && e.text !== 'no_unit') annUnit = e.text;
          break;
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
          /* Already closed by the row end that preceded it — spending it again
             would pop the ENCLOSING struct, and every field after it (`.z` in
             `.s = {.a = [{bad}]; .z = 5;};`) would leave the struct it is
             written in. */
          if (swallowStructClose) { swallowStructClose--; break; }
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
          /* An EXPLICIT annotation precedes the row; a synthesised one is emitted
             inside it, after this point — so annUnit here is the spelling the
             document actually wrote, and null when it wrote none. */
          var frame = { kind: 'array', node: anode, row: { elements: [] },
                        assign: assign, synth: synth, unitText: annUnit,
                        pendingDim: false };
          anode.rows.push(frame.row);
          stack.push(frame);
          break;
        }
        case 'array_row_end':
          /* Resync unwinds a broken element in the opposite order: the reader
             ends the ROW while the element struct is still open, and only then
             emits struct_close. Taken literally that leaves the row open for the
             rest of the document, and every following assignment lands in it as
             an element — `.a = [{.b = <uint:8> 999;}, …]; .c = 42; .d = 7;` came
             back as a three-element array with no .c and no .d at all. The row
             ends where it was opened, so unwind to that array. */
          if (haveArray()) while (top().kind !== 'array') { stack.pop(); swallowStructClose++; }
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
          if (a && annUnit && node.unit) a.unitText = annUnit;
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
   *
   * Called from parseFaithful, NOT from the shared rawParse: the playground is
   * the only surface that shows this channel, and the demos re-parse a freshly
   * encoded frame every animation step -- a text the memo below can never hit.
   * The pass costs ~64 us against ~204 us for the parse itself on a demo-sized
   * frame, so charging it to the demos would be a third more work per frame for
   * a verdict nothing there displays.
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
    lastRaw = { events: events, errors: mapErrors(errObj) };
    lastText = text;
    return lastRaw;
  }

  function wasmFaithful(text, opts) {
    var raw = rawParse(text);
    if (!raw.tree) raw.tree = buildTree(raw.events, makeScan(text));
    if (!(opts && opts.documentTier))
      return { events: raw.events, errors: raw.errors, tree: raw.tree };
    /* Memoised beside the tree, and kept OUT of raw.errors: that array is what
       the demos read, and they have no channel for a positionless
       document-tier error -- they would refuse a config over an error reported
       at "line 0", and pay for the extra DOM pass on every animation frame. */
    if (raw.faithfulErrors === undefined) {
      var docErr = raw.errors.length ? null : docTierError(text);
      raw.faithfulErrors = docErr ? raw.errors.concat([docErr]) : raw.errors;
    }
    return { events: raw.events, errors: raw.faithfulErrors, tree: raw.tree };
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
    function haveArray() {
      for (var k = 0; k < stack.length; k++) if (stack[k].kind === 'array') return true;
      return false;
    }
    /* Close what is still open. A document that dies mid-container gets no
       ev_stream_end at all — the reader stops at the error — so without this the
       last assignment, struct and row are never closed and a consumer's nesting
       never returns to the top. ev_stream_end itself is NOT synthesised: its
       absence is how the C says the document was cut short. */
    function drain() {
      while (stack.length > 1) {
        var f = stack.pop();
        if (f.kind === 'array') { if (f.row) E(EV.ARRAY_ROW_END); }
        else E(EV.STRUCT_END);
        endValue();
      }
      flushPending();
    }
    var sawStreamEnd = false;
    /* struct_close events already accounted for by a resynced array_row_end */
    var swallowStructClose = 0;
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
          if (swallowStructClose) { swallowStructClose--; break; }
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
          /* see buildTree: resync ends the row while the element struct is still
             open. Close that struct here — the row it lives in is over — and
             swallow the struct_close that follows, so the stream stays nested
             instead of ending a row inside a struct it also never left. */
          while (haveArray() && top().kind !== 'array') {
            flushPending();              /* the field the resync abandoned */
            stack.pop();
            E(EV.STRUCT_END);
            swallowStructClose++;
            endValue();
          }
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
          drain();
          sawStreamEnd = true;
          E(EV.STREAM_END);
          break;
        default: break;
      }
    }
    if (!sawStreamEnd) drain();
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
  /*
   * parseFaithful(text[, { documentTier: true }]) -> { events, errors, tree }
   *
   * The document tier is opt-in because it is a second full pass (bvn_dom_parse
   * plus the JSON projection, ~64 us against ~204 us for the parse itself on a
   * demo-sized frame) and its finding has no line:column. The playground asks
   * for it; the demos, which re-parse a freshly encoded frame every animation
   * step, do not.
   */
  function parseFaithful(text, opts) {
    // A throw inside the shim used to return the same empty result as "WASM not
    // loaded yet", so the playground reported a green "ok - 0 events - no errors"
    // for a document it had actually failed on. Flag it so the caller can tell.
    var failed = false;
    if (wasm) {
      try { return wasmFaithful(text, opts); }
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
