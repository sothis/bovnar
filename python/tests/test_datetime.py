# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Janos Sonntag
"""Spec 1.1 native time family (<datetime:width,epoch>) via the bindings."""

import pytest

from conftest import needs_lib

import bovnar
from bovnar import Reader, Event, ValueTypeFamily, ErrorCode, BovnarParseError


@needs_lib
def test_loads_decodes_epoch_seconds():
    assert bovnar.loads('#!bovnar 1.1\n.t = <datetime:64,unix> 1750000000;') \
        == {"t": 1750000000}
    assert bovnar.loads('#!bovnar 1.1\n.t = <datetime> -100;') == {"t": -100}


@needs_lib
def test_family_and_epoch_on_event():
    seen = []

    def on_event(ev, d):
        if ev == Event.DATA:
            seen.append((d.value_type.family, d.value_type.base))
        return True

    Reader().read_mem(b'#!bovnar 1.1\n.t = <datetime:64,gps> 99;',
                      on_verified=on_event)
    assert seen
    family, epoch_index = seen[0]
    assert family == ValueTypeFamily.DATETIME
    assert epoch_index == 2          # gps is index 2 in the epoch table


@needs_lib
def test_typed_epoch_recoverable_and_roundtrips():
    doc = bovnar.loads('#!bovnar 1.1\n.t = <datetime:64,gps> 1750000000;',
                       typed=True)
    q = doc['t']
    assert q.value == 1750000000
    assert q.epoch_name == 'gps'
    assert q.epoch_mjd == 44244
    # dumps emits the annotation + epoch AND the #!bovnar 1.1 directive, so it
    # round-trips losslessly
    out = bovnar.dumps(doc)
    assert out.startswith(b'#!bovnar 1.1\n')
    assert b'<datetime:64,gps>' in out
    doc2 = bovnar.loads(out, typed=True)
    assert doc2['t'].value == 1750000000
    assert doc2['t'].epoch_name == 'gps'


@needs_lib
def test_plain_dumps_has_no_version_directive():
    # documents that use no 1.1 feature must not gain a directive
    assert not bovnar.dumps({'a': 1}).startswith(b'#!bovnar')


@needs_lib
def test_unix_epoch_datetime_roundtrips():
    # the default (unix) epoch must still emit a <datetime> annotation, else the
    # value reloads as a plain int and loses its family
    doc = bovnar.loads('#!bovnar 1.1\n.t = <datetime:64,unix> 100;', typed=True)
    out = bovnar.dumps(doc)
    assert b'datetime' in out and out.startswith(b'#!bovnar 1.1\n')
    doc2 = bovnar.loads(out, typed=True)
    assert int(doc2['t'].vtype.family) == int(bovnar.ValueTypeFamily.DATETIME)
    assert doc2['t'].value == 100


@needs_lib
def test_typed_array_dumps_does_not_crash():
    # loads(typed=True) yields Quantity elements; dumps must handle them
    assert bovnar.loads(bovnar.dumps(bovnar.loads('.a = [1, 2, 3];',
                                                  typed=True))) == {'a': [1, 2, 3]}


@needs_lib
def test_datetime_array_roundtrips_family():
    # a datetime ARRAY must keep its family through dumps, like the scalar path —
    # otherwise it silently reloads as plain uint (regression: the array-level
    # annotation was not chosen for datetime Quantity leaves)
    doc = bovnar.loads('#!bovnar 1.1\n.t = [2026-01-01, 2026-01-02];', typed=True)
    out = bovnar.dumps(doc)
    assert b'datetime' in out
    fams = [int(q.vtype.family) for q in bovnar.loads(out, typed=True)['t']]
    assert fams == [int(bovnar.ValueTypeFamily.DATETIME)] * 2


@needs_lib
def test_datetime_array_preserves_non_unix_epoch():
    doc = bovnar.loads('#!bovnar 1.1\n.t = [<datetime:64,tai> 100, 200];', typed=True)
    out = bovnar.dumps(doc)
    assert b'tai' in out
    assert all(q.epoch_name == 'tai' for q in bovnar.loads(out, typed=True)['t'])


@needs_lib
def test_datetime_array_mixed_epochs_rejected():
    # no single array-level annotation can express two epochs; reject rather than
    # silently dropping one
    doc = bovnar.loads(
        '#!bovnar 1.1\n.t = [<datetime:64,tai> 1];', typed=True)
    doc['t'].append(bovnar.loads(
        '#!bovnar 1.1\n.u = <datetime:64,unix> 2;', typed=True)['u'])
    with pytest.raises(bovnar.BovnarArgumentError):
        bovnar.dumps(doc)


@needs_lib
def test_gated_on_version():
    with pytest.raises(BovnarParseError) as ei:
        bovnar.loads('.t = <datetime> 1;')
    assert ei.value.code == ErrorCode.ILLEGAL_VALUE_TYPE


@needs_lib
@pytest.mark.parametrize("doc,code", [
    ('#!bovnar 1.1\n.t = <datetime> 1.5;',    ErrorCode.TYPE_VALUE_MISMATCH),
    ('#!bovnar 1.1\n.t = <datetime> 1e3;',    ErrorCode.TYPE_VALUE_MISMATCH),
    ('#!bovnar 1.1\n.t = <datetime:xyz> 1;',  ErrorCode.ILLEGAL_VALUE_TYPE),
    ('#!bovnar 1.1\n.t = <datetime:_16> 1;',  ErrorCode.ILLEGAL_VALUE_TYPE),
    ('#!bovnar 1.1\n.t = <datetime:8> 300;',  ErrorCode.VALUE_OUT_OF_RANGE),
])
def test_invalid_datetimes(doc, code):
    with pytest.raises(BovnarParseError) as ei:
        bovnar.loads(doc)
    assert ei.value.code == code


# ── ISO-8601 datetime literals (spec 1.1) ───────────────────────────────────
@needs_lib
@pytest.mark.parametrize("doc,expected", [
    ('#!bovnar 1.1\n.t = 2026-06-15;',                          1781481600),
    ('#!bovnar 1.1\n.t = 2026-06-15T12:00:00Z;',                1781524800),
    ('#!bovnar 1.1\n.t = 2026-06-15T12:00:00;',                 1781524800),
    ('#!bovnar 1.1\n.t = 1970-01-01T00:00:00Z;',                0),
    ('#!bovnar 1.1\n.t = 1960-01-01;',                          -315619200),
    ('#!bovnar 1.1\n.t = <datetime> 2026-06-15;',               1781481600),
    # tai is leap-second correct: 2017-01-01 carries +37 leap seconds
    ('#!bovnar 1.1\n.t = <datetime:64,tai> 2017-01-01T00:00:00Z;', 1861920037),
    # fractional seconds floor to the whole-second carrier (the digits are
    # preserved separately; see test_iso_fraction_is_visible_and_roundtrips)
    ('#!bovnar 1.1\n.t = 2026-06-15T12:00:00.999Z;',            1781524800),
    ('#!bovnar 1.1\n.t = 2026-06-15T12:00:00.000000123;',       1781524800),
    # ±HH:MM offset folds to true UTC
    ('#!bovnar 1.1\n.t = 2026-06-15T12:00:00+02:00;',           1781517600),
    ('#!bovnar 1.1\n.t = 2026-06-15T12:00:00-05:00;',           1781542800),
    ('#!bovnar 1.1\n.t = 2026-06-15T01:00:00+02:00;',           1781478000),
    ('#!bovnar 1.1\n.t = 2026-06-15T12:00:00.5+02:00;',         1781517600),
    # tai offset is folded before the leap lookup -> stays leap-correct
    ('#!bovnar 1.1\n.t = <datetime:64,tai> 2017-01-01T02:00:00+02:00;', 1861920037),
])
def test_iso_literal_values(doc, expected):
    # loads goes through the C reader; an ISO literal decodes to its epoch
    # seconds carrier exactly like the equivalent integer would.
    assert bovnar.loads(doc)['t'] == expected


@needs_lib
def test_iso_literal_array_is_homogeneous():
    assert bovnar.loads('#!bovnar 1.1\n.ts = [2026-01-01, 2026-01-02];')['ts'] == \
        [1767225600, 1767312000]


@needs_lib
def test_iso_fraction_is_visible_and_roundtrips():
    # spec 1.1: the carrier stays whole seconds, but a DOM consumer can read the
    # verbatim sub-second digits as a string.
    from bovnar.dom import DomDoc
    doc = DomDoc.parse(b'#!bovnar 1.1\n.t = 2026-06-15T12:00:00.000000123Z;\n')
    node = doc.lookup('t')
    assert node.to_python() == 1781524800           # carrier = whole second
    assert node.datetime_fraction == '000000123'    # fraction preserved verbatim

    # a datetime given as an integer carrier (or with no fraction) exposes none
    plain = DomDoc.parse(b'#!bovnar 1.1\n.t = <datetime:64> 42;\n')
    assert plain.lookup('t').datetime_fraction is None
    none2 = DomDoc.parse(b'#!bovnar 1.1\n.t = 2026-06-15T12:00:00Z;\n')
    assert none2.lookup('t').datetime_fraction is None


@needs_lib
@pytest.mark.parametrize("src", [
    '#!bovnar 1.1\n.t = <datetime:64> 2026-06-15T12:00:00.123Z;\n',  # explicit ann + frac
    '#!bovnar 1.1\n.t = 2026-06-15T12:00:00.123Z;\n',                # bare literal + frac
    '#!bovnar 1.1\n.t = 1969-12-31T23:59:59.5Z;\n',                  # pre-epoch (floored) + frac
    '#!bovnar 1.1\n.t = <datetime:64,tai> 2026-05-28T00:00:00.25Z;\n',  # tai + frac
    '#!bovnar 1.1\n.t = 2026-06-15T12:00:00Z;\n',                    # bare literal, no frac
    '#!bovnar 1.1\n.t = [2026-06-15T12:00:00.5Z, 2026-06-16T00:00:00.25Z];\n',  # frac array
    '#!bovnar 1.1\n.t = <datetime:64,tai> [2026-05-28T00:00:00.5Z, 2026-05-29T00:00:00.25Z];\n',  # tai frac array
])
def test_typed_datetime_roundtrips_losslessly(src):
    # Regression: the dict-based typed loads/dumps path once (a) dropped a
    # datetime's sub-second fraction (Quantity carried no fraction) and (b) emitted
    # an illegal "<datetime:…,no_unit>" annotation for a bare ISO literal (the
    # no-unit sentinel rendered as a unit). Both must now round-trip losslessly.
    d = bovnar.loads(src, typed=True)
    out = bovnar.dumps(d)
    assert bovnar.loads(out, typed=True) is not None        # re-parses cleanly
    assert bovnar.dumps(bovnar.loads(out, typed=True)) == out   # stable / idempotent


@needs_lib
def test_typed_datetime_fraction_survives_dumps():
    q = bovnar.loads('#!bovnar 1.1\n.t = 2026-06-15T12:00:00.123Z;\n',
                     typed=True)['t']
    assert q.datetime_fraction == '123'                      # captured on the Quantity
    assert b'2026-06-15T12:00:00.123Z' in bovnar.dumps({'t': q})  # re-emitted verbatim


@needs_lib
def test_typed_datetime_array_fraction_survives_dumps():
    # An array element's fraction must survive too (a separate emit path from the
    # scalar one): re-emit must contain the ISO literals, not the bare carriers.
    src = '#!bovnar 1.1\n.t = [2026-06-15T12:00:00.5Z, 2026-06-16T00:00:00.25Z];\n'
    out = bovnar.dumps(bovnar.loads(src, typed=True)).decode()
    assert '2026-06-15T12:00:00.5Z' in out and '2026-06-16T00:00:00.25Z' in out
    assert '1781524800' not in out                           # not the dropped-fraction carrier
    # the captured fractions are visible on the element Quantities
    fracs = [q.datetime_fraction for q in bovnar.loads(src, typed=True)['t']]
    assert fracs == ['5', '25']


@needs_lib
def test_datetime_write_path_struct_layout():
    # Writing a datetime drives Writer._write_scalar, which passes &BvnrData to
    # the C bvnr_write_event; the C serializer reads bvnr_data_t.frac_data. If the
    # ctypes BvnrData mirror is short (missing the spec-1.1 frac_* fields) C reads
    # out of bounds and crashes. Load a typed datetime and write it back to
    # exercise that path end to end (under the ASAN build this catches the OOB).
    for src in ('#!bovnar 1.1\n.t = <datetime:64,tai> 1750000000;\n',
                '#!bovnar 1.1\n.t = <datetime:64,unix> 1781524800;\n',
                '#!bovnar 1.1\n.t = <datetime:64> -100;\n'):
        out = bovnar.dumps(bovnar.loads(src, typed=True))
        assert b'datetime' in out
    # round-trip preserves the carrier value
    rt = bovnar.loads(bovnar.dumps(
        bovnar.loads('#!bovnar 1.1\n.t = <datetime:64> 1781524800;\n', typed=True)))
    assert rt['t'] == 1781524800


@needs_lib
@pytest.mark.parametrize("doc,code", [
    ('#!bovnar 1.1\n.t = 2026-13-01;',                ErrorCode.INVALID_DATETIME_LITERAL),
    ('#!bovnar 1.1\n.t = 2026-06-31;',                ErrorCode.INVALID_DATETIME_LITERAL),
    ('#!bovnar 1.1\n.t = 2025-02-29;',                ErrorCode.INVALID_DATETIME_LITERAL),
    ('#!bovnar 1.1\n.t = 2026-006-15;',               ErrorCode.INVALID_DATETIME_LITERAL),
    ('#!bovnar 1.1\n.t = <datetime:64,gps> 2026-01-01;',
                                                      ErrorCode.DATETIME_LITERAL_UNSUPPORTED_EPOCH),
    ('#!bovnar 1.1\n.t = <sint:32> 2026-06-15;',      ErrorCode.TYPE_VALUE_MISMATCH),
    ('.t = 2026-06-15;',                              ErrorCode.ILLEGAL_VALUE_TYPE),  # 1.0 gate
    # fractional/offset edge cases
    ('#!bovnar 1.1\n.t = 2026-06-15T12:00:00.Z;',     ErrorCode.INVALID_DATETIME_LITERAL),  # '.' no digit
    ('#!bovnar 1.1\n.t = 2026-06-15T12:00:00+2:00;',  ErrorCode.INVALID_DATETIME_LITERAL),  # 1-digit hour
    ('#!bovnar 1.1\n.t = 2026-06-15T12:00:00+0200;',  ErrorCode.UNEXPECTED_INPUT_BYTE),     # no colon
    ('#!bovnar 1.1\n.t = 2026-06-15+02:00;',          ErrorCode.UNEXPECTED_INPUT_BYTE),     # offset on date-only
    ('#!bovnar 1.1\n.t = 2026-06-15T12:00:00Z+02:00;', ErrorCode.UNEXPECTED_INPUT_BYTE),    # Z and offset
])
def test_invalid_iso_literals(doc, code):
    with pytest.raises(BovnarParseError) as ei:
        bovnar.loads(doc)
    assert ei.value.code == code
