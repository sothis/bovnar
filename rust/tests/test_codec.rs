// SPDX-License-Identifier: MIT
//! The pure-Rust framing codec (`bovnar::codec`) against the C reference:
//! what `MuxWriter` writes is byte for byte what `bvnr_mux_send` writes
//! (`stream::mux_dump`), the C demultiplexer (`stream::mux_load`) reads it,
//! and `MuxReader` reads what C writes, fed in pieces of every size, with the
//! C demultiplexer's verdict on every malformed or cut-short stream. The
//! frame codec is held to `bvnr_frame_write` and `bvnr_doc_stream_read` the
//! same way.

use bovnar::codec::{
    self, FRAME_HEADER_SIZE, FrameReader, Item, MAX_CHANNELS, MuxReader, MuxWriter,
};
use bovnar::stream::{self, BVNR_MUX_MAX_MESSAGE};
use bovnar::{Error, ErrorCode, Value};

type Msgs = Vec<(u64, Vec<u8>)>;

/// A deterministic byte pattern, so a misplaced byte cannot go unnoticed.
fn payload(len: usize, seed: u64) -> Vec<u8> {
    let mut x = seed.wrapping_mul(0x9E37_79B9_7F4A_7C15) | 1;
    (0..len)
        .map(|_| {
            x ^= x << 13;
            x ^= x >> 7;
            x ^= x << 17;
            x as u8
        })
        .collect()
}

/// Piece sizes from a seed, 1 to `max` bytes each.
fn pieces(data: &[u8], seed: u64, max: usize) -> Vec<&[u8]> {
    let mut x = seed | 1;
    let mut out = Vec::new();
    let mut rest = data;
    while !rest.is_empty() {
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        let n = (x as usize % max + 1).min(rest.len());
        out.push(&rest[..n]);
        rest = &rest[n..];
    }
    out
}

fn ours(msgs: &[(u64, Vec<u8>)], key: &str) -> Vec<u8> {
    let mux = MuxWriter::new(key).unwrap();
    let mut out = Vec::new();
    mux.begin(&mut out);
    for (c, p) in msgs {
        mux.write_message(&mut out, *c, p);
    }
    mux.finish(&mut out);
    out
}

/// Feeds `parts` in order to `reader` and collects every message, then
/// checks the end.
fn read_parts(reader: &mut MuxReader, parts: &[&[u8]]) -> bovnar::Result<Msgs> {
    let mut got = Vec::new();
    for part in parts {
        let mut input = *part;
        while let Some(m) = reader.read(&mut input)? {
            got.push((m.channel, m.payload.to_vec()));
        }
        assert!(input.is_empty(), "read() returned None with input left");
    }
    reader.finish()?;
    Ok(got)
}

fn read_all(data: &[u8]) -> bovnar::Result<Msgs> {
    read_parts(&mut MuxReader::new(), &[data])
}

fn kind(r: &bovnar::Result<Msgs>) -> Option<ErrorCode> {
    r.as_ref()
        .err()
        .map(|e| e.kind().expect("an error with a code"))
}

/// The C demultiplexer's verdict.
fn c_load(data: &[u8]) -> bovnar::Result<Msgs> {
    stream::mux_load(data, 0, None)
}

/// A document under `mux` holding `chunks` as octet chunks, as given.
fn raw_doc(chunks: &[Vec<u8>]) -> Vec<u8> {
    let mut d = b".mux=\0".to_vec();
    for c in chunks {
        assert!(!c.is_empty() && c.len() <= 65536);
        d.push(1);
        d.extend_from_slice(&((c.len() & 0xFFFF) as u16).to_le_bytes());
        d.extend_from_slice(c);
    }
    d.extend_from_slice(b"\0;");
    d
}

fn varint(mut v: u64) -> Vec<u8> {
    let mut out = Vec::new();
    loop {
        let b = (v & 0x7F) as u8;
        v >>= 7;
        if v == 0 {
            out.push(b);
            return out;
        }
        out.push(b | 0x80);
    }
}

/// A chunk on `channel` holding `body` (any logical-stream bytes).
fn chunk(channel: u64, body: &[u8]) -> Vec<u8> {
    let mut c = varint(channel);
    c.extend_from_slice(body);
    c
}

/// A message on the logical stream: its length varint and its bytes.
fn framed(msg: &[u8]) -> Vec<u8> {
    let mut m = varint(msg.len() as u64);
    m.extend_from_slice(msg);
    m
}

/// Message sets that cover the chunking: empty messages, one-byte and
/// ten-byte channel varints, and lengths on either side of every chunk
/// boundary `bvnr_mux_send` has.
fn corpus() -> Vec<Msgs> {
    let first = 65536 - 1 - 3; // channel 1, a three-byte length varint
    let max_ch = 65536 - 10 - 3; // channel u64::MAX
    let mut sets = vec![
        vec![],
        vec![(0, vec![])],
        vec![(1, b"hello".to_vec()), (42, b"world".to_vec()), (1, vec![])],
        vec![
            (127, payload(127, 1)),
            (128, payload(128, 2)),
            (16383, payload(16383, 3)),
        ],
        vec![
            (16384, payload(16384, 4)),
            (u64::MAX, payload(300, 5)),
            (1 << 63, vec![]),
        ],
        vec![(7, payload(BVNR_MUX_MAX_MESSAGE as usize, 6))],
        vec![(u64::MAX, payload(BVNR_MUX_MAX_MESSAGE as usize, 7))],
        vec![
            (u64::MAX, payload(max_ch, 8)),
            (u64::MAX, payload(max_ch + 1, 9)),
        ],
    ];
    for len in [first - 1, first, first + 1, first + 65535, first + 65536] {
        sets.push(vec![(1, payload(len, len as u64)), (2, b"after".to_vec())]);
    }
    // The longest channel varint a later chunk's header is written for as one
    // word (five bytes), and the shortest it is not.
    sets.push(vec![
        ((1 << 35) - 1, payload(70_000, 13)),
        (1 << 35, payload(70_000, 14)),
    ]);
    sets.push(vec![
        (3, payload(400_000, 10)),
        (4, payload(150, 11)),
        (3, payload(30_000, 12)),
    ]);
    sets
}

// ---------------------------------------------------------------------------
// Writer
// ---------------------------------------------------------------------------

#[test]
fn writer_is_byte_identical_to_bvnr_mux_send() {
    for (i, msgs) in corpus().iter().enumerate() {
        for key in ["mux", "media", "größe", "a-b_c+d"] {
            let c = stream::mux_dump(msgs, key).unwrap();
            assert_eq!(ours(msgs, key), c, "set {i}, key {key}");

            // The vectored form and the io::Write form give the same bytes.
            let mux = MuxWriter::new(key).unwrap();
            let mut head = Vec::new();
            let mut parts = mux.opener().to_vec();
            let mut written = mux.opener().to_vec();
            for (ch, p) in msgs {
                let encoded = mux.encode(&mut head, *ch, p);
                let wire_len = encoded.wire_len();
                let before = parts.len();
                for part in encoded {
                    assert!(!part.is_empty());
                    parts.extend_from_slice(part);
                }
                assert_eq!(parts.len() - before, wire_len);
                mux.write_message_to(&mut written, *ch, p).unwrap();
            }
            parts.extend_from_slice(mux.terminator());
            written.extend_from_slice(mux.terminator());
            assert_eq!(parts, c, "set {i}, encode");
            assert_eq!(written, c, "set {i}, write_message_to");
        }
    }
}

#[test]
fn a_full_chunk_has_length_field_zero() {
    // Channel 1 and a three-byte length leave 65532 payload bytes in the
    // first chunk: exactly that many fill it to 65536.
    let out = ours(&[(1, payload(65532, 1))], "mux");
    assert_eq!(&out[5..9], &[0, 1, 0, 0], "opener 0x00, tag, length 0x0000");
    assert_eq!(out.len(), 6 + 3 + 65536 + 2);
    assert_eq!(c_load(&out).unwrap(), [(1, payload(65532, 1))]);
}

#[test]
fn c_reads_what_the_writer_writes() {
    for msgs in corpus() {
        assert_eq!(c_load(&ours(&msgs, "mux")).unwrap(), msgs);
        assert_eq!(
            stream::mux_load(&ours(&msgs, "mux"), 0, Some("mux")).unwrap(),
            msgs
        );
    }
}

#[test]
fn keys_are_accepted_as_the_c_writer_accepts_them() {
    let mut keys: Vec<String> = Vec::new();
    for b in 1u8..0x80 {
        let c = char::from(b);
        keys.push(format!("{c}"));
        keys.push(format!("a{c}"));
        keys.push(format!("{c}a"));
    }
    for k in [
        "é", "aé", "a\u{80}", "a\u{bf}", "\u{c0}", "a\u{c0}", "日本", "a😀", "_1", "-a",
    ] {
        keys.push(k.to_string());
    }
    keys.push("k".repeat(255));
    for key in &keys {
        let c = stream::mux_dump(&[(1, b"x")], key);
        let r = MuxWriter::new(key);
        assert_eq!(r.is_ok(), c.is_ok(), "key {key:?}: {r:?} vs {c:?}");
        if let Ok(c) = c {
            assert_eq!(read_all(&c).unwrap(), [(1, b"x".to_vec())], "key {key:?}");
        }
    }
    // The C writer takes up to 65535 bytes; default readers stop at 255.
    let long = "k".repeat(256);
    assert_eq!(
        MuxWriter::new(&long).unwrap_err().kind(),
        Some(ErrorCode::IdentifierTooLong)
    );
    assert!(stream::mux_load(&stream::mux_dump(&[(1, b"x")], &long).unwrap(), 0, None).is_err());
}

// ---------------------------------------------------------------------------
// Reader: what C writes, in pieces
// ---------------------------------------------------------------------------

#[test]
fn reader_reads_c_output_whole_bytewise_and_in_pieces() {
    for (i, msgs) in corpus().iter().enumerate() {
        let c = stream::mux_dump(msgs, "mux").unwrap();
        assert_eq!(read_all(&c).unwrap(), *msgs, "set {i} whole");
        for seed in 1..4 {
            let parts = pieces(&c, seed, 70_000);
            assert_eq!(
                read_parts(&mut MuxReader::new(), &parts).unwrap(),
                *msgs,
                "set {i}"
            );
            let parts = pieces(&c, seed, 300);
            assert_eq!(
                read_parts(&mut MuxReader::new(), &parts).unwrap(),
                *msgs,
                "set {i}"
            );
        }
        if c.len() < 200_000 {
            let bytes: Vec<&[u8]> = c.chunks(1).collect();
            assert_eq!(
                read_parts(&mut MuxReader::new(), &bytes).unwrap(),
                *msgs,
                "set {i} bytewise"
            );
        }
    }
}

#[test]
fn reader_handles_a_split_at_every_byte() {
    let msgs = vec![
        (1, b"hello".to_vec()),
        (300, vec![]),
        (u64::MAX, payload(200, 1)),
        (1, payload(70_000, 2)),
        (0, b"z".to_vec()),
    ];
    let c = stream::mux_dump(&msgs, "mux").unwrap();
    // Every split point near the structure, and every 97th byte in between.
    for at in 0..=c.len() {
        if at > 400 && at + 400 < c.len() && at % 97 != 0 && (at as isize - 65_960).abs() > 200 {
            continue;
        }
        let got = read_parts(&mut MuxReader::new(), &[&c[..at], &c[at..]]);
        assert_eq!(got.unwrap(), msgs, "split at {at}");
    }
}

#[test]
fn whole_messages_are_borrowed_from_the_input() {
    let c = stream::mux_dump(&[(1, b"hello".to_vec()), (2, payload(1000, 1))], "mux").unwrap();
    let range = c.as_ptr_range();
    let mut reader = MuxReader::new();
    let mut input = &c[..];
    let mut n = 0;
    while let Some(m) = reader.read(&mut input).unwrap() {
        assert!(
            range.contains(&m.payload.as_ptr()),
            "message {n} was copied"
        );
        n += 1;
    }
    assert_eq!(n, 2);
    assert!(reader.is_done());
}

#[test]
fn reader_accepts_packed_interleaved_and_split_varints() {
    let big = payload(200, 3);
    let docs = [
        // Three messages in one chunk, one of them empty.
        raw_doc(&[[chunk(1, &framed(b"ab")), framed(b"cde"), framed(b"")].concat()]),
        // Channel 1's message spans two chunks with channel 2 between them.
        raw_doc(&[
            chunk(1, &[&varint(5)[..], b"ab"].concat()),
            chunk(2, &framed(b"xy")),
            chunk(1, b"cde"),
        ]),
        // A length varint split across chunks, with another channel between.
        raw_doc(&[
            chunk(1, &[0xC8]),
            chunk(2, &framed(b"q")),
            chunk(1, &[&[0x01][..], &big].concat()),
        ]),
        // A message ending inside a chunk that then starts the next one,
        // whose length varint continues into the next chunk.
        raw_doc(&[
            chunk(9, &[&framed(b"one")[..], &[0x80]].concat()),
            chunk(9, &[&[0x01][..], &payload(128, 4)].concat()),
        ]),
        // Chunks carrying only a channel varint, and a non-minimal varint.
        raw_doc(&[
            chunk(4, b""),
            vec![0x84, 0x00, 3, b'a', b'b', b'c'],
            chunk(4, b""),
        ]),
    ];
    for (i, d) in docs.iter().enumerate() {
        let c = c_load(d).unwrap();
        assert!(!c.is_empty(), "doc {i}");
        assert_eq!(read_all(d).unwrap(), c, "doc {i}");
        let bytes: Vec<&[u8]> = d.chunks(1).collect();
        assert_eq!(
            read_parts(&mut MuxReader::new(), &bytes).unwrap(),
            c,
            "doc {i} bytewise"
        );
    }
    assert_eq!(
        read_all(&docs[1]).unwrap(),
        [(2, b"xy".to_vec()), (1, b"abcde".to_vec())]
    );
}

#[test]
fn malformed_streams_fail_as_the_c_demux_fails() {
    let overlong = [0x80u8; 11];
    let mut bad_tenth = [0xFFu8; 10];
    bad_tenth[9] = 0x02;
    let docs = [
        // A chunk tag other than 0x01.
        [&b".mux=\0\x02"[..], b"\0;"].concat(),
        // A channel varint the chunk ends inside.
        raw_doc(&[vec![0x80]]),
        raw_doc(&[vec![0x80, 0x80]]),
        // Overlong varints: channel, length.
        raw_doc(&[overlong.to_vec()]),
        raw_doc(&[bad_tenth.to_vec()]),
        raw_doc(&[chunk(1, &overlong)]),
        raw_doc(&[chunk(1, &bad_tenth)]),
        // Ten continuation bytes, then the stream closes: truncated.
        raw_doc(&[chunk(1, &[0x80; 10])]),
        // Ten continuation bytes, then one more in the next chunk.
        raw_doc(&[chunk(1, &[0x80; 10]), chunk(1, &[0x00])]),
        // A message cut short by the stream's end, and a length cut short.
        raw_doc(&[chunk(1, &[&varint(10)[..], b"abc"].concat())]),
        raw_doc(&[chunk(1, &[0x81])]),
        // A chunk whose length runs past the terminator: the input ends in it.
        [&b".mux=\0\x01\x0a\x00\x01\x02ab"[..], b"\0;"].concat(),
        // A declared length over the 64 MiB default.
        raw_doc(&[chunk(1, &varint(64 * 1024 * 1024 + 1))]),
    ];
    for (i, d) in docs.iter().enumerate() {
        let c = c_load(d);
        let r = read_all(d);
        assert!(c.is_err(), "doc {i}: C accepted it");
        assert_eq!(kind(&r), kind(&c), "doc {i}");
        // An error read() raised is latched.
        let mut reader = MuxReader::new();
        let mut input = &d[..];
        let failed = loop {
            match reader.read(&mut input) {
                Ok(Some(_)) => {}
                Ok(None) => break false,
                Err(_) => break true,
            }
        };
        if failed {
            let again = reader.read(&mut &b"\0;"[..]);
            assert_eq!(
                again.err().and_then(|e| e.kind()),
                kind(&c),
                "doc {i} latched"
            );
            assert_eq!(
                reader.finish().err().and_then(|e| e.kind()),
                kind(&c),
                "doc {i}"
            );
        }
    }
}

#[test]
fn every_truncation_fails_as_the_c_demux_fails() {
    let msgs = vec![(1, b"hello".to_vec()), (2, payload(70_000, 1)), (3, vec![])];
    let c = stream::mux_dump(&msgs, "mux").unwrap();
    for n in 0..c.len() {
        if n > 100 && n + 100 < c.len() && n % 1013 != 0 {
            continue;
        }
        let r = read_all(&c[..n]);
        let reference = c_load(&c[..n]);
        assert_eq!(kind(&r), kind(&reference), "cut at {n}");
        assert_eq!(r.is_ok(), n == 0, "cut at {n}");
    }
    // The messages before the cut have been delivered.
    let mut reader = MuxReader::new();
    let mut input = &c[..c.len() - 3];
    let first = reader.read(&mut input).unwrap().unwrap();
    assert_eq!(first.payload, b"hello");
    assert_eq!(
        reader.finish().unwrap_err().kind(),
        Some(ErrorCode::ReadCompleteChunkFailed)
    );
}

#[test]
fn what_follows_the_document() {
    let c = stream::mux_dump(&[(1, b"x".to_vec())], "mux").unwrap();
    for tail in [&b""[..], b"\n", b" \t\r\n"] {
        assert_eq!(
            read_all(&[&c[..], tail].concat()).unwrap(),
            [(1, b"x".to_vec())]
        );
    }
    for tail in [&b"x"[..], b";", b"\0", b".x = 1;"] {
        let r = read_all(&[&c[..], tail].concat());
        assert_eq!(kind(&r), Some(ErrorCode::UnexpectedInputByte), "{tail:?}");
    }
    // The C reader agrees but for a second statement, which it reads on.
    assert_eq!(
        kind(&c_load(&[&c[..], b"x"].concat())),
        Some(ErrorCode::UnexpectedInputByte)
    );
    // Whitespace (all six ws-chars) and comments before and after the `;`,
    // read as the C lexer reads them, split at every byte.
    let body = &c[..c.len() - 1];
    assert_eq!(c.last(), Some(&b';'));
    for (before, after) in [
        (&b" "[..], &b""[..]),
        (b"\x0b\x0c", b"\x0b\x0c"),
        (b" # before\n", b""),
        (b"#\r", b"# after"),
        (b"", b"\n# one\n# two\n"),
    ] {
        let doc = [body, before, b";", after].concat();
        let want = c_load(&doc).unwrap();
        assert_eq!(want, [(1, b"x".to_vec())], "{doc:?}");
        for cut in 0..=doc.len() {
            let mut reader = MuxReader::new();
            assert_eq!(
                read_parts(&mut reader, &[&doc[..cut], &doc[cut..]]).unwrap(),
                want
            );
        }
    }
    // A control byte in such a comment is refused, as C refuses it.
    let bad = [body, b";# \x01\n"].concat();
    assert_eq!(kind(&read_all(&bad)), Some(ErrorCode::UnexpectedInputByte));
    assert_eq!(kind(&read_all(&bad)), kind(&c_load(&bad)));
    // A comment is UTF-8 text: well-formed characters pass, a malformed
    // byte, a sequence a newline cuts short and one the input ends inside
    // are refused, each as C refuses it, wherever the input is cut.
    for (tail, ok) in [
        (&b"# caf\xc3\xa9 \xe2\x82\xac \xf0\x9f\x98\x80\n"[..], true),
        (b"# caf\xe9\n", false),
        (b"# \xc3\n", false),
        (b"# \xed\xa0\x80\n", false),
        (b"# \xc0\xaf\n", false),
        (b"#\xc3", false),
        (b"#\xf0\x9f\x98", false),
    ] {
        for at_semicolon in [false, true] {
            let doc = if at_semicolon {
                [body, tail, b"\n;"].concat()
            } else {
                [body, b";", tail].concat()
            };
            let want = c_load(&doc);
            assert_eq!(want.is_ok(), ok, "C on {doc:?}");
            for cut in 0..=doc.len() {
                let got = read_parts(&mut MuxReader::new(), &[&doc[..cut], &doc[cut..]]);
                assert_eq!(kind(&got), kind(&want), "{doc:?} cut at {cut}");
            }
        }
    }
    // The C writer's pretty form.
    let pretty = [&b".mux = \0"[..], &c[6..], b"\n"].concat();
    assert_eq!(read_all(&pretty).unwrap(), c_load(&pretty).unwrap());
    // An empty input is an empty stream; text that never opens one is not.
    assert_eq!(read_all(b"").unwrap(), []);
    assert_eq!(
        kind(&read_all(b".mux=")),
        Some(ErrorCode::GotIncompleteBvnrStream)
    );
    assert_eq!(
        kind(&read_all(b".a = 1;")),
        Some(ErrorCode::GotIncompleteBvnrStream)
    );
}

#[test]
fn the_4096_channel_limit() {
    let msgs = |n: u64| (0..n).map(|c| (c * 1000, vec![c as u8])).collect::<Msgs>();
    let ok = stream::mux_dump(&msgs(MAX_CHANNELS as u64), "mux").unwrap();
    assert_eq!(read_all(&ok).unwrap(), msgs(MAX_CHANNELS as u64));
    let over = stream::mux_dump(&msgs(MAX_CHANNELS as u64 + 1), "mux").unwrap();
    let r = read_all(&over);
    assert_eq!(kind(&r), Some(ErrorCode::OctetStreamOutOfSync));
    assert_eq!(kind(&r), kind(&c_load(&over)));
    // A channel seen again does not count twice.
    let mut again = msgs(MAX_CHANNELS as u64);
    again.extend(msgs(10));
    let again_doc = stream::mux_dump(&again, "mux").unwrap();
    assert_eq!(read_all(&again_doc).unwrap(), again);
}

#[test]
fn channels_that_differ_above_32_bits_stay_apart() {
    // On a 32-bit target, a table indexed by `id as usize` would read channel
    // 2^32 + 1 as channel 1.
    let high = (1u64 << 32) | 1;
    let packed = raw_doc(&[
        chunk(high, &framed(b"x")),
        chunk(1, &[framed(b"a"), framed(b"b")].concat()),
    ]);
    let want: Msgs = vec![
        (high, b"x".to_vec()),
        (1, b"a".to_vec()),
        (1, b"b".to_vec()),
    ];
    assert_eq!(c_load(&packed).unwrap(), want);
    assert_eq!(read_all(&packed).unwrap(), want);
    // Interleaved: channel 1's message is continued after the other's.
    let mut first = varint(5);
    first.extend_from_slice(b"ab");
    let split = raw_doc(&[
        chunk(1, &first),
        chunk(high, &framed(b"xy")),
        chunk(1, b"cde"),
    ]);
    let want: Msgs = vec![(high, b"xy".to_vec()), (1, b"abcde".to_vec())];
    assert_eq!(c_load(&split).unwrap(), want);
    assert_eq!(read_all(&split).unwrap(), want);
}

#[test]
fn max_message_is_enforced_as_in_c() {
    let doc = stream::mux_dump(&[(1, payload(10, 1)), (1, payload(11, 2))], "mux").unwrap();
    let mut reader = MuxReader::with_options(10, None).unwrap();
    let r = read_parts(&mut reader, &[&doc]);
    assert_eq!(kind(&r), Some(ErrorCode::ValueOutOfRange));
    assert_eq!(kind(&r), kind(&stream::mux_load(&doc, 10, None)));
    let mut reader = MuxReader::with_options(11, None).unwrap();
    assert_eq!(read_parts(&mut reader, &[&doc]).unwrap().len(), 2);
}

#[test]
fn a_key_filter_refuses_another_key() {
    let doc = stream::mux_dump(&[(1, b"x".to_vec())], "other").unwrap();
    let mut reader = MuxReader::with_options(0, Some("mux")).unwrap();
    let e = read_parts(&mut reader, &[&doc]).unwrap_err();
    assert!(matches!(e, Error::Argument { .. }), "{e:?}");
    assert!(e.to_string().contains("\"other\""), "{e}");
    let mut reader = MuxReader::with_options(0, Some("other")).unwrap();
    assert_eq!(
        read_parts(&mut reader, &[&doc]).unwrap(),
        [(1, b"x".to_vec())]
    );
    assert_eq!(reader.key(), Some("other"));
    assert!(MuxReader::with_options(0, Some("no key")).is_err());
}

#[test]
fn reset_reads_a_second_stream() {
    let a = stream::mux_dump(&[(1, payload(100_000, 1))], "mux").unwrap();
    let b = stream::mux_dump(&[(2, b"b".to_vec())], "other").unwrap();
    let mut reader = MuxReader::new();
    assert_eq!(
        read_parts(&mut reader, &pieces(&a, 5, 999)).unwrap().len(),
        1
    );
    reader.reset();
    assert_eq!(
        read_parts(&mut reader, &[&b]).unwrap(),
        [(2, b"b".to_vec())]
    );
}

// ---------------------------------------------------------------------------
// Peek, skip, forward
// ---------------------------------------------------------------------------

/// A peek's channel, length and head.
type Peeked = (u64, u64, Vec<u8>);

/// Runs a router over `parts`: peeks with `head` bytes, decides with
/// `decide`, and collects what comes out.
fn route(
    parts: &[&[u8]],
    head: usize,
    mut decide: impl FnMut(u64, u64, &[u8]) -> u8,
) -> (Vec<Peeked>, Msgs, Vec<u8>) {
    let mut reader = MuxReader::new();
    let mut peeks = Vec::new();
    let mut messages = Vec::new();
    let mut wire = Vec::new();
    for part in parts {
        let mut input = *part;
        while let Some(item) = reader.next_item(&mut input, head).unwrap() {
            match item {
                Item::Peek(p) => {
                    peeks.push((p.channel, p.len, p.head.to_vec()));
                    match decide(p.channel, p.len, p.head) {
                        0 => {}
                        1 => p.skip(),
                        _ => p.forward(),
                    }
                }
                Item::Message(m) => messages.push((m.channel, m.payload.to_vec())),
                Item::Wire(w) => {
                    assert_eq!(
                        c_load(&[&b".m=\0"[..], w.bytes, b"\0;"].concat()).unwrap(),
                        [(w.channel, payload_of(w.bytes))]
                    );
                    wire.extend_from_slice(w.bytes);
                }
            }
        }
    }
    reader.finish().unwrap();
    (peeks, messages, wire)
}

fn payload_of(wire: &[u8]) -> Vec<u8> {
    c_load(&[&b".m=\0"[..], wire, b"\0;"].concat())
        .unwrap()
        .remove(0)
        .1
}

#[test]
fn peek_reports_every_message_start_with_its_head() {
    let set = corpus();
    for (i, msgs) in set.iter().enumerate() {
        let c = stream::mux_dump(msgs, "mux").unwrap();
        for parts in [
            vec![&c[..]],
            pieces(&c, i as u64 + 1, 5000),
            c.chunks(7).collect(),
        ] {
            let (peeks, messages, wire) = route(&parts, 5, |_, _, _| 0);
            let expect: Vec<_> = msgs
                .iter()
                .map(|(ch, p)| (*ch, p.len() as u64, p[..p.len().min(5)].to_vec()))
                .collect();
            assert_eq!(peeks, expect, "set {i}");
            assert_eq!(messages, *msgs, "set {i}");
            assert!(wire.is_empty());
        }
    }
}

#[test]
fn skip_and_forward_route_by_channel() {
    let msgs = vec![
        (1, payload(150, 1)),
        (2, payload(30_000, 2)),
        (1, payload(400_000, 3)),
        (3, vec![]),
        (2, payload(65_532, 4)),
        (1, b"tail".to_vec()),
    ];
    let c = stream::mux_dump(&msgs, "mux").unwrap();
    let keep: Msgs = msgs.iter().filter(|(ch, _)| *ch != 2).cloned().collect();
    let keep_doc = stream::mux_dump(&keep, "mux").unwrap();
    let keep_wire = &keep_doc[6..keep_doc.len() - 2];
    for parts in [
        vec![&c[..]],
        pieces(&c, 1, 100_000),
        pieces(&c, 2, 1000),
        c.chunks(1).collect(),
    ] {
        // Forward channels 1 and 3, skip 2: the forwarded chunks are the
        // input's, byte for byte, however the input was cut.
        let (peeks, messages, wire) = route(&parts, 0, |ch, _, _| if ch == 2 { 1 } else { 2 });
        assert_eq!(peeks.len(), msgs.len());
        assert!(messages.is_empty());
        assert_eq!(wire, keep_wire);
        // Skip everything.
        let (_, messages, wire) = route(&parts, 3, |_, _, _| 1);
        assert!(messages.is_empty() && wire.is_empty());
        // Receive channel 2 only.
        let (_, messages, _) = route(&parts, 1, |ch, _, _| if ch == 2 { 0 } else { 1 });
        let only2: Msgs = msgs.iter().filter(|(ch, _)| *ch == 2).cloned().collect();
        assert_eq!(messages, only2);
    }
}

#[test]
fn forwarding_whole_input_is_zero_copy() {
    let msgs = vec![(1, payload(150, 1)), (5, payload(400_000, 2))];
    let c = stream::mux_dump(&msgs, "mux").unwrap();
    let range = c.as_ptr_range();
    let mut reader = MuxReader::new();
    let mut input = &c[..];
    let mut n = 0;
    while let Some(item) = reader.next_item(&mut input, 0).unwrap() {
        match item {
            Item::Peek(p) => p.forward(),
            Item::Wire(w) => {
                assert!(range.contains(&w.bytes.as_ptr()), "wire {n} was copied");
                assert_eq!(payload_of(w.bytes), msgs[n].1);
                n += 1;
            }
            Item::Message(_) => panic!("forwarded messages come out as wire"),
        }
    }
    assert_eq!(n, 2);
}

#[test]
fn forwarding_a_packed_stream_rewrites_it() {
    let d = raw_doc(&[
        [chunk(1, &framed(b"ab")), framed(b"cde")].concat(),
        chunk(2, &[&varint(5)[..], b"x"].concat()),
        chunk(1, &framed(b"")),
        chunk(2, b"yzzz"),
    ]);
    let expect = c_load(&d).unwrap();
    for parts in [vec![&d[..]], d.chunks(1).collect::<Vec<_>>()] {
        let (_, _, wire) = route(&parts, 2, |_, _, _| 2);
        assert_eq!(
            c_load(&[&b".m=\0"[..], &wire, b"\0;"].concat()).unwrap(),
            expect
        );
    }
}

#[test]
fn a_head_cut_by_the_input_is_gathered() {
    let msgs = vec![
        (1, payload(100, 1)),
        (2, payload(3, 2)),
        (3, payload(70_000, 3)),
    ];
    let c = stream::mux_dump(&msgs, "mux").unwrap();
    for at in 6..40 {
        let (peeks, messages, _) = route(&[&c[..at], &c[at..]], 8, |_, _, _| 0);
        assert_eq!(peeks[0].2, msgs[0].1[..8], "split at {at}");
        assert_eq!(peeks[1].2, msgs[1].1, "split at {at}");
        assert_eq!(messages, msgs, "split at {at}");
    }
    // A head longer than the producer put in the first chunk stops there.
    let d = raw_doc(&[
        chunk(1, &[&varint(10)[..], b"abc"].concat()),
        chunk(1, b"defghij"),
    ]);
    let (peeks, messages, _) = route(&[&d], 8, |_, _, _| 0);
    assert_eq!(peeks, [(1, 10, b"abc".to_vec())]);
    assert_eq!(messages, [(1, b"abcdefghij".to_vec())]);
}

#[test]
fn read_after_a_peek_delivers_the_message() {
    let msgs = vec![(1, payload(100_000, 1)), (2, b"two".to_vec())];
    let c = stream::mux_dump(&msgs, "mux").unwrap();
    let mut reader = MuxReader::new();
    let mut input = &c[..];
    let Some(Item::Peek(p)) = reader.next_item(&mut input, 4).unwrap() else {
        panic!("a peek first")
    };
    assert_eq!((p.channel, p.len), (1, 100_000));
    p.forward();
    // read() hands a forwarded message over as its payload.
    let m = reader.read(&mut input).unwrap().unwrap();
    assert_eq!((m.channel, m.payload), (1, &msgs[0].1[..]));
    let m = reader.read(&mut input).unwrap().unwrap();
    assert_eq!((m.channel, m.payload), (2, &b"two"[..]));
    assert!(reader.read(&mut input).unwrap().is_none());
    assert!(reader.is_done());
}

// ---------------------------------------------------------------------------
// Preamble
// ---------------------------------------------------------------------------

const META: &[u8] = b"#!bovnar 1.2\n.meta = { .codec = \"av1\"; .rate = <float:32,Hz> 30.0; };\n";

fn with_preamble(preamble: &[u8], key: &str, msgs: &[(u64, Vec<u8>)]) -> Vec<u8> {
    let mux = MuxWriter::with_preamble(preamble, key).unwrap();
    let mut out = Vec::new();
    mux.begin(&mut out);
    for (c, p) in msgs {
        mux.write_message(&mut out, *c, p);
    }
    mux.finish(&mut out);
    out
}

#[test]
fn a_preamble_round_trips_and_c_reads_it() {
    let msgs = vec![(1, b"frame".to_vec()), (2, payload(70_000, 1))];
    let out = with_preamble(META, "media", &msgs);
    assert!(out.starts_with(META));
    assert_eq!(
        MuxWriter::with_preamble(META, "media").unwrap().preamble(),
        META
    );

    // The C demultiplexer and the full parser read it.
    assert_eq!(stream::mux_load(&out, 0, Some("media")).unwrap(), msgs);
    let Value::Struct(fields) = bovnar::parse(&out).unwrap() else {
        unreachable!()
    };
    assert_eq!(fields[0].0, "meta");
    assert_eq!(fields[1].0, "media");

    // So does the codec, handing the preamble over first.
    let mut reader = MuxReader::with_options(0, Some("media")).unwrap();
    let mut input = &out[..];
    assert_eq!(reader.read_preamble(&mut input).unwrap(), Some(META));
    let value = reader.preamble_value(false).unwrap().unwrap();
    assert_eq!(value, bovnar::parse(META).unwrap());
    assert!(reader.preamble_value(true).unwrap().is_some());
    let mut got = Vec::new();
    while let Some(m) = reader.read(&mut input).unwrap() {
        got.push((m.channel, m.payload.to_vec()));
    }
    reader.finish().unwrap();
    assert_eq!(got, msgs);
    assert_eq!(reader.preamble(), Some(META));
}

#[test]
fn a_preamble_split_at_every_byte() {
    let msgs = vec![(1, b"frame".to_vec()), (3, vec![])];
    let out = with_preamble(META, "media", &msgs);
    for at in 0..=out.len() {
        let mut reader = MuxReader::new();
        let mut first = &out[..at];
        let pre = reader
            .read_preamble(&mut first)
            .unwrap()
            .map(<[u8]>::to_vec);
        assert_eq!(
            pre.is_some(),
            at > META.len() + ".media=".len(),
            "split at {at}"
        );
        let mut got = read_parts(&mut reader, &[first, &out[at..]]).unwrap();
        assert_eq!(reader.preamble(), Some(META), "split at {at}");
        got.sort();
        assert_eq!(got, msgs, "split at {at}");
    }
}

#[test]
fn a_preamble_written_by_c() {
    // The C writer's own documents, values first and the stream after it.
    let meta = Value::Struct(vec![
        ("codec".into(), Value::String("av1".into())),
        ("rate".into(), Value::Float(30.0)),
    ]);
    let msgs = vec![(1, b"frame".to_vec())];
    for pretty in [false, true] {
        let text = bovnar::to_string(&meta, pretty).unwrap();
        let doc = [text.clone(), stream::mux_dump(&msgs, "media").unwrap()].concat();
        let mut reader = MuxReader::new();
        assert_eq!(read_parts(&mut reader, &[&doc]).unwrap(), msgs);
        assert_eq!(reader.preamble(), Some(&text[..]));
        assert_eq!(reader.preamble_value(false).unwrap(), Some(meta.clone()));
        assert_eq!(reader.key(), Some("media"));
    }
}

#[test]
fn a_bare_directive_and_whitespace_are_a_preamble() {
    for pre in [&b"#!bovnar 1.2\n"[..], b"\n  ", b"# a comment\n"] {
        let out = with_preamble(pre, "mux", &[(1, b"x".to_vec())]);
        assert_eq!(c_load(&out).unwrap(), [(1, b"x".to_vec())]);
        let mut reader = MuxReader::new();
        assert_eq!(
            read_parts(&mut reader, &[&out]).unwrap(),
            [(1, b"x".to_vec())]
        );
        assert_eq!(reader.preamble(), Some(pre));
    }
}

#[test]
fn bad_preambles_are_refused() {
    // An octet stream, or any NUL byte, is not text.
    let e = MuxWriter::with_preamble(b".blob = \0\0;", "mux").unwrap_err();
    assert_eq!(e.kind(), Some(ErrorCode::OctetStreamForbidden));
    // Text that does not parse, or that swallows the stream's assignment.
    assert!(MuxWriter::with_preamble(b".a = ", "mux").is_err());
    assert!(MuxWriter::with_preamble(b".a = 1; # note", "mux").is_err());
    assert!(MuxWriter::with_preamble(b".a = \"open", "mux").is_err());

    // The reader: text that does not parse before the stream.
    let doc = [
        &b".a = [1;"[..],
        &stream::mux_dump(&[(1, b"x")], "mux").unwrap(),
    ]
    .concat();
    assert!(read_all(&doc).is_err());
    // A stream that is not the last assignment's value.
    assert_eq!(
        kind(&read_all(b".a = 1;\0\0;")),
        Some(ErrorCode::UnexpectedInputByte)
    );
    assert_eq!(
        kind(&read_all(b"\0\0;")),
        Some(ErrorCode::UnexpectedInputByte)
    );
    // An octet stream in the preamble: its 0x00 opens the stream, so with a
    // key the reader names the wrong one, and without it the real stream is
    // a second statement.
    let doc = [
        &b".blob=\0\x01\x01\x00a\0;"[..],
        &stream::mux_dump(&[(1, b"x")], "media").unwrap(),
    ]
    .concat();
    let mut reader = MuxReader::with_options(0, Some("media")).unwrap();
    let e = read_parts(&mut reader, &[&doc]).unwrap_err();
    assert!(e.to_string().contains("\"blob\""), "{e}");
    assert_eq!(kind(&read_all(&doc)), Some(ErrorCode::UnexpectedInputByte));
}

#[test]
fn the_preamble_cap() {
    let pre = format!(".pad = \"{}\";", "x".repeat(100));
    let out = with_preamble(pre.as_bytes(), "mux", &[(1, b"x".to_vec())]);
    let text_len = pre.len() + ".mux=".len();
    let mut reader = MuxReader::new().with_max_preamble(text_len);
    assert_eq!(read_parts(&mut reader, &[&out]).unwrap().len(), 1);
    for parts in [vec![&out[..]], out.chunks(9).collect()] {
        let mut reader = MuxReader::new().with_max_preamble(text_len - 1);
        let r = read_parts(&mut reader, &parts);
        assert_eq!(kind(&r), Some(ErrorCode::TextDataTooLong));
    }
    // The default is 64 KiB.
    let big = many_statements(8000);
    assert!(big.len() > 65_536);
    let out = with_preamble(&big, "mux", &[]);
    assert_eq!(kind(&read_all(&out)), Some(ErrorCode::TextDataTooLong));
    assert_eq!(codec::DEFAULT_MAX_PREAMBLE, 65_536);
}

// ---------------------------------------------------------------------------
// Frames
// ---------------------------------------------------------------------------

/// A long document of short statements (a long string would pass the
/// reader's default string cap).
fn many_statements(n: usize) -> Vec<u8> {
    (0..n)
        .flat_map(|i| format!(".k{i} = {i};").into_bytes())
        .collect()
}

fn frame_docs() -> Vec<Vec<u8>> {
    vec![
        b".a = 1;".to_vec(),
        Vec::new(),
        b".b = [1, 2, 3];".to_vec(),
        many_statements(5000),
        b".c = \"z\";".to_vec(),
    ]
}

fn read_frames(reader: &mut FrameReader, parts: &[&[u8]]) -> bovnar::Result<Vec<Vec<u8>>> {
    let mut got = Vec::new();
    for part in parts {
        let mut input = *part;
        while let Some(doc) = reader.read(&mut input)? {
            got.push(doc.to_vec());
        }
        assert!(input.is_empty());
    }
    reader.finish()?;
    Ok(got)
}

#[test]
fn frames_are_byte_identical_to_bvnr_frame_write() {
    let docs = frame_docs();
    let c = stream::frame_documents(&docs).unwrap();
    let mut ours = Vec::new();
    let mut to = Vec::new();
    for d in &docs {
        codec::write_frame(&mut ours, d);
        codec::write_frame_to(&mut to, d).unwrap();
    }
    assert_eq!(ours, c);
    assert_eq!(to, c);
    assert_eq!(codec::frame_header(7), *b"BVF1\x07\0\0\0\0\0\0\0");
    let loaded = stream::load_documents(&ours, false, false, 0).unwrap();
    assert_eq!(loaded.len(), docs.len());
}

#[test]
fn frame_reader_reads_every_split() {
    let docs = frame_docs();
    let c = stream::frame_documents(&docs).unwrap();
    assert_eq!(read_frames(&mut FrameReader::new(), &[&c]).unwrap(), docs);
    for at in 0..=c.len() {
        if at > 200 && at + 200 < c.len() && at % 101 != 0 {
            continue;
        }
        let got = read_frames(&mut FrameReader::new(), &[&c[..at], &c[at..]]).unwrap();
        assert_eq!(got, docs, "split at {at}");
    }
    let bytes: Vec<&[u8]> = c.chunks(1).collect();
    assert_eq!(read_frames(&mut FrameReader::new(), &bytes).unwrap(), docs);
    // The documents parse as load_documents parses them.
    let parsed: Vec<_> = docs.iter().map(|d| bovnar::parse(d).ok()).collect();
    assert_eq!(stream::load_documents(&c, false, false, 0).unwrap(), parsed);
}

#[test]
fn frames_whole_in_the_input_are_borrowed() {
    let c = stream::frame_documents(&frame_docs()).unwrap();
    let range = c.as_ptr_range();
    let mut reader = FrameReader::new();
    let mut input = &c[..];
    while let Some(doc) = reader.read(&mut input).unwrap() {
        assert!(doc.is_empty() || range.contains(&doc.as_ptr()));
    }
}

#[test]
fn frame_limits_and_errors() {
    let c = stream::frame_documents(&[b".a = 1;"]).unwrap();
    // max_document_size: at the cap reads, over it fails, as in C.
    assert_eq!(
        read_frames(&mut FrameReader::with_max_document_size(7), &[&c])
            .unwrap()
            .len(),
        1
    );
    let r = read_frames(&mut FrameReader::with_max_document_size(6), &[&c]);
    assert_eq!(r.unwrap_err().kind(), Some(ErrorCode::ValueOutOfRange));
    assert!(stream::load_documents(&c, false, false, 6).is_err());
    // The default cap is 256 MiB, and a forged length allocates nothing.
    let forged = codec::frame_header(256 * 1024 * 1024 + 1);
    let r = read_frames(&mut FrameReader::new(), &[&forged]);
    assert_eq!(r.unwrap_err().kind(), Some(ErrorCode::ValueOutOfRange));
    // A length that fits usize everywhere, wasm32 included.
    let mut reader = FrameReader::with_max_document_size(u64::MAX);
    let mut input = &codec::frame_header(u64::from(u32::MAX))[..];
    assert!(reader.read(&mut input).unwrap().is_none());
    // One past usize is out of range even with no cap.
    if let Some(over) = (usize::MAX as u64).checked_add(1) {
        let mut reader = FrameReader::with_max_document_size(u64::MAX);
        let mut input = &codec::frame_header(over)[..];
        let e = reader.read(&mut input).unwrap_err();
        assert_eq!(e.kind(), Some(ErrorCode::ValueOutOfRange));
    }
    // Bad magic.
    let r = read_frames(&mut FrameReader::new(), &[b"BVF2\0\0\0\0\0\0\0\0"]);
    assert_eq!(r.unwrap_err().kind(), Some(ErrorCode::UnexpectedInputByte));
    // Every cut inside a header or a document is a truncation, as for C.
    let two = stream::frame_documents(&[&b".a = 1;"[..], b".b = 2;"]).unwrap();
    for n in 0..=two.len() {
        let boundary = n == 0 || n == FRAME_HEADER_SIZE + 7 || n == two.len();
        let r = read_frames(&mut FrameReader::new(), &[&two[..n]]);
        assert_eq!(r.is_ok(), boundary, "cut at {n}");
        assert_eq!(
            stream::load_documents(&two[..n], false, false, 0).is_ok(),
            boundary
        );
        if !boundary {
            assert_eq!(
                r.unwrap_err().kind(),
                Some(ErrorCode::GotIncompleteBvnrStream)
            );
        }
    }
}

#[test]
fn a_call_after_a_peek_without_the_rest_of_the_input_is_refused() {
    let c = stream::mux_dump(&[(1, b"payload".to_vec())], "mux").unwrap();
    let mut reader = MuxReader::new();
    let mut input = &c[..];
    loop {
        match reader.next_item(&mut input, 4).unwrap() {
            Some(Item::Peek(_)) => break,
            Some(_) => {}
            None => panic!("no peek"),
        }
    }
    // Not the rest of the input: refused, not a slice out of bounds.
    let r = reader.next_item(&mut &[][..], 4);
    assert!(matches!(r, Err(Error::Argument { .. })), "{r:?}");
}
