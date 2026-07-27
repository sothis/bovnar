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

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "bovnar.h"

static int failures = 0;
static int tests = 0;

#define ASSERT_TRUE(cond, msg) do {                                   \
	tests++;                                                         \
	if (!(cond)) {                                                   \
		fprintf(stderr, "FAIL line %d: %s\n", __LINE__, (msg));  \
		failures++;                                                  \
	}                                                                \
} while (0)

#define ASSERT_EQ_INT(a, b, msg) do {                                 \
	tests++;                                                         \
	int64_t _a = (int64_t)(a);                                       \
	int64_t _b = (int64_t)(b);                                       \
	if (_a != _b) {                                                  \
		fprintf(stderr, "FAIL line %d: %s\n  got %lld, expected %lld\n", \
				__LINE__, (msg), (long long)_a, (long long)_b);      \
		failures++;                                                  \
	}                                                                \
} while (0)

#define ASSERT_EQ_UINT(a, b, msg) do {                                \
	tests++;                                                         \
	uint64_t _a = (uint64_t)(a);                                     \
	uint64_t _b = (uint64_t)(b);                                     \
	if (_a != _b) {                                                  \
		fprintf(stderr, "FAIL line %d: %s\n  got %llu, expected %llu\n", \
				__LINE__, (msg), (unsigned long long)_a, (unsigned long long)_b); \
		failures++;                                                  \
	}                                                                \
} while (0)

typedef struct {
	uint32_t verified_events;
	uint32_t unverified_events;
	uint32_t error_callback_count;
	bool     saw_second_assignment;
	error_code_t last_error;
} reader_ctx_t;

static bool on_unverified(void *userdata, bvnr_event_t ev, bvnr_data_t *data)
{
	reader_ctx_t *ctx = userdata;
	ctx->unverified_events++;
	(void)ev;
	(void)data;
	return true;
}

static bool on_verified(void *userdata, bvnr_event_t ev, bvnr_data_t *data)
{
	reader_ctx_t *ctx = userdata;
	ctx->verified_events++;

	if (ev == ev_assignment_start && data && data->data && data->length > 0) {
		if (data->length == 6 && memcmp(data->data, "second", 6) == 0)
			ctx->saw_second_assignment = true;
	}

	return true;
}

static void on_error(void *userdata,
					 error_code_t err,
					 uint64_t line,
					 uint64_t column,
					 uint32_t byte_val,
					 uint64_t offset)
{
	reader_ctx_t *ctx = userdata;
	ctx->error_callback_count++;
	ctx->last_error = err;
	(void)line; (void)column; (void)byte_val; (void)offset;
}

static void run_reader(const char *payload,
					   bool continue_on_error,
					   error_code_t expected_error,
					   bool expect_recovery,
					   bool expect_second_assignment)
{
	reader_ctx_t ctx = {0};
	bvnr_read_flags_t flags;
	memset(&flags, 0, sizeof(flags));
	flags.userdata = &ctx;
	flags.on_unverified = on_unverified;
	flags.on_verified = on_verified;
	flags.on_error = on_error;
	flags.continue_on_error = continue_on_error;

	bvnr_reader_t *reader = bvnr_reader_create();
	ASSERT_TRUE(reader != NULL, "bvnr_reader_create must succeed");
	if (!reader) return;

	bool opened = bvnr_open_read_mem(reader, payload,
									 (uint32_t)strlen(payload),
									 NULL, 0, &flags);
	ASSERT_TRUE(opened, "bvnr_open_read_mem must open the payload");

	bool ok = false;
	if (opened)
		ok = bvnr_read(reader);

	error_code_t err = bvnr_reader_get_error(reader);
	uint64_t recovery = bvnr_reader_get_recovery_count(reader);

	if (expected_error == error_none) {
		ASSERT_TRUE(ok, "bvnr_read must succeed when no parse error is expected");
		ASSERT_EQ_INT(err, error_none, "reader must report error_none");
	} else if (continue_on_error) {
		ASSERT_TRUE(ok, "bvnr_read must succeed in continue_on_error mode even if errors are recovered");
		ASSERT_EQ_INT(err, error_none, "reader must report error_none after recovery in continue_on_error mode");
		ASSERT_TRUE(ctx.error_callback_count > 0, "error callback must fire for recovered parse errors");
		ASSERT_EQ_INT(ctx.last_error, expected_error, "error callback must report the expected error code");
	} else {
		ASSERT_TRUE(!ok, "bvnr_read must fail when parse error is expected without resync");
		ASSERT_EQ_INT(err, expected_error, "reader must report the expected error code");
	}

	if (expect_recovery)
		ASSERT_TRUE(recovery > 0, "recovery_count must be greater than zero when resync is expected");
	else
		ASSERT_EQ_UINT(recovery, 0, "recovery_count must be zero when no resync is expected");

	if (expect_second_assignment)
		ASSERT_TRUE(ctx.saw_second_assignment, "second assignment must be reached after resync");

	bvnr_reader_destroy(reader);
}

static void test_resync_on_error(void)
{
	const char *payload = ".first = 1; .broken = <float:64,_2> 1.0; .second = 2;";
	run_reader(payload,
			   true,
			   error_illegal_value_type,
			   true,
			   true);
}

/*
 * Regression: a semantic error (value out of range) is detected only once the
 * value's terminating ';' has been consumed, so the stream is already at a
 * clean assignment boundary. Recovery must resume at the *next* assignment
 * rather than skipping forward to the following ';' (which would swallow the
 * valid ".second"). See §13.2.
 */
static void test_resync_after_value_error_keeps_next(void)
{
	const char *payload = ".first = 1; .broken = <uint:8> 999; .second = 2;";
	run_reader(payload,
			   true,
			   error_value_out_of_range,
			   true,
			   true);   /* .second must survive recovery */
}

static void test_incomplete_stream(void)
{
	const char *payload = ".a = \"value";
	run_reader(payload,
			   false,
			   error_got_incomplete_bvnr_stream,
			   false,
			   false);
}

static void test_comment_inside_array(void)
{
	const char *payload = ".arr = [1, # comment\n 2, 3];";
	run_reader(payload,
			   false,
			   error_none,
			   false,
			   false);
}

static void test_bom_error_after_comment(void)
{
	const char *payload = "# comment\n\xEF\xBB\xBF.a = 1;";
	reader_ctx_t ctx = {0};
	bvnr_read_flags_t flags;
	memset(&flags, 0, sizeof(flags));
	flags.userdata = &ctx;
	flags.on_unverified = on_unverified;
	flags.on_verified = on_verified;
	flags.on_error = on_error;
	flags.continue_on_error = false;

	bvnr_reader_t *reader = bvnr_reader_create();
	ASSERT_TRUE(reader != NULL, "bvnr_reader_create must succeed");
	if (!reader) return;

	bool opened = bvnr_open_read_mem(reader, payload,
									 (uint32_t)strlen(payload),
									 NULL, 0, &flags);
	ASSERT_TRUE(opened, "bvnr_open_read_mem must open the BOM payload");

	bool ok = false;
	if (opened)
		ok = bvnr_read(reader);

	error_code_t err = bvnr_reader_get_error(reader);
	ASSERT_TRUE(!ok, "bvnr_read must fail for invalid BOM after comment");
	ASSERT_TRUE(err != error_none, "invalid BOM after comment must produce a parse error");

	bvnr_reader_destroy(reader);
}


static void test_unterminated_array_is_rejected(void)
{
	/* A ';' or '}' ends the whole value, so every '[' opened for it must already
	 * have been closed. Accepting an unterminated one emitted no array_row_end
	 * and silently zeroed the nesting level, so the NEXT assignment's value was
	 * absorbed into the array and its key vanished: ".a = [1;  .b = 2;" validated
	 * as OK and converted to {"a":[1,2]} with no ".b" at all. Inside a struct the
	 * level was not even reset, so a top-level key afterwards migrated into the
	 * struct. */
	static const struct { const char *doc; bool ok; const char *why; } cases[] = {
		{ ".a = [1;\n.b = 2;\n", false,
		  "an unterminated array is refused, not silently absorbed" },
		{ ".s = { .a = [1; .b = 2; };\n.c = 3;\n", false,
		  "...including inside a struct, where it used to escape the struct" },
		{ ".a = [1,2;\n", false, "a partially filled row too" },
		{ ".a = [[1,2];\n", false, "and a nested one" },
		/* Everything legal must still parse — a ';' inside a struct that is
		 * inside an array is at a deeper struct level and is not the array's. */
		{ ".a = [1,2,3];\n", true, "a closed array still parses" },
		{ ".m = [1,2]/[3,4];\n", true, "a multi-row array still parses" },
		{ ".n = [[1,2],[3,4]];\n", true, "a nested array still parses" },
		{ ".e = [];\n", true, "an empty array still parses" },
		{ ".z = []/[];\n", true, "empty rows still parse" },
		{ ".r = [{.a = 1;}, {.a = 2;}];\n", true,
		  "a struct inside an array keeps its own semicolons" },
		{ ".r = [{.a = [1,2];}];\n", true, "and its own arrays" },
		{ ".s = { .x = [1]; };\n", true, "an array inside a struct still parses" },
	};
	for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
		bvnr_reader_t *r = bvnr_reader_create();
		bvnr_read_flags_t f;
		memset(&f, 0, sizeof f);
		ASSERT_TRUE(bvnr_open_read_mem(r, cases[i].doc,
					       (uint32_t)strlen(cases[i].doc),
					       NULL, 0, &f), "open");
		bool ok = bvnr_read(r);
		ASSERT_EQ_INT((int)ok, (int)cases[i].ok, cases[i].why);
		bvnr_reader_destroy(r);
	}
}


static bool abort_cb_count;
static int  abort_cb_limit, abort_cb_seen;
static bool abort_cb(void *ud, bvnr_event_t e, bvnr_data_t *d)
{
	(void)ud; (void)e; (void)d;
	abort_cb_seen++;
	return abort_cb_seen < abort_cb_limit;
}

static void test_consumer_abort_is_not_a_document_error(void)
{
	/* The API documents a callback returning false as "abort". With
	 * continue_on_error set the reader treated it as a parse error instead: it
	 * entered resync and kept calling the consumer that had just said stop, and
	 * counted each of those as a recovery. */
	const char *doc = ".a = 1;\n.b = 2;\n.c = 3;\n";
	for (int co = 0; co < 2; co++) {
		abort_cb_seen  = 0;
		abort_cb_limit = 3;
		bvnr_reader_t *r = bvnr_reader_create();
		bvnr_read_flags_t f;
		memset(&f, 0, sizeof f);
		f.continue_on_error = co != 0;
		f.on_verified = abort_cb;
		bvnr_open_read_mem(r, doc, (uint32_t)strlen(doc), NULL, 0, &f);
		bool ok = bvnr_read(r);
		ASSERT_TRUE(!ok, "an aborting consumer fails the read");
		ASSERT_EQ_INT((int)bvnr_reader_get_error(r),
			      (int)error_scanner_callback_failed,
			      "...with scanner_callback_failed");
		ASSERT_EQ_INT(abort_cb_seen, 3,
			      "the consumer is not called again after it aborts");
		ASSERT_EQ_INT((int64_t)bvnr_reader_get_recovery_count(r), 0,
			      "a consumer abort is not counted as a recovery");
		bvnr_reader_destroy(r);
	}
	(void)abort_cb_count;
}

static char resync_keys[256];
static bool resync_key_cb(void *ud, bvnr_event_t e, bvnr_data_t *d)
{
	(void)ud;
	if (e == ev_assignment_start && d && d->data && d->length) {
		size_t used = strlen(resync_keys);
		if (used + d->length + 2 < sizeof resync_keys) {
			resync_keys[used] = '.';
			memcpy(resync_keys + used + 1, d->data, d->length);
			resync_keys[used + 1 + d->length] = ' ';
			resync_keys[used + 2 + d->length] = '\0';
		}
	}
	return true;
}

static void test_resync_handles_a_quote_as_the_offending_byte(void)
{
	/* Resync picked the string sub-machine only when the error happened INSIDE a
	 * string. If the offending byte was itself a '"' — illegal after a number,
	 * say — that quote was consumed and plain resync started inside the string it
	 * opened, so the string's CLOSING quote read as an opening one and every
	 * following assignment was swallowed to the next quote or EOF. */
	static const struct { const char *doc; const char *keys; } cases[] = {
		{ ".a = 1 \"x\";\n.b = 2;\n.c = 3;\n", ".a .b .c " },
		{ ".a = 1 ?;\n.b = 2;\n.c = 3;\n",       ".a .b .c " },   /* control */
		{ ".a = 1 \"x\" \"y\";\n.b = 2;\n",     ".a .b " },
	};
	for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
		resync_keys[0] = '\0';
		bvnr_reader_t *r = bvnr_reader_create();
		bvnr_read_flags_t f;
		memset(&f, 0, sizeof f);
		f.continue_on_error = true;
		f.on_verified = resync_key_cb;
		bvnr_open_read_mem(r, cases[i].doc, (uint32_t)strlen(cases[i].doc),
				   NULL, 0, &f);
		bvnr_read(r);
		ASSERT_TRUE(strcmp(resync_keys, cases[i].keys) == 0,
			    "resync resumes at the next assignment, quote or not");
		bvnr_reader_destroy(r);
	}
}

static void test_resync_enters_the_string_machine_on_the_first_body_byte(void)
{
	/* The string sub-machine was selected for copy_string_byte but not for
	 * string_intro — the state the FIRST byte of a string body is read in, whose
	 * transition row is identical. So recovery depended on the string having one
	 * good character first: with the bad byte in front, the closing quote was read
	 * as an OPENING one and everything after it was swallowed to the next quote or
	 * EOF, failing the whole parse with incomplete_bvnr_stream even though a
	 * perfectly good assignment followed. Both a malformed UTF-8 byte and a
	 * rejected control byte reach that state, as does the first byte of a
	 * continuation string and of an array element. */
	static const struct { const char *doc; const char *keys; } cases[] = {
		/* bad UTF-8 as the first body byte — the case that used to fail */
		{ ".a = \"\xff\";\n.b = 2;\n.c = 3;\n",  ".a .b .c " },
		/* the same byte one character in — the case that always worked */
		{ ".a = \"x\xff\";\n.b = 2;\n.c = 3;\n", ".a .b .c " },
		/* a rejected control byte reaches the same state */
		{ ".a = \"\x01\";\n.b = 2;\n.c = 3;\n",  ".a .b .c " },
		/* string_intro is re-entered for a continuation string ... */
		{ ".a = \"ok\" \"\xff\";\n.b = 2;\n",    ".a .b " },
		/* ... and for an array element */
		{ ".a = [\"\xff\", \"y\"];\n.b = 2;\n",  ".a .b " },
	};
	for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
		resync_keys[0] = '\0';
		bvnr_reader_t *r = bvnr_reader_create();
		bvnr_read_flags_t f;
		memset(&f, 0, sizeof f);
		f.continue_on_error = true;
		f.on_verified = resync_key_cb;
		bvnr_open_read_mem(r, cases[i].doc, (uint64_t)strlen(cases[i].doc),
				   NULL, 0, &f);
		ASSERT_TRUE(bvnr_read(r),
			    "a bad first string byte still leaves a recoverable document");
		ASSERT_TRUE(strcmp(resync_keys, cases[i].keys) == 0,
			    "recovery keeps every assignment after a broken string");
		bvnr_reader_destroy(r);
	}
}

static void test_bom_only_at_offset_zero(void)
{
	/* Spec 3.2: the BOM is legal only at byte offset 0. The guard tested
	 * last_state == undefined, but that state self-loops on whitespace, so a BOM
	 * after a space or a newline slipped through. */
	static const struct { const char *doc; bool ok; } cases[] = {
		{ "\xef\xbb\xbf.a = 1;\n",     true  },
		{ "  \xef\xbb\xbf.a = 1;\n",   false },
		{ "\n\xef\xbb\xbf.a = 1;\n",   false },
	};
	for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
		bvnr_reader_t *r = bvnr_reader_create();
		bvnr_read_flags_t f;
		memset(&f, 0, sizeof f);
		bvnr_open_read_mem(r, cases[i].doc, (uint32_t)strlen(cases[i].doc),
				   NULL, 0, &f);
		ASSERT_EQ_INT((int)bvnr_read(r), (int)cases[i].ok,
			      "a BOM is accepted only at byte offset 0");
		bvnr_reader_destroy(r);
	}
}

static void test_version_directive_terminated_by_eof(void)
{
	/* A document that is nothing but the directive was accepted straight from
	 * the EOF handler, which skipped the only place the directive is parsed — so
	 * a malformed or unsupported version passed with no trailing newline. */
	static const struct { const char *doc; bool strict; bool ok; int err; } cases[] = {
		{ "#!bovnar bogus",   false, false, (int)error_invalid_spec_version },
		{ "#!bovnar bogus\n", false, false, (int)error_invalid_spec_version },
		{ "#!bovnar 99.9",    true,  false, (int)error_unsupported_spec_version },
		{ "#!bovnar 99.9\n",  true,  false, (int)error_unsupported_spec_version },
		{ "#!bovnar 99.9",    false, true,  (int)error_none },
		{ "#!bovnar 1.1",     true,  true,  (int)error_none },
		{ "# just a comment", false, true,  (int)error_none },
	};
	for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
		bvnr_reader_t *r = bvnr_reader_create();
		bvnr_read_flags_t f;
		memset(&f, 0, sizeof f);
		f.strict_version = cases[i].strict;
		bvnr_open_read_mem(r, cases[i].doc, (uint32_t)strlen(cases[i].doc),
				   NULL, 0, &f);
		ASSERT_EQ_INT((int)bvnr_read(r), (int)cases[i].ok,
			      "the version directive is parsed with or without a newline");
		ASSERT_EQ_INT((int)bvnr_reader_get_error(r), cases[i].err,
			      "...and reports the same error either way");
		bvnr_reader_destroy(r);
	}
}

int main(void)
{
	printf("Running bovnar_reader_test regression suite...\n");
	test_resync_on_error();
	test_resync_after_value_error_keeps_next();
	test_incomplete_stream();
	test_comment_inside_array();
	test_bom_error_after_comment();
	test_unterminated_array_is_rejected();
	test_consumer_abort_is_not_a_document_error();
	test_resync_handles_a_quote_as_the_offending_byte();
	test_resync_enters_the_string_machine_on_the_first_body_byte();
	test_bom_only_at_offset_zero();
	test_version_directive_terminated_by_eof();

	if (failures == 0) {
		printf("PASSED %d tests\n", tests);
		return 0;
	}

	fprintf(stderr, "FAILED %d of %d tests\n", failures, tests);
	return 1;
}
