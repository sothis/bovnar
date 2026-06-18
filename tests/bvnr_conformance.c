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
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
/* IUT mode (--iut) drives an external binary via fork/exec + pipes; these
 * headers and the iut_run/run_iut_test code below are POSIX-only. The default
 * self-test mode needs none of them, so the suite still builds (and runs under
 * Wine) on the MinGW Windows cross build. */
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
#include "bovnar.h"
#include "bovnar_dom.h"

/* =========================================================================
 * Event log buffer
 * ========================================================================= */

#define EVLOG_INIT_CAP 65536u

typedef struct {
	char   *buf;
	size_t  used;
	size_t  cap;
	bool    oom;
} evlog_t;

static void evlog_init(evlog_t *l)
{
	l->buf  = malloc(EVLOG_INIT_CAP);
	l->used = 0;
	l->cap  = l->buf ? EVLOG_INIT_CAP : 0;
	l->oom  = !l->buf;
}

static void evlog_free(evlog_t *l)
{
	free(l->buf);
	l->buf  = NULL;
	l->used = 0;
	l->cap  = 0;
}

static void evlog_reserve(evlog_t *l, size_t extra)
{
	if (l->oom) return;
	if (l->used + extra <= l->cap) return;
	size_t nc = l->cap * 2;
	if (nc < l->used + extra) nc = l->used + extra + 4096;
	char *nb = realloc(l->buf, nc);
	if (!nb) { l->oom = true; return; }
	l->buf = nb;
	l->cap = nc;
}

static void evlog_putc(evlog_t *l, char c)
{
	evlog_reserve(l, 1);
	if (!l->oom) l->buf[l->used++] = c;
}

static void evlog_puts(evlog_t *l, const char *s)
{
	size_t n = strlen(s);
	evlog_reserve(l, n);
	if (!l->oom) { memcpy(l->buf + l->used, s, n); l->used += n; }
}

static void evlog_putu32(evlog_t *l, uint32_t v)
{
	char tmp[16];
	int n = snprintf(tmp, sizeof(tmp), "%" PRIu32, v);
	if (n > 0) evlog_reserve(l, (size_t)n);
	if (!l->oom && n > 0) {
		memcpy(l->buf + l->used, tmp, (size_t)n);
		l->used += (size_t)n;
	}
}

static void evlog_put_safe(evlog_t *l, const uint8_t *data, uint32_t len)
{
	for (uint32_t i = 0; i < len; i++) {
		uint8_t b = data[i];
		if (b >= 0x20u && b <= 0x7Eu && b != '\\') {
			evlog_putc(l, (char)b);
		} else {
			char esc[5];
			snprintf(esc, sizeof(esc), "\\x%02x", b);
			evlog_puts(l, esc);
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

static void evlog_append_event(evlog_t *l, bvnr_event_t ev, bvnr_data_t *d)
{
	switch (ev) {
	case ev_stream_start:
		evlog_puts(l, "STREAM_START\n");
		break;
	case ev_assignment_start:
		evlog_puts(l, "ASSIGNMENT_START ");
		if (d->data && d->length)
			evlog_put_safe(l, (const uint8_t *)d->data, d->length);
		evlog_putc(l, '\n');
		break;
	case ev_type_annotation_start:
		evlog_puts(l, "TYPE_ANN_START ");
		if (d->data && d->length)
			evlog_put_safe(l, (const uint8_t *)d->data, d->length);
		evlog_putc(l, '\n');
		break;
	case ev_type_annotation_type_family:
		evlog_puts(l, "TYPE_FAMILY ");
		if (d->data && d->length)
			evlog_put_safe(l, (const uint8_t *)d->data, d->length);
		evlog_putc(l, '\n');
		break;
	case ev_type_annotation_type_family_parameter:
		if (d->type == token_is_type_width) {
			evlog_puts(l, "TYPE_PARAM_WIDTH ");
			evlog_putu32(l, bvn_effective_width(d->value_type));
			evlog_putc(l, '\n');
		} else if (d->type == token_is_type_base) {
			evlog_puts(l, "TYPE_PARAM_BASE ");
			evlog_putu32(l, bvn_effective_base(d->value_type));
			evlog_putc(l, '\n');
		} else if (d->type == token_is_type_q) {
			evlog_puts(l, "TYPE_PARAM_Q ");
			evlog_putu32(l, bvn_effective_q(d->value_type));
			evlog_putc(l, '\n');
		} else if (d->type == token_is_unit) {
			evlog_puts(l, "TYPE_PARAM_UNIT ");
			if (d->data && d->length)
				evlog_put_safe(l, (const uint8_t *)d->data, d->length);
			evlog_putc(l, '\n');
		}
		break;
	case ev_type_annotation_end:
		evlog_puts(l, "TYPE_ANN_END ");
		if (d->data && d->length)
			evlog_put_safe(l, (const uint8_t *)d->data, d->length);
		evlog_putc(l, '\n');
		break;
	case ev_data:
		evlog_puts(l, "DATA ");
		evlog_puts(l, toktype_str(d->type));
		evlog_putc(l, ' ');
		if (d->type == token_is_octet_stream) {
			evlog_putu32(l, d->length);
			evlog_puts(l, " bytes");
		} else if (d->data && d->length) {
			evlog_put_safe(l, (const uint8_t *)d->data, d->length);
		}
		evlog_putc(l, '\n');
		break;
	case ev_struct_start:
		evlog_puts(l, "STRUCT_START\n");
		break;
	case ev_struct_end:
		evlog_puts(l, "STRUCT_END\n");
		break;
	case ev_array_row_start:
		evlog_puts(l, "ARRAY_ROW_START\n");
		break;
	case ev_array_row_end:
		evlog_puts(l, "ARRAY_ROW_END\n");
		break;
	case ev_array_dim_start:
		evlog_puts(l, "ARRAY_DIM_START\n");
		break;
	case ev_octet_stream_start:
		evlog_puts(l, "OCTET_STREAM_START\n");
		break;
	case ev_octet_stream_end:
		evlog_puts(l, "OCTET_STREAM_END\n");
		break;
	case ev_stream_end:
		break;
	}
}

/* =========================================================================
 * Reference-implementation parse helpers
 * ========================================================================= */

typedef struct {
	evlog_t      log;
	error_code_t last_error;
	uint32_t     error_count;
} ref_ctx_t;

static bool ref_on_verified(void *ud, bvnr_event_t ev, bvnr_data_t *d)
{
	ref_ctx_t *ctx = (ref_ctx_t *)ud;
	evlog_append_event(&ctx->log, ev, d);
	return true;
}

static void ref_on_error(void *ud, error_code_t err,
                         uint64_t line, uint64_t col,
                         uint32_t byte, uint64_t off)
{
	ref_ctx_t *ctx = (ref_ctx_t *)ud;
	ctx->last_error = err;
	ctx->error_count++;
	(void)line; (void)col; (void)byte; (void)off;
}

typedef struct {
	bool         parse_ok;
	bool         opened;
	error_code_t error;
	uint64_t     error_line;
	uint64_t     error_column;
	uint64_t     recovery_count;
	evlog_t      log;
} parse_result_t;

static void parse_result_free(parse_result_t *r)
{
	evlog_free(&r->log);
}

static parse_result_t ref_parse(
	const void *input, uint32_t input_len,
	bool continue_on_error,
	uint16_t max_id_len,
	uint16_t max_str_len,
	uint16_t max_num_len,
	uint16_t max_sym_len,
	uint16_t max_ref_len,
	uint8_t  max_struct_nesting,
	uint8_t  max_array_nesting,
	uint64_t max_array_items)
{
	parse_result_t r;
	memset(&r, 0, sizeof(r));
	evlog_init(&r.log);

	ref_ctx_t ctx;
	memset(&ctx, 0, sizeof(ctx));
	ctx.log = r.log;

	bvnr_read_flags_t flags;
	memset(&flags, 0, sizeof(flags));
	flags.userdata          = &ctx;
	flags.on_verified       = ref_on_verified;
	flags.on_error          = ref_on_error;
	flags.continue_on_error = continue_on_error;
	if (max_id_len)          flags.max_identifier_length = max_id_len;
	if (max_str_len)         flags.max_string_length     = max_str_len;
	if (max_num_len)         flags.max_number_length     = max_num_len;
	if (max_sym_len)         flags.max_symbol_length     = max_sym_len;
	if (max_ref_len)         flags.max_reference_length  = max_ref_len;
	if (max_struct_nesting)  flags.max_struct_nesting    = max_struct_nesting;
	if (max_array_nesting)   flags.max_array_nesting     = max_array_nesting;
	if (max_array_items)     flags.max_array_items       = max_array_items;

	bvnr_reader_t *reader = bvnr_reader_create();
	if (!reader) {
		r.log.oom = true;
		return r;
	}

	r.opened = bvnr_open_read_mem(reader, input, (uint64_t)input_len,
	                              NULL, 0, &flags);
	if (r.opened)
		r.parse_ok = bvnr_read(reader);

	r.error          = bvnr_reader_get_error(reader);
	r.error_line     = bvnr_reader_get_error_line(reader);
	r.error_column   = bvnr_reader_get_error_column(reader);
	r.recovery_count = bvnr_reader_get_recovery_count(reader);
	bvnr_reader_destroy(reader);

	if (ctx.last_error != error_none && r.error == error_none)
		r.error = ctx.last_error;

	r.log = ctx.log;
	return r;
}

static parse_result_t ref_parse_str_flags(
	const char *input, bool cont,
	uint16_t max_id, uint16_t max_str, uint16_t max_num,
	uint16_t max_sym, uint16_t max_ref,
	uint8_t max_snest, uint8_t max_anest, uint64_t max_aitems)
{
	return ref_parse(input, (uint32_t)strlen(input),
	                 cont, max_id, max_str, max_num,
	                 max_sym, max_ref,
	                 max_snest, max_anest, max_aitems);
}

/* =========================================================================
 * Test case type (needed by iut_run below)
 * ========================================================================= */

typedef enum {
	CF_VALID = 0,
	CF_ERROR
} cf_expect_t;

typedef struct cf_case_t {
	const char    *id;
	const char    *group;
	const char    *description;
	const char    *input;
	cf_expect_t    expect;
	error_code_t   expected_error;
	bool           continue_on_error;
	uint16_t       max_id_len;
	uint16_t       max_str_len;
	uint16_t       max_num_len;
	uint8_t        max_struct_nesting;
	uint8_t        max_array_nesting;
	uint64_t       max_array_items;
	const char    *expect_key;
	const char    *expect_data;
	const char    *iut_skip_reason;
	const uint8_t *input_bin;
	uint32_t       input_bin_len;
	uint16_t       max_sym_len;
	uint16_t       max_ref_len;
	/* When true, the case is validated through the materialised-document
	 * (DOM) tier — bvn_dom_parse — instead of the streaming reader.  The
	 * spec-1.0 array-homogeneity (§7.4), struct-shape, and duplicate-key
	 * (§8.1) rules are enforced above the lexer, so they are unreachable
	 * through the streaming on_verified path the IUT protocol uses.  These
	 * cases run in self-test mode only and are skipped under --iut. */
	bool           dom_validate;
} cf_case_t;

/* =========================================================================
 * IUT invocation (fork/exec + pipes)
 * ========================================================================= */
#ifndef _WIN32

#define IUT_BUF_CAP (1u << 20)

typedef struct {
	bool    ok;
	int     exit_code;
	char   *out_buf;
	size_t  out_len;
} iut_result_t;

static void iut_result_free(iut_result_t *r)
{
	free(r->out_buf);
	r->out_buf = NULL;
	r->out_len = 0;
}

static iut_result_t iut_run(const char *iut_path,
                             const void *input, size_t input_len,
                             const cf_case_t *tc)
{
	iut_result_t res;
	memset(&res, 0, sizeof(res));

	char a_max_id[32], a_max_str[32], a_max_num[32];
	char a_max_sym[32], a_max_ref[32];
	char a_max_snest[32], a_max_anest[32], a_max_aitems[32];

	char *argv[32];
	int  argc = 0;
	char iut_copy[4096];
	snprintf(iut_copy, sizeof(iut_copy), "%s", iut_path);
	argv[argc++] = iut_copy;

#define PUSH_LIMIT(flag_, name_, val_) \
	do { if (val_) { \
		snprintf(flag_, sizeof(flag_), "%llu", (unsigned long long)(val_)); \
		argv[argc++] = (char *)(name_); \
		argv[argc++] = flag_; \
	} } while (0)

	PUSH_LIMIT(a_max_id,     "--max-id",     tc->max_id_len);
	PUSH_LIMIT(a_max_str,    "--max-str",    tc->max_str_len);
	PUSH_LIMIT(a_max_num,    "--max-num",    tc->max_num_len);
	PUSH_LIMIT(a_max_sym,    "--max-sym",    tc->max_sym_len);
	PUSH_LIMIT(a_max_ref,    "--max-ref",    tc->max_ref_len);
	PUSH_LIMIT(a_max_snest,  "--max-snest",  tc->max_struct_nesting);
	PUSH_LIMIT(a_max_anest,  "--max-anest",  tc->max_array_nesting);
	PUSH_LIMIT(a_max_aitems, "--max-aitems", tc->max_array_items);
#undef PUSH_LIMIT

	argv[argc] = NULL;

	int in_pipe[2], out_pipe[2];
	if (pipe(in_pipe) != 0) {
		fprintf(stderr, "pipe() failed: %s\n", strerror(errno));
		return res;
	}
	if (pipe(out_pipe) != 0) {
		fprintf(stderr, "pipe() failed: %s\n", strerror(errno));
		close(in_pipe[0]); close(in_pipe[1]);
		return res;
	}

	pid_t pid = fork();
	if (pid < 0) {
		fprintf(stderr, "fork() failed: %s\n", strerror(errno));
		close(in_pipe[0]); close(in_pipe[1]);
		close(out_pipe[0]); close(out_pipe[1]);
		return res;
	}

	if (pid == 0) {
		close(in_pipe[1]);
		close(out_pipe[0]);
		dup2(in_pipe[0],  STDIN_FILENO);
		dup2(out_pipe[1], STDOUT_FILENO);
		close(in_pipe[0]);
		close(out_pipe[1]);
		execv(iut_path, argv);
		_exit(127);
	}

	close(in_pipe[0]);
	close(out_pipe[1]);

	const char *p = (const char *)input;
	size_t left   = input_len;
	while (left > 0) {
		ssize_t w = write(in_pipe[1], p, left);
		if (w < 0) { if (errno == EINTR) continue; break; }
		p    += (size_t)w;
		left -= (size_t)w;
	}
	close(in_pipe[1]);

	res.out_buf = malloc(IUT_BUF_CAP);
	if (!res.out_buf) {
		waitpid(pid, NULL, 0);
		close(out_pipe[0]);
		return res;
	}
	res.out_len = 0;
	for (;;) {
		size_t avail = IUT_BUF_CAP - res.out_len - 1;
		if (avail == 0) break;
		ssize_t n = read(out_pipe[0], res.out_buf + res.out_len, avail);
		if (n <= 0) { if (n < 0 && errno == EINTR) continue; break; }
		res.out_len += (size_t)n;
	}
	res.out_buf[res.out_len] = '\0';
	close(out_pipe[0]);

	int status = 0;
	waitpid(pid, &status, 0);
	res.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
	res.ok = true;
	return res;
}

#endif /* !_WIN32 (IUT invocation) */

/* =========================================================================
 * TAP output
 * ========================================================================= */

/*
 * TAP version 14 output. The suite is emitted as a parent stream whose test
 * points are the case groups; each group is a 4-space-indented subtest (a child
 * TAP stream) carrying its individual cases, a trailing child plan, and a leading
 * "# Subtest:" comment, then a parent-level roll-up point per the TAP 14 subtest
 * convention. tap_ok/tap_fail/tap_skip emit the child (case) points; the suite/
 * subtest framing is driven from main().
 */
/* Parent (suite) level. */
static int g_grp_next     = 1;   /* next parent test-point number          */
static int g_grp_total    = 0;   /* groups (subtests) emitted              */
static int g_grp_failures = 0;   /* groups with at least one failed case   */
/* Child (current subtest) level. */
static int g_sub_next     = 1;   /* next case point within the group       */
static int g_sub_failures = 0;   /* failed cases in the current group       */
/* Overall case tallies (drive the summary line; semantics unchanged). */
static int g_tap_failures = 0;
static int g_tap_total    = 0;
static bool g_verbose     = false;
static const char *g_filter_group = NULL;

static void tap_begin(int group_count)
{
	printf("TAP version 14\n1..%d\n", group_count);
}

static void tap_subtest_begin(const char *group)
{
	printf("# Subtest: %s\n", group);
	g_sub_next     = 1;
	g_sub_failures = 0;
}

static void tap_subtest_end(const char *group)
{
	/* Trailing child plan, then the parent-level roll-up point. */
	printf("    1..%d\n", g_sub_next - 1);
	if (g_sub_failures > 0) {
		printf("not ok %d - %s\n", g_grp_next++, group);
		g_grp_failures++;
	} else {
		printf("ok %d - %s\n", g_grp_next++, group);
	}
	g_grp_total++;
}

static void tap_ok(const char *id, const char *desc)
{
	printf("    ok %d - [%s] %s\n", g_sub_next++, id, desc);
	g_tap_total++;
	if (g_verbose)
		printf("    # PASS\n");
}

/*
 * Emit `s` on a single physical line, escaping the control bytes — newlines in
 * particular — that would otherwise break out of the indented YAML diagnostic
 * block and produce malformed TAP. IUT failure details embed raw event-log bytes
 * (e.g. "STREAM_START\n…"), so this keeps the `message:` scalar well-formed for
 * strict TAP parsers.
 */
static void tap_print_escaped(const char *s)
{
	for (; *s; s++) {
		unsigned char c = (unsigned char)*s;
		switch (c) {
		case '\n': fputs("\\n", stdout); break;
		case '\r': fputs("\\r", stdout); break;
		case '\t': fputs("\\t", stdout); break;
		default:
			if (c < 0x20u || c == 0x7fu)
				printf("\\x%02x", c);
			else
				putchar((int)c);
		}
	}
}

static void tap_fail(const char *id, const char *desc, const char *detail)
{
	printf("    not ok %d - [%s] %s\n", g_sub_next++, id, desc);
	if (detail && *detail) {
		fputs("      ---\n      message: ", stdout);
		tap_print_escaped(detail);
		fputs("\n      ...\n", stdout);
	}
	g_tap_total++;
	g_tap_failures++;
	g_sub_failures++;
}

#ifndef _WIN32  /* only the IUT runner emits SKIPs; unused on the Windows build */
static void tap_skip(const char *id, const char *desc, const char *reason)
{
	printf("    ok %d - [%s] %s # SKIP %s\n", g_sub_next++, id, desc, reason);
	g_tap_total++;
}
#endif

/* =========================================================================
 * Test case definitions
 * ========================================================================= */

#define VALID(id_, grp_, desc_, inp_) \
	{ id_, grp_, desc_, inp_, CF_VALID, error_none, \
	  false, 0, 0, 0, 0, 0, 0, NULL, NULL, NULL, NULL, 0, 0, 0 }

#define VALID_KEY(id_, grp_, desc_, inp_, key_, data_) \
	{ id_, grp_, desc_, inp_, CF_VALID, error_none, \
	  false, 0, 0, 0, 0, 0, 0, key_, data_, NULL, NULL, 0, 0, 0 }

#define VALID_BIN(id_, grp_, desc_, data_, len_) \
	{ id_, grp_, desc_, NULL, CF_VALID, error_none, \
	  false, 0, 0, 0, 0, 0, 0, NULL, NULL, NULL, data_, len_, 0, 0 }

#define ERROR_CASE(id_, grp_, desc_, inp_, err_) \
	{ id_, grp_, desc_, inp_, CF_ERROR, err_, \
	  false, 0, 0, 0, 0, 0, 0, NULL, NULL, NULL, NULL, 0, 0, 0 }

#define ERROR_BIN(id_, grp_, desc_, data_, len_, err_) \
	{ id_, grp_, desc_, NULL, CF_ERROR, err_, \
	  false, 0, 0, 0, 0, 0, 0, NULL, NULL, NULL, data_, len_, 0, 0 }

#define ERROR_CONT(id_, grp_, desc_, inp_, err_) \
	{ id_, grp_, desc_, inp_, CF_ERROR, err_, \
	  true, 0, 0, 0, 0, 0, 0, NULL, NULL, NULL, NULL, 0, 0, 0 }

#define ERROR_LIM(id_, grp_, desc_, inp_, err_, mid_, mst_, mnu_, msn_, man_, mai_) \
	{ id_, grp_, desc_, inp_, CF_ERROR, err_, \
	  false, mid_, mst_, mnu_, msn_, man_, mai_, NULL, NULL, NULL, NULL, 0, 0, 0 }

#define ERROR_SYM(id_, grp_, desc_, inp_, err_, msl_) \
	{ id_, grp_, desc_, inp_, CF_ERROR, err_, \
	  false, 0, 0, 0, 0, 0, 0, NULL, NULL, NULL, NULL, 0, msl_, 0 }

#define ERROR_REF(id_, grp_, desc_, inp_, err_, mrl_) \
	{ id_, grp_, desc_, inp_, CF_ERROR, err_, \
	  false, 0, 0, 0, 0, 0, 0, NULL, NULL, NULL, NULL, 0, 0, mrl_ }

/* Materialised-document (DOM) tier cases — validated via bvn_dom_parse,
 * not the streaming reader.  The trailing initializer sets dom_validate. */
#define DOM_VALID(id_, grp_, desc_, inp_) \
	{ id_, grp_, desc_, inp_, CF_VALID, error_none, \
	  false, 0, 0, 0, 0, 0, 0, NULL, NULL, NULL, NULL, 0, 0, 0, true }

#define DOM_ERROR(id_, grp_, desc_, inp_, err_) \
	{ id_, grp_, desc_, inp_, CF_ERROR, err_, \
	  false, 0, 0, 0, 0, 0, 0, NULL, NULL, NULL, NULL, 0, 0, 0, true }

/* Binary test data for octet stream cases.
 * These cannot be NUL-terminated C strings since they contain 0x00 bytes. */
static const uint8_t g_oct_single[] = {
	'.','b',' ','=',' ',
	0x00, 0x01, 0x05, 0x00, 'h','e','l','l','o', 0x00,
	';'
};
static const uint8_t g_oct_two_chunks[] = {
	'.','b',' ','=',' ',
	0x00, 0x01, 0x05, 0x00, 'h','e','l','l','o',
	      0x01, 0x03, 0x00, 'b','y','e', 0x00,
	';'
};
static const uint8_t g_oct_empty[] = {
	'.','b',' ','=',' ',
	0x00, 0x00,
	';'
};
static const uint8_t g_oct_bad_tag[] = {
	'.','b',' ','=',' ',
	0x00, 0x02, 0x05, 0x00, 'h','e','l','l','o', 0x00,
	';'
};
static const uint8_t g_enc_nul_in_id[] = {
	'.','f','o', 0x00, 'o',' ','=',' ','1',';'
};

static const cf_case_t g_cases[] = {

	/* ── ENCODING ─────────────────────────────────────────────────── */
	VALID("ENC-001", "encoding", "empty stream",
	      ""),
	VALID("ENC-002", "encoding", "UTF-8 BOM at byte 0",
	      "\xEF\xBB\xBF.x = 1;"),
	ERROR_CASE("ENC-003", "encoding", "UTF-8 BOM after first comment",
	           "# comment\n\xEF\xBB\xBF.x = 1;",
	           error_unexpected_input_byte),
	ERROR_CASE("ENC-004", "encoding", "invalid UTF-8 byte 0xFF in text",
	           ".x = \xFF;",
	           error_invalid_utf8_byte),
	ERROR_CASE("ENC-005", "encoding", "overlong UTF-8 sequence",
	           ".x = \xC0\x80;",
	           error_invalid_utf8_byte),
	VALID("ENC-006", "encoding", "UTF-8 multi-byte in string value",
	      ".x = \"caf\xC3\xA9\";"),
	VALID("ENC-007", "encoding", "UTF-8 multi-byte in identifier",
	      ".\xC3\xA9l\xC3\xA8ve = 1;"),
	VALID("ENC-008", "encoding", "comment with UTF-8 content",
	      "# caf\xC3\xA9\n.x = 1;"),
	ERROR_BIN("ENC-009", "encoding", "NUL byte in identifier body",
	          g_enc_nul_in_id, (uint32_t)sizeof(g_enc_nul_in_id),
	          error_unexpected_input_byte),

	/* ── VERSION DIRECTIVE (spec 1.1) ────────────────────────────── */
	VALID("VER-001", "version", "directive declaring the current spec version",
	      "#!bovnar 1.1\n.x = 1;"),
	VALID("VER-002", "version", "directive declaring an older supported version",
	      "#!bovnar 1.0\n.x = 1;"),
	VALID("VER-003", "version", "BOM followed by a version directive",
	      "\xEF\xBB\xBF#!bovnar 1.0\n.x = 1;"),
	VALID("VER-004", "version", "newer minor is lenient by default",
	      "#!bovnar 1.9\n.x = 1;"),
	VALID("VER-005", "version", "newer major is lenient by default",
	      "#!bovnar 2.0\n.x = 1;"),
	VALID("VER-006", "version", "directive-only document, no trailing newline",
	      "#!bovnar 1.1"),
	VALID("VER-007", "version", "prefix lookalike is an ordinary comment",
	      "#!bovnarish 9.9\n.x = 1;"),
	VALID("VER-008", "version", "a #!bovnar on the second line is just a comment",
	      "# header\n#!bovnar 9.9\n.x = 1;"),
	VALID("VER-009", "version", "leading whitespace before the directive",
	      "  #!bovnar 1.1\n.x = 1;"),
	ERROR_CASE("VER-010", "version", "version with missing minor component",
	           "#!bovnar 1\n.x = 1;",
	           error_invalid_spec_version),
	ERROR_CASE("VER-011", "version", "non-numeric version",
	           "#!bovnar a.b\n.x = 1;",
	           error_invalid_spec_version),
	ERROR_CASE("VER-012", "version", "leading zero in a version component",
	           "#!bovnar 1.01\n.x = 1;",
	           error_invalid_spec_version),
	ERROR_CASE("VER-013", "version", "trailing junk after the version",
	           "#!bovnar 1.1 beta\n.x = 1;",
	           error_invalid_spec_version),

	/* ── IDENTIFIERS ─────────────────────────────────────────────── */
	VALID_KEY("ID-001", "identifiers", "simple lowercase identifier",
	          ".foo = 1;", "foo", "1"),
	VALID_KEY("ID-002", "identifiers", "uppercase identifier",
	          ".Foo = 1;", "Foo", "1"),
	VALID_KEY("ID-003", "identifiers", "identifier with underscore",
	          ".foo_bar = 1;", "foo_bar", "1"),
	VALID_KEY("ID-004", "identifiers", "identifier with hyphen",
	          ".my-key = 1;", "my-key", "1"),
	VALID_KEY("ID-005", "identifiers", "identifier with plus",
	          ".my+key = 1;", "my+key", "1"),
	VALID_KEY("ID-006", "identifiers", "identifier starting with underscore",
	          "._private = 1;", "_private", "1"),
	VALID_KEY("ID-007", "identifiers", "identifier with digits in body",
	          ".foo123 = 1;", "foo123", "1"),
	ERROR_CASE("ID-008", "identifiers", "empty identifier after dot",
	           ". = 1;",
	           error_empty_identifier),
	ERROR_CASE("ID-009", "identifiers", "identifier starting with digit",
	           ".123foo = 1;",
	           error_unexpected_input_byte),
	ERROR_CASE("ID-010", "identifiers", "comma inside identifier",
	           ".foo,bar = 1;",
	           error_unexpected_input_byte),
	ERROR_LIM("ID-011", "identifiers", "identifier too long",
	          ".abcdefghij = 1;",
	          error_identifier_too_long, 5, 0, 0, 0, 0, 0),

	/* ── STRINGS ─────────────────────────────────────────────────── */
	VALID_KEY("STR-001", "strings", "simple string",
	          ".s = \"hello\";", "s", "hello"),
	VALID_KEY("STR-002", "strings", "empty string",
	          ".s = \"\";", "s", ""),
	VALID_KEY("STR-003", "strings", "string with tab escape",
	          ".s = \"a\\tb\";", "s", "a\tb"),
	VALID_KEY("STR-004", "strings", "string with newline escape",
	          ".s = \"a\\nb\";", "s", "a\nb"),
	VALID_KEY("STR-005", "strings", "string with carriage return escape",
	          ".s = \"a\\rb\";", "s", "a\rb"),
	VALID_KEY("STR-006", "strings", "string with vertical tab escape",
	          ".s = \"a\\vb\";", "s", "a\vb"),
	VALID_KEY("STR-007", "strings", "string with form feed escape",
	          ".s = \"a\\fb\";", "s", "a\fb"),
	VALID_KEY("STR-008", "strings", "string with backslash escape",
	          ".s = \"a\\\\b\";", "s", "a\\b"),
	VALID_KEY("STR-009", "strings", "string with quote escape",
	          ".s = \"a\\\"b\";", "s", "a\"b"),
	VALID_KEY("STR-010", "strings", "string concatenation",
	          ".s = \"hello\" \" \" \"world\";", "s", "hello world"),
	VALID_KEY("STR-011", "strings", "string with UTF-8 multibyte",
	          ".s = \"caf\xC3\xA9\";", "s", "caf\xC3\xA9"),
	VALID("STR-012", "strings", "utf8 type annotation on string",
	      ".s = <utf8> \"hello\";"),
	ERROR_CASE("STR-013", "strings", "illegal escape sequence in string",
	           ".s = \"\\q\";",
	           error_illegal_escape_sequence),
	ERROR_CASE("STR-014", "strings", "raw control byte 0x01 in string",
	           ".s = \"\x01\";",
	           error_unexpected_input_byte),
	/* DEL (0x7F) after a run of ordinary bytes: exercises the bulk-copy
	 * fast path, which must reject it identically to the per-byte path
	 * (regression guard against bvn_string_ext_end drifting from the
	 * authoritative transition table's BVN_REJECT_ASCII_CTRL). */
	ERROR_CASE("STR-017", "strings", "raw DEL 0x7F mid-string (bulk path)",
	           ".s = \"aaaaaaaa\x7f\";",
	           error_unexpected_input_byte),
	ERROR_LIM("STR-015", "strings", "string too long",
	          ".s = \"abcdefghij\";",
	          error_string_too_long, 0, 5, 0, 0, 0, 0),
	ERROR_CASE("STR-016", "strings", "utf8 type with number value",
	           ".s = <utf8> 42;",
	           error_type_value_mismatch),
	/* ── \x / \u escapes (spec 1.1) ──────────────────────────────── */
	VALID_KEY("STR-018", "strings", "\\u{...} decodes a code point",
	          "#!bovnar 1.1\n.s = \"\\u{41}\";", "s", "A"),
	VALID_KEY("STR-019", "strings", "\\xHH decodes a byte",
	          "#!bovnar 1.1\n.s = \"\\x41\";", "s", "A"),
	VALID_KEY("STR-020", "strings", "\\x pair spells a UTF-8 char",
	          "#!bovnar 1.1\n.s = \"caf\\xC3\\xA9\";", "s", "caf\xC3\xA9"),
	VALID_KEY("STR-021", "strings", "\\u{...} astral plane (4-byte UTF-8)",
	          "#!bovnar 1.1\n.s = \"\\u{1F600}\";", "s", "\xF0\x9F\x98\x80"),
	ERROR_CASE("STR-022", "strings", "\\u{...} surrogate is rejected",
	           "#!bovnar 1.1\n.s = \"\\u{D800}\";",
	           error_invalid_codepoint),
	ERROR_CASE("STR-023", "strings", "\\u{...} above U+10FFFF is rejected",
	           "#!bovnar 1.1\n.s = \"\\u{110000}\";",
	           error_invalid_codepoint),
	ERROR_CASE("STR-024", "strings", "lone \\xFF breaks UTF-8",
	           "#!bovnar 1.1\n.s = \"\\xFF\";",
	           error_invalid_utf8_byte),
	ERROR_CASE("STR-025", "strings", "\\x with a non-hex digit",
	           "#!bovnar 1.1\n.s = \"\\xZZ\";",
	           error_illegal_escape_sequence),
	ERROR_CASE("STR-026", "strings", "empty \\u{}",
	           "#!bovnar 1.1\n.s = \"\\u{}\";",
	           error_illegal_escape_sequence),
	ERROR_CASE("STR-027", "strings", "\\u without braces",
	           "#!bovnar 1.1\n.s = \"\\u41\";",
	           error_illegal_escape_sequence),
	ERROR_CASE("STR-028", "strings", "\\u is illegal in an unversioned (1.0) document",
	           ".s = \"\\u{41}\";",
	           error_illegal_escape_sequence),
	ERROR_CASE("STR-029", "strings", "\\x is illegal in an unversioned (1.0) document",
	           ".s = \"\\x41\";",
	           error_illegal_escape_sequence),
	ERROR_CASE("STR-030", "strings", "\\u{0} (NUL) control byte is rejected",
	           "#!bovnar 1.1\n.s = \"\\u{0}\";",
	           error_unexpected_input_byte),
	ERROR_CASE("STR-031", "strings", "\\x01 control byte is rejected",
	           "#!bovnar 1.1\n.s = \"\\x01\";",
	           error_unexpected_input_byte),
	ERROR_CASE("STR-032", "strings", "\\x7f (DEL) is rejected",
	           "#!bovnar 1.1\n.s = \"\\x7f\";",
	           error_unexpected_input_byte),
	VALID_KEY("STR-033", "strings", "\\x09 (a whitespace control) is allowed",
	          "#!bovnar 1.1\n.s = \"a\\x09b\";", "s", "a\tb"),

	/* ── DATETIME family (spec 1.1) ──────────────────────────────── */
	VALID_KEY("DT-001", "datetime", "unix epoch seconds",
	          "#!bovnar 1.1\n.t = <datetime:64,unix> 1750000000;",
	          "t", "1750000000"),
	VALID("DT-002", "datetime", "bare <datetime> defaults to unix/64",
	      "#!bovnar 1.1\n.t = <datetime> 0;"),
	VALID("DT-003", "datetime", "negative (pre-epoch) instant",
	      "#!bovnar 1.1\n.t = <datetime> -100;"),
	VALID("DT-004", "datetime", "epoch as first parameter",
	      "#!bovnar 1.1\n.t = <datetime:tai> 42;"),
	VALID("DT-005", "datetime", "width and epoch",
	      "#!bovnar 1.1\n.t = <datetime:64,gps> 99;"),
	ERROR_CASE("DT-006", "datetime", "fractional value is rejected",
	           "#!bovnar 1.1\n.t = <datetime> 1.5;",
	           error_type_value_mismatch),
	ERROR_CASE("DT-007", "datetime", "exponent value is rejected",
	           "#!bovnar 1.1\n.t = <datetime> 1e3;",
	           error_type_value_mismatch),
	ERROR_CASE("DT-008", "datetime", "unknown epoch name",
	           "#!bovnar 1.1\n.t = <datetime:xyz> 1;",
	           error_illegal_value_type),
	ERROR_CASE("DT-009", "datetime", "numeric base is not allowed",
	           "#!bovnar 1.1\n.t = <datetime:_16> 1;",
	           error_illegal_value_type),
	ERROR_CASE("DT-010", "datetime", "value out of range for width",
	           "#!bovnar 1.1\n.t = <datetime:8> 300;",
	           error_value_out_of_range),
	ERROR_CASE("DT-011", "datetime", "datetime is illegal in an unversioned (1.0) document",
	           ".t = <datetime> 1;",
	           error_illegal_value_type),
	ERROR_CASE("DT-012", "datetime", "string carrier is rejected",
	           "#!bovnar 1.1\n.t = <datetime> \"x\";",
	           error_type_value_mismatch),
	/* A datetime carries no unit in ANY form. DTLIT-111 rejects the ISO-literal
	 * form; this rejects the plain integer carrier, which the validator once
	 * accepted (silently dropping the unit on emit). */
	ERROR_CASE("DT-013", "datetime", "a unit on the integer carrier is rejected",
	           "#!bovnar 1.1\n.t = <datetime:64> 100 m;",
	           error_unit_illegal),

	/* ── ISO-8601 datetime literals (spec 1.1) ───────────────────── */
	VALID_KEY("DTLIT-001", "datetime", "bare date-only literal infers unix midnight",
	          "#!bovnar 1.1\n.t = 2026-06-15;",
	          "t", "1781481600"),
	VALID_KEY("DTLIT-002", "datetime", "bare date-time Z literal infers unix",
	          "#!bovnar 1.1\n.t = 2026-06-15T12:00:00Z;",
	          "t", "1781524800"),
	VALID_KEY("DTLIT-003", "datetime", "unix epoch literal is zero",
	          "#!bovnar 1.1\n.t = 1970-01-01T00:00:00Z;",
	          "t", "0"),
	VALID_KEY("DTLIT-004", "datetime", "date-time without Z is treated as UTC",
	          "#!bovnar 1.1\n.t = 2026-06-15T12:00:00;",
	          "t", "1781524800"),
	VALID_KEY("DTLIT-005", "datetime", "tai literal is leap-second correct (2017: +37)",
	          "#!bovnar 1.1\n.t = <datetime:64,tai> 2017-01-01T00:00:00Z;",
	          "t", "1861920037"),
	VALID_KEY("DTLIT-006", "datetime", "pre-epoch literal is a negative count",
	          "#!bovnar 1.1\n.t = 1960-01-01;",
	          "t", "-315619200"),
	VALID_KEY("DTLIT-007", "datetime", "explicit <datetime> (unix) date-only literal",
	          "#!bovnar 1.1\n.t = <datetime> 2026-06-15;",
	          "t", "1781481600"),
	VALID("DTLIT-008", "datetime", "homogeneous array of date literals",
	      "#!bovnar 1.1\n.ts = [2026-01-01, 2026-01-02, 2026-01-03];"),
	VALID_KEY("DTLIT-009", "datetime", "Feb 29 in a leap year is valid",
	          "#!bovnar 1.1\n.t = 2024-02-29;",
	          "t", "1709164800"),
	ERROR_CASE("DTLIT-101", "datetime", "month 13 is rejected",
	           "#!bovnar 1.1\n.t = 2026-13-01;",
	           error_invalid_datetime_literal),
	ERROR_CASE("DTLIT-102", "datetime", "day past month length is rejected",
	           "#!bovnar 1.1\n.t = 2026-06-31;",
	           error_invalid_datetime_literal),
	ERROR_CASE("DTLIT-103", "datetime", "Feb 29 in a non-leap year is rejected",
	           "#!bovnar 1.1\n.t = 2025-02-29;",
	           error_invalid_datetime_literal),
	ERROR_CASE("DTLIT-104", "datetime", "a three-digit month field is rejected",
	           "#!bovnar 1.1\n.t = 2026-006-15;",
	           error_invalid_datetime_literal),
	ERROR_CASE("DTLIT-105", "datetime", "hour 24 is rejected",
	           "#!bovnar 1.1\n.t = 2026-06-15T24:00:00Z;",
	           error_invalid_datetime_literal),
	ERROR_CASE("DTLIT-106", "datetime", "literal for a gps epoch is unsupported",
	           "#!bovnar 1.1\n.t = <datetime:64,gps> 2026-01-01;",
	           error_datetime_literal_unsupported_epoch),
	ERROR_CASE("DTLIT-107", "datetime", "literal for a glonass epoch is unsupported",
	           "#!bovnar 1.1\n.t = <datetime:64,glonass> 2026-01-01;",
	           error_datetime_literal_unsupported_epoch),
	ERROR_CASE("DTLIT-108", "datetime", "ISO literal under a non-datetime annotation",
	           "#!bovnar 1.1\n.t = <sint:32> 2026-06-15;",
	           error_type_value_mismatch),
	ERROR_CASE("DTLIT-109", "datetime", "ISO literal is illegal in an unversioned (1.0) document",
	           ".t = 2026-06-15;",
	           error_illegal_value_type),
	ERROR_CASE("DTLIT-110", "datetime", "literal value out of range for the width",
	           "#!bovnar 1.1\n.t = <datetime:8> 2026-06-15;",
	           error_value_out_of_range),
	ERROR_CASE("DTLIT-111", "datetime", "a unit on a datetime literal is rejected",
	           "#!bovnar 1.1\n.t = 2026-06-15 kg;",
	           error_unit_illegal),
	/* A datetime array's canonical form annotates only the first element; the
	 * bare integers after it must inherit datetime so the DOM accepts the
	 * re-read array as homogeneous (regression: bare elements used to infer
	 * uint, making datetime/uint mix that the DOM rejected). */
	DOM_VALID("DTLIT-112", "datetime", "datetime array with bare trailing elements is homogeneous",
	          "#!bovnar 1.1\n.t = [<datetime:64> 100, 200, 300];"),
	DOM_VALID("DTLIT-113", "datetime", "bare ISO-literal array is a homogeneous datetime array",
	          "#!bovnar 1.1\n.t = [2026-01-01, 2026-01-02, 2026-01-03];"),
	/* Fractional seconds (spec 1.1): the whole-second carrier is unchanged; the
	 * fraction digits are preserved separately (and round-trip as a literal). */
	VALID_KEY("DTLIT-114", "datetime", "fractional seconds floor to the whole-second carrier",
	          "#!bovnar 1.1\n.t = 2026-06-15T12:00:00.999Z;",
	          "t", "1781524800"),
	VALID_KEY("DTLIT-115", "datetime", "nanosecond fraction floors to the whole-second carrier",
	          "#!bovnar 1.1\n.t = 2026-06-15T12:00:00.000000123;",
	          "t", "1781524800"),
	/* tz offsets (spec 1.1): folded to true UTC. */
	VALID_KEY("DTLIT-116", "datetime", "positive tz offset folds to UTC",
	          "#!bovnar 1.1\n.t = 2026-06-15T12:00:00+02:00;",
	          "t", "1781517600"),
	VALID_KEY("DTLIT-117", "datetime", "negative tz offset folds to UTC",
	          "#!bovnar 1.1\n.t = 2026-06-15T12:00:00-05:00;",
	          "t", "1781542800"),
	VALID_KEY("DTLIT-118", "datetime", "tz offset crossing the day boundary",
	          "#!bovnar 1.1\n.t = 2026-06-15T01:00:00+02:00;",
	          "t", "1781478000"),
	VALID_KEY("DTLIT-119", "datetime", "fractional and offset combined",
	          "#!bovnar 1.1\n.t = 2026-06-15T12:00:00.5+02:00;",
	          "t", "1781517600"),
	VALID_KEY("DTLIT-120", "datetime", "tai offset is leap-correct (folded before the leap lookup)",
	          "#!bovnar 1.1\n.t = <datetime:64,tai> 2017-01-01T02:00:00+02:00;",
	          "t", "1861920037"),
	ERROR_CASE("DTLIT-121", "datetime", "fractional point with no digit is rejected",
	           "#!bovnar 1.1\n.t = 2026-06-15T12:00:00.Z;",
	           error_invalid_datetime_literal),
	ERROR_CASE("DTLIT-122", "datetime", "one-digit offset hour is rejected",
	           "#!bovnar 1.1\n.t = 2026-06-15T12:00:00+2:00;",
	           error_invalid_datetime_literal),
	ERROR_CASE("DTLIT-123", "datetime", "offset without a colon is rejected",
	           "#!bovnar 1.1\n.t = 2026-06-15T12:00:00+0200;",
	           error_unexpected_input_byte),
	ERROR_CASE("DTLIT-124", "datetime", "a tz offset on a date-only literal is rejected",
	           "#!bovnar 1.1\n.t = 2026-06-15+02:00;",
	           error_unexpected_input_byte),
	ERROR_CASE("DTLIT-125", "datetime", "both Z and an offset is rejected",
	           "#!bovnar 1.1\n.t = 2026-06-15T12:00:00Z+02:00;",
	           error_unexpected_input_byte),

	/* ── NUMBERS ─────────────────────────────────────────────────── */
	VALID_KEY("NUM-001", "numbers", "plain unsigned integer",
	          ".x = 42;", "x", "42"),
	VALID_KEY("NUM-002", "numbers", "zero",
	          ".x = 0;", "x", "0"),
	VALID_KEY("NUM-003", "numbers", "negative integer (sint synthesis)",
	          ".x = -7;", "x", "-7"),
	VALID_KEY("NUM-004", "numbers", "float with decimal point",
	          ".x = 3.14;", "x", "3.14"),
	VALID_KEY("NUM-005", "numbers", "dot-led float",
	          ".x = .5;", "x", ".5"),
	VALID_KEY("NUM-006", "numbers", "float with exponent",
	          ".x = 1.23e10;", "x", "1.23e10"),
	VALID_KEY("NUM-007", "numbers", "float with negative exponent",
	          ".x = 1.5e-10;", "x", "1.5e-10"),
	VALID_KEY("NUM-008", "numbers", "float with positive exponent marker",
	          ".x = 1e+3;", "x", "1e+3"),
	VALID_KEY("NUM-009", "numbers", "special number nan",
	          ".x = nan;", "x", "nan"),
	VALID_KEY("NUM-010", "numbers", "special number inf",
	          ".x = inf;", "x", "inf"),
	VALID_KEY("NUM-011", "numbers", "special number ninf",
	          ".x = ninf;", "x", "ninf"),
	VALID("NUM-012", "numbers", "large uint64 at boundary",
	      ".x = <uint:64> 18446744073709551615;"),
	VALID("NUM-013", "numbers", "sint64 at negative boundary",
	      ".x = <sint:64> -9223372036854775808;"),
	VALID("NUM-014", "numbers", "two numeric assignments in sequence",
	      ".a = 1; .b = 2;"),
	ERROR_LIM("NUM-015", "numbers", "number too long",
	          ".x = 12345678901234;",
	          error_number_too_long, 0, 0, 5, 0, 0, 0),
	VALID_KEY("NUM-016", "numbers", "leading zeros are valid",
	          ".x = 007;", "x", "007"),

	/* ── TYPE ANNOTATIONS ────────────────────────────────────────── */
	VALID("TYP-001", "types", "uint:8",
	      ".x = <uint:8> 255;"),
	VALID("TYP-002", "types", "uint:16",
	      ".x = <uint:16> 65535;"),
	VALID("TYP-003", "types", "uint:32",
	      ".x = <uint:32> 4294967295;"),
	VALID("TYP-004", "types", "uint:64",
	      ".x = <uint:64> 0;"),
	VALID("TYP-005", "types", "sint:8 positive",
	      ".x = <sint:8> 127;"),
	VALID("TYP-006", "types", "sint:8 negative",
	      ".x = <sint:8> -128;"),
	VALID("TYP-007", "types", "sint:16",
	      ".x = <sint:16> -32768;"),
	VALID("TYP-008", "types", "sint:32",
	      ".x = <sint:32> -2147483648;"),
	VALID("TYP-009", "types", "float:32",
	      ".x = <float:32> 1.0;"),
	VALID("TYP-010", "types", "float:64",
	      ".x = <float:64> 3.14159265358979;"),
	VALID("TYP-011", "types", "float_fix:32,q16",
	      ".x = <float_fix:32,q16> 1.5;"),
	VALID("TYP-011a", "types", "float_fix:16,q8 max in range",
	      ".x = <float_fix:16,q8> 127.99609375;"),
	VALID("TYP-011b", "types", "float_fix:16,q8 min in range",
	      ".x = <float_fix:16,q8> -128;"),
	ERROR_CASE("TYP-011c", "types", "float_fix:16,q8 value over range",
	      ".x = <float_fix:16,q8> 128;",
	      error_value_out_of_range),
	ERROR_CASE("TYP-011d", "types", "float_fix:16,q8 value far over range",
	      ".x = <float_fix:16,q8> 1000000;",
	      error_value_out_of_range),
	ERROR_CASE("TYP-011e", "types", "float_fix:16,q0 integer over range",
	      ".x = <float_fix:16,q0> 70000;",
	      error_value_out_of_range),
	VALID("TYP-012", "types", "float_dec:64",
	      ".x = <float_dec:64> 3.14;"),
	VALID("TYP-013", "types", "uint:_16 base annotation (hex string)",
	      ".x = <uint:8,_16> \"FF\";"),
	VALID("TYP-014", "types", "uint:_2 base annotation (binary string)",
	      ".x = <uint:8,_2> \"11111111\";"),
	VALID("TYP-015", "types", "uint:_8 base annotation (octal string)",
	      ".x = <uint:8,_8> \"377\";"),
	VALID("TYP-016", "types", "sint negative hex",
	      ".x = <sint:8,_16> \"-7F\";"),
	VALID("TYP-017", "types", "special number with explicit float type",
	      ".x = <float:64> nan;"),
	VALID("TYP-018", "types", "special number with sint type",
	      ".x = <sint:32> inf;"),
	ERROR_CASE("TYP-019", "types", "uint:8 value 256 out of range",
	           ".x = <uint:8> 256;",
	           error_value_out_of_range),
	ERROR_CASE("TYP-020", "types", "sint:8 value 128 out of range",
	           ".x = <sint:8> 128;",
	           error_value_out_of_range),
	ERROR_CASE("TYP-021", "types", "sint:8 value -129 out of range",
	           ".x = <sint:8> -129;",
	           error_value_out_of_range),
	/* Arbitrary-width (>64-bit) integers use the bignum range check; an
	 * over-range value must report error_value_out_of_range, not the
	 * error_none the path left set before the fix. uint:128 max is 2^128-1;
	 * this is 2^128. */
	VALID_KEY("TYP-021b", "types", "uint:128 max (2^128-1) in range",
	          ".x = <uint:128> 340282366920938463463374607431768211455;",
	          "x", "340282366920938463463374607431768211455"),
	ERROR_CASE("TYP-021c", "types", "uint:128 value 2^128 out of range",
	           ".x = <uint:128> 340282366920938463463374607431768211456;",
	           error_value_out_of_range),
	ERROR_CASE("TYP-021d", "types", "sint:128 below min out of range",
	           ".x = <sint:128> -170141183460469231731687303715884105729;",
	           error_value_out_of_range),
	ERROR_CASE("TYP-022", "types", "digit not in base-2",
	           ".x = <uint:8,_2> \"2\";",
	           error_digit_not_in_base),
	ERROR_CASE("TYP-023", "types", "digit not in base-16",
	           ".x = <uint:8,_16> \"GG\";",
	           error_digit_not_in_base),
	VALID("TYP-023b", "types", "base-16 uint with e digit (e=14 in hex)",
	      ".x = <uint:16,_16> \"ae\";"),
	VALID("TYP-023c", "types", "base-16 uint string 1e1 = 481",
	      ".x = <uint:16,_16> \"1e1\";"),
	VALID("TYP-023d", "types", "base-16 uint string ffee",
	      ".x = <uint:32,_16> \"ffee\";"),
	ERROR_CASE("TYP-024", "types", "illegal value type float_dec width 8",
	           ".x = <float_dec:8> 1.0;",
	           error_illegal_value_type),
	ERROR_CASE("TYP-025", "types", "illegal value type float_dec with base",
	           ".x = <float_dec:64,_10> 1.0;",
	           error_illegal_value_type),
	ERROR_CASE("TYP-026", "types", "utf8 type with number value",
	           ".x = <utf8> 42;",
	           error_type_value_mismatch),
	/* utf8 and bool are parameterless families: any width/base/unit is
	 * error_illegal_value_type (symmetric — utf8 no longer accepts and
	 * silently ignores parameters). */
	ERROR_CASE("TYP-031", "types", "utf8 with width parameter",
	           ".x = <utf8:8> \"hi\";",
	           error_illegal_value_type),
	ERROR_CASE("TYP-032", "types", "utf8 with unit parameter",
	           ".x = <utf8:m> \"hi\";",
	           error_illegal_value_type),
	ERROR_CASE("TYP-033", "types", "bool with width parameter",
	           ".x = <bool:8> true;",
	           error_illegal_value_type),
	/* Strict type-param-list: empty / trailing / bare-colon parameter
	 * components are rejected (a comma must introduce a real parameter,
	 * and ":" requires a non-empty list). Guards against the re-parser
	 * drifting lenient from the EBNF type-param-list production. */
	ERROR_CASE("TYP-027", "types", "trailing comma in type params",
	           ".x = <uint:8,> 5;",
	           error_illegal_value_type),
	ERROR_CASE("TYP-028", "types", "double comma (empty middle param)",
	           ".x = <uint:8,,> 5;",
	           error_illegal_value_type),
	ERROR_CASE("TYP-029", "types", "empty width before base",
	           ".x = <uint:,_16> \"ff\";",
	           error_illegal_value_type),
	ERROR_CASE("TYP-030", "types", "bare colon with empty param list",
	           ".x = <uint:> 5;",
	           error_illegal_value_type),
	/* Each parameter class may appear at most once (EBNF type-param-list,
	 * spec §501). A repeated class is rejected rather than silently taking
	 * the last occurrence — last-wins could otherwise mask a real range
	 * violation, e.g. "<uint:8,16> 300" accepting 300 under width 16. */
	ERROR_CASE("TYP-034", "types", "duplicate width parameter",
	           ".x = <uint:8,16> 300;",
	           error_illegal_value_type),
	ERROR_CASE("TYP-035", "types", "duplicate base parameter",
	           ".x = <uint:8,_2,_16> \"ff\";",
	           error_illegal_value_type),
	ERROR_CASE("TYP-036", "types", "duplicate q parameter",
	           ".x = <float_fix:16,q4,q8> 1.0;",
	           error_illegal_value_type),
	ERROR_CASE("TYP-037", "types", "duplicate unit parameter",
	           ".x = <float:64,m/s,kg> 1.0;",
	           error_illegal_value_type),
	/* Bases 64 (standard Base64) and 85 (Ascii85) use '+'/'-'/'.' etc. as
	 * digit characters, so they carry no sign and are unsigned-only. The full
	 * alphabet must round-trip for uint (TYP-038/039), and a signed integer in
	 * these bases is an illegal type (TYP-040/041). */
	VALID_KEY("TYP-038", "types", "uint base-64 with '+' digit (62)",
	          ".x = <uint:64,_64> \"z+\";", "x", "z+"),
	VALID_KEY("TYP-039", "types", "uint base-85 with '-' and '.' digits",
	          ".x = <uint:64,_85> \"-.\";", "x", "-."),
	ERROR_CASE("TYP-040", "types", "sint illegal in base 64 (no sign char)",
	           ".x = <sint:8,_64> \"A\";",
	           error_illegal_value_type),
	ERROR_CASE("TYP-041", "types", "sint illegal in base 85 (no sign char)",
	           ".x = <sint:16,_85> \"AB\";",
	           error_illegal_value_type),

	/* ── DEFAULT TYPE SYNTHESIS ──────────────────────────────────── */
	VALID("DTS-001", "default_synthesis", "plain integer → uint:64",
	      ".x = 100;"),
	VALID("DTS-002", "default_synthesis", "negative integer → sint:64",
	      ".x = -1;"),
	VALID("DTS-003", "default_synthesis", "float literal → float:64",
	      ".x = 1.5;"),
	VALID("DTS-004", "default_synthesis", "nan → float:64",
	      ".x = nan;"),
	VALID("DTS-005", "default_synthesis", "string → utf8",
	      ".x = \"hello\";"),
	VALID("DTS-006", "default_synthesis", "untyped uint and explicit uint:64 are both valid",
	      ".a = 42; .b = <uint:64> 42;"),
	VALID("DTS-007", "default_synthesis", "untyped negative int and explicit sint:64 are both valid",
	      ".a = -1; .b = <sint:64> -1;"),
	VALID("DTS-008", "default_synthesis", "untyped float and explicit float:64 are both valid",
	      ".a = 1.5; .b = <float:64> 1.5;"),

	/* ── SYMBOLS ─────────────────────────────────────────────────── */
	VALID_KEY("SYM-001", "symbols", "simple symbol",
	          ".s = ok;", "s", "ok"),
	VALID_KEY("SYM-002", "symbols", "boolean-like symbol true",
	          ".b = true;", "b", "true"),
	VALID_KEY("SYM-003", "symbols", "boolean-like symbol false",
	          ".b = false;", "b", "false"),
	VALID_KEY("SYM-004", "symbols", "enum-like symbol",
	          ".d = Monday;", "d", "Monday"),
	VALID("SYM-005", "symbols", "symbol with hyphen",
	      ".s = my-symbol;"),
	ERROR_SYM("SYM-006", "symbols", "symbol too long",
	          ".s = abcdefghij;",
	          error_symbol_too_long, 5),

	/* ── REFERENCES ──────────────────────────────────────────────── */
	VALID_KEY("REF-001", "references", "simple reference",
	          ".r = &.host;", "r", ".host"),
	VALID_KEY("REF-002", "references", "nested reference path",
	          ".r = &.config.host;", "r", ".config.host"),
	VALID_KEY("REF-003", "references", "three-level reference",
	          ".r = &.a.b.c;", "r", ".a.b.c"),
	ERROR_REF("REF-004", "references", "reference too long",
	          ".r = &.abcdefghijklmno;",
	          error_reference_too_long, 5),
	/* Array indexing in a reference path (spec 1.1); stored unresolved, the
	 * index syntax is captured verbatim into the path string. */
	VALID_KEY("REF-005", "references", "single array index",
	          "#!bovnar 1.1\n.r = &.a[0];", "r", ".a[0]"),
	VALID_KEY("REF-006", "references", "two-dimensional index",
	          "#!bovnar 1.1\n.r = &.matrix[0][1];", "r", ".matrix[0][1]"),
	VALID_KEY("REF-007", "references", "index then field",
	          "#!bovnar 1.1\n.r = &.rows[2].name;", "r", ".rows[2].name"),
	VALID("REF-008", "references", "reference with an index inside an array",
	      "#!bovnar 1.1\n.r = [&.a[0]];"),
	ERROR_CASE("REF-009", "references", "index in a reference needs spec 1.1",
	           ".r = &.a[0];",
	           error_unexpected_input_byte),
	ERROR_CASE("REF-010", "references", "an empty reference index is rejected",
	           "#!bovnar 1.1\n.r = &.a[];",
	           error_unexpected_input_byte),

	/* ── NULL VALUES ─────────────────────────────────────────────── */
	VALID("NUL-001", "null_values", "null scalar value",
	      ".x = ;"),
	VALID("NUL-002", "null_values", "null in array leading comma",
	      ".a = [,1,2];"),
	VALID("NUL-003", "null_values", "null in array trailing comma",
	      ".a = [1,2,];"),
	VALID("NUL-004", "null_values", "null in array middle",
	      ".a = [1,,2];"),
	VALID("NUL-005", "null_values", "typed null with uint annotation",
	      ".x = <uint:32> ;"),

	/* ── STRUCTS ─────────────────────────────────────────────────── */
	VALID("STU-001", "structs", "simple struct",
	      ".s = { .x = 1; .y = 2; };"),
	VALID("STU-002", "structs", "empty struct",
	      ".s = {};"),
	VALID("STU-003", "structs", "nested struct two levels",
	      ".a = { .b = { .c = 1; }; };"),
	VALID("STU-004", "structs", "struct with string and integer",
	      ".s = { .name = \"Alice\"; .age = 30; };"),
	VALID("STU-005", "structs", "struct after scalar assignment",
	      ".x = 1; .s = { .y = 2; };"),
	ERROR_CASE("STU-006", "structs", "unmatched closing brace",
	           ".x = 1; };",
	           error_illegal_struct_close),
	ERROR_LIM("STU-007", "structs", "struct nesting too deep",
	          ".a = { .b = { .c = {}; }; };",
	          error_struct_nesting_too_high, 0, 0, 0, 2, 0, 0),

	/* ── ARRAYS ──────────────────────────────────────────────────── */
	VALID("ARR-001", "arrays", "simple 1D integer array",
	      ".a = [1, 2, 3];"),
	VALID("ARR-002", "arrays", "2D array with row separator",
	      ".a = [1, 2, 3]/[4, 5, 6];"),
	VALID("ARR-003", "arrays", "1D string array",
	      ".a = [\"a\", \"b\", \"c\"];"),
	VALID("ARR-004", "arrays", "mixed null elements in array",
	      ".a = [,1,,2,];"),
	VALID("ARR-005", "arrays", "typed array annotation",
	      ".a = <uint:8> [1, 2, 255];"),
	VALID("ARR-006", "arrays", "typed element annotation in array",
	      ".a = [<uint:8> 1, <uint:8> 2];"),
	VALID("ARR-007", "arrays", "nested array as element",
	      ".a = [[1, 2], [3, 4]];"),
	VALID("ARR-008", "arrays", "array of structs",
	      ".a = [{.x = 1;}, {.x = 2;}];"),
	VALID("ARR-009", "arrays", "2D array equal-length rows",
	      ".a = [1, 2]/[3, 4];"),
	VALID("ARR-010", "arrays", "single-element array",
	      ".a = [42];"),
	VALID("ARR-011", "arrays", "array with hex-typed elements",
	      ".a = <uint:8,_16> [\"FF\", \"00\", \"AA\"];"),
	ERROR_LIM("ARR-012", "arrays", "array nesting too deep",
	          ".a = [[[1]]];",
	          error_array_nesting_too_high, 0, 0, 0, 0, 2, 0),
	ERROR_LIM("ARR-013", "arrays", "too many array items",
	          ".a = [1, 2, 3, 4, 5];",
	          error_too_many_array_items, 0, 0, 0, 0, 0, 3),
	/*
	 * Row-size consistency is per-array and scoped to '/'-dimension rows only
	 * (Model: per-array rectangular, not globally rectangular). The '/'-rows of
	 * one array must match; comma-separated elements — including sub-arrays —
	 * are independent and may be ragged.
	 */
	ERROR_CASE("ARR-014", "arrays", "/-dimension rows of one array must match",
	           ".a = [1, 2, 3]/[4, 5];",
	           error_array_row_size_mismatch),
	ERROR_CASE("ARR-015", "arrays", "third /-row disagrees with established width",
	           ".a = [1, 2]/[3, 4]/[5, 6, 7];",
	           error_array_row_size_mismatch),
	ERROR_CASE("ARR-016", "arrays", "nested /-array's own rows must match",
	           ".a = [[1, 2]/[3, 4, 5]];",
	           error_array_row_size_mismatch),
	VALID("ARR-017", "arrays", "ragged comma-separated sub-arrays are independent",
	      ".a = [[1, 2], [3, 4, 5]];"),
	VALID("ARR-018", "arrays", "sibling /-blocks may differ in leaf width",
	      ".a = [[1, 2]/[3, 4], [5, 6, 7]/[8, 9, 10]];"),
	VALID("ARR-019", "arrays", "/-row width does not leak across a sibling branch",
	      ".a = [[1, 2]/[3, 4]]/[[5, 6, 7]/[8, 9, 10]];"),

	/* ── OCTET STREAMS ───────────────────────────────────────────── */
	VALID_BIN("OCT-001", "octet_streams", "single-chunk octet stream",
	          g_oct_single, (uint32_t)sizeof(g_oct_single)),
	VALID_BIN("OCT-002", "octet_streams", "two-chunk octet stream",
	          g_oct_two_chunks, (uint32_t)sizeof(g_oct_two_chunks)),
	VALID_BIN("OCT-003", "octet_streams", "empty octet stream",
	          g_oct_empty, (uint32_t)sizeof(g_oct_empty)),
	ERROR_BIN("OCT-004", "octet_streams", "invalid chunk tag in octet stream",
	          g_oct_bad_tag, (uint32_t)sizeof(g_oct_bad_tag),
	          error_octet_stream_out_of_sync),

	/* ── UNITS ───────────────────────────────────────────────────── */
	VALID("UNT-001", "units", "simple SI unit meter",
	      ".d = <float:64,m> 1.5;"),
	VALID("UNT-002", "units", "SI prefix kilo-meter",
	      ".d = <float:64,k~m> 1.5;"),
	VALID("UNT-003", "units", "IEC prefix kibi-byte",
	      ".mem = <uint:64,Ki~B> 4;"),
	VALID("UNT-004", "units", "compound unit m/s",
	      ".v = <float:64,m/s> 9.81;"),
	VALID("UNT-005", "units", "compound unit m/s^2",
	      ".a = <float:64,m/s^2> 9.81;"),
	VALID("UNT-006", "units", "compound unit kg*m/s^2",
	      ".f = <float:64,k~g*m/s^2> 1.0;"),
	VALID("UNT-007", "units", "no_unit keyword",
	      ".x = <float:64,no_unit> 3.14;"),
	VALID("UNT-007b", "units", "angstrom U+212B form",
	      ".x = <float:64,\xe2\x84\xab> 1.5;"),
	VALID("UNT-007c", "units", "angstrom U+00C5 Latin form normalises to U+212B",
	      ".x = <float:64,\xc3\x85> 1.5;"),
	VALID("UNT-007d", "units", "angstrom long-form ASCII alias",
	      ".x = <float:64,angstrom> 1.5;"),
	VALID("UNT-008", "units", "uint with byte unit",
	      ".sz = <uint:64,B> 1024;"),
	VALID("UNT-009", "units", "inline unit suffix",
	      ".d = 100 m;"),
	VALID("UNT-010", "units", "inline compound unit suffix",
	      ".v = 9.81 m/s;"),
	VALID("UNT-011", "units", "annotation unit matches inline unit",
	      ".d = <float:64,m> 1.5 m;"),
	/* Unit multiplication is commutative: an annotation unit and an inline
	 * unit that list the same components in a different order denote the
	 * same unit and must reconcile (spec doc/2_bovnar_unit_system.md). A
	 * positional comparison wrongly rejected these. */
	VALID("UNT-011a", "units", "annotation vs inline unit reordered (m/s vs s^-1 m)",
	      ".d = <float:64,m/s> 1.0 s⁻¹·m;"),
	VALID("UNT-011b", "units", "annotation vs inline unit reordered (multi-component)",
	      ".d = <float:64,s³·m⁻⁵> 1.0 m⁻⁵·s³;"),
	VALID("UNT-012", "units", "pascal pressure unit",
	      ".p = <float:64,Pa> 101325;"),
	VALID("UNT-013", "units", "kelvin temperature",
	      ".t = <float:64,K> 273.15;"),
	VALID("UNT-014", "units", "hertz frequency",
	      ".f = <float:64,k~Hz> 2.4;"),
	VALID("UNT-015", "units", "mebi-byte IEC prefix",
	      ".mem = <uint:64,Mi~B> 512;"),
	/* An inline unit may begin with a parenthesised group, the same as a
	 * type-annotation unit (inline-unit-start includes "("). */
	VALID("UNT-015b", "units", "inline unit with leading paren group",
	      ".a = 1.0 (m/s)/s;"),
	VALID("UNT-015c", "units", "annotation unit with leading paren group",
	      ".a = <float:64,(m/s)/s> 1.0;"),
	ERROR_CASE("UNT-015d", "units", "inline unit unbalanced leading paren",
	           ".a = 1.0 (m/s;",
	           error_unit_illegal),
	ERROR_CASE("UNT-016", "units", "annotation unit m, inline unit s",
	           ".d = <float:64,m> 1.5 s;",
	           error_unit_mismatch),
	ERROR_CASE("UNT-017", "units", "illegal unit string",
	           ".x = <float:64,zzz_invalid_unit> 1.0;",
	           error_unit_illegal),

	/* ── SPECIAL NUMBERS ─────────────────────────────────────────── */
	VALID("SPC-001", "special_numbers", "nan with float:64",
	      ".x = <float:64> nan;"),
	VALID("SPC-002", "special_numbers", "inf with float:32",
	      ".x = <float:32> inf;"),
	VALID("SPC-003", "special_numbers", "ninf with float:64",
	      ".x = <float:64> ninf;"),
	VALID("SPC-004", "special_numbers", "nan with uint:8 (range check bypassed)",
	      ".x = <uint:8> nan;"),
	VALID("SPC-005", "special_numbers", "inf with sint:16",
	      ".x = <sint:16> inf;"),

	/* ── ROUNDTRIP / MULTI-ASSIGNMENT ────────────────────────────── */
	VALID("RT-001", "roundtrip", "multiple typed scalars",
	      ".age = <uint:8> 30; .temp = <sint:16> -15; .ratio = <float:64> 0.5;"),
	VALID("RT-002", "roundtrip", "struct with typed fields",
	      ".p = { .x = <float:32> 1.0; .y = <float:32> 2.0; };"),
	VALID("RT-003", "roundtrip", "complex nested structure",
	      ".cfg = { .host = \"localhost\"; .port = <uint:16> 8080; "
	      ".tls = { .enabled = true; }; };"),
	VALID("RT-004", "roundtrip", "array of typed integers",
	      ".data = <uint:8> [0, 127, 255];"),
	VALID("RT-005", "roundtrip", "mixed scalar types in sequence",
	      ".a = \"text\"; .b = 42; .c = -1; .d = 3.14; .e = true;"),

	/* ── ERROR RECOVERY ──────────────────────────────────────────── */
	ERROR_CONT("REC-001", "recovery",
	           "illegal type followed by valid assignment",
	           ".first = 1; .broken = <float:64,_2> 1.0; .second = 2;",
	           error_illegal_value_type),
	ERROR_CONT("REC-002", "recovery",
	           "out-of-range value followed by valid assignment",
	           ".x = <uint:8> 999; .y = 1;",
	           error_value_out_of_range),

	/* ── COMMENT HANDLING ─────────────────────────────────────────── */
	VALID("CMT-001", "comments", "full-line comment",
	      "# this is a comment\n.x = 1;"),
	VALID("CMT-002", "comments", "inline comment after assignment",
	      ".x = 1; # inline comment"),
	VALID("CMT-003", "comments", "multiple comment lines",
	      "# line 1\n# line 2\n.x = 1;"),
	VALID("CMT-004", "comments", "comment between assignments",
	      ".x = 1;\n# between\n.y = 2;"),
	/* A document that is only a leading comment terminated by EOF (no
	 * trailing newline) is a valid empty document: the comment production
	 * ends on CR | LF | EOF. Guards the first_comment_intro EOF-accept. */
	VALID("CMT-005", "comments", "comment-only document ending at EOF",
	      "# just a comment, no trailing newline"),
	VALID("CMT-006", "comments", "two comment lines, last unterminated",
	      "# line 1\n# line 2 no newline"),

	/* ── WHITESPACE HANDLING ─────────────────────────────────────── */
	VALID("WS-001", "whitespace", "leading whitespace",
	      "   .x = 1;"),
	VALID("WS-002", "whitespace", "trailing whitespace",
	      ".x = 1;   "),
	VALID("WS-003", "whitespace", "newlines between assignments",
	      ".x = 1;\n\n.y = 2;"),
	VALID("WS-004", "whitespace", "tabs and spaces around operator",
	      ".x\t=\t1\t;"),

	/* ── HOMOGENEITY (materialised-document / DOM tier, spec 1.0) ──── *
	 * Array homogeneity (§7.4), struct shape, and key uniqueness (§8.1)
	 * are enforced above the lexer, so they are validated through
	 * bvn_dom_parse rather than the streaming reader.  These cases run
	 * in self-test mode only (skipped under --iut). */
	DOM_VALID("HOM-001", "homogeneity", "mixed numeric encodings, one dimension",
	          ".a = [1, 2.5, 3];"),
	DOM_VALID("HOM-002", "homogeneity", "sparse array with null holes",
	          ".a = [1, , 3];"),
	DOM_VALID("HOM-003", "homogeneity", "rectangular nested sub-arrays",
	          ".a = [[1, 2], [3, 4]];"),
	DOM_VALID("HOM-004", "homogeneity", "record array — same keys, fields free (per-currency)",
	          ".a = [{.cur = USD; .bal = <float_dec:64,$USD> 1.0;},"
	          " {.cur = EUR; .bal = <float_dec:64,$EUR> 2.0;}];"),
	DOM_ERROR("HOM-005", "homogeneity", "mixed element kinds (number vs string)",
	          ".a = [1, \"two\"];",
	          error_array_element_type_mismatch),
	DOM_ERROR("HOM-006", "homogeneity", "mixed physical dimension (length vs mass)",
	          ".a = [<float:64,m> 1.0, <float:64,k~g> 2.0];",
	          error_array_element_type_mismatch),
	DOM_ERROR("HOM-007", "homogeneity", "mixed currency dimension (USD vs EUR)",
	          ".a = [<float_dec:64,$USD> 1.0, <float_dec:64,$EUR> 2.0];",
	          error_array_element_type_mismatch),
	DOM_ERROR("HOM-008", "homogeneity", "ragged sibling sub-arrays",
	          ".a = [[1, 2], [3, 4, 5]];",
	          error_array_row_size_mismatch),
	DOM_ERROR("HOM-009", "homogeneity", "sibling structs with differing keys",
	          ".a = [{.x = 1;}, {.y = 1;}];",
	          error_struct_shape_mismatch),
	DOM_ERROR("HOM-010", "homogeneity", "record field differs in kind across siblings",
	          ".a = [{.v = 1;}, {.v = \"two\";}];",
	          error_array_element_type_mismatch),
	DOM_ERROR("HOM-011", "homogeneity", "duplicate key at top-level scope",
	          ".x = 1; .x = 2;",
	          error_duplicate_struct_key),
	DOM_ERROR("HOM-012", "homogeneity", "duplicate key within a struct scope",
	          ".s = {.x = 1; .x = 2;};",
	          error_duplicate_struct_key),
	/* datetime (spec 1.1) is its own kind for homogeneity, and its epoch is a
	 * dimension (like a currency) — mirrors HOM-006/HOM-007. */
	DOM_VALID("HOM-013", "homogeneity", "homogeneous same-epoch datetime array",
	          "#!bovnar 1.1\n.a = [<datetime:64,tai> 1, <datetime:64,tai> 2];"),
	DOM_ERROR("HOM-014", "homogeneity", "datetime does not mix with a plain number",
	          "#!bovnar 1.1\n.a = [<datetime:64,unix> 1, <sint:64> 2];",
	          error_array_element_type_mismatch),
	DOM_ERROR("HOM-015", "homogeneity", "datetimes on different epochs do not mix",
	          "#!bovnar 1.1\n.a = [<datetime:64,unix> 1, <datetime:64,tai> 2];",
	          error_array_element_type_mismatch),
	/* Stronger than HOM-008 (ragged, differing cell counts): sibling matrices
	 * with the SAME total cell count but a DIFFERENT shape (2x3 vs 3x2) must be
	 * rejected. A flat-stored matrix once compared only its flattened cell count,
	 * so this geometry slipped through. */
	DOM_ERROR("HOM-016", "homogeneity", "sibling matrices, equal cell count, different shape",
	          ".a = [[1,2,3]/[4,5,6], [7,8]/[9,10]/[11,12]];",
	          error_array_row_size_mismatch),
};

#define NUM_CASES ((int)(sizeof(g_cases) / sizeof(g_cases[0])))

/* =========================================================================
 * Assertion helpers
 * ========================================================================= */

static bool evlog_contains(const evlog_t *l, const char *needle)
{
	if (!l->buf || !needle) return false;
	char *term = malloc(l->used + 1);
	if (!term) return false;
	memcpy(term, l->buf, l->used);
	term[l->used] = '\0';
	bool found = strstr(term, needle) != NULL;
	free(term);
	return found;
}

#ifndef _WIN32  /* only the IUT runner compares logs; unused on the Windows build */
static bool evlog_equal(const evlog_t *a, const char *b_buf, size_t b_len)
{
	return a->used == b_len && memcmp(a->buf, b_buf, b_len) == 0;
}
#endif

static bool check_key_in_log(const evlog_t *log, const char *key)
{
	if (!key) return true;
	char needle[512];
	snprintf(needle, sizeof(needle), "ASSIGNMENT_START %s\n", key);
	return evlog_contains(log, needle);
}

static void evlog_escape_str(const char *in, char *out, size_t outsz)
{
	size_t pos = 0;
	for (const uint8_t *p = (const uint8_t *)in; *p; p++) {
		uint8_t b = *p;
		if (b >= 0x20u && b <= 0x7Eu && b != '\\') {
			if (pos + 1 >= outsz) break;
			out[pos++] = (char)b;
		} else {
			if (pos + 5 >= outsz) break;
			snprintf(out + pos, 5, "\\x%02x", b);
			pos += 4;
		}
	}
	out[pos] = '\0';
}

static bool check_data_in_log(const evlog_t *log, const char *data)
{
	if (!data) return true;
	if (!log->buf || !log->used) return false;

	char esc[512];
	evlog_escape_str(data, esc, sizeof(esc));

	char *term = malloc(log->used + 1);
	if (!term) return false;
	memcpy(term, log->buf, log->used);
	term[log->used] = '\0';

	bool found = false;
	char *line = term;
	while (line && *line) {
		char *nl = strchr(line, '\n');
		if (nl) *nl = '\0';
		if (strncmp(line, "DATA ", 5) == 0) {
			char *sp = strchr(line + 5, ' ');
			if (sp && strcmp(sp + 1, esc) == 0) {
				found = true;
				break;
			}
		}
		line = nl ? nl + 1 : NULL;
	}
	free(term);
	return found;
}

/* =========================================================================
 * Self-test runner (no IUT)
 * ========================================================================= */

/* Materialised-document (DOM) tier self-test: parse through bvn_dom_parse
 * and check the resulting parse error against the case expectation.  Used
 * for the spec-1.0 homogeneity / struct-shape / duplicate-key rules, which
 * the streaming reader does not enforce. */
static void run_self_test_dom(const cf_case_t *tc)
{
	const void *input     = tc->input_bin ? (const void *)tc->input_bin
	                                       : (const void *)tc->input;
	uint32_t    input_len = tc->input_bin ? tc->input_bin_len
	                                       : (uint32_t)strlen(tc->input);

	bvn_dom_doc_t *doc = bvn_dom_parse(input, input_len);
	if (!doc) {
		tap_fail(tc->id, tc->description, "bvn_dom_parse returned NULL");
		return;
	}

	error_code_t err = bvn_dom_doc_get_parse_error(doc);
	bvn_dom_doc_destroy(doc);

	char detail[512];
	if (tc->expect == CF_VALID) {
		if (err != error_none) {
			snprintf(detail, sizeof(detail),
			         "expected no error but DOM parse got: %s",
			         bvn_error_to_string(err));
			tap_fail(tc->id, tc->description, detail);
		} else {
			tap_ok(tc->id, tc->description);
		}
	} else {
		if (err == error_none) {
			snprintf(detail, sizeof(detail),
			         "expected error %s but DOM parse succeeded",
			         bvn_error_to_string(tc->expected_error));
			tap_fail(tc->id, tc->description, detail);
		} else if (tc->expected_error != error_none &&
		           err != tc->expected_error) {
			snprintf(detail, sizeof(detail),
			         "expected error %s but got %s",
			         bvn_error_to_string(tc->expected_error),
			         bvn_error_to_string(err));
			tap_fail(tc->id, tc->description, detail);
		} else {
			tap_ok(tc->id, tc->description);
		}
	}
}

static void run_self_test(const cf_case_t *tc)
{
	if (tc->dom_validate) {
		run_self_test_dom(tc);
		return;
	}

	parse_result_t r;
	if (tc->input_bin) {
		r = ref_parse(tc->input_bin, tc->input_bin_len,
		              tc->continue_on_error,
		              tc->max_id_len, tc->max_str_len, tc->max_num_len,
		              tc->max_sym_len, tc->max_ref_len,
		              tc->max_struct_nesting, tc->max_array_nesting,
		              tc->max_array_items);
	} else {
		r = ref_parse_str_flags(
		    tc->input,
		    tc->continue_on_error,
		    tc->max_id_len, tc->max_str_len, tc->max_num_len,
		    tc->max_sym_len, tc->max_ref_len,
		    tc->max_struct_nesting, tc->max_array_nesting,
		    tc->max_array_items);
	}

	char detail[512];
	detail[0] = '\0';

	if (tc->expect == CF_VALID) {
		if (!r.opened) {
			snprintf(detail, sizeof(detail),
			         "bvnr_open_read_mem failed");
			tap_fail(tc->id, tc->description, detail);
		} else if (!r.parse_ok && r.error == error_none) {
			snprintf(detail, sizeof(detail),
			         "parse returned false with no error set");
			tap_fail(tc->id, tc->description, detail);
		} else if (r.error != error_none) {
			snprintf(detail, sizeof(detail),
			         "expected no error but got: %s",
			         bvn_error_to_string(r.error));
			tap_fail(tc->id, tc->description, detail);
		} else if (r.log.oom) {
			tap_fail(tc->id, tc->description,
			         "event log OOM during capture");
		} else {
			bool key_ok  = check_key_in_log(&r.log, tc->expect_key);
			bool data_ok = check_data_in_log(&r.log, tc->expect_data);
			if (!key_ok) {
				snprintf(detail, sizeof(detail),
				         "expected ASSIGNMENT_START %s not found in log",
				         tc->expect_key ? tc->expect_key : "?");
				tap_fail(tc->id, tc->description, detail);
			} else if (!data_ok) {
				snprintf(detail, sizeof(detail),
				         "expected DATA value not found in log");
				tap_fail(tc->id, tc->description, detail);
			} else {
				tap_ok(tc->id, tc->description);
			}
		}
	} else {
		bool got_error = !r.parse_ok || r.error != error_none;
		if (!got_error && tc->continue_on_error)
			got_error = r.error_line == 0 && r.error_column == 0 &&
			            r.recovery_count > 0;
		if (!got_error) {
			snprintf(detail, sizeof(detail),
			         "expected error %s but parse succeeded",
			         bvn_error_to_string(tc->expected_error));
			tap_fail(tc->id, tc->description, detail);
		} else if (tc->expected_error != error_none &&
		           r.error != tc->expected_error) {
			snprintf(detail, sizeof(detail),
			         "expected error %s but got %s",
			         bvn_error_to_string(tc->expected_error),
			         bvn_error_to_string(r.error));
			tap_fail(tc->id, tc->description, detail);
		} else {
			tap_ok(tc->id, tc->description);
		}
	}

	parse_result_free(&r);
}

/* =========================================================================
 * IUT test runner
 * ========================================================================= */
#ifndef _WIN32

static void run_iut_test(const cf_case_t *tc, const char *iut_path)
{
	if (tc->dom_validate) {
		tap_skip(tc->id, tc->description,
		         "DOM-tier check; not part of streaming IUT protocol v1");
		return;
	}
	if (tc->iut_skip_reason) {
		tap_skip(tc->id, tc->description, tc->iut_skip_reason);
		return;
	}

	parse_result_t ref;
	const void *input_ptr;
	size_t input_len;
	if (tc->input_bin) {
		input_ptr = tc->input_bin;
		input_len = tc->input_bin_len;
		ref = ref_parse(tc->input_bin, tc->input_bin_len,
		                tc->continue_on_error,
		                tc->max_id_len, tc->max_str_len, tc->max_num_len,
		                tc->max_sym_len, tc->max_ref_len,
		                tc->max_struct_nesting, tc->max_array_nesting,
		                tc->max_array_items);
	} else {
		input_ptr = tc->input;
		input_len = strlen(tc->input);
		ref = ref_parse_str_flags(
		    tc->input,
		    tc->continue_on_error,
		    tc->max_id_len, tc->max_str_len, tc->max_num_len,
		    tc->max_sym_len, tc->max_ref_len,
		    tc->max_struct_nesting, tc->max_array_nesting,
		    tc->max_array_items);
	}

	iut_result_t iut = iut_run(iut_path, input_ptr, input_len, tc);

	char detail[1024];
	detail[0] = '\0';

	if (!iut.ok) {
		snprintf(detail, sizeof(detail), "failed to invoke IUT binary");
		tap_fail(tc->id, tc->description, detail);
		goto cleanup;
	}

	if (tc->expect == CF_VALID) {
		if (iut.exit_code != 0) {
			snprintf(detail, sizeof(detail),
			         "IUT exited %d for valid input; IUT stdout: %.200s",
			         iut.exit_code,
			         iut.out_buf ? iut.out_buf : "(empty)");
			tap_fail(tc->id, tc->description, detail);
			goto cleanup;
		}
		if (ref.log.oom) {
			tap_skip(tc->id, tc->description, "reference log OOM");
			goto cleanup;
		}
		if (!evlog_equal(&ref.log, iut.out_buf, iut.out_len)) {
			size_t diff_at = 0;
			while (diff_at < ref.log.used &&
			       diff_at < iut.out_len &&
			       ref.log.buf[diff_at] == iut.out_buf[diff_at])
				diff_at++;
			snprintf(detail, sizeof(detail),
			         "event log mismatch at byte %zu; "
			         "ref=%zu bytes, iut=%zu bytes; "
			         "ref[%zu..]: %.80s ...; "
			         "iut[%zu..]: %.80s",
			         diff_at,
			         ref.log.used, iut.out_len,
			         diff_at,
			         (diff_at < ref.log.used)
			             ? (ref.log.buf + diff_at) : "(end)",
			         diff_at,
			         (diff_at < iut.out_len)
			             ? (iut.out_buf + diff_at) : "(end)");
			tap_fail(tc->id, tc->description, detail);
			goto cleanup;
		}
		tap_ok(tc->id, tc->description);
	} else {
		if (iut.exit_code == 0) {
			snprintf(detail, sizeof(detail),
			         "IUT exited 0 for invalid input (expected error %s)",
			         bvn_error_to_string(tc->expected_error));
			tap_fail(tc->id, tc->description, detail);
			goto cleanup;
		}
		if (tc->expected_error != error_none) {
			char expected_prefix[128];
			snprintf(expected_prefix, sizeof(expected_prefix),
			         "ERROR %s\n",
			         bvn_error_to_string(tc->expected_error));
			bool prefix_ok = iut.out_len >= strlen(expected_prefix) &&
			                 memcmp(iut.out_buf, expected_prefix,
			                        strlen(expected_prefix)) == 0;
			if (!prefix_ok) {
				snprintf(detail, sizeof(detail),
				         "IUT error line mismatch; expected: '%s', got: %.100s",
				         expected_prefix,
				         iut.out_buf ? iut.out_buf : "(empty)");
				tap_fail(tc->id, tc->description, detail);
				goto cleanup;
			}
		}
		tap_ok(tc->id, tc->description);
	}

cleanup:
	parse_result_free(&ref);
	iut_result_free(&iut);
}

#endif /* !_WIN32 (IUT test runner) */

/* =========================================================================
 * Main
 * ========================================================================= */

static void usage(const char *prog)
{
	fprintf(stderr,
	        "Usage: %s [OPTIONS]\n"
	        "\n"
	        "Bovnar Conformance Test Tool\n"
	        "\n"
	        "Without --iut, runs the conformance suite against the built-in\n"
	        "reference implementation.\n"
	        "\n"
	        "Options:\n"
	        "  --iut <binary>   Path to the IUT binary to test (see protocol\n"
	        "                   in doc/7_bovnar_conformance.md)\n"
	        "  --filter <group> Run only cases in the specified group\n"
	        "                   Groups: encoding, identifiers, strings, numbers,\n"
	        "                           types, default_synthesis, symbols,\n"
	        "                           references, null_values, structs, arrays,\n"
	        "                           octet_streams, units, special_numbers,\n"
	        "                           roundtrip, recovery, comments, whitespace\n"
	        "  --list           List all test case IDs and descriptions\n"
	        "  --verbose        Print additional diagnostic information\n"
	        "  --help           Show this help and exit\n"
	        "\n"
	        "Exit code: 0 if all tests pass, 1 if any test fails.\n",
	        prog);
}

int main(int argc, char **argv)
{
	const char *iut_path    = NULL;
	bool        list_only   = false;

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--iut") == 0 && i + 1 < argc) {
			iut_path = argv[++i];
		} else if (strcmp(argv[i], "--filter") == 0 && i + 1 < argc) {
			g_filter_group = argv[++i];
		} else if (strcmp(argv[i], "--list") == 0) {
			list_only = true;
		} else if (strcmp(argv[i], "--verbose") == 0) {
			g_verbose = true;
		} else if (strcmp(argv[i], "--help") == 0 ||
		           strcmp(argv[i], "-h") == 0) {
			usage(argv[0]);
			return 0;
		} else {
			fprintf(stderr, "Unknown option: %s\n", argv[i]);
			usage(argv[0]);
			return 1;
		}
	}

#ifdef _WIN32
	if (iut_path) {
		fprintf(stderr,
		        "--iut (external IUT mode) is not supported on Windows: it "
		        "requires POSIX fork/exec. Run without --iut for the "
		        "self-test against the built-in reference implementation.\n");
		return 2;
	}
#endif

	if (list_only) {
		printf("%-12s %-20s %s\n", "ID", "GROUP", "DESCRIPTION");
		for (int i = 0; i < NUM_CASES; i++) {
			const cf_case_t *tc = &g_cases[i];
			printf("%-12s %-20s %s\n",
			       tc->id, tc->group, tc->description);
		}
		return 0;
	}

	/* Collect the distinct groups (honouring --filter) in first-appearance
	 * order; each becomes one TAP 14 subtest. */
	const char *groups[NUM_CASES];
	int ngroups = 0;
	for (int i = 0; i < NUM_CASES; i++) {
		const cf_case_t *tc = &g_cases[i];
		if (g_filter_group && strcmp(tc->group, g_filter_group) != 0)
			continue;
		bool seen = false;
		for (int g = 0; g < ngroups; g++)
			if (strcmp(groups[g], tc->group) == 0) { seen = true; break; }
		if (!seen)
			groups[ngroups++] = tc->group;
	}

	tap_begin(ngroups);

	for (int g = 0; g < ngroups; g++) {
		tap_subtest_begin(groups[g]);
		for (int i = 0; i < NUM_CASES; i++) {
			const cf_case_t *tc = &g_cases[i];
			if (strcmp(tc->group, groups[g]) != 0)
				continue;
#ifndef _WIN32
			if (iut_path)
				run_iut_test(tc, iut_path);
			else
#endif
				run_self_test(tc);
		}
		tap_subtest_end(groups[g]);
	}

	if (g_tap_failures > 0) {
		fprintf(stderr, "\n# %d of %d conformance tests FAILED\n",
		        g_tap_failures, g_tap_total);
		return 1;
	}
	fprintf(stderr, "\n# All %d conformance tests passed\n", g_tap_total);
	return 0;
}
