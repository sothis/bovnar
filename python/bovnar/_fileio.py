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
    file-like object, as opposed to a buffer of bytes."""
    if isinstance(obj, (bytes, bytearray, memoryview, str)):
        return False
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
    else ``readinto``, else ``read``. A short read is fine; an empty one is
    end of input, as the C pull contract has it. A non-blocking object with
    nothing to read has no way to say "later" through a pull source, so that is
    an error here: feed such a transport to a DocumentFeed or DemuxFeed.
    """

    __slots__ = ('handle', 'exc', '_cb')

    def __init__(self, obj) -> None:
        _refuse_text(obj)
        self.exc = None
        recv_into = getattr(obj, 'recv_into', None)
        readinto = (getattr(obj, 'readinto1', None) or
                    getattr(obj, 'readinto', None))
        read = getattr(obj, 'read', None)
        if recv_into is None and readinto is None and read is None:
            raise BovnarArgumentError(
                f"cannot read from {type(obj).__name__}: it has no recv_into, "
                f"readinto or read")

        def pull(_src, buf, want, got):
            try:
                if recv_into is not None:
                    n = recv_into((ctypes.c_ubyte * want).from_address(buf), want)
                elif readinto is not None:
                    n = readinto((ctypes.c_ubyte * want).from_address(buf))
                else:
                    n = _read_into(read, buf, want)
                if n is None:
                    raise BlockingIOError(
                        "the source is non-blocking and has no data yet; a "
                        "pull source cannot wait for it -- feed the bytes to "
                        "a DocumentFeed or DemuxFeed instead")
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

    __slots__ = ('handle', 'exc', '_push', '_flush')

    def __init__(self, obj) -> None:
        _refuse_text(obj)
        self.exc = None
        sendall = getattr(obj, 'sendall', None)
        write = getattr(obj, 'write', None)
        flush = getattr(obj, 'flush', None)
        if sendall is None and write is None:
            raise BovnarArgumentError(
                f"cannot write to {type(obj).__name__}: it has no sendall or "
                f"write")

        def push(_sink, buf, n):
            try:
                data = ctypes.string_at(buf, n)
                if sendall is not None:
                    sendall(data)
                    return True
                view = memoryview(data)
                while view:
                    k = write(view)
                    if k is None:
                        raise BlockingIOError(
                            "the sink is non-blocking and took nothing")
                    view = view[k:]
                return True
            except BaseException as exc:            # noqa: BLE001 (re-raised by the caller)
                if self.exc is None:
                    self.exc = exc
                return False

        def do_flush(_sink):
            try:
                flush()
                return True
            except BaseException as exc:            # noqa: BLE001 (re-raised by the caller)
                if self.exc is None:
                    self.exc = exc
                return False

        self._push = PUSH_FUNC(push)
        self._flush = FLUSH_FUNC(do_flush) if flush is not None else FLUSH_FUNC()
        self.handle = BvnrSink()
        get_library().bvnr_sink_to_callback(ctypes.byref(self.handle),
                                            self._push, self._flush, None)
