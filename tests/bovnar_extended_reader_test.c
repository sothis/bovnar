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
#include <stdlib.h>
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
	uint32_t event_count;
	uint32_t error_count;
	error_code_t last_error;
	bool has_errors;
} parse_ctx_t;

static bool on_unverified(void *userdata, bvnr_event_t ev, bvnr_data_t *data)
{
	parse_ctx_t *ctx = (parse_ctx_t*)userdata;
	ctx->event_count++;
	(void)ev; (void)data;
	return true;
}

static bool on_verified(void *userdata, bvnr_event_t ev, bvnr_data_t *data)
{
	(void)userdata; (void)ev; (void)data;
	return true;
}

static void on_error(void *userdata, error_code_t err,
					 uint64_t line, uint64_t column,
					 uint32_t byte_val, uint64_t offset)
{
	parse_ctx_t *ctx = (parse_ctx_t*)userdata;
	ctx->error_count++;
	ctx->last_error = err;
	ctx->has_errors = true;
	(void)line; (void)column; (void)byte_val; (void)offset;
}

static void parse_payload(const char *payload, bool continue_on_error,
						  parse_ctx_t *out_ctx)
{
	parse_ctx_t ctx = {0};
	*out_ctx = ctx;
	bvnr_read_flags_t flags;
	memset(&flags, 0, sizeof(flags));
	flags.userdata = &ctx;
	flags.on_unverified = on_unverified;
	flags.on_verified = on_verified;
	flags.on_error = on_error;
	flags.continue_on_error = continue_on_error;

	bvnr_reader_t *reader = bvnr_reader_create();
	if (!reader) {
		fprintf(stderr, "Failed to create reader\n");
		return;
	}

	bool opened = bvnr_open_read_mem(reader, payload,
									 (uint32_t)strlen(payload),
									 NULL, 0, &flags);
	if (opened)
		bvnr_read(reader);

	bvnr_reader_destroy(reader);
	*out_ctx = ctx;
}

static void test_parse_various_integer_bases(void)
{
	printf("  test_parse_various_integer_bases...\n");

	parse_ctx_t ctx;

	parse_payload(".a = <uint:_2> \"1010\";", false, &ctx);
	ASSERT_TRUE(!ctx.has_errors, "binary integer <uint:_2> \"1010\" must parse");

	parse_payload(".a = <uint:_8> \"755\";", false, &ctx);
	ASSERT_TRUE(!ctx.has_errors, "octal integer <uint:_8> \"755\" must parse");

	parse_payload(".a = <uint:_16> \"FF00\";", false, &ctx);
	ASSERT_TRUE(!ctx.has_errors, "hex integer <uint:_16> \"FF00\" must parse");

	parse_payload(".a = 12345;", false, &ctx);
	ASSERT_TRUE(!ctx.has_errors, "decimal 12345 must parse");

	parse_payload(".a = <sint:8,_16> \"-7F\";", false, &ctx);
	ASSERT_TRUE(!ctx.has_errors, "signed hex <sint:8,_16> \"-7F\" must parse");

	parse_payload(".a = <uint:8> 256;", false, &ctx);
	ASSERT_TRUE(ctx.has_errors, "uint:8 value 256 must be rejected");
	ASSERT_EQ_INT(ctx.last_error, error_value_out_of_range,
		"expected error_value_out_of_range for uint:8 = 256");

	parse_payload(".a = [<uint:8,_16> \"FF\", <uint:8,_16> \"AA\"];",
				  false, &ctx);
	ASSERT_TRUE(!ctx.has_errors,
		"hex integers in array via per-element annotation must parse");

	parse_payload(".a = <uint:8,_2> [\"11111111\", \"00000000\"];",
				  false, &ctx);
	ASSERT_TRUE(!ctx.has_errors,
		"binary integers in array via whole-array annotation must parse");

	parse_payload(".a = [<sint:8,_16> \"-7F\", <sint:8,_16> \"1\"];",
				  false, &ctx);
	ASSERT_TRUE(!ctx.has_errors,
		"signed hex integers in array must parse");

	parse_payload(".a = [<uint:8,_16> \"FF\", <uint:8,_16> \"100\"];",
				  false, &ctx);
	ASSERT_TRUE(ctx.has_errors,
		"hex overflow inside array must be rejected");
	ASSERT_EQ_INT(ctx.last_error, error_value_out_of_range,
		"expected error_value_out_of_range for hex \"100\" > uint:8");
}

static void test_parse_string_escapes(void)
{
	printf("  test_parse_string_escapes...\n");

	parse_ctx_t ctx;

	parse_payload(".str = \"hello\\\"world\";", false, &ctx);
	ASSERT_TRUE(!ctx.has_errors, "string with escaped quotes must parse");

	parse_payload(".path = \"C:\\\\Users\\\\test\";", false, &ctx);
	ASSERT_TRUE(!ctx.has_errors, "string with escaped backslashes must parse");

	parse_payload(".text = \"line1\\nline2\";", false, &ctx);
	ASSERT_TRUE(!ctx.has_errors, "string with newline escape must parse");

	parse_payload(".data = \"col1\\tcol2\";", false, &ctx);
	ASSERT_TRUE(!ctx.has_errors, "string with tab escape must parse");

	parse_payload(".crlf = \"dos\\rline\";", false, &ctx);
	ASSERT_TRUE(!ctx.has_errors, "string with CR escape must parse");

	parse_payload(".vt = \"vt\\vhere\";", false, &ctx);
	ASSERT_TRUE(!ctx.has_errors, "string with vertical-tab escape must parse");

	parse_payload(".ff = \"ff\\fhere\";", false, &ctx);
	ASSERT_TRUE(!ctx.has_errors, "string with form-feed escape must parse");
}

static void test_parse_negative_numbers(void)
{
	printf("  test_parse_negative_numbers...\n");

	parse_ctx_t ctx;

	parse_payload(".temp = -273;", false, &ctx);
	ASSERT_TRUE(!ctx.has_errors, "negative integer must parse");

	parse_payload(".loss = -3.14;", false, &ctx);
	ASSERT_TRUE(!ctx.has_errors, "negative float must parse");

	parse_payload(".val = <sint:32> -42;", false, &ctx);
	ASSERT_TRUE(!ctx.has_errors, "typed negative integer must parse");
}

static void test_parse_scientific_notation(void)
{
	printf("  test_parse_scientific_notation...\n");

	parse_ctx_t ctx;

	parse_payload(".big = 1.23e10;", false, &ctx);
	ASSERT_TRUE(!ctx.has_errors, "scientific notation 1.23e10 must parse");

	parse_payload(".small = 1.5e-10;", false, &ctx);
	ASSERT_TRUE(!ctx.has_errors, "scientific notation 1.5e-10 must parse");

	parse_payload(".val = 5e3;", false, &ctx);
	ASSERT_TRUE(!ctx.has_errors, "integer scientific notation 5e3 must parse");
}

static void test_parse_arrays_2d(void)
{
	printf("  test_parse_arrays_2d...\n");

	parse_ctx_t ctx;

	parse_payload(".matrix = [1,2,3]/[4,5,6]/[7,8,9];", false, &ctx);
	ASSERT_TRUE(!ctx.has_errors, "2D array must parse");

	parse_payload(".matrix = [<uint:8> 1, <uint:8> 2]/[<uint:8> 3, <uint:8> 4];",
				  false, &ctx);
	ASSERT_TRUE(!ctx.has_errors, "2D array with typed elements must parse");
}

static void test_parse_nested_structs(void)
{
	printf("  test_parse_nested_structs...\n");

	parse_ctx_t ctx;

	const char *nested =
		".root = {\n"
		"    .level1 = {\n"
		"        .level2 = {\n"
		"            .value = 42;\n"
		"        };\n"
		"    };\n"
		"};\n";

	parse_payload(nested, false, &ctx);
	ASSERT_TRUE(!ctx.has_errors, "deeply nested structs must parse");
}

static void test_parse_comments_variations(void)
{
	printf("  test_parse_comments_variations...\n");

	parse_ctx_t ctx;

	parse_payload(".a = 1; # comment", false, &ctx);
	ASSERT_TRUE(!ctx.has_errors, "line comment after value must parse");

	parse_payload(
		".a = # comment\n"
		"42;",
		false, &ctx);
	ASSERT_TRUE(!ctx.has_errors, "comment between tokens must parse");

	parse_payload(
		"# header comment\n"
		".a = 1; # value comment\n"
		"# footer comment\n",
		false, &ctx);
	ASSERT_TRUE(!ctx.has_errors, "multiple comments must parse");
}

static void test_parse_whitespace_variations(void)
{
	printf("  test_parse_whitespace_variations...\n");

	parse_ctx_t ctx;

	parse_payload(".a=1;.b=2;", false, &ctx);
	ASSERT_TRUE(!ctx.has_errors, "assignments without whitespace must parse");

	parse_payload(
		"   .a   =   1   ;   \n"
		"   .b   =   2   ;   \n",
		false, &ctx);
	ASSERT_TRUE(!ctx.has_errors, "assignments with excessive whitespace must parse");

	parse_payload(".a\t=\t1\t;\n.b\n=\n2\n;\n", false, &ctx);
	ASSERT_TRUE(!ctx.has_errors, "assignments with tabs and newlines must parse");

	/* CRLF line endings: the second assignment must parse cleanly and
	 * column tracking must not be thrown off by the CR+LF pair. */
	parse_payload(".a = 1;\r\n.b = 2;\r\n", false, &ctx);
	ASSERT_TRUE(!ctx.has_errors, "assignments with CRLF line endings must parse");
}

static void test_parse_null_values_multiple(void)
{
	printf("  test_parse_null_values_multiple...\n");

	parse_ctx_t ctx;

	parse_payload(".a = ;", false, &ctx);
	ASSERT_TRUE(!ctx.has_errors, "untyped null must parse");

	parse_payload(".b = <uint:32> ;", false, &ctx);
	ASSERT_TRUE(!ctx.has_errors, "typed null must parse");

	parse_payload(".arr = [1, , 3, ];", false, &ctx);
	ASSERT_TRUE(!ctx.has_errors, "array with null elements must parse");
}

static void test_parse_float_variations(void)
{
	printf("  test_parse_float_variations...\n");

	parse_ctx_t ctx;

	parse_payload(".a = .5;", false, &ctx);
	ASSERT_TRUE(!ctx.has_errors, ".5 decimal notation must parse");

	parse_payload(".b = 5.;", false, &ctx);
	ASSERT_TRUE(!ctx.has_errors, "5. decimal notation must parse");

	parse_payload(".c = 1000000;", false, &ctx);
	ASSERT_TRUE(!ctx.has_errors, "large integer without separators must parse");
}

static void test_parse_mixed_array_types(void)
{
	printf("  test_parse_mixed_array_types...\n");

	parse_ctx_t ctx;

	parse_payload(".names = [\"Alice\", \"Bob\", \"Charlie\"];", false, &ctx);
	ASSERT_TRUE(!ctx.has_errors, "string array must parse");

	parse_payload(".vals = [1, , 3, , 5];", false, &ctx);
	ASSERT_TRUE(!ctx.has_errors, "mixed array with nulls must parse");
}

static void test_parse_units_variations(void)
{
	printf("  test_parse_units_variations...\n");

	parse_ctx_t ctx;

	parse_payload(".distance = <uint:32,m> 100;", false, &ctx);
	ASSERT_TRUE(!ctx.has_errors, "simple unit must parse");

	parse_payload(".length = <uint:32,k~m> 5;", false, &ctx);
	ASSERT_TRUE(!ctx.has_errors, "unit with SI prefix must parse");

	parse_payload(".size = <uint:64,Ki~B> 1024;", false, &ctx);
	ASSERT_TRUE(!ctx.has_errors, "unit with IEC prefix must parse");

	parse_payload(".speed = <float:64,m/s> 9.81;", false, &ctx);
	ASSERT_TRUE(!ctx.has_errors, "compound unit must parse");
}

static void test_parse_inline_unit_scalar(void)
{
	printf("  test_parse_inline_unit_scalar...\n");

	parse_ctx_t ctx;

	/* space-only separator — the primary form */
	parse_payload(".distance = 100 m;", false, &ctx);
	ASSERT_TRUE(!ctx.has_errors, "inline unit (space-only) on uint must parse");

	/* with type annotation, space separator */
	parse_payload(".dist = <float:64> 1.5 k~m;", false, &ctx);
	ASSERT_TRUE(!ctx.has_errors,
		"inline unit with annotation that has no unit must parse");

	/* compound unit */
	parse_payload(".force = 9.81 k~g*m/s\xc2\xb2;", false, &ctx);
	ASSERT_TRUE(!ctx.has_errors, "inline compound unit must parse");

	/* string literal with space separator */
	parse_payload(".hex_dist = <uint:32,_16> \"FF\" m;", false, &ctx);
	ASSERT_TRUE(!ctx.has_errors,
		"inline unit on string-encoded integer must parse");

	/* no_unit keyword */
	parse_payload(".ratio = 3.14 no_unit;", false, &ctx);
	ASSERT_TRUE(!ctx.has_errors, "inline 'no_unit' keyword must parse");

	/* special-number with space separator */
	parse_payload(".x = $nan s;", false, &ctx);
	ASSERT_TRUE(!ctx.has_errors, "inline unit after special-number must parse");

	/* number with exponent */
	parse_payload(".x = 1.5e3 k~J;", false, &ctx);
	ASSERT_TRUE(!ctx.has_errors, "inline unit after exponent must parse");

	/* unit starting with E — space separator removes exponent ambiguity */
	parse_payload(".x = 100 E~m;", false, &ctx);
	ASSERT_TRUE(!ctx.has_errors,
		"unit starting with E is unambiguous after whitespace separator");

	/* unit starting with e */
	parse_payload(".x = 9.81 eV;", false, &ctx);
	ASSERT_TRUE(!ctx.has_errors,
		"unit starting with e (eV) is unambiguous after whitespace separator");

	/* comment replaces explicit whitespace: newline from comment counts */
	parse_payload(".x = $nan # comment\n s;", false, &ctx);
	ASSERT_TRUE(!ctx.has_errors,
		"comment newline satisfies whitespace requirement for special-number");
}

static void test_parse_inline_unit_separator_errors(void)
{
	printf("  test_parse_inline_unit_separator_errors...\n");

	parse_ctx_t ctx;

	/* no-separator no-space must be rejected */
	parse_payload(".x = 100m;", true, &ctx);
	ASSERT_TRUE(ctx.has_errors,
		"direct adjacency without any separator must be rejected");

	/* no separator on float must be rejected */
	parse_payload(".speed = 9.81m/s;", true, &ctx);
	ASSERT_TRUE(ctx.has_errors,
		"direct adjacency on float must be rejected");

	/* no separator after special-number */
	parse_payload(".x = $nans;", true, &ctx);
	ASSERT_TRUE(ctx.has_errors,
		"direct adjacency after special-number must error");

	/* no separator after exponent */
	parse_payload(".x = 1.5e3k-J;", true, &ctx);
	ASSERT_TRUE(ctx.has_errors,
		"direct adjacency after exponent must be rejected");

	/* inline unit inside an array element */
	parse_payload(".arr = [1 m, 2 m];", true, &ctx);
	ASSERT_TRUE(ctx.has_errors,
		"inline unit inside array must be rejected");
}

static void test_parse_inline_unit_annotation_match(void)
{
	printf("  test_parse_inline_unit_annotation_match...\n");

	parse_ctx_t ctx;

	parse_payload(".dist = <float:64,m> 1.5 m;", false, &ctx);
	ASSERT_TRUE(!ctx.has_errors,
		"matching inline and annotation unit must parse");

	parse_payload(".speed = <float:64,m/s> 9.81 m/s;", false, &ctx);
	ASSERT_TRUE(!ctx.has_errors,
		"matching inline and annotation compound unit must parse");

	parse_payload(".mass = <float:32,k~g> 70.0 k~g;", false, &ctx);
	ASSERT_TRUE(!ctx.has_errors,
		"matching inline and annotation SI-prefix unit must parse");
}

static void test_parse_inline_unit_annotation_mismatch(void)
{
	printf("  test_parse_inline_unit_annotation_mismatch...\n");

	parse_ctx_t ctx;

	parse_payload(".x = <float:64,m> 1.0 s;", false, &ctx);
	ASSERT_TRUE(ctx.has_errors,
		"mismatched inline vs annotation unit must produce an error");
	ASSERT_EQ_INT(ctx.last_error, error_unit_mismatch,
		"expected error_unit_mismatch for unit conflict");

	parse_payload(".x = <float:64,m/s> 9.81 k~m/s;", false, &ctx);
	ASSERT_TRUE(ctx.has_errors,
		"mismatched compound inline vs annotation unit must error");
	ASSERT_EQ_INT(ctx.last_error, error_unit_mismatch,
		"expected error_unit_mismatch for compound unit conflict");

	parse_payload(".x = <float:64,no_unit> 1.0 m;", false, &ctx);
	ASSERT_TRUE(ctx.has_errors,
		"inline unit conflicting with explicit no_unit annotation must error");
	ASSERT_EQ_INT(ctx.last_error, error_unit_mismatch,
		"expected error_unit_mismatch when annotation is no_unit");
}

static void test_parse_inline_unit_array_rejected(void)
{
	printf("  test_parse_inline_unit_array_rejected...\n");

	parse_ctx_t ctx;

	parse_payload(".arr = [1 m, 2 m, 3 m];", true, &ctx);
	ASSERT_TRUE(ctx.has_errors,
		"inline unit inside array must produce an error");

	parse_payload(".arr = [9.81 m/s];", true, &ctx);
	ASSERT_TRUE(ctx.has_errors,
		"inline compound unit inside array must produce an error");
}

static void test_parse_identifier_edge_cases(void)
{
	printf("  test_parse_identifier_edge_cases...\n");

	parse_ctx_t ctx;

	parse_payload(".my_var = 1;", false, &ctx);
	ASSERT_TRUE(!ctx.has_errors, "identifier with underscore must parse");

	parse_payload(".var123 = 2;", false, &ctx);
	ASSERT_TRUE(!ctx.has_errors, "identifier with numbers must parse");

	parse_payload(".myVarName = 3;", false, &ctx);
	ASSERT_TRUE(!ctx.has_errors, "CamelCase identifier must parse");
}

static void test_parse_error_recovery_multiple(void)
{
	printf("  test_parse_error_recovery_multiple...\n");

	parse_ctx_t ctx;

	const char *payload =
		".a = 1;\n"
		".b = <invalid:99> 2;\n"
		".c = 3;\n";

	parse_payload(payload, true, &ctx);
	ASSERT_TRUE(ctx.has_errors, "malformed input must trigger error callback");
	ASSERT_TRUE(ctx.error_count > 0, "error_count must be incremented");
}

static void test_parse_empty_stream(void)
{
	printf("  test_parse_empty_stream...\n");

	parse_ctx_t ctx;
	parse_payload("", false, &ctx);
	ASSERT_TRUE(!ctx.has_errors, "empty stream must not cause error");
}

static void test_parse_only_comments(void)
{
	printf("  test_parse_only_comments...\n");

	parse_ctx_t ctx;
	parse_payload("# just comments\n# and more comments\n", false, &ctx);
	ASSERT_TRUE(!ctx.has_errors, "stream with only comments must parse");
}

static void test_parse_type_variations(void)
{
	printf("  test_parse_type_variations...\n");

	parse_ctx_t ctx;

	parse_payload(".a = <uint:8> 255;", false, &ctx);
	ASSERT_TRUE(!ctx.has_errors, "uint:8 must parse");

	parse_payload(".b = <uint:16> 65535;", false, &ctx);
	ASSERT_TRUE(!ctx.has_errors, "uint:16 must parse");

	parse_payload(".c = <uint:32> 4294967295;", false, &ctx);
	ASSERT_TRUE(!ctx.has_errors, "uint:32 must parse");

	parse_payload(".d = <sint:16> -32768;", false, &ctx);
	ASSERT_TRUE(!ctx.has_errors, "sint:16 must parse");

	parse_payload(".e = <float:32> 1.5;", false, &ctx);
	ASSERT_TRUE(!ctx.has_errors, "float:32 must parse");

	parse_payload(".f = <float:64> 3.14159;", false, &ctx);
	ASSERT_TRUE(!ctx.has_errors, "float:64 must parse");
}

int main(void)
{
	printf("Running bovnar_extended_reader_test regression suite...\n");

	test_parse_various_integer_bases();
	test_parse_string_escapes();
	test_parse_negative_numbers();
	test_parse_scientific_notation();
	test_parse_arrays_2d();
	test_parse_nested_structs();
	test_parse_comments_variations();
	test_parse_whitespace_variations();
	test_parse_null_values_multiple();
	test_parse_float_variations();
	test_parse_mixed_array_types();
	test_parse_units_variations();
	test_parse_inline_unit_scalar();
	test_parse_inline_unit_separator_errors();
	test_parse_inline_unit_annotation_match();
	test_parse_inline_unit_annotation_mismatch();
	test_parse_inline_unit_array_rejected();
	test_parse_identifier_edge_cases();
	test_parse_error_recovery_multiple();
	test_parse_empty_stream();
	test_parse_only_comments();
	test_parse_type_variations();

	if (failures == 0) {
		printf("PASSED %d tests\n", tests);
		return 0;
	}

	fprintf(stderr, "FAILED %d of %d tests\n", failures, tests);
	return 1;
}
