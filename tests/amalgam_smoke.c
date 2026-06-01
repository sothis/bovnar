/*
 * Smoke test for the single-file amalgamation (dist/bovnar.h + dist/bovnar.c).
 *
 * Compiled and run by cmake/amalgam_smoke.cmake (CTest target bvnr_amalgam),
 * which first regenerates dist/ via amalgamate.py. It exercises a value across
 * several subsystems (lexer, validator, DOM, units, big-int, special floats)
 * so a broken amalgamation fails loudly rather than silently rotting.
 */
#include "bovnar.h"
#include <stdio.h>
#include <string.h>

#define CHECK(cond, msg) do { if (!(cond)) { \
	printf("amalgam_smoke FAIL: %s\n", (msg)); return 1; } } while (0)

int main(void)
{
	const char *src =
		".speed = <float:64,m/s> 9.81;\n"
		".accel = 9.81 (m/s)/s;\n"            /* inline leading-paren unit */
		".big   = <uint:128> 340282366920938463463374607431768211455;\n"
		".inf   = inf;\n"
		".ninf  = ninf;\n"
		".price = <float_dec:64,$USD> 19.99;\n";

	uint32_t n = (uint32_t)strlen(src);
	bvn_dom_doc_t *d = bvn_dom_parse(src, n);
	CHECK(d != NULL, "bvn_dom_parse returned NULL");
	CHECK(bvn_dom_doc_get_parse_error(d) == error_none, "unexpected parse error");

	bvn_dom_node_t *speed = bvn_dom_lookup(d, ".speed");
	double f = 0.0;
	CHECK(speed && bvn_dom_get_float(speed, &f) && f > 9.8 && f < 9.82, ".speed value");

	bvn_dom_node_t *inf = bvn_dom_lookup(d, ".inf");
	double iv = 0.0;
	CHECK(inf && bvn_dom_get_float(inf, &iv) && iv > 1e308, ".inf value");

	bvn_dom_node_t *big = bvn_dom_lookup(d, ".big");
	CHECK(big != NULL, ".big present");

	/* A document the validator must reject (mixed-kind array) still parses to a
	 * doc but records the error, confirming the validator is wired in. */
	const char *bad = ".x = [1, \"two\"];\n";
	bvn_dom_doc_t *bd = bvn_dom_parse(bad, (uint32_t)strlen(bad));
	CHECK(bd != NULL, "bad-doc parse NULL");
	CHECK(bvn_dom_doc_get_parse_error(bd) == error_array_element_type_mismatch,
	      "expected array_element_type_mismatch");

	bvn_dom_doc_destroy(d);
	bvn_dom_doc_destroy(bd);
	printf("amalgam_smoke OK\n");
	return 0;
}
