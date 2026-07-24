// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#include <assert.h>
#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "misc/bit.h"
#include "misc/common.h"
#include "misc/endian.h"
#include "misc/fast_math.h"
#include "misc/math.h"
#include "misc/mparser.h"
#include "misc/time.h"

#include "raster/pal.h"
#include "raster/pix.h"
#include "raster/unpack.h"

#include "opts.h"
#include "wudefs.h"

#define FULL_X32 "0x%08" PRIx32
static const uint8_t NUM_SEQ[] = {
	0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
};
static void test_name(const char *str) {
	fputs("---=== ", stdout);
	fputs(str, stdout);
	fputs(" ===---\n", stdout);
}
static void print_blob(const void *buf, const size_t len, const char trail) {
	const uint8_t *b = buf;
	putchar('{');
	for (size_t i = 0; i < len; ++i) {
		printf("%02x%c", b[i], (i + 1 == len) ? '}' : ',');
	}
	putchar(trail);
}
static const char * ok_str(const bool ok) {
	return ok ? "" : "!!!!!!";
}

typedef float (*fclampf_fn)(float n, float x, float y);
static bool test_fclamp_inner(const float fcla[static 4], const fclampf_fn fn) {
	float n = fcla[0], x = fcla[1], y = fcla[2], exp = fcla[3];
	const float result = (*fn)(n, x, y);
	const bool ok = exp == result;
	printf("%s\t%+.3f\t%+.3f\t%+.3f\t%+.3f\t%+.3f\n", ok_str(ok),
		n, x, y, exp, result);
	return ok;
}
static bool test_fclampf_fn(const fclampf_fn fn) {
	puts("\tn\tmin\tmax\texpect\tresult");
	const float third = (float)(1.0/3.0);
	const float fcla[][4] = {
		{third, -.5f, .5f, third},
		{third, -.2f, .2f, .2f},
		{-third, -.2f, .2f, -.2f},
		{-1, -0, 0, -0},
	};
	bool ok = true;
	for (size_t i = 0; i < ARRAY_LEN(fcla); ++i) {
		ok &= test_fclamp_inner(fcla[i], fn);
	}
	return ok;
}

/* fast_math tests */
static void print_float_range(const float start, const float limit,
const char c) {
	printf("[%a, %a]%c", start, limit, c);
}
static void print_float_misses(unsigned misses, unsigned tested,
const char c) {
	printf("%u out of %u (%f%%)%c",
		misses, tested, (float)misses / ((float)tested / 100), c);
}
static bool test_roundf_miss(const float x, const float norm) {
	return (long)fm_pre_roundf(x, norm) != lroundf(x * norm);
}
static bool test_roundf(const float start, const float limit,
const float norm) {
	unsigned tested = 0;
	unsigned misses = 0;
	for (float x = start; x <= limit; x = nextafterf(x, norm)) {
		misses += test_roundf_miss(x, norm);
		++tested;
	}
	const bool ok = !misses;
	printf("%s\t", ok_str(ok));
	print_float_range(start, limit, '\t');
	print_float_misses(misses, tested, '\n');
	fflush(stdout);
	return ok;
}
static float floor_norm(const float x, const float norm) {
	return floorf(fm_pre_roundf(x, norm));
}
static bool test_powf(const float start, const float limit, const float norm,
const double exp) {
	const float e = (float)exp;
	const float ie = (float)(1/exp);
	unsigned worst = 0;
	unsigned tested = 0;
	unsigned misses = 0;
	for (float x = start; x <= limit; x = nextafterf(x, norm)) {
		float ref = powf(powf(x, e), ie);
		float fast = fm_powf(fm_powf(x, e), ie);
		float diff = floor_norm(ref, norm) - floor_norm(fast, norm);
		misses += diff != 0;
		++tested;
		worst = umax(worst, (unsigned)fabsf(diff));
	}
	const bool ok = worst <= 1;
	printf("%s\t", ok_str(ok));
	print_float_range(start, limit, '\t');
	printf("%u\t", worst);
	print_float_misses(misses, tested, '\n');
	fflush(stdout);
	return ok;
}
static bool test_powf_basic(const float x, const float y, const float expect) {
	// Use _unchecked version to avoid unused function warnings
	float r = fm_powf_unchecked(x, y);
	const bool ok = r == expect;
	printf("%s\t%.3f\t%+.3f\t%.3f\t%.3f\n", ok_str(ok), x, y, expect, r);
	return ok;
}
static bool test_log2f(const float x) {
	float a = fm_log2f_for_pow(x, 1);
	const bool ok = a >= -127 && a <= 128;
	printf("%s\t%a\t%f\n", ok_str(ok), x, a);
	return ok;
}
static bool test_fm_mix(float iters, float mix, float lo_add,
float hi_add) {
	float hi = 1.0f;
	float mid = mix;
	float lo = 0.0f;
	float mid_add = fm_mix(lo_add, hi_add, mix);
	unsigned misses = 0;
	unsigned tested = (unsigned)iters;
	for (float i = 0; i < iters; ++i) {
		float m = fm_mix(lo + lo_add*i, hi + hi_add*i, mix);
		misses += m != mid + mid_add*i;
	}
	const bool ok = !misses;
	printf("%s\t%+.3f\t%+.3f\t%+.3f\t", ok_str(ok), mix, lo_add, hi_add);
	print_float_misses(misses, tested, '\n');
	return ok;
}

static bool fast_math_tests(void) {
	test_name("fast_math_tests (slowness ahead!)");
	bool kay = true;

	puts("assumptions");
	puts("\tlast subnormal\tfirst normal");
	const float first_normal = 0x1p-126;
	const float last_subnormal = nextafterf(first_normal, 0);
	printf("%s\t%a\t%a\n",
		ok_str(kay), last_subnormal, first_normal);
	puts("");

	puts("fm_fclampf()");
	kay &= test_fclampf_fn(fm_fclampf);
	puts("");

	puts("fm_mix(low + lowA*i, high + highA*i, mix)");
	puts("\tmix\tlowA\thighA\tnr misses");
	const float mixa[][3] = {
		{0.5f, 0.0f, 2.0f},
		{0.75f, -1.0f, 1.0f},
		{0.25f, -0x1p-8f, 0x1p-8f},
	};
	for (size_t i = 0; i < ARRAY_LEN(mixa); ++i) {
		kay &= test_fm_mix(1 << 16, mixa[i][0], mixa[i][1], mixa[i][2]);
	}
	puts("");

	const float roundf_norm = 0x1p+16 - 1;
	printf("(long)fm_pre_roundf() vs lroundf(), floats scaled to [0, %.0f]\n",
		roundf_norm);
	puts("\tall floats in range\tnr misses");
	kay &= test_roundf(0, 0, roundf_norm);
	kay &= test_roundf(0x1p-17, 0x1p-0, roundf_norm);
	puts("");

	puts("fm_log2f() output in range [-127, 128], inputs <= first_normal don't matter");
	puts("\tinput\toutput");
	const float log2_args[] = {
		(float)-first_normal, 0.0f, first_normal,
		1, 0x1p+127f, (float)0x1p+128, (float)0x1p+129,
	};
	for (size_t i = 0; i < ARRAY_LEN(log2_args); ++i) {
		kay &= test_log2f(log2_args[i]);
	}
	puts("");

	const float pow_args[][3] = {
		{.25f, 0, 1},
		{.25f, 1, .25f},
		{.25f, -1, 4},
	};
	puts("fm_powf() with simple exponents");
	puts("\tbase\texp\texpect\tresult");
	for (size_t i = 0; i < ARRAY_LEN(pow_args); ++i) {
		const float *p = pow_args[i];
		kay &= test_powf_basic(p[0], p[1], p[2]);
	}
	puts("");

	const float powf_norm = 0x1p+16 - 1;
	const double exp = COLOR_SRGB_DISPLAY_GAMMA;
	printf("fm_powf() vs powf(), (x^%.1f)^(1/%.1f) then scaled to [0, %.0f], at most off by one\n",
		exp, exp, powf_norm);
	puts("\tall floats in range\tworst diff\tnr misses");
	const float ranges[][2] = {
		{0, 0},
		{first_normal, 0x1p-125},
		{0x1p-18, 0x1p-17},
		{0x1p-17, 0x1p-16},
		{0x1p-16, 1},
	};
	for (size_t i = 0; i < ARRAY_LEN(ranges); ++i) {
		kay &= test_powf(ranges[i][0], ranges[i][1], powf_norm, exp);
	}
	puts("");
	return kay;
}

/* opts tests */
enum test_opts_c {
	to_done = 0,

	to_a = 'a',
	to_b = 'b',

	to_k = 'k',
	to_h = 'h',

	to_no_char_flag = 0x80,
	to_no_char_opt = 0x81,
};
static const struct opts test_opts[] = {
	{to_a, "", "", "flag with short name"},
	{to_b, "bool", "", "flag with short and long names"},
	{to_no_char_flag, "enable", "", "flag with long name"},

	{to_k, "", "NUM", "opt with short name"},
	{to_h, "hey", "KEY", "opt with short and long name"},
	{to_no_char_opt, "yay", "KEY", "opt with long name"},
};
struct test_opts_args {
	bool a, b, c;
	const char *k;
	const char *h;
	const char *y;
};
static bool test_opts_parse(const int argc, char *const *argv, int *idx,
struct test_opts_args *out) {
	for (;;) {
		const enum test_opts_c c = opts_next(argc, argv, idx,
			test_opts, ARRAY_LEN(test_opts));
		switch (c) {
		case to_a: out->a = true; break;
		case to_b: out->b = true; break;
		case to_no_char_flag: out->c = true; break;
		case to_k: out->k = argv[*idx]; break;
		case to_h: out->h = argv[*idx]; break;
		case to_no_char_opt: out->y = argv[*idx]; break;
		case to_done: return true;
		}
		++*idx;
	}
	return true;
}
static bool test_cmdline(const int argc, char *const *argv,
const struct test_opts_args *expect) {
	struct test_opts_args result = {0};
	int read = 0;
	test_opts_parse(argc, argv, &read, &result);
	int c = memcmp(expect, &result, sizeof(result));
	bool ok = !c;
	printf("%s\t%d\n", ok_str(ok), c);
	return ok;
}
static bool opts_tests(void) {
	test_name(__func__);
	bool kay = true;

	puts("opts parsing, really basic test");
	puts("\tmemcmp(&expect, &result)");
	const char *ay = "ay";
	const char *lmao = "lmao";
	const char *you = "you";
	const char *me = "me";
	struct {
		const char *cmdline[6];
		struct test_opts_args expect;
	} cases[] = {
		{
			.cmdline = {"-a", "-b", "-k", ay, "-h", lmao},
			.expect = {
				.a = true, .b = true,
				.k = ay, .h = lmao,
			},
		}, {
			.cmdline = {
				"--bool", "--enable", "--hey", you, "--yay", me,
			},
			.expect = {
				.b = true, .c = true,
				.h = you, .y = me,
			},
		},
	};
	for (size_t i = 0; i < ARRAY_LEN(cases); ++i) {
		kay &= test_cmdline((int)ARRAY_LEN(cases[i].cmdline),
			(char *const *)cases[i].cmdline, &cases[i].expect);
	}
	puts("");
	return kay;
}

/* wudefs tests */
static bool test_alloc_sub(const char *name,
struct wuimg * (*fn)(struct image_file *file, size_t nr),
struct image_file *file, size_t nr) {
	const size_t before = file->nr;
	const bool expect_null = (nr == 0) | (nr == SIZE_MAX);
	const size_t expect_nr = expect_null ? zumin(before, nr) : nr;

	const void *ptr = (*fn)(file, nr);
	const bool ok = (file->nr == expect_nr) & (expect_null == !ptr);
	printf("%s\t%s\t%i\t%zu\t%zu\t%zu\n", ok_str(ok), name,
		!ptr, nr, expect_nr, file->nr);
	return ok;
}
static bool wudefs_tests(void) {
	test_name(__func__);
	bool kay = true;

	puts("(re)alloc_sub_images()");
	puts("\tfn\tnull?\tattempt\texpect\talloc");
	struct image_file file = {0};
	const size_t sizes[] = {
		0, 1, 3, 1, 2, 0, 2, 2, SIZE_MAX, 4, 3,
	};
	for (size_t i = 0; i < ARRAY_LEN(sizes); ++i) {
		kay &= test_alloc_sub(i > 1 ? "realloc" : "alloc",
			i > 1 ? realloc_sub_images : alloc_sub_images,
			&file, sizes[i]);
	}
	puts("");

	puts("will image_file_free() return without crashing after that?");
	image_file_free(&file);
	puts("\tyes");
	puts("");
	return kay;
}

/* raster/ tests */
union test_unpack_mem {
	uint8_t m8[8];
	uint16_t m16[4];
	float mf[2];
};
struct test_unpack_params {
	uint8_t bitdepth;
	enum pix_attr attr:8;
	enum endianness bit:8;
	enum unpack_op op:8;
	union test_unpack_mem e;
};
static bool cmp_unpack(const struct test_unpack_params *p,
const union test_unpack_mem *e, const uint8_t *blob, enum pix_attr attr) {
	size_t elems = ARRAY_LEN(e->m8);
	if (p->bitdepth > 32) { // pack double
		elems = ARRAY_LEN(e->mf);
	} else if (p->bitdepth > 8) {
		elems = ARRAY_LEN(e->m16);
	}
	const size_t stride = unpack_stride(elems, p->bitdepth, attr, p->op, NULL);
	union test_unpack_mem out = {0};
	unpack_strip(&out, blob, elems, p->bitdepth, attr, p->bit, p->op, NULL);

	const bool ok = !memcmp(&out, e->m8, sizeof(*e)) && stride;
	printf("%s\t%02i/%s/%s/%s\t%zu\t",
		ok_str(ok), p->bitdepth,
		p->bit == big_endian ? "ms" : "ls", unpack_op_str(p->op),
		pix_attr_str(attr), stride);
	print_blob(e->m8, sizeof(*e), '\t');
	print_blob(&out, sizeof(out), '\n');
	return ok;
}
static void synth_case(union test_unpack_mem *e, const union test_unpack_mem *p,
uint8_t depth, uint32_t xor) {
	if (depth > 8) {
		for (size_t i = 0; i < ARRAY_LEN(e->m16); ++i) {
			e->m16[i] = (uint16_t)(p->m16[i] ^ xor);
		}
	} else {
		for (size_t i = 0; i < ARRAY_LEN(e->m8); ++i) {
			e->m8[i] = (uint8_t)(p->m8[i] ^ xor);
		}
	}
}
static bool test_unpack_synth(const struct test_unpack_params *p,
const void *blob) {
	const uint32_t md = u32min(p->bitdepth, 16);
	union test_unpack_mem e_inv;
	synth_case(&e_inv, &p->e, p->bitdepth, bit_set32(md));
	union test_unpack_mem e_sig;
	synth_case(&e_sig, &p->e, p->bitdepth, 1 << (md - 1));
	return cmp_unpack(p, &p->e, blob, pix_normal)
		& cmp_unpack(p, &e_sig, blob, pix_signed);
}
static bool test_unpack_single(const struct test_unpack_params *p,
const void *blob) {
	return cmp_unpack(p, &p->e, blob, p->attr);
}
static bool unpack_tests(void) {
	test_name(__func__);
	bool kay = true;

	puts("strip unpacking");
	puts("\tdepth/order/op/attr\tstride\texpected\toutput");
	const union {
		uint8_t mem[16];
		uint64_t aligner;
	} data = {
		{
			0xba, 0x98, 0x76, 0x54, 0x32, 0x10, 0xfe, 0xdc,
			0xba, 0x98, 0x76, 0x54, 0x32, 0x10, 0xfe, 0xdc,
		},
	};
	// See unpack_array_gen.py
	const struct test_unpack_params up[] = {
		{1, pix_normal, big_endian, op_repack,
			.e.m8 = {1,0,1,1, 1,0,1,0}},
		{2, pix_normal, big_endian, op_repack,
			.e.m8 = {2,3, 2,2, 2,1, 2,0}},
		{3, pix_normal, big_endian, op_repack,
			.e.m8 = {5,6,5,1, 4,1,6,6}},
		{4, pix_normal, big_endian, op_repack,
			.e.m8 = {0xb, 0xa, 0x9, 0x8, 0x7, 0x6, 0x5, 0x4}},
		{5, pix_normal, big_endian, op_repack,
			.e.m8 = {0x17, 0x0a, 0x0c, 0x07, 0x0c, 0x15, 0x01, 0x12}},
		{6, pix_normal, big_endian, op_repack,
			.e.m8 = {0x2e, 0x29, 0x21, 0x36, 0x15, 0x03, 0x08, 0x10}},
		{7, pix_normal, big_endian, op_repack,
			.e.m8 = {0x5d, 0x26, 0x0e, 0x65, 0x21, 0x48, 0x21, 0x7e}},

		{9, pix_normal, big_endian, op_repack,
			.e.m16 = {0x0175, 0x0061, 0x01b2, 0x0143}},
		{10, pix_normal, big_endian, op_repack,
			.e.m16 = {0x02ea, 0x0187, 0x0195, 0x0032}},
		{11, pix_normal, big_endian, op_repack,
			.e.m16 = {0x05d4, 0x061d, 0x04a8, 0x0321}},
		{12, pix_normal, big_endian, op_repack,
			.e.m16 = {0x0ba9, 0x0876, 0x0543, 0x0210}},
		{13, pix_normal, big_endian, op_repack,
			.e.m16 = {0x1753, 0x01d9, 0x0a19, 0x010f}},
		{14, pix_normal, big_endian, op_repack,
			.e.m16 = {0x2ea6, 0x0765, 0x10c8, 0x10fe}},
		{15, pix_normal, big_endian, op_repack,
			.e.m16 = {0x5d4c, 0x1d95, 0x0642, 0x0fed}},
		// 16-, 32-, and 64-bits assumes native endianness

		{20, pix_normal, big_endian, op_repack,
			.e.m16 = {0xba98, 0x6543, 0x10fe, 0xcba9}},
		{24, pix_normal, big_endian, op_repack,
			.e.m16 = {0xba98, 0x5432, 0xfedc, 0x9876}},
		{28, pix_normal, big_endian, op_repack,
			.e.m16 = {0xba98, 0x4321, 0xdcba, 0x6543}},
	};
	for (size_t i = 0; i < ARRAY_LEN(up); ++i) {
		kay &= test_unpack_synth(up + i, data.mem);
	}
	const struct test_unpack_params single[] = {
		{1, pix_normal, little_endian, op_repack,
			.e.m8 = {0,1,0,1, 1,1,0,1}},
		{4, pix_normal, little_endian, op_repack,
			.e.m8 = {0xa, 0xb, 0x8, 0x9, 0x6, 0x7, 0x4, 0x5}},
		{8, pix_signed, big_endian, op_repack,
			.e.m8 = {0x3a, 0x18, 0xf6, 0xd4, 0xb2, 0x90, 0x7e, 0x5c}},
	};
	for (size_t i = 0; i < ARRAY_LEN(single); ++i) {
		kay &= test_unpack_single(single + i, data.mem);
	}
	const double mem[4] = {1, 2, 3, 4};
	const struct test_unpack_params floats[] = {
		{64, pix_float, big_endian, op_repack,
			.e.mf = {1,2}},
	};
	for (size_t i = 0; i < ARRAY_LEN(floats); ++i) {
		kay &= test_unpack_single(floats + i, mem);
	}
	puts("");
	return kay;
}

struct pix_layout_names {
	enum pix_layout l:8;
	const char name[5];
};
static bool swz_test(const struct pix_layout_names name) {
	char linear[sizeof(name.name)] = "    ";
	char swz[sizeof(name.name)] = "    ";
	pix_layout_swizzle(linear, name.name, 1, sizeof(name.name) - 1,
		pix_rgba, name.l);
	pix_layout_swizzle(swz, linear, 1, sizeof(name.name) - 1,
		name.l, pix_rgba);
	const bool ok = !strcmp(swz, name.name);
	printf("%s\t%s\t%s\t%s\n", ok_str(ok), name.name, linear, swz);
	return ok;
}
static bool layout_name_test(const struct pix_layout_names name) {
	uint8_t tgt[sizeof(name.name)] = "    ";
	pix_layout_repr(tgt, name.l);
	tgt[4] = 0;
	const bool ok = !strcmp(name.name, (char *)tgt);
	printf("%s\t%s\t%s\n", ok_str(ok), name.name, (char *)tgt);
	return ok;
}
static bool pix_layout_tests(void) {
	static const struct pix_layout_names names[] = {
		{pix_gray, "rg  "}, // More precisely GrayAlpha
		{pix_rgba, "rgba"},
		{pix_grba, "grba"},
		{pix_argb, "argb"},
		{pix_gbra, "gbra"},
		{pix_bgra, "bgra"},
		{pix_abgr, "abgr"},
		{PIX_LAYOUT_PACK(0, 2, 1, 3), "rbga"},
		{PIX_LAYOUT_PACK(3, 1, 0, 2), "bgar"},

		/* The bizarre packings in lib/utahrle.c */
		// AlphaGray
		{PIX_LAYOUT_PACK(1, 1, 1, 0), "gr  "},
		// GrayAlpha but on a palette. This one is really pushing it
		{PIX_LAYOUT_PACK(0, 0, 0, 3), "r  g"},
	};
	test_name(__func__);
	bool kay = true;

	puts("Does swizzling to RGBA and back work?");
	puts("\torig\tlinear\tback");
	for (size_t i = 0; i < ARRAY_LEN(names); ++i) {
		kay &= swz_test(names[i]);
	}
	puts("");

	puts("Do enum values match their names (is the repr function working)?");
	puts("\tenum\trepr");
	for (size_t i = 0; i < ARRAY_LEN(names); ++i) {
		kay &= layout_name_test(names[i]);
	}
	puts("");
	return kay;
}

struct palette_test_rgb8 {
	uint8_t bits;
	struct pix_rgba8 expect[2];
};
static bool run_rgb8_test(const uint8_t *blob,
const struct palette_test_rgb8 *p) {
	const size_t nmemb = ARRAY_LEN(p->expect);
	struct palette pal;
	if (p->bits) {
		palette_from_rgb8_bitrange(&pal, blob, nmemb, p->bits);
	} else {
		palette_from_rgb8(&pal, blob, nmemb);
	}
	const size_t bytes = sizeof(p->expect);
	const bool ok = !memcmp(pal.color, p->expect, bytes);
	printf("%s\t%d\t", ok_str(ok), p->bits);
	print_blob(blob, nmemb*3, '\t');
	print_blob(p->expect, bytes, '\t');
	print_blob(pal.color, bytes, '\n');
	return ok;
}
static bool palette_tests(void) {
	test_name(__func__);
	bool kay = true;

	puts("Create a palette from rgb8 data");
	puts("\tbits\tmemory input\texpected\tresult");
	const struct palette_test_rgb8 pp[] = {
		{5, {{0x11, 0x22, 0x33, 0x1f}, {0x44, 0x55, 0x66, 0x1f}}},
		{8, {{0x22, 0x33, 0x44, 0xff}, {0x55, 0x66, 0x77, 0xff}}},
		{0, {{0x33, 0x44, 0x55, 0xff}, {0x66, 0x77, 0x88, 0xff}}},
	};
	for (size_t i = 0; i < ARRAY_LEN(pp); ++i) {
		kay &= run_rgb8_test(NUM_SEQ + i, pp + i);
	}
	puts("");
	return kay;
}

/* misc/ tests */
struct time_test_params {
	time_t unix;
	int year, month, day, hour, min, sec;
};
static bool to_epoch_test(const struct time_test_params *p) {
	const time_t r = utc_to_epoch(p->year, p->month, p->day,
		p->hour, p->min, p->sec);
	const bool ok = p->unix == r;
	printf("%s\t"
		"%i\t%i\t%i\t"
		"%i\t%i\t%i\t"
		"%ju\t%ju\n",
		ok_str(ok),
		p->year, p->month, p->day,
		p->hour, p->min, p->sec,
		(uintmax_t)p->unix, (uintmax_t)r);
	return ok;
}
#define TEST_RFC3339_BUFSIZ (4+1+2+1+2+1 +2+1+2+1+2 +1 +1)
static bool test_rfc3339(const struct time_test_params *p, FILE *out) {
	char expect[TEST_RFC3339_BUFSIZ];
	const int e = snprintf(expect, sizeof(expect),
		"%i-%.2i-%.2i %.2i:%.2i:%.2iZ",
		p->year, p->month, p->day, p->hour, p->min, p->sec);

	fseek(out, 0, SEEK_SET);
	rfc3339_format(p->unix, out);
	fflush(out);
	fseek(out, 0, SEEK_SET);
	char result[TEST_RFC3339_BUFSIZ];
	const size_t r = fread(result, 1, sizeof(result)-1, out);
	result[r] = 0;

	const bool ok = (e >= 0) && !strcmp(expect, result);
	printf("%s\t%ju\t%s\t%s\n",
		ok_str(ok), (uintmax_t)p->unix, expect, result);
	return ok;
}
static bool time_tests(void) {
	test_name(__func__);
	bool kay = true;

	puts("Is utc_to_epoch() not broken-down? feat. time_from_dos()");
	puts("\tyear\tmonth\tday\thour\tminute\tsecond\tunix\tresult");
	const struct time_test_params dates[] = {
		{0, 1970, 1, 1, 0, 0, 0},
		{time_from_dos(0), 1980, 1, 1, 0, 0, 0},
		{999999999, 2001, 9, 9, 1, 46, 39},
		{0x7fffffff, 2038, 1, 19, 3, 14, 7},
	};
	for (size_t i = 0; i < ARRAY_LEN(dates); ++i) {
		kay &= to_epoch_test(dates + i);
	}
	puts("");

	puts("rfc3339_format()");
	puts("\tunix\texpect\tresult");
	FILE *tmp = fmemopen(NULL, TEST_RFC3339_BUFSIZ, "r+");
	assert(tmp && "fmemopen() failure");
	for (size_t i = 0; i < ARRAY_LEN(dates); ++i) {
		kay &= test_rfc3339(dates + i, tmp);
	}
	fclose(tmp);
	puts("");
	return kay;
}

static bool cmpc_mp(const char *fn, int val, int expect) {
	const bool ok = val == expect;
	printf("%s\t%s\t0x%x\t0x%x\n", ok_str(ok), fn,
		(unsigned)expect, (unsigned)val);
	return ok;
}
static bool cmpz_mp(const char *fn, size_t val, size_t expect) {
	return cmpc_mp(fn, (int)val, (int)expect);
}
static bool cmpstr_mp(const char *fn, const struct wuptr w, const char *str) {
	const bool ok = wuptr_eq_str(w, str);
	printf("%s\t%s\t\"", ok_str(ok), fn);
	wuptr_print(w, stdout);
	printf("\"\t\"%s\"\n", str);
	return ok;
}
static bool cmpuunsafe_mp(const char *fn, struct mparser *mp, uintmax_t expect) {
	uintmax_t val = 789;
	size_t r = mp_scan_uint_unsafe(mp, &val);
	(void)r;
	const bool ok = val == expect;
	printf("%s\t%s\t%ju\t%ju\n", ok_str(ok), fn, expect, val);
	return ok;
}
static bool cmpu_mp(const char *fn,
size_t (*scan)(struct mparser *mp, size_t digits, uintmax_t *val),
struct mparser *mp, size_t digits, uintmax_t expect) {
	uintmax_t val = 789;
	size_t r = (*scan)(mp, digits, &val);
	(void)r;
	const bool ok = val == expect;
	printf("%s\t%s\t%ju\t%ju\n", ok_str(ok), fn, expect, val);
	return ok;
}
static bool cmpi_mp(const char *fn,
size_t (*scan)(struct mparser *mp, size_t digits, intmax_t *val),
struct mparser *mp, size_t digits, intmax_t expect) {
	intmax_t val = -789;
	size_t r = (*scan)(mp, digits, &val);
	(void)r;
	const bool ok = val == expect;
	printf("%s\t%s\t%ji\t%ji\n", ok_str(ok), fn, expect, val);
	return ok;
}
static bool test_mp_next(void) {
	const char str[] = "a b c \t\r\n d  straße"
		" 00  0123456789 0123456789-65537-65538 0xf1 0xf2_:*\\ "
		"0xabcdef0xABCDEF-0x10x000002"
		"   \r\n\f e3 f g";
	struct mparser mp = mp_wuptr(wuptr_str(str));
	struct mparser mp2 = mp_mem(strlen(str), str);

	return !memcmp(&mp, &mp2, sizeof(mp))
		& cmpc_mp("cur_char", mp_cur_char(&mp), 'a')
		& cmpc_mp("next_char", mp_next_char(&mp), 'a')
		& cmpc_mp("next_nonblank", mp_next_nonblank(&mp), 'b')
		& cmpc_mp("next_nonspace", mp_next_nonspace(&mp), 'c')
		& cmpc_mp("next_nonblank", mp_next_nonblank(&mp), '\r')
		& cmpc_mp("next_nonspace", mp_next_nonspace(&mp), 'd')
		& cmpz_mp("skip_blank", mp_skip_blank(&mp), 2)
		& cmpstr_mp("next_word", mp_next_word(&mp), "straße")

		& cmpz_mp("skip_space", mp_skip_space(&mp), 1)
		& cmpu_mp("scan_uint", mp_scan_uint, &mp, 2, 0)
		& cmpz_mp("skip_space", mp_skip_space(&mp), 2)
		& cmpz_mp("skip_space", mp_skip_space(&mp), 0)
		& cmpu_mp("scan_uint", mp_scan_uint, &mp, 0, 0)
		& cmpu_mp("scan_uint", mp_scan_uint, &mp, SIZE_MAX, 123456789)
		& cmpu_mp("scan_uint", mp_scan_uint, &mp, SIZE_MAX, 0)
		& cmpz_mp("skip_blank", mp_skip_blank(&mp), 1)
		& cmpuunsafe_mp("scan_uint_unsafe", &mp, 123456789)
		& cmpuunsafe_mp("scan_uint_unsafe", &mp, 0)

		& cmpz_mp("skip_blank", mp_skip_blank(&mp), 0)
		& cmpu_mp("scan_uint", mp_scan_uint, &mp, SIZE_MAX, 0)
		& cmpi_mp("scan_int", mp_scan_int, &mp, 0, 0)
		& cmpi_mp("scan_int", mp_scan_int, &mp, 1, 6)
		& cmpi_mp("scan_int", mp_scan_int, &mp, SIZE_MAX, 5537)

		& cmpi_mp("scan_int", mp_scan_int, &mp, SIZE_MAX, -65538)
		& cmpi_mp("scan_int", mp_scan_int, &mp, SIZE_MAX, 0)

		& cmpc_mp("next_char", mp_next_char(&mp), ' ')
		& cmpu_mp("scan_xint", mp_scan_xint, &mp, 1, 0xf)
		& cmpu_mp("scan_xint", mp_scan_xint, &mp, 1, 0)
		& cmpu_mp("scan_xint", mp_scan_xint, &mp, SIZE_MAX, 0)
		& cmpc_mp("next_char", mp_next_char(&mp), '1')

		& cmpc_mp("next_char", mp_next_char(&mp), ' ')
		& cmpu_mp("scan_xint", mp_scan_xint, &mp, 0, 0)
		& cmpu_mp("scan_xint", mp_scan_xint, &mp, 2, 0)
		& cmpstr_mp("next_word", mp_next_word(&mp), "f2_:*\\")

		& cmpc_mp("next_char", mp_next_char(&mp), ' ')
		& cmpu_mp("scan_xint", mp_scan_xint, &mp, 6, 0xabcdef)
		& cmpu_mp("scan_xint", mp_scan_xint, &mp, SIZE_MAX, 0xabcdef)

		& cmpi_mp("scan_int", mp_scan_int, &mp, SIZE_MAX, -0)
		& cmpu_mp("scan_uint", mp_scan_uint, &mp, SIZE_MAX, 0)
		& cmpu_mp("scan_xint", mp_scan_xint, &mp, SIZE_MAX, 0)
		& cmpc_mp("next_char", mp_next_char(&mp), 'x')

		& cmpu_mp("scan_anyuint", mp_scan_anyuint, &mp, 0, 0)
		& cmpu_mp("scan_anyuint", mp_scan_anyuint, &mp, 1, 1)
		& cmpu_mp("scan_anyuint", mp_scan_anyuint, &mp, 6, 2)
		& cmpu_mp("scan_anyuint", mp_scan_anyuint, &mp, SIZE_MAX, 0)

		& cmpc_mp("cur_char", mp_cur_char(&mp), ' ')

		& cmpz_mp("skip_blank", mp_skip_blank(&mp), 3)
		& cmpz_mp("skip_space_unsafe", mp_skip_space_unsafe(&mp), 4)
		& cmpc_mp("cur_char", mp_cur_char(&mp), 'e')
		& cmpc_mp("skip_until", mp_skip_until(&mp, 'f'), true)
		& cmpc_mp("cur_char", mp_cur_char(&mp), ' ')
		& cmpc_mp("skip_until", mp_skip_until(&mp, 'h'), false)
		& cmpc_mp("skip_until", mp_skip_until(&mp, 'h'), false)
		& cmpz_mp("-end pos-", mp.pos, mp.len);
}
static bool cmpptr_mp(const char *fn, const void *ptr, const char *base,
const size_t pos) {
	const bool ok = ptr == base + pos;
	printf("%s\t%s\t%zu\t%ti\n", ok_str(ok), fn, pos, ((char *)ptr - base));
	return ok;
}
static bool cmpwuptr_mp(const char *fn, struct wuptr ptr, const void *expect,
const size_t len) {
	const struct wuptr e = wuptr_mem(expect, len);
	const bool ok = !memcmp(&ptr, &e, sizeof(ptr));
	printf("%s\t%s\t%zu\t%zu\n", ok_str(ok), fn, len, ptr.len);
	return ok;
}
static bool cmpzwuptr_mp(const char *fn, struct wuptr ptr) {
	const bool ok = ptr.len == 0;
	printf("%s\t%s\t0\t%zu\n", ok_str(ok), fn, ptr.len);
	return ok;
}
static bool cmpnull_mp(const char *fn, const void *ptr) {
	const bool ok = !ptr;
	printf("%s\t%s\t%p\t%p\n", ok_str(ok), fn, NULL, ptr);
	return ok;
}
static bool cmpupto_mp(const char *fn, struct mparser *mp, const char c,
const void *ptr, const size_t len, const bool succeed) {
	struct wuptr out = {.ptr = (void *)-1, .len = 765};
	const bool no_eof = mp_upto(mp, &out, c);
	const struct wuptr expect = wuptr_mem(ptr, len);
	const bool ok = !memcmp(&out, &expect, sizeof(out)) & (no_eof == succeed);
	printf("%s\t%s\t%zu\t%zu\n", ok_str(ok), fn, len, out.len);
	return ok;
}
static bool cmpseek_mp(struct mparser *mp, size_t where) {
	const bool ok = mp_seek_set(mp, where) == where;
	printf("%s\tseek_set\t%zu\t%zu\n", ok_str(ok), where, mp->pos);
	return ok;
}
static bool test_mp_slice(void) {
	const char str[] = "0123456789";
	const size_t len = strlen(str);
	struct mparser mp = mp_mem(len, str);

	return cmpptr_mp("slice", mp_slice(&mp, 1), str, 0)
		& cmpnull_mp("!slice", mp_slice(&mp, len))
		& cmpnull_mp("!slice", mp_slice(&mp, SIZE_MAX))
		& cmpupto_mp("upto", &mp, '4', str+1, 3, true)
		& cmpupto_mp("upto", &mp, '9', str+5, 4, true)
		& cmpupto_mp("upto", &mp, 'y', str+len, 0, false)
		& cmpseek_mp(&mp, 1)

		& cmpwuptr_mp("avail", mp_avail(&mp, 1), str+1, 1)
		& cmpwuptr_mp("avail", mp_avail(&mp, SIZE_MAX), str+2, 8)
		& cmpptr_mp("slice", mp_slice(&mp, 0), str, len)
		& cmpnull_mp("!slice", mp_slice(&mp, 1))

		& cmpptr_mp("slice_at", mp_slice_at(&mp, 1, 1), str, 1)
		& cmpnull_mp("!slice_at", mp_slice_at(&mp, 1, len))
		& cmpnull_mp("!slice_at", mp_slice_at(&mp, 1, SIZE_MAX))
		& cmpnull_mp("!slice_at", mp_slice_at(&mp, len, len))
		& cmpnull_mp("!slice_at", mp_slice_at(&mp, SIZE_MAX, len))
		& cmpwuptr_mp("avail_at", mp_avail_at(&mp, 2, len), str+2, 8)
		& cmpwuptr_mp("avail_at", mp_avail_at(&mp, 2, SIZE_MAX), str+2, 8)
		& cmpzwuptr_mp("avail_at", mp_avail_at(&mp, len, SIZE_MAX))
		& cmpseek_mp(&mp, 0)

		& cmpwuptr_mp("remaining", mp_remaining(&mp), str, len)
		& cmpwuptr_mp("remaining", mp_remaining(&mp), str + len, 0);
}
static bool test_seek_set(struct mparser *mp, size_t seek, size_t expect) {
	const size_t n = mp_seek_set(mp, seek);
	const bool ok = n == expect;
	printf("%s\tseek_set\t%zu\t%zu\t%zu\n", ok_str(ok), seek, expect, n);
	return ok;
}
static bool test_seek_cur(struct mparser *mp, ptrdiff_t seek, size_t expect) {
	const size_t n = mp_seek_cur(mp, seek);
	const bool ok = n == expect;
	printf("%s\tseek_cur\t%ti\t%zu\t%zu\n", ok_str(ok), seek, expect, n);
	return ok;
}
static bool test_mp_seek(void) {
	const char str[] = "qwerty";
	const size_t len = strlen(str);
	struct mparser mp = mp_mem(len, str);
	bool ok = test_seek_set(&mp, 0, 0)
		& test_seek_set(&mp, 1, 1)
		& test_seek_set(&mp, 0, 0)
		& test_seek_set(&mp, len, len)
		& test_seek_set(&mp, len*2, len)
		& test_seek_set(&mp, SIZE_MAX, len)
		& test_seek_cur(&mp, -1, len-1)
		& test_seek_cur(&mp, 0, len-1)
		& test_seek_cur(&mp, 1, len)
		& test_seek_cur(&mp, -(ptrdiff_t)len, 0)
		& test_seek_cur(&mp, PTRDIFF_MAX, len)
		& test_seek_cur(&mp, PTRDIFF_MIN, 0);
	mp.pos = SIZE_MAX;
	return ok & test_seek_cur(&mp, PTRDIFF_MAX, len);
}

static bool mparser_tests(void) {
	test_name(__func__);
	bool kay = true;

	puts("mp_next_*, mp_skip_*, mp_scan_*");
	puts("\tfn\texpect\tresult");
	kay &= test_mp_next();
	puts("");

	puts("mp_slice, mp_avail, mp_remaining");
	puts("\tfn\texpect\tresult");
	kay &= test_mp_slice();
	puts("");

	puts("mp_seek_*");
	puts("\tfn\tseek\texpect\tresult");
	kay &= test_mp_seek();
	puts("");
	return kay;
}

struct test_twice_u {
	const char name[7];
	uint8_t expect;
	uintmax_t a, b;
};
struct test_twice_i {
	const char name[7];
	int8_t expect;
	intmax_t a, b;
};
static bool minmax_u_test(const struct test_twice_u *t, uint8_t x, uint8_t y) {
	const bool ok = (t->expect == t->a) & (t->expect == t->b);
	printf("%s\t%s\t"
		"%u\t%u\t"
		"%u\t%ju\t%ju\n",
		ok_str(ok), t->name,
		x, y,
		t->expect, t->a, t->b);
	return ok;
}
static bool minmax_i_test(const struct test_twice_i *t, int8_t x, int8_t y) {
	const bool ok = (t->expect == t->a) & (t->expect == t->b);
	printf("%s\t%s\t"
		"%+i\t%+i\t"
		"%+i\t%+ji\t%+ji\n",
		ok_str(ok), t->name,
		x, y,
		t->expect, t->a, t->b);
	return ok;
}
static bool ceildiv_test(const char *name, uint8_t x, uint8_t y,
uint8_t expect) {
	const uintmax_t r = zuceildiv(x, y);
	const bool ok = (r == expect);
	printf("%s\t%s\t"
		"%i\t%i\t%i\t%ju\n",
		ok_str(ok), name,
		x, y, expect, r);
	return ok;
}
static bool ulog2_test(const char *name, uint8_t val, uint8_t expect,
uintmax_t x) {
	const bool ok = expect == x;
	printf("%s\t%s\t%i\t%i\t%ju\n", ok_str(ok), name, val, expect, x);
	return ok;
}
static bool test_iclamp(const int8_t cla[static 4]) {
	int8_t n = cla[0], x = cla[1], y = cla[2], exp = cla[3];
	const int result = iclamp(n, x, y);
	const bool ok = exp == result;
	printf("%s\t%+i\t%+i\t%+i\t%+i\t%+i\n", ok_str(ok),
		n, x, y, exp, result);
	return ok;
}
static bool math_tests(void) {
	test_name(__func__);
	bool kay = true;

	puts("Do the min/max functions work? Also with swapped arguments?");
	puts("\tfn\tx\ty\texpect\tfn(x,y)\tfn(y,x)");
	const uint8_t ulo = 9;
	const uint8_t uhi = 12;
	const struct test_twice_u up[] = {
		{  "umin", ulo,   umin(ulo, uhi),   umin(uhi, ulo)},
		{ "zumin", ulo,  zumin(ulo, uhi),  zumin(uhi, ulo)},
		{"u32min", ulo, u32min(ulo, uhi), u32min(uhi, ulo)},
		{  "umax", uhi,   umax(ulo, uhi),   umax(uhi, ulo)},
		{ "zumax", uhi,  zumax(ulo, uhi),  zumax(uhi, ulo)},
		{"u32max", uhi, u32max(ulo, uhi), u32max(uhi, ulo)},
	};
	for (size_t i = 0; i < ARRAY_LEN(up); ++i) {
		kay &= minmax_u_test(up + i, ulo, uhi);
	}
	const int8_t ilo = -1;
	const int8_t ihi = 108;
	const struct test_twice_i ip[] = {
		{"imin", ilo, imin(ilo, ihi), imin(ihi, ilo)},
		{"lmin", ilo, lmin(ilo, ihi), lmin(ihi, ilo)},
		{"imax", ihi, imax(ilo, ihi), imax(ihi, ilo)},
		{"lmax", ihi, lmax(ilo, ihi), lmax(ihi, ilo)},
	};
	for (size_t i = 0; i < ARRAY_LEN(ip); ++i) {
		kay &= minmax_i_test(ip + i, ilo, ihi);
	}
	puts("");

	puts("iclamp()");
	puts("\tn\tmin\tmax\texpect\tresult");
	const int8_t cla[][4] = {
		{3, -8, 8, 3},
		{9, -8, 8, 8},
		{-9, -8, 8, -8},
		{3, 0, 0, 0},
		{-1, -1, -1, -1},
	};
	for (size_t i = 0; i < ARRAY_LEN(cla); ++i) {
		kay &= test_iclamp(cla[i]);
	}
	puts("");

	puts("fclampf()");
	kay &= test_fclampf_fn(fclampf);
	puts("");

	puts("Integer positive mod");
	puts("\tfn\tx\ty\texpect\tfn(x,y)\tfn(x+y,y)");
	const int8_t xm = -13;
	const int8_t ym = 7;
	const struct test_twice_i modp[] = {
		{"imod", 1, imod(xm, ym), imod(xm + ym, ym)},
		{"lmod", 1, lmod(xm, ym), lmod(xm + ym, ym)},
	};
	for (size_t i = 0; i < ARRAY_LEN(modp); ++i) {
		kay &= minmax_i_test(modp + i, xm, ym);
	}
	puts("");

	puts("Ceiling division");
	puts("\tfn\tx\ty\texpect\tfn(x, y)");
	const struct {
		uint8_t expect;
		uint8_t x, y;
	} cdp[] = {
		{0, 0, 1}, // Check for underflow
		{6, 59, 10},
		{6, 60, 10},
		{7, 61, 10},
	};
	for (size_t i = 0; i < ARRAY_LEN(cdp); ++i) {
		kay &= ceildiv_test("zuceil", cdp[i].x, cdp[i].y, cdp[i].expect);
	}
	puts("");

	puts("Integer log2 functions");
	puts("\tfn\tx\texpect\tfn(x)");
	const struct {
		uint8_t val, expect;
	} logp[] = {
		// 0 is UB
		{1, 0},
		{127, 6},
		{128, 7},
		{129, 7},
	};
	for (size_t i = 0; i < ARRAY_LEN(logp); ++i) {
		const uint8_t val = logp[i].val;
		const uint8_t expect = logp[i].expect;
		kay &= ulog2_test("ulog2", val, expect, ulog2(val));
		kay &= ulog2_test("zulog2", val, expect, zulog2(val));
	}
	puts("");
	return kay;
}

typedef uint32_t (*end_fn_t)(const void *buf);
typedef uint32_t (*end_enum_fn_t)(const void *buf, enum endianness e);
struct endian_group {
	end_fn_t b;
	end_fn_t l;
	end_enum_fn_t e;
};
struct endian_params {
	uint32_t bits;
	uint32_t expect_b;
	uint32_t expect_l;
	struct endian_group end;
	struct endian_group buf;
};
static uint16_t mem_to_u16(const void *buf) {
	uint16_t v;
	memcpy(&v, buf, sizeof(v));
	return v;
}
static uint32_t mem_to_u32(const void *buf) {
	uint32_t v;
	memcpy(&v, buf, sizeof(v));
	return v;
}
static uint32_t end16(const void *buf, enum endianness e) {
	return endian16(mem_to_u16(buf), e);
}
static uint32_t end16b(const void *buf) {
	return endian16b(mem_to_u16(buf));
}
static uint32_t end16l(const void *buf) {
	return endian16l(mem_to_u16(buf));
}
static uint32_t end32(const void *buf, enum endianness e) {
	return endian32(mem_to_u32(buf), e);
}
static uint32_t end32b(const void *buf) {
	return endian32b(mem_to_u32(buf));
}
static uint32_t end32l(const void *buf) {
	return endian32l(mem_to_u32(buf));
}
static uint32_t buf_end16(const void *buf, enum endianness e) {
	return buf_endian16(buf, e);
}
static uint32_t buf_end16b(const void *buf) {
	return buf_endian16b(buf);
}
static uint32_t buf_end16l(const void *buf) {
	return buf_endian16l(buf);
}
static char end_chr(enum endianness e) {
	return e == big_endian ? 'b' : 'l';
}
static bool end_test(const struct endian_params *p, const uint8_t *blob,
const char *prefix, const enum endianness end, const struct endian_group *fn) {
	const uint32_t e = (end == big_endian ? p->expect_b : p->expect_l);
	const uint32_t v1 = (end == big_endian ? fn->b : fn->l)(blob);
	const uint32_t v2 = fn->e(blob, end);

	/* Apply the conversion again, the memory order must match the
	 * original blob */
	const uint32_t inv = p->bits == 16
		? endian16((uint16_t)v1, end)
		: endian32(v1, end);
	uint8_t minv[sizeof(inv)];
	memcpy(minv, &inv, sizeof(inv));

	const bool ok = (e == v1) & (e == v2) & !memcmp(blob, minv, p->bits/8);
	printf("%s\t%s%u%c\t",
		ok_str(ok), prefix, p->bits, end_chr(end));
	print_blob(blob, 4, '\t');
	printf(FULL_X32 "\t" FULL_X32 "\t" FULL_X32 "\t",
		e, v1, v2);
	print_blob(minv, 4, '\n');
	return ok;
}
static bool end_test_battery(const struct endian_params *p, const uint8_t *blob) {
	return end_test(p, blob, "", big_endian, &p->end)
		& end_test(p, blob, "", little_endian, &p->end)
		& end_test(p, blob, "buf", big_endian, &p->buf)
		& end_test(p, blob, "buf", little_endian, &p->buf);
}
static bool buf_end24_test(const uint8_t *blob, enum endianness end,
uint32_t expect) {
	uint32_t v = buf_endian24(blob, end);
	const bool ok = v == expect;
	printf("%s\tbuf24%c\t", ok_str(ok), end_chr(end));
	print_blob(blob, 4, '\t');
	printf(FULL_X32 "\t" FULL_X32 "\n", expect, v);
	return ok;
}
static bool endf32_val_test(const uint8_t *blob, const char *prefix,
const enum endianness end, const float expect, float v1, float v2) {
	const bool ok = (expect == v1) & (v1 == v2);
	printf("%s\t%sf32%c\t",
		ok_str(ok), prefix, end_chr(end));
	print_blob(blob, 4, '\t');
	printf("%.12f\t%.12f\t%.12f\n", expect, v1, v2);
	return ok;
}
static bool endf32_test(const void *blob, const enum endianness e,
const float expect) {
	const uint32_t f32 = mem_to_u32(blob);
	return endf32_val_test(blob, "", e, expect,
			(e == big_endian ? endianf32b : endianf32l)(f32),
			endianf32(f32, e))
		& endf32_val_test(blob, "buf", e, expect,
			(e == big_endian ? buf_endianf32b : buf_endianf32l)(blob),
			buf_endianf32(blob, e));
}
static bool which_end_test(const uint8_t *blob) {
	const enum endianness cpu = which_end();
	const uint32_t val = buf_endian32(blob, cpu);
	const bool ok = !memcmp(&val, blob, sizeof(val));

	uint8_t mval[sizeof(val)];
	memcpy(mval, &val, sizeof(val));
	printf("%s\t", ok_str(ok));
	print_blob(blob, 4, '\t');
	print_blob(mval, 4, '\t');
	puts(endian_str(cpu));
	return ok;
}
static bool endian_tests(void) {
	test_name(__func__);
	bool kay = true;

	puts("Do endian functions work? Are they revertible?");
	puts("\tfn\tinput memory\texpected val"
		"\tfn(input)\tfn(input, enum)\tfn(fn(input))");
	static const struct endian_params params[] = {
		{16, 0x1122, 0x2211,
			{end16b, end16l, end16},
			{buf_end16b, buf_end16l, buf_end16}},
		{32, 0x11223344, 0x44332211,
			{end32b, end32l, end32},
			{buf_endian32b, buf_endian32l, buf_endian32}},
	};
	for (size_t i = 0; i < ARRAY_LEN(params); ++i) {
		kay &= end_test_battery(params + i, NUM_SEQ);
	}
	puts("");

	puts("The unexpected buf_endian24()");
	puts("\tfn\tinput memory\texpected val\tfn(input)");
	kay &= buf_end24_test(NUM_SEQ, big_endian, 0x112233);
	kay &= buf_end24_test(NUM_SEQ, little_endian, 0x332211);
	puts("");

	puts("Does endianf32() work?");
	puts("\tfn\tinput memory\texpected val\tfn(input)\tfn(input, enum)");
	const float tau = (float)(M_PI * 2.0);
	const uint32_t tau_b = buf_endian32b(&tau);
	const uint32_t tau_l = buf_endian32l(&tau);
	kay &= endf32_test(&tau_b, big_endian, tau);
	kay &= endf32_test(&tau_l, little_endian, tau);
	puts("");

	puts("Does which_end() tell the CPU endianness?"
		" An int must have the same memory layout as the source");
	puts("\tinput memory\toutput memory\tcpu endianess");
	kay &= which_end_test(NUM_SEQ);
	puts("");
	return kay;
}

typedef void * (*small_alloc_fn_t)(size_t nmemb, size_t size);
struct common_alloc_params {
	const char name[8];
	small_alloc_fn_t fn;
};
static void * small_realloc_from_nothing(size_t nmemb, size_t size) {
	return small_realloc(NULL, nmemb, size);
}
static bool ok_if_null(const struct common_alloc_params *p, size_t x, size_t y) {
	void *p1 = p->fn(x, y);
	void *p2 = p->fn(y, x);
	const bool ok = !p1 & !p2;
	printf("%s\t%s\t0x%zx\t0x%zx\t%p\t%p\n", ok_str(ok), p->name, x, y, p1, p2);
	free(p1);
	free(p2);
	return ok;
}
static bool test_num_cpus(void) {
	const long cpus = num_cpus();
	const bool ok = cpus > 0;
	printf("%s\t%li\n", ok_str(ok), cpus);
	return ok;
}
static bool common_tests(void) {
	test_name(__func__);
	bool kay = true;

	puts("Do small_*alloc() functions limit memory? Are they overflow-proof?"
		" Tests must return NULL");
	puts("\tfn\telems\telem size\tfn(x,y)\tfn(y,x)");
	const struct common_alloc_params p[] = {
		{"malloc", small_malloc},
		{"calloc", small_calloc},
		{"realloc", small_realloc_from_nothing},
	};
	for (size_t i = 0; i < ARRAY_LEN(p); ++i) {
		const size_t n = 1 << 24;
		kay &= ok_if_null(p + i, n, 1);
	}
	for (size_t i = 0; i < ARRAY_LEN(p); ++i) {
		const size_t m = ((size_t)1 << (sizeof(m)*8 - 1)) | 1;
		kay &= ok_if_null(p + i, m, 2);
	}
	puts("");

	puts("num_cpus()");
	puts("\t> 0?");
	kay &= test_num_cpus();
	puts("");

	puts("The amazing null_function()");
	puts("\tnothing");
	null_function(0, "WOAAAAH", SIZE_MAX, "it does nothing");
	kay &= true;
	printf("%s\t%s\n", ok_str(kay), "");
	puts("");
	return kay;
}

typedef uint32_t (*bit32_fn_t)(uint32_t x);
struct bit_test_table {
	const char name[8];
	bit32_fn_t fn;
	uint32_t arg, expect;
};
static const char * test_order_str(enum endianness order) {
	return order == big_endian ? "msb" : "lsb";
}
static uint32_t minws_bits(uint32_t depth) {
	return bit_min_wordsize_bits((uint32_t)depth);
}
static uint32_t minws_log2(uint32_t depth) {
	return bit_min_wordsize_log2((uint32_t)depth);
}
static void aer_uux(uint32_t arg, uint32_t expect, uint32_t result) {
	printf("%" PRIu32 "\t%" PRIu32 "\t" FULL_X32 "\n",
		arg, expect, result);
}
static void aer_xxx(uint32_t arg, uint32_t expect, uint32_t result) {
	printf("" FULL_X32 "\t" FULL_X32 "\t" FULL_X32 "\n",
		arg, expect, result);
}
static void aer_uuu(uint32_t arg, uint32_t expect, uint32_t result) {
	printf("%" PRIu32 "\t%" PRIu32 "\t%" PRIu32 "\n",
		arg, expect, result);
}
static bool bit_test_run(const struct bit_test_table *t,
void (print_fn)(uint32_t a, uint32_t e, uint32_t r)) {
	const uint32_t result = t->fn(t->arg);
	const bool ok = t->expect == result;
	printf("%s\t%s\t", ok_str(ok), t->name);
	print_fn(t->arg, t->expect, result);
	return ok;
}
static bool test_bit_set(uint32_t n) {
	uint32_t mask = bit_set32(n);
	uint32_t cto = bit_cto32(mask);
	uint32_t clz = bit_clz32(mask);
	const bool ok = (cto == n) & (cto + clz == 32);
	printf("%s\t", ok_str(ok));
	aer_uux(n, cto+clz, mask);
	return ok;
}
#define BITREAD_LEN 28
static void test_print_bitread(const uint8_t *bitread, const size_t len,
const uint8_t end) {
	for (size_t i = 0; i < len; ++i) {
		putchar(bitread[i] + '0');
	}
	putchar(end);
}
static bool test_bit_next(enum endianness order, const uint8_t *expect,
const size_t seek) {
	bool ok = true;
	struct bitstrm bs;
	uint8_t result[BITREAD_LEN];

	bitstrm_from_wuptr(&bs, WUPTR_ARRAY(NUM_SEQ));
	bitstrm_seek(&bs, seek);
	for (size_t i = 0; i < BITREAD_LEN; ++i) {
		result[i] = bitstrm_next(&bs, order);
		ok &= result[i] == expect[i];
	}
	printf("%s\tbitstrm_next(%s)\t", ok_str(ok), test_order_str(order));
	test_print_bitread(expect, BITREAD_LEN, '\t');
	test_print_bitread(result, BITREAD_LEN, '\n');

	bitstrm_from_wuptr(&bs, WUPTR_ARRAY(NUM_SEQ));
	bitstrm_seek(&bs, seek);
	bool (*next_fn)(struct bitstrm *bs) = (order == big_endian)
		? bitstrm_msb_next : bitstrm_lsb_next;
	for (size_t i = 0; i < BITREAD_LEN; ++i) {
		result[i] = (*next_fn)(&bs);
		ok &= result[i] == expect[i];
	}
	printf("%s\tbitstrm_%s_next()\t", ok_str(ok), test_order_str(order));
	test_print_bitread(expect, BITREAD_LEN, '\t');
	test_print_bitread(result, BITREAD_LEN, '\n');
	return ok;
}
static bool test_bitstrm_from(void) {
	struct bitstrm bs1;
	struct bitstrm bs2;
	bitstrm_from_wuptr(&bs1, WUPTR_ARRAY(NUM_SEQ));
	bitstrm_from_bytes(&bs2, NUM_SEQ, sizeof(NUM_SEQ));
	// bitstrm may copy to its internal buffer and point to it
	bs1.buf = NULL;
	bs2.buf = NULL;
	int d = memcmp(&bs1, &bs2, sizeof(bs1));
	bool ok = !d;
	printf("%s\t%i\n", ok_str(ok), d);
	return ok;
}
static bool test_peek_32(struct bitstrm *bs, enum endianness order,
uint32_t expect, unsigned off) {
	uint32_t result1 = bitstrm_peek_32(bs, order);
	uint32_t result2 = (order == big_endian
		? bitstrm_msb_peek_32 : bitstrm_lsb_peek_32)(bs);
	bool ok = (expect == result1) & (expect == result2);
	printf("%s\t%s\t%u\t", ok_str(ok), test_order_str(order), off);
	aer_xxx(expect, result1, result2);
	return ok;
}
static bool test_bitstrm_peek_32(enum endianness order) {
	struct bitstrm bs;
	bitstrm_from_wuptr(&bs, WUPTR_ARRAY(NUM_SEQ));
	uint32_t expect = order == big_endian
		? buf_endian32b(NUM_SEQ)
		: bit_rev32(buf_endian32l(NUM_SEQ));

	bool ok = test_peek_32(&bs, order, expect, 0);

	const unsigned off = 3;
	bitstrm_seek(&bs, off);
	uint32_t lower = order == big_endian
		? (uint32_t)NUM_SEQ[4] >> (8 - off)
		: bit_rev32(NUM_SEQ[4]) >> (32 - off);
	expect = lower | expect << off;
	return ok & test_peek_32(&bs, order, expect, off);
}
static bool test_bit_get(const uint8_t *expect, size_t pos,
const size_t len) {
	bool ok = true;
	uint8_t result[BITREAD_LEN];
	for (size_t i = 0; i < len; ++i) {
		result[i] = bit_get(NUM_SEQ, pos + i);
		ok &= result[i] == expect[i];
	}
	printf("%s\t", ok_str(ok));
	test_print_bitread(expect, len, '\t');
	test_print_bitread(result, len, '\n');
	return ok;
}
static bool bit_tests(void) {
	test_name(__func__);
	bool kay = true;

	puts("Count of leading and trailing bits, bit reverse");
	puts("\tfn\tinput\texpected\tfn(input)");
	const struct bit_test_table bcp[] = {
		{"clo32", bit_clo32, 0, 0},
		{"clo32", bit_clo32, 0xffffffff, 32},
		{"clo32", bit_clo32, 0xf0f00fff, 4},

		{"cto32", bit_cto32, 0, 0},
		{"cto32", bit_cto32, 0xffffffff, 32},
		{"cto32", bit_cto32, 0xf0f00fff, 12},

		{"clz32", bit_clz32, 0, 32},
		{"clz32", bit_clz32, 0xffffffff, 0},
		{"clz32", bit_clz32, 0x0f0ff000, 4},

		{"ctz32", bit_ctz32, 0, 32},
		{"ctz32", bit_ctz32, 0xffffffff, 0},
		{"ctz32", bit_ctz32, 0x0f0ff000, 12},

		{"rev32", bit_rev32, 0xf0e0c080, 0x0103070f},
	};
	for (size_t i = 0; i < ARRAY_LEN(bcp); ++i) {
		kay &= bit_test_run(bcp + i, aer_xxx);
	}
	puts("");

	puts("bit_set32(input), input > 0");
	puts("\tinput\tcto+clz\tfn(input)");
	for (uint32_t n = 1; n <= 32; ++n) {
		kay &= test_bit_set(n);
	}
	puts("");

	puts("Minimal integer size for a given depth, in bits or as log2(bytes)");
	puts("\tfn\tinput\texpected\tfn(input)");
	const struct bit_test_table bmp[] = {
		{"bits", minws_bits, 1, 8},
		{"bits", minws_bits, 8, 8},
		{"bits", minws_bits, 9, 16},
		{"bits", minws_bits, 24, 32},
		{"bits", minws_bits, 33, 64},

		{"log2", minws_log2, 1, 0},
		{"log2", minws_log2, 8, 0},
		{"log2", minws_log2, 9, 1},
		{"log2", minws_log2, 24, 2},
		{"log2", minws_log2, 33, 3},
	};
	for (size_t i = 0; i < ARRAY_LEN(bmp); ++i) {
		kay &= bit_test_run(bmp + i, aer_uuu);
	}
	puts("");

	puts("bitstrm_from_(bytes|wuptr)() initialization equivalence");
	puts("\tmemcmp says");
	kay &= test_bitstrm_from();
	puts("");

	puts("bitstrm_(msb|lsb)_next(), with 4 bits overread");
	puts("\tfn\texpect\tresult");
	// Start at 0x66, as it's more interesting
	const size_t num_seq_off = 5*8;
	const enum endianness order[] = {big_endian, little_endian};
	const uint8_t bitread[ARRAY_LEN(order)][BITREAD_LEN] = {
		// overread 4 bits
		{0,1,1,0, 0,1,1,0, 0,1,1,1, 0,1,1,1, 1,0,0,0, 1,0,0,0, 0,0,0,0},
		{0,1,1,0, 0,1,1,0, 1,1,1,0, 1,1,1,0, 0,0,0,1, 0,0,0,1, 0,0,0,0},
	};
	for (size_t i = 0; i < ARRAY_LEN(order); ++i) {
		kay &= test_bit_next(order[i], bitread[i], num_seq_off);
	}
	puts("");

	puts("bitstrm_(msb|lsb)_peek_32()");
	puts("\torder\tseek\texpect\tpeek_32()\t(msb|lsb)_peek_32()");
	for (size_t i = 0; i < ARRAY_LEN(order); ++i) {
		kay &= test_bitstrm_peek_32(order[i]);
	}
	puts("");

	puts("bit_get()");
	puts("\texpect\tresult");
	kay &= test_bit_get(bitread[0], num_seq_off,
		zumin(BITREAD_LEN, sizeof(NUM_SEQ)*8 - num_seq_off));
	puts("");
	return kay;
}

int main(void) {
	const int kay = bit_tests()
		& common_tests()
		& endian_tests()
		& math_tests()
		& mparser_tests()
		& time_tests()
		& palette_tests()
		& pix_layout_tests()
		& unpack_tests()
		& opts_tests()
		& fast_math_tests()
		& wudefs_tests();
	return kay ? 0 : 1;
}
