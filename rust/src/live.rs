// SPDX-License-Identifier: MIT
//! Live documents: a document, a frame stream or a multiplexed stream that
//! arrives in pieces, read through the C library's push-fed readers
//! (doc/10 §8).
//!
//! [`crate::Reader`] and [`crate::stream`] read a document that is there,
//! in memory or behind a [`std::io::Read`] that blocks until it has more. The
//! types here are *handed* bytes instead, any number of times and in pieces of
//! any size, and are told separately when the input is over. That suits a
//! non-blocking socket, an event loop, a frame of a frame stream, or an octet
//! stream of another document. Each keeps one C object for its whole life, so
//! a stream that never ends costs one setup.
//!
//! | Type | Over | What comes out |
//! |---|---|---|
//! | [`DocumentFeed`] | `bvnr_open_read_feed` | the events of [`crate::Reader`] |
//! | [`ValueFeed`] | the same | each top-level assignment as a [`Value`] when it completes |
//! | [`EmbeddedFeed`] | the same | the documents embedded under a key of another document |
//! | [`FrameParser`] | `bvnr_frame_reader_create` | the events and the result of every framed document |
//! | [`FramePull`] | `bvnr_frame_reader_create_pull` | every framed document's bytes |
//! | [`Demux`] | `bvnr_demux_open_feed` | the messages of a multiplexed stream, and the document's own events in order with them |
//!
//! Whatever is fed, wherever it is cut, the events and errors are exactly
//! those the batch reader gives for the concatenation; running out of input
//! is never an error until `finish` says the input is over.
//!
//! # Feeding and draining
//!
//! [`DocumentFeed`], [`ValueFeed`] and [`FrameParser`] deliver what a feed
//! completes before the feed returns, so the bytes may be reused at once.
//! [`FramePull`] and [`Demux`] are drained instead: `next` takes `&mut &[u8]`
//! and returns one item per call, borrowed from the input when it lies whole
//! there, else from a buffer the reader keeps. The item is valid until the
//! next call (the borrow checker holds you to it). Unlike the readers of
//! [`crate::codec`], which advance the slice past each item they return, these
//! leave it as it is until a call returns `None`, which empties it: pass the
//! same slice to every call until then, as another one is refused.
//!
//! # [`Demux`] or [`crate::codec::MuxReader`]?
//!
//! Both read a multiplexed stream as it arrives, with the limits and error
//! codes of the C demultiplexer. `MuxReader` is pure Rust and parses only the
//! framing: a document that is one octet stream, possibly after a text
//! preamble. It is the fast path for a dedicated stream, and it can peek at a
//! message and skip or forward it. `Demux` runs the full C parser over the
//! whole document: values before the stream *and after it*, several octet
//! streams of which one is multiplexed ([`DemuxOptions::key`]), every limit
//! of [`crate::ReadOptions`], and the document's
//! events in order with the messages. Use it for documents of any other
//! shape, or when the values around the stream matter.

use std::ffi::CString;

use crate::dict_parser::DictParser;
use crate::error::{Error, Result};
use crate::ffi::{self, DemuxItem as RawItem, Next};
use crate::reader::{EventData, EventHandler, ReadOptions, UnitPolicy, reader_error, wrap};
use crate::{ErrorCode, Value};

/// Where a fed document is.
#[derive(Clone, Debug, PartialEq)]
enum State {
    Open,
    Finished,
    Failed(Error),
}

/// A document parser that is fed bytes: one C reader (`bvnr_open_read_feed`)
/// for a whole stream of documents, each read with [`feed`](Self::feed) and
/// [`finish`](Self::finish) and the next one started with
/// [`reset`](Self::reset).
///
/// The handler of each call sees the events that call completes, exactly the
/// events [`crate::Reader::read_mem`] delivers for the concatenation of
/// everything fed, in order and wherever the pieces were cut. Nothing is
/// buffered beyond one token or one octet chunk, so memory stays flat over an
/// endless document. Its text is capped at the library's default of
/// 2 147 483 647 bytes per document (`error_text_data_too_long`) unless
/// [`ReadOptions::max_text_bytes`] lifts it: set it (and `max_array_items`)
/// to `u64::MAX` for a document that does not end.
///
/// ```
/// use bovnar::live::DocumentFeed;
/// use bovnar::{Event, EventData, ReadOptions};
///
/// let mut feed = DocumentFeed::new(&ReadOptions::default())?;
/// let mut keys = Vec::new();
/// let mut on_event = |d: &EventData<'_>| {
///     if d.event == Event::AssignmentStart {
///         keys.push(d.text());
///     }
///     true
/// };
/// for piece in [&b".temp = 2"[..], b"1.5; .uni", b"t = \"C\";"] {
///     feed.feed(piece, &mut on_event)?;
/// }
/// feed.finish(&mut on_event)?;
/// assert_eq!(keys, ["temp", "unit"]);
/// # Ok::<(), bovnar::Error>(())
/// ```
pub struct DocumentFeed {
    handle: ffi::FeedHandle,
    options: ReadOptions,
    state: State,
}

impl DocumentFeed {
    /// A parser with `options`, ready for the first document.
    pub fn new(options: &ReadOptions) -> Result<Self> {
        let handle = ffi::FeedHandle::new().ok_or(Error::OutOfMemory("bvnr_reader_create()"))?;
        let mut feed = DocumentFeed {
            handle,
            options: options.clone(),
            state: State::Open,
        };
        feed.reset()?;
        Ok(feed)
    }

    /// Installs a unit policy, as [`crate::Reader::set_unit_policy`] does:
    /// it applies to the values that follow, in this document and the next
    /// ones. `None` clears it.
    pub fn set_unit_policy(&mut self, policy: Option<&UnitPolicy>) -> Result<()> {
        if let Some(p) = policy {
            p.check_counts()?;
        }
        if self
            .handle
            .reader_mut()
            .set_unit_policy(policy.map(UnitPolicy::spec).as_ref())
        {
            Ok(())
        } else {
            Err(Error::argument_with(
                ErrorCode::InvalidArgument,
                match policy {
                    Some(p) => crate::reader::describe_unit_policy_refusal(p),
                    None => "bvnr_reader_set_unit_policy failed".into(),
                },
            ))
        }
    }

    /// Parses `bytes`, the next piece of the document, and hands the events
    /// it completes to `on_event`; a handler returning false stops the
    /// document with `scanner_callback_failed`. Running out of input is not
    /// an error here. An error ends the document: this and every later call
    /// return it until [`reset`](Self::reset). Feeding a finished document
    /// is an [`Error::Argument`].
    pub fn feed(&mut self, bytes: &[u8], on_event: EventHandler<'_>) -> Result<()> {
        self.check_open()?;
        let mut verified = wrap(on_event);
        let mut h = ffi::Handlers {
            verified: Some(&mut verified),
            ..ffi::Handlers::default()
        };
        if self.handle.feed(bytes, &mut h) {
            Ok(())
        } else {
            Err(self.fail())
        }
    }

    /// Declares the end of the document and hands the closing events to
    /// `on_event`. A document cut off is an error here:
    /// `got_incomplete_bvnr_stream` inside a value or a struct,
    /// `read_complete_chunk_failed` inside an octet stream, as
    /// [`crate::Reader::read_mem`] reports them.
    pub fn finish(&mut self, on_event: EventHandler<'_>) -> Result<()> {
        self.check_open()?;
        let mut verified = wrap(on_event);
        let mut h = ffi::Handlers {
            verified: Some(&mut verified),
            ..ffi::Handlers::default()
        };
        if self.handle.finish(&mut h) {
            self.state = State::Finished;
            Ok(())
        } else {
            Err(self.fail())
        }
    }

    /// Starts the next document, after a finish, after an error, or
    /// abandoning the current one. The reader and its unit policy are kept.
    pub fn reset(&mut self) -> Result<()> {
        if self.handle.open(self.options.flags()) {
            self.state = State::Open;
            Ok(())
        } else {
            Err(self.fail())
        }
    }

    /// Whether the current document has been finished.
    pub fn is_finished(&self) -> bool {
        self.state == State::Finished
    }

    /// The `#!bovnar M.N` directive of the current document, once read.
    pub fn declared_version(&self) -> Option<(u16, u16)> {
        self.handle.reader().declared_version()
    }

    /// How many errors a `continue_on_error` document recovered from.
    pub fn recovery_count(&self) -> u64 {
        self.handle.reader().recovery().0
    }

    /// How many input bytes a `continue_on_error` document skipped.
    pub fn skipped_bytes(&self) -> u64 {
        self.handle.reader().recovery().1
    }

    fn check_open(&self) -> Result<()> {
        match &self.state {
            State::Open => Ok(()),
            State::Failed(e) => Err(e.clone()),
            State::Finished => Err(Error::argument(
                "the document is finished; reset() starts the next one",
            )),
        }
    }

    fn fail(&mut self) -> Error {
        let e = reader_error(self.handle.reader().error());
        self.state = State::Failed(e.clone());
        e
    }
}

/// Builds [`Value`]s from a document's events, one top-level assignment at a
/// time: the values [`crate::parse`] (or [`crate::parse_typed`]) gives,
/// handed out as each assignment completes, so that an endless document is
/// read in constant memory. Feed it the events of a [`DocumentFeed`], or the
/// [`DemuxEvent`]s of a [`Demux`] to have the metadata before a stream
/// as values before the first message. A [`Demux`] withholds the mux
/// stream's chunks but not its start and end, so the builder hands out the
/// stream's own key too, as an empty [`Value::Bytes`], once the stream ends.
///
/// A scalar, a struct or an octet stream is complete with its last event;
/// an array, which a `/` may still continue, with the event after it (the
/// next assignment or the document's end). A key assigned twice is handed
/// out twice.
pub struct ValueBuilder {
    typed: bool,
    parser: DictParser,
    done: Vec<(String, Value)>,
}

impl ValueBuilder {
    /// A builder of plain values, or of typed ones ([`crate::parse_typed`]).
    pub fn new(typed: bool) -> Self {
        ValueBuilder {
            typed,
            parser: DictParser::new(typed),
            done: Vec::new(),
        }
    }

    /// Takes one event.
    pub fn push(&mut self, event: &EventData<'_>) {
        self.parser.on_event(event);
        self.parser.take_completed(&mut self.done);
    }

    /// The assignments completed since the last call, in document order.
    pub fn drain(&mut self) -> std::vec::Drain<'_, (String, Value)> {
        self.done.drain(..)
    }

    /// Forgets the document in progress and anything not drained, for the
    /// next document.
    pub fn reset(&mut self) {
        self.parser = DictParser::new(self.typed);
        self.done.clear();
    }
}

/// A [`DocumentFeed`] that hands out each top-level assignment as a
/// [`Value`] once it is complete ([`ValueBuilder`]).
///
/// ```
/// use bovnar::live::ValueFeed;
/// use bovnar::{ReadOptions, Value};
///
/// let mut feed = ValueFeed::new(false, &ReadOptions::default())?;
/// let mut got = Vec::new();
/// feed.feed(b".a = 1; .b = { .c = ", &mut got)?;
/// assert_eq!(got, [("a".to_string(), Value::Int(1))]);
/// feed.feed(b"\"x\"; };", &mut got)?;
/// feed.finish(&mut got)?;
/// assert_eq!(got[1].0, "b");
/// # Ok::<(), bovnar::Error>(())
/// ```
pub struct ValueFeed {
    feed: DocumentFeed,
    builder: ValueBuilder,
}

impl ValueFeed {
    /// A parser of plain or typed values, with `options`.
    pub fn new(typed: bool, options: &ReadOptions) -> Result<Self> {
        Ok(ValueFeed {
            feed: DocumentFeed::new(options)?,
            builder: ValueBuilder::new(typed),
        })
    }

    /// Feeds `bytes` and appends the assignments they complete to `out`,
    /// those before an error included.
    pub fn feed(&mut self, bytes: &[u8], out: &mut Vec<(String, Value)>) -> Result<()> {
        let builder = &mut self.builder;
        let r = self.feed.feed(bytes, &mut |d| {
            builder.push(d);
            true
        });
        out.extend(builder.drain());
        r
    }

    /// Ends the document and appends its last assignments to `out`.
    pub fn finish(&mut self, out: &mut Vec<(String, Value)>) -> Result<()> {
        let builder = &mut self.builder;
        let r = self.feed.finish(&mut |d| {
            builder.push(d);
            true
        });
        out.extend(builder.drain());
        r
    }

    /// Starts the next document.
    pub fn reset(&mut self) -> Result<()> {
        self.builder.reset();
        self.feed.reset()
    }

    /// The parser underneath, for its version and counters.
    pub fn document(&self) -> &DocumentFeed {
        &self.feed
    }

    /// Installs a unit policy ([`DocumentFeed::set_unit_policy`]).
    pub fn set_unit_policy(&mut self, policy: Option<&UnitPolicy>) -> Result<()> {
        self.feed.set_unit_policy(policy)
    }
}

/// Reads the documents embedded under a key of another document (doc/10
/// §4) while that document arrives: hand it the outer document's events,
/// and it feeds each embedded document's octet chunks to one reused
/// [`DocumentFeed`] as they come, instead of gathering the octets and
/// starting a reader per document as [`crate::stream::parse_embedded`] does.
///
/// As `bvnr_demux_set_key` does, it takes an octet stream under the key at
/// any depth.
///
/// ```
/// use bovnar::live::{DocumentFeed, EmbeddedFeed};
/// use bovnar::{ReadOptions, Value, Writer};
///
/// let mut w = Writer::to_mem(4096, false)?;
/// w.embed_document("payload", b".n = 1;")?;
/// w.embed_document("payload", b".n = 2;")?;
/// w.finish()?;
/// let outer = w.get_output().to_vec();
///
/// let mut docs = Vec::new();
/// let mut embedded = EmbeddedFeed::new("payload", false)?;
/// let mut on_event = |d: &bovnar::EventData<'_>| {
///     docs.extend(embedded.push(d));
///     true
/// };
/// let mut feed = DocumentFeed::new(&ReadOptions::default())?;
/// for piece in outer.chunks(5) {
///     feed.feed(piece, &mut on_event)?;
/// }
/// feed.finish(&mut on_event)?;
/// let n = |v| Value::Struct(vec![("n".into(), Value::Int(v))]);
/// assert_eq!(docs, [Ok(n(1)), Ok(n(2))]);
/// # Ok::<(), bovnar::Error>(())
/// ```
pub struct EmbeddedFeed {
    key: Box<[u8]>,
    typed: bool,
    feed: DocumentFeed,
    parser: DictParser,
    key_match: bool,
    inside: bool,
    error: Option<Error>,
}

impl EmbeddedFeed {
    /// Reads the documents under `key`, as plain or typed values, with the
    /// reader options of [`crate::stream::parse_embedded`].
    pub fn new(key: &str, typed: bool) -> Result<Self> {
        Self::with_options(key, typed, &ReadOptions::default())
    }

    /// [`EmbeddedFeed::new`] with `options` for the embedded documents.
    pub fn with_options(key: &str, typed: bool, options: &ReadOptions) -> Result<Self> {
        Ok(EmbeddedFeed {
            key: key.as_bytes().into(),
            typed,
            feed: DocumentFeed::new(options)?,
            parser: DictParser::new(typed),
            key_match: false,
            inside: false,
            error: None,
        })
    }

    /// Takes one event of the outer document. Returns the embedded document
    /// this event completes: its value, or the error it failed with (the
    /// outer document reads on either way).
    pub fn push(&mut self, event: &EventData<'_>) -> Option<Result<Value>> {
        use crate::Event as E;
        match event.event {
            // A new outer document (a reused feed after one that failed or
            // was cut off inside the keyed stream) starts outside it.
            E::StreamStart => {
                self.key_match = false;
                self.inside = false;
                self.error = None;
            }
            E::AssignmentStart => self.key_match = event.raw == &*self.key,
            E::OctetStreamStart if self.key_match => {
                self.inside = true;
                self.parser = DictParser::new(self.typed);
                self.error = self.feed.reset().err();
            }
            E::Data if self.inside && self.error.is_none() => {
                let parser = &mut self.parser;
                self.error = self
                    .feed
                    .feed(event.raw, &mut |d| {
                        parser.on_event(d);
                        true
                    })
                    .err();
            }
            E::OctetStreamEnd if self.inside => {
                self.inside = false;
                let parser = std::mem::replace(&mut self.parser, DictParser::new(self.typed));
                if let Some(e) = self.error.take() {
                    return Some(Err(e));
                }
                let mut parser = parser;
                let r = self.feed.finish(&mut |d| {
                    parser.on_event(d);
                    true
                });
                return Some(r.map(|()| parser.result()));
            }
            _ => {}
        }
        None
    }
}

/// The error of a frame reader, by code: like [`crate::stream::load_documents`],
/// a failed document reports its code without a position.
fn frame_error(code: i32) -> Error {
    Error::parse_code(code)
}

/// A frame stream (doc/10 §2) that is fed bytes, every document parsed as it
/// arrives (`bvnr_frame_reader_create`): what
/// [`crate::stream::load_documents`] does, for a stream that never ends.
/// Each frame's payload is streamed into one reused reader, so a frame is
/// never held whole, however large.
pub struct FrameParser {
    handle: ffi::FrameParseHandle,
    finished: bool,
}

impl FrameParser {
    /// A parser with `options` for each document. With
    /// `continue_past_failed`, a document that fails is reported and the
    /// next one read; otherwise it ends the stream. A frame longer than
    /// `max_document_size` (0 for the default 256 MiB) ends it too.
    pub fn new(
        options: &ReadOptions,
        continue_past_failed: bool,
        max_document_size: u64,
    ) -> Result<Self> {
        let handle =
            ffi::FrameParseHandle::new(options.flags(), continue_past_failed, max_document_size)
                .ok_or(Error::OutOfMemory("bvnr_frame_reader_create()"))?;
        Ok(FrameParser {
            handle,
            finished: false,
        })
    }

    /// Feeds `bytes`. `on_event` sees the events of the documents they reach
    /// into, and `on_document` each document they complete, by its index and
    /// its result; returning false from it ends the stream with
    /// `scanner_callback_failed`. The stream's error ends it: this and every
    /// later call return it.
    pub fn feed(
        &mut self,
        bytes: &[u8],
        on_event: EventHandler<'_>,
        on_document: &mut dyn FnMut(u64, Result<()>) -> bool,
    ) -> Result<()> {
        if self.finished {
            return Err(Error::argument("the frame stream is finished"));
        }
        let mut verified = wrap(on_event);
        let mut documents = |index: u64, ok: bool, err: i32| {
            on_document(index, if ok { Ok(()) } else { Err(frame_error(err)) })
        };
        let mut h = ffi::Handlers {
            verified: Some(&mut verified),
            on_document: Some(&mut documents),
            ..ffi::Handlers::default()
        };
        if self.handle.feed(bytes, &mut h) {
            Ok(())
        } else {
            Err(frame_error(self.handle.error()))
        }
    }

    /// Declares the end of the stream: an error when it ended inside a
    /// frame (`got_incomplete_bvnr_stream`; the cut-off document is not
    /// reported), or the stream's error when it had failed.
    pub fn finish(&mut self) -> Result<()> {
        if self.finished {
            return Err(Error::argument("the frame stream is finished"));
        }
        self.finished = true;
        if self.handle.finish() {
            Ok(())
        } else {
            Err(frame_error(self.handle.error()))
        }
    }

    /// The documents seen so far.
    pub fn count(&self) -> u64 {
        self.handle.count()
    }
}

/// A frame stream that is fed bytes and hands out each document's bytes
/// (`bvnr_frame_reader_create_pull`), for the caller to parse as it likes:
/// with a [`DocumentFeed`] or a [`ValueFeed`] reused for every document, say.
/// The pure-Rust [`crate::codec::FrameReader`] does the same without C.
///
/// See [Feeding and draining](self#feeding-and-draining).
///
/// ```
/// use bovnar::live::{FramePull, ValueFeed};
/// use bovnar::{ReadOptions, stream};
///
/// let frames = stream::frame_documents(&[&b".n = 1;"[..], b".n = 2;"])?;
/// let mut pull = FramePull::new(0)?;
/// let mut values = ValueFeed::new(false, &ReadOptions::default())?;
/// let mut got = Vec::new();
/// for piece in frames.chunks(7) {
///     let mut input = piece;
///     while let Some(doc) = pull.next(&mut input)? {
///         values.feed(doc, &mut got)?;
///         values.finish(&mut got)?;
///         values.reset()?;
///     }
/// }
/// pull.finish()?;
/// assert_eq!(got.len(), 2);
/// # Ok::<(), bovnar::Error>(())
/// ```
pub struct FramePull {
    handle: ffi::FramePullHandle,
    finished: bool,
}

impl FramePull {
    /// A reader of frames up to `max_document_size` bytes (0 for the default
    /// 256 MiB).
    pub fn new(max_document_size: u64) -> Result<Self> {
        let handle = ffi::FramePullHandle::new(max_document_size)
            .ok_or(Error::OutOfMemory("bvnr_frame_reader_create_pull()"))?;
        Ok(FramePull {
            handle,
            finished: false,
        })
    }

    /// The next document whose last byte `input` holds: borrowed from
    /// `input` when its frame lies whole there, else gathered in a buffer the
    /// reader keeps. `None` once `input` is used up, which empties it. A bad
    /// magic is `unexpected_input_byte`, a frame over the cap
    /// `value_out_of_range`; the error ends the stream.
    pub fn next<'s, 'a: 's>(&'s mut self, input: &mut &'a [u8]) -> Result<Option<&'s [u8]>> {
        if self.finished {
            return Err(Error::argument("the frame stream is finished"));
        }
        match self.handle.next(input) {
            Next::Item(doc) => Ok(Some(doc)),
            Next::None => {
                *input = &input[input.len()..];
                Ok(None)
            }
            Next::Error(code) => Err(frame_error(code)),
            Next::OtherInput => Err(other_input()),
        }
    }

    /// Declares the end of the stream: an error when it ended inside a
    /// frame (`got_incomplete_bvnr_stream`), or the stream's error when it
    /// had failed.
    /// Refused, changing nothing, while fed bytes are left to drain.
    pub fn finish(&mut self) -> Result<()> {
        if self.finished {
            return Err(Error::argument("the frame stream is finished"));
        }
        if self.handle.is_attached() {
            return Err(undrained());
        }
        self.finished = true;
        if self.handle.finish() {
            Ok(())
        } else {
            Err(frame_error(self.handle.error()))
        }
    }

    /// The documents handed out so far.
    pub fn count(&self) -> u64 {
        self.handle.count()
    }
}

fn other_input() -> Error {
    Error::argument(
        "next() was given other bytes while the last ones were still being drained; \
         pass the same slice until next() returns None",
    )
}

fn undrained() -> Error {
    Error::argument("the bytes fed last are not drained yet; call next() until it returns None")
}

/// How a [`Demux`] reads.
#[derive(Clone, Debug, Default)]
pub struct DemuxOptions {
    /// The largest message (0 for the default 64 MiB); a longer declared
    /// length is `value_out_of_range`.
    pub max_message: u64,
    /// Only the octet streams opened under this key are multiplexed; the
    /// others are ordinary values. `None`: every octet stream is.
    pub key: Option<String>,
    /// Hand out the document's own events too ([`DemuxItem::Event`]): the
    /// values before the stream, its start and end, the values after it.
    pub deliver_events: bool,
    /// The limits of the document around the stream.
    pub read: ReadOptions,
}

/// A document event a [`Demux`] hands out. Its data is the demux's copy; a
/// converted value keeps its text, but not its exact rational
/// ([`EventData::converted_rational`] is `None`).
///
/// It is a handle of a few words, so that a [`DemuxItem`] stays small for
/// the messages; [`data`](Self::data) builds the full [`EventData`].
#[derive(Clone, Copy)]
pub struct DemuxEvent<'a> {
    event: crate::Event,
    view: ffi::EventView<'a>,
}

impl<'a> DemuxEvent<'a> {
    /// Which event it is.
    pub fn event(&self) -> crate::Event {
        self.event
    }

    /// The event and its data, as a reader's handler sees them.
    pub fn data(&self) -> EventData<'a> {
        EventData::with_event(&self.view, self.event)
    }
}

impl std::fmt::Debug for DemuxEvent<'_> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        self.data().fmt(f)
    }
}

/// What a [`Demux`] hands out.
#[derive(Clone, Copy, Debug)]
pub enum DemuxItem<'a> {
    /// A message.
    Message {
        /// The channel it was sent on.
        channel: u64,
        /// Its bytes: borrowed from the input when they lay whole in it,
        /// else from the demux's buffer for the channel.
        payload: &'a [u8],
    },
    /// A document event, with [`DemuxOptions::deliver_events`]: every event
    /// but the multiplexed stream's chunks, in document order and in order
    /// with the messages.
    Event(DemuxEvent<'a>),
}

/// A multiplexed stream (doc/10 §3) read as it arrives, through the C
/// demultiplexer: the general-purpose path, for a document of any shape. See
/// [which reader](self#demux-or-crate-codec-muxreader) to use, and
/// [Feeding and draining](self#feeding-and-draining).
///
/// The key scope, `max_message`, the 4096-channel limit and every error code
/// are those of [`crate::stream::mux_load`]. A document may begin with
/// ordinary values describing the stream; with
/// [`DemuxOptions::deliver_events`] their events come out before the first
/// message, and a [`ValueBuilder`] turns them into values.
///
/// ```
/// use bovnar::live::{Demux, DemuxItem, DemuxOptions, ValueBuilder};
/// use bovnar::Writer;
///
/// let mut w = Writer::to_mem(4096, false)?;
/// w.write_string("codec", "av1")?;
/// w.mux_begin("media")?;
/// w.mux_send(1, b"frame")?;
/// w.mux_end()?;
/// w.finish()?;
/// let doc = w.get_output().to_vec();
///
/// let options = DemuxOptions { deliver_events: true, ..DemuxOptions::default() };
/// let mut demux = Demux::new(&options)?;
/// let mut meta = ValueBuilder::new(false);
/// let mut seen = Vec::new();
/// for piece in doc.chunks(4) {
///     let mut input = piece;
///     while let Some(item) = demux.next(&mut input)? {
///         match item {
///             DemuxItem::Event(e) => meta.push(&e.data()),
///             DemuxItem::Message { channel, payload } => {
///                 // The metadata is complete before the first message.
///                 let codec: Vec<_> = meta.drain().collect();
///                 assert_eq!(codec[0].0, "codec");
///                 seen.push((channel, payload.to_vec()));
///             }
///         }
///     }
/// }
/// demux.finish()?;
/// while let Some(_closing) = demux.next(&mut &[][..])? {}
/// assert_eq!(seen, [(1, b"frame".to_vec())]);
/// # Ok::<(), bovnar::Error>(())
/// ```
///
/// A message borrows the demux until the next call:
///
/// ```compile_fail
/// # use bovnar::live::{Demux, DemuxItem, DemuxOptions};
/// # let mut demux = Demux::new(&DemuxOptions::default()).unwrap();
/// let mut input = &b""[..];
/// let first = demux.next(&mut input).unwrap();
/// let second = demux.next(&mut input).unwrap(); // `demux` is still borrowed
/// drop((first, second));
/// ```
pub struct Demux {
    handle: ffi::DemuxHandle,
    options: DemuxOptions,
    finished: bool,
}

impl Demux {
    /// A demux with `options`, ready for the first document.
    pub fn new(options: &DemuxOptions) -> Result<Self> {
        let key = options
            .key
            .as_deref()
            .map(CString::new)
            .transpose()
            .map_err(|_| Error::argument("mux key contains a NUL byte"))?;
        let handle = ffi::DemuxHandle::new(options.max_message, key.as_deref())
            .map_err(Error::OutOfMemory)?;
        let mut demux = Demux {
            handle,
            options: options.clone(),
            finished: false,
        };
        demux.reset()?;
        Ok(demux)
    }

    /// The next message or event whose last byte `input` holds; `None` once
    /// `input` is used up, which empties it. After [`finish`](Self::finish),
    /// pass an empty slice to have the closing events; more input is then an
    /// [`Error::Argument`] and is left untouched. The stream's error
    /// ends it: this and every later call return it, after the events read
    /// before it.
    pub fn next<'s, 'a: 's>(&'s mut self, input: &mut &'a [u8]) -> Result<Option<DemuxItem<'s>>> {
        if self.finished && !input.is_empty() {
            return Err(Error::argument(
                "the document is finished; pass an empty slice for the closing events, or reset()",
            ));
        }
        let fed = if self.finished { None } else { Some(*input) };
        match self.handle.next(fed) {
            Next::Item(RawItem::Message(channel, payload)) => {
                Ok(Some(DemuxItem::Message { channel, payload }))
            }
            Next::Item(RawItem::Event(event, view)) => {
                Ok(Some(DemuxItem::Event(DemuxEvent { event, view })))
            }
            Next::None => {
                *input = &input[input.len()..];
                Ok(None)
            }
            Next::Error((code, reader)) => Err(demux_error(code, reader)),
            Next::OtherInput => Err(other_input()),
        }
    }

    /// Declares the end of the document. An error when it is cut off, as
    /// [`crate::stream::mux_load`] reports it, or when the stream's end cut a
    /// message short (`octet_stream_truncated`). Refused, changing nothing,
    /// while fed bytes are left to drain.
    pub fn finish(&mut self) -> Result<()> {
        if self.finished {
            return Err(Error::argument(
                "the document is finished; reset() starts the next one",
            ));
        }
        if self.handle.is_attached() {
            return Err(undrained());
        }
        self.finished = true;
        let ok = self.handle.finish();
        let code = self.handle.error();
        if ok && code == 0 {
            Ok(())
        } else {
            Err(demux_error(code, self.handle.reader_error()))
        }
    }

    /// The `#!bovnar M.N` directive of the current document, once read.
    pub fn declared_version(&self) -> Option<(u16, u16)> {
        self.handle
            .reader_counters()
            .and_then(|c| c.declared_version)
    }

    /// How many errors a `continue_on_error` document recovered from.
    pub fn recovery_count(&self) -> u64 {
        self.handle
            .reader_counters()
            .map_or(0, |c| c.recovery_count)
    }

    /// How many input bytes a `continue_on_error` document skipped.
    pub fn skipped_bytes(&self) -> u64 {
        self.handle.reader_counters().map_or(0, |c| c.skipped_bytes)
    }

    /// Starts the next document: the channel buffers are kept; the channel
    /// ids (and with them the 4096-channel count), the error and all progress
    /// are reset.
    pub fn reset(&mut self) -> Result<()> {
        self.finished = false;
        if self
            .handle
            .open(self.options.read.flags(), self.options.deliver_events)
        {
            Ok(())
        } else {
            Err(Error::OutOfMemory("bvnr_demux_open_feed()"))
        }
    }
}

/// A fed demux's error, as [`crate::stream::mux_load`] gives it: the
/// reader's, with its position, when the document failed; the demux's own
/// code alone when the stream did.
fn demux_error(code: i32, reader: Option<ffi::ReaderError>) -> Error {
    match reader {
        Some(r) if r.code == code && code != 0 => reader_error(r),
        // A call the C demux refused without a stream error: it is not open
        // for feeding (a reset that failed).
        _ if code == 0 => Error::argument("the demux is not open; reset() it"),
        _ => Error::parse_code(code),
    }
}
