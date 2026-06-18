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



from .enums import ErrorCode

class BovnarError(Exception):
    pass

class BovnarLibraryNotFound(BovnarError):
    def __init__(self, search_paths: list[str]) -> None:
        self.search_paths = search_paths
        paths = ', '.join(search_paths) if search_paths else '(none)'
        super().__init__(
            f"Cannot find libbvnr.so.  Searched: {paths}.  "
            "Set LIBBOVNAR_PATH or install the library where ldconfig can find it."
        )

class BovnarParseError(BovnarError):
    def __init__(self,
                 code: ErrorCode,
                 line: int = 0,
                 column: int = 0,
                 offset: int = 0,
                 byte: int = 0,
                 message: str = '') -> None:
        self.code    = code
        self.line    = line
        self.column  = column
        self.offset  = offset
        self.byte    = byte
        msg = message or f"{code.name} (code {int(code)})"
        loc = f" at line {line} col {column} offset {offset}" if line else ''
        super().__init__(f"Parse error: {msg}{loc}")

class BovnarWriteError(BovnarError):
    def __init__(self,
                 code: ErrorCode,
                 offset: int = 0,
                 message: str = '') -> None:
        self.code   = code
        self.offset = offset
        msg = message or f"{code.name} (code {int(code)})"
        super().__init__(f"Write error: {msg} at offset {offset}")

class BovnarCallbackAbort(BovnarError):
    pass

class BovnarArgumentError(BovnarError):
    pass
