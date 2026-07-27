# SPDX-License-Identifier: MIT
#
# Copyright (c) 2026 Janos Sonntag
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.



import ctypes

from ._ffi import get_library
from .enums import (
    Event, TokenType, ValueTypeFamily,
    BaseUnit, SIPrefix, IECPrefix, Exponent, ErrorCode,
)
from .exceptions import BovnarWriteError, BovnarArgumentError
from .structs import (
    BvnrSink, BvnrWriteFlags, BvnrData,
    ValueTypeSpec, ValueUnit,
    build_unit_policy,
    make_type_spec, make_unit_dimensionless, make_unit_none,
    make_unit_si, make_unit_iec,
)

DEFAULT_MEM_CAP = 4 * 1024 * 1024

class Writer:


    def __init__(self) -> None:
        self._lib      = get_library()
        self._ptr      = None
        self._sink     = None
        self._buf      = None
        self._buf_cap  = 0
        self._finished = False
        self._struct_depth = 0

        ptr = self._lib.bvnr_writer_create()
        if not ptr:
            raise MemoryError("bvnr_writer_create() returned NULL")
        self._ptr = ptr

    @classmethod
    def to_mem(cls,
               buf: bytearray | None = None,
               cap: int = DEFAULT_MEM_CAP,
               *,
               pretty: bool = True) -> 'Writer':

        w = cls()

        if buf is not None:
            if len(buf) < cap:
                raise BovnarArgumentError(
                    f"Supplied buffer length {len(buf)} < cap {cap}")
            raw_buf = (ctypes.c_char * cap).from_buffer(buf)
        else:
            raw_buf = (ctypes.c_char * cap)()

        w._buf     = raw_buf
        w._buf_cap = cap

        flags = BvnrWriteFlags()
        ok = w._lib.bvnr_open_write_mem(
            w._ptr,
            ctypes.cast(raw_buf, ctypes.c_void_p),
            cap,
            pretty,
            ctypes.byref(flags),
        )
        if not ok:
            w._raise_error()
        return w

    @classmethod
    def to_fd(cls, fd: int, *, pretty: bool = True) -> 'Writer':

        if not isinstance(fd, int) or fd < 0:
            raise BovnarArgumentError(f"Invalid file descriptor: {fd!r}")

        w    = cls()
        sink = BvnrSink()
        w._lib.bvnr_sink_to_fd(ctypes.byref(sink), fd)
        w._sink = sink

        flags = BvnrWriteFlags()
        ok = w._lib.bvnr_open_write_sink(
            w._ptr,
            ctypes.byref(sink),
            pretty,
            ctypes.byref(flags),
        )
        if not ok:
            w._raise_error()
        return w

    @classmethod
    def to_file(cls, path: str, *, pretty: bool = True) -> 'Writer':

        import os
        fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o644)
        try:
            w = cls.to_fd(fd, pretty=pretty)
        except BaseException:
            os.close(fd)
            raise
        w._owned_fd = fd
        return w

    def __enter__(self) -> 'Writer':
        return self

    def __exit__(self, exc_type, exc_val, exc_tb) -> None:

        try:
            if not self._finished and exc_type is None:
                # A writer already in a sticky error state has an
                # unterminated, broken stream, and that error was raised
                # when it occurred.  finish() now reports that failure, so
                # calling it here would re-raise an error the caller has
                # already handled inside the block — skip it and just
                # release resources.
                poisoned = (self._ptr is not None and
                            self._lib.bvnr_writer_get_error(self._ptr) != 0)
                if not poisoned:
                    self.finish()
        finally:
            self._close_owned_fd()

    def __del__(self) -> None:
        try:
            self.destroy()
        except Exception:
            pass

    def _close_owned_fd(self) -> None:
        fd = getattr(self, '_owned_fd', None)
        if fd is not None and fd >= 0:
            import os
            try:
                os.close(fd)
            except OSError:
                pass
            self._owned_fd = None

    def finish(self) -> None:

        if self._ptr is None or self._finished:
            return
        if self._struct_depth != 0:
            raise BovnarWriteError(
                ErrorCode.GOT_INCOMPLETE_BVNR_STREAM, 0,
                f"finish() called with {self._struct_depth} unclosed struct(s)"
            )
        ok = self._lib.bvnr_write_finish(self._ptr)
        self._finished = True
        self._close_owned_fd()
        if not ok:
            self._raise_error()

    def destroy(self) -> None:

        if self._ptr is not None:
            self._lib.bvnr_writer_destroy(self._ptr)
            self._ptr = None
        self._close_owned_fd()

    def set_unit_policy(self, policy) -> None:
        """Refuse to EMIT a value a reader under `policy` would refuse to read.

        Takes the same :class:`~bovnar.UnitPolicy` the reader does, but only its
        VALIDATION half: ``require_unit`` and ``require_dimension_of``. A policy
        carrying ``targets``, ``normalise_si``, ``base`` or ``leave_inexact`` is
        rejected rather than half-honoured — the writer already rewrites values
        under ``UnitFlags.REDUCE``, and a second rewriting mode with different
        rules about exactness is how two features end up disagreeing about what
        a document says.

        This is the half the format's promise rests on: a reader can only reject
        a document somebody already wrote.

            w = Writer.to_mem()
            w.set_unit_policy(UnitPolicy(require_unit=True))
            w.write_float("speed", 64, 9.81, parse_unit("m/s"))   # fine
            w.write_float("bare", 64, 0.25)                        # BovnarWriteError

        The unit checked is the one the value will carry in the output, whether
        it was given inline or as a type-annotation parameter. Pass ``None`` to
        clear.
        """
        if self._ptr is None:
            raise BovnarArgumentError("Writer has been destroyed")
        if policy is None:
            if not self._lib.bvnr_writer_set_unit_policy(self._ptr, None):
                raise BovnarArgumentError("bvnr_writer_set_unit_policy failed")
            return
        if (policy.targets or policy.normalise_si or policy.base
                or policy.leave_inexact
                or any(r.convert for r in policy.rules)):
            raise BovnarArgumentError(
                "the writer takes only the validation half of a UnitPolicy "
                "(require_unit / require_dimension_of, and rules with "
                "convert=False); use UnitFlags.REDUCE for write-side unit "
                "rewriting")
        try:
            cp, keepalive = build_unit_policy(
                require_unit=policy.require_unit,
                require_dimension_of=policy.require_dimension_of,
                rules=policy.rules)
        except ValueError as e:
            raise BovnarArgumentError(str(e)) from None
        if not self._lib.bvnr_writer_set_unit_policy(self._ptr, ctypes.byref(cp)):
            raise BovnarArgumentError(
                "unusable unit policy — check the unit spellings in "
                "require_dimension_of")
        del keepalive

    @property
    def bytes_written(self) -> int:

        if self._ptr is None:
            raise BovnarArgumentError("Writer has been destroyed")
        return self._lib.bvnr_writer_bytes_written(self._ptr)

    def get_output(self) -> bytes:

        if self._buf is None:
            raise BovnarArgumentError(
                "get_output() is only available for mem writers")
        n = self.bytes_written
        return bytes(self._buf[:n])

    def emit(self,
             event: Event,
             *,
             key: str | None = None,
             value: str | None = None,
             vt: ValueTypeSpec | None = None,
             vu: ValueUnit | None = None) -> None:

        if self._ptr is None:
            raise BovnarArgumentError("Writer has been destroyed")

        d     = BvnrData()
        _keep = []

        if key is not None:
            raw = key.encode('utf-8')
            _keep.append(raw)
            d.data   = ctypes.cast(ctypes.c_char_p(raw), ctypes.c_void_p)
            d.length = len(raw)

        if vt is not None:
            d.value_type = vt

        if vu is not None:
            d.value_unit = vu

        if value is not None:
            raw = value.encode('utf-8')
            _keep.append(raw)
            d.data   = ctypes.cast(ctypes.c_char_p(raw), ctypes.c_void_p)
            d.length = len(raw)

            _decimal_float_families = frozenset((
                int(ValueTypeFamily.FLOAT_FIX),
                int(ValueTypeFamily.FLOAT_DEC),
            ))
            if vt is not None:
                fam = int(vt.family)
                if fam == int(ValueTypeFamily.UTF8):
                    d.type = self._TOKEN_IS_STRING
                elif (vt.base not in (0, 10)
                      and fam not in _decimal_float_families):
                    d.type = self._TOKEN_IS_STRING
                else:
                    d.type = self._TOKEN_IS_NUMBER
            else:
                d.type = self._TOKEN_IS_NUMBER

        ok = self._lib.bvnr_write_event(self._ptr, int(event), ctypes.byref(d))
        if not ok:
            self._raise_error()

    # Mirror of C token_type_t; see enums.TokenType, which test_enums.py
    # checks against the header so these cannot drift.
    _TOKEN_IS_IDENTIFIER   = TokenType.IDENTIFIER
    _TOKEN_IS_STRING       = TokenType.STRING
    _TOKEN_IS_NUMBER       = TokenType.NUMBER
    _TOKEN_IS_SYMBOL       = TokenType.SYMBOL
    _TOKEN_IS_REFERENCE    = TokenType.REFERENCE
    _TOKEN_IS_ARRAY_NUMBER = TokenType.ARRAY_NUMBER
    _TOKEN_IS_ARRAY_STRING = TokenType.ARRAY_STRING
    _TOKEN_IS_TYPE         = TokenType.TYPE
    _TOKEN_IS_OCTET_STREAM = TokenType.OCTET_STREAM
    _TOKEN_IS_NULL_VALUE   = TokenType.NULL_VALUE
    _TOKEN_IS_STRUCTURE    = TokenType.STRUCTURE
    _TOKEN_IS_UNIT         = TokenType.UNIT
    _TOKEN_IS_TYPE_WIDTH   = TokenType.TYPE_WIDTH
    _TOKEN_IS_TYPE_BASE    = TokenType.TYPE_BASE
    _TOKEN_IS_TYPE_Q       = TokenType.TYPE_Q
    _TOKEN_IS_BOOL         = TokenType.BOOL
    _TOKEN_IS_UNKNOWN      = TokenType.UNKNOWN

    def _emit_annotation(self,
                          family_name: str,
                          vt: ValueTypeSpec,
                          vu: ValueUnit) -> None:

        d_start = BvnrData()
        d_start.type = self._TOKEN_IS_TYPE
        d_start.value_type = vt
        d_start.value_unit = vu
        ok = self._lib.bvnr_write_event(
            self._ptr, int(Event.TYPE_ANNOTATION_START), ctypes.byref(d_start))
        if not ok:
            self._raise_error()

        d_fam = BvnrData()
        d_fam.type = self._TOKEN_IS_TYPE
        d_fam.value_type = vt
        ok = self._lib.bvnr_write_event(
            self._ptr, int(Event.TYPE_ANNOTATION_TYPE_FAMILY), ctypes.byref(d_fam))
        if not ok:
            self._raise_error()

        # Only numeric families carry a width; utf8/bool are parameterless
        # (a width on them is rejected by the C type-spec validator).
        _width_families = {
            int(ValueTypeFamily.UINT),
            int(ValueTypeFamily.SINT),
            int(ValueTypeFamily.FLOAT),
            int(ValueTypeFamily.FLOAT_FIX),
            int(ValueTypeFamily.FLOAT_DEC),
            int(ValueTypeFamily.DATETIME),
        }
        fam = int(vt.family)

        if fam in _width_families and vt.width != 0:
            d_width = BvnrData()
            d_width.type = self._TOKEN_IS_TYPE_WIDTH
            d_width.value_type = vt
            ok = self._lib.bvnr_write_event(
                self._ptr, int(Event.TYPE_ANNOTATION_TYPE_FAMILY_PARAM),
                ctypes.byref(d_width))
            if not ok:
                self._raise_error()

        if fam in (int(ValueTypeFamily.FLOAT),
                   int(ValueTypeFamily.SINT),
                   int(ValueTypeFamily.UINT)):
            if vt.base not in (0, 10):
                d_base = BvnrData()
                d_base.type = self._TOKEN_IS_TYPE_BASE
                d_base.value_type = vt
                ok = self._lib.bvnr_write_event(
                    self._ptr, int(Event.TYPE_ANNOTATION_TYPE_FAMILY_PARAM),
                    ctypes.byref(d_base))
                if not ok:
                    self._raise_error()

        if fam == int(ValueTypeFamily.FLOAT_FIX) and vt.base != 0:
            d_q = BvnrData()
            d_q.type = self._TOKEN_IS_TYPE_Q
            d_q.value_type = vt
            ok = self._lib.bvnr_write_event(
                self._ptr, int(Event.TYPE_ANNOTATION_TYPE_FAMILY_PARAM),
                ctypes.byref(d_q))
            if not ok:
                self._raise_error()

        # datetime epoch (spec 1.1): emitted as a named parameter (e.g. "tai").
        # base holds the epoch index; 0 (unix) is the default and stays implicit.
        if fam == int(ValueTypeFamily.DATETIME) and vt.base != 0:
            epoch = self._lib.bvnr_datetime_epoch_name(vt)
            if epoch:
                _keep_epoch = epoch
                d_ep = BvnrData()
                d_ep.type = self._TOKEN_IS_UNIT
                d_ep.value_type = vt
                d_ep.data = ctypes.cast(
                    ctypes.c_char_p(epoch), ctypes.c_void_p)
                d_ep.length = len(epoch)
                ok = self._lib.bvnr_write_event(
                    self._ptr, int(Event.TYPE_ANNOTATION_TYPE_FAMILY_PARAM),
                    ctypes.byref(d_ep))
                if not ok:
                    self._raise_error()

        # A datetime carries no unit — its only annotation parameter is the epoch
        # (emitted above). Its value_unit can still be the (1 × bu_none) no-unit
        # sentinel (e.g. for a bare ISO literal whose annotation is inferred),
        # which bvn_unit_to_string_ex renders as the literal "no_unit"; emitting
        # that as a unit produces an illegal "<datetime:…,no_unit>" annotation.
        if vu.num_components > 0 and fam != int(ValueTypeFamily.DATETIME):
            lib = self._lib
            ubuf = ctypes.create_string_buffer(256)
            unit_flags = lib.bvnr_writer_unit_flags(self._ptr)
            ulen = lib.bvn_unit_to_string_ex(vu, ubuf, 256, unit_flags)
            if ulen < 0:
                # Skipping the parameter on failure writes the value with its
                # unit silently GONE -- the annotation says <float:64> for a
                # value the caller gave a unit. The C writer refuses the same
                # document a moment later on the value side, so this was masked;
                # relying on that is relying on an unrelated check.
                raise BovnarArgumentError(
                    "cannot serialise the unit of this %s value: "
                    "bvn_unit_to_string_ex refused it (a bu_none component has "
                    "no spelling outside a bare no_unit, or the reduction "
                    "cannot express it). Writing the value without its unit "
                    "would change what it means." % family_name)
            if ulen > 0:
                raw_unit = ubuf.raw[:ulen]
                _keep_unit = raw_unit
                d_unit = BvnrData()
                d_unit.type = self._TOKEN_IS_UNIT
                d_unit.value_type = vt
                d_unit.value_unit = vu
                d_unit.data = ctypes.cast(
                    ctypes.c_char_p(raw_unit), ctypes.c_void_p)
                d_unit.length = ulen
                ok = self._lib.bvnr_write_event(
                    self._ptr, int(Event.TYPE_ANNOTATION_TYPE_FAMILY_PARAM),
                    ctypes.byref(d_unit))
                if not ok:
                    self._raise_error()

        d_end = BvnrData()
        d_end.type = self._TOKEN_IS_TYPE
        ok = self._lib.bvnr_write_event(
            self._ptr, int(Event.TYPE_ANNOTATION_END), ctypes.byref(d_end))
        if not ok:
            self._raise_error()

    def write_version(self, major: int = 1, minor: int = 1) -> None:
        """Emit a leading ``#!bovnar <major>.<minor>`` directive. Must be called
        before any value is written (else error_invalid_argument)."""
        if not self._lib.bvnr_write_version(self._ptr, major, minor):
            self._raise_error()

    def write_uint(self,
                   key: str,
                   value: int,
                   *,
                   width: int = 64,
                   base: int = 10,
                   unit_str: str | None = None,
                   unit_si_base: BaseUnit | None = None,
                   unit_si_prefix: SIPrefix = SIPrefix.NONE,
                   unit_si_exp: Exponent = Exponent.LINEAR,
                   unit_iec_base: BaseUnit | None = None,
                   unit_iec_prefix: IECPrefix = IECPrefix.NONE) -> None:

        vt  = make_type_spec(ValueTypeFamily.UINT, width, base)
        vu  = self._resolve_unit(unit_str, unit_si_base, unit_si_prefix,
                                 unit_si_exp, unit_iec_base, unit_iec_prefix)
        val = self._format_uint(value, base, width)
        self._write_scalar(key, 'uint', vt, vu, val)

    def write_sint(self,
                   key: str,
                   value: int,
                   *,
                   width: int = 64,
                   base: int = 10,
                   unit_str: str | None = None,
                   unit_si_base: BaseUnit | None = None,
                   unit_si_prefix: SIPrefix = SIPrefix.NONE,
                   unit_si_exp: Exponent = Exponent.LINEAR) -> None:

        vt  = make_type_spec(ValueTypeFamily.SINT, width, base)
        vu  = self._resolve_unit(unit_str, unit_si_base, unit_si_prefix,
                                 unit_si_exp, None, IECPrefix.NONE)
        val = self._format_sint(value, base)
        self._write_scalar(key, 'sint', vt, vu, val)

    def write_float(self,
                    key: str,
                    value: float,
                    *,
                    width: int = 64,
                    unit_str: str | None = None,
                    unit_si_base: BaseUnit | None = None,
                    unit_si_prefix: SIPrefix = SIPrefix.NONE,
                    unit_si_exp: Exponent = Exponent.LINEAR) -> None:

        vt  = make_type_spec(ValueTypeFamily.FLOAT, width, 0)
        vu  = self._resolve_unit(unit_str, unit_si_base, unit_si_prefix,
                                 unit_si_exp, None, IECPrefix.NONE)
        val = self._format_float(value, vt)
        self._write_scalar(key, 'float', vt, vu, val)

    def write_float_fix(self,
                        key: str,
                        value: float,
                        *,
                        width: int = 64,
                        q: int = 0,
                        unit_str: str | None = None,
                        unit_si_base: BaseUnit | None = None,
                        unit_si_prefix: SIPrefix = SIPrefix.NONE,
                        unit_si_exp: Exponent = Exponent.LINEAR) -> None:

        vt  = make_type_spec(ValueTypeFamily.FLOAT_FIX, width, q)
        vu  = self._resolve_unit(unit_str, unit_si_base, unit_si_prefix,
                                 unit_si_exp, None, IECPrefix.NONE)
        val = self._format_float(value, make_type_spec(ValueTypeFamily.FLOAT, width, 0))
        self._write_scalar(key, 'float_fix', vt, vu, val)

    def write_float_dec(self,
                        key: str,
                        value: float,
                        *,
                        width: int = 64,
                        unit_str: str | None = None,
                        unit_si_base: BaseUnit | None = None,
                        unit_si_prefix: SIPrefix = SIPrefix.NONE,
                        unit_si_exp: Exponent = Exponent.LINEAR) -> None:

        vt  = make_type_spec(ValueTypeFamily.FLOAT_DEC, width, 0)
        vu  = self._resolve_unit(unit_str, unit_si_base, unit_si_prefix,
                                 unit_si_exp, None, IECPrefix.NONE)
        val = self._format_float(value, make_type_spec(ValueTypeFamily.FLOAT, width, 0))
        self._write_scalar(key, 'float_dec', vt, vu, val)

    def write_string(self, key: str, value: str) -> None:

        self.emit(Event.ASSIGNMENT_START, key=key)
        raw = value.encode('utf-8')
        _keep = raw
        d = BvnrData()
        d.type = self._TOKEN_IS_STRING
        d.data = ctypes.cast(ctypes.c_char_p(raw), ctypes.c_void_p)
        d.length = len(raw)
        ok = self._lib.bvnr_write_event(self._ptr, int(Event.DATA), ctypes.byref(d))
        if not ok:
            self._raise_error()

    def write_bool(self, key: str, value: bool) -> None:

        self.emit(Event.ASSIGNMENT_START, key=key)
        sym = b'true' if value else b'false'
        d = BvnrData()
        d.type = self._TOKEN_IS_BOOL
        d.data = ctypes.cast(ctypes.c_char_p(sym), ctypes.c_void_p)
        d.length = len(sym)
        ok = self._lib.bvnr_write_event(self._ptr, int(Event.DATA), ctypes.byref(d))
        if not ok:
            self._raise_error()

    def write_null(self, key: str) -> None:

        self.emit(Event.ASSIGNMENT_START, key=key)
        d = BvnrData()
        d.type = self._TOKEN_IS_NULL_VALUE
        ok = self._lib.bvnr_write_event(self._ptr, int(Event.DATA), ctypes.byref(d))
        if not ok:
            self._raise_error()

    def begin_struct(self, key: str) -> None:

        self.emit(Event.ASSIGNMENT_START, key=key)
        self.emit(Event.STRUCT_START)
        self._struct_depth += 1

    def end_struct(self) -> None:

        self.emit(Event.STRUCT_END)
        self._struct_depth -= 1

    def begin_array_row(self) -> None:

        self.emit(Event.ARRAY_ROW_START)

    def end_array_row(self) -> None:

        self.emit(Event.ARRAY_ROW_END)

    def new_array_dim(self) -> None:

        self.emit(Event.ARRAY_DIM_START)

    def _write_scalar(self,
                      key: str,
                      family_name: str,
                      vt: ValueTypeSpec,
                      vu: ValueUnit,
                      val: str) -> None:
        self.emit(Event.ASSIGNMENT_START, key=key)
        self._emit_annotation(family_name, vt, vu)

        _SPECIAL = frozenset(('nan', 'inf', 'ninf'))
        _decimal_float_families = frozenset((
            int(ValueTypeFamily.FLOAT_FIX),
            int(ValueTypeFamily.FLOAT_DEC),
        ))
        if (vt.base not in (0, 10) and val not in _SPECIAL
                and int(vt.family) not in _decimal_float_families):
            data_token_type = self._TOKEN_IS_STRING
        else:
            data_token_type = self._TOKEN_IS_NUMBER
        d = BvnrData()
        d.type = data_token_type
        d.value_type = vt
        d.value_unit = vu
        raw = val.encode('utf-8')
        _keep = raw
        d.data = ctypes.cast(ctypes.c_char_p(raw), ctypes.c_void_p)
        d.length = len(raw)
        ok = self._lib.bvnr_write_event(
            self._ptr, int(Event.DATA), ctypes.byref(d))
        if not ok:
            self._raise_error()

    @staticmethod
    def _resolve_unit(unit_str, si_base, si_prefix, si_exp,
                      iec_base, iec_prefix) -> ValueUnit:

        if unit_str is not None:

            lib = get_library()
            ok  = ctypes.c_bool(True)
            raw = unit_str.encode('utf-8')
            arr = (ctypes.c_uint8 * len(raw)).from_buffer_copy(raw)
            vu  = lib.bvn_parse_unit_n(arr, ctypes.c_uint32(len(raw)), ctypes.byref(ok))
            if not ok.value:
                raise BovnarArgumentError(f"Invalid unit string: {unit_str!r}")
            return vu
        if iec_base is not None:
            return make_unit_iec(iec_base, iec_prefix)
        if si_base is not None:
            return make_unit_si(si_base, si_prefix, si_exp)
        return make_unit_none()

    def _format_uint(self, value: int, base: int, width: int) -> str:
        # The value crosses the FFI boundary as a c_uint64, which silently wraps
        # an out-of-range Python int (emitting a corrupt document). Reject here;
        # values beyond 64 bits must go through the bigint path (write_bvni).
        if not 0 <= value < (1 << 64):
            raise BovnarWriteError(
                ErrorCode.NONE,
                message=f"uint value {value} does not fit a 64-bit field "
                        "(use write_bvni for arbitrary-precision integers)")
        lib = get_library()
        buf = ctypes.create_string_buffer(128)
        n   = lib.bvn_format_uint64(buf, 128, value, base, 0)
        if n < 0:
            raise BovnarWriteError(ErrorCode.NONE, message="bvn_format_uint64 overflow")
        return buf.value.decode('ascii')

    def _format_sint(self, value: int, base: int) -> str:
        # See _format_uint: c_int64 silently wraps an out-of-range Python int.
        if not -(1 << 63) <= value < (1 << 63):
            raise BovnarWriteError(
                ErrorCode.NONE,
                message=f"sint value {value} does not fit a 64-bit field "
                        "(use write_bvni for arbitrary-precision integers)")
        lib = get_library()
        buf = ctypes.create_string_buffer(128)
        n   = lib.bvn_format_int64(buf, 128, value, base, 0)
        if n < 0:
            raise BovnarWriteError(ErrorCode.NONE, message="bvn_format_int64 overflow")
        return buf.value.decode('ascii')

    def _format_float(self, value: float, vt: ValueTypeSpec) -> str:
        lib = get_library()
        buf = ctypes.create_string_buffer(128)
        n   = lib.bvn_format_double(buf, 128, value, vt)
        if n < 0:
            raise BovnarWriteError(ErrorCode.NONE, message="bvn_format_double overflow")
        return buf.value.decode('ascii')

    @staticmethod
    def _format_bigint(value: int, base: int) -> str:
        _B64 = (
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
            "abcdefghijklmnopqrstuvwxyz"
            "0123456789+/"
        )

        def digit_to_char(d: int, b: int) -> str:
            if b == 64:
                return _B64[d]
            if b == 85:
                return chr(ord('!') + d)
            if d < 10:
                return chr(ord('0') + d)
            if d < 36:
                return chr(ord('a') + d - 10)
            return chr(ord('A') + d - 36)

        negative = value < 0
        mag = abs(value)
        if mag == 0:
            return digit_to_char(0, base)
        digits: list[str] = []
        while mag > 0:
            digits.append(digit_to_char(mag % base, base))
            mag //= base
        digits.reverse()
        s = ''.join(digits)
        return ('-' + s) if negative else s

    def write_bvnf_base(self,
                        key: str,
                        value_str: str,
                        *,
                        width: int = 0,
                        base: int = 10,
                        unit_str: str | None = None,
                        unit_si_base: BaseUnit | None = None,
                        unit_si_prefix: SIPrefix = SIPrefix.NONE,
                        unit_si_exp: Exponent = Exponent.LINEAR) -> None:

        if base not in (10, 16):
            raise BovnarArgumentError(
                f"write_bvnf_base: base must be 10 or 16, got {base!r}")
        vt_base = 0 if base == 10 else 16
        vt  = make_type_spec(ValueTypeFamily.FLOAT, width, vt_base)
        vu  = self._resolve_unit(unit_str, unit_si_base, unit_si_prefix,
                                 unit_si_exp, None, IECPrefix.NONE)
        self._write_scalar(key, 'float', vt, vu, value_str)

    def write_bvni(self,
                   key: str,
                   value: int,
                   *,
                   width: int = 64,
                   base: int = 10,
                   signed: bool | None = None,
                   unit_str: str | None = None,
                   unit_si_base: BaseUnit | None = None,
                   unit_si_prefix: SIPrefix = SIPrefix.NONE,
                   unit_si_exp: Exponent = Exponent.LINEAR) -> None:

        if signed is None:
            use_signed = value < 0
        else:
            use_signed = signed
        family = ValueTypeFamily.SINT if use_signed else ValueTypeFamily.UINT
        vt_base = 0 if base == 10 else base
        vt  = make_type_spec(family, width, vt_base)
        vu  = self._resolve_unit(unit_str, unit_si_base, unit_si_prefix,
                                 unit_si_exp, None, IECPrefix.NONE)
        val = self._format_bigint(value, base)
        family_name = 'sint' if use_signed else 'uint'
        self._write_scalar(key, family_name, vt, vu, val)

    def _raise_error(self) -> None:
        code   = ErrorCode(self._lib.bvnr_writer_get_error(self._ptr))
        offset = self._lib.bvnr_writer_get_error_offset(self._ptr)
        msg_b  = self._lib.bvn_error_to_string(int(code))
        msg    = msg_b.decode('utf-8') if msg_b else ''
        raise BovnarWriteError(code, offset, msg)


