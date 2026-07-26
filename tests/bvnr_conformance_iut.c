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

#define _POSIX_C_SOURCE 200809L
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bovnar.h"

/* =========================================================================
 * bvnr_conformance_iut.c
 *
 * Reference IUT adapter for the Bovnar Conformance Test Protocol v1.
 *
 * BEHAVIOUR
 *   Reads Bovnar text from stdin until EOF.
 *   On success: exits 0, writes the conformance event log to stdout.
 *   On error:   exits 1, writes "ERROR <code_name>\n" to stdout.
 *
 * This binary implements the IUT protocol and serves two purposes:
 *   1. Self-testing: pass it as --iut to bvnr_conformance to verify the
 *      protocol layer independently of the internal capture path.
 *   2. Template: third-party implementors can use this file as a reference
 *      for the event-log output format required by the protocol.
 *
 * OUTPUT FORMAT (success)
 *   One event per line.  Non-printable and non-ASCII bytes in text fields
 *   are hex-escaped as \xNN.  The stream always starts with STREAM_START.
 *
 *   STREAM_START
 *   ASSIGNMENT_START <key>
 *   TYPE_ANN_START <family>
 *   TYPE_FAMILY <family>
 *   TYPE_PARAM_WIDTH <N>
 *   TYPE_PARAM_BASE <N>
 *   TYPE_PARAM_Q <N>           (float_fix only)
 *   TYPE_PARAM_UNIT <unit>
 *   TYPE_ANN_END <family>
 *   DATA <token_type> <value>
 *   STRUCT_START
 *   STRUCT_END
 *   ARRAY_ROW_START
 *   ARRAY_ROW_END
 *   ARRAY_DIM_START
 *   OCTET_STREAM_START
 *   OCTET_STREAM_END
 *
 *   For DATA events with token type "octets", the value field is the
 *   decimal byte count followed by " bytes" (e.g. "5 bytes").
 *
 * OUTPUT FORMAT (error)
 *   ERROR <code_name>
 *
 *   Where <code_name> is the value returned by bvn_error_to_string(),
 *   e.g. "value_out_of_range". One code is the adapter's own rather than the
 *   library's: "event_log_overflow", when the document's log does not fit
 *   EVLOG_CAP. It is reported instead of a truncated log, never alongside one.
 *
 * NOTE ON <family> IN THE LINES BELOW
 *   TYPE_ANN_START / TYPE_FAMILY / TYPE_ANN_END carry the WHOLE annotation body
 *   -- "float:64,m/s", not "float". They read as the bare family keyword only
 *   for an annotation the reader SYNTHESISED for an untyped value. doc/7 showed
 *   the keyword form for an explicit annotation and was wrong; see the worked
 *   examples there, which check_conformance_doc.py now replays through this
 *   binary.
 * ========================================================================= */

#define READ_CHUNK  4096u
#define EVLOG_CAP  (1u << 18)

/* ── Output buffer ─────────────────────────────────────────────────────── */

static char  g_out[EVLOG_CAP];
static size_t g_out_used = 0;
static bool  g_error_mode = false;
static error_code_t g_error_code = error_none;
/* Set when anything had to be dropped for want of room. A truncated event log is
 * not a shorter log -- it is an invalid one, and the cut can land mid-event
 * ("DATA  1"), so it cannot be told apart from a formatting difference by the
 * driver that compares it. This adapter used to truncate in silence and still
 * exit 0: a 190 kB document produced 262143 bytes of log, a mangled last line,
 * and every appearance of success. Refusing is the same rule the library applies
 * to itself -- bvn_rational_to_str will not truncate an exact expansion, and the
 * writer refuses rather than emit a number it cannot represent. A third-party
 * adapter written from this file inherits whichever habit it finds here. */
static bool  g_out_overflow = false;

static void out_putc(char c)
{
	if (g_out_used + 1 < EVLOG_CAP)
		g_out[g_out_used++] = c;
	else
		g_out_overflow = true;
}

static void out_puts(const char *s)
{
	size_t n = strlen(s);
	if (g_out_used + n < EVLOG_CAP) {
		memcpy(g_out + g_out_used, s, n);
		g_out_used += n;
	} else {
		g_out_overflow = true;
	}
}

static void out_putu32(uint32_t v)
{
	char tmp[16];
	int n = snprintf(tmp, sizeof(tmp), "%" PRIu32, v);
	if (n > 0) out_puts(tmp);
}

static void out_put_safe(const uint8_t *data, uint32_t len)
{
	for (uint32_t i = 0; i < len; i++) {
		uint8_t b = data[i];
		if (b >= 0x20u && b <= 0x7Eu && b != '\\') {
			out_putc((char)b);
		} else {
			char esc[5];
			snprintf(esc, sizeof(esc), "\\x%02x", b);
			out_puts(esc);
		}
	}
}

static const char *toktype_str(token_type_t t)
{
	switch (t) {
	case token_is_number:       return "number";
	case token_is_string:       return "string";
	case token_is_symbol:       return "symbol";
	case token_is_reference:    return "reference";
	case token_is_array_number: return "array_number";
	case token_is_array_string: return "array_string";
	case token_is_null_value:   return "null";
	case token_is_octet_stream: return "octets";
	case token_is_structure:    return "structure";
	case token_is_identifier:   return "identifier";
	case token_is_type:         return "type";
	case token_is_type_width:   return "type_width";
	case token_is_type_base:    return "type_base";
	case token_is_type_q:       return "type_q";
	case token_is_unit:         return "unit";
	case token_is_bool:         return "bool";
	case token_is_unknown:      return "unknown";
	}
	return "?";
}

/* ── Event callback ────────────────────────────────────────────────────── */

static bool on_verified(void *ud, bvnr_event_t ev, bvnr_data_t *d)
{
	(void)ud;

	switch (ev) {
	case ev_stream_start:
		out_puts("STREAM_START\n");
		break;

	case ev_assignment_start:
		out_puts("ASSIGNMENT_START ");
		if (d->data && d->length)
			out_put_safe((const uint8_t *)d->data, d->length);
		out_putc('\n');
		break;

	case ev_type_annotation_start:
		out_puts("TYPE_ANN_START ");
		if (d->data && d->length)
			out_put_safe((const uint8_t *)d->data, d->length);
		out_putc('\n');
		break;

	case ev_type_annotation_type_family:
		out_puts("TYPE_FAMILY ");
		if (d->data && d->length)
			out_put_safe((const uint8_t *)d->data, d->length);
		out_putc('\n');
		break;

	case ev_type_annotation_type_family_parameter:
		if (d->type == token_is_type_width) {
			out_puts("TYPE_PARAM_WIDTH ");
			out_putu32(bvn_effective_width(d->value_type));
			out_putc('\n');
		} else if (d->type == token_is_type_base) {
			out_puts("TYPE_PARAM_BASE ");
			out_putu32(bvn_effective_base(d->value_type));
			out_putc('\n');
		} else if (d->type == token_is_type_q) {
			out_puts("TYPE_PARAM_Q ");
			out_putu32(bvn_effective_q(d->value_type));
			out_putc('\n');
		} else if (d->type == token_is_unit) {
			out_puts("TYPE_PARAM_UNIT ");
			if (d->data && d->length)
				out_put_safe((const uint8_t *)d->data, d->length);
			out_putc('\n');
		}
		break;

	case ev_type_annotation_end:
		out_puts("TYPE_ANN_END ");
		if (d->data && d->length)
			out_put_safe((const uint8_t *)d->data, d->length);
		out_putc('\n');
		break;

	case ev_data:
		out_puts("DATA ");
		out_puts(toktype_str(d->type));
		out_putc(' ');
		if (d->type == token_is_octet_stream) {
			out_putu32(d->length);
			out_puts(" bytes");
		} else if (d->data && d->length) {
			out_put_safe((const uint8_t *)d->data, d->length);
		}
		out_putc('\n');
		break;

	case ev_struct_start:
		out_puts("STRUCT_START\n");
		break;

	case ev_struct_end:
		out_puts("STRUCT_END\n");
		break;

	case ev_array_row_start:
		out_puts("ARRAY_ROW_START\n");
		break;

	case ev_array_row_end:
		out_puts("ARRAY_ROW_END\n");
		break;

	case ev_array_dim_start:
		out_puts("ARRAY_DIM_START\n");
		break;

	case ev_octet_stream_start:
		out_puts("OCTET_STREAM_START\n");
		break;

	case ev_octet_stream_end:
		out_puts("OCTET_STREAM_END\n");
		break;

	case ev_stream_end:
		break;
	}

	return true;
}

static void on_error(void *ud, error_code_t err,
                     uint64_t line, uint64_t col,
                     uint32_t byte, uint64_t off)
{
	(void)ud; (void)line; (void)col; (void)byte; (void)off;
	if (!g_error_mode) {
		g_error_mode = true;
		g_error_code = err;
	}
}

/* ── Stdin reader ──────────────────────────────────────────────────────── */

static uint8_t *read_stdin(size_t *out_len)
{
	size_t   cap  = 65536;
	size_t   used = 0;
	uint8_t *buf  = malloc(cap);
	if (!buf) return NULL;

	for (;;) {
		if (used + READ_CHUNK > cap) {
			size_t nc = cap * 2;
			uint8_t *nb = realloc(buf, nc);
			if (!nb) { free(buf); return NULL; }
			buf = nb;
			cap = nc;
		}
		size_t n = fread(buf + used, 1, READ_CHUNK, stdin);
		if (n == 0) break;
		used += n;
	}

	*out_len = used;
	return buf;
}

/* ── Main ─────────────────────────────────────────────────────────────── */

static uint64_t parse_u64_arg(const char *s)
{
	char *end;
	unsigned long long v = strtoull(s, &end, 10);
	return (*end == '\0') ? (uint64_t)v : 0;
}

int main(int argc, char *argv[])
{
	bvnr_read_flags_t flags;
	memset(&flags, 0, sizeof(flags));
	flags.on_verified = on_verified;
	flags.on_error    = on_error;

	for (int i = 1; i < argc; i++) {
		if (i + 1 < argc) {
			if      (!strcmp(argv[i], "--max-id"))     flags.max_identifier_length = (uint16_t)parse_u64_arg(argv[++i]);
			else if (!strcmp(argv[i], "--max-str"))    flags.max_string_length     = (uint16_t)parse_u64_arg(argv[++i]);
			else if (!strcmp(argv[i], "--max-num"))    flags.max_number_length     = (uint16_t)parse_u64_arg(argv[++i]);
			else if (!strcmp(argv[i], "--max-sym"))    flags.max_symbol_length     = (uint16_t)parse_u64_arg(argv[++i]);
			else if (!strcmp(argv[i], "--max-ref"))    flags.max_reference_length  = (uint16_t)parse_u64_arg(argv[++i]);
			else if (!strcmp(argv[i], "--max-snest"))  flags.max_struct_nesting    = (uint8_t) parse_u64_arg(argv[++i]);
			else if (!strcmp(argv[i], "--max-anest"))  flags.max_array_nesting     = (uint8_t) parse_u64_arg(argv[++i]);
			else if (!strcmp(argv[i], "--max-aitems")) flags.max_array_items       =           parse_u64_arg(argv[++i]);
		}
	}

	size_t   input_len = 0;
	uint8_t *input     = read_stdin(&input_len);
	if (!input) {
		fprintf(stdout, "ERROR read_failed\n");
		return 1;
	}

	bvnr_reader_t *reader = bvnr_reader_create();
	if (!reader) {
		free(input);
		fprintf(stdout, "ERROR out_of_memory\n");
		return 1;
	}

	bool opened = bvnr_open_read_mem(reader, input, (uint64_t)input_len,
	                                 NULL, 0, &flags);
	bool ok = false;
	if (opened)
		ok = bvnr_read(reader);

	error_code_t err = bvnr_reader_get_error(reader);
	if (err == error_none && g_error_code != error_none)
		err = g_error_code;

	bvnr_reader_destroy(reader);
	free(input);

	if (!ok || err != error_none) {
		fprintf(stdout, "ERROR %s\n", bvn_error_to_string(err));
		return 1;
	}

	if (g_out_overflow) {
		/* Report on stdout in the protocol's error shape so a driver sees a
		 * refusal rather than a log, and on stderr so a human sees why. */
		fprintf(stdout, "ERROR event_log_overflow\n");
		fprintf(stderr,
			"bvnr_conformance_iut: the event log for this document exceeds "
			"%u bytes; raise EVLOG_CAP. Refusing rather than emitting a "
			"truncated log, which would differ from the reference in a way "
			"indistinguishable from a formatting bug.\n", (unsigned)EVLOG_CAP);
		return 1;
	}

	fwrite(g_out, 1, g_out_used, stdout);
	return 0;
}
