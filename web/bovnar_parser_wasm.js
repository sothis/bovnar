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
  import('./bovnar_wasm.js?v=0da54e70edd1')
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
    function lineAt(idx) {
      var l = 1;
      for (var i = 0; i < idx && i < masked.length; i++) if (masked.charCodeAt(i) === 10) l++;
      return l;
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

  /* ── parseFaithful: assemble the tree from the verified event stream ───── */
  function buildTree(events, scan) {
    var root = { type: 'stream', children: [] };
    var stack = [root];
    var key = null, line = 1, synth = false;
    var arr = null;       // { assign, value } while building an array
    var oct = null;       // { assign, value } while building an octet stream
    function top() { return stack[stack.length - 1]; }
    function finishArray() {
      if (arr) { arr.value.dims = arr.value.rows.length; top().children.push(arr.assign); arr = null; }
    }
    for (var i = 0; i < events.length; i++) {
      var e = events[i];
      switch (e.ev) {
        case 'assign_start': {
          finishArray();
          var s = scan(e.text); key = e.text; line = s.line; synth = s.synthesized;
          break;
        }
        case 'struct_open': {
          var snode = { type: 'struct', children: [] };
          top().children.push({ type: 'assignment', key: key, line: line, col: 1, ann: null, value: snode });
          stack.push(snode); key = null;
          break;
        }
        // Commit a pending array BEFORE popping the struct: the C stream emits
        // array_row_end → struct_close, so an array that is the struct's last field
        // would otherwise be finished (on the next assign_start / stream_end) after
        // its parent struct is already popped, mis-parenting it to the outer scope.
        case 'struct_close': finishArray(); stack.pop(); break;
        case 'array_row_start': {
          if (!arr) {
            var anode = { type: 'array', dims: 0, rows: [] };
            arr = { assign: { type: 'assignment', key: key, line: line, col: 1, ann: null, value: anode }, value: anode };
          }
          arr.value.rows.push({ elements: [] });
          break;
        }
        case 'octet_start': {
          var onode = { type: 'octet', chunks: [], total: 0 };
          oct = { assign: { type: 'assignment', key: key, line: line, col: 1, ann: null, value: onode }, value: onode };
          key = null;
          break;
        }
        case 'octet_end':
          if (oct) { top().children.push(oct.assign); oct = null; }
          break;
        case 'data': {
          if (e.tok === 'octet_stream') {
            if (oct) { oct.value.chunks.push({ length: e.bytes || 0 }); oct.value.total += (e.bytes || 0); }
            break;
          }
          var node = scalarNode(e);
          if (arr) {
            arr.value.rows[arr.value.rows.length - 1].elements.push(node);
          } else {
            // Only numeric families carry a meaningful (synthesised or explicit)
            // type annotation; reference/symbol/null/string/bool values have no
            // family, so emitting annFromValue for them would render a bogus
            // "synthesised → family vt_undefined" node in the playground tree.
            top().children.push({ type: 'assignment', key: key, line: line, col: 1,
                                  ann: NUMERIC[node.familyName] ? annFromValue(node, synth) : null,
                                  value: node });
            key = null;
          }
          break;
        }
        case 'stream_end': finishArray(); break;
        default: break;   /* type_* events are folded into the data node's resolved type */
      }
    }
    finishArray();
    return root;
  }
  function mapErrors(errObj) {
    return (errObj.errors || []).map(function (e) {
      return { code: e.error_name, codeNum: e.error, message: e.error_name,
               line: e.line, col: e.column, offset: e.offset };
    });
  }
  function wasmFaithful(text) {
    var events = wasm.events(text).events || [];
    var tree = buildTree(events, makeScan(text));
    return { events: events, errors: mapErrors(wasm.errors(text)), tree: tree };
  }

  /* ── parseBovnar: translate the verified event stream to the live cb ───── */
  function wasmBovnar(text, cb) {
    var events = wasm.events(text).events || [];
    var scan = makeScan(text);
    var line = 0, arrayOpen = false;
    function E(type, data) { cb({ type: type, data: data || {}, line: line }); }
    for (var i = 0; i < events.length; i++) {
      var e = events[i];
      switch (e.ev) {
        case 'stream_start': E(EV.STREAM_START); break;
        case 'assign_start':
          if (arrayOpen) { E(EV.ASSIGNMENT_END); arrayOpen = false; }
          line = scan(e.text).line;
          E(EV.ASSIGNMENT_START, { key: e.text });
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
        case 'struct_open': E(EV.STRUCT_START); break;
        case 'struct_close':
          // Flush a struct-trailing array (array_row_end → struct_close) before
          // closing, so it stays parented to this struct rather than the outer scope.
          if (arrayOpen) { E(EV.ASSIGNMENT_END); arrayOpen = false; }
          E(EV.STRUCT_END); E(EV.ASSIGNMENT_END); break;
        case 'array_row_start': E(EV.ARRAY_ROW_START); arrayOpen = true; break;
        case 'array_row_end': E(EV.ARRAY_ROW_END); break;
        case 'array_dim_start': E(EV.ARRAY_DIM_START); break;
        case 'octet_start': E(EV.OCTET_STREAM_START); break;
        case 'octet_end': E(EV.OCTET_STREAM_END); E(EV.ASSIGNMENT_END); break;
        case 'data':
          if (e.tok === 'octet_stream') {
            E(EV.DATA, { kind: 'octet', value: null, text: '', length: e.bytes });
          } else {
            E(EV.DATA, { kind: dataKind(e), value: dataValue(e),
                         text: e.text != null ? e.text : '', unit: e.unit || null });
            if (!arrayOpen) E(EV.ASSIGNMENT_END);
          }
          break;
        case 'stream_end':
          if (arrayOpen) { E(EV.ASSIGNMENT_END); arrayOpen = false; }
          E(EV.STREAM_END);
          break;
        default: break;
      }
    }
    var errs = wasm.errors(text).errors || [];
    for (var k = 0; k < errs.length; k++)
      cb({ type: EV.ERROR, data: { msg: errs[k].error_name, code: errs[k].error_name }, line: errs[k].line });
  }

  function peekVersion(text) {
    var m = /^﻿?\s*#!bovnar[ \t]+(\d+)\.(\d+)[ \t]*$/m.exec(text || '');
    if (!m || /^0\d/.test(m[1]) || /^0\d/.test(m[2])) return null;
    return { major: parseInt(m[1], 10), minor: parseInt(m[2], 10) };
  }

  /* ── dispatcher: WASM once ready, empty results until then ───────────────── */
  function parseFaithful(text) {
    if (wasm) { try { return wasmFaithful(text); } catch (e) { if (window.console) console.warn('bovnar wasm parseFaithful failed', e); } }
    return { events: [], errors: [], tree: { type: 'stream', children: [] } };
  }
  function parseBovnar(text, cb) {
    if (wasm) { try { return wasmBovnar(text, cb); } catch (e) { if (window.console) console.warn('bovnar wasm parseBovnar failed', e); } }
  }

  window.BovnarParser = { parseFaithful: parseFaithful, parseBovnar: parseBovnar,
                          peekVersion: peekVersion, EV: EV,
                          isWasmReady: function () { return !!wasm; } };
})();
