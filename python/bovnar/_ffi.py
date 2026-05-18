import ctypes
import ctypes.util
import os
from pathlib import Path

from .structs import (
    BvnrSource, BvnrSink,
    BvnrReadFlags, BvnrWriteFlags,
    BvnrData, ValueTypeSpec, ValueUnit, ValueUnitPrefix,
    BvnDomEntry,
    EVENT_CALLBACK_FUNC, ON_ERROR_FUNC,
)
from .exceptions import BovnarLibraryNotFound

_lib: ctypes.CDLL | None = None

_LIBRARY_NAME = 'libbvnr_shared.so'
_LIBRARY_BASE = 'bovnar'

def _candidate_paths() -> list[str]:
    paths: list[str] = []

    env_path = os.environ.get('LIBBOVNAR_PATH')
    if env_path:
        paths.append(env_path)

    env_dir = os.environ.get('LIBBOVNAR_DIR')
    if env_dir:
        paths.append(str(Path(env_dir) / _LIBRARY_NAME))

    found = ctypes.util.find_library(_LIBRARY_BASE)
    if found:
        paths.append(found)

    script_dir = Path(__file__).parent
    for rel in ('../../build', '../../build/release', '../..', '.'):
        p = (script_dir / rel / _LIBRARY_NAME).resolve()
        if p.exists():
            paths.append(str(p))

    return paths

def load_library() -> ctypes.CDLL:
    global _lib
    if _lib is not None:
        return _lib

    candidates = _candidate_paths()
    last_error: Exception | None = None

    for path in candidates:
        try:
            candidate = ctypes.CDLL(path, use_errno=True)
            _declare_functions(candidate)
        except (OSError, AttributeError) as exc:
            last_error = exc
            continue
        _lib = candidate
        return _lib

    raise BovnarLibraryNotFound(candidates) from last_error

def get_library() -> ctypes.CDLL:
    return load_library()

def _declare_functions(lib: ctypes.CDLL) -> None:
    c_bool    = ctypes.c_bool
    c_int     = ctypes.c_int
    c_int8    = ctypes.c_int8
    c_uint8   = ctypes.c_uint8
    c_int16   = ctypes.c_int16
    c_uint16  = ctypes.c_uint16
    c_int32   = ctypes.c_int32
    c_uint32  = ctypes.c_uint32
    c_int64   = ctypes.c_int64
    c_uint64  = ctypes.c_uint64
    c_double  = ctypes.c_double
    c_size_t  = ctypes.c_size_t
    c_char_p  = ctypes.c_char_p
    c_void_p  = ctypes.c_void_p
    c_uint8_p = ctypes.POINTER(ctypes.c_uint8)

    P = ctypes.POINTER

    lib.bvnr_reader_create.restype  = c_void_p
    lib.bvnr_reader_create.argtypes = []

    lib.bvnr_reader_destroy.restype  = None
    lib.bvnr_reader_destroy.argtypes = [c_void_p]

    lib.bvnr_canon_observer_create.restype  = c_void_p
    lib.bvnr_canon_observer_create.argtypes = [P(BvnrSink), c_bool]

    lib.bvnr_canon_observer_destroy.restype  = None
    lib.bvnr_canon_observer_destroy.argtypes = [c_void_p]

    lib.bvnr_canon_observer_on_event.restype  = c_bool
    lib.bvnr_canon_observer_on_event.argtypes = [c_void_p, c_int, P(BvnrData)]

    lib.bvnr_canon_observer_finish.restype  = c_bool
    lib.bvnr_canon_observer_finish.argtypes = [c_void_p]

    lib.bvnr_source_from_fd.restype  = None
    lib.bvnr_source_from_fd.argtypes = [P(BvnrSource), c_int]

    lib.bvnr_source_from_mem.restype  = None
    lib.bvnr_source_from_mem.argtypes = [P(BvnrSource), c_void_p, c_uint64]

    lib.bvnr_open_read_source.restype  = c_bool
    lib.bvnr_open_read_source.argtypes = [
        c_void_p, P(BvnrSource), P(BvnrSink), P(BvnrReadFlags),
    ]

    lib.bvnr_open_read_mem.restype  = c_bool
    lib.bvnr_open_read_mem.argtypes = [
        c_void_p, c_void_p, c_uint64, c_void_p, c_uint32, P(BvnrReadFlags),
    ]

    lib.bvnr_read.restype  = c_bool
    lib.bvnr_read.argtypes = [c_void_p]

    lib.bvnr_reader_get_error.restype  = c_int
    lib.bvnr_reader_get_error.argtypes = [c_void_p]

    lib.bvnr_reader_get_error_line.restype  = c_uint64
    lib.bvnr_reader_get_error_line.argtypes = [c_void_p]

    lib.bvnr_reader_get_error_column.restype  = c_uint64
    lib.bvnr_reader_get_error_column.argtypes = [c_void_p]

    lib.bvnr_reader_get_error_offset.restype  = c_uint64
    lib.bvnr_reader_get_error_offset.argtypes = [c_void_p]

    lib.bvnr_reader_get_error_byte.restype  = c_uint32
    lib.bvnr_reader_get_error_byte.argtypes = [c_void_p]

    lib.bvnr_reader_get_recovery_count.restype  = c_uint64
    lib.bvnr_reader_get_recovery_count.argtypes = [c_void_p]

    lib.bvnr_writer_create.restype  = c_void_p
    lib.bvnr_writer_create.argtypes = []

    lib.bvnr_writer_destroy.restype  = None
    lib.bvnr_writer_destroy.argtypes = [c_void_p]

    lib.bvnr_sink_to_fd.restype  = None
    lib.bvnr_sink_to_fd.argtypes = [P(BvnrSink), c_int]

    lib.bvnr_sink_to_mem.restype  = None
    lib.bvnr_sink_to_mem.argtypes = [P(BvnrSink), c_void_p, c_uint32]

    lib.bvnr_sink_bytes_written.restype  = c_uint64
    lib.bvnr_sink_bytes_written.argtypes = [P(BvnrSink)]

    lib.bvnr_open_write_sink.restype  = c_bool
    lib.bvnr_open_write_sink.argtypes = [
        c_void_p, P(BvnrSink), c_bool, P(BvnrWriteFlags),
    ]

    lib.bvnr_open_write_mem.restype  = c_bool
    lib.bvnr_open_write_mem.argtypes = [
        c_void_p, c_void_p, c_uint32, c_bool, P(BvnrWriteFlags),
    ]

    lib.bvnr_write_event.restype  = c_bool
    lib.bvnr_write_event.argtypes = [c_void_p, c_int, P(BvnrData)]

    lib.bvnr_write_finish.restype  = c_bool
    lib.bvnr_write_finish.argtypes = [c_void_p]

    lib.bvnr_writer_get_error.restype  = c_int
    lib.bvnr_writer_get_error.argtypes = [c_void_p]

    lib.bvnr_writer_get_error_offset.restype  = c_uint64
    lib.bvnr_writer_get_error_offset.argtypes = [c_void_p]

    lib.bvnr_writer_bytes_written.restype  = c_uint64
    lib.bvnr_writer_bytes_written.argtypes = [c_void_p]

    lib.bvnr_writer_unit_flags.restype  = c_uint32
    lib.bvnr_writer_unit_flags.argtypes = [c_void_p]

    lib.bvn_parse_uint64.restype  = c_bool
    lib.bvn_parse_uint64.argtypes = [c_char_p, ValueTypeSpec, P(c_uint64)]

    lib.bvn_parse_int64.restype  = c_bool
    lib.bvn_parse_int64.argtypes = [c_char_p, ValueTypeSpec, P(c_int64)]

    lib.bvn_parse_double.restype  = c_bool
    lib.bvn_parse_double.argtypes = [c_char_p, ValueTypeSpec, P(c_double)]

    lib.bvn_format_uint64.restype  = c_int32
    lib.bvn_format_uint64.argtypes = [c_char_p, c_size_t, c_uint64, c_uint32, c_uint32]

    lib.bvn_format_int64.restype  = c_int32
    lib.bvn_format_int64.argtypes = [c_char_p, c_size_t, c_int64, c_uint32, c_uint32]

    lib.bvn_format_double.restype  = c_int32
    lib.bvn_format_double.argtypes = [c_char_p, c_size_t, c_double, ValueTypeSpec]

    lib.bvn_parse_unit.restype  = ValueUnit
    lib.bvn_parse_unit.argtypes = [c_uint8_p, P(c_bool)]

    lib.bvn_parse_unit_n.restype  = ValueUnit
    lib.bvn_parse_unit_n.argtypes = [c_uint8_p, c_uint32, P(c_bool)]

    lib.bvn_unit_to_string.restype  = c_int32
    lib.bvn_unit_to_string.argtypes = [ValueUnit, c_char_p, c_size_t]

    lib.bvn_unit_to_string_ex.restype  = c_int32
    lib.bvn_unit_to_string_ex.argtypes = [ValueUnit, c_char_p, c_size_t, c_uint32]

    lib.bvn_unit_valid.restype  = c_bool
    lib.bvn_unit_valid.argtypes = [ValueUnit]

    lib.bvn_unit_prefix_factor.restype  = c_double
    lib.bvn_unit_prefix_factor.argtypes = [ValueUnit]

    lib.bvn_unit_prefix_exponent.restype  = c_int32
    lib.bvn_unit_prefix_exponent.argtypes = [ValueUnit]

    lib.bvn_units_compatible.restype  = c_bool
    lib.bvn_units_compatible.argtypes = [ValueUnit, ValueUnit]

    lib.bvn_unit_convert_factor.restype  = c_double
    lib.bvn_unit_convert_factor.argtypes = [
        ValueUnit, ValueUnit, P(c_bool), P(c_bool),
    ]

    lib.bvn_unit_to_si_factor.restype  = c_double
    lib.bvn_unit_to_si_factor.argtypes = [
        ValueUnit, P(c_bool), P(c_double), P(c_bool),
    ]

    lib.bvn_unit_dimension_vector.restype  = c_bool
    lib.bvn_unit_dimension_vector.argtypes = [ValueUnit, P(c_int32)]

    lib.bvn_unit_reduce.restype  = ValueUnit
    lib.bvn_unit_reduce.argtypes = [ValueUnit, P(c_double), P(c_bool)]

    lib.bvn_exponent_to_int.restype  = c_int32
    lib.bvn_exponent_to_int.argtypes = [c_int]

    lib.bvn_int_to_exponent.restype  = c_int
    lib.bvn_int_to_exponent.argtypes = [c_int32]

    lib.bvn_prefix_unit_valid.restype  = c_bool
    lib.bvn_prefix_unit_valid.argtypes = [ValueUnitPrefix, c_int]

    lib.bvn_validate_identifier.restype  = c_bool
    lib.bvn_validate_identifier.argtypes = [c_char_p]

    lib.bvn_validate_symbol.restype  = c_bool
    lib.bvn_validate_symbol.argtypes = [c_char_p]

    lib.bvn_validate_reference.restype  = c_bool
    lib.bvn_validate_reference.argtypes = [c_char_p]

    lib.bvn_validate_uint_range.restype  = c_bool
    lib.bvn_validate_uint_range.argtypes = [c_char_p, c_uint32, c_uint32]

    lib.bvn_validate_sint_range.restype  = c_bool
    lib.bvn_validate_sint_range.argtypes = [c_char_p, c_uint32, c_uint32]

    lib.bvn_is_special_number_string.restype  = c_bool
    lib.bvn_is_special_number_string.argtypes = [c_char_p]

    lib.bvn_error_to_string.restype  = c_char_p
    lib.bvn_error_to_string.argtypes = [c_int]

    lib.bvn_dom_doc_create.restype  = c_void_p
    lib.bvn_dom_doc_create.argtypes = []

    lib.bvn_dom_doc_destroy.restype  = None
    lib.bvn_dom_doc_destroy.argtypes = [c_void_p]

    lib.bvn_dom_parse.restype  = c_void_p
    lib.bvn_dom_parse.argtypes = [c_void_p, c_uint32]

    lib.bvn_dom_parse_fd.restype  = c_void_p
    lib.bvn_dom_parse_fd.argtypes = [c_int]

    lib.bvn_dom_doc_get_parse_error.restype  = c_int
    lib.bvn_dom_doc_get_parse_error.argtypes = [c_void_p]

    lib.bvn_dom_lookup.restype  = c_void_p
    lib.bvn_dom_lookup.argtypes = [c_void_p, c_char_p]

    lib.bvn_dom_struct_get.restype  = c_void_p
    lib.bvn_dom_struct_get.argtypes = [c_void_p, c_char_p]

    lib.bvn_dom_array_at.restype  = c_void_p
    lib.bvn_dom_array_at.argtypes = [c_void_p, c_uint32]

    lib.bvn_dom_node_type.restype  = c_int
    lib.bvn_dom_node_type.argtypes = [c_void_p]

    lib.bvn_dom_is_null.restype  = c_bool
    lib.bvn_dom_is_null.argtypes = [c_void_p]

    lib.bvn_dom_get_float.restype  = c_bool
    lib.bvn_dom_get_float.argtypes = [c_void_p, P(c_double)]

    lib.bvn_dom_get_string.restype  = c_bool
    lib.bvn_dom_get_string.argtypes = [c_void_p, P(c_void_p), P(c_uint32)]

    lib.bvn_dom_get_symbol.restype  = c_bool
    lib.bvn_dom_get_symbol.argtypes = [c_void_p, P(c_void_p), P(c_uint32)]

    lib.bvn_dom_get_reference.restype  = c_bool
    lib.bvn_dom_get_reference.argtypes = [c_void_p, P(c_void_p), P(c_uint32)]

    lib.bvn_dom_get_octets.restype  = c_bool
    lib.bvn_dom_get_octets.argtypes = [c_void_p, P(c_void_p), P(c_uint32)]

    lib.bvn_dom_get_value_type.restype  = ValueTypeSpec
    lib.bvn_dom_get_value_type.argtypes = [c_void_p]

    lib.bvn_dom_get_unit.restype  = ValueUnit
    lib.bvn_dom_get_unit.argtypes = [c_void_p]

    lib.bvn_dom_get_unit_string.restype  = c_int32
    lib.bvn_dom_get_unit_string.argtypes = [c_void_p, c_char_p, c_size_t]

    lib.bvn_dom_get_value_in_base_units.restype  = c_double
    lib.bvn_dom_get_value_in_base_units.argtypes = [c_void_p]

    lib.bvn_dom_struct_count.restype  = c_uint32
    lib.bvn_dom_struct_count.argtypes = [c_void_p]

    lib.bvn_dom_array_count.restype  = c_uint32
    lib.bvn_dom_array_count.argtypes = [c_void_p]

    lib.bvn_dom_array_dims.restype  = c_uint32
    lib.bvn_dom_array_dims.argtypes = [c_void_p]

    lib.bvn_dom_struct_entries.restype  = P(BvnDomEntry)
    lib.bvn_dom_struct_entries.argtypes = [c_void_p]

    lib.bvn_dom_doc_entries.restype  = P(BvnDomEntry)
    lib.bvn_dom_doc_entries.argtypes = [c_void_p]

    lib.bvn_dom_doc_count.restype  = c_uint32
    lib.bvn_dom_doc_count.argtypes = [c_void_p]

    lib.bvn_dom_get_i64.restype  = c_bool
    lib.bvn_dom_get_i64.argtypes = [c_void_p, P(c_int64)]

    lib.bvn_dom_get_u64.restype  = c_bool
    lib.bvn_dom_get_u64.argtypes = [c_void_p, P(c_uint64)]

    lib.bvn_dom_get_i32.restype  = c_bool
    lib.bvn_dom_get_i32.argtypes = [c_void_p, P(c_int32)]

    lib.bvn_dom_get_u32.restype  = c_bool
    lib.bvn_dom_get_u32.argtypes = [c_void_p, P(c_uint32)]

    lib.bvn_dom_get_i16.restype  = c_bool
    lib.bvn_dom_get_i16.argtypes = [c_void_p, P(c_int16)]

    lib.bvn_dom_get_u16.restype  = c_bool
    lib.bvn_dom_get_u16.argtypes = [c_void_p, P(c_uint16)]

    lib.bvn_dom_get_i8.restype   = c_bool
    lib.bvn_dom_get_i8.argtypes  = [c_void_p, P(c_int8)]

    lib.bvn_dom_get_u8.restype   = c_bool
    lib.bvn_dom_get_u8.argtypes  = [c_void_p, P(c_uint8)]

    lib.bvn_dom_int_to_str.restype  = c_void_p
    lib.bvn_dom_int_to_str.argtypes = [c_void_p, c_uint32]
