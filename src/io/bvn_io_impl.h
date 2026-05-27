#ifndef BVN_IO_IMPL_H_
#define BVN_IO_IMPL_H_
#include "bovnar.h"
#include <stdbool.h>
#include <stdint.h>
typedef struct bvn_source_impl_s {
	bvnr_pull_fn	pull;
	int		fd;
	const uint8_t*	mem_ptr;
	uint64_t	mem_left;
} bvn_source_impl_t;
typedef struct bvn_sink_impl_s {
	bvnr_push_fn	push;
	bvnr_flush_fn	flush;
	int		fd;
	bool		is_mem;
	uint8_t*	mem_ptr;
	uint64_t	mem_left;
	uint64_t	mem_written;
} bvn_sink_impl_t;
typedef char bvn_source_size_check_[
	sizeof(bvn_source_impl_t) <= sizeof(bvnr_source_t) ? 1 : -1];
typedef char bvn_sink_size_check_[
	sizeof(bvn_sink_impl_t)   <= sizeof(bvnr_sink_t)   ? 1 : -1];
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
