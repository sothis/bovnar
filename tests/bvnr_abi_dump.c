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

/*
 * ABI manifest generator — the single source of truth for the public struct
 * layout the language bindings must mirror. It prints, for every public struct
 * that a binding reproduces (the Python ctypes mirrors in particular), the
 * compiler's own sizeof and per-field offsetof/sizeof.
 *
 * The Python ABI test (python/tests/test_abi.py) runs this and asserts each
 * ctypes Structure matches: exact size + every field offset/size for the
 * field-mirrored structs, and "binding size >= C size" for the opaque
 * pass-through blobs (bvnr_source_t / bvnr_sink_t), which a binding may safely
 * over-allocate but must never under-allocate.
 *
 * Output format (one record per line, whitespace-separated):
 *   STRUCT <c-typedef-name> <sizeof>
 *   FIELD  <c-typedef-name> <field-name> <offsetof> <sizeof-field>
 */

#include <stddef.h>
#include <stdio.h>
#include "bovnar.h"
#include "bovnar_dom.h"
#include "bovnar_stream.h"

#define ABI_STRUCT(st)     printf("STRUCT %s %zu\n", #st, sizeof(st))
#define ABI_FIELD(st, f)   printf("FIELD %s %s %zu %zu\n", #st, #f, \
                                  offsetof(st, f), sizeof(((st *)0)->f))

int main(void)
{
	ABI_STRUCT(value_type_spec_t);
	ABI_FIELD(value_type_spec_t, family);
	ABI_FIELD(value_type_spec_t, width);
	ABI_FIELD(value_type_spec_t, base);

	ABI_STRUCT(value_unit_prefix_t);
	ABI_FIELD(value_unit_prefix_t, system);
	ABI_FIELD(value_unit_prefix_t, id);

	ABI_STRUCT(value_unit_component_t);
	ABI_FIELD(value_unit_component_t, base);
	ABI_FIELD(value_unit_component_t, exponent);
	ABI_FIELD(value_unit_component_t, prefix);

	ABI_STRUCT(value_unit_t);
	ABI_FIELD(value_unit_t, num_components);
	ABI_FIELD(value_unit_t, components);

	ABI_STRUCT(bvnr_data_t);
	ABI_FIELD(bvnr_data_t, type);
	ABI_FIELD(bvnr_data_t, value_type);
	ABI_FIELD(bvnr_data_t, value_unit);
	ABI_FIELD(bvnr_data_t, data);
	ABI_FIELD(bvnr_data_t, length);
	ABI_FIELD(bvnr_data_t, frac_data);
	ABI_FIELD(bvnr_data_t, frac_length);

	ABI_STRUCT(bvnr_read_flags_t);
	ABI_STRUCT(bvnr_write_flags_t);

	/* opaque pass-through blobs: size only (a binding may over-allocate) */
	ABI_STRUCT(bvnr_source_t);
	ABI_STRUCT(bvnr_sink_t);

	ABI_STRUCT(bvnr_doc_stream_opts_t);
	ABI_FIELD(bvnr_doc_stream_opts_t, flags);
	ABI_FIELD(bvnr_doc_stream_opts_t, userdata);
	ABI_FIELD(bvnr_doc_stream_opts_t, on_document);
	ABI_FIELD(bvnr_doc_stream_opts_t, continue_past_failed);
	ABI_FIELD(bvnr_doc_stream_opts_t, max_document_size);

	ABI_STRUCT(bvn_dom_entry_t);
	ABI_FIELD(bvn_dom_entry_t, key);
	ABI_FIELD(bvn_dom_entry_t, value);

	return 0;
}
