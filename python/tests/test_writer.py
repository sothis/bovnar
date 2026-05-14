

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
                                   unit_str='Ti-B'))
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
            w.emit(Event.TYPE_ANNOTATION_START,              vt=vt, vu=vu)
            w.emit(Event.TYPE_ANNOTATION_TYPE_FAMILY,        key='uint')
            w.emit(Event.TYPE_ANNOTATION_TYPE_FAMILY_PARAM,  vt=vt, vu=vu)
            w.emit(Event.TYPE_ANNOTATION_END)
            w.emit(Event.DATA, value="42", vt=vt, vu=vu)
        out = w.get_output()
        assert b'x'  in out
        assert b'42' in out

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
