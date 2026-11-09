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

"""
File objects and sockets as bvnr sources and sinks (bvnr_source_from_callback,
bvnr_sink_to_callback).

The C reader and writer copy the source or sink they are given, so each
callback here is a closure over its Python object instead of a lookup through
the handle's user pointer. An exception raised inside a callback cannot cross
the C frames above it: it is kept on the adapter, the callback answers false so
that C stops, and the caller re-raises it once the C call has returned.
"""

import ctypes
import io

from ._ffi import get_library
from .exceptions import BovnarArgumentError
from .structs import BvnrSink, BvnrSource, PULL_FUNC, PUSH_FUNC, FLUSH_FUNC


def is_stream(obj) -> bool:
    """True for an object this module can read from: a socket or a binary
    file-like object, as opposed to a buffer of bytes.

    Whatever has a buffer is bytes, even with a ``read``: an ``mmap`` is read
    whole, not from its current position (which is its end once it has been
    written to)."""
    if isinstance(obj, (bytes, bytearray, memoryview, str)):
        return False
    try:
        memoryview(obj).release()
        return False
    except TypeError:
        pass
    return any(hasattr(obj, name)
               for name in ('recv_into', 'readinto1', 'readinto', 'read'))


def _refuse_text(obj) -> None:
    if isinstance(obj, io.TextIOBase):
        raise BovnarArgumentError(
            "a text-mode file was given; Bovnar is read and written as bytes, "
            "so open the file in binary mode ('rb' / 'wb')")


def _read_into(read, buf: int, want: int):
    """read(want) into the C buffer; None when a non-blocking object has
    nothing yet, as readinto() would answer."""
    chunk = read(want)
    if chunk is None:
        return None
    if isinstance(chunk, str):
        raise BovnarArgumentError(
            "read() returned str; open the file in binary mode")
    n = len(chunk)
    if n > want:
        raise BovnarArgumentError(f"read({want}) returned {n} bytes")
    if n:
        ctypes.memmove(buf, bytes(chunk), n)
    return n


class PullSource:
    """A bvnr_source_t that pulls from a socket or a binary file object.

    A socket is read with ``recv_into``; a file object with ``readinto1`` when
    it has one (one raw read, so a pipe or a socket file hands over what has
    arrived instead of blocking until the C reader's whole request is filled),
    else ``readinto``, else ``read``. A method that raises
    ``io.UnsupportedOperation`` is given up for the next one: an
    ``io.BufferedIOBase`` subclass that implements only ``read`` inherits a
    ``readinto1`` that raises it. A short read is fine; an empty one is
    end of input, as the C pull contract has it. A non-blocking object with
    nothing to read has no way to say "later" through a pull source, so that is
    an error here: feed such a transport to a DocumentFeed, FrameFeed or
    DemuxFeed.
    """

    __slots__ = ('handle', 'exc', '_cb')

    def __init__(self, obj, on_pull=None) -> None:
        # on_pull(buf_address, n) sees every pull's bytes before the reader
        # does (the canonicaliser sniffs the version directive with it).
        _refuse_text(obj)
        self.exc = None
        recv_into = getattr(obj, 'recv_into', None)
        # The file object's methods in order of preference: (True, readinto1),
        # (True, readinto), (False, read).
        chain = []
        for name in ('readinto1', 'readinto', 'read'):
            fn = getattr(obj, name, None)
            if fn is not None:
                chain.append((name != 'read', fn))
        if recv_into is None and not chain:
            raise BovnarArgumentError(
                f"cannot read from {type(obj).__name__}: it has no recv_into, "
                f"readinto or read")

        def pull(_src, buf, want, got):
            try:
                if recv_into is not None:
                    n = recv_into((ctypes.c_ubyte * want).from_address(buf), want)
                else:
                    while True:
                        into, fn = chain[0]
                        try:
                            n = (fn((ctypes.c_ubyte * want).from_address(buf))
                                 if into else _read_into(fn, buf, want))
                            break
                        except io.UnsupportedOperation:
                            if len(chain) == 1:
                                raise
                            del chain[0]
                if n is None:
                    raise BlockingIOError(
                        "the source is non-blocking and has no data yet; a "
                        "pull source cannot wait for it -- feed the bytes to "
                        "a DocumentFeed, FrameFeed or DemuxFeed instead")
                if type(n) is not int or not 0 <= n <= want:
                    raise BovnarArgumentError(
                        f"{type(obj).__name__} reported reading {n!r} bytes "
                        f"into a buffer of {want}")
                if on_pull is not None:
                    on_pull(buf, n)
                got[0] = n
                return True
            except BaseException as exc:            # noqa: BLE001 (re-raised by the caller)
                if self.exc is None:
                    self.exc = exc
                return False

        self._cb = PULL_FUNC(pull)
        self.handle = BvnrSource()
        get_library().bvnr_source_from_callback(ctypes.byref(self.handle),
                                                self._cb, None)


class PushSink:
    """A bvnr_sink_t that pushes into a socket (``sendall``) or a binary file
    object (``write``, repeated until it has taken every byte).

    The bytes are copied out of the writer's buffer before they are handed
    over, so a file object that keeps a reference to what it was given (a
    user-written one may) never holds memory the writer reuses. ``flush``, when
    the object has one, runs at ``Writer.flush()`` and ``Writer.finish()``.
    """

    __slots__ = ('handle', 'exc', 'busy', '_push', '_flush')

    def __init__(self, obj) -> None:
        _refuse_text(obj)
        self.exc = None
        # True while the writer is inside the object's write/flush: the
        # writer must not be destroyed from there (Writer.destroy refuses).
        self.busy = False
        sendall = getattr(obj, 'sendall', None)
        write = getattr(obj, 'write', None)
        flush = getattr(obj, 'flush', None)
        # Only a raw file object's None means "nothing taken, would block";
        # a duck-typed writer that returns None has taken everything.
        raw = isinstance(obj, io.RawIOBase)
        if sendall is None and write is None:
            raise BovnarArgumentError(
                f"cannot write to {type(obj).__name__}: it has no sendall or "
                f"write")
        # A non-blocking socket takes part of a buffer and then raises
        # BlockingIOError: the bytes already sent cannot be taken back, so the
        # stream would be cut mid-chunk. A writer needs a blocking transport.
        gettimeout = getattr(obj, 'gettimeout', None)
        if sendall is not None and gettimeout is not None:
            try:
                nonblocking = gettimeout() == 0.0
            except Exception:                       # noqa: BLE001 (duck-typed)
                nonblocking = False
            if nonblocking:
                raise BovnarArgumentError(
                    "the socket is non-blocking; a writer needs a blocking "
                    "socket (sock.setblocking(True), or a timeout)")

        def push(_sink, buf, n):
            self.busy = True
            try:
                data = ctypes.string_at(buf, n)
                if sendall is not None:
                    sendall(data)
                    return True
                # bytes for the first write, as a caller of write() expects;
                # a memoryview of the rest only after a short write.
                rest = data
                while rest:
                    k = write(rest)
                    if k is None:
                        if raw:
                            raise BlockingIOError(
                                "the sink is non-blocking and took nothing")
                        return True
                    if type(k) is not int or not 0 < k <= len(rest):
                        raise BovnarArgumentError(
                            f"{type(obj).__name__}.write() of {len(rest)} "
                            f"bytes returned {k!r}")
                    rest = memoryview(rest)[k:]
                return True
            except BaseException as exc:            # noqa: BLE001 (re-raised by the caller)
                if self.exc is None:
                    self.exc = exc
                return False
            finally:
                self.busy = False

        def do_flush(_sink):
            self.busy = True
            try:
                flush()
                return True
            except BaseException as exc:            # noqa: BLE001 (re-raised by the caller)
                if self.exc is None:
                    self.exc = exc
                return False
            finally:
                self.busy = False

        self._push = PUSH_FUNC(push)
        self._flush = FLUSH_FUNC(do_flush) if flush is not None else FLUSH_FUNC()
        self.handle = BvnrSink()
        get_library().bvnr_sink_to_callback(ctypes.byref(self.handle),
                                            self._push, self._flush, None)


class _AppendTo:
    """A bytearray as a file object: write() appends, and takes everything."""

    __slots__ = ('_buf',)

    def __init__(self, buf: bytearray) -> None:
        self._buf = buf

    def write(self, data) -> int:
        self._buf += data
        return len(data)


def push_sink(obj) -> PushSink:
    """A PushSink into a writable binary file object, a socket, or a
    bytearray (appended to)."""
    if isinstance(obj, bytearray):
        return PushSink(_AppendTo(obj))
    return PushSink(obj)
