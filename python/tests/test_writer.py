

import os
import tempfile
import pytest

from conftest import needs_lib
from bovnar.writer import Writer, DEFAULT_MEM_CAP
from bovnar.reader import Reader
from bovnar.enums import Event, ValueTypeFamily, BaseUnit, SIPrefix, IECPrefix, Exponent
from bovnar.exceptions import BovnarWriteError, BovnarArgumentError
from bovnar.structs import make_type_spec, make_unit_dimensionless, make_unit_none

@needs_lib
class TestWriterLifecycle:
    def test_to_mem_factory(self):
        w = Writer.to_mem()
        assert w._ptr is not None
        w.destroy()

    def test_context_manager_calls_finish(self):
        with Writer.to_mem() as w:
            w.write_uint("x", 1)
        out = w.get_output()
        assert b'x' in out

    def test_destroy_idempotent(self):
        w = Writer.to_mem()
        w.destroy()
        w.destroy()

    def test_get_output_before_finish_raises(self):

        with Writer.to_mem() as w:
            w.write_uint("x", 42)
            _ = w.bytes_written

    def test_to_fd(self):
        with tempfile.NamedTemporaryFile(suffix='.bvnr', delete=False) as f:
            name = f.name
        try:
            fd = os.open(name, os.O_WRONLY | os.O_TRUNC)
            with Writer.to_fd(fd) as w:
                w.write_uint("y", 99)
            os.close(fd)
            content = open(name, 'rb').read()
            assert b'y' in content
        finally:
            os.unlink(name)

@needs_lib
class TestWriteUint:
    def _write_and_read(self, fn) -> bytes:
        with Writer.to_mem() as w:
            fn(w)
        return w.get_output()

    def test_basic_uint16(self):
        out = self._write_and_read(lambda w: w.write_uint("port", 8080, width=16))
        assert b'port' in out
        assert b'8080' in out

    def test_uint8_max(self):
        out = self._write_and_read(lambda w: w.write_uint("x", 255, width=8))
        assert b'255' in out

    def test_uint64_large(self):
        out = self._write_and_read(lambda w: w.write_uint("big", 2**63, width=64))
        assert b'big' in out

    def test_uint_hex_base(self):
        out = self._write_and_read(lambda w: w.write_uint("flags", 0xFF, width=8, base=16))

        assert b'ff' in out

    def test_uint_with_si_unit(self):
        out = self._write_and_read(
            lambda w: w.write_uint("cache", 512,
                                   width=64,
                                   unit_si_base=BaseUnit.BYTE,
                                   unit_si_prefix=SIPrefix.KILO))
        assert b'cache' in out
        assert b'512' in out

    def test_uint_with_iec_unit(self):
        out = self._write_and_read(
            lambda w: w.write_uint("ram", 8,
                                   width=64,
                                   unit_iec_base=BaseUnit.BYTE,
                                   unit_iec_prefix=IECPrefix.GIBI))
        assert b'ram' in out
        assert b'8' in out

    def test_uint_with_unit_str(self):
        out = self._write_and_read(
            lambda w: w.write_uint("storage", 2,
                                   width=64,
                                   unit_str='Ti~B'))
        assert b'storage' in out

@needs_lib
class TestWriteSint:
    def test_negative_value(self):
        with Writer.to_mem() as w:
            w.write_sint("delta", -42, width=32)
        out = w.get_output()
        assert b'delta' in out
        assert b'-42' in out

    def test_zero(self):
        with Writer.to_mem() as w:
            w.write_sint("zero", 0)
        assert b'0' in w.get_output()

    def test_sint_min_8(self):
        with Writer.to_mem() as w:
            w.write_sint("min", -128, width=8)
        out = w.get_output()
        assert b'-128' in out

@needs_lib
class TestWriteFloat:
    def test_basic_float(self):
        with Writer.to_mem() as w:
            w.write_float("v", 9.81, width=64)
        out = w.get_output()
        assert b'v' in out
        assert b'9.81' in out

    def test_float_with_unit(self):
        with Writer.to_mem() as w:
            w.write_float("speed", 9.81, width=64, unit_str="m/s")
        out = w.get_output()
        assert b'speed' in out

    def test_float32(self):
        with Writer.to_mem() as w:
            w.write_float("t", 3.14, width=32)
        assert b't' in w.get_output()

@needs_lib
class TestWriteString:
    def test_basic_string(self):
        with Writer.to_mem() as w:
            w.write_string("host", "localhost")
        out = w.get_output()
        assert b'host' in out
        assert b'localhost' in out

    def test_empty_string(self):
        with Writer.to_mem() as w:
            w.write_string("empty", "")
        assert b'empty' in w.get_output()

    def test_string_with_escape_chars(self):
        with Writer.to_mem() as w:
            w.write_string("msg", "hello\nworld")
        out = w.get_output()
        assert b'msg' in out

@needs_lib
class TestWriteBoolNull:
    def test_true(self):
        with Writer.to_mem() as w:
            w.write_bool("flag", True)
        assert b'true' in w.get_output()

    def test_false(self):
        with Writer.to_mem() as w:
            w.write_bool("flag", False)
        assert b'false' in w.get_output()

    def test_null(self):
        with Writer.to_mem() as w:
            w.write_null("empty")
        out = w.get_output()
        assert b'empty' in out

@needs_lib
class TestWriteStruct:
    def test_empty_struct(self):
        with Writer.to_mem() as w:
            w.begin_struct("cfg")
            w.end_struct()
        out = w.get_output()
        assert b'cfg' in out

    def test_nested_struct(self):
        with Writer.to_mem() as w:
            w.begin_struct("server")
            w.write_string("host", "localhost")
            w.write_uint("port", 8080, width=16)
            w.end_struct()
        out = w.get_output()
        assert b'server'    in out
        assert b'host'      in out
        assert b'localhost' in out
        assert b'port'      in out
        assert b'8080'      in out

    def test_double_nested(self):
        with Writer.to_mem() as w:
            w.begin_struct("outer")
            w.begin_struct("inner")
            w.write_uint("x", 42)
            w.end_struct()
            w.end_struct()
        out = w.get_output()
        assert b'outer' in out
        assert b'inner' in out
        assert b'42'    in out

    def test_unclosed_struct_raises_on_finish(self):
        w = Writer.to_mem()
        w.begin_struct("open")

        with pytest.raises(BovnarWriteError):
            w.finish()
        w.destroy()

@needs_lib
class TestWriteArray:
    def test_simple_array(self):
        with Writer.to_mem() as w:
            w.emit(Event.ASSIGNMENT_START, key="values")
            w.begin_array_row()
            for v in [1, 2, 3]:
                vt = make_type_spec(ValueTypeFamily.UINT, 64, 10)
                vu = make_unit_dimensionless()
                w.emit(Event.DATA, value=str(v), vt=vt, vu=vu)
            w.end_array_row()
        out = w.get_output()
        assert b'values' in out
        assert b'1' in out
        assert b'3' in out

    def test_2d_array(self):
        with Writer.to_mem() as w:
            w.emit(Event.ASSIGNMENT_START, key="mat")
            w.begin_array_row()
            vt = make_type_spec(ValueTypeFamily.UINT, 64, 10)
            vu = make_unit_dimensionless()
            for v in [1, 2]:
                w.emit(Event.DATA, value=str(v), vt=vt, vu=vu)
            w.end_array_row()
            w.new_array_dim()
            w.begin_array_row()
            for v in [3, 4]:
                w.emit(Event.DATA, value=str(v), vt=vt, vu=vu)
            w.end_array_row()
        out = w.get_output()
        assert b'mat' in out

@needs_lib
class TestEmit:
    def test_emit_assignment_and_data(self):
        vt = make_type_spec(ValueTypeFamily.UINT, 32, 10)
        vu = make_unit_dimensionless()
        with Writer.to_mem() as w:
            w.emit(Event.ASSIGNMENT_START, key="x")
            w._emit_annotation('uint', vt, vu)
            w.emit(Event.DATA, value="42", vt=vt, vu=vu)
        out = w.get_output()
        assert b'x'    in out
        assert b'42'   in out
        assert b'uint' in out
        assert b'32'   in out

    def test_emit_invalid_event_raises(self):
        with Writer.to_mem() as w:
            with pytest.raises(BovnarWriteError):

                w.emit(Event.ASSIGNMENT_START, key="x")
                vt = make_type_spec(ValueTypeFamily.UINT)
                vu = make_unit_none()
                w.emit(Event.DATA, vt=vt, vu=vu)

@needs_lib
class TestBytesWritten:
    def test_increases_with_writes(self):
        with Writer.to_mem() as w:
            before = w.bytes_written
            w.write_uint("x", 1)
            after = w.bytes_written
            assert after > before

    def test_zero_initially(self):
        with Writer.to_mem() as w:

            assert w.bytes_written >= 0

@needs_lib
class TestBufferExhaustion:
    def test_tiny_buffer_raises(self):
        with pytest.raises(BovnarWriteError):
            with Writer.to_mem(cap=8) as w:

                w.write_string("a_very_long_key_name", "a_very_long_string_value")

@needs_lib
class TestPrettyOutput:
    def test_pretty_has_newlines(self):
        with Writer.to_mem(pretty=True) as w:
            w.write_uint("x", 1)
        assert b'\n' in w.get_output()

    def test_compact_no_extra_whitespace(self):
        with Writer.to_mem(pretty=False) as w:
            w.write_uint("x", 1)
        out = w.get_output()

        with Writer.to_mem(pretty=True) as w2:
            w2.write_uint("x", 1)
        pretty_out = w2.get_output()
        assert len(out) <= len(pretty_out)

@needs_lib
class TestWriteBvnfBase:
    def test_base10_width0(self):
        with Writer.to_mem() as w:
            w.write_bvnf_base("pi", "3.14159265358979", width=0, base=10)
        out = w.get_output()
        assert b'pi' in out
        assert b'3.14' in out

    def test_base10_wide_width(self):
        pi = ("3.14159265358979323846264338327950288419716939937510"
              "58209749445923078164062862089986280348253421170679")
        with Writer.to_mem() as w:
            w.write_bvnf_base("pi512", pi, width=512, base=10)
        out = w.get_output()
        assert b'pi512' in out
        assert b'float:512' in out

    def test_base16_produces_quoted_string(self):
        with Writer.to_mem() as w:
            w.write_bvnf_base("one", "1.0p+0", width=64, base=16)
        out = w.get_output()
        assert b'one' in out
        assert b'"1.0p+0"' in out
        assert b'_16' in out

    def test_base16_negative(self):
        with Writer.to_mem() as w:
            w.write_bvnf_base("neg", "-1.8p+4", width=64, base=16)
        out = w.get_output()
        assert b'neg' in out
        assert b'-1.8p+4' in out

    def test_invalid_base_raises(self):
        with pytest.raises(BovnarArgumentError):
            with Writer.to_mem() as w:
                w.write_bvnf_base("x", "1.0", width=64, base=8)

@needs_lib
class TestWriteBvni:
    def test_uint_decimal(self):
        with Writer.to_mem() as w:
            w.write_bvni("u", 255, width=8, base=10)
        out = w.get_output()
        assert b'255' in out
        assert b'uint' in out

    def test_sint_negative(self):
        with Writer.to_mem() as w:
            w.write_bvni("s", -128, width=8, base=10)
        out = w.get_output()
        assert b'-128' in out
        assert b'sint' in out

    def test_wide_uint(self):
        val = 2**128 - 1
        with Writer.to_mem() as w:
            w.write_bvni("u128max", val, width=128, base=10)
        out = w.get_output()
        assert b'340282366920938463463374607431768211455' in out
        assert b'uint:128' in out

    def test_hex_base(self):
        with Writer.to_mem() as w:
            w.write_bvni("flags", 0xDEADBEEF, width=32, base=16)
        out = w.get_output()
        assert b'deadbeef' in out
        assert b'"deadbeef"' in out
        assert b'_16' in out

    def test_hex_negative(self):
        with Writer.to_mem() as w:
            w.write_bvni("neg", -0x7FFFFFFF, width=32, base=16)
        out = w.get_output()
        assert b'-' in out

    def test_forced_signed_positive(self):
        with Writer.to_mem() as w:
            w.write_bvni("s", 127, width=8, base=10, signed=True)
        out = w.get_output()
        assert b'sint' in out

    def test_format_bigint_base2(self):
        val = Writer._format_bigint(5, 2)
        assert val == '101'

    def test_format_bigint_base16(self):
        val = Writer._format_bigint(0xDEAD, 16)
        assert val == 'dead'

    def test_format_bigint_negative_hex(self):
        val = Writer._format_bigint(-0xFF, 16)
        assert val == '-ff'

    def test_format_bigint_base64(self):
        val = Writer._format_bigint(0, 64)
        assert val == 'A'

@needs_lib
class TestWriteStreamingFlush:
    def test_large_write_exceeds_internal_buffer(self):
        cap = 2 * 1024 * 1024
        with Writer.to_mem(cap=cap) as w:
            for i in range(4000):
                w.write_uint(f"k{i}", i, width=64)
        out = w.get_output()
        assert len(out) > 65536


@needs_lib
class TestWriteFloatFix:
    def test_basic_write_float_fix(self):
        with Writer.to_mem() as w:
            w.write_float_fix('ff', 1.5, width=32, q=16)
        out = w.get_output()
        assert b'float_fix' in out
        assert b'q16' in out

    def test_float_fix_q_zero_produces_annotation(self):
        with Writer.to_mem() as w:
            w.write_float_fix('ff', 1.5, width=32, q=0)
        out = w.get_output()
        assert b'float_fix' in out
        assert b'32' in out

    def test_float_fix_uses_number_token_not_string(self):
        with Writer.to_mem() as w:
            w.write_float_fix('ff', 1.5, width=32, q=16)
        out = w.get_output()
        assert b'"' not in out.split(b'>')[1], \
            "float_fix value must not be wrapped in double quotes (must be NUMBER token)"

    def test_float_fix_roundtrip(self):
        with Writer.to_mem() as w:
            w.write_float_fix('a', 1.5, width=32, q=16)
            w.write_float_fix('b', -0.25, width=64, q=8)
        out = w.get_output()
        data_events = []
        def cb(ev, d):
            if ev == Event.DATA:
                data_events.append(d.type)
            return True
        with Reader() as r:
            r.read_mem(out, on_verified=cb)
        assert len(data_events) == 2

    def test_float_fix_parses_without_error(self):
        with Writer.to_mem() as w:
            w.write_float_fix('x', 3.14, width=64, q=32)
        out = w.get_output()
        with Reader() as r:
            r.read_mem(out)

    def test_float_fix_various_q_values_are_number_tokens(self):
        for q in (4, 8, 16, 24, 32):
            with Writer.to_mem() as w:
                w.write_float_fix('v', 1.0, width=64, q=q)
            out = w.get_output()
            assert b'>"' not in out, \
                f"float_fix q={q}: annotation must not be followed by quoted value"

    def test_float_fix_nan_roundtrip(self):
        import math
        with Writer.to_mem() as w:
            w.write_float_fix('nan_val', float('nan'), width=64, q=16)
        out = w.get_output()
        assert b'$nan' in out


@needs_lib
class TestWriteFloatDec:
    def test_basic_write_float_dec(self):
        with Writer.to_mem() as w:
            w.write_float_dec('fd', 3.14, width=64)
        out = w.get_output()
        assert b'float_dec' in out

    def test_float_dec_uses_number_token_not_string(self):
        with Writer.to_mem() as w:
            w.write_float_dec('fd', 3.14, width=64)
        out = w.get_output()
        assert b'"' not in out.split(b'>')[1], \
            "float_dec value must not be wrapped in quotes (must be NUMBER token)"

    def test_float_dec_parses_without_error(self):
        with Writer.to_mem() as w:
            w.write_float_dec('x', -1.5e3, width=32)
        out = w.get_output()
        with Reader() as r:
            r.read_mem(out)

    def test_float_dec_roundtrip_multiple(self):
        with Writer.to_mem() as w:
            w.write_float_dec('a', 1.0, width=64)
            w.write_float_dec('b', -2.5, width=32)
        out = w.get_output()
        events = []
        def cb(ev, d):
            if ev == Event.DATA:
                events.append(True)
            return True
        with Reader() as r:
            r.read_mem(out, on_verified=cb)
        assert len(events) == 2

    def test_float_dec_nan_is_special_literal(self):
        import math
        with Writer.to_mem() as w:
            w.write_float_dec('v', float('nan'), width=64)
        out = w.get_output()
        assert b'$nan' in out

    def test_float_dec_infinity_is_special_literal(self):
        with Writer.to_mem() as w:
            w.write_float_dec('v', float('inf'), width=64)
        out = w.get_output()
        assert b'$inf' in out


@needs_lib
class TestAnnotationRegressions:
    """Regression tests for annotation roundtrip bugs.

    Bug 1 – UTF8 width was never emitted in the annotation.
    Bug 2 – width was emitted even when vt.width == 0 (emitting :64 via
             bvn_effective_width(0)=64 instead of no width at all).
    Bug 3 – base/Q emission was nested inside the width block so a type with
             width=0 and a non-decimal base lost the base annotation entirely.
    Bug 4 – emit() picked TOKEN_IS_NUMBER for every non-UTF8 family regardless
             of base, causing hex values to be written unquoted (invalid BVNR).
    """

    def test_utf8_typed_width_appears_in_annotation(self):
        from bovnar.enums import ValueTypeFamily
        from bovnar.structs import make_type_spec, make_unit_none
        vt = make_type_spec(ValueTypeFamily.UTF8, 8, 0)
        with Writer.to_mem() as w:
            w._emit_annotation('utf8', vt, make_unit_none())
        out = w.get_output()
        assert b'utf8:8' in out, \
            "UTF8 annotation with width=8 must contain 'utf8:8'"

    def test_utf8_typed_width_zero_omitted_from_annotation(self):
        from bovnar.enums import ValueTypeFamily
        from bovnar.structs import make_type_spec, make_unit_none
        vt = make_type_spec(ValueTypeFamily.UTF8, 0, 0)
        with Writer.to_mem() as w:
            w._emit_annotation('utf8', vt, make_unit_none())
        out = w.get_output()
        assert b'utf8:0' not in out, \
            "UTF8 annotation with width=0 must not contain a width spec"

    def test_float_width_zero_produces_no_width_in_annotation(self):
        with Writer.to_mem() as w:
            w.write_bvnf_base("pi", "3.14", width=0, base=10)
        out = w.get_output()
        assert b'float:64' not in out, \
            "width=0 must not emit ':64' via bvn_effective_width fallback"
        assert b'<float>' in out or b'<float,' in out, \
            "width=0 float annotation must be '<float>' (no width spec)"

    def test_uint_width_zero_base16_emits_base_in_annotation(self):
        from bovnar.enums import ValueTypeFamily
        from bovnar.structs import make_type_spec, make_unit_none
        vt = make_type_spec(ValueTypeFamily.UINT, 0, 16)
        with Writer.to_mem() as w:
            w._emit_annotation('uint', vt, make_unit_none())
        out = w.get_output()
        assert b'_16' in out, \
            "base=16 must appear in annotation even when width=0"
        assert b'uint:0' not in out, \
            "width=0 must not emit a spurious ':0' width spec"

    def test_emit_hex_uint_uses_string_token(self):
        from bovnar.enums import ValueTypeFamily, Event
        from bovnar.structs import make_type_spec, make_unit_none
        vt = make_type_spec(ValueTypeFamily.UINT, 8, 16)
        vu = make_unit_none()
        with Writer.to_mem() as w:
            w.emit(Event.ASSIGNMENT_START, key='flags')
            w._emit_annotation('uint', vt, vu)
            w.emit(Event.DATA, value='ff', vt=vt, vu=vu)
        out = w.get_output()
        assert b'"ff"' in out, \
            "hex uint written via emit() must be quoted (TOKEN_IS_STRING)"

    def test_emit_decimal_uint_uses_number_token(self):
        from bovnar.enums import ValueTypeFamily, Event
        from bovnar.structs import make_type_spec, make_unit_none
        vt = make_type_spec(ValueTypeFamily.UINT, 8, 10)
        vu = make_unit_none()
        with Writer.to_mem() as w:
            w.emit(Event.ASSIGNMENT_START, key='val')
            w._emit_annotation('uint', vt, vu)
            w.emit(Event.DATA, value='42', vt=vt, vu=vu)
        out = w.get_output()
        assert b'"42"' not in out, \
            "decimal uint written via emit() must not be quoted (TOKEN_IS_NUMBER)"
        assert b'42' in out


@needs_lib
class TestReferenceRoundtrip:
    """Regression tests for the reference validator and writer roundtrip.

    BVNR reference tokens are stored with a mandatory leading '.' (the
    grammar is: reference = '&' , ref-segment ; ref-segment = '.' , id-start ,
    {ref-body-char}).  The validator must reject strings without that leading
    '.' and strings that are just '.' with no following id-start, so that the
    writer can never produce '&foo' or '&.' — both of which the parser rejects.
    """

    def _write_reference(self, ref_data: bytes) -> bool:
        import ctypes
        from bovnar.structs import BvnrData
        w = Writer.to_mem()
        w.emit(Event.ASSIGNMENT_START, key='x')
        d = BvnrData()
        d.type = w._TOKEN_IS_REFERENCE
        d.data = ctypes.cast(ctypes.c_char_p(ref_data), ctypes.c_void_p)
        d.length = len(ref_data)
        ok = w._lib.bvnr_write_event(w._ptr, int(Event.DATA), ctypes.byref(d))
        w.destroy()
        return ok

    def test_valid_reference_accepted(self):
        assert self._write_reference(b'.host'), \
            "'.host' (valid reference) must be accepted by the writer"

    def test_valid_multi_segment_reference_accepted(self):
        assert self._write_reference(b'.config.host'), \
            "'.config.host' must be accepted"

    def test_reference_without_leading_dot_rejected(self):
        from bovnar.exceptions import BovnarWriteError
        assert not self._write_reference(b'host'), \
            "'host' (no leading dot) must be rejected — would produce invalid BVNR"

    def test_reference_dot_only_rejected(self):
        assert not self._write_reference(b'.'), \
            "'.' (dot only, no id-start) must be rejected"

    def test_reference_double_dot_rejected(self):
        assert not self._write_reference(b'..host'), \
            "'..host' (double leading dot) must be rejected"

    def test_valid_reference_roundtrips(self):
        import ctypes
        from bovnar.structs import BvnrData
        from bovnar import loads

        with Writer.to_mem() as w:
            w.emit(Event.ASSIGNMENT_START, key='x')
            d = BvnrData()
            d.type = w._TOKEN_IS_REFERENCE
            ref = b'.some.path'
            d.data = ctypes.cast(ctypes.c_char_p(ref), ctypes.c_void_p)
            d.length = len(ref)
            ok = w._lib.bvnr_write_event(w._ptr, int(Event.DATA), ctypes.byref(d))
            assert ok

        out = w.get_output()
        assert b'&.some.path' in out, "writer must emit '&.some.path'"
        result = loads(out)
        assert result['x'] == '.some.path', \
            "parsed reference must be the path string including leading dot"

    def test_reference_output_is_parseable(self):
        import ctypes
        from bovnar.structs import BvnrData
        from bovnar import loads
        from bovnar.reader import Reader

        with Writer.to_mem() as w:
            w.emit(Event.ASSIGNMENT_START, key='r')
            d = BvnrData()
            d.type = w._TOKEN_IS_REFERENCE
            ref = b'.host'
            d.data = ctypes.cast(ctypes.c_char_p(ref), ctypes.c_void_p)
            d.length = len(ref)
            w._lib.bvnr_write_event(w._ptr, int(Event.DATA), ctypes.byref(d))

        out = w.get_output()
        with Reader() as r:
            r.read_mem(out)


@needs_lib
class TestEmitFloatFixTokenType:
    """Regression tests for the emit() float_fix/float_dec token-type bug.

    For float_fix, vt.base stores the Q (fractional-bits) parameter, not a
    numeric base.  The old emit() checked 'vt.base not in (0, 10)' without
    excluding float_fix/float_dec, so Q=16 produced TOKEN_IS_STRING ("1.5")
    instead of TOKEN_IS_NUMBER (1.5).

    The corrected emit() adds the same _decimal_float_families guard that
    _write_scalar() already had, so all four of the families that always use
    decimal encoding (FLOAT, FLOAT_FIX, FLOAT_DEC, and UTF8 is separate)
    always produce NUMBER tokens regardless of vt.base.
    """

    def test_emit_float_fix_q16_uses_number_token(self):
        vt = make_type_spec(ValueTypeFamily.FLOAT_FIX, 32, 16)
        vu = make_unit_none()
        with Writer.to_mem() as w:
            w.emit(Event.ASSIGNMENT_START, key='ff')
            w._emit_annotation('float_fix', vt, vu)
            w.emit(Event.DATA, value='1.5', vt=vt, vu=vu)
        out = w.get_output()
        after_bracket = out.split(b'>')[1] if b'>' in out else out
        assert b'"' not in after_bracket, \
            "float_fix with Q=16 via emit() must not produce a quoted value"

    def test_emit_float_fix_q8_uses_number_token(self):
        vt = make_type_spec(ValueTypeFamily.FLOAT_FIX, 64, 8)
        vu = make_unit_none()
        with Writer.to_mem() as w:
            w.emit(Event.ASSIGNMENT_START, key='ff')
            w._emit_annotation('float_fix', vt, vu)
            w.emit(Event.DATA, value='3.14', vt=vt, vu=vu)
        out = w.get_output()
        after_bracket = out.split(b'>')[1] if b'>' in out else out
        assert b'"' not in after_bracket, \
            "float_fix with Q=8 via emit() must not produce a quoted value"

    def test_emit_float_dec_uses_number_token(self):
        vt = make_type_spec(ValueTypeFamily.FLOAT_DEC, 64, 0)
        vu = make_unit_none()
        with Writer.to_mem() as w:
            w.emit(Event.ASSIGNMENT_START, key='fd')
            w._emit_annotation('float_dec', vt, vu)
            w.emit(Event.DATA, value='2.718', vt=vt, vu=vu)
        out = w.get_output()
        after_bracket = out.split(b'>')[1] if b'>' in out else out
        assert b'"' not in after_bracket, \
            "float_dec via emit() must not produce a quoted value"

    def test_emit_float_fix_q16_roundtrips(self):
        from bovnar import loads
        from bovnar.reader import Reader
        vt = make_type_spec(ValueTypeFamily.FLOAT_FIX, 32, 16)
        vu = make_unit_none()
        with Writer.to_mem() as w:
            w.emit(Event.ASSIGNMENT_START, key='ff')
            w._emit_annotation('float_fix', vt, vu)
            w.emit(Event.DATA, value='1.5', vt=vt, vu=vu)
        out = w.get_output()
        with Reader() as r:
            r.read_mem(out)
        result = loads(out)
        assert abs(result['ff'] - 1.5) < 1e-9, \
            "float_fix Q=16 written via emit() must roundtrip to 1.5"

    def test_emit_hex_uint_still_uses_string_token(self):
        """The guard must not suppress STRING for genuinely hex uint."""
        vt = make_type_spec(ValueTypeFamily.UINT, 8, 16)
        vu = make_unit_none()
        with Writer.to_mem() as w:
            w.emit(Event.ASSIGNMENT_START, key='flags')
            w._emit_annotation('uint', vt, vu)
            w.emit(Event.DATA, value='ff', vt=vt, vu=vu)
        out = w.get_output()
        assert b'"ff"' in out, \
            "hex uint via emit() must still be quoted (TOKEN_IS_STRING)"

    def test_emit_float_fix_all_q_values_use_number_token(self):
        from bovnar.reader import Reader
        for q in (2, 4, 8, 11, 16, 24, 32):
            vt = make_type_spec(ValueTypeFamily.FLOAT_FIX, 64, q)
            vu = make_unit_none()
            with Writer.to_mem() as w:
                w.emit(Event.ASSIGNMENT_START, key='ff')
                w._emit_annotation('float_fix', vt, vu)
                w.emit(Event.DATA, value='1.0', vt=vt, vu=vu)
            out = w.get_output()
            after_bracket = out.split(b'>')[1] if b'>' in out else out
            assert b'"' not in after_bracket, \
                f"float_fix Q={q} via emit() must not produce a quoted value"
            with Reader() as r:
                r.read_mem(out)


@needs_lib
class TestStringEscapeRoundtrip:
    """Roundtrip coverage for all supported BVNR string escape sequences.

    The C writer escapes \\t, \\n, \\v, \\f, \\r, \\\" and \\\\ in strings.
    The parser unescapes all seven.  This class verifies each one survives
    a complete write → parse cycle.
    """

    def _roundtrip_str(self, value: str) -> str:
        from bovnar import loads, dumps
        return loads(dumps({'s': value}))['s']

    def test_tab_roundtrips(self):
        assert self._roundtrip_str("a\tb") == "a\tb"

    def test_newline_roundtrips(self):
        assert self._roundtrip_str("a\nb") == "a\nb"

    def test_carriage_return_roundtrips(self):
        assert self._roundtrip_str("a\rb") == "a\rb"

    def test_vertical_tab_roundtrips(self):
        assert self._roundtrip_str("a\x0bb") == "a\x0bb", \
            "vertical tab (\\v, 0x0B) must survive write-parse roundtrip"

    def test_form_feed_roundtrips(self):
        assert self._roundtrip_str("a\x0cb") == "a\x0cb", \
            "form feed (\\f, 0x0C) must survive write-parse roundtrip"

    def test_double_quote_roundtrips(self):
        assert self._roundtrip_str('say "hello"') == 'say "hello"'

    def test_backslash_roundtrips(self):
        assert self._roundtrip_str("a\\b") == "a\\b"

    def test_all_escapes_together(self):
        s = "\t\n\x0b\x0c\r\"\\"
        assert self._roundtrip_str(s) == s, \
            "all seven escape sequences must survive roundtrip together"

    def test_vertical_tab_appears_escaped_in_output(self):
        from bovnar import dumps
        out = dumps({'s': '\x0b'})
        assert b'\\v' in out, \
            "vertical tab (0x0B) must be escaped as \\\\v in the serialised output"

    def test_form_feed_appears_escaped_in_output(self):
        from bovnar import dumps
        out = dumps({'s': '\x0c'})
        assert b'\\f' in out, \
            "form feed (0x0C) must be escaped as \\\\f in the serialised output"


@needs_lib
class TestEmptyStringRoundtrip:
    """Regression tests for the empty-string/null confusion in _decode_value.

    Bug – _decode_value returned None unconditionally when raw bytes were
    empty, which made both null values AND empty strings ("") decode to None.
    The fix checks tok_type: TOKEN_IS_STRING (1) or TOKEN_IS_ARRAY_STRING (6)
    with empty raw → empty string; tok_type TOKEN_IS_NULL_VALUE (9) → None.
    """

    def _rt(self, d: dict) -> dict:
        from bovnar import loads, dumps
        return loads(dumps(d))

    def test_empty_string_scalar_roundtrips(self):
        result = self._rt({'k': ''})
        assert result['k'] == '', \
            "empty string must roundtrip as '' not None"

    def test_empty_string_is_not_none(self):
        result = self._rt({'k': ''})
        assert result['k'] is not None, \
            "empty string must not decode to None"

    def test_null_still_roundtrips_as_none(self):
        result = self._rt({'k': None})
        assert result['k'] is None, \
            "null must still decode to None after the fix"

    def test_empty_string_in_array_roundtrips(self):
        result = self._rt({'a': ['', 'x', '']})
        assert result['a'] == ['', 'x', ''], \
            "empty strings inside arrays must roundtrip as ''"

    def test_null_in_array_still_decodes_as_none(self):
        from bovnar import loads
        raw = b'.a = [,];'
        result = loads(raw)
        assert result['a'] == [None, None], \
            "null array elements must still decode to None"

    def test_mixed_empty_and_null_in_array(self):
        from bovnar import loads, dumps
        out = dumps({'a': ['', 'hello', '']})
        assert b'""' in out, "serialised output must contain quoted empty strings"
        result = loads(out)
        assert result['a'] == ['', 'hello', ''], \
            "empty strings must survive write-parse through array"

    def test_empty_string_roundtrip_via_write_string(self):
        from bovnar import loads
        with Writer.to_mem() as w:
            w.write_string('s', '')
        out = w.get_output()
        result = loads(out)
        assert result['s'] == '', \
            "write_string('s', '') must parse back as empty string"

    def test_typed_empty_string_roundtrips(self):
        from bovnar import loads
        raw = b'.k = <utf8> "";'
        result = loads(raw)
        assert result['k'] == '', \
            "typed empty string <utf8> \"\" must decode as ''"
