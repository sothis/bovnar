/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2026 Janos Sonntag
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <errno.h>
#include <stdint.h>
#include <string.h>
#include "bvn_port.h"
#include "bovnar.h"
#include "bvn_io_impl.h"

#if !defined(_WIN32)
#  include <poll.h>
/*
 * Block until `fd` is ready for `events` (POLLIN/POLLOUT), retrying across
 * signals. This turns a non-blocking fd's EAGAIN/EWOULDBLOCK into a wait, so a
 * socket source/sink left in non-blocking mode still makes progress instead of
 * failing the whole parse/emit. Returns false only on a hard poll error or
 * POLLERR/POLLNVAL. On Windows the fd backend drives file descriptors (which
 * block), so no equivalent is needed there.
 */
static bool bvn_fd_wait(int fd, short events)
{
	struct pollfd pfd;
	int r;
	pfd.fd      = fd;
	pfd.events  = events;
	pfd.revents = 0;
	do {
		r = poll(&pfd, 1, -1);
	} while (r < 0 && errno == EINTR);
	return r > 0 && !(pfd.revents & (POLLERR | POLLNVAL));
}
#endif
/*
 * Source/sink backends.
 *
 * The reader and writer never touch a file descriptor or a memory buffer
 * directly; they only call the pull/push/flush function pointers installed
 * here. That single indirection is what lets the same parser drive a socket,
 * a regular file, an in-memory buffer, or a caller-supplied custom transport
 * with no special-casing in the core. This file provides the two backends
 * that ship with the library: a POSIX fd backend and a fixed memory buffer
 * backend.
 */

/*
 * fd source: read up to `want` bytes. read() may legitimately return fewer
 * bytes than requested (short read), so *got reports the actual count and the
 * caller loops as needed — we never assume a full fill. The EINTR retry loop
 * makes the pull robust against signals interrupting a blocking read, which is
 * otherwise a spurious failure on a perfectly good fd.
 */
static bool pull_fd(bvnr_source_t* s, void* buf, uint32_t want, uint32_t* got)
{
	bvn_source_impl_t* impl = bvn_source_impl(s);
	ssize_t n;
	for (;;) {
		n = read(impl->fd, buf, want);
		if (n >= 0) break;
		if (errno == EINTR) continue;
#if !defined(_WIN32)
		if ((errno == EAGAIN || errno == EWOULDBLOCK) &&
		    bvn_fd_wait(impl->fd, POLLIN))
			continue;
#endif
		*got = 0;
		return false;
	}
	*got = (uint32_t)n;
	return true;
}
/*
 * Memory source: hand out bytes from a caller-owned buffer. This is exposed
 * (non-static, declared in the lexer impl header) because the lexer needs to
 * recognise the in-memory case to enable zero-copy fast paths — when the input
 * already lives in RAM there is no point copying it through an intermediate
 * read buffer. Always succeeds; a short count simply signals end-of-input.
 */
bool bvn_pull_mem(bvnr_source_t* s, void* buf, uint32_t want, uint32_t* got)
{
	bvn_source_impl_t* impl = bvn_source_impl(s);
	uint64_t avail = impl->mem_left;
	uint32_t n = avail < (uint64_t)want ? (uint32_t)avail : want;
	if (n) {
		memcpy(buf, impl->mem_ptr, n);
		impl->mem_ptr  += n;
		impl->mem_left -= n;
	}
	*got = n;
	return true;
}
/*
 * fd sink: write exactly `len` bytes or fail. Unlike the pull side, the writer
 * contract is all-or-nothing, so we loop until the whole buffer is drained —
 * write() can accept a partial chunk on a pipe/socket. EINTR is retried; a
 * write() of 0 is treated as a full disk (ENOSPC) because there is no other
 * way to make progress. mem_written is tracked so callers can report how many
 * bytes were emitted even for a streaming fd.
 */
static bool push_fd(bvnr_sink_t* s, const void* buf, uint32_t len)
{
	bvn_sink_impl_t* impl = bvn_sink_impl(s);
	while (len) {
		ssize_t n = write(impl->fd, buf, len);
		if (n < 0) {
			if (errno == EINTR) continue;
#if !defined(_WIN32)
			if ((errno == EAGAIN || errno == EWOULDBLOCK) &&
			    bvn_fd_wait(impl->fd, POLLOUT))
				continue;
#endif
			return false;
		}
		if (n == 0) {
			errno = ENOSPC;
			return false;
		}
		impl->mem_written += (uint64_t)n;
		buf = (const uint8_t*)buf + n;
		len -= (uint32_t)n;
	}
	return true;
}
/*
 * Both backends flush eagerly (push_fd writes straight through, push_mem
 * copies straight in), so there is no buffered state to drain. flush is a
 * no-op kept only to satisfy the sink interface, so a generic caller can call
 * flush uniformly without knowing the backing type.
 */
static bool flush_fd (bvnr_sink_t* s) { (void)s; return true; }
static bool flush_mem(bvnr_sink_t* s) { (void)s; return true; }
/*
 * Memory sink: copy into a caller-supplied fixed buffer. Refuses to write past
 * `mem_left` rather than truncating, so an overflow is reported as a hard
 * failure (error_sink_buffer_exhausted upstream) instead of silently producing
 * a corrupt, partial stream.
 */
static bool push_mem(bvnr_sink_t* s, const void* buf, uint32_t len)
{
	bvn_sink_impl_t* impl = bvn_sink_impl(s);
	if (len > impl->mem_left) return false;
	memcpy(impl->mem_ptr, buf, len);
	impl->mem_ptr    += len;
	impl->mem_left   -= len;
	impl->mem_written += len;
	return true;
}
/*
 * Public constructors. Each zeroes the whole opaque blob first (so unused
 * reserved bytes are deterministic) and then installs the backend's function
 * pointers plus its state. After this the handle is fully self-contained and
 * can be passed to the reader/writer; the library reaches the backend only
 * through the stored pointers.
 */
void bvnr_source_from_fd(bvnr_source_t* s, int fd)
{
	memset(s, 0, sizeof(*s));
	bvn_source_impl_t* impl = bvn_source_impl(s);
	impl->pull = pull_fd;
	impl->fd   = fd;
}
void bvnr_source_from_mem(bvnr_source_t* s, const void* buf, uint64_t len)
{
	memset(s, 0, sizeof(*s));
	bvn_source_impl_t* impl = bvn_source_impl(s);
	impl->pull     = bvn_pull_mem;
	impl->mem_ptr  = (const uint8_t*)buf;
	impl->mem_left = len;
}
void bvnr_sink_to_fd(bvnr_sink_t* s, int fd)
{
	memset(s, 0, sizeof(*s));
	bvn_sink_impl_t* impl = bvn_sink_impl(s);
	impl->push  = push_fd;
	impl->flush = flush_fd;
	impl->fd    = fd;
}
void bvnr_sink_to_mem(bvnr_sink_t* s, void* buf, uint64_t cap)
{
	memset(s, 0, sizeof(*s));
	bvn_sink_impl_t* impl = bvn_sink_impl(s);
	impl->push        = push_mem;
	impl->flush       = flush_mem;
	impl->is_mem      = true;
	impl->mem_ptr     = (uint8_t*)buf;
	impl->mem_left    = cap;
	impl->mem_written = 0;
}
/*
 * Byte counter, valid for both sink kinds. For the memory sink it doubles as
 * the produced length; for an fd sink it is the running total actually handed
 * to write(). Lets callers size/verify output without re-measuring it.
 */
uint64_t bvnr_sink_bytes_written(const bvnr_sink_t* s)
{
	return bvn_sink_impl_c(s)->mem_written;
}
