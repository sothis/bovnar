#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "bovnar.h"

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
} cf_case_t;

/* =========================================================================
 * IUT invocation (fork/exec + pipes)
 * ========================================================================= */

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

/* =========================================================================
 * TAP output
 * ========================================================================= */

static int g_tap_next     = 1;
static int g_tap_failures = 0;
static int g_tap_total    = 0;
static bool g_verbose     = false;
static const char *g_filter_group = NULL;

static void tap_begin(int count)
{
	printf("TAP version 13\n1..%d\n", count);
}

static void tap_ok(const char *id, const char *desc)
{
	printf("ok %d - [%s] %s\n", g_tap_next++, id, desc);
	g_tap_total++;
	if (g_verbose)
		printf("  # PASS\n");
}

static void tap_fail(const char *id, const char *desc, const char *detail)
{
	printf("not ok %d - [%s] %s\n", g_tap_next++, id, desc);
	if (detail && *detail)
		printf("  ---\n  message: %s\n  ...\n", detail);
	g_tap_total++;
	g_tap_failures++;
}

static void tap_skip(const char *id, const char *desc, const char *reason)
{
	printf("ok %d - [%s] %s # SKIP %s\n", g_tap_next++, id, desc, reason);
	g_tap_total++;
}

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
	           error_unexpected_input_byte),
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
	           error_unexpected_input_byte),
	ERROR_CASE("STR-014", "strings", "raw control byte 0x01 in string",
	           ".s = \"\x01\";",
	           error_unexpected_input_byte),
	ERROR_LIM("STR-015", "strings", "string too long",
	          ".s = \"abcdefghij\";",
	          error_string_too_long, 0, 5, 0, 0, 0, 0),
	ERROR_CASE("STR-016", "strings", "utf8 type with number value",
	           ".s = <utf8> 42;",
	           error_type_value_mismatch),

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
	VALID_KEY("NUM-009", "numbers", "special number $nan$",
	          ".x = $nan$;", "x", "$nan$"),
	VALID_KEY("NUM-010", "numbers", "special number $infinity$",
	          ".x = $infinity$;", "x", "$infinity$"),
	VALID_KEY("NUM-011", "numbers", "special number $-infinity$",
	          ".x = $-infinity$;", "x", "$-infinity$"),
	VALID("NUM-012", "numbers", "large uint64 at boundary",
	      ".x = <uint:64> 18446744073709551615;"),
	VALID("NUM-013", "numbers", "sint64 at negative boundary",
	      ".x = <sint:64> -9223372036854775808;"),
	VALID("NUM-014", "numbers", "two numeric assignments in sequence",
	      ".a = 1; .b = 2;"),
	ERROR_LIM("NUM-015", "numbers", "number too long",
	          ".x = 12345678901234;",
	          error_number_too_long, 0, 0, 5, 0, 0, 0),

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
	      ".x = <float:64> $nan$;"),
	VALID("TYP-018", "types", "special number with sint type",
	      ".x = <sint:32> $infinity$;"),
	ERROR_CASE("TYP-019", "types", "uint:8 value 256 out of range",
	           ".x = <uint:8> 256;",
	           error_value_out_of_range),
	ERROR_CASE("TYP-020", "types", "sint:8 value 128 out of range",
	           ".x = <sint:8> 128;",
	           error_value_out_of_range),
	ERROR_CASE("TYP-021", "types", "sint:8 value -129 out of range",
	           ".x = <sint:8> -129;",
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

	/* ── DEFAULT TYPE SYNTHESIS ──────────────────────────────────── */
	VALID("DTS-001", "default_synthesis", "plain integer → uint:64",
	      ".x = 100;"),
	VALID("DTS-002", "default_synthesis", "negative integer → sint:64",
	      ".x = -1;"),
	VALID("DTS-003", "default_synthesis", "float literal → float:64",
	      ".x = 1.5;"),
	VALID("DTS-004", "default_synthesis", "$nan$ → float:64",
	      ".x = $nan$;"),
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
	VALID("UNT-012", "units", "pascal pressure unit",
	      ".p = <float:64,Pa> 101325;"),
	VALID("UNT-013", "units", "kelvin temperature",
	      ".t = <float:64,K> 273.15;"),
	VALID("UNT-014", "units", "hertz frequency",
	      ".f = <float:64,k~Hz> 2.4;"),
	VALID("UNT-015", "units", "mebi-byte IEC prefix",
	      ".mem = <uint:64,Mi~B> 512;"),
	ERROR_CASE("UNT-016", "units", "annotation unit m, inline unit s",
	           ".d = <float:64,m> 1.5 s;",
	           error_unit_mismatch),
	ERROR_CASE("UNT-017", "units", "illegal unit string",
	           ".x = <float:64,zzz_invalid_unit> 1.0;",
	           error_unit_illegal),

	/* ── SPECIAL NUMBERS ─────────────────────────────────────────── */
	VALID("SPC-001", "special_numbers", "$nan$ with float:64",
	      ".x = <float:64> $nan$;"),
	VALID("SPC-002", "special_numbers", "$infinity$ with float:32",
	      ".x = <float:32> $infinity$;"),
	VALID("SPC-003", "special_numbers", "$-infinity$ with float:64",
	      ".x = <float:64> $-infinity$;"),
	VALID("SPC-004", "special_numbers", "$nan$ with uint:8 (range check bypassed)",
	      ".x = <uint:8> $nan$;"),
	VALID("SPC-005", "special_numbers", "$infinity$ with sint:16",
	      ".x = <sint:16> $infinity$;"),

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

	/* ── WHITESPACE HANDLING ─────────────────────────────────────── */
	VALID("WS-001", "whitespace", "leading whitespace",
	      "   .x = 1;"),
	VALID("WS-002", "whitespace", "trailing whitespace",
	      ".x = 1;   "),
	VALID("WS-003", "whitespace", "newlines between assignments",
	      ".x = 1;\n\n.y = 2;"),
	VALID("WS-004", "whitespace", "tabs and spaces around operator",
	      ".x\t=\t1\t;"),
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

static bool evlog_equal(const evlog_t *a, const char *b_buf, size_t b_len)
{
	return a->used == b_len && memcmp(a->buf, b_buf, b_len) == 0;
}

static bool check_key_in_log(const evlog_t *log, const char *key)
{
	if (!key) return true;
	char needle[512];
	snprintf(needle, sizeof(needle), "ASSIGNMENT_START %s\n", key);
	return evlog_contains(log, needle);
}

/* =========================================================================
 * Self-test runner (no IUT)
 * ========================================================================= */

static void run_self_test(const cf_case_t *tc)
{
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
			if (!key_ok) {
				snprintf(detail, sizeof(detail),
				         "expected ASSIGNMENT_START %s not found in log",
				         tc->expect_key ? tc->expect_key : "?");
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

static void run_iut_test(const cf_case_t *tc, const char *iut_path)
{
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

	if (list_only) {
		printf("%-12s %-20s %s\n", "ID", "GROUP", "DESCRIPTION");
		for (int i = 0; i < NUM_CASES; i++) {
			const cf_case_t *tc = &g_cases[i];
			printf("%-12s %-20s %s\n",
			       tc->id, tc->group, tc->description);
		}
		return 0;
	}

	int active_count = 0;
	for (int i = 0; i < NUM_CASES; i++) {
		if (g_filter_group &&
		    strcmp(g_cases[i].group, g_filter_group) != 0)
			continue;
		active_count++;
	}

	tap_begin(active_count);

	for (int i = 0; i < NUM_CASES; i++) {
		const cf_case_t *tc = &g_cases[i];
		if (g_filter_group &&
		    strcmp(tc->group, g_filter_group) != 0)
			continue;

		if (iut_path)
			run_iut_test(tc, iut_path);
		else
			run_self_test(tc);
	}

	if (g_tap_failures > 0) {
		fprintf(stderr, "\n# %d of %d conformance tests FAILED\n",
		        g_tap_failures, g_tap_total);
		return 1;
	}
	fprintf(stderr, "\n# All %d conformance tests passed\n", g_tap_total);
	return 0;
}
