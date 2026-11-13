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
import math

from ._ffi import get_library
from ._fileio import PushSink
from ._bigint import int_repr, int_to_decimal
from .enums import (
    Event, TokenType, ValueTypeFamily,
    BaseUnit, SIPrefix, IECPrefix, Exponent, ErrorCode,
)
from .exceptions import BovnarWriteError, BovnarArgumentError
from .structs import (
    BvnrSink, BvnrWriteFlags, BvnrData, EVENT_CALLBACK_FUNC,
    ValueTypeSpec, ValueUnit,
    build_unit_policy, describe_unit_policy_refusal,
    make_type_spec, make_unit_dimensionless, make_unit_none,
    make_unit_si, make_unit_iec,
    UNIT_STRING_MAX,
)

DEFAULT_MEM_CAP = 4 * 1024 * 1024

# The reserved float spellings: written as a bare number token, never quoted.
_SPECIAL_FLOAT_TEXTS = frozenset(('nan', 'inf', 'ninf'))

# The families whose base field is not a radix (bvn_effective_base reports 10
# for them): Q for float_fix, unused for float_dec, the epoch index for
# datetime. Their literal is never quoted for it. Module constants: these were
# rebuilt as a frozenset on every value emitted.
_DECIMAL_FLOAT_FAMILIES = frozenset((
    int(ValueTypeFamily.FLOAT_FIX),
    int(ValueTypeFamily.FLOAT_DEC),
    int(ValueTypeFamily.DATETIME),
))
# Families an annotation writes a width parameter for (utf8 and bool are
# parameterless).
_WIDTH_FAMILIES = frozenset((
    int(ValueTypeFamily.UINT),
    int(ValueTypeFamily.SINT),
    int(ValueTypeFamily.FLOAT),
    int(ValueTypeFamily.FLOAT_FIX),
    int(ValueTypeFamily.FLOAT_DEC),
    int(ValueTypeFamily.DATETIME),
))
# Families whose annotation takes no unit parameter: a datetime's parameter is
# its epoch, and a utf8 value carries its unit as an inline suffix instead.
_NO_UNIT_PARAM_FAMILIES = (int(ValueTypeFamily.DATETIME),
                           int(ValueTypeFamily.UTF8))

class Writer:


    def __init__(self) -> None:
        # _ptr before get_library(): that call can raise
        # BovnarLibraryNotFound, and __del__ then runs on a half-built object.
        self._ptr      = None
        self._lib      = get_library()
        self._sink     = None
        self._stream   = None
        self._buf      = None
        self._buf_cap  = 0
        self._finished = False
        self._struct_depth = 0
        # The on_event observer: its ctypes callback, and a state holding the
        # exception it raised (for the write call to re-raise) and whether it
        # is running.
        self._observer = None
        self._observer_state = None

        ptr = self._lib.bvnr_writer_create()
        if not ptr:
            raise MemoryError("bvnr_writer_create() returned NULL")
        self._ptr = ptr

    @staticmethod
    def _make_flags(unit_flags, emit_version, max_struct_nesting=0,
                    max_array_nesting=0) -> BvnrWriteFlags:
        """The write options every constructor opens with.

        NESTING. The reader is opened at the 255-level cap in both directions
        (reader.py says why: it is what the CLI and the DOM use), and a zeroed
        bvnr_write_flags_t selects the C default of 64 -- so a document
        loads() had just taken, nested 65 to 255 deep, was refused by dumps()
        with *_nesting_too_high. The writer now matches what the reader reads:
        ``max_struct_nesting`` / ``max_array_nesting`` 0 is 255, as in the
        reader's READ_OPTIONS, and 1 to 255 is that cap.

        UNIT FLAGS AND THE DIRECTIVE. Both are fields this struct has always
        mirrored and no constructor let a caller set, while set_unit_policy's
        own error message sent callers to UnitFlags.REDUCE "for write-side
        unit rewriting". `unit_flags` takes UnitFlags (REDUCE, ASCII_EXP,
        NUM_CANONICAL); `emit_version` writes a leading "#!bovnar M.N" naming
        the spec version this build implements, as write_version() would.
        """
        def nesting(name, n):
            if type(n) is not int or not 0 <= n <= 255:
                raise BovnarArgumentError(
                    f"{name} must be an int from 0 to 255, not {n!r}")
            return n or 255
        flags = BvnrWriteFlags()
        flags.max_struct_nesting = nesting('max_struct_nesting',
                                           max_struct_nesting)
        flags.max_array_nesting  = nesting('max_array_nesting',
                                           max_array_nesting)
        flags.unit_flags         = int(unit_flags)
        flags.emit_version       = bool(emit_version)
        return flags

    @staticmethod
    def _check_observer(on_event) -> None:
        if on_event is not None and not callable(on_event):
            raise BovnarArgumentError(
                f"on_event must be callable, not {type(on_event).__name__}")

    def _install_observer(self, flags: BvnrWriteFlags, on_event) -> None:
        """Point ``flags.on_event`` at a callback running ``on_event(event,
        data)`` after each event is serialised, with what reached the output
        (bvnr_write_event: a BVN_UNIT_REDUCE rescale shows the rescaled
        digits). ``data`` is a snapshot the observer may keep. Returning
        ``False`` fails the write with SCANNER_CALLBACK_FAILED; an exception
        it raises fails it too, and is re-raised from the write call."""
        if on_event is None:
            return
        self._check_observer(on_event)
        from .reader import Reader          # deferred, as the package root does
        snapshot = Reader._snapshot
        # No reference to self: the callback lives on the writer, and a cycle
        # would leave the C writer to the garbage collector.
        state = {'exc': None, 'busy': False}

        def _c_cb(_ud, ev, dptr) -> bool:
            if state['exc'] is not None:
                return False
            try:
                event = Event(ev)
            except ValueError:
                # An event this binding does not know (a newer library):
                # skipped, as the Rust observer skips it.
                return True
            state['busy'] = True
            try:
                data = snapshot(dptr.contents) if dptr else None
                result = on_event(event, data)
                return True if result is None else bool(result)
            except BaseException as exc:            # noqa: BLE001 (re-raised by the write)
                state['exc'] = exc
                return False
            finally:
                state['busy'] = False

        self._observer = EVENT_CALLBACK_FUNC(_c_cb)
        self._observer_state = state
        flags.on_event = self._observer

    @classmethod
    def _opened(cls, unit_flags, emit_version, on_event, max_struct_nesting,
                max_array_nesting):
        """A fresh writer and the flags to open it with."""
        flags = cls._make_flags(unit_flags, emit_version, max_struct_nesting,
                                max_array_nesting)
        w = cls()
        w._install_observer(flags, on_event)
        return w, flags

    @classmethod
    def to_mem(cls,
               buf: bytearray | None = None,
               cap: int = DEFAULT_MEM_CAP,
               *,
               pretty: bool = True,
               unit_flags: int = 0,
               emit_version: bool = False,
               on_event=None,
               max_struct_nesting: int = 0,
               max_array_nesting: int = 0) -> 'Writer':
        """A writer into memory: ``buf`` (a bytearray of at least ``cap``
        bytes) or a buffer of its own; :meth:`get_output` is the document.

        The keywords are those of every constructor. ``pretty`` indents the
        output. ``unit_flags`` (:class:`UnitFlags`) rewrites units on output,
        and ``emit_version`` leads the document with ``#!bovnar M.N``.
        ``max_struct_nesting`` and ``max_array_nesting`` cap the nesting
        (``0`` is 255, the format's limit); a deeper struct or array fails
        the write with ``STRUCT_NESTING_TOO_HIGH`` or
        ``ARRAY_NESTING_TOO_HIGH``. ``on_event(event, data)`` observes each
        event once it has been serialised, with what reached the output (a
        ``UnitFlags.REDUCE`` rescale shows the rescaled digits); returning
        ``False`` fails the write with ``SCANNER_CALLBACK_FAILED``, and an
        exception it raises is re-raised from the write call.
        """
        w, flags = cls._opened(unit_flags, emit_version, on_event,
                               max_struct_nesting, max_array_nesting)

        if buf is not None:
            if len(buf) < cap:
                raise BovnarArgumentError(
                    f"Supplied buffer length {len(buf)} < cap {cap}")
            raw_buf = (ctypes.c_char * cap).from_buffer(buf)
        else:
            raw_buf = (ctypes.c_char * cap)()

        w._buf     = raw_buf
        w._buf_cap = cap

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
    def to_fd(cls, fd: int, *, pretty: bool = True,
              unit_flags: int = 0, emit_version: bool = False,
              on_event=None, max_struct_nesting: int = 0,
              max_array_nesting: int = 0) -> 'Writer':
        """A writer into file descriptor ``fd``; the keywords are those of
        :meth:`to_mem`."""

        if type(fd) is not int or fd < 0 or fd > 0x7FFFFFFF:
            raise BovnarArgumentError(f"Invalid file descriptor: {fd!r}")

        w, flags = cls._opened(unit_flags, emit_version, on_event,
                               max_struct_nesting, max_array_nesting)
        sink = BvnrSink()
        w._lib.bvnr_sink_to_fd(ctypes.byref(sink), fd)
        w._sink = sink

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
    def to_stream(cls, stream, *, pretty: bool = True,
                  unit_flags: int = 0, emit_version: bool = False,
                  on_event=None, max_struct_nesting: int = 0,
                  max_array_nesting: int = 0) -> 'Writer':
        """Write to a socket (``sendall``) or a binary file object
        (``write``). The writer batches its output into a 64 KiB buffer and
        pushes each full buffer, then the rest at :meth:`flush` or
        :meth:`finish`, which also call the object's ``flush`` when it has
        one. An exception the object raises is re-raised from the write call
        that pushed, unchanged. The keywords are those of :meth:`to_mem`."""
        sink = PushSink(stream)
        w, flags = cls._opened(unit_flags, emit_version, on_event,
                               max_struct_nesting, max_array_nesting)
        w._stream = sink
        ok = w._lib.bvnr_open_write_sink(
            w._ptr,
            ctypes.byref(sink.handle),
            pretty,
            ctypes.byref(flags),
        )
        if not ok:
            w._raise_error()
        return w

    @classmethod
    def to_file(cls, path: str, *, pretty: bool = True,
                unit_flags: int = 0, emit_version: bool = False,
                on_event=None, max_struct_nesting: int = 0,
                max_array_nesting: int = 0) -> 'Writer':
        """A writer into the file at ``path``, created or truncated; the
        keywords are those of :meth:`to_mem`."""

        import os
        # Check the options before the file is created or truncated.
        cls._make_flags(unit_flags, emit_version, max_struct_nesting,
                        max_array_nesting)
        cls._check_observer(on_event)
        fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o644)
        try:
            w = cls.to_fd(fd, pretty=pretty, unit_flags=unit_flags,
                          emit_version=emit_version, on_event=on_event,
                          max_struct_nesting=max_struct_nesting,
                          max_array_nesting=max_array_nesting)
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

    def _check_not_in_sink(self) -> None:
        stream = getattr(self, '_stream', None)
        if stream is not None and stream.busy:
            raise BovnarArgumentError(
                "the writer is in use by its own sink: it cannot be written "
                "to, flushed, finished, reconfigured or destroyed from the "
                "object's write() or flush()")
        self._check_not_observing()

    def _check_not_observing(self) -> None:
        # The observer runs inside bvnr_write_event: writing, finishing or
        # destroying the writer from there would run the writer inside itself.
        state = getattr(self, '_observer_state', None)
        if state is not None and state['busy']:
            raise BovnarArgumentError(
                "the writer is in use by its own on_event observer: it cannot "
                "be written to, flushed, finished, reconfigured or destroyed "
                "from there")

    def finish(self) -> None:

        if self._ptr is None or self._finished:
            return
        self._check_not_in_sink()
        if self._struct_depth != 0:
            raise BovnarWriteError(
                ErrorCode.GOT_INCOMPLETE_BVNR_STREAM, 0,
                f"finish() called with {self._struct_depth} unclosed struct(s)"
            )
        ok = self._lib.bvnr_write_finish(self._ptr)
        self._close_owned_fd()
        if not ok:
            # Leave _finished clear: the C writer has latched the error, and a
            # second finish() must report that same error rather than return
            # silently. (The Rust writer documents the same contract.)
            self._raise_error()
        self._finished = True

    def flush(self) -> None:
        """Hand everything written so far to the sink now, then call its
        flush (the object's ``flush()`` for :meth:`to_stream`), instead of
        when the 64 KiB buffer fills or at :meth:`finish`: what a consumer
        reading live needs after each record.

        Legal between any two writes, inside an open struct as well; the
        bytes come out exactly as without it, since a separator owed to the
        next value stays owed. A no-op for :meth:`to_mem` and after
        :meth:`finish`."""
        if self._ptr is None:
            raise BovnarArgumentError("Writer has been destroyed")
        self._check_not_in_sink()
        if not self._lib.bvnr_write_flush(self._ptr):
            self._raise_error()

    def destroy(self) -> None:

        self._check_not_in_sink()
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
        self._check_not_in_sink()
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
                "convert=False); for write-side unit rewriting open the writer "
                "with unit_flags=UnitFlags.REDUCE")
        try:
            cp, keepalive = build_unit_policy(
                require_unit=policy.require_unit,
                require_dimension_of=policy.require_dimension_of,
                rules=policy.rules)
        except ValueError as e:
            raise BovnarArgumentError(str(e)) from None
        if not self._lib.bvnr_writer_set_unit_policy(self._ptr, ctypes.byref(cp)):
            # The write-side-only refusals (targets, normalise, base, inexact,
            # convert rules) are caught above with their own message, so what
            # reaches here is a unit or a rule path the parser will not take --
            # exactly what the reader's setter refuses, and the same probe
            # names it. `rules` was missing from the old message entirely.
            raise BovnarArgumentError(describe_unit_policy_refusal(
                self._lib, require_unit=policy.require_unit,
                require_dimension_of=policy.require_dimension_of,
                rules=policy.rules))
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
        if self._observer_state is not None or self._stream is not None:
            self._check_not_in_sink()

        d     = BvnrData()
        _keep = []

        if key is not None:
            raw = key.encode('utf-8')
            _keep.append(raw)
            d.data   = ctypes.cast(ctypes.c_char_p(raw), ctypes.c_void_p).value
            d.length = len(raw)

        if vt is not None:
            d.value_type = vt

        if vu is not None:
            d.value_unit = vu

        if value is not None:
            raw = value.encode('utf-8')
            _keep.append(raw)
            d.data   = ctypes.cast(ctypes.c_char_p(raw), ctypes.c_void_p).value
            d.length = len(raw)

            if vt is not None:
                fam = int(vt.family)
                if fam == int(ValueTypeFamily.UTF8):
                    d.type = self._TOKEN_IS_STRING
                elif (vt.base not in (0, 10)
                      and fam not in _DECIMAL_FLOAT_FAMILIES):
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
                          vu: ValueUnit,
                          unit_source: str | None = None) -> None:

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
        fam = int(vt.family)

        if fam in _WIDTH_FAMILIES and vt.width != 0:
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
                d_ep.data = ctypes.cast(ctypes.c_char_p(epoch), ctypes.c_void_p).value
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
        #
        # A utf8 value is the same shape and was NOT excluded. The format lets a
        # string carry a unit — `.s = "x" m;` is legal, and the grammar says so
        # (`scalar-with-unit = (number | string), ws, inline-unit`) — but only as
        # an INLINE suffix; `<utf8,m>` is error_illegal_value_type. Emitting it
        # as an annotation parameter produced `<utf8:m> "x"`, which this
        # library's own reader refuses, so `loads(typed=True)` → `dumps` →
        # `loads` failed on a document it had just read. The C writer places it
        # inline (`<utf8> "x" m;`) and round-trips; leaving the parameter off
        # lets the value event's unit reach it and do the same.
        if vu.num_components > 0 and fam not in _NO_UNIT_PARAM_FAMILIES:
            lib = self._lib
            ubuf = ctypes.create_string_buffer(UNIT_STRING_MAX)
            unit_flags = lib.bvnr_writer_unit_flags(self._ptr)
            ulen = lib.bvn_unit_to_string_ex(vu, ubuf, UNIT_STRING_MAX,
                                             unit_flags)
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
                d_unit.data = ctypes.cast(ctypes.c_char_p(raw_unit), ctypes.c_void_p).value
                d_unit.length = ulen
                # library 2.0 — the producer's own spelling, when one was kept. The
                # C writer checks it against `vu` and ignores it under a unit
                # flag; `_keep_src` must outlive the call.
                if unit_source:
                    _keep_src = unit_source.encode('utf-8')
                    d_unit.unit_source = ctypes.cast(ctypes.c_char_p(_keep_src), ctypes.c_void_p).value
                    d_unit.unit_source_length = len(_keep_src)
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

    def write_version(self, major: int | None = None,
                      minor: int | None = None) -> None:
        """Emit a leading ``#!bovnar <major>.<minor>`` directive. Must be called
        before any value is written (else error_invalid_argument).

        With no arguments it names the spec version this build implements
        (:func:`bovnar.spec_version`), the same directive ``emit_version=True``
        writes. Pass both to declare an older version explicitly; giving only
        one raises BovnarArgumentError."""
        self._check_writable()
        if major is None and minor is None:
            maj, mn = ctypes.c_uint16(0), ctypes.c_uint16(0)
            self._lib.bvnr_spec_version(ctypes.byref(maj), ctypes.byref(mn))
            major, minor = maj.value, mn.value
        elif major is None or minor is None:
            raise BovnarArgumentError(
                "write_version() takes both major and minor, or neither")
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
                   unit_iec_prefix: IECPrefix = IECPrefix.NONE,
                   unit_source: str | None = None) -> None:

        vt  = make_type_spec(ValueTypeFamily.UINT, width, base)
        vu  = self._resolve_unit(unit_str, unit_si_base, unit_si_prefix,
                                 unit_si_exp, unit_iec_base, unit_iec_prefix)
        val = self._format_uint(value, base, width)
        self._write_scalar(key, 'uint', vt, vu, val, unit_source)

    def write_sint(self,
                   key: str,
                   value: int,
                   *,
                   width: int = 64,
                   base: int = 10,
                   unit_str: str | None = None,
                   unit_si_base: BaseUnit | None = None,
                   unit_si_prefix: SIPrefix = SIPrefix.NONE,
                   unit_si_exp: Exponent = Exponent.LINEAR,
                   unit_source: str | None = None) -> None:

        self._refuse_signed_high_base(base)
        vt  = make_type_spec(ValueTypeFamily.SINT, width, base)
        vu  = self._resolve_unit(unit_str, unit_si_base, unit_si_prefix,
                                 unit_si_exp, None, IECPrefix.NONE)
        val = self._format_sint(value, base)
        self._write_scalar(key, 'sint', vt, vu, val, unit_source)

    def write_float(self,
                    key: str,
                    value: float,
                    *,
                    width: int = 64,
                    unit_str: str | None = None,
                    unit_si_base: BaseUnit | None = None,
                    unit_si_prefix: SIPrefix = SIPrefix.NONE,
                    unit_si_exp: Exponent = Exponent.LINEAR,
                    unit_source: str | None = None) -> None:

        vt  = make_type_spec(ValueTypeFamily.FLOAT, width, 0)
        vu  = self._resolve_unit(unit_str, unit_si_base, unit_si_prefix,
                                 unit_si_exp, None, IECPrefix.NONE)
        val = self._format_float(value, vt)
        self._write_scalar(key, 'float', vt, vu, val, unit_source)

    def write_float_fix(self,
                        key: str,
                        value: float,
                        *,
                        width: int = 64,
                        q: int = 0,
                        unit_str: str | None = None,
                        unit_si_base: BaseUnit | None = None,
                        unit_si_prefix: SIPrefix = SIPrefix.NONE,
                        unit_si_exp: Exponent = Exponent.LINEAR,
                        unit_source: str | None = None) -> None:

        vt  = make_type_spec(ValueTypeFamily.FLOAT_FIX, width, q)
        vu  = self._resolve_unit(unit_str, unit_si_base, unit_si_prefix,
                                 unit_si_exp, None, IECPrefix.NONE)
        val = self._format_float(value, make_type_spec(ValueTypeFamily.FLOAT, width, 0))
        self._write_scalar(key, 'float_fix', vt, vu, val, unit_source)

    def write_float_dec(self,
                        key: str,
                        value: float,
                        *,
                        width: int = 64,
                        unit_str: str | None = None,
                        unit_si_base: BaseUnit | None = None,
                        unit_si_prefix: SIPrefix = SIPrefix.NONE,
                        unit_si_exp: Exponent = Exponent.LINEAR,
                        unit_source: str | None = None) -> None:

        vt  = make_type_spec(ValueTypeFamily.FLOAT_DEC, width, 0)
        vu  = self._resolve_unit(unit_str, unit_si_base, unit_si_prefix,
                                 unit_si_exp, None, IECPrefix.NONE)
        val = self._format_float(value, make_type_spec(ValueTypeFamily.FLOAT, width, 0))
        self._write_scalar(key, 'float_dec', vt, vu, val, unit_source)

    def write_plain(self, key: str, value: str) -> None:
        """``.key = value;`` with ``value`` written bare, as a plain symbol
        (``bvnr_write_plain``): ``.mode = auto;``. The C writer refuses a
        value that is not a symbol or is a reserved word (``null``, ``true``,
        ``false``, ``on``, ``off``, ``nan``, ``inf``, ``ninf``) with
        ``TYPE_VALUE_MISMATCH``, an empty one with ``EMPTY_IDENTIFIER`` and
        one too long with ``SYMBOL_TOO_LONG``. A ``key`` is refused first,
        as :func:`bovnar.dumps` refuses it, with ``BovnarArgumentError``, and
        so is a ``value`` holding NUL or a lone surrogate."""
        from .stream import _utf8, _writer_key  # deferred: avoids an import cycle
        self._check_writable()
        if not isinstance(value, str) or '\0' in value:
            raise BovnarArgumentError(
                f"write_plain() takes a str without NUL bytes, not {value!r}")
        kb = _writer_key(key)
        if not self._lib.bvnr_write_plain(self._ptr, kb,
                                          _utf8(value, "a plain value")):
            self._raise_error()

    def write_datetime(self, key: str, value: int, *, width: int = 64,
                       epoch: str | None = None) -> None:
        """``.key = <datetime:width[,epoch]> value;``: ``value`` signed
        seconds since ``epoch`` (``"unix"``, the default, ``"tai"``,
        ``"gps"``, ``"mjd"``, ``"ntp"``, ``"galileo"``, ``"glonass"``,
        ``"y2000"``, ``"beidou"``), as ``bvnr_write_datetime`` writes it.

        Datetime is spec 1.1, so the document must have declared it first:
        open the writer with ``emit_version=True`` or call
        :meth:`write_version` before any value. Without that the write fails
        with ``UNSUPPORTED_SPEC_VERSION``; the key is in the output by then
        and the writer is in its error state. An unknown epoch is
        ``INVALID_ARGUMENT`` from the C writer, with the same effect; it is
        refused here first, before anything is written."""
        from .stream import _writer_key     # deferred: avoids an import cycle
        self._check_writable()
        if type(value) is not int or not -(1 << 63) <= value < (1 << 63):
            raise BovnarArgumentError(
                f"write_datetime() takes an int that fits 64 signed bits, "
                f"not {value!r}")
        if type(width) is not int or not 0 <= width < (1 << 32):
            raise BovnarArgumentError(f"invalid datetime width {width!r}")
        ep = None
        if epoch is not None:
            # A NUL would end the name early in C: 'tai\0x' wrote tai.
            if not isinstance(epoch, str) or '\0' in epoch:
                raise BovnarArgumentError(
                    f"write_datetime() takes the epoch as a str without NUL "
                    f"bytes, not {epoch!r}")
            ep = epoch.encode('utf-8')
            if self._lib.bvnr_datetime_epoch_index(ep) < 0:
                raise BovnarArgumentError(f"unknown datetime epoch {epoch!r}")
        if not self._lib.bvnr_write_datetime(self._ptr, _writer_key(key),
                                             width, ep, value):
            self._raise_error()

    def write_type_annotation(self, vt: ValueTypeSpec, unit=None, *,
                              unit_source: str | None = None) -> None:
        """A type annotation, ``<family:width[,_base|,qQ|,epoch][,unit]>``,
        for the value written next (``bvnr_write_type_annotation``): after
        the key of an assignment, with :meth:`emit` sending the value, or
        before the first row of an array, which it then types whole.
        ``unit`` is a :class:`Unit`, a ``ValueUnit`` or a unit string;
        ``unit_source`` its own spelling, written in place of the canonical
        one (the C writer checks that it spells ``unit``).

            w.emit(Event.ASSIGNMENT_START, key="v")
            w.write_type_annotation(make_type_spec(ValueTypeFamily.UINT, 8))
            w.begin_array_row(); ...

        Only the parameters that say something are written, as for the typed
        ``write_*`` methods: no width of 0, no base 10, no unit on a string
        or a datetime."""
        self._check_writable()
        if not isinstance(vt, ValueTypeSpec):
            raise BovnarArgumentError(
                f"write_type_annotation() takes a ValueTypeSpec, not "
                f"{type(vt).__name__}")
        if unit is None:
            vu = make_unit_none()
        elif isinstance(unit, str):
            vu = self._resolve_unit(unit, None, SIPrefix.NONE,
                                    Exponent.LINEAR, None, IECPrefix.NONE)
        else:
            from .unit import _as_vu        # deferred, as the package root does
            # Raises TypeError for anything that is not a Unit or a
            # ValueUnit, the one rule for unit-shaped arguments (unit.py).
            vu = _as_vu(unit)
        if unit_source is not None and vu.num_components == 0:
            raise BovnarArgumentError(
                f"unit_source {unit_source!r} given without a unit")
        name = ValueTypeFamily(int(vt.family)).name.lower()
        self._emit_annotation(name, vt, vu, unit_source)

    def write_string(self, key: str, value: str) -> None:

        self.emit(Event.ASSIGNMENT_START, key=key)
        raw = value.encode('utf-8')
        _keep = raw
        d = BvnrData()
        d.type = self._TOKEN_IS_STRING
        d.data = ctypes.cast(ctypes.c_char_p(raw), ctypes.c_void_p).value
        d.length = len(raw)
        ok = self._lib.bvnr_write_event(self._ptr, int(Event.DATA), ctypes.byref(d))
        if not ok:
            self._raise_error()

    def write_bool(self, key: str, value: bool) -> None:

        if not isinstance(value, bool):
            raise BovnarArgumentError(
                f"write_bool takes a bool, not {type(value).__name__}")
        self.emit(Event.ASSIGNMENT_START, key=key)
        sym = b'true' if value else b'false'
        d = BvnrData()
        d.type = self._TOKEN_IS_BOOL
        d.data = ctypes.cast(ctypes.c_char_p(sym), ctypes.c_void_p).value
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

    def write_octets(self, key: str, value) -> None:
        """``.key = <octet stream>;`` with the bytes of ``value`` (any
        bytes-like object), as :func:`bovnar.dumps` writes a ``bytes``
        value. ``key`` and ``value`` are checked before anything is
        written, ``key`` as :func:`bovnar.dumps` checks it."""
        from . import _emit_octets  # deferred: avoids an import cycle
        from .stream import _as_bytes, _writer_key
        self._check_writable()
        _writer_key(key)
        data = _as_bytes(value)
        self.emit(Event.ASSIGNMENT_START, key=key)
        _emit_octets(self, data)

    def embed_document(self, key: str, doc) -> None:
        """``.key = <octets>;`` where the octets are the document ``doc``
        (``bvnr_embed_document``), for :func:`bovnar.stream.parse_embedded`
        to read back. Unlike :func:`bovnar.stream.embed_document`, which makes
        a whole document of one key, this embeds into the document being
        written, one after another if need be."""
        from .stream import _writer_key, _as_bytes
        self._check_writable()
        kb = _writer_key(key)
        doc = _as_bytes(doc)
        # bvnr_embed_document copies the bytes into the writer's output
        # before returning; they are handed over as they are.
        if not self._lib.bvnr_embed_document(self._ptr, kb, doc, len(doc)):
            self._raise_error()

    def mux_begin(self, key: str) -> None:
        """Open a multiplexed octet stream under ``key`` (``bvnr_mux_begin``):
        send messages with :meth:`mux_send` and close it with
        :meth:`mux_end`. Values written before it are read back before the
        first message, and the document may go on after it -- more values, or
        another stream -- as :class:`bovnar.stream.DemuxFeed` and
        :func:`bovnar.stream.mux_load` read it."""
        from .stream import _writer_key
        self._check_writable()
        if not self._lib.bvnr_mux_begin(self._ptr, _writer_key(key)):
            self._raise_error()

    def mux_send(self, channel: int, payload) -> None:
        """One message on ``channel`` (an unsigned 64-bit int) of the open
        mux stream (``bvnr_mux_send``), split over as many octet chunks as it
        needs. Call :meth:`flush` after it for a consumer reading live."""
        from .stream import _mux_channel, _as_bytes
        self._check_writable()
        ch = _mux_channel(channel)
        data = payload if type(payload) is bytes else _as_bytes(payload)
        if not self._lib.bvnr_mux_send(self._ptr, ch, data, len(data)):
            self._raise_error()

    def mux_end(self) -> None:
        """Close the open mux stream (``bvnr_mux_end``)."""
        self._check_writable()
        if not self._lib.bvnr_mux_end(self._ptr):
            self._raise_error()

    def _check_writable(self) -> None:
        if self._ptr is None:
            raise BovnarArgumentError("Writer has been destroyed")
        self._check_not_in_sink()

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
                      val: str,
                      unit_source: str | None = None) -> None:
        # unit_source: the unit's own spelling, written in the annotation in
        # place of the canonical one (bvnr_data_t.unit_source; the C writer
        # checks that it spells this unit). A value without a unit has no
        # annotation parameter to carry it, so it is refused, as
        # bvnr_writer_set_unit_source refuses a spelling for one.
        if unit_source is not None and vu.num_components == 0:
            raise BovnarArgumentError(
                f"unit_source {unit_source!r} given for a value without a unit")
        self.emit(Event.ASSIGNMENT_START, key=key)
        self._emit_annotation(family_name, vt, vu, unit_source)

        # Both sets are module constants: rebuilding them here put two
        # frozenset allocations on every scalar written.
        if (vt.base not in (0, 10) and val not in _SPECIAL_FLOAT_TEXTS
                and int(vt.family) not in _DECIMAL_FLOAT_FAMILIES):
            data_token_type = self._TOKEN_IS_STRING
        else:
            data_token_type = self._TOKEN_IS_NUMBER
        d = BvnrData()
        d.type = data_token_type
        d.value_type = vt
        d.value_unit = vu
        raw = val.encode('utf-8')
        _keep = raw
        d.data = ctypes.cast(ctypes.c_char_p(raw), ctypes.c_void_p).value
        d.length = len(raw)
        ok = self._lib.bvnr_write_event(
            self._ptr, int(Event.DATA), ctypes.byref(d))
        if not ok:
            self._raise_error()

    @staticmethod
    def _refuse_signed_high_base(base: int) -> None:
        # Base64 and Ascii85 have '+' and '-' among their DIGITS, so there is
        # no sign to write and the format has no signed integer in either. This
        # used to surface as "bvn_format_int64 overflow" carrying
        # ErrorCode.NONE from write_sint, and as illegal_value_type from
        # write_bvni -- neither of which names the cause.
        if base in (64, 85):
            raise BovnarArgumentError(
                f"base {base} is unsigned: its alphabet uses '+' and '-' as "
                "digits, so a signed integer cannot be written in it")

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
                message=f"uint value {int_repr(value)} does not fit a 64-bit field "
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
                message=f"sint value {int_repr(value)} does not fit a 64-bit field "
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
            # float:16 and float:32 round to IEEE binary16 and binary32, and
            # a finite value the format cannot hold is refused, as the C
            # writer refuses it.
            if (vt.family == ValueTypeFamily.FLOAT and vt.width in (16, 32)
                    and math.isfinite(value)):
                raise BovnarWriteError(
                    ErrorCode.VALUE_OUT_OF_RANGE, 0,
                    f"{value!r} does not fit a float:{vt.width}")
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

        if base == 10:
            # str() refuses past 4300 digits (int_max_str_digits); the format
            # holds ~9865.
            return int_to_decimal(value)
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
                        unit_si_exp: Exponent = Exponent.LINEAR,
                        unit_source: str | None = None) -> None:

        if base not in (10, 16):
            raise BovnarArgumentError(
                f"write_bvnf_base: base must be 10 or 16, got {base!r}")
        vt_base = 0 if base == 10 else 16
        vt  = make_type_spec(ValueTypeFamily.FLOAT, width, vt_base)
        vu  = self._resolve_unit(unit_str, unit_si_base, unit_si_prefix,
                                 unit_si_exp, None, IECPrefix.NONE)
        self._write_scalar(key, 'float', vt, vu, value_str, unit_source)

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
                   unit_si_exp: Exponent = Exponent.LINEAR,
                   unit_source: str | None = None) -> None:

        if signed is None:
            use_signed = value < 0
        else:
            use_signed = signed
        if use_signed:
            self._refuse_signed_high_base(base)
        family = ValueTypeFamily.SINT if use_signed else ValueTypeFamily.UINT
        vt_base = 0 if base == 10 else base
        vt  = make_type_spec(family, width, vt_base)
        vu  = self._resolve_unit(unit_str, unit_si_base, unit_si_prefix,
                                 unit_si_exp, None, IECPrefix.NONE)
        val = self._format_bigint(value, base)
        family_name = 'sint' if use_signed else 'uint'
        self._write_scalar(key, family_name, vt, vu, val, unit_source)

    def _raise_error(self) -> None:
        # A stream writer's sink fails because its object raised; that
        # exception is the error, not the error_writing_to_sink it became. It
        # is raised once, from the call that pushed: a later call on the failed
        # writer raises the latched BovnarWriteError, chained to it, so it does
        # not look like a second failure of the transport.
        # The on_event observer's exception is the same kind of failure: the
        # write it stopped raises it, later ones the latched
        # SCANNER_CALLBACK_FAILED chained to it.
        state = self._observer_state
        if state is not None and state['exc'] is not None:
            exc, state['exc'] = state['exc'], None
            self._stream_failure = exc
            raise exc
        if self._stream is not None and self._stream.exc is not None:
            exc, self._stream.exc = self._stream.exc, None
            self._stream_failure = exc
            raise exc
        cause = getattr(self, '_stream_failure', None)
        code   = ErrorCode(self._lib.bvnr_writer_get_error(self._ptr))
        offset = self._lib.bvnr_writer_get_error_offset(self._ptr)
        msg_b  = self._lib.bvn_error_to_string(int(code))
        msg    = msg_b.decode('utf-8') if msg_b else ''
        if cause is not None:
            raise BovnarWriteError(code, offset, msg) from cause
        raise BovnarWriteError(code, offset, msg)


