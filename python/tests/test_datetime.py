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
