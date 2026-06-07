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

#ifndef BVN_IO_IMPL_H_
#define BVN_IO_IMPL_H_
#include "bovnar.h"
#include <stdbool.h>
#include <stdint.h>
/*
 * Private layout of the source/sink handles.
 *
 * The public header exposes bvnr_source_t / bvnr_sink_t only as fixed-size
 * opaque blobs (a reserved byte array, see BVNR_SOURCE_RESERVED_SIZE). That
 * keeps the ABI stable: callers can place a source/sink on the stack or embed
 * it in their own structs without ever seeing — or being able to break on —
 * the real fields. Here we define the real layout and cast the opaque blob to
 * it. The static_assert-style size_check typedefs below guarantee at compile
 * time that the private struct still fits inside the public blob, so the cast
 * can never overflow the caller's storage.
 *
 * BVN_MAY_ALIAS: the impl structs deliberately reinterpret the storage of the
 * public union blob (a different declared type), so accessing the blob through
 * a bvn_*_impl_t* is a type-punning access. Without telling the compiler, the
 * type-based alias analysis (TBAA, active at -O2/-O3) is entitled to assume the
 * union store (e.g. `w->ser.sink = *sink;`) and the impl-typed loads of fields
 * like `is_mem`/`mem_left` never alias, and may drop the stores — which silently
 * miscompiles a memory sink. Separate compilation hid this (the optimiser cannot
 * co-analyse store and load across the opaque call boundary); the single-file
 * amalgamation at -O2 exposed it. Marking the impl types may_alias makes every
 * access through them alias-permissive, the standard fix for ABI-stable opaque
 * storage reinterpreted as a concrete struct. MSVC does not do this style of
 * TBAA and has no equivalent attribute, so it is a no-op there.
 */
#if defined(__GNUC__) || defined(__clang__)
#  define BVN_MAY_ALIAS __attribute__((__may_alias__))
#else
#  define BVN_MAY_ALIAS
#endif
typedef struct bvn_source_impl_s {
	bvnr_pull_fn	pull;
	int		fd;
	const uint8_t*	mem_ptr;
	uint64_t	mem_left;
} BVN_MAY_ALIAS bvn_source_impl_t;
typedef struct bvn_sink_impl_s {
	bvnr_push_fn	push;
	bvnr_flush_fn	flush;
	int		fd;
	bool		is_mem;
	uint8_t*	mem_ptr;
	uint64_t	mem_left;
	uint64_t	mem_written;
} BVN_MAY_ALIAS bvn_sink_impl_t;
/*
 * Compile-time size guards: a negative array length is a hard error, so the
 * build fails immediately if the private impl ever grows past the reserved
 * size in the public header — a clear signal to bump BVNR_*_RESERVED_SIZE
 * rather than silently corrupting caller memory at runtime.
 */
typedef char bvn_source_size_check_[
	sizeof(bvn_source_impl_t) <= sizeof(bvnr_source_t) ? 1 : -1];
typedef char bvn_sink_size_check_[
	sizeof(bvn_sink_impl_t)   <= sizeof(bvnr_sink_t)   ? 1 : -1];
/*
 * The reinterpret-cast accessors. Going through these named helpers (rather
 * than scattering casts everywhere) keeps the aliasing in one place and
 * documents intent. The pull/push wrappers below dispatch through the stored
 * function pointer so the rest of the library is agnostic to fd vs. memory
 * backing.
 */
static inline bvn_source_impl_t* bvn_source_impl(bvnr_source_t* s)
	{ return (bvn_source_impl_t*)(void*)s; }
static inline const bvn_source_impl_t* bvn_source_impl_c(const bvnr_source_t* s)
	{ return (const bvn_source_impl_t*)(const void*)s; }
static inline bvn_sink_impl_t* bvn_sink_impl(bvnr_sink_t* s)
	{ return (bvn_sink_impl_t*)(void*)s; }
static inline const bvn_sink_impl_t* bvn_sink_impl_c(const bvnr_sink_t* s)
	{ return (const bvn_sink_impl_t*)(const void*)s; }
static inline bool bvn_source_pull(
	bvnr_source_t* s, void* buf, uint32_t want, uint32_t* got)
{
	return bvn_source_impl(s)->pull(s, buf, want, got);
}
static inline bool bvn_sink_push(
	bvnr_sink_t* s, const void* buf, uint32_t len)
{
	return bvn_sink_impl(s)->push(s, buf, len);
}
#endif
