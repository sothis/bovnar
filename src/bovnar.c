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
	if (!rd) {
		fprintf(stderr, "error: failed to allocate reader\n");
		if (!from_stdin) close(fd);
		return 1;
	}
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
	if (!bvnr_open_read_source(rd, &src, NULL, &flags)) {
		fprintf(stderr, "error: bvnr_open_read_source failed\n");
		free(ctx);
		bvnr_reader_destroy(rd);
		if (!from_stdin) close(fd);
		return 1;
	}
	if (enable_debug)
		bvnr_reader_set_debug_fd(rd, STDERR_FILENO, debug_pretty);
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
	if (sz < 0) { close(fd); return 1; }
	if (sz > (off_t)UINT32_MAX) {
		fprintf(stderr, "pretty-print: file exceeds 4 GiB limit\n");
		close(fd);
		return 1;
	}
	if (lseek(fd, 0, SEEK_SET) != 0) { close(fd); return 1; }
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
static char *parse_json_string(const char **p)
{
	if (**p != '"') return NULL;
	(*p)++;
	const char *start = *p;
	while (**p && **p != '"') {
		if (**p == '\\') {
			(*p)++;
			if (!**p) return NULL;
		}
		(*p)++;
	}
	if (**p != '"') return NULL;
	ptrdiff_t diff = *p - start;
	if (diff < 0) return NULL;
	size_t len = (size_t)diff;
	char *str = malloc(len + 1);
	if (!str) return NULL;
	char *out = str;
	const char *in = start;
	while (in < *p) {
		if (*in == '\\') {
			in++;
			if (in >= *p) { free(str); return NULL; }
			switch (*in) {
			case '"':  *out++ = '"';  break;
			case '\\': *out++ = '\\'; break;
			case '/':  *out++ = '/';  break;
			case 'b':  *out++ = '\b'; break;
			case 'f':  *out++ = '\f'; break;
			case 'n':  *out++ = '\n'; break;
			case 'r':  *out++ = '\r'; break;
			case 't':  *out++ = '\t'; break;
			case 'u': {
				uint32_t cp = 0;
				int k;
				for (k = 0; k < 4; k++) {
					in++;
					if (in >= *p) { free(str); return NULL; }
					unsigned char h = (unsigned char)*in;
					uint32_t d;
					if      (h >= '0' && h <= '9') d = h - '0';
					else if (h >= 'a' && h <= 'f') d = 10u + (unsigned)(h - 'a');
					else if (h >= 'A' && h <= 'F') d = 10u + (unsigned)(h - 'A');
					else { free(str); return NULL; }
					cp = (cp << 4) | d;
				}
				if (cp >= 0xD800u && cp <= 0xDBFFu) {
					in++;
					if (in >= *p || (unsigned char)*in != '\\') { free(str); return NULL; }
					in++;
					if (in >= *p || (unsigned char)*in != 'u')  { free(str); return NULL; }
					uint32_t lo = 0;
					for (k = 0; k < 4; k++) {
						in++;
						if (in >= *p) { free(str); return NULL; }
						unsigned char h = (unsigned char)*in;
						uint32_t d;
						if      (h >= '0' && h <= '9') d = h - '0';
						else if (h >= 'a' && h <= 'f') d = 10u + (unsigned)(h - 'a');
						else if (h >= 'A' && h <= 'F') d = 10u + (unsigned)(h - 'A');
						else { free(str); return NULL; }
						lo = (lo << 4) | d;
					}
					if (lo < 0xDC00u || lo > 0xDFFFu) { free(str); return NULL; }
					cp = 0x10000u + ((cp - 0xD800u) << 10) + (lo - 0xDC00u);
				} else if (cp >= 0xDC00u && cp <= 0xDFFFu) {
					free(str); return NULL;
				}
				if (cp <= 0x7Fu) {
					*out++ = (char)cp;
				} else if (cp <= 0x7FFu) {
					*out++ = (char)(0xC0u | (cp >> 6));
					*out++ = (char)(0x80u | (cp & 0x3Fu));
				} else if (cp <= 0xFFFFu) {
					*out++ = (char)(0xE0u | (cp >> 12));
					*out++ = (char)(0x80u | ((cp >> 6) & 0x3Fu));
					*out++ = (char)(0x80u | (cp & 0x3Fu));
				} else {
					*out++ = (char)(0xF0u | (cp >> 18));
					*out++ = (char)(0x80u | ((cp >> 12) & 0x3Fu));
					*out++ = (char)(0x80u | ((cp >> 6) & 0x3Fu));
					*out++ = (char)(0x80u | (cp & 0x3Fu));
				}
				break;
			}
			default:   *out++ = *in; break;
			}
			in++;
		} else {
			*out++ = *in++;
		}
	}
	*out = '\0';
	(*p)++;
	return str;
}
static JsonNode *json_parse_value(const char **p);
static bool parse_json_array(const char **p, JsonNode *node)
{
	if (**p != '[') return false;
	(*p)++;
	skip_ws(p);
	node->type = BVN_JSON_ARRAY;
	node->u.arr.count = 0;
	node->u.arr.items = NULL;
	if (**p == ']') { (*p)++; return true; }
	while (**p) {
		skip_ws(p);
		JsonNode *item = json_parse_value(p);
		if (!item) return false;
		JsonNode **tmp_arr = realloc(node->u.arr.items,
									 (node->u.arr.count + 1) * sizeof(JsonNode*));
		if (!tmp_arr) { json_free_node(item); return false; }
		node->u.arr.items = tmp_arr;
		node->u.arr.items[node->u.arr.count++] = item;
		skip_ws(p);
		if (**p == ',') { (*p)++; continue; }
		if (**p == ']') { (*p)++; return true; }
		return false;
	}
	return false;
}
static bool parse_json_object(const char **p, JsonNode *node)
{
	if (**p != '{') return false;
	(*p)++;
	skip_ws(p);
	node->type = BVN_JSON_OBJECT;
	node->u.obj.count = 0;
	node->u.obj.keys = NULL;
	node->u.obj.values = NULL;
	if (**p == '}') { (*p)++; return true; }
	while (**p) {
		skip_ws(p);
		if (**p != '"') return false;
		char *key = parse_json_string(p);
		if (!key) return false;
		skip_ws(p);
		if (**p != ':') { free(key); return false; }
		(*p)++;
		skip_ws(p);
		JsonNode *val = json_parse_value(p);
		if (!val) { free(key); return false; }
		char **tmp_keys = realloc(node->u.obj.keys,
								   (node->u.obj.count + 1) * sizeof(char*));
		if (!tmp_keys) { free(key); json_free_node(val); return false; }
		node->u.obj.keys = tmp_keys;
		JsonNode **tmp_vals = realloc(node->u.obj.values,
									  (node->u.obj.count + 1) * sizeof(JsonNode*));
		if (!tmp_vals) { free(key); json_free_node(val); return false; }
		node->u.obj.values = tmp_vals;
		node->u.obj.keys[node->u.obj.count] = key;
		node->u.obj.values[node->u.obj.count] = val;
		node->u.obj.count++;
		skip_ws(p);
		if (**p == ',') { (*p)++; continue; }
		if (**p == '}') { (*p)++; return true; }
		return false;
	}
	return false;
}
static bool parse_json_number(const char **p, JsonNode *node)
{
	char *end;
	errno = 0;
	int64_t ival = strtoll(*p, &end, 10);
	if (end != *p && errno == 0) {
		if (*end != '.' && *end != 'e' && *end != 'E') {
			node->type = BVN_JSON_INT;
			node->u.i = ival;
			*p = end;
			return true;
		}
	}
	errno = 0;
	double fval = strtod(*p, &end);
	if (end != *p && errno == 0) {
		node->type = BVN_JSON_FLOAT;
		node->u.f = fval;
		*p = end;
		return true;
	}
	return false;
}
static bool parse_json_literal(const char **p, JsonNode *node)
{
	if (strncmp(*p, "true", 4) == 0) {
		node->type = BVN_JSON_BOOL; node->u.b = true; *p += 4; return true;
	}
	if (strncmp(*p, "false", 5) == 0) {
		node->type = BVN_JSON_BOOL; node->u.b = false; *p += 5; return true;
	}
	if (strncmp(*p, "null", 4) == 0) {
		node->type = BVN_JSON_NULL; *p += 4; return true;
	}
	return false;
}
static JsonNode *json_parse_value(const char **p)
{
	skip_ws(p);
	JsonNode *node = calloc(1, sizeof(JsonNode));
	if (!node) return NULL;
	if (**p == '{') {
		if (parse_json_object(p, node)) return node;
	} else if (**p == '[') {
		if (parse_json_array(p, node)) return node;
	} else if (**p == '"') {
		char *s = parse_json_string(p);
		if (s) { node->type = BVN_JSON_STRING; node->u.s = s; return node; }
	} else if (**p == 't' || **p == 'f' || **p == 'n') {
		if (parse_json_literal(p, node)) return node;
	} else {
		if (parse_json_number(p, node)) return node;
	}
	json_free_node(node);
	return NULL;
}
static char *sanitize_key(const char *key)
{
	size_t len = strlen(key);
	char *out = malloc(len + 2);
	if (!out) return NULL;
	char *p = out;
	if (len == 0 || !((*key >= 'a' && *key <= 'z') || (*key >= 'A' && *key <= 'Z') || *key == '_')) {
		*p++ = '_';
	}
	for (size_t i = 0; i < len; i++) {
		char c = key[i];
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
			(c >= '0' && c <= '9') || c == '_' || c == '+' || c == '-') {
			*p++ = c;
		} else {
			*p++ = '_';
		}
	}
	*p = '\0';
	return out;
}
static bool write_bvn_type(bvnr_writer_t *w, value_type_spec_t vt)
{
	return bvnr_write_type_annotation(w, vt, BVN_UNIT_NONE);
}
static bool write_bvn_node(bvnr_writer_t *w, const char *key, const JsonNode *node)
{
	if (key) {
		char *clean_key = sanitize_key(key);
		if (!clean_key) return false;
		bvnr_data_t d = {0};
		d.type   = token_is_identifier;
		d.data   = clean_key;
		d.length = (uint32_t)strlen(clean_key);
		bool ok = bvnr_write_event(w, ev_assignment_start, &d);
		free(clean_key);
		if (!ok) return false;
	}
	switch (node->type) {
	case BVN_JSON_NULL: {
		bvnr_data_t d = {0};
		d.type = token_is_null_value;
		if (!bvnr_write_event(w, ev_data, &d)) return false;
		break;
	}
	case BVN_JSON_BOOL: {
		bvnr_data_t d = {0};
		d.type = token_is_symbol;
		char sym[6];
		(void)strncpy(sym, node->u.b ? "true" : "false", sizeof(sym));
		d.data   = sym;
		d.length = (uint32_t)strlen(sym);
		if (!bvnr_write_event(w, ev_data, &d)) return false;
		break;
	}
	case BVN_JSON_INT: {
		bool neg = node->u.i < 0;
		value_type_spec_t vt = neg ? BVN_TYPE_SINT(64) : BVN_TYPE_UINT(64);
		if (!write_bvn_type(w, vt)) return false;
		char buf[32];
		if (neg) snprintf(buf, sizeof(buf), "%" PRId64, node->u.i);
		else     snprintf(buf, sizeof(buf), "%" PRIu64, (uint64_t)node->u.i);
		bvnr_data_t d = {0};
		d.type       = token_is_number;
		d.value_type = vt;
		d.data       = buf;
		d.length     = (uint32_t)strlen(buf);
		if (!bvnr_write_event(w, ev_data, &d)) return false;
		break;
	}
	case BVN_JSON_FLOAT: {
		value_type_spec_t vt = BVN_TYPE_FLOAT(64);
		if (!write_bvn_type(w, vt)) return false;
		char buf[64];
		snprintf(buf, sizeof(buf), "%.17g", node->u.f);
		bvnr_data_t d = {0};
		d.type       = token_is_number;
		d.value_type = vt;
		d.data       = buf;
		d.length     = (uint32_t)strlen(buf);
		if (!bvnr_write_event(w, ev_data, &d)) return false;
		break;
	}
	case BVN_JSON_STRING: {
		bvnr_data_t d = {0};
		d.type   = token_is_string;
		d.data   = node->u.s;
		d.length = (uint32_t)strlen(node->u.s);
		if (!bvnr_write_event(w, ev_data, &d)) return false;
		break;
	}
	case BVN_JSON_ARRAY: {
		bvnr_data_t d = {0};
		if (!bvnr_write_event(w, ev_array_row_start, &d)) return false;
		for (uint32_t i = 0; i < node->u.arr.count; i++) {
			if (!write_bvn_node(w, NULL, node->u.arr.items[i])) return false;
		}
		if (!bvnr_write_event(w, ev_array_row_end, &d)) return false;
		break;
	}
	case BVN_JSON_OBJECT: {
		bvnr_data_t d = {0};
		if (!bvnr_write_event(w, ev_struct_start, &d)) return false;
		for (uint32_t i = 0; i < node->u.obj.count; i++) {
			if (!write_bvn_node(w, node->u.obj.keys[i], node->u.obj.values[i])) return false;
		}
		if (!bvnr_write_event(w, ev_struct_end, &d)) return false;
		break;
	}
	}
	return true;
}
static bool write_bvn_root(bvnr_writer_t *w, const JsonNode *root)
{
	if (root->type == BVN_JSON_OBJECT) {
		for (uint32_t i = 0; i < root->u.obj.count; i++) {
			if (!write_bvn_node(w, root->u.obj.keys[i], root->u.obj.values[i])) return false;
		}
	} else {
		if (!write_bvn_node(w, "data", root)) return false;
	}
	return true;
}
static int cmd_convert_json_to_bvnr(const char *file)
{
	int fd = open(file, O_RDONLY);
	if (fd < 0) { perror(file); return 1; }
	off_t sz = lseek(fd, 0, SEEK_END);
	if (sz < 0) { close(fd); return 1; }
	if (sz >= (off_t)UINT32_MAX) {
		fprintf(stderr, "convert: file exceeds 4 GiB limit\n");
		close(fd);
		return 1;
	}
	if (lseek(fd, 0, SEEK_SET) != 0) { close(fd); return 1; }
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
static void usage(const char *prog)
{
	fprintf(stderr,
		"Usage: %s <command> [options] <file>\n"
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
		"\n"
		"Examples:\n"
		"  %s validate config.bvnr\n"
		"  %s query .system.host config.bvnr\n"
		"  %s pretty-print data.bvnr\n"
		"  %s convert --from json --to bvnr data.json\n"
		"  %s convert --from bvnr --to json data.bvnr\n"
		"  %s events data.bvnr\n"
		"  %s events -c -d data.bvnr\n"
		"  cat data.bvnr | %s events -\n",
		prog, prog, prog, prog, prog, prog, prog, prog, prog);
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
	} else if (strcmp(cmd, "-h") == 0 || strcmp(cmd, "--help") == 0) {
		usage(argv[0]);
		return 0;
	} else {
		fprintf(stderr, "Unknown command: %s\n", cmd);
		usage(argv[0]);
		return 1;
	}
}
