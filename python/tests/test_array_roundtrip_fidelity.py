# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Janos Sonntag
"""A typed load/dump cycle must not quietly drop an array's unit or precision.

An array carries ONE annotation for all its elements, and dumps() derived it only
for datetimes and wide integers. Everything else fell through to a bare array, so

    <float:64,k~m> [1.5, 2.5]   ->   [1.5, 2.5]

turning 1.5 km into a dimensionless 1.5. Separately, each element was re-emitted
from its decoded scalar, i.e. through a C double, so a float_dec:128 element with
29 significant digits came back with 17. Quantity's own docstring promises that
"numeric precision, integer base, and physical units are all preserved across a
load/dump cycle".

An array that needs no annotation must still get none -- a plain [1, 2, 3] gaining
a redundant <uint:64> would be its own regression.
"""

import pytest

from conftest import needs_lib

import bovnar

DOC = (
    "#!bovnar 1.1\n"
    ".dist  = <float:64,k~m> [1.5, 2.5];\n"
    ".exact = <float_dec:128> [1.2345678901234567890123456789];\n"
    ".big   = <uint:128> [340282366920938463463374607431768211455];\n"
    ".small = <uint:8> [1, 2, 3];\n"
    ".hole  = <float:64,m> [1.0, , 3.0];\n"
    ".plain = [1, 2, 3];\n"
    ".mixed = [1, 2.5, 3];\n"
)


@needs_lib
def test_array_unit_survives_a_typed_roundtrip():
    back = bovnar.loads(bovnar.dumps(bovnar.loads(DOC, typed=True)), typed=True)
    assert back["dist"][0].unit_str == "k~m"
    assert back["dist"][0].value == 1.5
    assert back["hole"][0].unit_str == "m"


@needs_lib
def test_array_element_precision_survives():
    back = bovnar.loads(bovnar.dumps(bovnar.loads(DOC, typed=True)), typed=True)
    assert str(back["exact"][0].decimal) == "1.2345678901234567890123456789"
    assert back["big"][0].value == 340282366920938463463374607431768211455


@needs_lib
def test_declared_width_survives():
    back = bovnar.loads(bovnar.dumps(bovnar.loads(DOC, typed=True)), typed=True)
    assert int(back["small"][0].vtype.width) == 8
    assert int(back["big"][0].vtype.width) == 128


@needs_lib
def test_null_hole_survives():
    back = bovnar.loads(bovnar.dumps(bovnar.loads(DOC, typed=True)), typed=True)
    assert [None if q is None else q.value for q in back["hole"]] == [1.0, None, 3.0]


@needs_lib
def test_arrays_needing_no_annotation_get_none():
    """A redundant annotation would be its own regression."""
    out = bovnar.dumps(bovnar.loads(DOC, typed=True)).decode()
    for line in out.splitlines():
        if line.startswith(".plain") or line.startswith(".mixed"):
            assert "<" not in line, line


@needs_lib
@pytest.mark.parametrize("shape", [(1, 4), (4, 1), (1, 1), (2, 3),
                                   (1, 2, 3), (2, 1, 3), (1, 5, 2), (4,)])
def test_numpy_shape_survives_a_roundtrip(shape):
    """A LEADING length-1 axis was dropped: (1,4) came back as (4,).

    write_array maps the outer list to /-separated rows while the reader maps
    NESTING to dimensions, and with exactly one row the two disagreed. The
    format can express it -- ".x = [[0, 1, 2, 3]];" reads back as (1,4) -- so
    this was a writer collapse, not a format limit.
    """
    np = pytest.importorskip("numpy")
    from bovnar._numpy import to_numpy, array_to_bvnr

    arr = np.arange(int(np.prod(shape)), dtype=np.int32).reshape(shape)
    out = array_to_bvnr("x", arr)
    src = out if isinstance(out, (bytes, bytearray)) else out.encode()
    if not src.startswith(b"#!"):
        src = b"#!bovnar 1.1\n" + src
    back = to_numpy(bovnar.loads(src, typed=True)["x"])
    assert back.shape == shape
    assert (back == arr).all()


@needs_lib
def test_datetime_fraction_is_part_of_equality():
    """Two instants 500 ms apart used to compare equal and collapse in a set."""
    d = bovnar.loads(
        "#!bovnar 1.1\n"
        ".t1 = <datetime:64> 2024-01-01T00:00:00.250Z;\n"
        ".t2 = <datetime:64> 2024-01-01T00:00:00.750Z;\n"
        ".t3 = <datetime:64> 2024-01-01T00:00:00.250Z;\n", typed=True)
    assert d["t1"] != d["t2"]
    assert d["t1"] == d["t3"]
    assert len({d["t1"], d["t2"], d["t3"]}) == 2


@needs_lib
def test_wide_int_beside_a_null_is_annotated():
    """A null constrains nothing, but it switched the width annotation off, and
    the array was then written as a bare 71-bit number nothing reads."""
    out = bovnar.dumps({'k': [2 ** 70, None]}, pretty=False)
    assert out == b'.k=<uint:71>[1180591620717411303424,null];'
    assert bovnar.loads(out) == {'k': [2 ** 70, None]}


@needs_lib
@pytest.mark.parametrize('value,annotation', [
    ([-2 ** 63, 1], b''),                       # an int64 array, as it is
    ([-2 ** 64], b'<sint:65>'),
    ([-2 ** 64 - 1], b'<sint:66>'),
    ([-2 ** 255, 2 ** 254], b'<sint:256>'),
])
def test_negative_power_of_two_needs_no_extra_bit(value, annotation):
    """A sint:N holds -2**(N-1). The width was the magnitude's bits plus one,
    so an int64 array holding its minimum went out as <sint:65>."""
    out = bovnar.dumps({'k': value}, pretty=False)
    body = b','.join(str(v).encode() for v in value)
    assert out == b'.k=' + annotation + b'[' + body + b'];'
    assert bovnar.loads(out) == {'k': value}


@needs_lib
@pytest.mark.parametrize('value,width', [
    (-2 ** 64, 65), (-2 ** 64 - 1, 66), (-2 ** 255, 256)])
def test_negative_power_of_two_scalar_width(value, width):
    out = bovnar.dumps({'k': value}, pretty=False)
    assert out == b'.k=<sint:%d>%d;' % (width, value)
    assert bovnar.loads(out) == {'k': value}


@needs_lib
@pytest.mark.parametrize('other', [1.5, True, 'x'])
def test_wide_int_in_an_array_with_no_annotation_is_refused(other):
    """An array has one annotation; beside a float, a bool or a string there is
    none to give it, and dumps() used to write a document its own loads()
    refused with value_out_of_range."""
    with pytest.raises(bovnar.BovnarArgumentError):
        bovnar.dumps({'k': [2 ** 70, other]})


@needs_lib
@pytest.mark.parametrize('src', [
    b'.a=[<uint:8>1,<float:64>2.5];',
    b'.a=[<uint:8>1,<uint:8>2]/[<uint:16>3,<uint:16>4];',
    b'.a=[<sint:16>5,null,<float:32>1.5];',
])
def test_mixed_type_unitless_array_keeps_its_element_types(src):
    """With a unit in common the elements were written under their own
    annotations; without one they were written bare and every width was lost."""
    doc = bovnar.loads(src, typed=True)
    again = bovnar.loads(bovnar.dumps(doc), typed=True)
    assert again == doc


@needs_lib
def test_plain_arrays_gain_no_element_annotations():
    doc = bovnar.loads(b'.a=[1,2.5,-3];', typed=True)
    assert bovnar.dumps(doc, pretty=False) == b'.a=[1,2.5,-3];'


@needs_lib
class TestBytesAreOctetStreams:
    """loads() hands an octet stream back as `bytes`, and dumps() had no way to
    write one -- so a document holding one could be read and not written."""

    def test_top_level_struct_and_array(self):
        doc = {'p': b'ab\x00cd', 's': {'q': b''}, 'a': [b'x', b'yz'],
               'n': 1}
        out = bovnar.dumps(doc)
        assert bovnar.loads(out) == doc
        assert bovnar.loads(out, typed=True)['p'] == b'ab\x00cd'

    def test_bytearray_and_memoryview(self):
        out = bovnar.dumps({'p': bytearray(b'ab'), 'q': memoryview(b'cd')})
        assert bovnar.loads(out) == {'p': b'ab', 'q': b'cd'}

    def test_multi_chunk_payload(self):
        payload = bytes(range(256)) * 800                  # 204800 bytes
        assert bovnar.loads(bovnar.dumps({'p': payload}))['p'] == payload

    def test_loads_dumps_of_an_embedded_document(self):
        outer = bovnar.stream.embed_document(bovnar.dumps({'v': 1}))
        assert bovnar.dumps(bovnar.loads(outer), pretty=False) == \
            bovnar.stream.embed_document(bovnar.dumps({'v': 1}), pretty=False)

    def test_multi_chunk_array_element(self):
        # The C writer used to put the element separator between the chunks of
        # one stream when it was an array element, which the reader reports as
        # octet_stream_out_of_sync at the second chunk header.
        payload = bytes(range(256)) * 300                  # 76800 bytes
        doc = {'a': [payload, b'tail']}
        assert bovnar.loads(bovnar.dumps(doc)) == doc


@needs_lib
def test_non_str_key_is_an_argument_error():
    for doc in ({1: 2}, {'a': {1: 2}}, {'a': [{None: 1}]}):
        with pytest.raises(bovnar.BovnarArgumentError):
            bovnar.dumps(doc)


@needs_lib
def test_typed_loads_does_not_pin_the_reader_snapshot():
    """Every Quantity kept a view into the 1136-byte bvnr_data_t snapshot its
    callback was handed, plus a 516-byte unit of its own: ~1.8 kB per scalar.
    Now the type is a copy and one Unit is shared per distinct unit."""
    d = bovnar.loads(b'.a=<float:64,m>[1.5,2.5]; .b=<float:64,m>3.5; .c=5 s;'
                     b' .d=sym;', typed=True)
    a0, a1 = d['a']
    assert a0.unit is a1.unit is d['b'].unit
    assert d['c'].unit is not a0.unit and str(d['c'].unit) == 's'
    for q in (a0, a1, d['b'], d['c'], d['d']):
        assert q.vtype._b_base_ is None
        assert q.unit.raw._b_base_ is None
    assert bovnar.dumps(d, pretty=False) == \
        b'.a=<float:64,m>[1.5,2.5];.b=<float:64,m>3.5;.c=<uint:64,s>5;.d=sym;'


@needs_lib
def test_float_elements_are_written_in_the_canonical_form():
    """An array's floats were written with repr() -- [1.5, -0.5] -- and a
    scalar float, and every float the Rust binding writes, in the C writer's
    canonical form: one value, two documents."""
    import math
    out = bovnar.dumps({'a': [1.0, 2.5, -0.5, 0.0, -0.0, math.inf, -math.inf,
                              math.nan, 5e-324, 1e300],
                        'b': 2.5}, pretty=False)
    assert out == (b'.a=[1e+0,2.5e+0,-5e-1,0.0,-0.0,inf,ninf,nan,5e-324,'
                   b'1e+300];.b=<float:64>2.5e+0;')


@needs_lib
def test_float_element_text_is_the_c_writers():
    """The element text is built from repr() where that is exact (a normal
    double, whose shortest nearest digits C picks too) and asked of C
    otherwise; either way it is bvn_format_double's text."""
    import ctypes
    import math
    import random
    import struct
    from bovnar._ffi import get_library
    from bovnar import _canonical_float_text, _FLOAT64_VT
    lib = get_library()
    buf = ctypes.create_string_buffer(64)
    rng = random.Random(20261109)
    values = [0.0, -0.0, math.inf, -math.inf, math.nan, 5e-324,
              2.2250738585072014e-308, 1.7976931348623157e308,
              -2.6546153960359173e+65, 0.1, 1e22, 1e23]
    values += [struct.unpack('<d', struct.pack('<Q', rng.getrandbits(64)))[0]
               for _ in range(20000)]
    values += [rng.uniform(-1e6, 1e6) for _ in range(5000)]
    for x in values:
        assert lib.bvn_format_double(buf, 64, x, _FLOAT64_VT) > 0
        assert _canonical_float_text(x) == buf.value, repr(x)
