// SPDX-License-Identifier: MIT
//! The streaming writer.

use crate::enums::{ErrorCode, Event, TokenType, ValueTypeFamily};
use crate::error::{Error, Result};
use crate::ffi::{self, BvnrWriteFlags, ValueTypeSpec};
use crate::quantity::Quantity;
use crate::reader::{EventData, UnitPolicy};
use crate::unit::Unit;

/// Default size of the memory sink.
pub const DEFAULT_MEM_CAP: usize = 4 * 1024 * 1024;

/// Writes into a memory sink that grows: `write` runs on a fresh writer of
/// about `size_hint` bytes and, while it fails with `sink_buffer_exhausted`,
/// again on one twice the size, up to 256 MiB (or twice the hint). The C
/// library has only fd and fixed memory sinks, so retrying is the growing
/// sink; `write` must therefore be repeatable.
/// Writes `size` bytes' worth of output with `write`, doubling the buffer
/// while it runs out.
pub(crate) fn grow_write(
    size: usize,
    pretty: bool,
    mut write: impl FnMut(&mut Writer) -> Result<()>,
) -> Result<Vec<u8>> {
    let mut cap = size + size / 4000 + 8192;
    let cap_max = (256 << 20).max(size * 2 + (1 << 20));
    loop {
        let mut w = Writer::to_mem(cap, pretty)?;
        match write(&mut w).and_then(|_| w.finish()) {
            Ok(()) => return Ok(w.get_output().to_vec()),
            Err(e) if e.code() == Some(crate::ErrorCode::SinkBufferExhausted) && cap < cap_max => {
                cap = (cap * 2).min(cap_max);
            }
            Err(e) => return Err(e),
        }
    }
}

/// Largest octet-stream chunk: the most one chunk header can count.
const OCTET_CHUNK: usize = 65536;
/// A numeric base bovnar reads and writes: 2..=62, 64 or 85 (the `_N` of
/// an annotation). A checked newtype rather than an enum of 63 variants;
/// the common ones are named.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub struct Base(u32);

impl Base {
    pub const BINARY: Self = Self(2);
    pub const OCTAL: Self = Self(8);
    pub const DECIMAL: Self = Self(10);
    pub const HEX: Self = Self(16);
    pub const BASE36: Self = Self(36);
    pub const BASE62: Self = Self(62);
    pub const BASE64: Self = Self(64);
    pub const BASE85: Self = Self(85);

    /// The base `n`, or `None` for one bovnar has no alphabet for.
    pub const fn new(n: u32) -> Option<Self> {
        if (n >= 2 && n <= 62) || n == 64 || n == 85 {
            Some(Self(n))
        } else {
            None
        }
    }

    /// The base as a number.
    pub const fn get(self) -> u32 {
        self.0
    }
}

impl Default for Base {
    fn default() -> Self {
        Self::DECIMAL
    }
}

impl TryFrom<u32> for Base {
    type Error = Error;
    fn try_from(n: u32) -> Result<Self> {
        Self::new(n)
            .ok_or_else(|| Error::argument(format!("base {n} is not one of 2..62, 64 or 85")))
    }
}

impl From<Base> for u32 {
    fn from(b: Base) -> u32 {
        b.0
    }
}

/// What [`Writer::emit`] sends with an event: a key, a data literal and its
/// type, each optional.
///
/// - `w.emit(Event::AssignmentStart, EmitSpec::key("x"))`
/// - `w.emit(Event::Data, EmitSpec::value("42", spec))`
/// - `w.emit(Event::StructEnd, EmitSpec::NONE)`
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct EmitSpec<'a> {
    /// The key of an assignment.
    pub key: Option<&'a str>,
    /// A data literal.
    pub value: Option<&'a str>,
    /// The literal's type; plain when `None`.
    pub vt: Option<ValueTypeSpec>,
}

impl<'a> EmitSpec<'a> {
    /// Nothing with the event.
    pub const NONE: Self = Self {
        key: None,
        value: None,
        vt: None,
    };

    /// An assignment's key.
    pub fn key(key: &'a str) -> Self {
        Self {
            key: Some(key),
            ..Self::NONE
        }
    }

    /// A data literal of type `vt`.
    pub fn value(value: &'a str, vt: ValueTypeSpec) -> Self {
        Self {
            value: Some(value),
            vt: Some(vt),
            ..Self::NONE
        }
    }
}

/// Whether [`Writer::write_bvni`] writes `sint` or `uint`.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, Default)]
pub enum Signedness {
    Signed,
    Unsigned,
    /// `sint` for a negative value, `uint` otherwise.
    #[default]
    Auto,
}

/// Runs `write` on a writer whose output streams into `out`, then finishes
/// it: the C writer pushes its 64 KiB blocks into `out` as they fill, the
/// rest at the end, and then flushes `out`. For the `*_to` functions, whose
/// `out` is borrowed for the one call.
pub(crate) fn stream_to(
    mut out: impl std::io::Write,
    pretty: bool,
    write: impl FnOnce(&mut Writer) -> Result<()>,
) -> Result<()> {
    let mut w = Writer::to_sink(ffi::PushState::scoped(), pretty, 0, false)?;
    ffi::with_scoped_out(&mut out, || write(&mut w).and_then(|()| w.finish()))
}

/// A writer's observer ([`WriteOptions::on_event`]): each event once it has
/// been serialised, with what reached the output; returning false fails the
/// write with `scanner_callback_failed`.
pub type WriteObserver = Box<dyn FnMut(&EventData<'_>) -> bool + Send>;

/// How a writer writes, for [`Writer::to_mem_with_options`] and
/// [`Writer::to_writer_with_options`]. The default is what
/// [`Writer::to_mem`] gives with `pretty` false.
#[derive(Default)]
pub struct WriteOptions {
    /// Indent the output.
    pub pretty: bool,
    /// [`crate::units::UnitFlags`] rewriting units on output.
    pub unit_flags: u32,
    /// Lead the document with `#!bovnar <major>.<minor>`.
    pub emit_version: bool,
    /// Deepest struct nesting; 0 is 255, the format's limit, as for
    /// [`crate::ReadOptions::max_struct_nesting`]. A deeper struct fails the
    /// write with `struct_nesting_too_high`.
    pub max_struct_nesting: u8,
    /// Deepest array nesting; 0 is 255. A deeper array fails the write with
    /// `array_nesting_too_high`.
    pub max_array_nesting: u8,
    /// Called with each event once it has been serialised
    /// (`bvnr_write_flags_t.on_event`), with what reached the output: under
    /// [`crate::units::UnitFlags::REDUCE`] a rescaled value's new digits and
    /// unit, not the ones it was given. Returning false fails that write,
    /// and the writer, with `scanner_callback_failed`; a panic in it reaches
    /// the caller of the write.
    pub on_event: Option<WriteObserver>,
}

impl std::fmt::Debug for WriteOptions {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("WriteOptions")
            .field("pretty", &self.pretty)
            .field("unit_flags", &self.unit_flags)
            .field("emit_version", &self.emit_version)
            .field("max_struct_nesting", &self.max_struct_nesting)
            .field("max_array_nesting", &self.max_array_nesting)
            .field("on_event", &self.on_event.as_ref().map(|_| ".."))
            .finish()
    }
}

/// A streaming BVNR writer into memory, or into a [`std::io::Write`].
pub struct Writer {
    handle: ffi::WriterHandle,
    struct_depth: u32,
    finished: bool,
    /// The error the first [`Writer::finish`] failed with, returned again by
    /// every later call instead of a success the output does not back.
    finish_error: Option<Error>,
}

impl Writer {
    /// A writer into a memory buffer of `cap` bytes; `pretty` indents the
    /// output. Writing past `cap` fails with `sink_buffer_exhausted`.
    pub fn to_mem(cap: usize, pretty: bool) -> Result<Self> {
        Self::to_mem_with(cap, pretty, 0, false)
    }

    /// [`Writer::to_mem`] with output options:
    /// `unit_flags` ([`crate::units::UnitFlags`]) rewrite units on output,
    /// and `emit_version` leads the document with `#!bovnar <major>.<minor>`.
    pub fn to_mem_with(
        cap: usize,
        pretty: bool,
        unit_flags: u32,
        emit_version: bool,
    ) -> Result<Self> {
        Self::open(
            WriteOptions {
                unit_flags,
                emit_version,
                ..WriteOptions::default()
            },
            |h, flags| h.open_mem(cap, pretty, flags),
        )
    }

    /// [`Writer::to_mem`] with every [`WriteOptions`]: the nesting caps and
    /// the `on_event` observer beside the options of
    /// [`Writer::to_mem_with`].
    ///
    /// ```
    /// use bovnar::{Event, Value, Writer, WriteOptions};
    /// use std::sync::{Arc, Mutex};
    ///
    /// let seen = Arc::new(Mutex::new(Vec::new()));
    /// let log = Arc::clone(&seen);
    /// let options = WriteOptions {
    ///     unit_flags: bovnar::units::UnitFlags::REDUCE,
    ///     on_event: Some(Box::new(move |d| {
    ///         if d.event == Event::Data {
    ///             log.lock().unwrap().push(d.text());
    ///         }
    ///         true
    ///     })),
    ///     ..WriteOptions::default()
    /// };
    /// let mut w = Writer::to_mem_with_options(4096, options)?;
    /// let Value::Struct(doc) = bovnar::parse_typed(b".d = <uint:64,k~m> 5;")? else {
    ///     unreachable!()
    /// };
    /// let Value::Quantity(q) = &doc[0].1 else { unreachable!() };
    /// w.write_quantity("d", q)?;
    /// w.finish()?;
    /// assert_eq!(w.get_output(), b".d=<uint:64,m>5000;");
    /// assert_eq!(*seen.lock().unwrap(), ["5000"]);
    /// # Ok::<(), bovnar::Error>(())
    /// ```
    pub fn to_mem_with_options(cap: usize, options: WriteOptions) -> Result<Self> {
        let pretty = options.pretty;
        Self::open(options, |h, flags| h.open_mem(cap, pretty, flags))
    }

    /// [`Writer::to_writer_with`] with every [`WriteOptions`]; `cap` is the
    /// most bytes `out` takes, `usize::MAX` for no cap.
    pub fn to_writer_with_options(
        out: impl std::io::Write + Send + 'static,
        cap: usize,
        options: WriteOptions,
    ) -> Result<Self> {
        let pretty = options.pretty;
        let state = ffi::PushState::owned(Box::new(out), sink_cap(cap));
        Self::open(options, |h, flags| h.open_sink(state, pretty, flags))
    }

    /// A writer whose document streams into any [`std::io::Write`], such as
    /// a [`std::fs::File`] or a socket.
    ///
    /// The writer batches its output as it does for an fd: `out` receives it
    /// in 64 KiB blocks as they fill, the rest on [`Writer::flush`] or
    /// [`Writer::finish`], which then flush `out`. So `out` may hold the
    /// start of a document whose writing later fails, and a document shorter
    /// than a block reaches `out` only on one of those two; dropping the
    /// writer unfinished drops what it holds. There is no cap on the
    /// document's size; [`Writer::to_writer_with`] sets one. Every write is
    /// whole: a short write or an `Interrupted` one is continued. A failing
    /// `out` is an [`Error::Io`] with `writing_to_sink`, the code of a
    /// failing fd sink, from the call whose output it refused and from every
    /// call after. A panic in `out` reaches the caller of that call.
    /// [`Writer::get_output`] is empty for such a writer.
    pub fn to_writer(out: impl std::io::Write + Send + 'static, pretty: bool) -> Result<Self> {
        Self::to_sink(
            ffi::PushState::owned(Box::new(out), u64::MAX),
            pretty,
            0,
            false,
        )
    }

    /// [`Writer::to_writer`] for a document of at most `cap` bytes
    /// (`usize::MAX` for no cap), with the output options
    /// [`Writer::to_mem_with`] takes. Output past `cap` fails
    /// with `sink_buffer_exhausted`, as with [`Writer::to_mem`], but only when
    /// it is pushed to `out`: the write that fills a 64 KiB block past the
    /// cap, or the [`Writer::flush`] or [`Writer::finish`] that pushes it, is
    /// the call that fails. What `out` holds then is the output up to the
    /// last push that fitted.
    pub fn to_writer_with(
        out: impl std::io::Write + Send + 'static,
        cap: usize,
        pretty: bool,
        unit_flags: u32,
        emit_version: bool,
    ) -> Result<Self> {
        Self::to_sink(
            ffi::PushState::owned(Box::new(out), sink_cap(cap)),
            pretty,
            unit_flags,
            emit_version,
        )
    }

    /// A writer into the callback sink `state`.
    pub(crate) fn to_sink(
        state: ffi::PushState,
        pretty: bool,
        unit_flags: u32,
        emit_version: bool,
    ) -> Result<Self> {
        Self::open(
            WriteOptions {
                unit_flags,
                emit_version,
                ..WriteOptions::default()
            },
            |h, flags| h.open_sink(state, pretty, flags),
        )
    }

    /// A fresh writer, opened by `open` with the write flags of `options`
    /// (its `pretty` is `open`'s to pass).
    fn open(
        options: WriteOptions,
        open: impl FnOnce(&mut ffi::WriterHandle, BvnrWriteFlags) -> bool,
    ) -> Result<Self> {
        let handle = ffi::WriterHandle::new().ok_or(Error::OutOfMemory("bvnr_writer_create()"))?;
        let mut w = Writer {
            handle,
            struct_depth: 0,
            finished: false,
            finish_error: None,
        };
        let mut flags = BvnrWriteFlags::zeroed();
        // The writer matches what the reader reads: the 255-level cap in both
        // directions unless told otherwise, where zero would select the C
        // default of 64.
        let nesting = |n: u8| if n == 0 { 255 } else { n };
        flags.max_struct_nesting = nesting(options.max_struct_nesting);
        flags.max_array_nesting = nesting(options.max_array_nesting);
        flags.unit_flags = options.unit_flags;
        flags.emit_version = options.emit_version;
        if let Some(mut f) = options.on_event {
            w.handle.set_observer(
                Box::new(move |view: &ffi::EventView<'_>| {
                    EventData::from_view(view).is_none_or(|d| f(&d))
                }),
                &mut flags,
            );
        }
        if !open(&mut w.handle, flags) {
            return Err(w.error());
        }
        Ok(w)
    }

    /// Sends one raw event; what goes with it is an [`EmitSpec`].
    pub fn emit(&mut self, event: Event, spec: EmitSpec<'_>) -> Result<()> {
        let EmitSpec { key, value, vt } = spec;
        let vt = vt.unwrap_or_default();
        let (token, data) = match (value, key) {
            (Some(v), _) => {
                let family = ValueTypeFamily::from_raw(vt.family);
                let decimal_float = matches!(
                    family,
                    Some(ValueTypeFamily::FloatFix | ValueTypeFamily::FloatDec)
                );
                let token = if family == Some(ValueTypeFamily::Utf8)
                    || (vt.base != 0 && vt.base != 10 && !decimal_float)
                {
                    TokenType::String
                } else {
                    TokenType::Number
                };
                (token, v.as_bytes())
            }
            (None, Some(k)) => (TokenType::Identifier, k.as_bytes()),
            (None, None) => (TokenType::Identifier, &[][..]),
        };
        self.event(event, token, vt, data)
    }

    /// Emits a leading `#!bovnar <major>.<minor>` directive; must come before
    /// any value.
    pub fn write_version(&mut self, major: u16, minor: u16) -> Result<()> {
        self.check(|h| h.write_version(major, minor))
    }

    /// `.key = <uint:width[,_base]> value;`
    pub fn write_uint(&mut self, key: &str, value: u64, width: u32, base: Base) -> Result<()> {
        let base = base.get();
        let vt = type_spec(ValueTypeFamily::Uint, width, base);
        let text = ffi::format_u64(value, base)
            .ok_or_else(|| Error::argument(format!("cannot format {value} in base {base}")))?;
        self.write_scalar(key, vt, &text)
    }

    /// `.key = <sint:width[,_base]> value;`
    pub fn write_sint(&mut self, key: &str, value: i64, width: u32, base: Base) -> Result<()> {
        let base = base.get();
        if base == 64 || base == 85 {
            return Err(Error::argument(format!(
                "base {base} is unsigned: its alphabet uses '+' and '-' as digits, \
                 so a signed integer cannot be written in it"
            )));
        }
        let vt = type_spec(ValueTypeFamily::Sint, width, base);
        let text = ffi::format_i64(value, base)
            .ok_or_else(|| Error::argument(format!("cannot format {value} in base {base}")))?;
        self.write_scalar(key, vt, &text)
    }

    /// `.key = <float:width> value;` A `float:16` or `float:32` value is
    /// rounded to IEEE binary16 or binary32, and a finite value the format
    /// cannot hold is an `Error::Write` with `ValueOutOfRange`.
    pub fn write_float(&mut self, key: &str, value: f64, width: u32) -> Result<()> {
        let vt = type_spec(ValueTypeFamily::Float, width, 0);
        let text = ffi::format_f64(value, vt).ok_or_else(|| {
            // float:16 and float:32 round to IEEE binary16 and binary32, and a
            // finite value the format cannot hold is refused, as the C writer
            // refuses it.
            if matches!(width, 16 | 32) && value.is_finite() {
                Error::Write {
                    code: ErrorCode::ValueOutOfRange as i32,
                    offset: 0,
                }
            } else {
                Error::argument(format!("cannot format {value}"))
            }
        })?;
        self.write_scalar(key, vt, &text)
    }

    /// `.key = <float_fix:width,qQ> value;` — a signed fixed point with `q`
    /// fractional bits.
    pub fn write_float_fix(&mut self, key: &str, value: f64, width: u32, q: u32) -> Result<()> {
        let text = format_float(value, width)?;
        let vt = type_spec(ValueTypeFamily::FloatFix, width, q);
        self.write_scalar(key, vt, &text)
    }

    /// `.key = <float_dec:width> value;` — an IEEE decimal float.
    pub fn write_float_dec(&mut self, key: &str, value: f64, width: u32) -> Result<()> {
        let text = format_float(value, width)?;
        self.write_scalar(key, type_spec(ValueTypeFamily::FloatDec, width, 0), &text)
    }

    /// `.key = <float:width[,_16]> literal;` — a float given as its literal,
    /// decimal (`base` 10) or hexadecimal (16), at any precision.
    pub fn write_bvnf_base(
        &mut self,
        key: &str,
        literal: &str,
        width: u32,
        base: Base,
    ) -> Result<()> {
        let vt_base = match base.get() {
            10 => 0,
            16 => 16,
            _ => {
                return Err(Error::argument(format!(
                    "write_bvnf_base: base must be 10 or 16, got {}",
                    base.get()
                )));
            }
        };
        self.write_scalar(
            key,
            type_spec(ValueTypeFamily::Float, width, vt_base),
            literal,
        )
    }

    /// `.key = <uint|sint:width[,_base]> value;` for an integer of any width
    /// up to 128 bits; `signed` picks sint or uint, and [`Signedness::Auto`]
    /// writes negative values as sint.
    pub fn write_bvni(
        &mut self,
        key: &str,
        value: i128,
        width: u32,
        base: Base,
        signed: Signedness,
    ) -> Result<()> {
        let base = base.get();
        let signed = match signed {
            Signedness::Signed => true,
            Signedness::Unsigned => false,
            Signedness::Auto => value < 0,
        };
        let family = if signed {
            refuse_signed_high_base(base)?;
            ValueTypeFamily::Sint
        } else {
            ValueTypeFamily::Uint
        };
        let text = format_bigint(value, base)?;
        let vt = type_spec(family, width, if base == 10 { 0 } else { base });
        self.write_scalar(key, vt, &text)
    }

    /// [`Writer::write_bvni`] for an integer of any size: `.key =
    /// <uint|sint:width[,_base]> value;`, for the values past `i128` that
    /// [`crate::Value::BigInt`] carries.
    pub fn write_big_int(
        &mut self,
        key: &str,
        value: &num_bigint::BigInt,
        width: u32,
        base: Base,
        signed: Signedness,
    ) -> Result<()> {
        let base = base.get();
        let signed = match signed {
            Signedness::Signed => true,
            Signedness::Unsigned => false,
            Signedness::Auto => value.sign() == num_bigint::Sign::Minus,
        };
        let family = if signed {
            refuse_signed_high_base(base)?;
            ValueTypeFamily::Sint
        } else {
            ValueTypeFamily::Uint
        };
        let text = format_big_int(value, base)?;
        let vt = type_spec(family, width, if base == 10 { 0 } else { base });
        self.write_scalar(key, vt, &text)
    }

    /// `.key = <annotation> literal;` for a typed scalar, as read by
    /// [`crate::parse_typed`]: type, unit, unit spelling and datetime
    /// fraction are kept. The annotation is left out where it says nothing
    /// the literal does not.
    pub fn write_quantity(&mut self, key: &str, q: &Quantity) -> Result<()> {
        if let Some(src) = q.unit_source.as_deref()
            && q.unit.is_dimensionless()
            && !Unit::parse(src).is_ok_and(|u| u.is_dimensionless())
        {
            return Err(Error::argument(format!(
                "cannot serialise {key:?}: unit_source {src:?} does not spell this \
                 Quantity's unit, which is no unit at all"
            )));
        }
        self.assignment(key)?;
        if needs_annotation(q) {
            self.annotation(q.vtype, &q.unit, q.unit_source.as_deref())?;
        }
        self.value(q.token_type, q, false)
    }

    /// Refuses to emit a value a reader under `policy` would refuse. Only the
    /// validation half applies: `require_unit`, `require_dimension_of` and
    /// non-converting rules. `None` clears it.
    pub fn set_unit_policy(&mut self, policy: Option<&UnitPolicy>) -> Result<()> {
        if let Some(p) = policy
            && (!p.targets.is_empty()
                || p.normalise_si
                || p.base != 0
                || p.leave_inexact
                || p.rules.iter().any(|r| r.convert))
        {
            return Err(Error::argument(
                "the writer takes only the validation half of a UnitPolicy \
                 (require_unit / require_dimension_of, and rules with convert=false)",
            ));
        }
        if let Some(p) = policy {
            p.check_counts()?;
        }
        if self
            .handle
            .set_unit_policy(policy.map(UnitPolicy::spec).as_ref())
        {
            Ok(())
        } else {
            // The write-side-only fields are refused above with their own
            // message, so what reaches here is a unit or a rule path the
            // parser will not take: what the reader's setter refuses too.
            Err(Error::argument(match policy {
                Some(p) => crate::reader::describe_unit_policy_refusal(p),
                None => "bvnr_writer_set_unit_policy failed".into(),
            }))
        }
    }

    /// `.key = "value";`
    pub fn write_string(&mut self, key: &str, value: &str) -> Result<()> {
        self.assignment(key)?;
        self.event(
            Event::Data,
            TokenType::String,
            ValueTypeSpec::default(),
            value.as_bytes(),
        )
    }

    /// `.key = true;` or `.key = false;`
    pub fn write_bool(&mut self, key: &str, value: bool) -> Result<()> {
        self.assignment(key)?;
        let text: &[u8] = if value { b"true" } else { b"false" };
        self.event(Event::Data, TokenType::Bool, ValueTypeSpec::default(), text)
    }

    /// `.key = null;`
    pub fn write_null(&mut self, key: &str) -> Result<()> {
        self.assignment(key)?;
        self.event(
            Event::Data,
            TokenType::NullValue,
            ValueTypeSpec::default(),
            &[],
        )
    }

    /// `.key = <octet stream>;`
    pub fn write_octets(&mut self, key: &str, value: &[u8]) -> Result<()> {
        self.assignment(key)?;
        self.octets(value)
    }

    /// An octet stream value, chunked.
    pub(crate) fn octets(&mut self, value: &[u8]) -> Result<()> {
        let none = ValueTypeSpec::default();
        self.event(Event::OctetStreamStart, TokenType::Identifier, none, &[])?;
        for chunk in value.chunks(OCTET_CHUNK) {
            self.event(Event::Data, TokenType::OctetStream, none, chunk)?;
        }
        self.event(Event::OctetStreamEnd, TokenType::Identifier, none, &[])
    }

    /// `.key = <octets>;` where the octets are the document `doc`
    /// (`bvnr_embed_document`), so that a reader can parse it back:
    /// [`crate::stream::parse_embedded`], or, on a live stream,
    /// [`crate::live::EmbeddedFeed`]. On a [`Writer::to_writer`] writer this
    /// embeds one document after another into a document that is still being
    /// written.
    pub fn embed_document(&mut self, key: &str, doc: &[u8]) -> Result<()> {
        check_key(key)?;
        self.check(|h| h.embed_document(key, doc))
    }

    /// Opens a multiplexed octet stream under `key` (`bvnr_mux_begin`): send
    /// messages with [`Writer::mux_send`] and close it with
    /// [`Writer::mux_end`]. Values written before it are read back before
    /// the first message (doc/10 §3.4), and the document may go on after it.
    pub fn mux_begin(&mut self, key: &str) -> Result<()> {
        check_key(key)?;
        self.check(|h| h.mux_begin(key))
    }

    /// One message on `channel` (`bvnr_mux_send`), split over as many octet
    /// chunks as it needs. On a [`Writer::to_writer`] writer it reaches the
    /// destination when the writer's 64 KiB block fills, at
    /// [`Writer::flush`], or when the document is finished: call
    /// [`Writer::flush`] after each send for a consumer that needs every
    /// message as it is sent.
    pub fn mux_send(&mut self, channel: u64, payload: &[u8]) -> Result<()> {
        self.check(|h| h.mux_send(channel, payload))
    }

    /// Closes the multiplexed octet stream (`bvnr_mux_end`).
    pub fn mux_end(&mut self) -> Result<()> {
        self.check(ffi::WriterHandle::mux_end)
    }

    /// `.key = {` — close with [`Writer::end_struct`].
    pub fn begin_struct(&mut self, key: &str) -> Result<()> {
        self.assignment(key)?;
        self.emit(Event::StructStart, EmitSpec::NONE)?;
        self.struct_depth += 1;
        Ok(())
    }

    /// `};`
    pub fn end_struct(&mut self) -> Result<()> {
        self.emit(Event::StructEnd, EmitSpec::NONE)?;
        self.struct_depth = self.struct_depth.saturating_sub(1);
        Ok(())
    }

    /// `[` of an array row.
    pub fn begin_array_row(&mut self) -> Result<()> {
        self.emit(Event::ArrayRowStart, EmitSpec::NONE)
    }

    /// `]` of an array row.
    pub fn end_array_row(&mut self) -> Result<()> {
        self.emit(Event::ArrayRowEnd, EmitSpec::NONE)
    }

    /// `/` between array rows.
    pub fn new_array_dim(&mut self) -> Result<()> {
        self.emit(Event::ArrayDimStart, EmitSpec::NONE)
    }

    /// Completes the document. Fails while a struct is still open. A
    /// [`Writer::to_writer`] writer then writes what its 64 KiB block still
    /// holds to its [`std::io::Write`] and flushes it. A finish that failed
    /// keeps failing: a second call returns the same error, also after a
    /// panic in the destination.
    pub fn finish(&mut self) -> Result<()> {
        if let Some(e) = &self.finish_error {
            return Err(e.clone());
        }
        if self.finished {
            return Ok(());
        }
        if self.struct_depth != 0 {
            return Err(Error::argument(format!(
                "finish() called with {} unclosed struct(s)",
                self.struct_depth
            )));
        }
        self.finished = true;
        // The error is kept before a panic in the sink is resumed, so a
        // finish that panicked fails again on the next call.
        let ok = self.handle.finish();
        if !ok {
            self.finish_error = Some(self.error());
        }
        self.handle.resume_sink_panic();
        match &self.finish_error {
            Some(e) => Err(e.clone()),
            None => Ok(()),
        }
    }

    /// Hands everything written so far to the destination now
    /// (`bvnr_write_flush`), then flushes it: a [`Writer::to_writer`]
    /// writer writes what its 64 KiB block holds into its
    /// [`std::io::Write`] and calls that writer's `flush` once. For a live
    /// consumer, such as a [`crate::live::Demux`] at the other end of a
    /// socket, that needs each message as it is sent rather than in 64 KiB
    /// bursts or at [`Writer::finish`].
    ///
    /// Legal between any two writer calls, inside an open struct, array or
    /// mux stream too, and it never changes the bytes: a separator owed to
    /// the next value stays owed, so a document flushed after every call is
    /// the one written without a flush. It allocates nothing. For a
    /// [`Writer::to_mem`] writer it does nothing. A failing destination is
    /// an [`Error::Io`] with `writing_to_sink`, from this call and every
    /// later one, as for any write. After a [`Writer::finish`] that
    /// succeeded it does nothing and succeeds.
    pub fn flush(&mut self) -> Result<()> {
        self.check(ffi::WriterHandle::flush)
    }

    /// Bytes written so far.
    pub fn bytes_written(&self) -> u64 {
        self.handle.bytes_written()
    }

    /// The document written so far; empty for a [`Writer::to_writer`]
    /// writer, whose output has gone to its destination.
    pub fn get_output(&self) -> &[u8] {
        self.handle.output()
    }

    pub(crate) fn assignment(&mut self, key: &str) -> Result<()> {
        self.emit(Event::AssignmentStart, EmitSpec::key(key))
    }

    /// An assignment of one annotated scalar literal.
    fn write_scalar(&mut self, key: &str, vt: ValueTypeSpec, text: &str) -> Result<()> {
        self.assignment(key)?;
        self.annotation(vt, &Unit::default(), None)?;
        let special = matches!(text, "nan" | "inf" | "ninf");
        let decimal_float = matches!(
            ValueTypeFamily::from_raw(vt.family),
            Some(ValueTypeFamily::FloatFix | ValueTypeFamily::FloatDec)
        );
        let token = if vt.base != 0 && vt.base != 10 && !special && !decimal_float {
            TokenType::String
        } else {
            TokenType::Number
        };
        self.event(Event::Data, token, vt, text.as_bytes())
    }

    /// A type annotation: `<family:width[,_base|,qN|,epoch][,unit]>`;
    /// `unit_source` is the unit's spelling.
    pub fn annotation(
        &mut self,
        vt: ValueTypeSpec,
        unit: &Unit,
        unit_source: Option<&str>,
    ) -> Result<()> {
        use ValueTypeFamily as F;
        let fam = ValueTypeFamily::from_raw(vt.family);
        let none = Unit::default();
        let param = Event::TypeAnnotationTypeFamilyParam;
        self.data_event(
            Event::TypeAnnotationStart,
            TokenType::Type,
            vt,
            unit,
            [b"", b"", b""],
        )?;
        self.event(Event::TypeAnnotationTypeFamily, TokenType::Type, vt, &[])?;
        let sized = matches!(
            fam,
            Some(F::Uint | F::Sint | F::Float | F::FloatFix | F::FloatDec | F::Datetime)
        );
        if sized && vt.width != 0 {
            self.event(param, TokenType::TypeWidth, vt, &[])?;
        }
        if matches!(fam, Some(F::Float | F::Sint | F::Uint)) && vt.base != 0 && vt.base != 10 {
            self.event(param, TokenType::TypeBase, vt, &[])?;
        }
        if fam == Some(F::FloatFix) && vt.base != 0 {
            self.event(param, TokenType::TypeQ, vt, &[])?;
        }
        if fam == Some(F::Datetime)
            && vt.base != 0
            && let Some(epoch) = ffi::datetime_epoch(vt).0
        {
            self.data_event(
                param,
                TokenType::Unit,
                vt,
                &none,
                [epoch.as_bytes(), b"", b""],
            )?;
        }
        if unit.0.num_components > 0 && !matches!(fam, Some(F::Datetime | F::Utf8)) {
            let text = ffi::unit_to_string_ex(&unit.0, self.handle.unit_flags()).map_err(|_| {
                Error::argument(
                    "cannot serialise this unit: bvn_unit_to_string_ex refused it, and \
                     writing the value without its unit would change what it means",
                )
            })?;
            if !text.is_empty() {
                let src = unit_source.unwrap_or("").as_bytes();
                self.data_event(
                    param,
                    TokenType::Unit,
                    vt,
                    unit,
                    [text.as_bytes(), b"", src],
                )?;
            }
        }
        self.event(
            Event::TypeAnnotationEnd,
            TokenType::Type,
            ValueTypeSpec::default(),
            &[],
        )
    }

    /// The data event of a typed value. Inside an array the token becomes
    /// its array form and the unit stays with the annotation.
    pub(crate) fn value(&mut self, token: TokenType, q: &Quantity, in_array: bool) -> Result<()> {
        let none = Unit::default();
        let (token, unit) = match (in_array, token) {
            (false, t) => (t, &q.unit),
            (true, TokenType::String | TokenType::ArrayString) => (TokenType::ArrayString, &none),
            (true, TokenType::Number) => (TokenType::ArrayNumber, &none),
            (true, t) => (t, &none),
        };
        let raw = q.raw.as_deref().unwrap_or("").as_bytes();
        let frac = q.frac.as_deref().unwrap_or("").as_bytes();
        let src = if in_array {
            &[][..]
        } else {
            q.unit_source.as_deref().unwrap_or("").as_bytes()
        };
        self.data_event(Event::Data, token, q.vtype, unit, [raw, frac, src])
    }

    /// One event with every payload field: literal, fraction, unit source.
    pub(crate) fn data_event(
        &mut self,
        event: Event,
        token: TokenType,
        vt: ValueTypeSpec,
        unit: &Unit,
        parts: [&[u8]; 3],
    ) -> Result<()> {
        if parts.iter().any(|p| u32::try_from(p.len()).is_err()) {
            return Err(Error::argument("a single value cannot exceed 4 GiB"));
        }
        self.check(|h| h.write_event_full(event as i32, token as i32, vt, &unit.0, parts))
    }

    /// Sends one raw event with an explicit token type (`bvnr_write_event`).
    pub fn event(
        &mut self,
        event: Event,
        token: TokenType,
        vt: ValueTypeSpec,
        data: &[u8],
    ) -> Result<()> {
        if u32::try_from(data.len()).is_err() {
            return Err(Error::argument("a single value cannot exceed 4 GiB"));
        }
        self.check(|h| h.write_event(event as i32, token as i32, vt, data))
    }

    pub(crate) fn check(
        &mut self,
        call: impl FnOnce(&mut ffi::WriterHandle) -> bool,
    ) -> Result<()> {
        let ok = call(&mut self.handle);
        self.handle.resume_sink_panic();
        if ok { Ok(()) } else { Err(self.error()) }
    }

    /// The writer's error; for a callback sink that failed, what the
    /// destination reported, or the cap it reached.
    fn error(&self) -> Error {
        let (code, offset) = self.handle.error();
        if code == ErrorCode::WritingToSink as i32
            && let Some(sink) = self.handle.sink()
        {
            if let Some(e) = sink.io_error() {
                return Error::io(ErrorCode::WritingToSink, e);
            }
            if sink.exhausted() {
                return Error::Write {
                    code: ErrorCode::SinkBufferExhausted as i32,
                    offset,
                };
            }
        }
        Error::Write { code, offset }
    }
}

/// A key the C mux and embedding calls can take: no NUL byte.
pub(crate) fn check_key(key: &str) -> Result<()> {
    if key.contains('\0') {
        Err(Error::argument(format!("key {key:?} contains a NUL byte")))
    } else {
        Ok(())
    }
}

/// A number with a literal of a numeric family.
pub(crate) fn is_numeric(q: &Quantity) -> bool {
    use ValueTypeFamily as F;
    matches!(
        q.family(),
        Some(F::Float | F::FloatDec | F::FloatFix | F::Uint | F::Sint)
    ) && q.raw.is_some()
}

/// True only when the annotation carries
/// something the bare literal would not give back.
///
/// A typed null has nothing but its annotation: `<float:64> ;` written bare
/// is `;`, an untyped null. And a quoted literal under a numeric family
/// (`<float:64,_10> "5.7"`) is a string to default synthesis: written bare
/// the writer refuses it, a float type over a string token.
pub(crate) fn needs_annotation(q: &Quantity) -> bool {
    use ValueTypeFamily as F;
    let vt = q.vtype;
    let fam = ValueTypeFamily::from_raw(vt.family);
    if matches!(fam, None | Some(F::Plain | F::Illegal)) {
        return false;
    }
    if q.raw.is_none() {
        return true;
    }
    if matches!(q.token_type, TokenType::String | TokenType::ArrayString) && fam != Some(F::Utf8) {
        return true;
    }
    match fam {
        None | Some(F::Plain | F::Bool | F::Illegal) => false,
        // A bare non-negative integer reads back as uint.
        Some(F::Sint)
            if q.raw
                .as_deref()
                .is_some_and(|r| !r.trim_start().starts_with('-')) =>
        {
            true
        }
        Some(F::Datetime | F::FloatFix | F::FloatDec) => true,
        Some(_) if !q.unit.is_dimensionless() => true,
        Some(_) if vt.base != 0 && vt.base != 10 => true,
        Some(_) => vt.width != 0 && vt.width != 64,
    }
}

fn format_float(value: f64, width: u32) -> Result<String> {
    ffi::format_f64(value, type_spec(ValueTypeFamily::Float, width, 0))
        .ok_or_else(|| Error::argument(format!("cannot format {value}")))
}

fn refuse_signed_high_base(base: u32) -> Result<()> {
    if base == 64 || base == 85 {
        return Err(Error::argument(format!(
            "base {base} is unsigned: its alphabet uses '+' and '-' as digits, \
             so a signed integer cannot be written in it"
        )));
    }
    Ok(())
}

/// `value` in any bovnar base (2..62, 64, 85).
pub fn format_bigint(value: i128, base: u32) -> Result<String> {
    format_big_int(&num_bigint::BigInt::from(value), base)
}

/// [`format_bigint`] for an integer of any size.
pub fn format_big_int(value: &num_bigint::BigInt, base: u32) -> Result<String> {
    const B64: &[u8] = b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    if !(2..=62).contains(&base) && base != 64 && base != 85 {
        return Err(Error::argument(format!("unsupported integer base {base}")));
    }
    let digit = |d: u8| -> char {
        match (base, d) {
            (64, _) => B64[usize::from(d)] as char,
            (85, _) => (b'!' + d) as char,
            (_, 0..=9) => (b'0' + d) as char,
            (_, 10..=35) => (b'a' + d - 10) as char,
            _ => (b'A' + d - 36) as char,
        }
    };
    let (sign, digits) = value.to_radix_be(base);
    let mut s: String = digits.into_iter().map(digit).collect();
    if sign == num_bigint::Sign::Minus {
        s.insert(0, '-');
    }
    Ok(s)
}

/// The width [`crate::to_string`] writes a [`crate::Value::BigInt`] at: the
/// smallest power of two from 128 that holds it as a `uint` (non-negative)
/// or a `sint` (negative), as `bovnar convert` writes a wide JSON integer.
pub(crate) fn big_int_width(value: &num_bigint::BigInt) -> u32 {
    let mag = value.magnitude();
    let bits = u32::try_from(mag.bits()).unwrap_or(u32::MAX);
    // A sint:N holds -2^(N-1): a negative power of two needs no extra bit.
    let pow2 = mag.bits() > 0 && mag.trailing_zeros() == Some(mag.bits() - 1);
    let need = if value.sign() == num_bigint::Sign::Minus && !pow2 {
        bits.saturating_add(1)
    } else {
        bits
    };
    let mut width = 128u32;
    while width < need && width < MAX_INT_WIDTH {
        width *= 2;
    }
    width
}

/// `BVN_MAX_INT_WIDTH`: the widest integer the library holds.
pub(crate) const MAX_INT_WIDTH: u32 = 32768;

pub(crate) fn type_spec(family: ValueTypeFamily, width: u32, base: u32) -> ValueTypeSpec {
    ValueTypeSpec {
        family: family as i32,
        width,
        base,
    }
}

/// The byte cap of a sink writer: `usize::MAX` is no cap, also on a 32-bit
/// target, where it would otherwise be a 4 GiB one.
fn sink_cap(cap: usize) -> u64 {
    if cap == usize::MAX {
        u64::MAX
    } else {
        cap as u64
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn written(f: impl FnOnce(&mut Writer) -> Result<()>) -> String {
        let mut w = Writer::to_mem(4096, false).unwrap();
        f(&mut w).unwrap();
        w.finish().unwrap();
        String::from_utf8(w.get_output().to_vec()).unwrap()
    }

    #[test]
    fn writes_scalars_and_structs() {
        let out = written(|w| {
            w.write_uint("u", 255, 64, Base::HEX)?;
            w.write_sint("i", -3, 64, Base::DECIMAL)?;
            w.write_float("f", 1.5, 64)?;
            w.begin_struct("s")?;
            w.write_string("t", "x")?;
            w.write_null("n")?;
            w.end_struct()
        });
        assert!(out.contains("\"ff\""), "{out}");
        assert!(out.contains("-3") && out.contains("1.5"), "{out}");
        assert!(out.contains("\"x\""), "{out}");
    }

    #[test]
    fn writes_typed_annotations() {
        let out = written(|w| {
            w.write_float_fix("fx", 1.5, 32, 16)?;
            w.write_float_dec("fd", 0.1, 64)?;
            w.write_bvni("big", -(1i128 << 100), 102, Base::DECIMAL, Signedness::Auto)?;
            w.write_bvnf_base("hx", "1.8p1", 64, Base::HEX)?;
            let km = Unit::parse("k~m").unwrap();
            w.write_quantity(
                "q",
                &Quantity::new("2.5", type_spec(ValueTypeFamily::Float, 64, 0), km),
            )
        });
        assert!(out.contains("<float_fix:32,q16>"), "{out}");
        assert!(out.contains("<float_dec:64>"), "{out}");
        assert!(
            out.contains("<sint:102>-1267650600228229401496703205376"),
            "{out}"
        );
        assert!(out.contains("<float:64,_16>"), "{out}");
        assert!(out.contains("<float:64,k~m>2.5"), "{out}");
        assert_eq!(format_bigint(255, 16).unwrap(), "ff");
        assert_eq!(format_bigint(63, 64).unwrap(), "/");
    }

    #[test]
    fn writer_policy_refuses_bare_numbers() {
        let mut w = Writer::to_mem(4096, false).unwrap();
        let strict = UnitPolicy {
            require_unit: true,
            ..UnitPolicy::default()
        };
        w.set_unit_policy(Some(&strict)).unwrap();
        assert_eq!(
            w.write_float("bare", 0.25, 64).unwrap_err().name(),
            Some("unit_mismatch")
        );
        let converting = UnitPolicy {
            targets: vec![("m".into(), 0)],
            ..UnitPolicy::default()
        };
        assert!(w.set_unit_policy(Some(&converting)).is_err());
    }

    #[test]
    fn full_buffer_reports_sink_buffer_exhausted() {
        let mut w = Writer::to_mem(8, false).unwrap();
        let err = w
            .write_string("key", "a value longer than eight bytes")
            .and_then(|_| w.finish());
        assert_eq!(err.unwrap_err().name(), Some("sink_buffer_exhausted"));
    }

    #[test]
    fn failed_finish_keeps_failing() {
        // The document fits until finish() appends its last bytes.
        let mut w = Writer::to_mem(6, false).unwrap();
        w.write_string("a", "x").unwrap();
        let first = w.finish().unwrap_err();
        assert_eq!(w.finish().unwrap_err(), first);

        struct Refuses;
        impl std::io::Write for Refuses {
            fn write(&mut self, _: &[u8]) -> std::io::Result<usize> {
                Err(std::io::Error::other("refused"))
            }
            fn flush(&mut self) -> std::io::Result<()> {
                Ok(())
            }
        }
        let mut w = Writer::to_writer(Refuses, false).unwrap();
        w.write_string("a", "x").unwrap();
        assert!(w.finish().is_err());
        assert!(w.finish().is_err());
    }
}
