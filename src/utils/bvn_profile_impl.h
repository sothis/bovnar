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

#ifndef BVN_PROFILE_IMPL_H_
#define BVN_PROFILE_IMPL_H_
#include <stdbool.h>
#include <stdint.h>
#include "bovnar.h"

/*
 * The unit profiles (spec 1.2). See doc/11_bovnar_unit_profiles.md.
 *
 * A "<namespace>:<code>" expression in the unit slot is translated, at parse
 * time, into an ordinary value_unit_t. Nothing downstream of bvn_parse_unit
 * knows the profile exists: equality, compatibility, conversion, the unit
 * policies, the DOM and the writer all operate on the translated unit.
 *
 * The one exception is a unit containing an OPAQUE base unit -- UCUM's arbitrary
 * atoms ([IU], [PFU], ...) and UNECE's package types among them. Those have no
 * native spelling, so they serialise back in profile notation, which is what
 * bvni_unit_has_opaque() and bvni_unit_profile_spelling() are for.
 */

/* Every way a profile expression can fail to become a unit. The caller maps
 * these onto error codes; see doc/11_bovnar_unit_profiles.md section 3.1. */
typedef enum {
	bvni_profile_ok = 0,
	/* Not a valid expression in that vocabulary, or a code it does not define. */
	bvni_profile_illegal,
	/* Valid in that vocabulary, but with no bovnar representation. */
	bvni_profile_unsupported,
	/* The namespace before the ':' is not a profile this build knows. */
	bvni_profile_unknown_ns
} bvni_profile_status_t;

/* True when `s` opens with a "name:" profile namespace. Cheap: it does not
 * validate the namespace, only notices that one is present, so the native
 * parser can hand off before doing any work of its own. */
bool bvni_unit_has_profile(const char* s, uint32_t len);

/* Translate a complete "name:code" profile expression. On bvni_profile_ok the
 * returned unit is an ordinary value_unit_t; otherwise it is BVN_UNIT_NONE and
 * *status says which of the three refusals applies. */
value_unit_t bvni_parse_profile_unit(const char* s, uint32_t len,
                                     bvni_profile_status_t* status);

/* True for a base unit in the shared OPAQUE block: a profile-only unit with no
 * native spelling and no dimension. A range test -- the block is contiguous and
 * sits above every native unit, both enforced at build time (gen_profiles.py and
 * the static assertions in bvn_internal_dims.h). */
bool bvni_is_opaque(value_base_unit_t base);

/* True when any component of `u` is an opaque unit, i.e. `u` has no native
 * spelling and bvn_unit_to_string must emit the profile form. */
bool bvni_unit_has_opaque(value_unit_t u);

/* Write `u` as a code in the named profile (no "<ns>:" prefix), NUL-terminated.
 * Returns the length written, or -1 if the namespace is unknown, the unit has no
 * form in that vocabulary, or the buffer is too small. Partial by construction:
 * a native unit outside the transliteration table -- the Old German units, water
 * hardness, turbidity, every currency -- has nowhere to go, and a flat profile
 * can spell only a single unprefixed component. */
int32_t bvni_unit_to_profile(const char* ns, uint32_t ns_len, value_unit_t u,
                             char* buf, size_t bufsize);

/* bvni_unit_to_profile against the "ucum" namespace, for the public
 * bvn_unit_to_ucum. */
int32_t bvni_unit_to_ucum(value_unit_t u, char* buf, size_t bufsize);

/* Write the full "<ns>:<code>" canonical spelling of a unit that has no native
 * one, picking the namespace that owns its opaque units. Returns -1 when `u`
 * has no opaque component, when its opaque components come from two different
 * profiles (no single spelling exists), or when the buffer is too small. */
int32_t bvni_unit_profile_spelling(value_unit_t u, char* buf, size_t bufsize);

#endif /* BVN_PROFILE_IMPL_H_ */
