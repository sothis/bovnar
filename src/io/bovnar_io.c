#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include "bovnar.h"
static bool pull_fd(bvnr_source_t* s, void* buf, uint32_t want, uint32_t* got)
{
	ssize_t n;
	do {
		n = read(s->fd, buf, want);
	} while (n < 0 && errno == EINTR);
	if (n < 0) { *got = 0; return false; }
	*got = (uint32_t)n;
	return true;
}
static bool pull_mem(bvnr_source_t* s, void* buf, uint32_t want, uint32_t* got)
{
	uint64_t avail = s->mem_left;
	uint32_t n = avail < (uint64_t)want ? (uint32_t)avail : want;
	if (n) {
		memcpy(buf, s->mem_ptr, n);
		s->mem_ptr  += n;
		s->mem_left -= n;
	}
	*got = n;
	return true;
}
static bool push_fd(bvnr_sink_t* s, const void* buf, uint32_t len)
{
	while (len) {
		ssize_t n = write(s->fd, buf, len);
		if (n < 0) {
			if (errno == EINTR) continue;
			return false;
		}
		if (n == 0) {
			errno = ENOSPC;
			return false;
		}
		s->mem_written += (uint32_t)n;
		buf = (const uint8_t*)buf + n;
		len -= (uint32_t)n;
	}
	return true;
}
static bool flush_fd (bvnr_sink_t* s) { (void)s; return true; }
static bool flush_mem(bvnr_sink_t* s) { (void)s; return true; }
static bool push_mem(bvnr_sink_t* s, const void* buf, uint32_t len)
{
	if (len > s->mem_left) return false;
	memcpy(s->mem_ptr, buf, len);
	s->mem_ptr    += len;
	s->mem_left   -= len;
	s->mem_written += len;
	return true;
}
void bvnr_source_from_fd(bvnr_source_t* s, int fd)
{
	memset(s, 0, sizeof(*s));
	s->pull = pull_fd;
	s->fd   = fd;
}
void bvnr_source_from_mem(bvnr_source_t* s, const void* buf, uint64_t len)
{
	memset(s, 0, sizeof(*s));
	s->pull     = pull_mem;
	s->mem_ptr  = (const uint8_t*)buf;
	s->mem_left = len;
}
void bvnr_sink_to_fd(bvnr_sink_t* s, int fd)
{
	memset(s, 0, sizeof(*s));
	s->push  = push_fd;
	s->flush = flush_fd;
	s->fd    = fd;
}
void bvnr_sink_to_mem(bvnr_sink_t* s, void* buf, uint32_t cap)
{
	memset(s, 0, sizeof(*s));
	s->push        = push_mem;
	s->flush       = flush_mem;
	s->is_mem      = true;
	s->mem_ptr     = (uint8_t*)buf;
	s->mem_left    = cap;
	s->mem_written = 0;
}
uint64_t bvnr_sink_bytes_written(const bvnr_sink_t* s)
{
	return s->mem_written;
}
