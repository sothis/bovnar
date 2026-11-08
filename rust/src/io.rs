// SPDX-License-Identifier: MIT
//! Reading a caller's `std::io::Read` for the `*_reader` entry points, the
//! counterpart of the C library's file-descriptor source.
//!
//! The input streams: the library pulls from `input` through a callback
//! source (`bvnr_source_from_callback`) as it parses, so memory stays flat
//! however long the input is. Only the DOM, which builds the whole document,
//! reads its input into memory first ([`read_capped`]).

use std::io::Read;

use crate::enums::ErrorCode;
use crate::error::{Error, Result};
use crate::ffi;

/// Runs `parse` on a source that pulls from `input`. A read that failed is
/// the result, an [`Error::Io`] with `reading_from_source_fd`, the code the C
/// fd source gives, whatever `parse` made of the input it had.
pub(crate) fn from_reader<R>(
    input: &mut dyn Read,
    parse: impl FnOnce(&mut ffi::BvnrSource) -> Result<R>,
) -> Result<R> {
    ffi::with_source(input, parse).map_err(|e| Error::io(ErrorCode::ReadingFromSourceFd, &e))?
}

/// Reads `input` to its end, or to one byte past `cap` when there is one,
/// so that a caller can tell an input over the cap from one at it. An I/O
/// error is `reading_from_source_fd`, the code the C fd source gives.
pub(crate) fn read_capped(mut input: impl Read, cap: Option<u64>) -> Result<Vec<u8>> {
    let mut data = Vec::new();
    let result = match cap {
        Some(cap) => input
            .by_ref()
            .take(cap.saturating_add(1))
            .read_to_end(&mut data),
        None => input.read_to_end(&mut data),
    };
    match result {
        Ok(_) => Ok(data),
        Err(e) => Err(Error::io(ErrorCode::ReadingFromSourceFd, &e)),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn reads_everything_without_a_cap() {
        assert_eq!(read_capped(&b"abcdef"[..], None).unwrap(), b"abcdef");
    }

    #[test]
    fn reads_one_byte_past_the_cap() {
        assert_eq!(read_capped(&b"abcdef"[..], Some(6)).unwrap(), b"abcdef");
        assert_eq!(read_capped(&b"abcdef"[..], Some(3)).unwrap(), b"abcd");
        assert_eq!(
            read_capped(std::io::repeat(b'x'), Some(9)).unwrap().len(),
            10
        );
    }

    struct Failing;

    impl Read for Failing {
        fn read(&mut self, _: &mut [u8]) -> std::io::Result<usize> {
            Err(std::io::Error::other("disk on fire"))
        }
    }

    #[test]
    fn an_io_error_keeps_its_kind_and_message() {
        for err in [
            read_capped(Failing, None).unwrap_err(),
            crate::parse_reader(Failing).unwrap_err(),
        ] {
            assert_eq!(err.kind(), Some(ErrorCode::ReadingFromSourceFd));
            assert_eq!(
                err,
                Error::Io {
                    code: ErrorCode::ReadingFromSourceFd,
                    io_kind: std::io::ErrorKind::Other,
                    message: "disk on fire".into(),
                }
            );
            assert_eq!(
                err.to_string(),
                "I/O error: reading_from_source_fd (disk on fire)"
            );
        }
    }
}
