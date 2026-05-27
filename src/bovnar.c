#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "bovnar.h"
#include "bovnar_dom.h"
static void print_indent(uint32_t level, bool pretty)
{
	if (!pretty) return;
	for (uint32_t i = 0; i < level; i++) fputc('\t', stdout);
}
typedef struct {
	bvnr_writer_t *writer;
} pp_callback_ctx_t;
static bool on_verified_write(void *ud, bvnr_event_t ev, bvnr_data_t *d)
{
	pp_callback_ctx_t *ctx = (pp_callback_ctx_t *)ud;
	return bvnr_write_event(ctx->writer, ev, d);
}
static void print_dom_node(const bvn_dom_node_t *node, uint32_t indent)
{
	if (!node) { printf("null"); return; }
	switch (bvn_dom_node_type(node)) {
	case BVN_DOM_NULL:
		printf("null");
		break;
	case BVN_DOM_INT: {
		value_type_spec_t vt = bvn_dom_get_value_type(node);
		if (vt.width > 64u) {
			char *s = bvn_dom_int_to_str(node, 10u);
			if (s) { fputs(s, stdout); free(s); }
			else   { printf("null"); }
		} else if (vt.family == vt_uint) {
			uint64_t v = 0;
			bvn_dom_get_u64(node, &v);
			printf("%" PRIu64, v);
		} else {
			int64_t v = 0;
			bvn_dom_get_i64(node, &v);
			printf("%" PRId64, v);
		}
		break;
	}
	case BVN_DOM_FLOAT: {
		double v;
		bvn_dom_get_float(node, &v);
		printf("%g", v);
		break;
	}
	case BVN_DOM_STRING: {
		const char *s; uint32_t l;
		bvn_dom_get_string(node, &s, &l);
		putchar('"');
		for (uint32_t i = 0; i < l; i++) {
			uint8_t c = (uint8_t)s[i];
			switch (c) {
			case '\t': fputs("\\t", stdout); break;
			case '\n': fputs("\\n", stdout); break;
			case '\r': fputs("\\r", stdout); break;
			case '"':  fputs("\\\"", stdout); break;
			case '\\': fputs("\\\\", stdout); break;
			default:   putchar(c);
			}
		}
		putchar('"');
		break;
	}
	case BVN_DOM_SYMBOL:
	case BVN_DOM_REFERENCE: {
		const char *s; uint32_t l;
		if (bvn_dom_get_symbol(node, &s, &l) || bvn_dom_get_reference(node, &s, &l)) {
			fwrite(s, 1, l, stdout);
		}
		break;
	}
	case BVN_DOM_STRUCT: {
		putchar('{');
		if (bvn_dom_struct_count(node)) {
			putchar('\n');
			for (uint32_t i = 0; i < bvn_dom_struct_count(node); i++) {
				const bvn_dom_entry_t *e = bvn_dom_struct_entries(node);
				if (!e) break;
				print_indent(indent + 1, true);
				printf(".%s = ", e[i].key);
				print_dom_node(e[i].value, indent + 1);
				printf(";\n");
			}
			print_indent(indent, true);
		}
		putchar('}');
		break;
	}
	case BVN_DOM_ARRAY: {
		putchar('[');
		uint32_t cnt = bvn_dom_array_count(node);
		for (uint32_t i = 0; i < cnt; i++) {
			if (i) fputs(", ", stdout);
			bvn_dom_node_t *elem = bvn_dom_array_at(node, i);
			print_dom_node(elem, indent);
		}
		putchar(']');
		break;
	}
	case BVN_DOM_OCTET_STREAM: {
		const uint8_t *b; uint32_t l;
		bvn_dom_get_octets(node, &b, &l);
		putchar('\\');
		putchar('x');
		printf("%02x", l ? b[0] : 0);
		break;
	}
	}
}
#define EVT_LOG_CAP  8192
#define EVT_COL_WIDTH 80
static const char *evt_tok_str(token_type_t t)
{
	switch (t) {
	case token_is_identifier:   return "identifier";
	case token_is_string:       return "string";
	case token_is_number:       return "number";
	case token_is_symbol:       return "symbol";
	case token_is_reference:    return "reference";
	case token_is_array_number: return "arr_number";
	case token_is_array_string: return "arr_string";
	case token_is_type:         return "type";
	case token_is_octet_stream: return "octets";
	case token_is_null_value:   return "null";
	case token_is_structure:    return "structure";
	case token_is_unit:         return "unit";
	case token_is_type_width:   return "type_width";
	case token_is_type_base:    return "type_base";
	case token_is_type_q:       return "type_q";
	case token_is_unknown:      return "unknown";
	}
	return "?";
}
static const char *evt_event_str(bvnr_event_t e)
{
	switch (e) {
	case ev_stream_start:                     return "stream_start";
	case ev_assignment_start:                      return "assign_start";
	case ev_octet_stream_start:                    return "octet_start";
	case ev_octet_stream_end:                      return "octet_end";
	case ev_struct_start:                          return "struct_open";
	case ev_struct_end:                            return "struct_close";
	case ev_array_row_start:                       return "array_row_start";
	case ev_array_row_end:                         return "array_row_end";
	case ev_array_dim_start:                       return "array_dim_start";
	case ev_data:                                  return "data";
	case ev_type_annotation_start:                 return "type_start";
	case ev_type_annotation_end:                   return "type_end";
	case ev_type_annotation_type_family:           return "type_family";
	case ev_type_annotation_type_family_parameter: return "type_param";
	case ev_stream_end:                            return "stream_end";
	default:                                       return "?";
	}
}
static const char *evt_vtf_str(value_type_family_t f)
{
	switch (f) {
	case vt_plain:     return "plain";
	case vt_utf8:      return "utf8";
	case vt_sint:      return "sint";
	case vt_uint:      return "uint";
	case vt_float:     return "float";
	case vt_float_fix: return "float_fix";
	case vt_float_dec: return "float_dec";
	case vt_illegal:   return "ILLEGAL";
	}
	return "?";
}
static bool evt_unit_has_real_base(value_unit_t u)
{
	if (u.num_components == 0)
		return false;
	for (uint32_t i = 0;
		 i < u.num_components && i < BVNR_MAX_UNIT_COMPONENTS;
		 i++) {
		if (u.components[i].base != bu_none)
			return true;
	}
	return false;
}
typedef struct {
	uint64_t     seq;
	bvnr_event_t event;
	token_type_t type;
	char         text[96];
	char         formatted[512];
} evt_logged_tok_t;
typedef struct {
	uint64_t         unverified_count;
	uint64_t         verified_count;
	uint64_t         next_seq;
	evt_logged_tok_t log[EVT_LOG_CAP];
	uint32_t         log_used;
	bvnr_canon_observer_t *canon;
} evt_ctx_t;
static void evt_format_token(const bvnr_data_t *d, bvnr_event_t e,
					  char *buf, size_t bufsize)
{
#define SCAT(...) \
	do { \
		if ((size_t)pos < bufsize) { \
			pos += snprintf(buf + pos, bufsize - (size_t)pos, __VA_ARGS__); \
			if ((size_t)pos >= bufsize) pos = (int)bufsize - 1; \
		} \
	} while (0)
	int pos = 0;
	SCAT("%-15s %-13s", evt_event_str(e), evt_tok_str(d->type));
	bool skip_data = (e == ev_type_annotation_start  ||
					  e == ev_type_annotation_end     ||
					  e == ev_type_annotation_type_family);
	if (d->type == token_is_type_width) {
		SCAT("%" PRIu32, d->value_type.width);
	} else if (d->type == token_is_type_base) {
		SCAT("%" PRIu32, d->value_type.base);
	} else if (d->type == token_is_unit) {
		if (d->data && d->length) {
			uint32_t show = d->length > 60 ? 60 : d->length;
			SCAT("\"");
			for (uint32_t i = 0;
				 i < show && (size_t)pos < bufsize - 4u;
				 i++) {
				uint8_t c = ((const uint8_t *)d->data)[i];
				if (c >= 0x20 && c < 0x7f)
					buf[pos++] = (char)c;
				else
					SCAT("\\x%02x", c);
			}
			if (show < d->length)
				SCAT("…");
			SCAT("\"");
		}
	} else if (!skip_data && d->data && d->length &&
			   d->type != token_is_octet_stream) {
		uint32_t show = d->length > 60 ? 60 : d->length;
		SCAT("\"");
		for (uint32_t i = 0;
			 i < show && (size_t)pos < bufsize - 4u;
			 i++) {
			uint8_t c = ((const uint8_t *)d->data)[i];
			if (c >= 0x20 && c < 0x7f)
				buf[pos++] = (char)c;
			else
				SCAT("\\x%02x", c);
		}
		if (show < d->length)
			SCAT("…");
		SCAT("\"");
	} else if (d->type == token_is_octet_stream) {
		uint32_t bytes = d->length ? d->length : 65536u;
		SCAT("<%u bytes>", bytes);
	}
	if (!bvn_type_is_plain(d->value_type)  &&
		d->type != token_is_type_width      &&
		d->type != token_is_type_base       &&
		d->type != token_is_unit) {
		value_type_spec_t vt = d->value_type;
		value_unit_t      u  = d->value_unit;
		SCAT(" <%s", evt_vtf_str(vt.family));
		if (bvn_type_is_numeric(vt)) {
			uint32_t ew = bvn_effective_width(vt);
			uint32_t eb = bvn_effective_base(vt);
			SCAT(":%" PRIu32 ",_%" PRIu32, ew, eb);
			if (evt_unit_has_real_base(u)) {
				char unit_buf[128];
				bvn_unit_to_string(u, unit_buf, sizeof(unit_buf));
				SCAT(",%s", unit_buf);
			} else {
				SCAT(",no_unit");
			}
		}
		SCAT(">");
	}
	buf[pos] = '\0';
#undef SCAT
}
static bool evt_on_unverified(void *ud, bvnr_event_t e, bvnr_data_t *d);
static bool evt_on_unverified_tee(void *ud, bvnr_event_t e, bvnr_data_t *d)
{
	evt_ctx_t *ctx = (evt_ctx_t *)ud;
	if (!bvnr_canon_observer_on_event(ctx->canon, e, d)) return false;
	return evt_on_unverified(ud, e, d);
}
static bool evt_on_unverified(void *ud, bvnr_event_t e, bvnr_data_t *d)
{
	evt_ctx_t *ctx = (evt_ctx_t *)ud;
	ctx->unverified_count++;
	if (ctx->log_used < EVT_LOG_CAP) {
		evt_logged_tok_t *entry = &ctx->log[ctx->log_used++];
		entry->seq   = ctx->next_seq;
		entry->event = e;
		entry->type  = d->type;
		if (d->data && d->length                        &&
			d->type != token_is_octet_stream            &&
			e != ev_type_annotation_start               &&
			e != ev_type_annotation_end                 &&
			e != ev_type_annotation_type_family) {
			uint32_t n = d->length < sizeof(entry->text) - 1
					   ? d->length
					   : (uint32_t)sizeof(entry->text) - 1;
			memcpy(entry->text, d->data, n);
			entry->text[n] = '\0';
		} else {
			entry->text[0] = '\0';
		}
		evt_format_token(d, e, entry->formatted, sizeof(entry->formatted));
	}
	ctx->next_seq++;
	return true;
}
static bool evt_on_verified(void *ud, bvnr_event_t e, bvnr_data_t *d)
{
	evt_ctx_t *ctx = (evt_ctx_t *)ud;
	ctx->verified_count++;
	int match_idx = -1;
	for (uint32_t i = 0; i < ctx->log_used; i++) {
		if (ctx->log[i].event == e && ctx->log[i].type == d->type) {
			match_idx = (int)i;
			break;
		}
	}
	char verified_str[512];
	evt_format_token(d, e, verified_str, sizeof(verified_str));
	if (match_idx >= 0) {
		printf("  %-*s │ %s\n", EVT_COL_WIDTH,
			   ctx->log[match_idx].formatted, verified_str);
		memmove(&ctx->log[match_idx],
				&ctx->log[match_idx + 1],
				(ctx->log_used - (uint32_t)match_idx - 1u)
				* sizeof(ctx->log[0]));
		ctx->log_used--;
	} else {
		printf("  %-*s │ %s\n", EVT_COL_WIDTH, "(unmatched)", verified_str);
	}
	return true;
}
static void evt_on_error(void *ud, error_code_t err,
						 uint64_t line, uint64_t column,
						 uint32_t byte, uint64_t offset)
{
	(void)ud;
	fprintf(stderr,
			"  ⚠ RECOVERY at line %" PRIu64 " col %" PRIu64
			": %s (byte 0x%02" PRIx32 " offset %" PRIu64 ")\n",
			line, column,
			bvn_error_to_string(err),
			byte, offset);
}
static int cmd_events(int argc, char **argv)
{
	bool continue_on_error = false;
	bool enable_debug      = false;
	bool debug_pretty      = false;
	int argi = 0;
	while (argi < argc && argv[argi][0] == '-') {
		if (strcmp(argv[argi], "-c") == 0) {
			continue_on_error = true; argi++; continue;
		}
		if (strcmp(argv[argi], "-d") == 0) {
			enable_debug = true;      argi++; continue;
		}
		if (strcmp(argv[argi], "-p") == 0) {
			debug_pretty = true;      argi++; continue;
		}
		if (strcmp(argv[argi], "-") != 0) {
			fprintf(stderr, "events: unknown option: %s\n", argv[argi]);
			return 2;
		}
		break;
	}
	if (argi >= argc) {
		fprintf(stderr, "events: missing file argument\n");
		return 2;
	}
	if (debug_pretty && !enable_debug)
		fprintf(stderr, "Warning: -p has no effect without -d\n");
	const char *filename  = argv[argi];
	bool        from_stdin = (strcmp(filename, "-") == 0);
	int fd;
	if (from_stdin) {
		fd = STDIN_FILENO;
	} else {
		fd = open(filename, O_RDONLY);
		if (fd < 0) { perror(filename); return 1; }
	}
	bvnr_reader_t *rd = bvnr_reader_create();
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wanalyzer-fd-leak"
	if (!rd) {
		fprintf(stderr, "error: failed to allocate reader\n");
		if (!from_stdin) { close(fd); fd = -1; }
		return 1;
	}
#pragma GCC diagnostic pop
	bvnr_source_t src;
	bvnr_source_from_fd(&src, fd);
	evt_ctx_t *ctx = calloc(1, sizeof(*ctx));
	if (!ctx) {
		fprintf(stderr, "error: failed to allocate event context\n");
		bvnr_reader_destroy(rd);
		if (!from_stdin) close(fd);
		return 1;
	}
	bvnr_read_flags_t flags;
	memset(&flags, 0, sizeof(flags));
	flags.max_file_size      = UINT32_MAX;
	flags.max_array_nesting  = 255;
	flags.max_struct_nesting = 255;
	flags.userdata           = ctx;
	flags.on_unverified      = evt_on_unverified;
	flags.on_verified        = evt_on_verified;
	flags.continue_on_error  = continue_on_error;
	flags.on_error           = evt_on_error;
	bvnr_canon_observer_t *canon = NULL;
	if (enable_debug) {
		bvnr_sink_t dbg_sink;
		bvnr_sink_to_fd(&dbg_sink, STDERR_FILENO);
		canon = bvnr_canon_observer_create(&dbg_sink, debug_pretty);
		if (!canon) {
			fprintf(stderr, "error: failed to allocate canon observer\n");
			free(ctx);
			bvnr_reader_destroy(rd);
			if (!from_stdin) close(fd);
			return 1;
		}
		ctx->canon = canon;
		flags.on_unverified = evt_on_unverified_tee;
	}
	if (!bvnr_open_read_source(rd, &src, NULL, &flags)) {
		fprintf(stderr, "error: bvnr_open_read_source failed\n");
		bvnr_canon_observer_destroy(canon);
		free(ctx);
		bvnr_reader_destroy(rd);
		if (!from_stdin) close(fd);
		return 1;
	}
	puts("═══════════════════════════════════════════════════════════════════"
		 "════════════════════════════════════════════════════════════════");
	printf("  Parsing: %s", from_stdin ? "<stdin>" : filename);
	if (enable_debug)
		printf("  [debug %s]", debug_pretty ? "pretty" : "compact");
	putchar('\n');
	puts("═══════════════════════════════════════════════════════════════════"
		 "════════════════════════════════════════════════════════════════");
	putchar('\n');
	printf("  %-80s │ %s\n", "LEXER (unverified)", "VALIDATOR (verified)");
	puts("  ────────────────────────────────────────────────────────────────"
		 "────────────────┼────────────────────────────────────────────────"
		 "────────────────────────────────────");
	bool ok = bvnr_read(rd);
	if (canon) {
		bvnr_canon_observer_finish(canon);
		bvnr_canon_observer_destroy(canon);
		canon = NULL;
		ctx->canon = NULL;
	}
	for (uint32_t i = 0; i < ctx->log_used; i++)
		printf("  %-*s │ —\n", EVT_COL_WIDTH, ctx->log[i].formatted);
	puts("\n───────────────────────────────────────────────────────────────────"
		 "────────────────────────────────────────────────────────────────");
	puts("  Summary");
	puts("───────────────────────────────────────────────────────────────────"
		 "────────────────────────────────────────────────────────────────");
	printf("  Lexer tokens     : %" PRIu64 "\n", ctx->unverified_count);
	printf("  Validated tokens : %" PRIu64 "\n", ctx->verified_count);
	uint64_t recoveries = bvnr_reader_get_recovery_count(rd);
	if (recoveries > 0)
		printf("\n  ⚠ %" PRIu64 " error(s) recovered from via resync.\n",
			   recoveries);
	if (!ok) {
		error_code_t err = bvnr_reader_get_error(rd);
		printf("\n  ✗ PARSE ERROR\n");
		printf("  ┌──────────────────────────────────────────────────────────"
			   "──────────────────────\n");
		printf("  │ code   : %d (%s)\n", err, bvn_error_to_string(err));
		printf("  │ line   : %" PRIu64 "\n", bvnr_reader_get_error_line(rd));
		printf("  │ column : %" PRIu64 "\n", bvnr_reader_get_error_column(rd));
		printf("  │ byte   : 0x%02" PRIx32 "\n", bvnr_reader_get_error_byte(rd));
		printf("  │ offset : %" PRIu64 "\n", bvnr_reader_get_error_offset(rd));
		printf("  └──────────────────────────────────────────────────────────"
			   "──────────────────────\n");
	}
	uint64_t unmatched = ctx->unverified_count - ctx->verified_count;
	if (unmatched > 0) {
		printf("\n  ⚠ %" PRIu64 " token(s) emitted by the lexer but never "
			   "validated.\n", unmatched);
		if (ctx->log_used > 0) {
			puts("\n  ╔══════════════════════════════════════════════════════"
				 "══════════════════════════╗");
			puts("  ║       UNVALIDATED TOKENS                              "
				 "                           ║");
			puts("  ╠══════════════════════════════════════════════════════"
				 "══════════════════════════╣");
			for (uint32_t i = 0; i < ctx->log_used; i++) {
				const evt_logged_tok_t *e = &ctx->log[i];
				printf("  ║  #%-5" PRIu64 " %-15s %-13s",
					   e->seq, evt_event_str(e->event), evt_tok_str(e->type));
				if (e->text[0])
					printf(" \"%s\"", e->text);
				putchar('\n');
			}
			puts("  ╚══════════════════════════════════════════════════════"
				 "══════════════════════════╝");
		}
	} else if (ok) {
		puts("\n  ✓ All tokens validated successfully.");
	}
	bvnr_reader_destroy(rd);
	if (!from_stdin) close(fd);
	free(ctx);
	return ok ? 0 : 1;
}
static int cmd_validate(const char *filename)
{
	int fd = open(filename, O_RDONLY);
	if (fd < 0) { perror(filename); return 1; }
	bvnr_reader_t *r = bvnr_reader_create();
	if (!r) { close(fd); return 1; }
	bvnr_source_t src;
	bvnr_source_from_fd(&src, fd);
	bvnr_read_flags_t flags = {0};
	flags.max_file_size     = UINT32_MAX;
	flags.max_array_nesting = 255;
	flags.max_struct_nesting = 255;
	bool ok = bvnr_open_read_source(r, &src, NULL, &flags) && bvnr_read(r);
	error_code_t err = bvnr_reader_get_error(r);
	if (!ok) {
		fprintf(stderr, "Validation failed: %s at line %" PRIu64 ", col %" PRIu64 "\n",
				bvn_error_to_string(err),
				bvnr_reader_get_error_line(r), bvnr_reader_get_error_column(r));
	} else {
		printf("%s: OK\n", filename);
	}
	bvnr_reader_destroy(r);
	close(fd);
	return ok ? 0 : 1;
}
static int cmd_query(const char *path, const char *filename)
{
	int fd = open(filename, O_RDONLY);
	if (fd < 0) { perror(filename); return 1; }
	bvn_dom_doc_t *doc = bvn_dom_parse_fd(fd);
	close(fd);
	if (!doc) {
		fprintf(stderr, "Failed to parse %s\n", filename);
		return 1;
	}
	{
		error_code_t perr = bvn_dom_doc_get_parse_error(doc);
		if (perr != error_none) {
			fprintf(stderr, "Parse error in %s: %s\n",
				filename, bvn_error_to_string(perr));
			bvn_dom_doc_destroy(doc);
			return 1;
		}
	}
	bvn_dom_node_t *node = bvn_dom_lookup(doc, path);
	if (!node) {
		fprintf(stderr, "Path '%s' not found\n", path);
		bvn_dom_doc_destroy(doc);
		return 1;
	}
	print_dom_node(node, 0);
	putchar('\n');
	bvn_dom_doc_destroy(doc);
	return 0;
}
static int cmd_pretty(const char *filename)
{
	int fd = open(filename, O_RDONLY);
	if (fd < 0) { perror(filename); return 1; }
	off_t sz = lseek(fd, 0, SEEK_END);
	if (sz < 0) {
		fprintf(stderr, "pretty-print: %s: cannot seek — not a regular file?\n",
		        filename);
		close(fd);
		return 1;
	}
	if (sz > (off_t)UINT32_MAX) {
		fprintf(stderr, "pretty-print: file exceeds 4 GiB limit\n");
		close(fd);
		return 1;
	}
	if (lseek(fd, 0, SEEK_SET) != 0) { perror(filename); close(fd); return 1; }
	if (sz == 0) { close(fd); return 0; }
	size_t size = (size_t)sz;
	uint8_t *buf = malloc(size);
	if (!buf) { close(fd); return 1; }
	size_t total = 0;
	while (total < size) {
		ssize_t nread = read(fd, buf + total, size - total);
		if (nread < 0) {
			if (errno == EINTR) continue;
			free(buf); close(fd); return 1;
		}
		if (nread == 0) break;
		total += (size_t)nread;
	}
	if (total != size) { free(buf); close(fd); return 1; }
	close(fd);
	bvnr_writer_t *w = bvnr_writer_create();
	if (!w) { free(buf); return 1; }
	bvnr_sink_t sink;
	bvnr_sink_to_fd(&sink, STDOUT_FILENO);
	bvnr_write_flags_t wflags = {0};
	if (!bvnr_open_write_sink(w, &sink, true, &wflags)) {
		bvnr_writer_destroy(w); free(buf); return 1;
	}
	bvnr_reader_t *r = bvnr_reader_create();
	if (!r) { bvnr_writer_destroy(w); free(buf); return 1; }
	bvnr_source_t src;
	bvnr_source_from_mem(&src, buf, (uint64_t)size);
	pp_callback_ctx_t cb_ctx = { .writer = w };
	bvnr_read_flags_t rflags = {0};
	rflags.userdata      = &cb_ctx;
	rflags.on_verified   = on_verified_write;
	rflags.max_file_size     = UINT32_MAX;
	rflags.max_array_nesting = 255;
	rflags.max_struct_nesting = 255;
	if (!bvnr_open_read_source(r, &src, NULL, &rflags)) {
		bvnr_reader_destroy(r); bvnr_writer_destroy(w); free(buf); return 1;
	}
	bool ok = bvnr_read(r);
	error_code_t err = bvnr_reader_get_error(r);
	if (!ok) {
		fprintf(stderr, "Pretty-print parse error: %s at line %" PRIu64 ", col %" PRIu64 "\n",
				bvn_error_to_string(err),
				bvnr_reader_get_error_line(r), bvnr_reader_get_error_column(r));
	}
	if (!bvnr_write_finish(w)) {
		fprintf(stderr, "Writer finish failed: %s\n",
				bvn_error_to_string(bvnr_writer_get_error(w)));
		ok = false;
	}
	bvnr_reader_destroy(r);
	bvnr_writer_destroy(w);
	free(buf);
	return ok ? 0 : 1;
}
typedef enum {
	BVN_JSON_NULL, BVN_JSON_BOOL, BVN_JSON_INT, BVN_JSON_FLOAT,
	BVN_JSON_STRING, BVN_JSON_ARRAY, BVN_JSON_OBJECT
} JsonNodeType;
typedef struct JsonNode {
	JsonNodeType type;
	union {
		bool b;
		int64_t i;
		double f;
		char *s;
		struct { struct JsonNode **items; uint32_t count; } arr;
		struct { char **keys; struct JsonNode **values; uint32_t count; } obj;
	} u;
} JsonNode;
static void json_free_node(JsonNode *node)
{
	if (!node) return;
	switch (node->type) {
	case BVN_JSON_STRING:
		free(node->u.s);
		break;
	case BVN_JSON_ARRAY:
		for (uint32_t i = 0; i < node->u.arr.count; i++)
			json_free_node(node->u.arr.items[i]);
		free(node->u.arr.items);
		break;
	case BVN_JSON_OBJECT:
		for (uint32_t i = 0; i < node->u.obj.count; i++) {
			free(node->u.obj.keys[i]);
			json_free_node(node->u.obj.values[i]);
		}
		free(node->u.obj.keys);
		free(node->u.obj.values);
		break;
	default:
		break;
	}
	free(node);
}
static void skip_ws(const char **p)
{
	while (**p && isspace((unsigned char)**p)) (*p)++;
}
static int parse_hex4(const char *s)
{
	uint32_t v = 0;
	for (int i = 0; i < 4; i++) {
		uint32_t c = (uint8_t)s[i];
		uint32_t d;
		if      (c >= '0' && c <= '9') d = c - '0';
		else if (c >= 'a' && c <= 'f') d = c - 'a' + 10u;
		else if (c >= 'A' && c <= 'F') d = c - 'A' + 10u;
		else return -1;
		v = (v << 4) | d;
	}
	return (int)v;
}
static char *encode_utf8(char *out, uint32_t cp)
{
	if (cp <= 0x7fu) {
		*out++ = (char)(uint8_t)cp;
	} else if (cp <= 0x7ffu) {
		*out++ = (char)(uint8_t)(0xc0u | (cp >> 6));
		*out++ = (char)(uint8_t)(0x80u | (cp & 0x3fu));
	} else if (cp <= 0xffffu) {
		*out++ = (char)(uint8_t)(0xe0u | (cp >> 12));
		*out++ = (char)(uint8_t)(0x80u | ((cp >> 6) & 0x3fu));
		*out++ = (char)(uint8_t)(0x80u | (cp & 0x3fu));
	} else if (cp <= 0x10ffffu) {
		*out++ = (char)(uint8_t)(0xf0u | (cp >> 18));
		*out++ = (char)(uint8_t)(0x80u | ((cp >> 12) & 0x3fu));
		*out++ = (char)(uint8_t)(0x80u | ((cp >> 6)  & 0x3fu));
		*out++ = (char)(uint8_t)(0x80u | (cp & 0x3fu));
	}
	return out;
}
static char *parse_json_string(const char **p)
{
	if (**p != '"') return NULL;
	(*p)++;
	const char *start = *p;
	size_t raw_len = 0;
	while (**p && **p != '"') {
		if (**p == '\\') {
			(*p)++;
			if (!**p) return NULL;
			if (**p == 'u') {
				if (!(*p)[1] || !(*p)[2] || !(*p)[3] || !(*p)[4])
					return NULL;
				(*p) += 4;
			}
		}
		(*p)++;
		raw_len++;
	}
	if (**p != '"') return NULL;
	const char *end = *p;
	char *str = malloc(raw_len * 4u + 1u);
	if (!str) return NULL;
	char *out = str;
	const char *in = start;
	while (in < end) {
		if (*in == '\\') {
			in++;
			if (in >= end) { free(str); return NULL; }
			switch (*in) {
			case '"':  *out++ = '"';  break;
			case '\\': *out++ = '\\'; break;
			case '/':  *out++ = '/';  break;
			case 'b':  *out++ = '\b'; break;
			case 'f':  *out++ = '\f'; break;
			case 'n':  *out++ = '\n'; break;
			case 'r':  *out++ = '\r'; break;
			case 't':  *out++ = '\t'; break;
			case 'v':  *out++ = '\v'; break;
			case 'u': {
				if (end - in < 5) { free(str); return NULL; }
				int hi = parse_hex4(in + 1);
				if (hi < 0) { free(str); return NULL; }
				in += 4;
				uint32_t cp = (uint32_t)hi;
				if (cp >= 0xd800u && cp <= 0xdbffu) {
					if (end - in < 7 || in[1] != '\\' || in[2] != 'u') {
						free(str); return NULL;
					}
					int lo = parse_hex4(in + 3);
					if (lo < 0 || (uint32_t)lo < 0xdc00u ||
					    (uint32_t)lo > 0xdfffu) {
						free(str); return NULL;
					}
					cp = 0x10000u + (((cp - 0xd800u) << 10) |
					     ((uint32_t)lo - 0xdc00u));
					in += 6;
				}
				out = encode_utf8(out, cp);
				break;
			}
			default:   *out++ = *in;  break;
			}
		} else {
			*out++ = *in;
		}
		in++;
	}
	*out = '\0';
	(*p)++;
	return str;
}
#define JSON_MAX_DEPTH 512
static JsonNode *json_parse_value(const char **p);
static JsonNode *json_parse_value_depth(const char **p, int depth);
static JsonNode *json_parse_array(const char **p, int depth)
{
	(*p)++;
	JsonNode *node = calloc(1, sizeof(*node));
	if (!node) return NULL;
	node->type = BVN_JSON_ARRAY;
	skip_ws(p);
	if (**p == ']') { (*p)++; return node; }
	uint32_t cap = 8;
	node->u.arr.items = malloc(cap * sizeof(*node->u.arr.items));
	if (!node->u.arr.items) { free(node); return NULL; }
	while (**p) {
		JsonNode *elem = json_parse_value_depth(p, depth);
		if (!elem) { json_free_node(node); return NULL; }
		if (node->u.arr.count == cap) {
			cap *= 2;
			JsonNode **tmp = realloc(node->u.arr.items,
									 cap * sizeof(*node->u.arr.items));
			if (!tmp) { json_free_node(elem); json_free_node(node); return NULL; }
			node->u.arr.items = tmp;
		}
		node->u.arr.items[node->u.arr.count++] = elem;
		skip_ws(p);
		if (**p == ',') { (*p)++; skip_ws(p); continue; }
		if (**p == ']') { (*p)++; break; }
		json_free_node(node); return NULL;
	}
	return node;
}
static JsonNode *json_parse_object(const char **p, int depth)
{
	(*p)++;
	JsonNode *node = calloc(1, sizeof(*node));
	if (!node) return NULL;
	node->type = BVN_JSON_OBJECT;
	skip_ws(p);
	if (**p == '}') { (*p)++; return node; }
	uint32_t cap = 8;
	node->u.obj.keys   = malloc(cap * sizeof(*node->u.obj.keys));
	node->u.obj.values = malloc(cap * sizeof(*node->u.obj.values));
	if (!node->u.obj.keys || !node->u.obj.values) { json_free_node(node); return NULL; }
	while (**p) {
		char *key = parse_json_string(p);
		if (!key) { json_free_node(node); return NULL; }
		skip_ws(p);
		if (**p != ':') { free(key); json_free_node(node); return NULL; }
		(*p)++;
		skip_ws(p);
		JsonNode *val = json_parse_value_depth(p, depth);
		if (!val) { free(key); json_free_node(node); return NULL; }
		if (node->u.obj.count == cap) {
			uint32_t nc = cap * 2;
			char     **tk = malloc(nc * sizeof(*node->u.obj.keys));
			JsonNode **tv = malloc(nc * sizeof(*node->u.obj.values));
			if (!tk || !tv) {
				free(tk); free(tv);
				free(key); json_free_node(val); json_free_node(node); return NULL;
			}
			memcpy(tk, node->u.obj.keys,   cap * sizeof(*node->u.obj.keys));
			memcpy(tv, node->u.obj.values, cap * sizeof(*node->u.obj.values));
			free(node->u.obj.keys);
			free(node->u.obj.values);
			node->u.obj.keys   = tk;
			node->u.obj.values = tv;
			cap = nc;
		}
		node->u.obj.keys[node->u.obj.count]   = key;
		node->u.obj.values[node->u.obj.count] = val;
		node->u.obj.count++;
		skip_ws(p);
		if (**p == ',') { (*p)++; skip_ws(p); continue; }
		if (**p == '}') { (*p)++; break; }
		json_free_node(node); return NULL;
	}
	return node;
}
static JsonNode *json_parse_value_depth(const char **p, int depth)
{
	skip_ws(p);
	if (!**p) return NULL;
	if (depth > JSON_MAX_DEPTH) {
		fprintf(stderr, "convert: JSON nesting exceeds limit (%d)\n",
			JSON_MAX_DEPTH);
		return NULL;
	}
	if (**p == '"') {
		char *s = parse_json_string(p);
		if (!s) return NULL;
		JsonNode *n = calloc(1, sizeof(*n));
		if (!n) { free(s); return NULL; }
		n->type = BVN_JSON_STRING;
		n->u.s = s;
		return n;
	}
	if (**p == '[') return json_parse_array(p, depth + 1);
	if (**p == '{') return json_parse_object(p, depth + 1);
	if (strncmp(*p, "null",  4) == 0) {
		*p += 4;
		JsonNode *n = calloc(1, sizeof(*n));
		if (!n) return NULL;
		n->type = BVN_JSON_NULL;
		return n;
	}
	if (strncmp(*p, "true",  4) == 0) {
		*p += 4;
		JsonNode *n = calloc(1, sizeof(*n));
		if (!n) return NULL;
		n->type = BVN_JSON_BOOL; n->u.b = true;
		return n;
	}
	if (strncmp(*p, "false", 5) == 0) {
		*p += 5;
		JsonNode *n = calloc(1, sizeof(*n));
		if (!n) return NULL;
		n->type = BVN_JSON_BOOL; n->u.b = false;
		return n;
	}
	if (**p == '-' || isdigit((unsigned char)**p)) {
		char *end;
		bool is_float = false;
		const char *scan = *p;
		if (*scan == '-') scan++;
		while (isdigit((unsigned char)*scan)) scan++;
		if (*scan == '.' || *scan == 'e' || *scan == 'E') is_float = true;
		JsonNode *n = calloc(1, sizeof(*n));
		if (!n) return NULL;
		if (is_float) {
			n->type = BVN_JSON_FLOAT;
			n->u.f = strtod(*p, &end);
		} else {
			n->type = BVN_JSON_INT;
			n->u.i = (int64_t)strtoll(*p, &end, 10);
		}
		*p = end;
		return n;
	}
	return NULL;
}
static JsonNode *json_parse_value(const char **p)
{
	return json_parse_value_depth(p, 0);
}
static bool write_bvn_value(bvnr_writer_t *w, const char *key, const JsonNode *node);
static bool write_bvn_array(bvnr_writer_t *w, const char *key, const JsonNode *node)
{
	bool all_strings = (node->u.arr.count > 0);
	for (uint32_t i = 0; i < node->u.arr.count; i++) {
		const JsonNode *e = node->u.arr.items[i];
		if (!e || e->type != BVN_JSON_STRING) { all_strings = false; break; }
	}
	{
		bvnr_data_t d = {0};
		d.type   = token_is_identifier;
		d.data   = key;
		d.length = (uint32_t)strlen(key);
		if (!bvnr_write_event(w, ev_assignment_start, &d)) return false;
	}
	{
		bvnr_data_t d = {0};
		d.type = all_strings ? token_is_array_string : token_is_array_number;
		if (!bvnr_write_event(w, ev_array_row_start, &d)) return false;
	}
	for (uint32_t i = 0; i < node->u.arr.count; i++) {
		const JsonNode *e = node->u.arr.items[i];
		bvnr_data_t d = {0};
		char nbuf[64];
		int n;
		if (!e || e->type == BVN_JSON_NULL) {
			d.type = token_is_null_value;
		} else if (e->type == BVN_JSON_BOOL) {
			const char *sym = e->u.b ? "true" : "false";
			d.type   = token_is_symbol;
			d.data   = sym;
			d.length = (uint32_t)strlen(sym);
		} else if (e->type == BVN_JSON_INT) {
			if (e->u.i >= 0) {
				n = snprintf(nbuf, sizeof(nbuf), "%" PRIu64,
					     (uint64_t)e->u.i);
			} else {
				n = snprintf(nbuf, sizeof(nbuf), "%" PRId64, e->u.i);
			}
			d.type   = token_is_array_number;
			d.data   = nbuf;
			d.length = (n > 0) ? (uint32_t)n : 1u;
		} else if (e->type == BVN_JSON_FLOAT) {
			n = snprintf(nbuf, sizeof(nbuf), "%.17g", e->u.f);
			d.type   = token_is_array_number;
			d.data   = nbuf;
			d.length = (n > 0) ? (uint32_t)n : 1u;
		} else if (e->type == BVN_JSON_STRING) {
			d.type   = token_is_array_string;
			d.data   = e->u.s;
			d.length = (uint32_t)strlen(e->u.s);
		} else {
			fprintf(stderr, "warn: nested array/object inside JSON array ignored\n");
			continue;
		}
		if (!bvnr_write_event(w, ev_data, &d)) return false;
	}
	{
		bvnr_data_t d = {0};
		if (!bvnr_write_event(w, ev_array_row_end, &d)) return false;
	}
	return true;
}
static bool write_bvn_root(bvnr_writer_t *w, const JsonNode *root)
{
	if (!root || root->type != BVN_JSON_OBJECT) {
		fprintf(stderr, "JSON root must be an object\n");
		return false;
	}
	for (uint32_t i = 0; i < root->u.obj.count; i++) {
		if (!write_bvn_value(w, root->u.obj.keys[i], root->u.obj.values[i]))
			return false;
	}
	return true;
}
static bool write_bvn_value(bvnr_writer_t *w, const char *key, const JsonNode *node)
{
	if (!node) return bvnr_write_null(w, key);
	switch (node->type) {
	case BVN_JSON_NULL:   return bvnr_write_null(w, key);
	case BVN_JSON_BOOL:   return bvnr_write_bool(w, key, node->u.b);
	case BVN_JSON_INT:
		if (node->u.i >= 0)
			return bvnr_write_uint(w, key, 0, (uint64_t)node->u.i);
		return bvnr_write_sint(w, key, 0, node->u.i);
	case BVN_JSON_FLOAT:  return bvnr_write_float(w, key, 0, node->u.f);
	case BVN_JSON_STRING: return bvnr_write_string(w, key, node->u.s);
	case BVN_JSON_ARRAY:  return write_bvn_array(w, key, node);
	case BVN_JSON_OBJECT: {
		if (!bvnr_write_struct_start(w, key)) return false;
		for (uint32_t i = 0; i < node->u.obj.count; i++) {
			if (!write_bvn_value(w, node->u.obj.keys[i], node->u.obj.values[i]))
				return false;
		}
		return bvnr_write_struct_end(w);
	}
	}
	return false;
}
static int cmd_convert_json_to_bvnr(const char *file)
{
	int fd = open(file, O_RDONLY);
	if (fd < 0) { perror(file); return 1; }
	off_t sz = lseek(fd, 0, SEEK_END);
	if (sz < 0) {
		fprintf(stderr, "convert: %s: cannot seek — not a regular file?\n", file);
		close(fd); return 1;
	}
	if (lseek(fd, 0, SEEK_SET) != 0) { perror(file); close(fd); return 1; }
	if (sz > (off_t)UINT32_MAX) {
		fprintf(stderr, "convert: file exceeds 4 GiB limit\n");
		close(fd); return 1;
	}
	size_t size = (size_t)sz;
	char *buf = malloc(size + 1);
	if (!buf) { close(fd); return 1; }
	size_t total = 0;
	while (total < size) {
		ssize_t nread = read(fd, buf + total, size - total);
		if (nread < 0) {
			if (errno == EINTR) continue;
			free(buf); close(fd); return 1;
		}
		if (nread == 0) break;
		total += (size_t)nread;
	}
	if (total != size) { free(buf); close(fd); return 1; }
	close(fd);
	buf[size] = '\0';
	const char *p = buf;
	JsonNode *root = json_parse_value(&p);
	if (!root) {
		fprintf(stderr, "Failed to parse JSON\n");
		free(buf);
		return 1;
	}
	bvnr_writer_t *w = bvnr_writer_create();
	if (!w) { json_free_node(root); free(buf); return 1; }
	bvnr_sink_t sink;
	bvnr_sink_to_fd(&sink, STDOUT_FILENO);
	bvnr_write_flags_t wflags = {0};
	if (!bvnr_open_write_sink(w, &sink, true, &wflags)) {
		bvnr_writer_destroy(w);
		json_free_node(root);
		free(buf);
		return 1;
	}
	bool ok = write_bvn_root(w, root);
	if (ok) ok = bvnr_write_finish(w);
	if (!ok) {
		fprintf(stderr, "Bovnar writer error: %s\n", bvn_error_to_string(bvnr_writer_get_error(w)));
	}
	bvnr_writer_destroy(w);
	json_free_node(root);
	free(buf);
	return ok ? 0 : 1;
}
static void print_json_node(const bvn_dom_node_t *node, int indent, bool pretty);
static void print_json_indent(int level, bool pretty)
{
	if (!pretty) return;
	for (int i = 0; i < level; i++) fputs("  ", stdout);
}
static void print_json_string_escaped(const char *s, uint32_t len)
{
	putchar('"');
	for (uint32_t i = 0; i < len; i++) {
		uint8_t c = (uint8_t)s[i];
		switch (c) {
		case '"':  fputs("\\\"", stdout); break;
		case '\\': fputs("\\\\", stdout); break;
		case '\b': fputs("\\b",  stdout); break;
		case '\f': fputs("\\f",  stdout); break;
		case '\n': fputs("\\n",  stdout); break;
		case '\r': fputs("\\r",  stdout); break;
		case '\t': fputs("\\t",  stdout); break;
		default:
			if (c < 0x20)
				printf("\\u%04x", c);
			else
				putchar(c);
		}
	}
	putchar('"');
}
static void print_json_node(const bvn_dom_node_t *node, int indent, bool pretty)
{
	if (!node) { fputs("null", stdout); return; }
	switch (bvn_dom_node_type(node)) {
	case BVN_DOM_NULL:
		fputs("null", stdout);
		break;
	case BVN_DOM_INT: {
		value_type_spec_t vt = bvn_dom_get_value_type(node);
		if (vt.width > 64u) {
			char *s = bvn_dom_int_to_str(node, 10u);
			if (s) {
				putchar('"');
				fputs(s, stdout);
				putchar('"');
				free(s);
			} else {
				fputs("null", stdout);
			}
		} else if (vt.family == vt_uint) {
			uint64_t v = 0;
			bvn_dom_get_u64(node, &v);
			printf("%" PRIu64, v);
		} else {
			int64_t v = 0;
			bvn_dom_get_i64(node, &v);
			printf("%" PRId64, v);
		}
		break;
	}
	case BVN_DOM_FLOAT: {
		double v;
		bvn_dom_get_float(node, &v);
		if (isnan(v))        fputs("null", stdout);
		else if (isinf(v))   fputs(v > 0 ? "1e308" : "-1e308", stdout);
		else                 printf("%.17g", v);
		break;
	}
	case BVN_DOM_STRING: {
		const char *s; uint32_t l;
		bvn_dom_get_string(node, &s, &l);
		print_json_string_escaped(s, l);
		break;
	}
	case BVN_DOM_SYMBOL:
	case BVN_DOM_REFERENCE: {
		const char *s = NULL; uint32_t l = 0;
		bvn_dom_get_symbol(node, &s, &l);
		if (!s) bvn_dom_get_reference(node, &s, &l);
		if (s) print_json_string_escaped(s, l);
		else   fputs("null", stdout);
		break;
	}
	case BVN_DOM_OCTET_STREAM: {
		const uint8_t *b; uint32_t l;
		bvn_dom_get_octets(node, &b, &l);
		putchar('"');
		for (uint32_t i = 0; i < l; i++) printf("%02x", b[i]);
		putchar('"');
		break;
	}
	case BVN_DOM_STRUCT: {
		uint32_t cnt = bvn_dom_struct_count(node);
		const bvn_dom_entry_t *e = bvn_dom_struct_entries(node);
		putchar('{');
		if (cnt && pretty) putchar('\n');
		for (uint32_t i = 0; i < cnt; i++) {
			print_json_indent(indent + 1, pretty);
			print_json_string_escaped(e[i].key, (uint32_t)strlen(e[i].key));
			putchar(':');
			if (pretty) putchar(' ');
			print_json_node(e[i].value, indent + 1, pretty);
			if (i + 1 < cnt) putchar(',');
			if (pretty) putchar('\n');
		}
		if (cnt && pretty) print_json_indent(indent, pretty);
		putchar('}');
		break;
	}
	case BVN_DOM_ARRAY: {
		uint32_t cnt = bvn_dom_array_count(node);
		putchar('[');
		for (uint32_t i = 0; i < cnt; i++) {
			if (i) { putchar(','); if (pretty) putchar(' '); }
			print_json_node(bvn_dom_array_at(node, i), indent, pretty);
		}
		putchar(']');
		break;
	}
	}
}
static int cmd_convert_bvnr_to_json(const char *file)
{
	int fd = open(file, O_RDONLY);
	if (fd < 0) { perror(file); return 1; }
	bvn_dom_doc_t *doc = bvn_dom_parse_fd(fd);
	close(fd);
	if (!doc) {
		fprintf(stderr, "Failed to parse %s\n", file);
		return 1;
	}
	{
		error_code_t perr = bvn_dom_doc_get_parse_error(doc);
		if (perr != error_none) {
			fprintf(stderr, "Parse error in %s: %s\n",
				file, bvn_error_to_string(perr));
			bvn_dom_doc_destroy(doc);
			return 1;
		}
	}
	uint32_t cnt = bvn_dom_doc_count(doc);
	const bvn_dom_entry_t *entries = bvn_dom_doc_entries(doc);
	putchar('{');
	if (cnt) putchar('\n');
	for (uint32_t i = 0; i < cnt; i++) {
		fputs("  ", stdout);
		print_json_string_escaped(entries[i].key, (uint32_t)strlen(entries[i].key));
		fputs(": ", stdout);
		print_json_node(entries[i].value, 1, true);
		if (i + 1 < cnt) putchar(',');
		putchar('\n');
	}
	puts("}");
	bvn_dom_doc_destroy(doc);
	return 0;
}
static int cmd_convert(const char *from, const char *to, const char *file)
{
	if (strcmp(from, "json") == 0 && strcmp(to, "bvnr") == 0)
		return cmd_convert_json_to_bvnr(file);
	if (strcmp(from, "bvnr") == 0 && strcmp(to, "json") == 0)
		return cmd_convert_bvnr_to_json(file);
	fprintf(stderr,
			"Unsupported conversion: %s -> %s\n"
			"Supported: json -> bvnr,  bvnr -> json\n",
			from, to);
	return 1;
}
typedef enum {
	BMARK_PROFILE_SCALARS,
	BMARK_PROFILE_TYPED,
	BMARK_PROFILE_STRUCTS,
	BMARK_PROFILE_ARRAYS,
	BMARK_PROFILE_UNITS,
	BMARK_PROFILE_MIXED,
	BMARK_PROFILE_COUNT
} bmark_profile_t;
static const char *bmark_profile_names[BMARK_PROFILE_COUNT] = {
	"scalars", "typed", "structs", "arrays", "units", "mixed"
};
typedef struct {
	bmark_profile_t profile;
	size_t          payload_size;
	size_t          num_assignments;
	size_t          num_events;
	double          elapsed_sec;
	double          cpu_sec;
} bmark_result_t;
typedef struct {
	bool     profiles[BMARK_PROFILE_COUNT];
	size_t   sizes[64];
	uint32_t num_sizes;
	uint32_t iterations;
	uint32_t warmup;
	bool     verbose;
	bool     json;
	bool     min_overhead;
} bmark_cfg_t;
static bmark_cfg_t bmark_cfg = {
	.profiles   = {true, true, true, true, true, true},
	.sizes      = {1024, 4096, 16384, 65536},
	.num_sizes  = 4,
	.iterations = 100,
	.warmup     = 10,
	.verbose    = false,
	.json       = false,
	.min_overhead = false,
};
typedef struct timespec bmark_wall_clock_t;
static bmark_wall_clock_t bmark_timer_now(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts;
}
static double bmark_timer_sec(const bmark_wall_clock_t *start,
                              const bmark_wall_clock_t *end)
{
	return (double)(end->tv_sec - start->tv_sec)
	     + (double)(end->tv_nsec - start->tv_nsec) * 1e-9;
}
static double bmark_cpu_sec(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
	return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}
typedef struct {
	size_t      event_count;
	size_t      assign_count;
	const bool *min_mode;
} bmark_counter_ctx_t;
static bool bmark_event_counter(void *ud, bvnr_event_t ev, bvnr_data_t *d)
{
	bmark_counter_ctx_t *ctx = ud;
	ctx->event_count++;
	if (ev == ev_assignment_start)
		ctx->assign_count++;
	(void)d;
	return true;
}
static size_t bmark_gen_scalars(uint8_t *buf, size_t cap, size_t *out_assignments)
{
	static const char *templates[] = {
		".k%u=%lld;\n",
		".k%u=%lld;\n",
		".k%u=\"%s\";\n",
		".k%u=true;\n",
	};
	static const int64_t values[] = {0, 42, -17, 1000000, 255, 9999};
	size_t pos   = 0;
	size_t count = 0;
	for (size_t i = 0; ; i++) {
		int     tmpl_idx = (int)(i % 4);
		int64_t val      = values[i % 6];
		char    buf32[64];
		int     n;
		if (tmpl_idx == 2) {
			static const char *strs[] = {"hello","world","test","value","key","data"};
			n = snprintf(buf32, sizeof(buf32), ".k%u=\"%s\";\n",
			             (unsigned)i, strs[i % 6]);
		} else {
			n = snprintf(buf32, sizeof(buf32), templates[tmpl_idx],
			             (unsigned)i, (long long)val);
		}
		if (n < 0 || (size_t)n >= cap - pos - 1)
			break;
		memcpy(buf + pos, buf32, (size_t)n);
		pos   += (size_t)n;
		count++;
	}
	*out_assignments = count;
	return pos;
}
static size_t bmark_gen_typed(uint8_t *buf, size_t cap, size_t *out_assignments)
{
	static const char *tmpls[] = {
		".k%u=<uint:8>%u;\n",
		".k%u=<uint:16>%u;\n",
		".k%u=<uint:32>%u;\n",
		".k%u=<sint:16>%d;\n",
		".k%u=<float:32>%.17g;\n",
		".k%u=<float:64>%.17g;\n",
		".k%u=<utf8>\"%s\";\n",
	};
	static const unsigned  u8v[]  = {0, 1,  42, 127,       200,       255};
	static const unsigned  u16v[] = {0, 1, 100, 1000,    32767,     65535};
	static const unsigned  u32v[] = {0, 1, 999, 65536, 2147483648u, 4294967295u};
	static const int       s16v[] = {-32768, -100, -1, 0,   100, 32767};
	static const double    fltv[] = {3.14, -1.5, 2.71828, 1e10,  0.5,  -0.5};
	static const char     *strv[] = {"text","data","value","name","label","x"};
	size_t pos   = 0;
	size_t count = 0;
	for (size_t i = 0; ; i++) {
		int    tmpl = (int)(i % 7);
		size_t vi   = (i / 7) % 6;
		char   tmp[128];
		int    n;
		switch (tmpl) {
		case 0: n = snprintf(tmp, sizeof(tmp), tmpls[0], (unsigned)i, u8v[vi]);  break;
		case 1: n = snprintf(tmp, sizeof(tmp), tmpls[1], (unsigned)i, u16v[vi]); break;
		case 2: n = snprintf(tmp, sizeof(tmp), tmpls[2], (unsigned)i, u32v[vi]); break;
		case 3: n = snprintf(tmp, sizeof(tmp), tmpls[3], (unsigned)i, s16v[vi]); break;
		case 4: n = snprintf(tmp, sizeof(tmp), tmpls[4], (unsigned)i, fltv[vi]); break;
		case 5: n = snprintf(tmp, sizeof(tmp), tmpls[5], (unsigned)i, fltv[vi]); break;
		case 6: n = snprintf(tmp, sizeof(tmp), tmpls[6], (unsigned)i, strv[vi]); break;
		default: n = -1; break;
		}
		if (n < 0 || (size_t)n >= cap - pos - 1)
			break;
		memcpy(buf + pos, tmp, (size_t)n);
		pos   += (size_t)n;
		count++;
	}
	*out_assignments = count;
	return pos;
}
static size_t bmark_gen_structs(uint8_t *buf, size_t cap, size_t *out_assignments)
{
	static const size_t MAX_DEPTH = 16;
	size_t pos    = 0;
	size_t count  = 0;
	size_t serial = 0;
	bool   done   = false;
	while (!done) {
		size_t depth = 0;
		while (depth < MAX_DEPTH) {
			char tmp[64];
			int n = snprintf(tmp, sizeof(tmp),
			                 ".s%zu={.a=%zu;.b=%zu;.c=%zu;",
			                 serial, serial, serial + 1, serial + 2);
			if (n < 0) { done = true; break; }
			size_t close_needed = (depth + 1) * 3;
			if (pos + (size_t)n + close_needed > cap) { done = true; break; }
			memcpy(buf + pos, tmp, (size_t)n);
			pos    += (size_t)n;
			count  += 4;
			serial++;
			depth++;
		}
		for (size_t i = 0; i < depth && pos + 3 <= cap; i++) {
			memcpy(buf + pos, "};\n", 3);
			pos += 3;
		}
		if (depth == 0) break;
	}
	*out_assignments = count;
	return pos;
}
static size_t bmark_gen_arrays(uint8_t *buf, size_t cap, size_t *out_assignments)
{
	static const int ROW_LEN       = 5;
	static const int ROWS_PER_ASGN = 8;
	static const int int_vals[] = {42, 0, 100, 999, 7};
	static const int NVALS = (int)(sizeof(int_vals) / sizeof(int_vals[0]));
	size_t pos  = 0;
	size_t asgn = 0;
	size_t elem = 0;
	for (;;) {
		char hdr[32];
		int  hn = snprintf(hdr, sizeof(hdr), ".a%zu=[", asgn);
		if (hn < 0) break;
		size_t max_per_asgn = (size_t)hn
			+ (size_t)ROWS_PER_ASGN * ((size_t)ROW_LEN * 5u + 4u)
			+ 8u;
		if (pos + max_per_asgn > cap) break;
		memcpy(buf + pos, hdr, (size_t)hn);
		pos += (size_t)hn;
		for (int row = 0; row < ROWS_PER_ASGN; row++) {
			if (row > 0) {
				memcpy(buf + pos, "]/[", 3);
				pos += 3;
			}
			for (int col = 0; col < ROW_LEN; col++) {
				if (col > 0) buf[pos++] = ',';
				char val[16];
				int n = snprintf(val, sizeof(val), "%d",
				                 int_vals[elem % (size_t)NVALS]);
				size_t vl = (n > 0) ? (size_t)n : 1u;
				memcpy(buf + pos, val, vl);
				pos += vl;
				elem++;
			}
		}
		memcpy(buf + pos, "];\n", 3);
		pos += 3;
		asgn++;
	}
	*out_assignments = asgn;
	return pos;
}
static size_t bmark_gen_units(uint8_t *buf, size_t cap, size_t *out_assignments)
{
	static const char *unit_templates[] = {
		".k%u=<float:64,m/s>%.17g;\n",
		".k%u=<float:64,k~g\xc2\xb7m/s\xc2\xb2>%.17g;\n",
		".k%u=<float:64,k~J>%.17g;\n",
		".k%u=<uint:64,Gi~B>%llu;\n",
		".k%u=<float:64,K>%.17g;\n",
		".k%u=<uint:64,Mi~b>%llu;\n",
		".k%u=<float:64,m/s\xc2\xb2>%.17g;\n",
		".k%u=<float:64,k~Pa>%.17g;\n",
		".k%u=<float:64,k~g/m\xc2\xb3>%.17g;\n",
		".k%u=<float:64,m*s>%.17g;\n",
		".k%u=<uint:32,no_unit>%llu;\n",
		".k%u=<float:64,V/m>%.17g;\n",
	};
	static const double flt_vals[] = {
		9.81, 9.81, 5400.0, 0, 300.0, 0, 9.81, 101.325, 7800.0, 9.81, 0, 150.0
	};
	static const uint64_t uint_vals[] = {0, 8, 256, 0, 0, 0, 0, 0, 0, 0, 42, 0};
	size_t ntemplates = sizeof(unit_templates) / sizeof(unit_templates[0]);
	size_t pos   = 0;
	size_t count = 0;
	for (size_t i = 0; ; i++) {
		int  t_idx = (int)(i % ntemplates);
		char buf128[256];
		int  n;
		if (t_idx == 3 || t_idx == 5 || t_idx == 10)
			n = snprintf(buf128, sizeof(buf128), unit_templates[t_idx],
			             (unsigned)i, (unsigned long long)uint_vals[i % 12]);
		else
			n = snprintf(buf128, sizeof(buf128), unit_templates[t_idx],
			             (unsigned)i, flt_vals[i % 12]);
		if (n < 0 || (size_t)n >= cap - pos - 1)
			break;
		memcpy(buf + pos, buf128, (size_t)n);
		pos   += (size_t)n;
		count++;
	}
	*out_assignments = count;
	return pos;
}
static size_t bmark_gen_mixed(uint8_t *buf, size_t cap, size_t *out_assignments)
{
	static const char block1[] =
		".system={\n"
		".host=\"localhost\";\n"
		".port=<uint:16>8080;\n"
		".limits={\n"
		".timeout=<float:64,s>30;\n"
		".max_payload=<uint:64,Mi~B>16;\n"
		"};\n"
		"};\n"
		".sensors=[\n"
		"{.name=\"temp\";.value=<float:64,\xc2\xb0\x43>23.5;.precision=<float:32>0.1;},\n"
		"{.name=\"pressure\";.value=<float:64,k~Pa>101.3;}\n"
		"];\n";
	static const char block2[] =
		".matrix=[1,2,3,4]/[5,6,7,8]/[9,10,11,12];\n"
		".count=<uint:32>42;\n"
		".name=<utf8>\"test object\";\n"
		".ratio=0.95;\n"
		".flags=[true,false,true,false];\n";
	size_t pos = 0;
	size_t count = 0;
	size_t block1_size    = sizeof(block1) - 1;
	size_t block2_size    = sizeof(block2) - 1;
	size_t block1_assigns = 8;
	size_t block2_assigns = 5;
	while (pos < cap) {
		if (pos + block1_size < cap) {
			memcpy(buf + pos, block1, block1_size);
			pos   += block1_size;
			count += block1_assigns;
			if (pos + block2_size < cap) {
				memcpy(buf + pos, block2, block2_size);
				pos   += block2_size;
				count += block2_assigns;
			} else {
				break;
			}
		} else {
			break;
		}
	}
	*out_assignments = count;
	return pos;
}
typedef struct {
	size_t (*gen)(uint8_t *, size_t, size_t *);
	const char *name;
} bmark_builder_t;
static const bmark_builder_t bmark_builders[BMARK_PROFILE_COUNT] = {
	{ bmark_gen_scalars, "scalars" },
	{ bmark_gen_typed,   "typed"   },
	{ bmark_gen_structs, "structs" },
	{ bmark_gen_arrays,  "arrays"  },
	{ bmark_gen_units,   "units"   },
	{ bmark_gen_mixed,   "mixed"   },
};
static bmark_result_t bmark_run(bmark_profile_t profile,
                                size_t target_size,
                                uint32_t iterations)
{
	bmark_result_t result;
	memset(&result, 0, sizeof(result));
	result.profile = profile;
	uint8_t *buf = malloc(target_size + 1024 + 256);
	if (!buf) {
		fprintf(stderr, "  ERROR: malloc(%zu) failed\n", target_size + 1280);
		return result;
	}
	size_t assign_count = 0;
	size_t actual_len = bmark_builders[profile].gen(buf, target_size + 1024,
	                                                &assign_count);
	if (actual_len == 0) {
		free(buf);
		return result;
	}
	result.num_assignments = assign_count;
	bmark_counter_ctx_t count_ctx = {0, 0, &bmark_cfg.min_overhead};
	bvnr_read_flags_t count_flags;
	memset(&count_flags, 0, sizeof(count_flags));
	count_flags.userdata      = &count_ctx;
	count_flags.on_verified   = bmark_event_counter;
	count_flags.on_unverified = bmark_cfg.min_overhead ? NULL : bmark_event_counter;
	bvnr_reader_t *r_count = bvnr_reader_create();
	if (!r_count) { free(buf); return result; }
	bool ok = bvnr_open_read_mem(r_count, buf, (uint64_t)actual_len,
	                             NULL, 0, &count_flags)
	       && bvnr_read(r_count);
	if (ok)
		result.num_events = count_ctx.event_count;
	bvnr_reader_destroy(r_count);
	if (!ok) {
		fprintf(stderr, "  ERROR: count-parse failed for profile=%s size=%zu\n",
		        bmark_profile_names[profile], target_size);
		result.payload_size = actual_len;
		free(buf);
		return result;
	}
	for (uint32_t w = 0; w < bmark_cfg.warmup; w++) {
		bmark_counter_ctx_t warm_ctx = {0, 0, &bmark_cfg.min_overhead};
		bvnr_read_flags_t warm_flags;
		memset(&warm_flags, 0, sizeof(warm_flags));
		warm_flags.userdata      = &warm_ctx;
		warm_flags.on_verified   = bmark_event_counter;
		warm_flags.on_unverified = bmark_cfg.min_overhead ? NULL : bmark_event_counter;
		bvnr_reader_t *r_warm = bvnr_reader_create();
		if (r_warm) {
			bvnr_open_read_mem(r_warm, buf, (uint64_t)actual_len,
			                   NULL, 0, &warm_flags);
			bvnr_read(r_warm);
			bvnr_reader_destroy(r_warm);
		}
	}
	double            cpu_start  = bmark_cpu_sec();
	bmark_wall_clock_t wall_start = bmark_timer_now();
	for (uint32_t i = 0; i < iterations; i++) {
		bmark_counter_ctx_t run_ctx = {0, 0, &bmark_cfg.min_overhead};
		bvnr_read_flags_t run_flags;
		memset(&run_flags, 0, sizeof(run_flags));
		run_flags.userdata      = &run_ctx;
		run_flags.on_verified   = bmark_event_counter;
		run_flags.on_unverified = bmark_cfg.min_overhead ? NULL : bmark_event_counter;
		bvnr_reader_t *r_run = bvnr_reader_create();
		if (!r_run) { free(buf); return result; }
		if (!bvnr_open_read_mem(r_run, buf, (uint64_t)actual_len,
		                        NULL, 0, &run_flags)) {
			bvnr_reader_destroy(r_run);
			free(buf);
			return result;
		}
		bvnr_read(r_run);
		bvnr_reader_destroy(r_run);
	}
	bmark_wall_clock_t wall_end = bmark_timer_now();
	double             cpu_end  = bmark_cpu_sec();
	result.elapsed_sec  = bmark_timer_sec(&wall_start, &wall_end);
	result.cpu_sec      = cpu_end - cpu_start;
	result.payload_size = actual_len;
	free(buf);
	return result;
}
static void bmark_print_header(void)
{
	if (bmark_cfg.json) return;
	printf("%-10s %8s %8s %12s %12s %12s %10s %10s\n",
	       "Profile", "Bytes", "Assigns", "Wall (ms)", "CPU (ms)",
	       "MB/s", "Ass/s", "Ev/s");
	printf("────────── ──────── ──────── ──────────── "
	       "──────────── ──────────── ────────── ──────────\n");
}
static void bmark_print_result(const bmark_result_t *r, uint32_t iterations)
{
	double wall_ms          = r->elapsed_sec * 1000.0;
	double cpu_ms           = r->cpu_sec * 1000.0;
	double wall_per_iter_ms = wall_ms / (double)iterations;
	double mb_per_sec       = (double)r->payload_size * (double)iterations
	                        / (1024.0 * 1024.0) / r->elapsed_sec;
	double assign_per_sec   = (double)r->num_assignments * (double)iterations
	                        / r->elapsed_sec;
	double events_per_sec   = (double)r->num_events * (double)iterations
	                        / r->elapsed_sec;
	if (bmark_cfg.json) {
		printf("{\"profile\":\"%s\",\"bytes\":%zu,\"assignments\":%zu,"
		       "\"events\":%zu,\"iterations\":%u,\"wall_ms\":%.3f,"
		       "\"cpu_ms\":%.3f,\"mb_per_sec\":%.3f,\"ass_per_sec\":%.0f,"
		       "\"ev_per_sec\":%.0f,\"wall_per_iter_us\":%.1f}\n",
		       bmark_profile_names[r->profile],
		       r->payload_size, r->num_assignments, r->num_events,
		       iterations,
		       wall_ms, cpu_ms,
		       mb_per_sec, assign_per_sec, events_per_sec,
		       wall_per_iter_ms * 1000.0);
	} else {
		printf("%-10s %8zu %8zu %12.3f %12.3f %12.2f %10.0f %10.0f\n",
		       bmark_profile_names[r->profile],
		       r->payload_size, r->num_assignments,
		       wall_ms, cpu_ms,
		       mb_per_sec, assign_per_sec, events_per_sec);
	}
}
static void bmark_parse_profile_list(const char *list)
{
	for (int i = 0; i < BMARK_PROFILE_COUNT; i++)
		bmark_cfg.profiles[i] = false;
	if (strcmp(list, "all") == 0) {
		for (int i = 0; i < BMARK_PROFILE_COUNT; i++)
			bmark_cfg.profiles[i] = true;
		return;
	}
	char *copy = strdup(list);
	if (!copy) return;
	char *token = strtok(copy, ",");
	while (token) {
		for (int i = 0; i < BMARK_PROFILE_COUNT; i++) {
			if (strcmp(token, bmark_profile_names[i]) == 0) {
				bmark_cfg.profiles[i] = true;
				break;
			}
		}
		token = strtok(NULL, ",");
	}
	free(copy);
}
static void bmark_parse_size_list(const char *list)
{
	bmark_cfg.num_sizes = 0;
	char *copy = strdup(list);
	if (!copy) return;
	char *token = strtok(copy, ",");
	while (token && bmark_cfg.num_sizes < 64) {
		bmark_cfg.sizes[bmark_cfg.num_sizes++] =
		    (size_t)strtoul(token, NULL, 10);
		token = strtok(NULL, ",");
	}
	free(copy);
}
static int cmd_bench(int argc, char **argv)
{
	for (int i = 0; i < argc; i++) {
		if (strcmp(argv[i], "--profile") == 0 && i + 1 < argc) {
			bmark_parse_profile_list(argv[++i]);
		} else if (strcmp(argv[i], "--size") == 0 && i + 1 < argc) {
			bmark_parse_size_list(argv[++i]);
		} else if (strcmp(argv[i], "--iterations") == 0 && i + 1 < argc) {
			bmark_cfg.iterations = (uint32_t)strtoul(argv[++i], NULL, 10);
			if (bmark_cfg.iterations < 1) bmark_cfg.iterations = 1;
		} else if (strcmp(argv[i], "--warmup") == 0 && i + 1 < argc) {
			bmark_cfg.warmup = (uint32_t)strtoul(argv[++i], NULL, 10);
		} else if (strcmp(argv[i], "--verbose") == 0) {
			bmark_cfg.verbose = true;
		} else if (strcmp(argv[i], "--json") == 0) {
			bmark_cfg.json = true;
		} else if (strcmp(argv[i], "--min-overhead") == 0) {
			bmark_cfg.min_overhead = true;
		} else if (strcmp(argv[i], "-h") == 0 ||
		           strcmp(argv[i], "--help") == 0) {
			fprintf(stderr,
			    "Usage: bovnar bench [options]\n"
			    "\n"
			    "Options:\n"
			    "  --profile <list>   Comma-separated profile names:\n"
			    "                     all,scalars,typed,structs,arrays,units,mixed\n"
			    "                     (default: all)\n"
			    "  --size <list>      Comma-separated payload sizes in bytes\n"
			    "                     (default: 1024,4096,16384,65536)\n"
			    "  --iterations <N>   Parse rounds per size×profile (default: 100)\n"
			    "  --warmup <N>       Warm-up iterations (default: 10)\n"
			    "  --verbose          Print per-run details\n"
			    "  --json             Machine-readable JSON output\n"
			    "  --min-overhead     Skip on_verified callback for pure lexer throughput\n"
			    "  -h, --help         Show this help and exit\n"
			    "\n"
			    "Examples:\n"
			    "  bovnar bench --profile scalars --size 4096\n"
			    "  bovnar bench --profile all --size 1024,65536 --iterations 200 --json\n"
			    "  bovnar bench --min-overhead --profile scalars,units --size 4096\n");
			return 0;
		} else {
			fprintf(stderr, "bench: unknown option: %s\n", argv[i]);
			return 1;
		}
	}
	if (bmark_cfg.json) {
		printf("{\n\"config\":{\"iterations\":%u,\"warmup\":%u,"
		       "\"min_overhead\":%s},\n\"results\":[\n",
		       bmark_cfg.iterations, bmark_cfg.warmup,
		       bmark_cfg.min_overhead ? "true" : "false");
	} else {
		printf("═════════════════════════════════════════════════════════════\n");
		printf("  Bovnar Parsing Throughput Benchmark\n");
		printf("  Iterations: %u   Warmup: %u   Min-overhead: %s\n",
		       bmark_cfg.iterations, bmark_cfg.warmup,
		       bmark_cfg.min_overhead ? "yes" : "no");
		printf("═════════════════════════════════════════════════════════════\n");
		bmark_print_header();
	}
	bool first = true;
	for (int p = 0; p < BMARK_PROFILE_COUNT; p++) {
		if (!bmark_cfg.profiles[p]) continue;
		for (uint32_t s = 0; s < bmark_cfg.num_sizes; s++) {
			bmark_result_t r = bmark_run((bmark_profile_t)p,
			                             bmark_cfg.sizes[s],
			                             bmark_cfg.iterations);
			if (r.payload_size == 0) {
				fprintf(stderr, "  SKIP: %s/%zu (generation or parse failed)\n",
				        bmark_profile_names[p], bmark_cfg.sizes[s]);
				continue;
			}
			if (bmark_cfg.json && !first)
				printf(",\n");
			first = false;
			bmark_print_result(&r, bmark_cfg.iterations);
			if (bmark_cfg.verbose && !bmark_cfg.json) {
				printf("  ── detail: profile=%s, size=%zu bytes, "
				       "assignments=%zu, events=%zu, "
				       "avg_cost=%.3f us/assign\n",
				       bmark_profile_names[p], r.payload_size,
				       r.num_assignments, r.num_events,
				       (r.elapsed_sec * 1e6) /
				           (double)(r.num_assignments * bmark_cfg.iterations));
			}
		}
	}
	if (bmark_cfg.json)
		printf("\n]}\n");
	printf("\n");
	return 0;
}
static void usage(const char *prog)
{
	fprintf(stderr,
		"Usage: %s <command> [options] [file]\n"
		"Commands:\n"
		"  validate      Validate a .bvnr file\n"
		"  query <path>  Query a value by path (e.g. .sensor.temperature)\n"
		"  pretty-print  Pretty-print a .bvnr file\n"
		"  convert --from <fmt> --to <fmt>  Convert between formats\n"
		"                  Supported: json -> bvnr,  bvnr -> json\n"
		"  events [opts] <file|->\n"
		"                  Print lexer and validator events side by side.\n"
		"                  Pass '-' to read from stdin.\n"
		"                  Options:\n"
		"                    -c  Continue parsing on errors (resync mode)\n"
		"                    -d  Enable debug re-serialisation output to stderr\n"
		"                    -p  Pretty-print debug output (requires -d)\n"
		"  bench [opts]\n"
		"                  Run parsing throughput benchmark.\n"
		"                  Options:\n"
		"                    --profile <list>   scalars,typed,structs,arrays,units,mixed\n"
		"                    --size <list>      payload sizes in bytes\n"
		"                    --iterations <N>   parse rounds per cell (default: 100)\n"
		"                    --warmup <N>       warm-up rounds (default: 10)\n"
		"                    --verbose          per-run detail\n"
		"                    --json             machine-readable JSON output\n"
		"                    --min-overhead     skip on_verified callback\n"
		"\n"
		"Examples:\n"
		"  %s validate config.bvnr\n"
		"  %s query .system.host config.bvnr\n"
		"  %s pretty-print data.bvnr\n"
		"  %s convert --from json --to bvnr data.json\n"
		"  %s convert --from bvnr --to json data.bvnr\n"
		"  %s events data.bvnr\n"
		"  %s events -c -d data.bvnr\n"
		"  cat data.bvnr | %s events -\n"
		"  %s bench --profile scalars --size 4096\n"
		"  %s bench --profile all --size 1024,65536 --iterations 200 --json\n",
		prog,
		prog, prog, prog, prog, prog, prog, prog, prog, prog, prog);
}
int main(int argc, char **argv)
{
	if (argc < 2) { usage(argv[0]); return 1; }
	const char *cmd = argv[1];
	if (strcmp(cmd, "validate") == 0) {
		if (argc < 3) { fprintf(stderr, "Usage: %s validate <file>\n", argv[0]); return 1; }
		return cmd_validate(argv[2]);
	} else if (strcmp(cmd, "query") == 0) {
		if (argc < 4) { fprintf(stderr, "Usage: %s query <path> <file>\n", argv[0]); return 1; }
		return cmd_query(argv[2], argv[3]);
	} else if (strcmp(cmd, "pretty-print") == 0) {
		if (argc < 3) { fprintf(stderr, "Usage: %s pretty-print <file>\n", argv[0]); return 1; }
		return cmd_pretty(argv[2]);
	} else if (strcmp(cmd, "convert") == 0) {
		const char *from = NULL, *to = NULL, *file = NULL;
		for (int i = 2; i < argc; i++) {
			if (strcmp(argv[i], "--from") == 0 && i+1 < argc) from = argv[++i];
			else if (strcmp(argv[i], "--to") == 0 && i+1 < argc) to = argv[++i];
			else file = argv[i];
		}
		if (!from || !to || !file) {
			fprintf(stderr, "Usage: %s convert --from <fmt> --to <fmt> <file>\n", argv[0]);
			return 1;
		}
		return cmd_convert(from, to, file);
	} else if (strcmp(cmd, "events") == 0) {
		return cmd_events(argc - 2, argv + 2);
	} else if (strcmp(cmd, "bench") == 0) {
		return cmd_bench(argc - 2, argv + 2);
	} else if (strcmp(cmd, "-h") == 0 || strcmp(cmd, "--help") == 0) {
		usage(argv[0]);
		return 0;
	} else {
		fprintf(stderr, "Unknown command: %s\n", cmd);
		usage(argv[0]);
		return 1;
	}
}
