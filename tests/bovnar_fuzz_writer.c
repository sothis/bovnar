#define _POSIX_C_SOURCE 200809L
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bovnar.h"
#include "bvn_internal_dims.h"

#define WRITE_CAP   4096u

#define MAX_EVENTS  64u

typedef struct {
	const uint8_t *p;
	size_t         remaining;
} cursor_t;

static uint8_t  cur_u8  (cursor_t *c)           { return c->remaining ? (c->remaining--, *c->p++) : 0; }
static uint16_t cur_u16 (cursor_t *c)           { uint16_t v = cur_u8(c); v |= (uint16_t)cur_u8(c) << 8; return v; }
static uint32_t cur_u32 (cursor_t *c)           { uint32_t v = cur_u16(c); v |= (uint32_t)cur_u16(c) << 16; return v; }

static uint32_t cur_str(cursor_t *c, char *dst, uint32_t len)
{
	uint32_t got = 0;
	while (got < len && c->remaining) {
		dst[got++] = (char)*c->p++;
		c->remaining--;
	}

	if (got == 0) { dst[got++] = 'k'; }
	dst[got] = '\0';
	return got;
}

static const value_type_family_t FAMILIES[] = {
	vt_plain, vt_utf8, vt_sint, vt_uint, vt_float
};
#define N_FAMILIES (sizeof(FAMILIES) / sizeof(FAMILIES[0]))

static const uint32_t WIDTHS[]  = { 0, 8, 16, 32, 64, 128 };
static const uint32_t BASES[]   = { 0, 2, 8, 10, 16, 36, 62, 64, 85 };
#define N_WIDTHS (sizeof(WIDTHS) / sizeof(WIDTHS[0]))
#define N_BASES  (sizeof(BASES)  / sizeof(BASES[0]))

static const token_type_t TOKENS[] = {
	token_is_number,
	token_is_string,
	token_is_symbol,
	token_is_reference,
	token_is_array_number,
	token_is_array_string,
	token_is_octet_stream,
	token_is_null_value,
	token_is_identifier,
};
#define N_TOKENS (sizeof(TOKENS) / sizeof(TOKENS[0]))

static const bvnr_event_t EVENTS[] = {
	ev_assignment_start,
	ev_type_annotation_start,
	ev_type_annotation_type_family,
	ev_type_annotation_type_family_parameter,
	ev_type_annotation_end,
	ev_data,
	ev_struct_start,
	ev_struct_end,
	ev_array_row_start,
	ev_array_row_end,
	ev_array_dim_start,
	ev_octet_stream_start,
	ev_octet_stream_end,
};
#define N_EVENTS (sizeof(EVENTS) / sizeof(EVENTS[0]))

static bool readback_on_verified(void *ud, bvnr_event_t ev, bvnr_data_t *d)
{
	(void)ud; (void)ev; (void)d;
	return true;
}

static bool readback_validate(const uint8_t *buf, uint32_t len)
{
	bvnr_reader_t *r = bvnr_reader_create();
	if (!r) return true;

	bvnr_read_flags_t flags;
	memset(&flags, 0, sizeof(flags));
	flags.on_verified = readback_on_verified;

	bool ok = bvnr_open_read_mem(r, buf, len, NULL, 0, &flags)
		   && bvnr_read(r);
	bvnr_reader_destroy(r);
	return ok;
}

static void harness_random_events(const uint8_t *data, size_t len)
{
	cursor_t cur = { data, len };

	uint8_t  cfg      = cur_u8(&cur);
	bool     pretty   = (cfg >> 0) & 1u;
	bool     tiny     = (cfg >> 1) & 1u;
	uint8_t  n_events = cur_u8(&cur);
	if (n_events > MAX_EVENTS) n_events = MAX_EVENTS;

	uint32_t cap = tiny ? (uint32_t)(cur_u8(&cur) + 1u) : WRITE_CAP;

	uint8_t *outbuf = malloc(cap + 1u);
	if (!outbuf) return;

	bvnr_writer_t *w = bvnr_writer_create();
	if (!w) { free(outbuf); return; }

	if (!bvnr_open_write_mem(w, outbuf, cap, pretty, NULL))
		goto done;

	{
		bvnr_data_t hdr; memset(&hdr, 0, sizeof(hdr));
		(void)bvnr_write_event(w, ev_stream_start, &hdr);
	}

	for (uint8_t i = 0; i < n_events; i++) {
		uint8_t ev_idx = cur_u8(&cur) % N_EVENTS;
		bvnr_event_t ev = EVENTS[ev_idx];

		uint8_t  fam_idx = cur_u8(&cur) % N_FAMILIES;
		uint8_t  wid_idx = cur_u8(&cur) % N_WIDTHS;
		uint8_t  bas_idx = cur_u8(&cur) % N_BASES;
		uint8_t  tok_idx = cur_u8(&cur) % N_TOKENS;

		value_type_spec_t vt = {
			.family = FAMILIES[fam_idx],
			.width  = WIDTHS[wid_idx],
			.base   = BASES[bas_idx],
		};

		value_unit_t vu;
		memset(&vu, 0, sizeof(vu));
		uint8_t use_unit = cur_u8(&cur);
		if (use_unit & 1u) {
			static const value_base_unit_t BU[] = {
				bu_meter, bu_second, bu_gram, bu_kelvin,
				bu_byte, bu_hertz, bu_celsius, bu_none,
			};
			static const si_prefix_id_t PFX[] = {
				si_none, si_milli, si_kilo, si_mega, si_micro,
			};
			vu.num_components = 1;
			vu.components[0].base = BU[(use_unit >> 1) % 8];
			vu.components[0].exponent = exp_linear;
			vu.components[0].prefix.system = prefix_si;
			vu.components[0].prefix.id.si  = PFX[(use_unit >> 4) % 5];
		}

		char payload[256];
		uint8_t plen_byte = cur_u8(&cur);
		uint32_t plen = (uint32_t)(plen_byte & 0x3Fu) + 1u;
		uint32_t got  = cur_str(&cur, payload, plen);

		bvnr_data_t d;
		memset(&d, 0, sizeof(d));
		d.value_type = vt;
		d.value_unit = vu;
		d.data       = payload;
		d.length     = got;
		d.type       = TOKENS[tok_idx];

		(void)bvnr_write_event(w, ev, &d);
	}

	bool finished = bvnr_write_finish(w);

	error_code_t ec = bvnr_writer_get_error(w);
	if ((unsigned)ec >= (unsigned)BVN_ERROR_COUNT) __builtin_trap();

	uint32_t bw = bvnr_writer_bytes_written(w);
	if (bw > cap) __builtin_trap();

	if (finished && bw > 0) {
		if (!readback_validate(outbuf, bw)) {

			__builtin_trap();
		}
	}

done:
	bvnr_writer_destroy(w);
	free(outbuf);
}

static void harness_structured(const uint8_t *data, size_t len)
{
	cursor_t cur = { data, len };

	uint8_t  cfg    = cur_u8(&cur);
	bool     pretty = (cfg >> 0) & 1u;
	uint8_t  depth  = (cur_u8(&cur) & 0x07u) + 1u;
	uint8_t  n_keys = (cur_u8(&cur) & 0x0Fu) + 1u;

	uint8_t outbuf[WRITE_CAP];
	bvnr_writer_t *w = bvnr_writer_create();
	if (!w) return;

	if (!bvnr_open_write_mem(w, outbuf, sizeof(outbuf), pretty, NULL))
		goto done;

	bvnr_data_t hdr = {0};
	if (!bvnr_write_event(w, ev_stream_start, &hdr)) goto done;

	for (uint8_t d = 0; d < depth; d++) {
		char key[32];
		snprintf(key, sizeof(key), "level%u", (unsigned)d);
		if (!bvnr_write_struct_start(w, key)) goto done;
	}

	for (uint8_t k = 0; k < n_keys; k++) {
		char kbuf[32];
		snprintf(kbuf, sizeof(kbuf), "k%u", (unsigned)k);

		bool ok;
		switch (k % 8u) {
		case 0: ok = bvnr_write_uint  (w, kbuf,  8,  (uint64_t)cur_u8(&cur));       break;
		case 1: ok = bvnr_write_uint  (w, kbuf, 16,  (uint64_t)cur_u16(&cur));      break;
		case 2: ok = bvnr_write_sint  (w, kbuf, 32,  (int64_t)(int32_t)cur_u32(&cur)); break;
		case 3: ok = bvnr_write_float (w, kbuf, 64,  (double)(int32_t)cur_u32(&cur) / 1000.0); break;
		case 4: {
			char vbuf[64];
			cur_str(&cur, vbuf, 32);
			ok = bvnr_write_string(w, kbuf, vbuf);
			break;
		}
		case 5: ok = bvnr_write_bool  (w, kbuf, (bool)(cur_u8(&cur) & 1u));          break;
		case 6: ok = bvnr_write_null  (w, kbuf);                                     break;
		case 7: {
			value_unit_t vu = BVN_UNIT_SI(bu_meter, si_kilo);
			ok = bvnr_write_uint_unit(w, kbuf, 32, (uint64_t)cur_u16(&cur), vu);
			break;
		}
		default: ok = true; break;
		}
		if (!ok) goto done;
	}

	for (uint8_t d = 0; d < depth; d++) {
		if (!bvnr_write_struct_end(w)) goto done;
	}

	bool finished = bvnr_write_finish(w);

	error_code_t ec = bvnr_writer_get_error(w);
	if ((unsigned)ec >= (unsigned)BVN_ERROR_COUNT) __builtin_trap();

	uint32_t bw = bvnr_writer_bytes_written(w);
	if (bw > sizeof(outbuf)) __builtin_trap();

	if (finished && bw > 0) {
		if (!readback_validate(outbuf, bw)) __builtin_trap();
	}

done:
	bvnr_writer_destroy(w);
}

static bool write_flag_on_event(void *ud, bvnr_event_t ev, bvnr_data_t *d)
{
	unsigned *cnt = ud;
	(*cnt)++;
	if ((unsigned)ev >= (unsigned)BVN_EVENT_COUNT) __builtin_trap();
	(void)d;
	return true;
}

static void harness_write_flags(const uint8_t *data, size_t len)
{
	cursor_t cur = { data, len };

	uint8_t cfg = cur_u8(&cur);
	bool pretty = (cfg >> 0) & 1u;
	bool use_cb = (cfg >> 1) & 1u;
	bool use_limits = (cfg >> 2) & 1u;

	unsigned event_count = 0;

	bvnr_write_flags_t wflags;
	memset(&wflags, 0, sizeof(wflags));
	if (use_cb) {
		wflags.on_event  = write_flag_on_event;
		wflags.userdata  = &event_count;
	}
	if (use_limits) {
		wflags.max_identifier_length = 8;
		wflags.max_string_length     = 16;
		wflags.max_struct_nesting    = 4;
	}

	bvnr_writer_t *w = bvnr_writer_create();
	if (!w) return;

	if (!bvnr_open_write_sink(w, &(bvnr_sink_t){0}, pretty, &wflags)) {
		bvnr_writer_destroy(w);
		return;
	}

	bvnr_data_t hdr = {0};
	(void)bvnr_write_event(w, ev_stream_start, &hdr);

	char key[32], val[32];
	cur_str(&cur, key, 12);
	cur_str(&cur, val, 16);
	(void)bvnr_write_string(w, key, val);
	(void)bvnr_write_finish(w);

	error_code_t ec = bvnr_writer_get_error(w);
	if ((unsigned)ec >= (unsigned)BVN_ERROR_COUNT) __builtin_trap();

	bvnr_writer_destroy(w);
}

static void harness_fd_stream(const uint8_t *data, size_t len)
{
	cursor_t cur = { data, len };

	uint8_t  cfg    = cur_u8(&cur);
	bool     pretty = (cfg >> 0) & 1u;
	uint8_t  n_keys = (cur_u8(&cur) & 0x0Fu) + 1u;

	FILE *f = tmpfile();
	if (!f) return;

	bvnr_writer_t *w = bvnr_writer_create();
	if (!w) { fclose(f); return; }

	bvnr_sink_t sink;
	bvnr_sink_to_fd(&sink, fileno(f));

	if (!bvnr_open_write_sink(w, &sink, pretty, NULL)) goto done;

	bvnr_data_t hdr = {0};
	if (!bvnr_write_event(w, ev_stream_start, &hdr)) goto done;

	for (uint8_t k = 0; k < n_keys; k++) {
		char kbuf[32];
		snprintf(kbuf, sizeof(kbuf), "k%u", (unsigned)k);
		switch (k % 4u) {
		case 0: (void)bvnr_write_uint (w, kbuf, 32, (uint64_t)cur_u32(&cur)); break;
		case 1: (void)bvnr_write_sint (w, kbuf, 32,
			(int64_t)(int32_t)cur_u32(&cur)); break;
		case 2: (void)bvnr_write_float(w, kbuf, 64,
			(double)(int32_t)cur_u32(&cur) / 1000.0); break;
		case 3: {
			char vbuf[64];
			cur_str(&cur, vbuf, 32);
			(void)bvnr_write_string(w, kbuf, vbuf);
			break;
		}
		}
	}

	if (bvnr_write_finish(w)) {
		error_code_t ec = bvnr_writer_get_error(w);
		if ((unsigned)ec >= (unsigned)BVN_ERROR_COUNT) __builtin_trap();

		uint32_t bw = bvnr_writer_bytes_written(w);
		if (bw > 0) {
			rewind(f);
			uint8_t *rbuf = malloc(bw);
			if (rbuf) {
				size_t got = fread(rbuf, 1, bw, f);
				if (got > 0) {
					if (!readback_validate(rbuf, (uint32_t)got))
						__builtin_trap();
				}
				free(rbuf);
			}
		}
	}

done:
	bvnr_writer_destroy(w);
	fclose(f);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	if (size == 0) return 0;

	uint8_t sel = data[0] % 4u;
	const uint8_t *payload = data + 1;
	size_t         plen    = size  - 1;

	switch (sel) {
	case 0: harness_random_events(payload, plen); break;
	case 1: harness_structured   (payload, plen); break;
	case 2: harness_write_flags  (payload, plen); break;
	case 3: harness_fd_stream    (payload, plen); break;
	}

	return 0;
}

#ifdef FUZZ_STANDALONE

static uint64_t fw_prng_state;
static uint64_t fw_prng_next(void)
{
	uint64_t x = fw_prng_state;
	x ^= x << 13; x ^= x >> 7; x ^= x << 17;
	fw_prng_state = x ? x : 0x9E3779B97F4A7C15ull;
	return fw_prng_state;
}
static size_t fw_prng_range(size_t lo, size_t hi)
{
	if (hi <= lo) return lo;
	return lo + (size_t)(fw_prng_next() % (hi - lo + 1));
}
static void fw_mutate(uint8_t *buf, size_t *plen, size_t cap,
					   const uint8_t *seed, size_t seedlen)
{
	if (*plen == 0) {
		size_t n = seedlen < cap ? seedlen : cap;
		memcpy(buf, seed, n);
		*plen = n ? n : 1;
		if (*plen == 0) buf[0] = 0;
		return;
	}
	switch (fw_prng_next() % 6u) {
	case 0: { /* bit flip */
		size_t pos = fw_prng_range(0, *plen - 1);
		buf[pos] ^= (uint8_t)(1u << (fw_prng_next() % 8));
		break;
	}
	case 1: { /* byte set */
		size_t pos = fw_prng_range(0, *plen - 1);
		buf[pos] = (uint8_t)fw_prng_next();
		break;
	}
	case 2: { /* insert byte */
		if (*plen < cap) {
			size_t pos = fw_prng_range(0, *plen);
			memmove(buf + pos + 1, buf + pos, *plen - pos);
			buf[pos] = (uint8_t)fw_prng_next();
			(*plen)++;
		}
		break;
	}
	case 3: { /* delete byte */
		if (*plen > 1) {
			size_t pos = fw_prng_range(0, *plen - 1);
			memmove(buf + pos, buf + pos + 1, *plen - pos - 1);
			(*plen)--;
		}
		break;
	}
	case 4: { /* random block */
		size_t pos = fw_prng_range(0, *plen - 1);
		size_t n = fw_prng_range(1, 16);
		if (pos + n > *plen) n = *plen - pos;
		for (size_t i = 0; i < n; i++)
			buf[pos + i] = (uint8_t)fw_prng_next();
		break;
	}
	default: { /* reset to seed */
		size_t n = seedlen < cap ? seedlen : cap;
		memcpy(buf, seed, n);
		*plen = n ? n : 1;
		break;
	}
	}
}

static int run_file(const char *path, size_t iterations, uint64_t seed)
{
	FILE *f = fopen(path, "rb");
	if (!f) { perror(path); return 1; }
	fseek(f, 0, SEEK_END);
	long sz = ftell(f);
	rewind(f);
	if (sz <= 0) { fclose(f); return 0; }
	uint8_t *seed_buf = malloc((size_t)sz);
	if (!seed_buf) { fclose(f); return 1; }
	if (fread(seed_buf, 1, (size_t)sz, f) != (size_t)sz) {
		free(seed_buf); fclose(f); return 1;
	}
	fclose(f);
	LLVMFuzzerTestOneInput(seed_buf, (size_t)sz);
	if (iterations > 0) {
		size_t cap = (size_t)sz * 4u + 64u;
		uint8_t *work = malloc(cap);
		if (!work) { free(seed_buf); return 1; }
		fw_prng_state = seed ? seed : 0x123456789ABCDEF0ull;
		size_t plen = 0;
		for (size_t i = 0; i < iterations; i++) {
			fw_mutate(work, &plen, cap, seed_buf, (size_t)sz);
			LLVMFuzzerTestOneInput(work, plen);
		}
		free(work);
	}
	free(seed_buf);
	return 0;
}

int main(int argc, char **argv)
{
	size_t   iterations = 0;
	uint64_t seed       = 0xC0FFEE00C0FFEE00ull;
	const char *env_iter = getenv("BVNR_FUZZ_ITERATIONS");
	if (env_iter) iterations = (size_t)strtoul(env_iter, NULL, 10);
	int positional_start = argc;
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--iterations") == 0 && i + 1 < argc) {
			iterations = (size_t)strtoul(argv[++i], NULL, 10);
		} else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
			seed = (uint64_t)strtoull(argv[++i], NULL, 10);
		} else {
			positional_start = i;
			break;
		}
	}
	if (positional_start >= argc) {
		fprintf(stderr,
			"Usage: %s [--iterations N] [--seed S] <corpus_file> [...]\n",
			argv[0]);
		return 1;
	}
	int ret = 0;
	for (int i = positional_start; i < argc; i++)
		ret |= run_file(argv[i], iterations, seed);
	if (ret == 0)
		printf("[bvnr_fuzz_writer] PASS  %zu mutation iterations\n",
		       iterations);
	return ret;
}

#endif
