// SPDX-License-Identifier: MIT
//! Document streams: framing several
//! documents into one byte stream, multiplexing binary messages over
//! channels, and embedding one document inside another.
//!
//! These work on whole streams: they return everything at once, and suit a
//! file or a finite transfer. For a stream that never ends, see
//! [`crate::codec`] and [`crate::live`].
//!
//! Each function has a twin over [`std::io::Read`] (`*_reader`) or
//! [`std::io::Write`] (`*_to`) that streams through the library's callback
//! source and sink instead of going through memory. A reader twin parses as
//! it reads, with no total cap (`max_file_size`
//! [`BVNR_FILESIZE_UNLIMITED`]) and the in-memory twin's
//! `max_document_size` and `max_message`, and returns what its in-memory
//! twin returns for the same bytes; it reads until its input reports its end.
//! A writer twin writes as it serialises and flushes at the end: a document
//! writer ([`mux_dump_to`], [`embed_document_to`]) in 64 KiB blocks, a frame
//! writer ([`frame_documents_to`], [`dump_documents_to`]) one write for each
//! frame header and one for each payload, so give it a
//! [`std::io::BufWriter`] for many small frames on an unbuffered file or
//! socket. Its output is byte for byte its in-memory twin's, but a failure
//! part-way leaves what was already written. A failing reader or writer is
//! an [`Error::Io`] with `reading_from_source_fd` or `writing_to_sink`, also
//! when the bytes before the failure would already have failed to parse;
//! short reads and writes are continued, `Interrupted` is retried, and a
//! panic in either reaches the caller. `WouldBlock` is not retried: it is a
//! failure like any other, so a non-blocking input belongs in
//! [`crate::live`].

use std::cell::RefCell;

use crate::dict_parser::DictParser;
use crate::enums::ErrorCode;
use crate::error::{Error, Position, Result};
use crate::ffi::{self, BvnrReadFlags, BvnrSource, DemuxError, Handlers, ReaderHandle};
use crate::io;
use crate::reader::{EventData, wrap};
use crate::writer;
use crate::{Value, to_string};

/// `max_file_size` for no limit.
pub const BVNR_FILESIZE_UNLIMITED: u64 = 0;
/// Default per-document cap of [`load_documents`].
pub const BVNR_DOC_DEFAULT_MAX: u64 = 256 * 1024 * 1024;
/// Default per-message cap of [`mux_load`].
pub const BVNR_MUX_DEFAULT_MAX: u64 = 64 * 1024 * 1024;
/// Largest payload a single mux frame carries.
pub const BVNR_MUX_MAX_MESSAGE: u64 = 65536 - 20;

fn read_flags() -> BvnrReadFlags {
    let mut flags = BvnrReadFlags::zeroed();
    flags.max_file_size = BVNR_FILESIZE_UNLIMITED;
    flags.max_struct_nesting = 255;
    flags.max_array_nesting = 255;
    flags
}

fn parse_error(code: i32) -> Error {
    Error::parse_code(code)
}

/// Frames already-serialised documents into one stream.
pub fn frame_documents<D: AsRef<[u8]>>(docs: &[D]) -> Result<Vec<u8>> {
    let docs: Vec<&[u8]> = docs.iter().map(AsRef::as_ref).collect();
    ffi::frame_documents(&docs).ok_or_else(|| Error::argument("bvnr_frame_write failed"))
}

/// [`frame_documents`] into any [`std::io::Write`], one frame at a time.
pub fn frame_documents_to<D: AsRef<[u8]>>(docs: &[D], mut out: impl std::io::Write) -> Result<()> {
    docs.iter()
        .try_for_each(|d| ffi::frame_write_to(d.as_ref(), &mut out))
        .and_then(|()| out.flush())
        .map_err(|e| Error::io(ErrorCode::WritingToSink, &e))
}

/// Serialises each value with [`to_string`] and frames the results.
pub fn dump_documents(values: &[Value], pretty: bool) -> Result<Vec<u8>> {
    let docs = values
        .iter()
        .map(|v| to_string(v, pretty))
        .collect::<Result<Vec<_>>>()?;
    frame_documents(&docs)
}

/// [`dump_documents`] into any [`std::io::Write`]: each document is
/// serialised and framed in turn, so only one is in memory at a time.
pub fn dump_documents_to(
    values: &[Value],
    pretty: bool,
    mut out: impl std::io::Write,
) -> Result<()> {
    let io_error = |e: std::io::Error| Error::io(ErrorCode::WritingToSink, &e);
    for v in values {
        ffi::frame_write_to(&to_string(v, pretty)?, &mut out).map_err(io_error)?;
    }
    out.flush().map_err(io_error)
}

/// Parses a framed stream, one value per document (typed like
/// [`crate::parse_typed`] when `typed`). With `continue_past_failed`, a
/// document that fails is `None` and the rest are still read; otherwise the
/// first failure is the error. `max_document_size` 0 is the default cap.
pub fn load_documents(
    data: &[u8],
    typed: bool,
    continue_past_failed: bool,
    max_document_size: u64,
) -> Result<Vec<Option<Value>>> {
    ffi::with_mem_source(data, |src| {
        load_from(src, typed, continue_past_failed, max_document_size)
    })
}

fn load_from(
    src: &mut BvnrSource,
    typed: bool,
    continue_past_failed: bool,
    max_document_size: u64,
) -> Result<Vec<Option<Value>>> {
    let parser = RefCell::new(DictParser::new(typed));
    let mut results = Vec::new();
    let mut on_event = |d: &EventData<'_>| {
        parser.borrow_mut().on_event(d);
        true
    };
    let mut verified = wrap(&mut on_event);
    let mut on_document = |_index: u64, ok: bool, _err: i32| {
        let done = parser.replace(DictParser::new(typed));
        results.push(ok.then(|| done.result()));
        true
    };
    let mut h = ffi::Handlers {
        verified: Some(&mut verified),
        on_document: Some(&mut on_document),
        ..ffi::Handlers::default()
    };
    let (ok, _, error) = ffi::doc_stream_read(
        src,
        read_flags(),
        continue_past_failed,
        max_document_size,
        &mut h,
    );
    if !ok {
        // Why the stream ended, with the codes live::FrameParser gives: a
        // failed document's own code (without continue_past_failed), or the
        // framing's.
        return Err(parse_error(error));
    }
    Ok(results)
}

/// [`load_documents`] over any [`std::io::Read`], such as a
/// [`std::fs::File`]. It returns once `input` has ended, so it suits a finite
/// stream; for an endless one, feed a [`crate::live::FrameParser`] or a
/// [`crate::live::FramePull`].
pub fn load_documents_reader(
    mut input: impl std::io::Read,
    typed: bool,
    continue_past_failed: bool,
    max_document_size: u64,
) -> Result<Vec<Option<Value>>> {
    io::from_reader(&mut input, |src| {
        load_from(src, typed, continue_past_failed, max_document_size)
    })
}

/// Multiplexes `(channel, payload)` messages into one document under `key`
/// (conventionally `"mux"`).
pub fn mux_dump<P: AsRef<[u8]>>(messages: &[(u64, P)], key: &str) -> Result<Vec<u8>> {
    let msgs = borrowed(messages);
    let total: usize = msgs.iter().map(|(_, p)| p.len()).sum();
    writer::grow_write(total + msgs.len() * 64 + key.len(), false, |w| {
        w.check(|h| h.mux(key, &msgs))
    })
}

/// [`mux_dump`] into any [`std::io::Write`].
pub fn mux_dump_to<P: AsRef<[u8]>>(
    messages: &[(u64, P)],
    key: &str,
    out: impl std::io::Write,
) -> Result<()> {
    let msgs = borrowed(messages);
    writer::stream_to(out, false, |w| w.check(|h| h.mux(key, &msgs)))
}

fn borrowed<P: AsRef<[u8]>>(messages: &[(u64, P)]) -> Vec<(u64, &[u8])> {
    messages.iter().map(|(c, p)| (*c, p.as_ref())).collect()
}

/// The `(channel, payload)` messages of a multiplexed document, in order.
/// `max_message` 0 is the default cap; `key` `None` accepts any.
pub fn mux_load(data: &[u8], max_message: u64, key: Option<&str>) -> Result<Vec<(u64, Vec<u8>)>> {
    mux_load_with(|r, f, h| r.read_mem(data, f, h), max_message, key)
}

/// [`mux_load`] of the document `read` reads.
fn mux_load_with(
    read: impl FnOnce(&mut ReaderHandle, BvnrReadFlags, &mut Handlers<'_>) -> bool,
    max_message: u64,
    key: Option<&str>,
) -> Result<Vec<(u64, Vec<u8>)>> {
    let key = key
        .map(std::ffi::CString::new)
        .transpose()
        .map_err(|_| Error::argument("mux key contains a NUL byte"))?;
    ffi::demux_read(read, read_flags(), max_message, key.as_deref()).map_err(|e| match e {
        DemuxError::Demux(code) => parse_error(code),
        DemuxError::Read(r) => Error::Parse {
            code: r.code,
            position: Some(Position {
                line: r.line,
                column: r.column,
                offset: r.offset,
                byte: r.byte,
            }),
        },
        DemuxError::OutOfMemory(call) => Error::OutOfMemory(call),
    })
}

/// [`mux_load`] over any [`std::io::Read`]. It returns once `input` has
/// ended; for messages as they arrive, feed a [`crate::live::Demux`].
pub fn mux_load_reader(
    mut input: impl std::io::Read,
    max_message: u64,
    key: Option<&str>,
) -> Result<Vec<(u64, Vec<u8>)>> {
    io::from_reader(&mut input, |src| {
        mux_load_with(|r, f, h| r.read_source(src, f, h), max_message, key)
    })
}

/// Embeds the document `inner` under `key` (conventionally `"payload"`)
/// in a new document.
pub fn embed_document(inner: &[u8], key: &str, pretty: bool) -> Result<Vec<u8>> {
    writer::grow_write(inner.len() + key.len(), pretty, |w| {
        w.check(|h| h.embed_document(key, inner))
    })
}

/// [`embed_document`] into any [`std::io::Write`].
pub fn embed_document_to(
    inner: &[u8],
    key: &str,
    pretty: bool,
    out: impl std::io::Write,
) -> Result<()> {
    writer::stream_to(out, pretty, |w| w.check(|h| h.embed_document(key, inner)))
}

/// Parses an embedded document (the payload [`embed_document`] wrote).
pub fn parse_embedded(inner: &[u8], typed: bool) -> Result<Value> {
    parse_embedded_with(typed, |flags, h| Ok(ffi::parse_embedded(inner, flags, h)))
}

/// [`parse_embedded`] of the document `read` reads with the flags and
/// handlers it is given.
fn parse_embedded_with(
    typed: bool,
    read: impl FnOnce(BvnrReadFlags, &mut Handlers<'_>) -> Result<bool>,
) -> Result<Value> {
    let mut parser = DictParser::new(typed);
    let (ok, first_error) = {
        let mut on_event = |d: &EventData<'_>| {
            parser.on_event(d);
            true
        };
        let mut verified = wrap(&mut on_event);
        let mut h = ffi::Handlers {
            verified: Some(&mut verified),
            ..ffi::Handlers::default()
        };
        (read(read_flags(), &mut h)?, h.first_error)
    };
    if !ok {
        return Err(match first_error {
            Some((code, line, column, byte, offset)) => Error::Parse {
                code,
                position: Some(Position {
                    line,
                    column,
                    offset,
                    byte,
                }),
            },
            None => parse_error(ErrorCode::GotIncompleteBvnrStream as i32),
        });
    }
    Ok(parser.result())
}

/// [`parse_embedded`] over any [`std::io::Read`], parsed as it is read.
pub fn parse_embedded_reader(mut input: impl std::io::Read, typed: bool) -> Result<Value> {
    parse_embedded_with(typed, |flags, h| {
        let mut reader = ReaderHandle::new().ok_or(Error::OutOfMemory("bvnr_reader_create()"))?;
        io::from_reader(&mut input, |src| Ok(reader.read_source(src, flags, h)))
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::parse;

    fn doc(n: i128) -> Value {
        Value::Struct(vec![("n".into(), Value::Int(n))])
    }

    #[test]
    fn frames_and_loads_documents() {
        let stream = dump_documents(&[doc(1), doc(2)], false).unwrap();
        let docs = load_documents(&stream, false, false, 0).unwrap();
        assert_eq!(docs, vec![Some(doc(1)), Some(doc(2))]);

        let bad = frame_documents(&[&b".n = 1;"[..], b".n = [1;", b".n = 3;"]).unwrap();
        let r = load_documents(&bad, false, false, 0);
        assert!(r.is_err(), "{r:?}");
        let docs = load_documents(&bad, false, true, 0).unwrap();
        assert_eq!(docs, vec![Some(doc(1)), None, Some(doc(3))]);
    }

    #[test]
    fn mux_round_trips() {
        let msgs = vec![
            (0u64, b"hello".to_vec()),
            (7, vec![0, 255]),
            (0, Vec::new()),
        ];
        let text = mux_dump(&msgs, "mux").unwrap();
        assert!(parse(&text).is_ok());
        assert_eq!(mux_load(&text, 0, None).unwrap(), msgs);
        assert_eq!(mux_load(&text, 0, Some("mux")).unwrap(), msgs);
    }

    #[test]
    fn embeds_and_parses_back() {
        let inner = b".a = 1; .b = \"x\";";
        let outer = embed_document(inner, "payload", true).unwrap();
        let Value::Struct(entries) = parse(&outer).unwrap() else {
            unreachable!()
        };
        let Value::Bytes(payload) = &entries[0].1 else {
            panic!("{entries:?}")
        };
        let back = parse_embedded(payload, false).unwrap();
        assert_eq!(back, parse(inner).unwrap());
        assert!(parse_embedded(b".a = ", false).is_err());
    }
}
