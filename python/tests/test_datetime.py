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
@pytest.mark.parametrize("doc,code", [
    ('#!bovnar 1.1\n.t = 2026-13-01;',                ErrorCode.INVALID_DATETIME_LITERAL),
    ('#!bovnar 1.1\n.t = 2026-06-31;',                ErrorCode.INVALID_DATETIME_LITERAL),
    ('#!bovnar 1.1\n.t = 2025-02-29;',                ErrorCode.INVALID_DATETIME_LITERAL),
    ('#!bovnar 1.1\n.t = 2026-006-15;',               ErrorCode.INVALID_DATETIME_LITERAL),
    ('#!bovnar 1.1\n.t = <datetime:64,gps> 2026-01-01;',
                                                      ErrorCode.DATETIME_LITERAL_UNSUPPORTED_EPOCH),
    ('#!bovnar 1.1\n.t = <sint:32> 2026-06-15;',      ErrorCode.TYPE_VALUE_MISMATCH),
    ('.t = 2026-06-15;',                              ErrorCode.ILLEGAL_VALUE_TYPE),  # 1.0 gate
])
def test_invalid_iso_literals(doc, code):
    with pytest.raises(BovnarParseError) as ei:
        bovnar.loads(doc)
    assert ei.value.code == code
