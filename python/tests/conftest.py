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

import pytest
import bovnar
from bovnar.exceptions import BovnarLibraryNotFound

def _lib_available() -> bool:
    try:
        bovnar._ffi.load_library()
        return True
    except (BovnarLibraryNotFound, OSError):
        return False

LIB_AVAILABLE = _lib_available()

needs_lib = pytest.mark.skipif(
    not LIB_AVAILABLE,
    reason="libbvnr_shared.so not found — set LIBBOVNAR_PATH to enable integration tests",
)

@pytest.fixture
def simple_scalar_uint():
    return b".port = <uint:16> 8080;\n"

@pytest.fixture
def simple_scalar_sint():
    return b".offset = <sint:32> -42;\n"

@pytest.fixture
def simple_scalar_float():
    return b".velocity = <float:64,m/s> 9.81;\n"

@pytest.fixture
def simple_scalar_string():
    return b'.host = "localhost";\n'

@pytest.fixture
def untyped_integer():
    return b".count = 1000;\n"

@pytest.fixture
def untyped_float():
    return b".ratio = 3.14;\n"

@pytest.fixture
def untyped_negative():
    return b".delta = -7;\n"

@pytest.fixture
def typed_hex():
    return b'.flags = <uint:_16> "ff";\n'

@pytest.fixture
def struct_payload():
    return (
        b".config = {\n"
        b'    .host = "localhost";\n'
        b"    .port = <uint:16> 8080;\n"
        b"};\n"
    )

@pytest.fixture
def nested_struct():
    return (
        b".server = {\n"
        b'    .host = "example.com";\n'
        b"    .limits = {\n"
        b"        .max_conn = <uint:32> 100;\n"
        b"        .timeout = <float:64,s> 2.5;\n"
        b"    };\n"
        b"};\n"
    )

@pytest.fixture
def array_1d():
    return b".values = [1, 2, 3];\n"

@pytest.fixture
def array_2d():
    return b".matrix = [1, 2, 3]/[4, 5, 6];\n"

@pytest.fixture
def array_typed():
    return b".ports = [<uint:16> 80, <uint:16> 443, <uint:16> 8080];\n"

@pytest.fixture
def null_value():
    return b".empty = ;\n"

@pytest.fixture
def null_typed():
    return b".placeholder = <uint:32> ;\n"

@pytest.fixture
def special_floats():
    return (
        b".not_a_number = nan;\n"
        b".pos_inf = inf;\n"
        b".neg_inf = ninf;\n"
    )

@pytest.fixture
def compound_unit_payload():
    return ".force = <float:64,k~g\u00b7m/s\u00b2> 9.81;\n".encode('utf-8')

@pytest.fixture
def iec_unit_payload():
    return b".ram = <uint:64,Gi~B> 8;\n"

@pytest.fixture
def multi_assignment():
    return (
        b'.name = "Alice";\n'
        b".age = <uint:8> 30;\n"
        b".score = <float:64> 98.5;\n"
        b".active = true;\n"
    )

@pytest.fixture
def all_si_units():
    return (
        b".temperature = <float:64,K> 300.0;\n"
        b".pressure = <float:64,k~Pa> 101.325;\n"
        b".energy = <float:64,k~J> 5400.0;\n"
        b".speed = <float:64,m/s> 12.5;\n"
    )

@pytest.fixture
def error_invalid_utf8():
    return b".key\xff = 1;\n"

@pytest.fixture
def error_value_out_of_range():
    return b".x = <uint:8> 256;\n"

@pytest.fixture
def error_unknown_unit():
    return b".x = <float:64,x-m> 1.0;\n"

@pytest.fixture
def deep_struct():
    return (
        b".a = {\n"
        b"    .b = {\n"
        b"        .c = {\n"
        b"            .d = {\n"
        b"                .e = 42;\n"
        b"            };\n"
        b"        };\n"
        b"    };\n"
        b"};\n"
    )

@pytest.fixture
def string_escapes():
    return b'.s = "tab:\\there\\nnewline";\n'

@pytest.fixture
def string_concat():
    return b'.s = "hello " "world";\n'

@pytest.fixture
def comment_payload():
    return (
        b"# top-level comment\n"
        b".foo = 42;  # inline comment\n"
        b"# another comment\n"
        b".bar = 7;\n"
    )

@pytest.fixture
def symbol_value():
    return b".status = ok;\n"

@pytest.fixture
def reference_value():
    return b'.host = "localhost";\n.alias = &.host;\n'
