// SPDX-License-Identifier: MIT
//! Incremental framing for streams that never end: octet multiplexing and
//! `BVF1` document frames, in pure Rust.
//!
//! [`crate::stream`] reads and writes whole buffers through the C library.
//! This module does the same two wire conventions of doc/10 §2 and §3 on a
//! live stream instead: a writer hands out each message's bytes as soon as it
//! is sent, and a reader is fed bytes in pieces of any size and hands out each
//! message as soon as its last byte arrives. No message goes through C (only
//! a preamble is checked with the C parser, once per stream), and neither
//! allocates per message once its buffers have grown to the largest message.
//!
//! # Octet multiplexing
//!
//! A multiplexed stream is one document holding one octet stream (spec §9):
//!
//! ```text
//! .KEY=  0x00  { 0x01 len:u16-LE chunk }  0x00  ;
//! chunk   = varint(channel) fragment          (one channel per chunk)
//! channel = { varint(message_len) message }   (the fragments of a channel, in order)
//! ```
//!
//! The varints are unsigned LEB128, a chunk length of `0x0000` is 65536, and a
//! message may span any number of chunks. [`MuxWriter`] writes exactly what
//! `bvnr_mux_send` writes, so its output is byte for byte that of
//! [`crate::stream::mux_dump`]. [`MuxReader`] reads that and every other valid
//! octet region: several messages packed into one chunk, channels interleaved
//! chunk by chunk, a length varint split across chunks.
//!
//! **A preamble.** Text may come before the stream's assignment in the same
//! document: complete statements such as `.meta = { .codec = "av1"; };`, a
//! `#!bovnar` directive first if needed, so that a stream carries ordinary
//! values about itself ([`MuxWriter::with_preamble`]). The preamble is text
//! only. Text holds no NUL byte, so the reader takes the document's first
//! `0x00` for the stream's opener: it gathers the text up to it (at most
//! [`DEFAULT_MAX_PREAMBLE`] bytes, or the cap of
//! [`MuxReader::with_max_preamble`]), requires it to end with the assignment
//! `.KEY =`, and checks the preamble once with the full parser. The preamble
//! is then [`MuxReader::preamble`], before any message, and the reader goes
//! on to the octet stream, which never goes through C.
//!
//! **What the reader reads.** A document of a preamble (possibly empty), one
//! octet stream as the value of its last assignment (`.KEY =`, whitespace
//! around the `=` but no comment between it and the stream), `;`, and then
//! only whitespace and comments, which may also come before the `;`. An
//! octet stream in the preamble, a statement after the stream, or any other
//! byte after it is an error. To pull a
//! multiplexed stream out of a general document, parse it with
//! [`crate::stream::mux_load`] or [`crate::Reader`].
//!
//! The reader keeps the C demultiplexer's limits and errors (doc/10 §3.2):
//!
//! | Condition | [`ErrorCode`] |
//! |---|---|
//! | A chunk tag other than `0x01` or `0x00`; a channel varint not whole in its chunk; a varint over 10 bytes or over 64 bits; a 4097th distinct channel | `OctetStreamOutOfSync` |
//! | A declared message length over `max_message` (default 64 MiB) | `ValueOutOfRange` |
//! | The octet stream closes while a message (or its length) is incomplete | `OctetStreamTruncated` |
//! | The input ends inside the octet stream ([`MuxReader::finish`]) | `ReadCompleteChunkFailed` |
//! | The input ends before the octet stream opens, or before the `;` ([`MuxReader::finish`]) | `GotIncompleteBvnrStream` |
//! | The text before the stream is over the preamble cap | `TextDataTooLong` |
//! | The text before the `0x00` does not end in `.KEY =`, or a byte other than whitespace follows the `;` | `UnexpectedInputByte` |
//! | The preamble does not parse | the parser's [`Error::Parse`] |
//! | The stream is under another key than [`MuxReader::with_options`] asked for | `UnexpectedInputByte`, as an [`Error::Argument`] naming both keys |
//!
//! The first five rows are the codes [`crate::stream::mux_load`] gives for
//! the same input. The rest are the codec's own: it reads this one shape,
//! where `mux_load` parses any document, and accepts one without an octet
//! stream, one with statements after it, or one under any key. An error is
//! latched: every later call returns it again.
//!
//! # Frames
//!
//! [`frame_header`], [`write_frame`] and [`write_frame_to`] write the
//! `"BVF1" len:u64-LE document` records of doc/10 §2, as `bvnr_frame_write`
//! does, and [`FrameReader`] reads them back from pieces, one complete
//! document at a time, under the same `max_document_size` as
//! `bvnr_doc_stream_read`.
//!
//! # Feeding a reader
//!
//! [`MuxReader::read`], [`MuxReader::next_item`] and [`FrameReader::read`]
//! take `&mut &[u8]`: the bytes not yet read. Each call reads from the front
//! and leaves the rest. A call that returns `Ok(None)` has taken every byte
//! (what is part of an unfinished message is kept inside the reader), so the
//! caller's buffer can be refilled. A call that returns an item may leave
//! bytes; pass the same slice to the next call until it returns `None`.
//!
//! A message or document that lies whole in the bytes passed to the call is
//! returned as a slice of those bytes, without a copy. One that arrives over
//! several calls, or over several chunks, is gathered in a buffer the reader
//! keeps and reuses. Either way the slice is valid until the next call.
#![forbid(unsafe_code)]

use std::collections::HashMap;
use std::hash::{BuildHasherDefault, Hasher};
use std::io::{self, IoSlice, Write};

use crate::enums::ErrorCode;
use crate::error::{Error, Result};
use crate::stream::{BVNR_DOC_DEFAULT_MAX, BVNR_MUX_DEFAULT_MAX};

/// The largest octet chunk (spec §9.2); its length field reads `0x0000`.
pub const CHUNK_SIZE: usize = 65536;

/// The most distinct channels one multiplexed stream may use, as in the C
/// demultiplexer: a chunk naming a 4097th is `octet_stream_out_of_sync`.
pub const MAX_CHANNELS: usize = 4096;

/// The longest key [`MuxWriter`] writes and [`MuxReader`] reads: the C
/// reader's default `max_identifier_length`, so every reader left at its
/// defaults reads the stream.
pub const MAX_KEY_LEN: usize = 255;

/// The size of a `BVF1` frame header: the magic and a `u64` length.
pub const FRAME_HEADER_SIZE: usize = 12;

/// The magic that opens every frame.
pub const FRAME_MAGIC: [u8; 4] = *b"BVF1";

/// The longest chunk header [`MuxWriter`] writes: tag, length, and the
/// channel and length varints.
pub const MAX_CHUNK_HEADER: usize = 3 + 2 * MAX_VARINT;

// Cut::header encodes both varints into a header buffer of this size, each
// into ten bytes of room.
const _: () = assert!(MAX_CHUNK_HEADER >= 3 + MAX_VARINT + MAX_VARINT);

/// The default cap of [`MuxReader::with_max_preamble`]: the text before a
/// multiplexed stream's octet stream, preamble and opening assignment.
pub const DEFAULT_MAX_PREAMBLE: usize = 64 * 1024;

const MAX_VARINT: usize = 10;

/// Channel ids below this are looked up in a table rather than hashed.
const SMALL_CHANNELS: usize = 64;

/// What closes the octet stream and the document, as the C writer ends it.
const TERMINATOR: &[u8] = b"\0;";

// ---------------------------------------------------------------------------
// Varints and keys
// ---------------------------------------------------------------------------

/// Unsigned LEB128 into `out`; returns its length.
#[inline]
fn varint_encode(mut v: u64, out: &mut [u8; MAX_VARINT]) -> usize {
    // Over the array rather than by index: no bounds check per byte.
    for (n, byte) in out.iter_mut().enumerate() {
        if v < 0x80 {
            *byte = v as u8;
            return n + 1;
        }
        *byte = v as u8 | 0x80;
        v >>= 7;
    }
    unreachable!("a u64 is at most ten groups of seven bits")
}

/// Decodes the varint at the start of `buf` as `bvn_varint_decode` does:
/// `Ok(Some((value, length)))`, `Ok(None)` when `buf` ends inside it, and
/// `Err(())` when it is longer than ten bytes or does not fit 64 bits (the
/// tenth byte may carry only the lowest bit).
#[inline(always)]
fn varint_decode(buf: &[u8]) -> std::result::Result<Option<(u64, usize)>, ()> {
    // Channels are one byte, and the length of a message that fits one chunk
    // is at most three.
    let low = |b: u8| u64::from(b & 0x7F);
    match *buf {
        [b0, ..] if b0 < 0x80 => Ok(Some((u64::from(b0), 1))),
        [b0, b1, ..] if b1 < 0x80 => Ok(Some((low(b0) | u64::from(b1) << 7, 2))),
        [b0, b1, b2, ..] if b2 < 0x80 => {
            Ok(Some((low(b0) | low(b1) << 7 | u64::from(b2) << 14, 3)))
        }
        _ => varint_decode_long(buf),
    }
}

fn varint_decode_long(buf: &[u8]) -> std::result::Result<Option<(u64, usize)>, ()> {
    let mut v = 0u64;
    for (i, &byte) in buf.iter().take(MAX_VARINT).enumerate() {
        if i == MAX_VARINT - 1 && byte & 0xFE != 0 {
            return Err(());
        }
        v |= u64::from(byte & 0x7F) << (7 * i);
        if byte & 0x80 == 0 {
            return Ok(Some((v, i + 1)));
        }
    }
    Ok(None)
}

/// Can `b` begin a key? An ASCII letter, `_`, or the lead byte of a code
/// point from U+00C0 on (`bvn_validate_identifier`).
fn key_start(b: u8) -> bool {
    b.is_ascii_alphabetic() || b == b'_' || (0xC3..=0xF4).contains(&b)
}

/// May `b` stand inside a key after its first byte? Not a control byte or
/// space, not 0xC2 (U+0080 to U+00BF), and none of the punctuation spec §4.2
/// forbids.
fn key_body(b: u8) -> bool {
    b > 0x20 && b != 0x7F && b != 0xC2 && !b"\"#,./;<=>[]{}!$%&'()*:?@\\^`|~".contains(&b)
}

/// Why the C writer would refuse `key` (or a default reader not read it).
fn key_error(key: &[u8]) -> Option<ErrorCode> {
    let Some((&first, rest)) = key.split_first() else {
        return Some(ErrorCode::EmptyIdentifier);
    };
    if key.len() > MAX_KEY_LEN {
        Some(ErrorCode::IdentifierTooLong)
    } else if !key_start(first) || !rest.iter().all(|&b| key_body(b)) {
        Some(ErrorCode::UnexpectedInputByte)
    } else if std::str::from_utf8(key).is_err() {
        Some(ErrorCode::InvalidUtf8Byte)
    } else {
        None
    }
}

fn check_key(key: &str) -> Result<()> {
    match key_error(key.as_bytes()) {
        None => Ok(()),
        Some(code) => Err(Error::argument_with(
            code,
            format!(
                "{key:?} is not a mux key: it must be 1 to {MAX_KEY_LEN} bytes, start with a \
                 letter or `_`, and hold no space, control byte or punctuation of spec §4.2"
            ),
        )),
    }
}

// ---------------------------------------------------------------------------
// Writer
// ---------------------------------------------------------------------------

/// How `bvnr_mux_send` cuts one message into chunks: the channel varint leads
/// every chunk, the length varint follows it in the first, and each chunk is
/// filled to 65536 bytes before the next begins.
#[derive(Clone, Copy)]
struct Cut {
    channel: u64,
    channel_len: usize,
    length_len: usize,
    len: usize,
}

impl Cut {
    #[inline]
    fn new(channel: u64, len: usize) -> Self {
        Cut {
            channel,
            channel_len: varint_len(channel),
            length_len: varint_len(len as u64),
            len,
        }
    }

    /// Payload bytes in the chunk that starts at payload offset `pos`.
    #[inline]
    fn take(&self, pos: usize) -> usize {
        let room = if pos == 0 {
            CHUNK_SIZE - self.channel_len - self.length_len
        } else {
            CHUNK_SIZE - self.channel_len
        };
        (self.len - pos).min(room)
    }

    /// Writes the header of the chunk at payload offset `pos` to the front
    /// of `out`; returns its length and how many payload bytes the chunk
    /// carries.
    ///
    /// The bytes go straight to where they are wanted. A header put together
    /// byte by byte elsewhere and then copied is read back wider than it was
    /// written, which the CPU cannot forward from its store buffer: that
    /// stall cost more than the rest of the message.
    #[inline]
    fn header(&self, pos: usize, out: &mut [u8; MAX_CHUNK_HEADER]) -> (usize, usize) {
        let take = self.take(pos);
        let varints = self.channel_len + if pos == 0 { self.length_len } else { 0 };
        // 65536 wraps to the 0x0000 that stands for it.
        let body = ((varints + take) as u32 as u16).to_le_bytes();
        out[0] = 0x01;
        out[1] = body[0];
        out[2] = body[1];
        varint_encode(self.channel, varint_room(out, 3));
        if pos == 0 {
            varint_encode(self.len as u64, varint_room(out, 3 + self.channel_len));
        }
        (3 + varints, take)
    }

    fn chunks(&self) -> usize {
        let rest = self.len - self.take(0);
        1 + rest.div_ceil(CHUNK_SIZE - self.channel_len)
    }
}

/// The length of `v` as a varint: one byte per seven bits, at least one.
#[inline]
fn varint_len(v: u64) -> usize {
    (64 - (v | 1).leading_zeros() as usize).div_ceil(7)
}

/// The ten bytes of `header` from `at` on, where a varint goes.
#[inline]
fn varint_room(header: &mut [u8; MAX_CHUNK_HEADER], at: usize) -> &mut [u8; MAX_VARINT] {
    header[at..]
        .first_chunk_mut()
        .expect("MAX_CHUNK_HEADER leaves room for both varints")
}

/// Appends the header of the chunk at payload offset `pos` to `out`, written
/// in place (see [`Cut::header`]); returns the payload bytes the chunk
/// carries.
#[inline]
fn push_header(out: &mut Vec<u8>, cut: &Cut, pos: usize) -> usize {
    let at = out.len();
    out.resize(at + MAX_CHUNK_HEADER, 0);
    let room = out[at..].first_chunk_mut().expect("just made room");
    let (n, take) = cut.header(pos, room);
    out.truncate(at + n);
    take
}

/// [`push_header`] for a chunk after the first, whose header is the tag, the
/// length and the channel varint. With a channel varint of at most five
/// bytes that fits a word, and one store of it replaces zeroing room first.
#[inline]
fn push_later_header(out: &mut Vec<u8>, cut: &Cut, pos: usize) -> usize {
    if cut.channel_len > 5 {
        return push_header(out, cut, pos);
    }
    let take = cut.take(pos);
    let body = u64::from((cut.channel_len + take) as u32 as u16);
    let word = 0x01 | body << 8 | varint_bits(cut.channel) << 24;
    let at = out.len();
    out.extend_from_slice(&word.to_le_bytes());
    out.truncate(at + 3 + cut.channel_len);
    take
}

/// `v` as a varint in the low bytes of a word, the first byte lowest: for a
/// `v` of at most 56 bits, whose varint fits.
#[inline]
fn varint_bits(mut v: u64) -> u64 {
    let mut bits = 0;
    let mut shift = 0;
    while v >= 0x80 {
        bits |= u64::from(v as u8 | 0x80) << shift;
        v >>= 7;
        shift += 8;
    }
    bits | v << shift
}

/// Appends the chunks of one message, as `bvnr_mux_send` writes them.
#[inline]
fn encode_into(out: &mut Vec<u8>, channel: u64, payload: &[u8]) {
    let cut = Cut::new(channel, payload.len());
    out.reserve(payload.len() + cut.chunks() * MAX_CHUNK_HEADER);
    let mut pos = push_header(out, &cut, 0);
    out.extend_from_slice(&payload[..pos]);
    while pos < payload.len() {
        let take = push_later_header(out, &cut, pos);
        out.extend_from_slice(&payload[pos..pos + take]);
        pos += take;
    }
}

/// `write_all` for several buffers, which `std` has only on nightly.
fn write_all_vectored<W: Write + ?Sized>(
    out: &mut W,
    mut bufs: &mut [IoSlice<'_>],
) -> io::Result<()> {
    IoSlice::advance_slices(&mut bufs, 0);
    while !bufs.is_empty() {
        match out.write_vectored(bufs) {
            Ok(0) => return Err(io::ErrorKind::WriteZero.into()),
            Ok(n) => IoSlice::advance_slices(&mut bufs, n),
            Err(e) if e.kind() == io::ErrorKind::Interrupted => {}
            Err(e) => return Err(e),
        }
    }
    Ok(())
}

fn sink_error(e: &io::Error) -> Error {
    Error::io(ErrorCode::WritingToSink, e)
}

/// Writes a multiplexed stream: a complete document that holds one octet
/// stream under a key, as `bvnr_mux_begin`, `bvnr_mux_send` and
/// `bvnr_mux_end` write it.
///
/// The writer keeps no state between messages, so one value serves any
/// number of streams and threads. A stream is [`opener`](Self::opener), any
/// number of messages, then [`terminator`](Self::terminator); the bytes are
/// those [`crate::stream::mux_dump`] produces for the same key and messages.
///
/// [`encode`](Self::encode) is the zero-copy form: it writes only the chunk
/// headers, into a buffer the caller owns and reuses, and returns them in wire
/// order with the borrowed payload for a vectored write.
/// [`write_message`](Self::write_message) and
/// [`write_message_to`](Self::write_message_to) write the whole message to a
/// `Vec` or to any [`std::io::Write`].
///
/// ```
/// use bovnar::codec::MuxWriter;
///
/// let mux = MuxWriter::new("mux")?;
/// let mut out = Vec::new();
/// mux.begin(&mut out);
/// mux.write_message(&mut out, 1, b"hello");
/// mux.write_message(&mut out, 42, b"world");
/// mux.finish(&mut out);
/// assert_eq!(out, bovnar::stream::mux_dump(&[(1, b"hello"), (42, b"world")], "mux")?);
/// # Ok::<(), bovnar::Error>(())
/// ```
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct MuxWriter {
    key: Box<str>,
    preamble_len: usize,
    opener: Box<[u8]>,
}

impl MuxWriter {
    /// A writer for streams under `key` (conventionally `"mux"`), with no
    /// preamble.
    ///
    /// The key must be one the C writer accepts and a reader with default
    /// limits reads: 1 to [`MAX_KEY_LEN`] bytes, starting with an ASCII
    /// letter, `_` or a character from U+00C0 on, and holding no space,
    /// control character, U+0080 to U+00BF, or punctuation spec §4.2 forbids
    /// in a key (`-` and `+` are allowed). Anything else is an
    /// [`Error::Argument`] with the code the C writer gives.
    pub fn new(key: &str) -> Result<Self> {
        Self::with_preamble(b"", key)
    }

    /// A writer for streams under `key` that open with `preamble`: complete
    /// Bovnar statements written before the stream's assignment, such as
    /// `.meta = { .codec = "av1"; };`, so a stream can carry ordinary values
    /// about itself. A `#!bovnar` directive may lead it.
    ///
    /// The preamble is text: an octet stream in it is an
    /// [`Error::Argument`] with `octet_stream_forbidden`, because a reader
    /// takes the first `0x00` of the document for the stream's opener. It is
    /// checked once, here, with the full parser ([`crate::parse`]); a
    /// preamble that does not parse is that parse error, also one that ends
    /// inside a comment and so would swallow the stream's assignment (the
    /// parser refuses the opening `0x00` inside the comment). Messages are not
    /// affected: they never go through the C library. A preamble longer than
    /// [`DEFAULT_MAX_PREAMBLE`] is written, but a [`MuxReader`] reads it only
    /// with a larger cap ([`MuxReader::with_max_preamble`]).
    ///
    /// ```
    /// use bovnar::codec::{MuxReader, MuxWriter};
    ///
    /// let mux = MuxWriter::with_preamble(b".meta = { .codec = \"av1\"; };", "media")?;
    /// let mut out = Vec::new();
    /// mux.begin(&mut out);
    /// mux.write_message(&mut out, 0, b"frame");
    /// mux.finish(&mut out);
    ///
    /// let mut reader = MuxReader::new();
    /// let mut input = &out[..];
    /// let preamble = reader.read_preamble(&mut input)?.expect("all of it is in input");
    /// assert_eq!(preamble, b".meta = { .codec = \"av1\"; };");
    /// assert!(reader.preamble_value(false)?.is_some());
    /// assert_eq!(reader.read(&mut input)?.map(|m| m.payload), Some(&b"frame"[..]));
    /// # Ok::<(), bovnar::Error>(())
    /// ```
    pub fn with_preamble(preamble: &[u8], key: &str) -> Result<Self> {
        check_key(key)?;
        if preamble.contains(&0) {
            return Err(Error::argument_with(
                ErrorCode::OctetStreamForbidden,
                "a mux preamble is text: it cannot hold an octet stream (or any NUL byte)",
            ));
        }
        let mut opener = Vec::with_capacity(preamble.len() + key.len() + 3);
        opener.extend_from_slice(preamble);
        opener.push(b'.');
        opener.extend_from_slice(key.as_bytes());
        opener.push(b'=');
        if !preamble.is_empty() {
            check_document(&opener, key.as_bytes())?;
        }
        opener.push(0);
        Ok(MuxWriter {
            key: key.into(),
            preamble_len: preamble.len(),
            opener: opener.into(),
        })
    }

    /// The preamble written before the stream's assignment.
    pub fn preamble(&self) -> &[u8] {
        &self.opener[..self.preamble_len]
    }

    /// The key the stream is written under.
    pub fn key(&self) -> &str {
        &self.key
    }

    /// The bytes that open the stream: the preamble, `.KEY=` and the octet
    /// stream's opening `0x00`.
    pub fn opener(&self) -> &[u8] {
        &self.opener
    }

    /// The bytes that close the stream and its document: the octet stream's
    /// closing `0x00` and `;`.
    pub fn terminator(&self) -> &'static [u8] {
        TERMINATOR
    }

    /// Appends [`opener`](Self::opener) to `out`.
    pub fn begin(&self, out: &mut Vec<u8>) {
        out.extend_from_slice(&self.opener);
    }

    /// Appends [`terminator`](Self::terminator) to `out`.
    pub fn finish(&self, out: &mut Vec<u8>) {
        out.extend_from_slice(TERMINATOR);
    }

    /// Writes the chunk headers of `payload` on `channel` into `head`
    /// (cleared first) and returns the message as its parts in wire order:
    /// a header, the payload bytes of that chunk, the next header, and so on.
    /// Only the headers are written; the payload is borrowed.
    ///
    /// A message of at most [`crate::stream::BVNR_MUX_MAX_MESSAGE`] bytes is
    /// one chunk, so two parts (one for an empty message). A longer one is cut as `bvnr_mux_send` cuts
    /// it: every chunk filled to 65536 bytes, the channel varint leading each.
    /// `head` grows to the largest message's headers and then stays.
    ///
    /// ```
    /// use std::io::{IoSlice, Write};
    /// use bovnar::codec::MuxWriter;
    ///
    /// let mux = MuxWriter::new("mux")?;
    /// let mut socket = Vec::new(); // any Write
    /// let mut head = Vec::new();
    /// socket.write_all(mux.opener())?;
    /// let parts = mux.encode(&mut head, 7, b"payload");
    /// let mut slices: Vec<IoSlice<'_>> = parts.map(IoSlice::new).collect();
    /// // A vectored write may take only part of the message: go on until it
    /// // is all written (write_message_to does this for you).
    /// let mut rest = &mut slices[..];
    /// while !rest.is_empty() {
    ///     let n = socket.write_vectored(rest)?;
    ///     if n == 0 {
    ///         return Err(std::io::Error::from(std::io::ErrorKind::WriteZero).into());
    ///     }
    ///     IoSlice::advance_slices(&mut rest, n);
    /// }
    /// socket.write_all(mux.terminator())?;
    /// # Ok::<(), Box<dyn std::error::Error>>(())
    /// ```
    #[inline]
    pub fn encode<'a>(&self, head: &'a mut Vec<u8>, channel: u64, payload: &'a [u8]) -> Parts<'a> {
        let cut = Cut::new(channel, payload.len());
        head.clear();
        let mut pos = push_header(head, &cut, 0);
        let mut chunks = 1;
        while pos < payload.len() {
            pos += push_later_header(head, &cut, pos);
            chunks += 1;
        }
        Parts {
            head,
            payload,
            head_rest: head,
            payload_rest: payload,
            next: 3 + cut.channel_len + cut.length_len,
            later: 3 + cut.channel_len,
            pending: 0,
            left: 2 * chunks - usize::from(payload.is_empty()),
        }
    }

    /// Appends the whole message to `out`: its chunk headers and a copy of
    /// its payload.
    pub fn write_message(&self, out: &mut Vec<u8>, channel: u64, payload: &[u8]) {
        encode_into(out, channel, payload);
    }

    /// Writes the whole message to `out`, one vectored write per chunk with
    /// the header on the stack, so neither the payload is copied nor anything
    /// allocated. `out` is not flushed. A failing `out` is an [`Error::Io`]
    /// with `writing_to_sink`.
    pub fn write_message_to<W: Write + ?Sized>(
        &self,
        out: &mut W,
        channel: u64,
        payload: &[u8],
    ) -> Result<()> {
        let cut = Cut::new(channel, payload.len());
        let mut header = [0; MAX_CHUNK_HEADER];
        let mut pos = 0;
        loop {
            let (n, take) = cut.header(pos, &mut header);
            let mut bufs = [
                IoSlice::new(&header[..n]),
                IoSlice::new(&payload[pos..pos + take]),
            ];
            write_all_vectored(out, &mut bufs).map_err(|e| sink_error(&e))?;
            pos += take;
            if pos == payload.len() {
                return Ok(());
            }
        }
    }
}

/// One message as [`MuxWriter::encode`] returns it: an iterator over its
/// wire bytes in order, alternating chunk headers (from the caller's buffer)
/// and payload slices (borrowed). An empty payload is one header and no
/// payload slice; no part is ever empty.
#[derive(Clone, Debug)]
pub struct Parts<'a> {
    head: &'a [u8],
    payload: &'a [u8],
    /// The headers and the payload bytes not yet handed out.
    head_rest: &'a [u8],
    payload_rest: &'a [u8],
    /// The length of the next header, and of each header after the first.
    next: usize,
    later: usize,
    /// The payload bytes of the current chunk, the next part when not 0.
    pending: usize,
    left: usize,
}

impl<'a> Parts<'a> {
    /// The message's size on the wire: headers and payload.
    pub fn wire_len(&self) -> usize {
        self.head.len() + self.payload.len()
    }

    /// All chunk headers, back to back, as written into the caller's buffer.
    pub fn headers(&self) -> &'a [u8] {
        self.head
    }
}

impl<'a> Iterator for Parts<'a> {
    type Item = &'a [u8];

    #[inline]
    fn next(&mut self) -> Option<&'a [u8]> {
        if self.pending > 0 {
            let (part, rest) = self.payload_rest.split_at(self.pending);
            self.payload_rest = rest;
            self.pending = 0;
            self.left -= 1;
            return Some(part);
        }
        if self.head_rest.is_empty() {
            return None;
        }
        let (part, rest) = self.head_rest.split_at(self.next);
        self.head_rest = rest;
        // What a chunk under this header holds of the payload (Cut::take).
        self.pending = self.payload_rest.len().min(CHUNK_SIZE + 3 - self.next);
        self.next = self.later;
        self.left -= 1;
        Some(part)
    }

    fn size_hint(&self) -> (usize, Option<usize>) {
        (self.left, Some(self.left))
    }
}

impl ExactSizeIterator for Parts<'_> {}

// ---------------------------------------------------------------------------
// Reader
// ---------------------------------------------------------------------------

/// A message [`MuxReader`] has read whole.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Message<'a> {
    /// The channel it was sent on.
    pub channel: u64,
    /// Its bytes: borrowed from the input when they lay whole in it, else
    /// from the reader's buffer for the channel.
    pub payload: &'a [u8],
}

/// A message's wire bytes, ready to be written into any multiplexed stream
/// between two messages: whole chunks that carry this message and nothing
/// else.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Wire<'a> {
    /// The channel it was sent on.
    pub channel: u64,
    /// The length of its payload.
    pub len: u64,
    /// Its chunks. When the message lay in the input as consecutive chunks of
    /// its own, these are those input bytes, unchanged and not copied;
    /// otherwise the reader gathered the payload and wrote it out again as
    /// [`MuxWriter`] would.
    pub bytes: &'a [u8],
}

/// What becomes of a message [`MuxReader::next_item`] peeked at.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
enum Decision {
    #[default]
    Deliver,
    Skip,
    Forward,
}

/// The start of a message: its channel, its length and its first bytes, read
/// without touching the rest. Drop it to receive the message as an
/// [`Item::Message`], or decide otherwise with [`skip`](Self::skip) or
/// [`forward`](Self::forward).
#[derive(Debug)]
pub struct Peek<'a> {
    /// The channel the message is on.
    pub channel: u64,
    /// The length of its payload, as declared.
    pub len: u64,
    /// Its first bytes: as many as asked for, or all of a shorter message.
    /// Fewer only when the producer split the message's start across chunks,
    /// which `bvnr_mux_send` and [`MuxWriter`] never do for the first 65516
    /// bytes.
    pub head: &'a [u8],
    decision: &'a mut Decision,
}

impl Peek<'_> {
    /// Drops the message: its remaining bytes are passed over as they
    /// arrive, without being gathered.
    pub fn skip(self) {
        *self.decision = Decision::Skip;
    }

    /// Hands the message on as an [`Item::Wire`] instead of an
    /// [`Item::Message`].
    pub fn forward(self) {
        *self.decision = Decision::Forward;
    }
}

/// What [`MuxReader::next_item`] returns.
#[derive(Debug)]
pub enum Item<'a> {
    /// A message has started.
    Peek(Peek<'a>),
    /// A message is complete.
    Message(Message<'a>),
    /// A message marked with [`Peek::forward`] is complete.
    Wire(Wire<'a>),
}

/// Where the reader is in the document.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum State {
    /// The text before the octet stream: the preamble and the assignment
    /// that opens the stream, up to the stream's `0x00`.
    Text,
    /// Expects a chunk tag, or the `0x00` that closes the octet stream.
    Tag,
    /// Has the tag, expects the length's low byte.
    Len0,
    /// Has the length's low byte.
    Len1(u8),
    /// Inside a chunk's channel varint.
    Channel,
    /// Inside a chunk's fragment.
    Fragment,
    /// After the closing `0x00`, expects `;`.
    Semicolon,
    /// Inside a comment before the `;`.
    SemicolonComment,
    /// The document is complete; only whitespace and comments may follow.
    Done,
    /// Inside a comment after the `;`.
    DoneComment,
}

/// One channel's reassembly state (`demux_channel_t`).
#[derive(Debug)]
struct Channel {
    id: u64,
    /// The current message, as far as it has been gathered.
    buf: Vec<u8>,
    need: u64,
    have: u64,
    active: bool,
    decision: Decision,
    /// A length varint split across chunks.
    varint: [u8; MAX_VARINT],
    varint_len: u8,
}

impl Channel {
    fn new(id: u64, buf: Vec<u8>) -> Self {
        Channel {
            id,
            buf,
            need: 0,
            have: 0,
            active: false,
            decision: Decision::Deliver,
            varint: [0; MAX_VARINT],
            varint_len: 0,
        }
    }
}

/// A message start whose chunk header was read straight from the input: the
/// input was left at the chunk's tag, so the message can still be handed on
/// as the input's own bytes. Its channel is left as it was, with nothing
/// open, until the message turns out not to lie whole in the input.
#[derive(Clone, Copy, Debug)]
struct Fresh {
    channel: u64,
    len: u64,
    /// Tag, length and both varints.
    header: usize,
    /// Where the chunk ends, from its tag.
    end: usize,
}

/// A chunk at the front of the input that starts a message on a channel with
/// nothing open, and carries nothing but that message: all of it, or its
/// first part.
#[derive(Clone, Copy, Debug)]
struct Start {
    cur: usize,
    channel: u64,
    len: u64,
    /// Tag, length and both varints.
    header: usize,
    /// Where the chunk ends, from its tag.
    end: usize,
}

impl Start {
    /// Whether the chunk holds all of the message.
    #[inline]
    fn whole(&self) -> bool {
        self.len == (self.end - self.header) as u64
    }
}

#[derive(Clone, Copy, Debug, Default)]
enum Pending {
    #[default]
    None,
    /// A message's first bytes are being gathered into `head` before it is
    /// peeked at.
    Gathering { target: usize },
    /// A peek was returned; its decision is applied on the next call.
    Peeked { fresh: Option<Fresh> },
}

/// Where a returned slice lives.
#[derive(Clone, Copy, Debug)]
enum Src<'a> {
    Input(&'a [u8]),
    Channel(usize),
    Head,
    Wire,
}

/// What one step of the reader found, before it is turned into borrowed
/// output.
#[derive(Debug)]
enum Found<'a> {
    Nothing,
    Message {
        channel: u64,
        src: Src<'a>,
    },
    Peek {
        channel: u64,
        len: u64,
        src: Src<'a>,
    },
    Wire {
        channel: u64,
        len: u64,
        src: Src<'a>,
    },
}

/// A hasher for channel ids. They come from the input, but at most
/// [`MAX_CHANNELS`] are ever stored, which bounds what colliding ids can cost;
/// SipHash would cost more than the rest of a message.
#[derive(Default)]
struct ChannelHasher(u64);

impl Hasher for ChannelHasher {
    fn finish(&self) -> u64 {
        self.0
    }

    fn write(&mut self, bytes: &[u8]) {
        for &b in bytes {
            self.write_u64(self.0 ^ u64::from(b));
        }
    }

    fn write_u64(&mut self, v: u64) {
        let h = (v ^ (v >> 33)).wrapping_mul(0xFF51_AFD7_ED55_8CCD);
        self.0 = h ^ (h >> 33);
    }
}

/// Reads a multiplexed stream fed in pieces of any size: the push-fed
/// counterpart of [`crate::stream::mux_load`] and of the C demultiplexer.
///
/// [`read`](Self::read) returns each message once it is complete, in the
/// order messages complete (which is the order they were sent, unless a
/// producer interleaves the chunks of large messages).
/// [`next_item`](Self::next_item) also stops at the start of every message
/// with an [`Item::Peek`], for a router that decides by channel or by the
/// first bytes whether to receive, skip or forward it.
///
/// Memory: one buffer per channel, grown to that channel's largest message
/// that did not lie whole in the input; a chunk header or message start
/// gathered for a peek (at most one chunk, 64 KiB); the preamble's text (at
/// most the preamble cap); and for [`next_item`](Self::next_item), the
/// largest forwarded message that had to be rewritten (at most
/// `max_message`). Nothing is allocated for a message once those buffers are
/// large enough.
///
/// ```
/// use bovnar::codec::MuxReader;
///
/// let stream = bovnar::stream::mux_dump(&[(1, b"hello"), (42, b"world")], "mux")?;
/// let mut reader = MuxReader::new();
/// let mut got = Vec::new();
/// for piece in stream.chunks(3) {
///     let mut input = piece;
///     while let Some(m) = reader.read(&mut input)? {
///         got.push((m.channel, m.payload.to_vec()));
///     }
/// }
/// reader.finish()?;
/// assert_eq!(got, [(1, b"hello".to_vec()), (42, b"world".to_vec())]);
/// # Ok::<(), bovnar::Error>(())
/// ```
#[derive(Debug)]
pub struct MuxReader {
    max_message: u64,
    filter: Option<Box<[u8]>>,
    max_preamble: usize,
    state: State,
    key: Vec<u8>,
    /// The text before the octet stream while it is read; the preamble once
    /// the stream is open.
    text: Vec<u8>,
    /// Bytes left in the current chunk.
    chunk_left: usize,
    /// A channel varint split across pieces of input.
    channel_varint: [u8; MAX_VARINT],
    channel_varint_len: usize,
    /// The channel of the current chunk.
    cur: usize,
    channels: Vec<Channel>,
    /// The buffers of the channels of earlier streams, kept by
    /// [`reset`](Self::reset) for the channels of the next.
    spare: Vec<Vec<u8>>,
    index: HashMap<u64, u32, BuildHasherDefault<ChannelHasher>>,
    /// The channel found last, checked before the index.
    last: usize,
    /// The channels with the smallest ids, the usual ones, found without
    /// hashing: their index plus one, or 0.
    small: [u16; SMALL_CHANNELS],
    pending: Pending,
    decision: Decision,
    head: Vec<u8>,
    wire: Vec<u8>,
    error: Option<Error>,
}

impl Default for MuxReader {
    fn default() -> Self {
        Self::new()
    }
}

impl MuxReader {
    /// A reader with the C defaults: messages up to
    /// [`crate::stream::BVNR_MUX_DEFAULT_MAX`] (64 MiB), any key.
    pub fn new() -> Self {
        MuxReader {
            max_message: BVNR_MUX_DEFAULT_MAX,
            filter: None,
            max_preamble: DEFAULT_MAX_PREAMBLE,
            state: State::Text,
            key: Vec::new(),
            text: Vec::new(),
            chunk_left: 0,
            channel_varint: [0; MAX_VARINT],
            channel_varint_len: 0,
            cur: 0,
            channels: Vec::new(),
            spare: Vec::new(),
            index: HashMap::default(),
            last: usize::MAX,
            small: [0; SMALL_CHANNELS],
            pending: Pending::None,
            decision: Decision::Deliver,
            head: Vec::new(),
            wire: Vec::new(),
            error: None,
        }
    }

    /// A reader with limits, like [`crate::stream::mux_load`]'s:
    /// `max_message` caps a declared message length (0 is the default,
    /// 64 MiB), and with `key` the stream must be under that key. A stream
    /// under another key is an [`Error::Argument`] naming both, where
    /// `bvnr_demux_set_key` would ignore it: this reader reads one stream, so
    /// there is nothing else to read. An invalid `key` could never match, and
    /// is an [`Error::Argument`] at once.
    pub fn with_options(max_message: u64, key: Option<&str>) -> Result<Self> {
        if let Some(k) = key {
            check_key(k)?;
        }
        Ok(MuxReader {
            max_message: if max_message == 0 {
                BVNR_MUX_DEFAULT_MAX
            } else {
                max_message
            },
            filter: key.map(|k| k.as_bytes().into()),
            ..Self::new()
        })
    }

    /// The same reader with another cap on the text before the octet stream
    /// (the preamble and the assignment that opens the stream): `max_preamble`
    /// bytes, 0 for the default [`DEFAULT_MAX_PREAMBLE`] (64 KiB). Longer
    /// text is `text_data_too_long`.
    pub fn with_max_preamble(mut self, max_preamble: usize) -> Self {
        self.max_preamble = if max_preamble == 0 {
            DEFAULT_MAX_PREAMBLE
        } else {
            max_preamble
        };
        self
    }

    /// The stream's key, once the stream is open.
    pub fn key(&self) -> Option<&str> {
        match self.state {
            State::Text => None,
            _ => std::str::from_utf8(&self.key).ok(),
        }
    }

    /// The preamble, once the stream is open: the complete statements before
    /// the assignment that opens it, as they were sent (a `#!bovnar`
    /// directive included), and empty when there were none.
    pub fn preamble(&self) -> Option<&[u8]> {
        match self.state {
            State::Text => None,
            _ => Some(&self.text),
        }
    }

    /// The preamble parsed with [`crate::parse`] (or [`crate::parse_typed`]
    /// when `typed`), once the stream is open: a [`crate::Value::Struct`],
    /// empty when there was no preamble.
    pub fn preamble_value(&self, typed: bool) -> Result<Option<crate::Value>> {
        let Some(text) = self.preamble() else {
            return Ok(None);
        };
        if typed {
            crate::parse_typed(text).map(Some)
        } else {
            crate::parse(text).map(Some)
        }
    }

    /// Reads from the front of `input` up to the opening of the octet stream
    /// and returns the preamble, or `None` once every byte of `input` has been
    /// taken without reaching it. Call it until it returns the preamble to
    /// have the preamble before any message; once the stream is open it
    /// returns the preamble again and takes nothing. [`read`](Self::read)
    /// and [`next_item`](Self::next_item) read past the preamble on their own.
    pub fn read_preamble<'s>(&'s mut self, input: &mut &[u8]) -> Result<Option<&'s [u8]>> {
        if let Some(e) = &self.error {
            return Err(e.clone());
        }
        if self.state == State::Text {
            self.read_text(input)?;
        }
        Ok(self.preamble())
    }

    /// Whether the document is complete: the octet stream and the assignment
    /// are closed.
    pub fn is_done(&self) -> bool {
        matches!(self.state, State::Done | State::DoneComment) && self.error.is_none()
    }

    /// Checks that the input ended where a stream may end: after the
    /// document's `;`, or before its first byte (an empty input is an empty
    /// document, as [`crate::stream::mux_load`] reads it). Text without an
    /// octet stream is `got_incomplete_bvnr_stream`, even when it is a
    /// complete document: it is not a stream. Call it when the input has
    /// ended; the reader is unchanged.
    pub fn finish(&self) -> Result<()> {
        if let Some(e) = &self.error {
            return Err(e.clone());
        }
        let code = match self.state {
            State::Text if self.text.is_empty() => return Ok(()),
            // A comment ends at the end of input.
            State::Done | State::DoneComment => return Ok(()),
            State::Text | State::Semicolon | State::SemicolonComment => {
                ErrorCode::GotIncompleteBvnrStream
            }
            State::Tag | State::Len0 | State::Len1(_) | State::Channel | State::Fragment => {
                ErrorCode::ReadCompleteChunkFailed
            }
        };
        Err(Error::parse_code(code as i32))
    }

    /// Makes the reader ready for a new stream, keeping its options and the
    /// capacity of its message buffers.
    pub fn reset(&mut self) {
        let mut spare = std::mem::take(&mut self.spare);
        spare.extend(self.channels.drain(..).map(|c| c.buf));
        let channels = std::mem::take(&mut self.channels);
        let mut index = std::mem::take(&mut self.index);
        index.clear();
        *self = MuxReader {
            max_message: self.max_message,
            filter: self.filter.take(),
            max_preamble: self.max_preamble,
            key: std::mem::take(&mut self.key),
            text: std::mem::take(&mut self.text),
            head: std::mem::take(&mut self.head),
            wire: std::mem::take(&mut self.wire),
            channels,
            spare,
            index,
            ..Self::new()
        };
        self.key.clear();
        self.text.clear();
        self.head.clear();
    }

    /// Reads from the front of `input` and returns the next complete message,
    /// or `None` once every byte of `input` has been taken without completing
    /// one. See [Feeding a reader](crate::codec#feeding-a-reader).
    ///
    /// A message [`next_item`](Self::next_item) peeked at and that is still
    /// open is delivered here too, as its payload, unless it was skipped.
    #[inline]
    pub fn read<'s, 'a: 's>(&'s mut self, input: &mut &'a [u8]) -> Result<Option<Message<'s>>> {
        // The common case first, straight from the input: a chunk that holds
        // one whole message.
        let buf: &'a [u8] = input;
        if self.ready()
            && let Some(s) = self.start(buf)?
            && s.whole()
        {
            let (bytes, rest) = buf.split_at(s.end);
            *input = rest;
            return Ok(Some(Message {
                channel: s.channel,
                payload: &bytes[s.header..],
            }));
        }
        self.read_general(input)
    }

    fn read_general<'s, 'a: 's>(&'s mut self, input: &mut &'a [u8]) -> Result<Option<Message<'s>>> {
        loop {
            match self.advance(input, None)? {
                Found::Nothing => return Ok(None),
                Found::Message { channel, src } => {
                    return Ok(Some(Message {
                        channel,
                        payload: self.resolve(src),
                    }));
                }
                // A peek left over from next_item, or gathered for it: it is
                // delivered, read() never forwards.
                Found::Peek { .. } => {}
                Found::Wire { .. } => unreachable!("only next_item() forwards"),
            }
        }
    }

    /// Like [`read`](Self::read), but also stops at the start of each message
    /// with an [`Item::Peek`] holding its channel, its length and its first
    /// `head` bytes, so that a router can decide what becomes of it before it
    /// arrives. The decision ([`Peek::skip`], [`Peek::forward`], or nothing)
    /// takes effect on the next call; a skipped message is passed over
    /// without being gathered.
    ///
    /// A forwarded message comes out as an [`Item::Wire`]. If its chunks lie
    /// whole and consecutive in the input passed to the call after the peek,
    /// they are returned as they are, with no copy, as soon as that call
    /// starts.
    ///
    /// The call after a peek must be given the rest of the same input, as
    /// the peek left it: a peek may leave the input at the chunk it read the
    /// message's start from. Another slice is an [`Error::Argument`] when it
    /// is too short to be that rest.
    ///
    /// ```
    /// use bovnar::codec::{Item, MuxReader, MuxWriter};
    ///
    /// let mux = MuxWriter::new("mux")?;
    /// let mut stream = Vec::new();
    /// mux.begin(&mut stream);
    /// mux.write_message(&mut stream, 1, b"keep");
    /// mux.write_message(&mut stream, 2, b"drop");
    /// mux.finish(&mut stream);
    ///
    /// let mut reader = MuxReader::new();
    /// let mut out = Vec::new();
    /// mux.begin(&mut out);
    /// let mut input = &stream[..];
    /// while let Some(item) = reader.next_item(&mut input, 0)? {
    ///     match item {
    ///         Item::Peek(p) if p.channel == 1 => p.forward(),
    ///         Item::Peek(p) => p.skip(),
    ///         Item::Wire(w) => out.extend_from_slice(w.bytes),
    ///         Item::Message(_) => unreachable!("every message was decided"),
    ///     }
    /// }
    /// mux.finish(&mut out);
    /// assert_eq!(bovnar::stream::mux_load(&out, 0, None)?, [(1, b"keep".to_vec())]);
    /// # Ok::<(), bovnar::Error>(())
    /// ```
    #[inline]
    pub fn next_item<'s, 'a: 's>(
        &'s mut self,
        input: &mut &'a [u8],
        head: usize,
    ) -> Result<Option<Item<'s>>> {
        // The common case first, straight from the input: the message peeked
        // at last lies whole in it, and the next chunk starts a message.
        let mut buf: &'a [u8] = input;
        if let Pending::Peeked { fresh: Some(f) } = self.pending
            && let Some(n) = settled(buf, &f, self.decision)
        {
            let (bytes, rest) = buf.split_at(n);
            buf = rest;
            *input = rest;
            self.pending = Pending::None;
            match std::mem::take(&mut self.decision) {
                Decision::Skip => {}
                Decision::Deliver => {
                    return Ok(Some(Item::Message(Message {
                        channel: f.channel,
                        payload: &bytes[f.header..],
                    })));
                }
                Decision::Forward => {
                    return Ok(Some(Item::Wire(Wire {
                        channel: f.channel,
                        len: f.len,
                        bytes,
                    })));
                }
            }
        }
        if self.ready()
            && let Some(s) = self.start(buf)?
        {
            let head = self.begin_peek(s, buf, head);
            return Ok(Some(Item::Peek(Peek {
                channel: s.channel,
                len: s.len,
                head,
                decision: &mut self.decision,
            })));
        }
        self.next_item_general(input, head)
    }

    fn next_item_general<'s, 'a: 's>(
        &'s mut self,
        input: &mut &'a [u8],
        head: usize,
    ) -> Result<Option<Item<'s>>> {
        Ok(match self.advance(input, Some(head))? {
            Found::Nothing => None,
            Found::Message { channel, src } => Some(Item::Message(Message {
                channel,
                payload: self.resolve(src),
            })),
            Found::Wire { channel, len, src } => Some(Item::Wire(Wire {
                channel,
                len,
                bytes: self.resolve(src),
            })),
            Found::Peek { channel, len, src } => {
                let head = match src {
                    Src::Input(s) => s,
                    _ => &self.head[..],
                };
                Some(Item::Peek(Peek {
                    channel,
                    len,
                    head,
                    decision: &mut self.decision,
                }))
            }
        })
    }

    /// Whether the reader is between chunks with nothing to finish first.
    #[inline]
    fn ready(&self) -> bool {
        self.state == State::Tag && matches!(self.pending, Pending::None) && self.error.is_none()
    }

    /// The chunk at the front of `buf`, if it is all there and starts a
    /// message as [`Start`] describes. `None`, with nothing changed but the
    /// channel registered, sends any other chunk the general way, which also
    /// reports the errors.
    #[inline(always)]
    fn start(&mut self, buf: &[u8]) -> Result<Option<Start>> {
        let [0x01, lo, hi, rest @ ..] = buf else {
            return Ok(None);
        };
        let n = chunk_len(*lo, *hi);
        let Some(body) = rest.get(..n) else {
            return Ok(None);
        };
        let Ok(Some((channel, channel_len))) = varint_decode(body) else {
            return Ok(None);
        };
        let Ok(Some((len, length_len))) = varint_decode(&body[channel_len..]) else {
            return Ok(None);
        };
        let header = 3 + channel_len + length_len;
        let first = (n + 3 - header) as u64;
        if len < first || len > self.max_message || usize::try_from(len).is_err() {
            return Ok(None);
        }
        let cur = self.channel(channel)?;
        let c = &self.channels[cur];
        if c.active || c.varint_len > 0 {
            return Ok(None);
        }
        Ok(Some(Start {
            cur,
            channel,
            len,
            header,
            end: 3 + n,
        }))
    }

    /// Stops at the message `s` starts, leaving `buf` at its chunk's tag;
    /// returns its first bytes, up to `head`.
    #[inline]
    fn begin_peek<'a>(&mut self, s: Start, buf: &'a [u8], head: usize) -> &'a [u8] {
        self.cur = s.cur;
        self.pending = Pending::Peeked {
            fresh: Some(Fresh {
                channel: s.channel,
                len: s.len,
                header: s.header,
                end: s.end,
            }),
        };
        let first = &buf[s.header..s.end];
        &first[..head.min(first.len())]
    }

    fn resolve<'s, 'a: 's>(&'s self, src: Src<'a>) -> &'s [u8] {
        match src {
            Src::Input(s) => s,
            Src::Channel(i) => &self.channels[i].buf,
            Src::Head => &self.head,
            Src::Wire => &self.wire,
        }
    }

    fn fail(&mut self, code: ErrorCode) -> Error {
        let e = Error::parse_code(code as i32);
        self.error = Some(e.clone());
        e
    }

    /// The index of channel `id`, registering it on first sight.
    #[inline(always)]
    fn channel(&mut self, id: u64) -> Result<usize> {
        // Compared as u64: `id as usize` would alias ids modulo 2^32 on a
        // 32-bit target.
        if id < SMALL_CHANNELS as u64 {
            let slot = self.small[id as usize];
            if slot > 0 {
                return Ok(usize::from(slot) - 1);
            }
        }
        if let Some(c) = self.channels.get(self.last)
            && c.id == id
        {
            return Ok(self.last);
        }
        self.channel_indexed(id)
    }

    /// [`channel`](Self::channel) for an id it has not just seen and that
    /// is not small: out of line, so that the common case stays small enough
    /// to be inlined.
    #[inline(never)]
    fn channel_indexed(&mut self, id: u64) -> Result<usize> {
        let i = match self.index.get(&id) {
            Some(&i) => i as usize,
            None => {
                if self.channels.len() >= MAX_CHANNELS {
                    return Err(self.fail(ErrorCode::OctetStreamOutOfSync));
                }
                let i = self.channels.len();
                let mut buf = self.spare.pop().unwrap_or_default();
                buf.clear();
                self.channels.push(Channel::new(id, buf));
                if id < SMALL_CHANNELS as u64 {
                    // Below MAX_CHANNELS, so the index fits.
                    self.small[id as usize] = i as u16 + 1;
                } else {
                    self.index.insert(id, i as u32);
                }
                i
            }
        };
        self.last = i;
        Ok(i)
    }

    /// The octet stream closed: a channel still inside a message, or inside
    /// its length, is a truncated stream (`bvnr_demux_on_event`).
    fn close_octets(&mut self) -> Result<()> {
        let mut truncated = false;
        for c in &mut self.channels {
            truncated |= c.active || c.varint_len > 0;
            c.active = false;
            c.need = 0;
            c.have = 0;
            c.varint_len = 0;
        }
        if truncated {
            return Err(self.fail(ErrorCode::OctetStreamTruncated));
        }
        Ok(())
    }

    /// Reads text from the front of `input` up to the octet stream's opening
    /// `0x00`, and opens the stream there. Text cannot hold a NUL byte, so
    /// the first one is the opener.
    fn read_text(&mut self, input: &mut &[u8]) -> Result<()> {
        let buf = *input;
        let nul = buf.iter().position(|&b| b == 0);
        let n = nul.unwrap_or(buf.len());
        if self.text.len() + n > self.max_preamble {
            return Err(self.fail(ErrorCode::TextDataTooLong));
        }
        self.text.extend_from_slice(&buf[..n]);
        *input = &buf[(n + 1).min(buf.len())..];
        if nul.is_some() {
            self.open()?;
        }
        Ok(())
    }

    /// The text before the opener is complete: find the assignment it opens
    /// and check the preamble before it.
    fn open(&mut self) -> Result<()> {
        let Some((pre, key)) = split_opener(&self.text) else {
            return Err(self.fail(ErrorCode::UnexpectedInputByte));
        };
        let key_bytes = &self.text[key];
        if let Some(code) = key_error(key_bytes) {
            return Err(self.fail(code));
        }
        let plain = pre == 0 && matches!(&self.text[1 + key_bytes.len()..], b"=" | b" = ");
        let checked = if plain {
            Ok(())
        } else {
            check_document(&self.text, key_bytes)
        };
        let checked = checked.and_then(|()| match &self.filter {
            Some(f) if **f != *key_bytes => Err(Error::argument_with(
                ErrorCode::UnexpectedInputByte,
                format!(
                    "the multiplexed stream is under key {:?}, not {:?}",
                    String::from_utf8_lossy(key_bytes),
                    String::from_utf8_lossy(f)
                ),
            )),
            _ => Ok(()),
        });
        if let Err(e) = checked {
            self.error = Some(e.clone());
            return Err(e);
        }
        self.key.clear();
        self.key.extend_from_slice(key_bytes);
        self.text.truncate(pre);
        self.state = State::Tag;
        Ok(())
    }

    /// One byte after the octet stream: its `;`, with whitespace and
    /// comments before and after it, as the C lexer reads them.
    fn end_byte(&mut self, b: u8) -> Result<()> {
        use State as S;
        match (self.state, b) {
            (S::SemicolonComment, b'\n' | b'\r') => self.state = S::Semicolon,
            (S::DoneComment, b'\n' | b'\r') => self.state = S::Done,
            // A comment's body takes any byte but the control bytes.
            (S::SemicolonComment | S::DoneComment, 0x00..=0x08 | 0x0E..=0x1F | 0x7F) => {
                return Err(self.fail(ErrorCode::UnexpectedInputByte));
            }
            (S::SemicolonComment | S::DoneComment, _) => {}
            (S::Semicolon, b';') => self.state = S::Done,
            (S::Semicolon, b'#') => self.state = S::SemicolonComment,
            (S::Done, b'#') => self.state = S::DoneComment,
            (S::Semicolon | S::Done, b) if is_space(b) => {}
            _ => return Err(self.fail(ErrorCode::UnexpectedInputByte)),
        }
        Ok(())
    }

    /// Reads on until something is found or `input` is used up.
    fn advance<'a>(&mut self, input: &mut &'a [u8], peek: Option<usize>) -> Result<Found<'a>> {
        if let Some(e) = &self.error {
            return Err(e.clone());
        }
        if let Pending::Peeked { fresh } = self.pending {
            self.pending = Pending::None;
            let found = self.resume(input, fresh, peek.is_some())?;
            if !matches!(found, Found::Nothing) {
                return Ok(found);
            }
        }
        // The input from the current chunk's tag on, while the chunk's
        // header has been read from this input without a break.
        let mut tag: Option<&'a [u8]> = None;
        loop {
            let buf = *input;
            let Some(&b) = buf.first() else {
                return Ok(Found::Nothing);
            };
            match self.state {
                State::Tag => match b {
                    0x01 if buf.len() >= 3 => {
                        if let Some(s) = self.start(buf)? {
                            if let Some(head) = peek {
                                let head = self.begin_peek(s, buf, head);
                                return Ok(Found::Peek {
                                    channel: s.channel,
                                    len: s.len,
                                    src: Src::Input(head),
                                });
                            }
                            if s.whole() {
                                *input = &buf[s.end..];
                                return Ok(Found::Message {
                                    channel: s.channel,
                                    src: Src::Input(&buf[s.header..s.end]),
                                });
                            }
                        }
                        self.chunk_left = chunk_len(buf[1], buf[2]);
                        tag = Some(buf);
                        *input = &buf[3..];
                        self.state = State::Channel;
                    }
                    0x01 => {
                        *input = &buf[1..];
                        self.state = State::Len0;
                        tag = None;
                    }
                    0x00 => {
                        *input = &buf[1..];
                        self.state = State::Semicolon;
                        self.close_octets()?;
                    }
                    _ => return Err(self.fail(ErrorCode::OctetStreamOutOfSync)),
                },
                State::Len0 => {
                    *input = &buf[1..];
                    self.state = State::Len1(b);
                }
                State::Len1(lo) => {
                    *input = &buf[1..];
                    self.chunk_left = chunk_len(lo, b);
                    self.state = State::Channel;
                }
                State::Channel => {
                    let avail = &buf[..buf.len().min(self.chunk_left)];
                    let old = self.channel_varint_len;
                    let k = avail.len().min(MAX_VARINT - old);
                    let bytes = if old == 0 {
                        &avail[..k]
                    } else {
                        self.channel_varint[old..old + k].copy_from_slice(&avail[..k]);
                        &self.channel_varint[..old + k]
                    };
                    match varint_decode(bytes) {
                        Ok(Some((id, used))) => {
                            let eaten = used - old;
                            *input = &buf[eaten..];
                            self.chunk_left -= eaten;
                            self.channel_varint_len = 0;
                            self.cur = self.channel(id)?;
                            self.state = if self.chunk_left == 0 {
                                State::Tag
                            } else {
                                State::Fragment
                            };
                            if old > 0 {
                                tag = None;
                            }
                        }
                        Ok(None) if k < self.chunk_left => {
                            if old == 0 {
                                self.channel_varint[..k].copy_from_slice(&avail[..k]);
                            }
                            *input = &buf[k..];
                            self.chunk_left -= k;
                            self.channel_varint_len = old + k;
                            tag = None;
                        }
                        // The chunk ends inside the channel varint, or it
                        // runs over ten bytes or 64 bits.
                        _ => return Err(self.fail(ErrorCode::OctetStreamOutOfSync)),
                    }
                }
                State::Fragment => {
                    let piece = &buf[..buf.len().min(self.chunk_left)];
                    let (used, found) = self.fragment(piece, peek, tag.take())?;
                    match found {
                        FragFound::Fresh {
                            at,
                            channel,
                            len,
                            head,
                        } => {
                            // Leave the input at the chunk's tag: a forward
                            // can then hand the chunks on as they are.
                            let header = at.len() - (buf.len() - used);
                            *input = at;
                            self.state = State::Tag;
                            self.pending = Pending::Peeked {
                                fresh: Some(Fresh {
                                    channel,
                                    len,
                                    header,
                                    end: header + self.chunk_left - used,
                                }),
                            };
                            return Ok(Found::Peek {
                                channel,
                                len,
                                src: Src::Input(head),
                            });
                        }
                        FragFound::Found(found) => {
                            *input = &buf[used..];
                            self.chunk_left -= used;
                            if self.chunk_left == 0 {
                                self.state = State::Tag;
                            }
                            if !matches!(found, Found::Nothing) {
                                return Ok(found);
                            }
                        }
                    }
                }
                State::Text => self.read_text(input)?,
                State::Semicolon | State::SemicolonComment | State::Done | State::DoneComment => {
                    *input = &buf[1..];
                    self.end_byte(b)?;
                }
            }
        }
    }

    /// Reads `piece`, the next bytes of the current chunk's fragment, as
    /// `demux_feed` does: a length varint where no message is open, then
    /// message bytes, any number of messages. Returns how much of `piece` it
    /// took. `tag` is the input from the chunk's tag on when the header was
    /// read from it unbroken and `piece` starts the fragment.
    fn fragment<'a>(
        &mut self,
        piece: &'a [u8],
        peek: Option<usize>,
        tag: Option<&'a [u8]>,
    ) -> Result<(usize, FragFound<'a>)> {
        let cur = self.cur;
        let nothing = FragFound::Found(Found::Nothing);
        if let Pending::Gathering { target } = self.pending {
            // The rest of a peek's first bytes, all in this chunk.
            let k = (target - self.head.len()).min(piece.len());
            self.head.extend_from_slice(&piece[..k]);
            let c = &mut self.channels[cur];
            c.have += k as u64;
            if self.head.len() < target {
                return Ok((k, nothing));
            }
            self.pending = Pending::Peeked { fresh: None };
            let found = Found::Peek {
                channel: c.id,
                len: c.need,
                src: Src::Head,
            };
            return Ok((k, FragFound::Found(found)));
        }
        let mut pos = 0;
        loop {
            let c = &mut self.channels[cur];
            if !c.active {
                if pos == piece.len() {
                    return Ok((pos, nothing));
                }
                // The length varint, which may continue from earlier chunks:
                // gathered until a byte without the high bit, and more than
                // ten bytes are out of sync, as in demux_feed.
                let start = pos;
                let old = usize::from(c.varint_len);
                let room = MAX_VARINT - old;
                let avail = &piece[pos..];
                let Some(end) = avail.iter().take(room).position(|&b| b & 0x80 == 0) else {
                    if avail.len() > room {
                        return Err(self.fail(ErrorCode::OctetStreamOutOfSync));
                    }
                    c.varint[old..old + avail.len()].copy_from_slice(avail);
                    c.varint_len = (old + avail.len()) as u8;
                    return Ok((piece.len(), nothing));
                };
                let bytes = if old == 0 {
                    &avail[..=end]
                } else {
                    c.varint[old..=old + end].copy_from_slice(&avail[..=end]);
                    &c.varint[..=old + end]
                };
                let Ok(Some((need, _))) = varint_decode(bytes) else {
                    return Err(self.fail(ErrorCode::OctetStreamOutOfSync));
                };
                pos += end + 1;
                c.varint_len = 0;
                if need > self.max_message || usize::try_from(need).is_err() {
                    return Err(self.fail(ErrorCode::ValueOutOfRange));
                }
                // The first bytes come from this chunk only: another
                // channel's chunk may follow it.
                let target = peek.map(|head| head.min(need as usize).min(self.chunk_left - pos));
                let avail = piece.len() - pos;
                if let Some(target) = target
                    && avail >= target
                    && let Some(at) = tag
                    && start == 0
                    && old == 0
                {
                    // A fresh start: the channel stays idle (see Fresh).
                    let fresh = FragFound::Fresh {
                        at,
                        channel: c.id,
                        len: need,
                        head: &piece[pos..pos + target],
                    };
                    return Ok((pos, fresh));
                }
                c.active = true;
                c.need = need;
                c.have = 0;
                c.decision = Decision::Deliver;
                c.buf.clear();
                if let Some(target) = target {
                    if avail >= target {
                        self.pending = Pending::Peeked { fresh: None };
                        let found = Found::Peek {
                            channel: c.id,
                            len: need,
                            src: Src::Input(&piece[pos..pos + target]),
                        };
                        return Ok((pos, FragFound::Found(found)));
                    }
                    c.have = avail as u64;
                    self.head.clear();
                    self.head.extend_from_slice(&piece[pos..]);
                    self.pending = Pending::Gathering { target };
                    return Ok((piece.len(), nothing));
                }
            }
            let remaining = (c.need - c.have) as usize;
            let take = remaining.min(piece.len() - pos);
            let part = &piece[pos..pos + take];
            pos += take;
            if c.decision == Decision::Skip {
                c.have += take as u64;
                c.active = c.have != c.need;
            } else {
                let whole = c.have == 0 && take == remaining;
                if !whole {
                    c.buf.extend_from_slice(part);
                }
                c.have += take as u64;
                if c.have == c.need {
                    c.active = false;
                    let decision = c.decision;
                    let src = if whole {
                        Src::Input(part)
                    } else {
                        Src::Channel(cur)
                    };
                    let found = self.complete(cur, decision, src, peek.is_some());
                    return Ok((pos, FragFound::Found(found)));
                }
            }
            if pos == piece.len() {
                return Ok((pos, nothing));
            }
        }
    }

    /// A message is complete: a [`Found::Wire`] when it is forwarded through
    /// [`MuxReader::next_item`], a [`Found::Message`] otherwise.
    fn complete<'a>(
        &mut self,
        cur: usize,
        decision: Decision,
        src: Src<'a>,
        wire: bool,
    ) -> Found<'a> {
        let c = &self.channels[cur];
        let (channel, len) = (c.id, c.need);
        if decision == Decision::Forward && wire {
            let payload = match src {
                Src::Input(s) => s,
                _ => &c.buf[..],
            };
            self.wire.clear();
            encode_into(&mut self.wire, channel, payload);
            return Found::Wire {
                channel,
                len,
                src: Src::Wire,
            };
        }
        Found::Message { channel, src }
    }

    /// Applies the decision on the message last peeked at, and completes it
    /// if its first bytes were all of it.
    fn resume<'a>(
        &mut self,
        input: &mut &'a [u8],
        fresh: Option<Fresh>,
        wire: bool,
    ) -> Result<Found<'a>> {
        let mut decision = std::mem::take(&mut self.decision);
        if decision == Decision::Forward && !wire {
            decision = Decision::Deliver;
        }
        let cur = self.cur;
        if let Some(f) = fresh {
            // A peek read the chunk's header from the input and left the input
            // at its tag; a call with a shorter slice did not pass that input
            // back. Refused (and latched: the peek has been used up) rather
            // than sliced out of bounds.
            if input.len() < f.header {
                let e = Error::argument(
                    "after a peek, the next call must be given the rest of the same input",
                );
                self.error = Some(e.clone());
                return Err(e);
            }
            // The input is still at the message's first chunk: when the
            // message is all there, it is handed over or passed over at once.
            if let Some(n) = settled(input, &f, decision) {
                let (bytes, rest) = input.split_at(n);
                *input = rest;
                return Ok(match decision {
                    Decision::Deliver => Found::Message {
                        channel: f.channel,
                        src: Src::Input(&bytes[f.header..]),
                    },
                    Decision::Skip => Found::Nothing,
                    Decision::Forward => Found::Wire {
                        channel: f.channel,
                        len: f.len,
                        src: Src::Input(bytes),
                    },
                });
            }
            // It is not: the channel is inside it from here on.
            let c = &mut self.channels[cur];
            c.active = true;
            c.need = f.len;
            c.have = 0;
            c.buf.clear();
            *input = &input[f.header..];
            self.chunk_left = f.end - f.header;
            self.state = if self.chunk_left == 0 {
                State::Tag
            } else {
                State::Fragment
            };
        }
        let c = &mut self.channels[cur];
        c.decision = decision;
        if decision != Decision::Skip {
            c.buf.extend_from_slice(&self.head);
        }
        self.head.clear();
        if c.have != c.need {
            return Ok(Found::Nothing);
        }
        c.active = false;
        Ok(match decision {
            Decision::Skip => Found::Nothing,
            _ => self.complete(cur, decision, Src::Channel(cur), wire),
        })
    }
}

/// What [`MuxReader::fragment`] found.
enum FragFound<'a> {
    /// A message starts the chunk whose tag is at `at`.
    Fresh {
        at: &'a [u8],
        channel: u64,
        len: u64,
        head: &'a [u8],
    },
    Found(Found<'a>),
}

/// Splits the text before an octet stream's opening `0x00` at the
/// assignment the stream is the value of: `PREAMBLE .KEY = `, with any
/// whitespace around the `=`. Returns where the preamble ends (at the `.`)
/// and where the key lies.
fn split_opener(text: &[u8]) -> Option<(usize, std::ops::Range<usize>)> {
    let space = is_space;
    let mut end = text.len();
    while end > 0 && space(text[end - 1]) {
        end -= 1;
    }
    end = end.checked_sub(1).filter(|&e| text[e] == b'=')?;
    while end > 0 && space(text[end - 1]) {
        end -= 1;
    }
    let key_end = end;
    while end > 0 && key_body(text[end - 1]) {
        end -= 1;
    }
    let dot = end
        .checked_sub(1)
        .filter(|&d| text[d] == b'.' && end < key_end)?;
    Some((dot, end..key_end))
}

/// Checks with the full parser that `text`, closed by an empty octet stream,
/// is a document whose last assignment is that octet stream under `key`. A
/// preamble ending inside a comment, or holding anything but complete
/// statements, fails here.
fn check_document(text: &[u8], key: &[u8]) -> Result<()> {
    let mut doc = Vec::with_capacity(text.len() + TERMINATOR.len() + 1);
    doc.extend_from_slice(text);
    doc.push(0);
    doc.extend_from_slice(TERMINATOR);
    if let crate::Value::Struct(entries) = crate::parse(&doc)?
        && let Some((k, crate::Value::Bytes(b))) = entries.last()
        && k.as_bytes() == key
        && b.is_empty()
    {
        return Ok(());
    }
    Err(Error::argument_with(
        ErrorCode::UnexpectedInputByte,
        "the preamble must be complete statements: the assignment that opens the \
         stream does not follow it (does the preamble end inside a comment?)",
    ))
}

/// Bovnar whitespace (`ws-char`): HT, LF, VT, FF, CR and space.
fn is_space(b: u8) -> bool {
    matches!(b, b'\t' | b'\n' | 0x0B | 0x0C | b'\r' | b' ')
}

fn chunk_len(lo: u8, hi: u8) -> usize {
    match u16::from_le_bytes([lo, hi]) {
        0 => CHUNK_SIZE,
        n => usize::from(n),
    }
}

/// The wire bytes at the front of `input` of the message `f` peeked at, when
/// they are all there and `decision` takes them as they are: one chunk that
/// holds all of it, or, unless it is delivered (and so gathered), the run of
/// chunks [`raw_span`] finds.
#[inline]
fn settled(input: &[u8], f: &Fresh, decision: Decision) -> Option<usize> {
    let first = f.end - f.header;
    if f.len == first as u64 {
        (input.len() >= f.end).then_some(f.end)
    } else if decision == Decision::Deliver {
        None
    } else {
        raw_span(input, f.end, first, f.channel, f.len)
    }
}

/// The length of the chunks at the front of `buf` that carry one message and
/// nothing else, if they are all there: the first chunk (`first` bytes on the
/// wire, `first_payload` of them the message's) and as many following chunks
/// of channel `id` as the rest of its `need` bytes takes, each ending inside
/// the message or at its end.
fn raw_span(buf: &[u8], first: usize, first_payload: usize, id: u64, need: u64) -> Option<usize> {
    let mut left = need.checked_sub(first_payload as u64)?;
    let mut end = first;
    if end > buf.len() {
        return None;
    }
    while left > 0 {
        let h = buf.get(end..end + 3)?;
        if h[0] != 0x01 {
            return None;
        }
        let n = chunk_len(h[1], h[2]);
        let body = buf.get(end + 3..end + 3 + n)?;
        let (channel, used) = varint_decode(body).ok()??;
        let frag = (n - used) as u64;
        if channel != id || frag > left {
            return None;
        }
        left -= frag;
        end += 3 + n;
    }
    Some(end)
}

// ---------------------------------------------------------------------------
// Frames
// ---------------------------------------------------------------------------

/// The 12-byte header `bvnr_frame_write` puts before a document of
/// `doc_len` bytes: `"BVF1"` and the length as a little-endian `u64`.
pub fn frame_header(doc_len: u64) -> [u8; FRAME_HEADER_SIZE] {
    let mut h = [0; FRAME_HEADER_SIZE];
    h[..4].copy_from_slice(&FRAME_MAGIC);
    h[4..].copy_from_slice(&doc_len.to_le_bytes());
    h
}

/// Appends one framed document to `out`, as `bvnr_frame_write` writes it
/// (and [`crate::stream::frame_documents`] for each document).
pub fn write_frame(out: &mut Vec<u8>, doc: &[u8]) {
    out.reserve(FRAME_HEADER_SIZE + doc.len());
    out.extend_from_slice(&frame_header(doc.len() as u64));
    out.extend_from_slice(doc);
}

/// Writes one framed document to `out` in one vectored write, without
/// copying it. `out` is not flushed. A failing `out` is an [`Error::Io`]
/// with `writing_to_sink`.
pub fn write_frame_to<W: Write + ?Sized>(out: &mut W, doc: &[u8]) -> Result<()> {
    let header = frame_header(doc.len() as u64);
    let mut bufs = [IoSlice::new(&header), IoSlice::new(doc)];
    write_all_vectored(out, &mut bufs).map_err(|e| sink_error(&e))
}

/// Reads `BVF1` frames fed in pieces of any size and returns each complete
/// document's bytes: the push-fed counterpart of `bvnr_doc_stream_read`'s
/// framing. Parse each document with [`crate::parse`], a [`crate::Reader`] or
/// a [`crate::DomDoc`].
///
/// A document that lies whole in the input is returned from it; one that
/// arrives over several calls is gathered in a buffer that grows with the
/// bytes received (never ahead of them, so a forged length cannot force an
/// allocation) and is reused.
///
/// | Condition | [`ErrorCode`] |
/// |---|---|
/// | A header without the `BVF1` magic | `UnexpectedInputByte` |
/// | A length over `max_document_size` (default 256 MiB), or over `usize` | `ValueOutOfRange` |
/// | The input ends inside a header or a document ([`FrameReader::finish`]) | `GotIncompleteBvnrStream` |
///
/// `bvnr_doc_stream_read` reports all three as a failed read, which
/// [`crate::stream::load_documents`] gives as `GotIncompleteBvnrStream`. An
/// error is latched: every later call returns it again.
///
/// ```
/// use bovnar::codec::FrameReader;
///
/// let stream = bovnar::stream::frame_documents(&[".a = 1;", ".b = 2;"])?;
/// let mut reader = FrameReader::new();
/// let mut docs = Vec::new();
/// for piece in stream.chunks(5) {
///     let mut input = piece;
///     while let Some(doc) = reader.read(&mut input)? {
///         docs.push(bovnar::parse(doc)?);
///     }
/// }
/// reader.finish()?;
/// assert_eq!(docs, [bovnar::parse(b".a = 1;")?, bovnar::parse(b".b = 2;")?]);
/// # Ok::<(), bovnar::Error>(())
/// ```
#[derive(Debug)]
pub struct FrameReader {
    max_document_size: u64,
    header: [u8; FRAME_HEADER_SIZE],
    header_len: usize,
    /// The length of the document being gathered, once its header is read.
    need: Option<usize>,
    buf: Vec<u8>,
    error: Option<Error>,
}

impl Default for FrameReader {
    fn default() -> Self {
        Self::new()
    }
}

impl FrameReader {
    /// A reader with the C default cap,
    /// [`crate::stream::BVNR_DOC_DEFAULT_MAX`] (256 MiB) per document.
    pub fn new() -> Self {
        Self::with_max_document_size(0)
    }

    /// A reader that refuses a document over `max_document_size` bytes; 0
    /// is the default cap, and `u64::MAX` lifts it, as in
    /// `bvnr_doc_stream_opts_t`.
    pub fn with_max_document_size(max_document_size: u64) -> Self {
        FrameReader {
            max_document_size: if max_document_size == 0 {
                BVNR_DOC_DEFAULT_MAX
            } else {
                max_document_size
            },
            header: [0; FRAME_HEADER_SIZE],
            header_len: 0,
            need: None,
            buf: Vec::new(),
            error: None,
        }
    }

    /// Checks that the input ended on a frame boundary, as
    /// `bvnr_doc_stream_read` requires of its end. The reader is unchanged.
    pub fn finish(&self) -> Result<()> {
        if let Some(e) = &self.error {
            return Err(e.clone());
        }
        if self.need.is_some() || self.header_len > 0 {
            return Err(Error::parse_code(ErrorCode::GotIncompleteBvnrStream as i32));
        }
        Ok(())
    }

    /// Makes the reader ready for a new stream, keeping its cap and its
    /// buffer's capacity.
    pub fn reset(&mut self) {
        self.header_len = 0;
        self.need = None;
        self.buf.clear();
        self.error = None;
    }

    fn fail(&mut self, code: ErrorCode) -> Error {
        let e = Error::parse_code(code as i32);
        self.error = Some(e.clone());
        e
    }

    /// Reads from the front of `input` and returns the next complete
    /// document, or `None` once every byte of `input` has been taken without
    /// completing one. See [Feeding a reader](crate::codec#feeding-a-reader).
    pub fn read<'s, 'a: 's>(&'s mut self, input: &mut &'a [u8]) -> Result<Option<&'s [u8]>> {
        if let Some(e) = &self.error {
            return Err(e.clone());
        }
        let need = match self.need {
            Some(need) => need,
            None => {
                let k = (FRAME_HEADER_SIZE - self.header_len).min(input.len());
                self.header[self.header_len..self.header_len + k].copy_from_slice(&input[..k]);
                self.header_len += k;
                *input = &input[k..];
                if self.header_len < FRAME_HEADER_SIZE {
                    return Ok(None);
                }
                self.header_len = 0;
                if self.header[..4] != FRAME_MAGIC {
                    return Err(self.fail(ErrorCode::UnexpectedInputByte));
                }
                let mut len = [0; 8];
                len.copy_from_slice(&self.header[4..]);
                let len = u64::from_le_bytes(len);
                let Some(need) = usize::try_from(len)
                    .ok()
                    .filter(|_| len <= self.max_document_size)
                else {
                    return Err(self.fail(ErrorCode::ValueOutOfRange));
                };
                if input.len() >= need {
                    let (doc, rest) = input.split_at(need);
                    *input = rest;
                    return Ok(Some(doc));
                }
                self.buf.clear();
                self.need = Some(need);
                need
            }
        };
        let k = (need - self.buf.len()).min(input.len());
        self.buf.extend_from_slice(&input[..k]);
        *input = &input[k..];
        if self.buf.len() < need {
            return Ok(None);
        }
        self.need = None;
        Ok(Some(&self.buf))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn varints_round_trip_and_refuse_overlong() {
        for v in [
            0,
            1,
            127,
            128,
            300,
            (1 << 35) - 1,
            1 << 35,
            1 << 55,
            u64::MAX - 1,
            u64::MAX,
        ] {
            let mut buf = [0; MAX_VARINT];
            let n = varint_encode(v, &mut buf);
            assert_eq!(varint_len(v), n, "{v}");
            if n <= 8 {
                assert_eq!(varint_bits(v).to_le_bytes()[..n], buf[..n], "{v}");
                assert_eq!(varint_bits(v) >> (8 * n - 1) >> 1, 0, "{v}");
            }
            assert_eq!(varint_decode(&buf[..n]), Ok(Some((v, n))), "{v}");
            assert_eq!(varint_decode(&buf[..n - 1]), Ok(None), "{v}");
        }
        // The tenth byte may carry only bit 63.
        let mut over = [0xFF; MAX_VARINT];
        over[9] = 0x02;
        assert_eq!(varint_decode(&over), Err(()));
        assert_eq!(varint_decode(&[0x80; 11]), Err(()));
    }

    #[test]
    fn cut_matches_bvnr_mux_send() {
        // Channel 1, a length of three varint bytes: 65532 bytes in the
        // first chunk, then 65535 per chunk.
        let cut = Cut::new(1, 131_072);
        assert_eq!((cut.take(0), cut.take(65_532)), (65_532, 65_535));
        assert_eq!(cut.chunks(), 3);
        assert_eq!(Cut::new(1, 0).chunks(), 1);
        assert_eq!(Cut::new(1, 65_532).chunks(), 1);
        assert_eq!(Cut::new(1, 65_533).chunks(), 2);
    }

    #[test]
    fn parts_alternate_headers_and_payload() {
        let mux = MuxWriter::new("mux").unwrap();
        let mut head = Vec::new();
        let payload = vec![7u8; 70_000];
        let parts = mux.encode(&mut head, 5, &payload);
        assert_eq!(parts.len(), 4);
        let lens: Vec<usize> = parts.map(<[u8]>::len).collect();
        assert_eq!(lens, [3 + 1 + 3, 65_532, 3 + 1, 70_000 - 65_532]);
        assert_eq!(mux.encode(&mut head, 5, b"").len(), 1);
    }

    /// One C writer session: values first, then `bvnr_mux_begin`,
    /// `bvnr_mux_send` and `bvnr_mux_end` on the same writer.
    #[test]
    fn reads_a_c_session_with_values_before_the_stream() {
        let zeros = [0u8; 70_000];
        let msgs: [(u64, &[u8]); 2] = [(1, b"frame"), (2, &zeros)];
        for pretty in [false, true] {
            let doc = crate::writer::grow_write(80_000, pretty, |w| {
                w.write_version(1, 2)?;
                w.begin_struct("meta")?;
                w.write_string("codec", "av1")?;
                w.end_struct()?;
                w.write_bool("live", true)?;
                w.check(|h| h.mux("media", &msgs))
            })
            .unwrap();
            let mut reader = MuxReader::with_options(0, Some("media")).unwrap();
            let mut got = Vec::new();
            for piece in doc.chunks(1000) {
                let mut input = piece;
                while let Some(m) = reader.read(&mut input).unwrap() {
                    got.push((m.channel, m.payload.to_vec()));
                }
            }
            reader.finish().unwrap();
            assert_eq!(got, msgs.map(|(c, p)| (c, p.to_vec())), "pretty {pretty}");
            let Some(crate::Value::Struct(fields)) = reader.preamble_value(false).unwrap() else {
                panic!("a preamble struct")
            };
            let keys: Vec<&str> = fields.iter().map(|(k, _)| k.as_str()).collect();
            assert_eq!(keys, ["meta", "live"], "pretty {pretty}");
            assert!(reader.preamble().unwrap().starts_with(b"#!bovnar 1.2"));
        }
    }

    #[test]
    fn keys_follow_the_identifier_rules() {
        for ok in ["mux", "_", "a-b+c", "größe", "x1"] {
            assert!(MuxWriter::new(ok).is_ok(), "{ok}");
        }
        for (bad, code) in [
            ("", ErrorCode::EmptyIdentifier),
            ("1a", ErrorCode::UnexpectedInputByte),
            ("a b", ErrorCode::UnexpectedInputByte),
            ("a.b", ErrorCode::UnexpectedInputByte),
            ("a\u{a0}", ErrorCode::UnexpectedInputByte),
        ] {
            assert_eq!(
                MuxWriter::new(bad).unwrap_err().kind(),
                Some(code),
                "{bad:?}"
            );
        }
        let long = "k".repeat(MAX_KEY_LEN + 1);
        assert_eq!(
            MuxWriter::new(&long).unwrap_err().kind(),
            Some(ErrorCode::IdentifierTooLong)
        );
    }
}
