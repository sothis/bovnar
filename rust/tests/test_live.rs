// SPDX-License-Identifier: MIT
//! `bovnar::live`: documents, frame streams and multiplexed streams fed in
//! pieces give exactly what the batch functions give for the whole input,
//! wherever the input is cut: the same events, the same values, the same
//! messages, and the same errors at the same places. The corpus is
//! `examples/`, the documents in `tests/` and the smaller registry sources in
//! `src/gendata/`, with hand-made documents good and bad.

use std::path::Path;

use bovnar::codec::{FrameReader, MuxReader, MuxWriter};
use bovnar::live::{
    Demux, DemuxItem, DemuxOptions, DocumentFeed, EmbeddedFeed, FrameParser, FramePull,
    ValueBuilder, ValueFeed,
};
use bovnar::stream;
use bovnar::{
    Error, ErrorCode, Event, EventData, EventPayload, ReadOptions, Reader, UnitPolicy, Value,
    Writer, parse, parse_typed,
};

/// A deterministic xorshift64* generator, so that a failure reproduces.
struct Rng(u64);

impl Rng {
    fn next(&mut self) -> u64 {
        self.0 ^= self.0 >> 12;
        self.0 ^= self.0 << 25;
        self.0 ^= self.0 >> 27;
        self.0.wrapping_mul(0x2545_F491_4F6C_DD1D)
    }

    fn below(&mut self, n: usize) -> usize {
        (self.next() % n as u64) as usize
    }
}

fn repo() -> &'static Path {
    Path::new(env!("CARGO_MANIFEST_DIR")).parent().unwrap()
}

/// Every `*.bvnr` under `dir` up to `max` bytes.
fn files(dir: &str, max: u64) -> Vec<(String, Vec<u8>)> {
    let mut out: Vec<_> = std::fs::read_dir(repo().join(dir))
        .unwrap()
        .map(|e| e.unwrap().path())
        .filter(|p| p.extension().is_some_and(|x| x == "bvnr"))
        .filter(|p| p.metadata().unwrap().len() <= max)
        .map(|p| (p.display().to_string(), std::fs::read(&p).unwrap()))
        .collect();
    out.sort();
    out
}

/// Documents good and bad that the files do not cover: errors of every
/// kind, octet streams, recovery, an empty document.
fn hand_made() -> Vec<(String, Vec<u8>)> {
    let mut docs: Vec<Vec<u8>> = vec![
        b"".to_vec(),
        b".a = 1;".to_vec(),
        b".a = @;".to_vec(),
        b".a = [1, 2;".to_vec(),
        b".a = { .b = 1;".to_vec(),
        b".a = \"open".to_vec(),
        b".a = <uint:8> 300;".to_vec(),
        b".a = 1 .b = 2;".to_vec(),
        b".\xff = 1;".to_vec(),
        b".a = [1, 2] / [3, 4]; .b = [[1], [2]];".to_vec(),
        b"#!bovnar 1.2\n.t = <float:64,ucum:m/s> 3;".to_vec(),
        b"#!bovnar 9.9\n.a = 1;".to_vec(),
    ];
    let mut w = Writer::to_mem(1 << 20, true).unwrap();
    w.write_octets("small", b"\x00\x01\x02").unwrap();
    w.write_octets("big", &[0xAB; 140_000]).unwrap();
    w.write_octets("empty", b"").unwrap();
    w.finish().unwrap();
    let octets = w.get_output().to_vec();
    docs.push(octets[..octets.len() - 70_000].to_vec());
    docs.push(octets);
    docs.into_iter()
        .enumerate()
        .map(|(i, d)| (format!("hand-made #{i}"), d))
        .collect()
}

fn corpus() -> Vec<(String, Vec<u8>)> {
    let mut all = files("examples", u64::MAX);
    all.extend(files("tests", u64::MAX));
    all.extend(files("src/gendata", 160_000));
    all.extend(hand_made());
    assert!(all.len() > 40, "{} documents", all.len());
    all
}

type Events = (bovnar::Result<()>, Vec<EventPayload>);
type Messages = Vec<(u64, Vec<u8>)>;

fn read_mem(doc: &[u8], options: &ReadOptions) -> Events {
    let mut events = Vec::new();
    let mut on = |d: &EventData<'_>| {
        events.push(EventPayload::from(d));
        true
    };
    let r = Reader::new()
        .unwrap()
        .read_mem(doc, options, Some(&mut on), None);
    (r, events)
}

/// `doc` fed in the pieces between `cuts` (ascending), then finished, as
/// the next document of `feed`.
fn fed(feed: &mut DocumentFeed, doc: &[u8], cuts: &[usize]) -> Events {
    feed.reset().unwrap();
    let mut events = Vec::new();
    let mut on = |d: &EventData<'_>| {
        events.push(EventPayload::from(d));
        true
    };
    let mut start = 0;
    let mut r = Ok(());
    for &end in cuts.iter().chain(std::iter::once(&doc.len())) {
        r = feed.feed(&doc[start..end], &mut on);
        if r.is_err() {
            break;
        }
        start = end;
    }
    if r.is_ok() {
        r = feed.finish(&mut on);
    }
    (r, events)
}

/// Cut patterns for `len` bytes: every byte its own piece, a few random
/// sizes, and (for a short document) a single cut at every position.
fn cut_patterns(len: usize, rng: &mut Rng) -> Vec<Vec<usize>> {
    let mut out = vec![(1..len).collect::<Vec<_>>(), Vec::new()];
    for max in [3, 17, 4096, 70_000] {
        let mut cuts = Vec::new();
        let mut at = 0;
        loop {
            at += 1 + rng.below(max);
            if at >= len {
                break;
            }
            cuts.push(at);
        }
        out.push(cuts);
    }
    if len <= 1200 {
        out.extend((0..=len).map(|at| vec![at]));
    } else {
        out.extend((0..64).map(|_| vec![rng.below(len + 1)]));
    }
    out
}

#[test]
fn a_fed_document_gives_the_events_of_the_whole_one() {
    let mut rng = Rng(0x9E37_79B9_7F4A_7C15);
    let options = ReadOptions::default();
    let mut feed = DocumentFeed::new(&options).unwrap();
    for (name, doc) in corpus() {
        let want = read_mem(&doc, &options);
        for cuts in cut_patterns(doc.len(), &mut rng) {
            let got = fed(&mut feed, &doc, &cuts);
            assert_eq!(got, want, "{name} cut at {:?}", &cuts[..cuts.len().min(5)]);
        }
    }
}

#[test]
fn a_cut_off_document_fails_as_the_whole_reader_fails() {
    let mut feed = DocumentFeed::new(&ReadOptions::default()).unwrap();
    for (name, doc) in corpus().into_iter().filter(|(_, d)| d.len() <= 1200) {
        for end in 0..doc.len() {
            let prefix = &doc[..end];
            let want = read_mem(prefix, &ReadOptions::default());
            assert_eq!(
                fed(&mut feed, prefix, &[end / 2]),
                want,
                "{name} cut off at {end}"
            );
        }
    }
}

#[test]
fn a_fed_document_takes_the_reader_options_and_policy() {
    let docs = [
        &b".a = 1; .b = @; .c = 3; .d = [1;"[..],
        b".a = 1; .b = 2; .c = 3;",
        b".d = <uint:64,k~m> 3; .e = <float:64,m> 2.5;",
        b".blob = \x00\x01\x02\x00x\x00;",
    ];
    let options = [
        ReadOptions {
            continue_on_error: true,
            ..ReadOptions::default()
        },
        ReadOptions {
            max_file_size: 12,
            ..ReadOptions::default()
        },
        ReadOptions {
            text_only: true,
            ..ReadOptions::default()
        },
    ];
    let policy = UnitPolicy {
        targets: vec![("m".into(), 16)],
        ..UnitPolicy::default()
    };
    let mut rng = Rng(7);
    for o in &options {
        let mut feed = DocumentFeed::new(o).unwrap();
        let mut reader = Reader::new().unwrap();
        for with_policy in [false, true] {
            let p = with_policy.then_some(&policy);
            feed.set_unit_policy(p).unwrap();
            reader.set_unit_policy(p).unwrap();
            for doc in docs {
                let mut events = Vec::new();
                let mut on = |d: &EventData<'_>| {
                    events.push(EventPayload::from(d));
                    true
                };
                let want = (reader.read_mem(doc, o, Some(&mut on), None), events);
                for cuts in cut_patterns(doc.len(), &mut rng) {
                    assert_eq!(fed(&mut feed, doc, &cuts), want, "{doc:?} {o:?}");
                    assert_eq!(feed.recovery_count(), reader.recovery_count());
                    assert_eq!(feed.skipped_bytes(), reader.skipped_bytes());
                }
            }
        }
    }
    let mut feed = DocumentFeed::new(&ReadOptions::default()).unwrap();
    feed.feed(b"#!bovnar 1.1\n.a = 1;", &mut |_| true).unwrap();
    assert_eq!(feed.declared_version(), Some((1, 1)));
}

#[test]
fn a_fed_document_is_over_after_its_finish_or_its_error() {
    let mut feed = DocumentFeed::new(&ReadOptions::default()).unwrap();
    let mut none = |_: &EventData<'_>| true;
    feed.feed(b".a = 1;", &mut none).unwrap();
    feed.finish(&mut none).unwrap();
    assert!(feed.is_finished());
    assert!(matches!(
        feed.feed(b".b = 2;", &mut none),
        Err(Error::Argument { .. })
    ));
    feed.reset().unwrap();
    let e = feed.feed(b".a = @;", &mut none).unwrap_err();
    assert_eq!(e.code(), Some(ErrorCode::UnexpectedInputByte));
    assert_eq!(feed.feed(b".b = 2;", &mut none).unwrap_err(), e);
    assert_eq!(feed.finish(&mut none).unwrap_err(), e);
    feed.reset().unwrap();
    let mut stop = |_: &EventData<'_>| false;
    let e = feed.feed(b".a = 1;", &mut stop).unwrap_err();
    assert_eq!(e.code(), Some(ErrorCode::ScannerCallbackFailed));
}

#[test]
#[cfg_attr(panic = "abort", ignore = "a panic cannot be caught where it aborts")]
fn a_panic_in_a_fed_handler_reaches_the_caller() {
    let mut feed = DocumentFeed::new(&ReadOptions::default()).unwrap();
    let payload = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        let mut boom = |_: &EventData<'_>| -> bool { panic!("boom") };
        let _ = feed.feed(b".a = 1;", &mut boom);
    }))
    .unwrap_err();
    assert_eq!(payload.downcast_ref::<&str>(), Some(&"boom"));
    feed.reset().unwrap();
    let mut n = 0;
    feed.feed(b".a = 1;", &mut |_| {
        n += 1;
        true
    })
    .unwrap();
    assert!(n > 0);
}

/// `entries` as one struct, a repeated key keeping its first place and its
/// last value, as `parse` builds a document.
fn as_document(entries: Vec<(String, Value)>) -> Value {
    let mut out: Vec<(String, Value)> = Vec::new();
    for (k, v) in entries {
        match out.iter_mut().find(|(key, _)| *key == k) {
            Some(slot) => slot.1 = v,
            None => out.push((k, v)),
        }
    }
    Value::Struct(out)
}

#[test]
fn a_value_feed_gives_the_values_of_parse() {
    let mut rng = Rng(42);
    for typed in [false, true] {
        let mut feed = ValueFeed::new(typed, &ReadOptions::default()).unwrap();
        for (name, doc) in corpus() {
            let want = if typed {
                parse_typed(&doc)
            } else {
                parse(&doc)
            };
            let patterns = cut_patterns(doc.len(), &mut rng);
            for cuts in patterns.iter().take(6) {
                let mut got = Vec::new();
                let mut start = 0;
                let mut r = Ok(());
                for &end in cuts.iter().chain(std::iter::once(&doc.len())) {
                    r = feed.feed(&doc[start..end], &mut got);
                    if r.is_err() {
                        break;
                    }
                    start = end;
                }
                if r.is_ok() {
                    r = feed.finish(&mut got);
                }
                feed.reset().unwrap();
                match (&want, r) {
                    // Debug, which shows a NaN as NaN, where == would refuse it.
                    (Ok(v), Ok(())) => {
                        assert_eq!(
                            format!("{:?}", as_document(got)),
                            format!("{v:?}"),
                            "{name}"
                        )
                    }
                    (Err(e), Err(f)) => assert_eq!(e.code(), f.code(), "{name}"),
                    (w, r) => panic!("{name}: parse gave {w:?}, the feed {r:?}"),
                }
            }
        }
    }
}

#[test]
fn a_value_feed_hands_out_each_assignment_when_it_completes() {
    let mut feed = ValueFeed::new(false, &ReadOptions::default()).unwrap();
    let mut got = Vec::new();
    feed.feed(b".a = 1; .s = { .x = 1; ", &mut got).unwrap();
    assert_eq!(got, [("a".to_string(), Value::Int(1))]);
    feed.feed(b"}; .l = [1] / [2]", &mut got).unwrap();
    assert_eq!(got.len(), 2, "the array may still go on: {got:?}");
    feed.feed(b"; .a = 2;", &mut got).unwrap();
    assert_eq!(got.len(), 4, "a repeated key comes again: {got:?}");
    assert_eq!(got[3], ("a".to_string(), Value::Int(2)));
    feed.feed(b".z = @", &mut got).unwrap_err();
    feed.reset().unwrap();
    got.clear();
    feed.feed(b".last = [1, 2];", &mut got).unwrap();
    assert!(got.is_empty());
    feed.finish(&mut got).unwrap();
    assert_eq!(got.len(), 1, "the end of the document completes the array");
}

/// Every message of `doc` from a fed demux, fed in the pieces between
/// `cuts`; the events too with `deliver_events`. The finish's result last.
fn demuxed(
    demux: &mut Demux,
    doc: &[u8],
    cuts: &[usize],
) -> (Messages, Vec<EventPayload>, bovnar::Result<()>) {
    let (mut msgs, mut events) = (Vec::new(), Vec::new());
    let mut start = 0;
    let mut collect = |item: DemuxItem<'_>| match item {
        DemuxItem::Message { channel, payload } => msgs.push((channel, payload.to_vec())),
        DemuxItem::Event(e) => events.push(EventPayload::from(&e.data())),
    };
    let r = (|| {
        for &end in cuts.iter().chain(std::iter::once(&doc.len())) {
            let mut input = &doc[start..end];
            while let Some(item) = demux.next(&mut input)? {
                collect(item);
            }
            assert!(input.is_empty());
            start = end;
        }
        demux.finish()?;
        while let Some(item) = demux.next(&mut &[][..])? {
            collect(item);
        }
        Ok(())
    })();
    demux.reset().unwrap();
    (msgs, events, r)
}

/// What the batch functions give for `doc`: mux_load's messages (or its
/// error), and the reader's events without the multiplexed chunks.
fn batch(doc: &[u8], options: &DemuxOptions) -> (bovnar::Result<Messages>, Vec<EventPayload>) {
    let msgs = stream::mux_load(doc, options.max_message, options.key.as_deref());
    let (_, all) = read_mem(doc, &ReadOptions::default());
    let mut events = Vec::new();
    let (mut key, mut inside) = (Vec::new(), false);
    for e in all {
        match e.event {
            Event::AssignmentStart => key = e.raw.clone(),
            Event::OctetStreamStart => {
                inside = options.key.as_deref().is_none_or(|k| k.as_bytes() == key);
            }
            Event::OctetStreamEnd => inside = false,
            Event::Data if inside => continue,
            _ => {}
        }
        events.push(e);
    }
    (msgs, events)
}

/// A multiplexed document with metadata before the stream and a value
/// after it.
fn mux_document(messages: &[(u64, Vec<u8>)], key: &str) -> Vec<u8> {
    let mut doc = b".meta = { .codec = \"av1\"; .rate = <float:32,Hz> 30.0; };\n".to_vec();
    doc.extend(stream::mux_dump(messages, key).unwrap());
    doc.extend_from_slice(b"\n.after = [1, 2, 3];");
    doc
}

fn sample_messages() -> Vec<(u64, Vec<u8>)> {
    let sizes = [0usize, 1, 150, 30_000, 65_515, 65_516, 65_517, 400_000];
    let channels = [0u64, 1, 127, 128, 1 << 40, u64::MAX];
    sizes
        .iter()
        .enumerate()
        .map(|(i, &n)| {
            let payload = (0..n).map(|b| (b * 7 + i) as u8).collect();
            (channels[i % channels.len()], payload)
        })
        .collect()
}

#[test]
fn a_fed_demux_gives_mux_load_and_the_document_around_the_stream() {
    let mut rng = Rng(3);
    let messages = sample_messages();
    let doc = mux_document(&messages, "media");
    for deliver_events in [false, true] {
        for key in [None, Some("media".to_string())] {
            let options = DemuxOptions {
                key,
                deliver_events,
                ..DemuxOptions::default()
            };
            let (want_msgs, want_events) = batch(&doc, &options);
            assert_eq!(want_msgs.as_ref().unwrap(), &messages);
            let mut demux = Demux::new(&options).unwrap();
            let patterns = cut_patterns(doc.len(), &mut rng);
            for cuts in patterns.iter().take(8) {
                let (msgs, events, r) = demuxed(&mut demux, &doc, cuts);
                r.unwrap();
                assert_eq!(msgs, messages);
                if deliver_events {
                    assert_eq!(events, want_events);
                } else {
                    assert!(events.is_empty());
                }
            }
        }
    }
}

#[test]
fn the_metadata_comes_before_the_first_message() {
    let doc = mux_document(&sample_messages(), "media");
    let options = DemuxOptions {
        deliver_events: true,
        ..DemuxOptions::default()
    };
    let mut demux = Demux::new(&options).unwrap();
    let mut values = ValueBuilder::new(true);
    let mut before = None;
    let mut last_message = 0;
    let mut events_after = 0;
    let mut n = 0;
    for piece in doc.chunks(1000) {
        let mut input = piece;
        while let Some(item) = demux.next(&mut input).unwrap() {
            n += 1;
            match item {
                DemuxItem::Event(e) => {
                    values.push(&e.data());
                    if before.is_some() {
                        events_after = n;
                    }
                }
                DemuxItem::Message { .. } => {
                    if before.is_none() {
                        before = Some(values.drain().collect::<Vec<_>>());
                    }
                    last_message = n;
                }
            }
        }
    }
    demux.finish().unwrap();
    while let Some(item) = demux.next(&mut &[][..]).unwrap() {
        if let DemuxItem::Event(e) = item {
            values.push(&e.data());
        }
    }
    let meta = before.unwrap();
    assert_eq!(
        as_document(meta),
        parse_typed(&doc[..doc.iter().position(|&b| b == b'\n').unwrap()]).unwrap()
    );
    assert!(
        events_after > last_message,
        "the value after the stream follows the messages"
    );
    let after: Vec<_> = values.drain().collect();
    assert_eq!(after.last().unwrap().0, "after");
}

#[test]
fn a_fed_demux_fails_as_mux_load_fails() {
    let messages = sample_messages();
    let good = mux_document(&messages[..4], "mux");
    let many: Vec<(u64, Vec<u8>)> = (0..4097).map(|c| (c, Vec::new())).collect();
    let mut bad_varint = b".mux=\x00\x01\x0b\x00".to_vec();
    bad_varint.extend_from_slice(&[0xFF; 11]);
    bad_varint.extend_from_slice(b"\x00;");
    let mut two_streams = Writer::to_mem(1 << 16, false).unwrap();
    two_streams.write_octets("blob", b"\xFF\xFF\xFF").unwrap();
    two_streams.mux_begin("mux").unwrap();
    two_streams.mux_send(3, b"three").unwrap();
    two_streams.mux_end().unwrap();
    two_streams.finish().unwrap();
    let two_streams = two_streams.get_output().to_vec();
    let mut after_bad = stream::mux_dump(&messages[..2], "mux").unwrap();
    after_bad.extend_from_slice(b".after = @;");

    let cases: Vec<(&str, Vec<u8>, DemuxOptions)> = vec![
        (
            "over max_message",
            good.clone(),
            DemuxOptions {
                max_message: 100,
                ..DemuxOptions::default()
            },
        ),
        (
            "4097 channels",
            stream::mux_dump(&many, "mux").unwrap(),
            DemuxOptions::default(),
        ),
        (
            "4096 channels",
            stream::mux_dump(&many[..4096], "mux").unwrap(),
            DemuxOptions::default(),
        ),
        ("bad varint", bad_varint, DemuxOptions::default()),
        (
            "two streams, keyed",
            two_streams.clone(),
            DemuxOptions {
                key: Some("mux".into()),
                ..DemuxOptions::default()
            },
        ),
        ("two streams, any key", two_streams, DemuxOptions::default()),
        (
            "bad value after the stream",
            after_bad,
            DemuxOptions::default(),
        ),
        (
            "not multiplexed",
            b".a = 1; .b = \"x\";".to_vec(),
            DemuxOptions::default(),
        ),
        ("empty", Vec::new(), DemuxOptions::default()),
    ];
    let mut rng = Rng(11);
    for (name, doc, options) in cases {
        let want = stream::mux_load(&doc, options.max_message, options.key.as_deref());
        let mut demux = Demux::new(&options).unwrap();
        for cuts in cut_patterns(doc.len(), &mut rng).iter().take(4) {
            let (msgs, _, r) = demuxed(&mut demux, &doc, cuts);
            match &want {
                Ok(m) => {
                    assert_eq!(r, Ok(()), "{name}");
                    assert_eq!(&msgs, m, "{name}");
                }
                Err(e) => assert_eq!(&r.unwrap_err(), e, "{name}"),
            }
        }
    }

    // Cut off anywhere: the finish fails as mux_load fails on the prefix.
    let small = mux_document(&[(1, b"hello".to_vec()), (2, vec![9; 300])], "mux");
    let mut demux = Demux::new(&DemuxOptions::default()).unwrap();
    for end in 0..small.len() {
        let prefix = &small[..end];
        let want = stream::mux_load(prefix, 0, None);
        let (msgs, _, r) = demuxed(&mut demux, prefix, &[end / 3]);
        match want {
            Ok(m) => assert_eq!((msgs, r), (m, Ok(())), "cut off at {end}"),
            Err(e) => assert_eq!(r.unwrap_err(), e, "cut off at {end}"),
        }
    }
}

#[test]
fn a_fed_demux_agrees_with_the_codec_on_one_stream_documents() {
    let messages = sample_messages();
    for preamble in [&b""[..], b".meta = { .codec = \"av1\"; };\n"] {
        let mux = MuxWriter::with_preamble(preamble, "media").unwrap();
        let mut doc = Vec::new();
        mux.begin(&mut doc);
        for (ch, p) in &messages {
            mux.write_message(&mut doc, *ch, p);
        }
        mux.finish(&mut doc);
        for piece in [1usize, 777, 65_536, doc.len()] {
            let mut codec = MuxReader::new();
            let mut from_codec = Vec::new();
            for chunk in doc.chunks(piece) {
                let mut input = chunk;
                while let Some(m) = codec.read(&mut input).unwrap() {
                    from_codec.push((m.channel, m.payload.to_vec()));
                }
            }
            codec.finish().unwrap();
            let mut demux = Demux::new(&DemuxOptions::default()).unwrap();
            let cuts: Vec<usize> = (piece..doc.len()).step_by(piece).collect();
            let (from_demux, _, r) = demuxed(&mut demux, &doc, &cuts);
            r.unwrap();
            assert_eq!(from_demux, from_codec);
            assert_eq!(from_demux, messages);
        }
    }
}

#[test]
fn a_fed_demux_refuses_other_input_until_drained() {
    let doc = stream::mux_dump(&[(1, b"one".to_vec()), (2, b"two".to_vec())], "mux").unwrap();
    let mut demux = Demux::new(&DemuxOptions::default()).unwrap();
    let mut input = &doc[..];
    let first = demux.next(&mut input).unwrap();
    assert!(matches!(first, Some(DemuxItem::Message { channel: 1, .. })));
    assert_eq!(
        input.len(),
        doc.len(),
        "the slice is kept until it is drained"
    );
    let other = doc.clone();
    assert!(matches!(
        demux.next(&mut &other[..]),
        Err(Error::Argument { .. })
    ));
    assert!(matches!(demux.finish(), Err(Error::Argument { .. })));
    let second = demux.next(&mut input).unwrap();
    assert!(matches!(
        second,
        Some(DemuxItem::Message {
            channel: 2,
            payload: b"two"
        })
    ));
    assert!(demux.next(&mut input).unwrap().is_none());
    assert!(input.is_empty());
    demux.finish().unwrap();
    // The same demux reads the next document, its channels kept.
    demux.reset().unwrap();
    let mut input = &doc[..];
    let mut n = 0;
    while demux.next(&mut input).unwrap().is_some() {
        n += 1;
    }
    assert_eq!(n, 2);
    demux.finish().unwrap();
}

#[test]
fn a_message_whole_in_the_input_is_not_copied() {
    let doc = stream::mux_dump(&[(1, vec![7; 1000])], "mux").unwrap();
    let mut demux = Demux::new(&DemuxOptions::default()).unwrap();
    let mut input = &doc[..];
    let Some(DemuxItem::Message { payload, .. }) = demux.next(&mut input).unwrap() else {
        panic!("no message")
    };
    let range = doc.as_ptr_range();
    assert!(
        range.contains(&payload.as_ptr()),
        "the payload points into the input"
    );
}

fn framed_corpus() -> (Vec<Vec<u8>>, Vec<u8>) {
    let docs: Vec<Vec<u8>> = vec![
        b".n = 1;".to_vec(),
        Vec::new(),
        b".n = [1;".to_vec(),
        bovnar::to_string(
            &Value::Struct(vec![("blob".into(), Value::Bytes(vec![1; 100_000]))]),
            false,
        )
        .unwrap(),
        b".n = 3;".to_vec(),
    ];
    let frames = stream::frame_documents(&docs).unwrap();
    (docs, frames)
}

#[test]
fn a_frame_pull_hands_out_the_framed_documents() {
    let (docs, frames) = framed_corpus();
    let mut rng = Rng(5);
    for cuts in cut_patterns(frames.len(), &mut rng).iter().take(8) {
        let mut pull = FramePull::new(0).unwrap();
        let mut got = Vec::new();
        let mut start = 0;
        for &end in cuts.iter().chain(std::iter::once(&frames.len())) {
            let mut input = &frames[start..end];
            while let Some(doc) = pull.next(&mut input).unwrap() {
                got.push(doc.to_vec());
            }
            start = end;
        }
        pull.finish().unwrap();
        assert_eq!(got, docs);
        assert_eq!(pull.count(), docs.len() as u64);

        let mut codec = FrameReader::new();
        let mut from_codec = Vec::new();
        let mut start = 0;
        for &end in cuts.iter().chain(std::iter::once(&frames.len())) {
            let mut input = &frames[start..end];
            while let Some(doc) = codec.read(&mut input).unwrap() {
                from_codec.push(doc.to_vec());
            }
            start = end;
        }
        assert_eq!(from_codec, got);
    }
}

#[test]
fn a_frame_pull_fails_as_the_batch_reader_fails() {
    let (_, frames) = framed_corpus();
    let mut bad_magic = frames.clone();
    bad_magic[0] = b'X';
    let pull_all = |data: &[u8], max: u64| -> bovnar::Result<usize> {
        let mut pull = FramePull::new(max)?;
        let mut input = data;
        let mut n = 0;
        while pull.next(&mut input)?.is_some() {
            n += 1;
        }
        pull.finish()?;
        Ok(n)
    };
    let code = |r: bovnar::Result<usize>| r.unwrap_err().code();
    assert_eq!(
        code(pull_all(&bad_magic, 0)),
        Some(ErrorCode::UnexpectedInputByte)
    );
    assert_eq!(
        code(pull_all(&frames, 1000)),
        Some(ErrorCode::ValueOutOfRange)
    );
    for end in [1, 11, 12, 13, frames.len() - 1] {
        assert_eq!(
            code(pull_all(&frames[..end], 0)),
            Some(ErrorCode::GotIncompleteBvnrStream),
            "{end}"
        );
        assert!(stream::load_documents(&frames[..end], false, true, 0).is_err());
    }
    assert_eq!(pull_all(&[], 0), Ok(0));
}

#[test]
fn a_frame_parser_gives_what_load_documents_gives() {
    let (_, frames) = framed_corpus();
    let mut rng = Rng(9);
    for typed in [false, true] {
        for cont in [false, true] {
            let want = stream::load_documents(&frames, typed, cont, 0);
            for cuts in cut_patterns(frames.len(), &mut rng).iter().take(6) {
                let mut parser = FrameParser::new(&ReadOptions::default(), cont, 0).unwrap();
                let mut builder = ValueBuilder::new(typed);
                let mut docs: Vec<Option<Value>> = Vec::new();
                let mut start = 0;
                let mut r = Ok(());
                for &end in cuts.iter().chain(std::iter::once(&frames.len())) {
                    let builder = std::cell::RefCell::new(&mut builder);
                    let mut on_event = |d: &EventData<'_>| {
                        builder.borrow_mut().push(d);
                        true
                    };
                    let mut on_document = |_: u64, ok: bovnar::Result<()>| {
                        let mut b = builder.borrow_mut();
                        let entries: Vec<_> = b.drain().collect();
                        b.reset();
                        docs.push(ok.ok().map(|()| as_document(entries)));
                        true
                    };
                    r = parser.feed(&frames[start..end], &mut on_event, &mut on_document);
                    if r.is_err() {
                        break;
                    }
                    start = end;
                }
                if r.is_ok() {
                    r = parser.finish();
                }
                match &want {
                    Ok(w) => {
                        r.unwrap();
                        assert_eq!(&docs, w);
                    }
                    Err(e) => assert_eq!(r.unwrap_err().code(), e.code()),
                }
            }
        }
    }
}

#[test]
fn an_embedded_feed_reads_what_parse_embedded_reads() {
    let inners: Vec<Vec<u8>> = vec![
        b".n = 1;".to_vec(),
        b".n = @;".to_vec(),
        Vec::new(),
        bovnar::to_string(
            &Value::Struct(vec![("blob".into(), Value::Bytes(vec![2; 150_000]))]),
            false,
        )
        .unwrap(),
        b".s = { .t = \"x\"; };".to_vec(),
    ];
    let mut w = Writer::to_mem(1 << 20, false).unwrap();
    w.write_string("before", "x").unwrap();
    for inner in &inners {
        w.embed_document("payload", inner).unwrap();
    }
    w.write_octets("other", b"\x00\x01").unwrap();
    w.begin_struct("nested").unwrap();
    w.embed_document("payload", b".deep = true;").unwrap();
    w.end_struct().unwrap();
    w.finish().unwrap();
    let outer = w.get_output().to_vec();
    let mut want: Vec<_> = inners
        .iter()
        .map(|i| stream::parse_embedded(i, true))
        .collect();
    want.push(stream::parse_embedded(b".deep = true;", true));

    let mut rng = Rng(13);
    for cuts in cut_patterns(outer.len(), &mut rng).iter().take(6) {
        let mut embedded = EmbeddedFeed::new("payload", true).unwrap();
        let mut got = Vec::new();
        let mut on_event = |d: &EventData<'_>| {
            got.extend(embedded.push(d));
            true
        };
        let mut outer_feed = DocumentFeed::new(&ReadOptions::default()).unwrap();
        let mut start = 0;
        for &end in cuts.iter().chain(std::iter::once(&outer.len())) {
            outer_feed.feed(&outer[start..end], &mut on_event).unwrap();
            start = end;
        }
        outer_feed.finish(&mut on_event).unwrap();
        assert_eq!(got.len(), want.len());
        for (g, w) in got.iter().zip(&want) {
            match (g, w) {
                (Ok(g), Ok(w)) => assert_eq!(g, w),
                (Err(g), Err(w)) => assert_eq!(g.code(), w.code()),
                _ => panic!("{g:?} vs {w:?}"),
            }
        }
    }
}
