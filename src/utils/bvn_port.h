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

#ifndef BVN_PORT_H_
#define BVN_PORT_H_

/*
 * Platform portability shim for the file-descriptor I/O backends and the CLI's
 * benchmark timing. On POSIX this is a thin pass-through to <unistd.h>,
 * <fcntl.h>, and <time.h>; on Windows (MinGW64 and MSVC, 64-bit only) it maps
 * the low-level fd calls onto the C runtime, forces binary mode (a binary format
 * must never be subjected to the CRT's CRLF text translation), and supplies
 * monotonic / CPU clocks.
 *
 * Only the library's own translation units include this header — it is never
 * part of the public API — so the read()/write()/open()/close() remaps below
 * stay confined to this build and cannot leak into a consumer's namespace.
 */

#if defined(_WIN32)

#  if !defined(_WIN64)
#    error "Bovnar on Windows supports 64-bit builds only"
#  endif

#  include <io.h>
#  include <fcntl.h>
#  include <stdio.h>     /* _fileno */
#  include <limits.h>    /* UINT_MAX for the read/write count clamp below */

#  if defined(_MSC_VER)
#    include <basetsd.h>
typedef SSIZE_T ssize_t;   /* MSVC has no ssize_t */
typedef __int64 off_t;     /* MSVC has no off_t; 64-bit to match _lseeki64 and
                            * keep the CLI's 4 GiB file-size check honest. */
/* MSVC spells the POSIX fd calls with a leading underscore. All call sites in
 * this library use the bare names as plain libc calls (no member/identifier
 * collisions), so remapping them here is safe and keeps the I/O code single
 * source. read/write go through the casting wrappers below. */
#    define open  _open
#    define close _close
#    define lseek _lseeki64
#  else
/* MinGW supplies the POSIX fd names directly, but its default off_t/lseek are
 * 32-bit (long), so a >2 GiB seek wraps or fails and the CLI's 4 GiB file-size
 * guard ((uint64_t)sz > UINT32_MAX) can never fire — diverging from the MSVC
 * path. Force a 64-bit off_t and the wide seek so both Windows toolchains agree.
 * The library calls lseek only to size a file (SEEK_END/SEEK_SET), so remapping
 * it to _lseeki64 is safe. */
#    include <unistd.h>
#    undef  off_t
#    define off_t __int64
#    undef  lseek
#    define lseek _lseeki64
#  endif

/* Both Windows CRTs spell low-level read/write with a leading underscore and
 * take an unsigned int byte count, while the library passes a size_t. Wrap them
 * so the (buffer-bounded) count converts explicitly — silencing the MSVC C4267
 * and GCC -Wconversion "size_t -> unsigned int" warnings without weakening the
 * check for the rest of the code. static inline, so an unused copy in a TU that
 * never does fd I/O does not warn. */
static inline int bvn_port_read(int fd, void *buf, size_t n)
{
	return _read(fd, buf, n > (size_t)UINT_MAX ? UINT_MAX : (unsigned int)n);
}
static inline int bvn_port_write(int fd, const void *buf, size_t n)
{
	return _write(fd, buf, n > (size_t)UINT_MAX ? UINT_MAX : (unsigned int)n);
}
#  define read  bvn_port_read
#  define write bvn_port_write

#  ifndef O_BINARY
#    define O_BINARY _O_BINARY
#  endif
#  ifndef STDIN_FILENO
#    define STDIN_FILENO  0
#  endif
#  ifndef STDOUT_FILENO
#    define STDOUT_FILENO 1
#  endif
#  ifndef STDERR_FILENO
#    define STDERR_FILENO 2
#  endif

/* Console code-page control for the CLI's human-readable output (see
 * bvn_set_binary_stdio). Forward-declared here so this header need not drag
 * <windows.h> into the I/O translation units; the signatures match the Win32
 * headers exactly, so the later <windows.h> include (under BVN_PORT_TIMING)
 * re-declares them identically without conflict. */
#  ifndef CP_UTF8
#    define CP_UTF8 65001
#  endif
__declspec(dllimport) int __stdcall SetConsoleOutputCP(unsigned int wCodePageID);
__declspec(dllimport) int __stdcall SetConsoleCP(unsigned int wCodePageID);

#else  /* POSIX */

#  include <unistd.h>
#  include <fcntl.h>
#  ifndef O_BINARY
#    define O_BINARY 0   /* POSIX has no text/binary distinction */
#  endif

#endif

/* Open flag for reading the (binary) format. Identical to O_RDONLY on POSIX;
 * adds O_BINARY on Windows so reads are byte-exact. */
#define BVN_O_RDONLY (O_RDONLY | O_BINARY)

/*
 * Put the standard streams into binary mode so the CLI's byte stream is not
 * mangled by CRLF translation, and switch the Windows console to UTF-8. No-op
 * on POSIX. Call once at program start, before any fd or stdio I/O.
 */
static inline void bvn_set_binary_stdio(void)
{
#if defined(_WIN32)
	(void)_setmode(_fileno(stdin),  _O_BINARY);
	(void)_setmode(_fileno(stdout), _O_BINARY);
	(void)_setmode(_fileno(stderr), _O_BINARY);
	/* The CLI's decorative UTF-8 UI (events table, bench tables, box-drawing
	 * rules) goes to a real console via WriteConsoleW (see cw_write in
	 * bovnar.c), which is code-page independent and renders correctly on both
	 * Windows and Wine. Pinning the console code pages to UTF-8 here is a
	 * best-effort fallback for the *data* subcommands that still write raw
	 * bytes with stdio (e.g. a UTF-8 string printed by `query` straight to a
	 * console): a console otherwise defaults to its OEM code page (CP437/CP850)
	 * and would garble those bytes. Harmless when the streams are redirected
	 * (SetConsoleOutputCP simply fails on a non-console handle). */
	(void)SetConsoleOutputCP(CP_UTF8);
	(void)SetConsoleCP(CP_UTF8);
#endif
}

/*
 * Monotonic / per-thread-CPU clocks in seconds, for the bench command only.
 * Guarded so the library I/O translation units do not drag in <windows.h>.
 */
#if defined(BVN_PORT_TIMING)

#  if defined(_WIN32)
#    ifndef WIN32_LEAN_AND_MEAN
#      define WIN32_LEAN_AND_MEAN
#    endif
#    ifndef NOMINMAX
#      define NOMINMAX
#    endif
#    include <windows.h>
#  else
#    include <time.h>
#  endif

static inline double bvn_monotonic_sec(void)
{
#if defined(_WIN32)
	LARGE_INTEGER freq, cnt;
	QueryPerformanceFrequency(&freq);
	QueryPerformanceCounter(&cnt);
	return (double)cnt.QuadPart / (double)freq.QuadPart;
#else
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
#endif
}

static inline double bvn_cpu_sec(void)
{
#if defined(_WIN32)
	FILETIME creation, exit, kernel, user;
	if (!GetThreadTimes(GetCurrentThread(), &creation, &exit, &kernel, &user))
		return 0.0;
	ULARGE_INTEGER k, u;
	k.LowPart = kernel.dwLowDateTime; k.HighPart = kernel.dwHighDateTime;
	u.LowPart = user.dwLowDateTime;   u.HighPart = user.dwHighDateTime;
	/* FILETIME is in 100-nanosecond ticks. */
	return (double)(k.QuadPart + u.QuadPart) * 1e-7;
#else
	struct timespec ts;
	clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
	return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
#endif
}

#endif /* BVN_PORT_TIMING */

#endif /* BVN_PORT_H_ */
