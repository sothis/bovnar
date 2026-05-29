#include <errno.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int mkdirp(const char *path)
{
	char tmp[4096];
	snprintf(tmp, sizeof(tmp), "%s", path);
	size_t len = strlen(tmp);
	if (len && tmp[len - 1] == '/')
		tmp[--len] = '\0';

	for (size_t i = 1; i <= len; i++) {
		if (tmp[i] == '/' || tmp[i] == '\0') {
			char saved = tmp[i];
			tmp[i] = '\0';
			if (mkdir(tmp, 0755) != 0 && errno != EEXIST)
				return -1;
			tmp[i] = saved;
		}
	}
	return 0;
}

static unsigned g_file_counter = 0;

static int write_seed(const char *dir, const void *data, size_t len,
					  const char *suffix)
{
	char path[4096];
	snprintf(path, sizeof(path), "%s/seed_%04u%s", dir,
			 g_file_counter++, suffix);
	FILE *f = fopen(path, "wb");
	if (!f) { perror(path); return -1; }
	fwrite(data, 1, len, f);
	fclose(f);
	printf("  wrote %s  (%zu bytes)\n", path, len);
	return 0;
}

static const char *bvnr_seeds[] = {

	".x = 42;\n",
	".x = -17;\n",
	".x = 3.14;\n",
	".x = \"hello world\";\n",
	".x = true;\n",
	".x = false;\n",
	".x = ;\n",

	".x = $nan;\n",
	".x = $inf;\n",
	".x = $-inf;\n",

	".x = \"tab\\there\";\n",
	".x = \"nl\\nhere\";\n",
	".x = \"quote\\\"here\";\n",
	".x = \"bs\\\\here\";\n",

	".x = \"hello \" \"world\";\n",

	".x = <uint:8> 255;\n",
	".x = <uint:16> 65535;\n",
	".x = <uint:32> 4294967295;\n",
	".x = <uint:64> 18446744073709551615;\n",
	".x = <sint:8> -128;\n",
	".x = <sint:8> 127;\n",
	".x = <sint:32> -2147483648;\n",
	".x = <float:32> 3.14;\n",
	".x = <float:64> 1e100;\n",
	".x = <float:64> $nan;\n",
	".x = <utf8> \"text\";\n",

	".x = <uint:_16> \"ff\";\n",
	".x = <uint:_2> \"1010\";\n",
	".x = <uint:_8> \"77\";\n",
	".x = <uint:_36> \"zz\";\n",

	".x = <float:64,s> 2.5;\n",
	".x = <float:64,m> 100.0;\n",
	".x = <float:64,g> 500.0;\n",
	".x = <uint:64,B> 1024;\n",
	".x = <uint:64,Ki~B> 1;\n",
	".x = <uint:64,Mi~B> 1;\n",
	".x = <float:64,no_unit> 1.0;\n",

	".x = <float:64,m/s> 9.81;\n",
	".x = <float:64,m/s\xC2\xB2> 9.81;\n",
	".x = <float:64,k~g\xC2\xB7m/s\xC2\xB2> 9.81;\n",
	".x = <float:64,k~g\xC2\xB7m\xC2\xB2/s\xC2\xB2> 100.0;\n",
	".x = <float:64,m*s> 1.0;\n",
	".x = <float:64,k~g/m\xC2\xB3> 7800.0;\n",

	".a = [1, 2, 3];\n",
	".a = [1,2,3]/[4,5,6];\n",
	".a = [,1,,2,];\n",
	".a = [[1,2],[3,4]];\n",
	".a = [\"a\", \"b\", \"c\"];\n",
	".a = [true, false, true];\n",
	".a = [<sint:16> 1, <sint:16> , <sint:16> 3];\n",

	".s = {};\n",
	".s = { .x = 1; .y = 2; };\n",
	".s = { .inner = { .val = 42; }; };\n",
	".s = [\n{.name = \"Alice\"; .age = 30;},\n{.name = \"Bob\"; .age = 25;}\n];\n",

	".host = \"localhost\";\n.ref = &.host;\n",
	".a = 1;\n.b = &.a;\n",

	".status = ok;\n",
	".day = Monday;\n",
	".flags = [red, green, blue];\n",

	"# full line comment\n.x = 1;\n",
	".x = 1; # inline comment\n",

	"\xEF\xBB\xBF.x = 1;\n",

	".caf\xC3\xA9 = 1;\n",

	".x = .5;\n",
	".x = 123.;\n",
	".x = 007;\n",
	".x = 1e6;\n",
	".x = -2.5e-3;\n",
	".x = 1E+10;\n",

	".b = \x00\x01\x05\x00hello\x01\x03\x00bye\x00;\n",

	". = 42;\n",
	".x = <uint:8> 256;\n",
	".x = <uint:8> -1;\n",
	".x = <float:8> 1.0;\n",
	".x = <float:64,_2> 1.0;\n",
	".x = \"\\q\";\n",
	".x = <utf8> 42;\n",
	".x = [[1,2],[3,4,5]];\n",
	".x = };\n",
	".x = <float:64,m//s> 1.0;\n",

	".a = { .b = { .c = { .d = 1; }; }; };\n",

	".m = [<float:32> 1.0, <float:32> 2.0]/[<float:32> 3.0, <float:32> 4.0];\n",

	".velocity = <float:64,m/s> 9.81;\n"
	".force = <float:64,k~g\xC2\xB7m/s\xC2\xB2> 9.81;\n"
	".storage = <uint:64,Ti~B> 2;\n"
	".frequency = <float:64,k~Hz> 2.4;\n",

	"",

	"   \n\t  \n",

	".a=1;.b=2;.c=3;.d=4;.e=5;.f=6;.g=7;.h=8;.i=9;.j=10;\n",
};

static void write_reader_seeds(const char *dir)
{
	printf("Reader seeds:\n");
	size_t n = sizeof(bvnr_seeds) / sizeof(bvnr_seeds[0]);

	static const uint8_t configs[] = {
		0x00,
		0x04,
		0x01,
		0x05,
		0x02,
		0x06,
	};

	for (size_t i = 0; i < n; i++) {
		size_t slen = strlen(bvnr_seeds[i]);
		size_t bufsz = slen + 2;
		uint8_t *buf = malloc(bufsz);
		if (!buf) continue;

		for (size_t c = 0; c < sizeof(configs); c++) {
			buf[0] = configs[c];
			buf[1] = (uint8_t)(i % 256);
			memcpy(buf + 2, bvnr_seeds[i], slen);
			write_seed(dir, buf, bufsz, ".bvnr");
		}
		free(buf);
	}
}

static void write_dom_seeds(const char *dir)
{
	printf("DOM seeds:\n");
	size_t n = sizeof(bvnr_seeds) / sizeof(bvnr_seeds[0]);
	for (size_t i = 0; i < n; i++) {
		write_seed(dir, bvnr_seeds[i], strlen(bvnr_seeds[i]), ".bvnr");
	}
}

typedef struct {
	uint8_t     selector;
	uint8_t     aux_a;
	uint8_t     aux_b;
	const char *payload;
	const char *description;
} utils_seed_t;

static const utils_seed_t utils_seeds[] = {

	{ 0, 0, 0, "foo",               "valid identifier" },
	{ 0, 0, 0, "_private",          "underscore identifier" },
	{ 0, 0, 0, "my-key",            "hyphen identifier" },
	{ 0, 0, 0, "",                  "empty identifier" },
	{ 0, 0, 0, "123bad",            "digit-started" },
	{ 0, 0, 0, "foo!bar",           "illegal char in id" },

	{ 1, 0, 0, "ok",                "valid symbol" },
	{ 1, 0, 0, "Monday",            "capitalized symbol" },
	{ 1, 0, 0, "",                  "empty symbol" },

	{ 2, 0, 0, ".host",             "simple reference" },
	{ 2, 0, 0, ".config.host",      "dotted reference" },
	{ 2, 0, 0, "noledot",           "missing leading dot" },

	{ 3, 0, 0, "42",                "integer" },
	{ 3, 0, 0, "-17",               "negative integer" },
	{ 3, 0, 0, "3.14",              "float" },
	{ 3, 0, 0, "1e6",               "scientific" },
	{ 3, 0, 0, ".5",                "dot-led" },
	{ 3, 0, 0, "123.",              "trailing dot" },
	{ 3, 0, 0, "abc",               "non-numeric" },

	{ 4, 0, 0, "hello world",       "plain string" },
	{ 4, 0, 0, "caf\xC3\xA9",       "UTF-8 string" },
	{ 4, 0, 0, "",                  "empty string" },

	{ 5, 0, 0, "nan",               "nan" },
	{ 5, 0, 0, "inf",               "inf" },
	{ 5, 0, 0, "-inf",              "-inf" },
	{ 5, 0, 0, "notspecial",        "not special" },

	{ 6, 10, 0, "12345",            "base-10 digits" },
	{ 6, 16, 0, "deadbeef",         "base-16 hex" },
	{ 6, 2,  0, "1010",             "base-2 binary" },
	{ 6, 16, 0, "xyz",              "invalid hex digits" },

	{ 10, 0, 0, "m",                "meter" },
	{ 10, 0, 0, "m/s",              "m per s" },
	{ 10, 0, 0, "m/s\xC2\xB2",      "m/s²" },
	{ 10, 0, 0, "k~g\xC2\xB7m/s\xC2\xB2", "k~g·m/s²" },
	{ 10, 0, 0, "Ki~B",             "kibibyte" },
	{ 10, 0, 0, "no_unit",          "dimensionless" },
	{ 10, 0, 0, "m//s",             "invalid: empty component" },
	{ 10, 0, 0, "",                 "empty unit string" },
	{ 10, 0, 0, "m*s*k~g*A*K*mol*cd*b*Hz", "9 components (overflow)" },

	{ 11, 3, 8, "42",               "uint64 parse" },
	{ 11, 3, 8, "-1",               "negative into uint" },
	{ 11, 2, 0, "-128",             "sint8 parse" },
	{ 11, 2, 0, "127",              "sint8 max" },

	{ 12, 3, 0, "255",              "uint8 255" },
	{ 12, 3, 0, "18446744073709551615", "uint64 max" },
	{ 12, 3, 0, "ffffffffffffffff",  "hex digits (wrong base)" },

	{ 15, 0, 0, "3.14",             "double" },
	{ 15, 0, 0, "1e6",              "scientific" },
	{ 15, 0, 0, "42",               "integer" },
	{ 15, 0, 0, "nan",              "nan" },

	{ 16, 10, 0, "\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF", "uint64 max base-10" },
	{ 16, 16, 8, "\xDE\xAD\xBE\xEF\x00\x00\x00\x00", "deadbeef base-16" },

	{ 20, 0, 0, "uint:32",          "uint:32 annotation" },
	{ 20, 0, 0, "",                 "empty annotation" },
	{ 20, 0, 0, "uint:",            "uint no params" },
	{ 20, 0, 0, "uint:_16",         "uint base-16" },
	{ 20, 0, 0, "float:64,k~g\xC2\xB7m/s\xC2\xB2", "compound unit" },
};

static void write_utils_seeds(const char *dir)
{
	printf("Utils seeds:\n");
	size_t n = sizeof(utils_seeds) / sizeof(utils_seeds[0]);

	for (size_t i = 0; i < n; i++) {
		const utils_seed_t *s = &utils_seeds[i];
		size_t plen = strlen(s->payload);
		size_t bufsz = 3 + plen;
		uint8_t *buf = malloc(bufsz + 1);
		if (!buf) continue;

		buf[0] = s->selector;
		buf[1] = s->aux_a;
		buf[2] = s->aux_b;
		memcpy(buf + 3, s->payload, plen);

		write_seed(dir, buf, bufsz, ".bin");
		free(buf);
	}
}

int main(int argc, char **argv)
{
	const char *base = argc > 1 ? argv[1] : "corpus";

	char reader_dir[4096], dom_dir[4096], utils_dir[4096];
	snprintf(reader_dir, sizeof(reader_dir), "%s/reader", base);
	snprintf(dom_dir,    sizeof(dom_dir),    "%s/dom",    base);
	snprintf(utils_dir,  sizeof(utils_dir),  "%s/utils",  base);

	if (mkdirp(reader_dir) || mkdirp(dom_dir) || mkdirp(utils_dir)) {
		fprintf(stderr, "Failed to create output directories: %s\n",
				strerror(errno));
		return 1;
	}

	write_reader_seeds(reader_dir);
	g_file_counter = 0;
	write_dom_seeds(dom_dir);
	g_file_counter = 0;
	write_utils_seeds(utils_dir);

	printf("\nDone. Corpus written to %s/{reader,dom,utils}/\n", base);
	return 0;
}
