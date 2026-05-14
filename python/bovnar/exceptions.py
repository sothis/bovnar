

from .enums import ErrorCode

class BovnarError(Exception):
    pass

class BovnarLibraryNotFound(BovnarError):
    def __init__(self, search_paths: list[str]) -> None:
        self.search_paths = search_paths
        paths = ', '.join(search_paths) if search_paths else '(none)'
        super().__init__(
            f"Cannot find libbvnr_shared.so.  Searched: {paths}.  "
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
