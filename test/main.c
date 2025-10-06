// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "misc/bit.h"
#include "misc/common.h"
#include "misc/endian.h"
#include "misc/math.h"
#include "misc/time.h"

#include "raster/pal.h"
#include "raster/pix.h"
#include "raster/unpack.h"

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
	const size_t elems = p->bitdepth > 32
		? 2
		: (p->bitdepth > 8 ? 4 : 8);
	uint8_t out[sizeof(*e)] = {0};
	unpack_strip(out, blob, elems, p->bitdepth, attr, p->bit, p->op, NULL);

	const bool ok = !memcmp(out, e->m8, sizeof(out));
	printf("%s\t%02i/%s/%s/%s\t",
		ok_str(ok), p->bitdepth,
		p->bit == big_endian ? "ms" : "ls", unpack_op_str(p->op),
		pix_attr_str(attr));
	print_blob(e->m8, sizeof(*e), '\t');
	print_blob(out, sizeof(*e), '\n');
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
		& cmp_unpack(p, &e_inv, blob, pix_inverted)
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
	puts("\tdepth/order/op/attr\texpected\toutput");
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
		{1, pix_normal, big_endian, op_unpack,
			.e.m8 = {1,0,1,1, 1,0,1,0}},
		{2, pix_normal, big_endian, op_unpack,
			.e.m8 = {2,3, 2,2, 2,1, 2,0}},
		{3, pix_normal, big_endian, op_unpack,
			.e.m8 = {5,6,5,1, 4,1,6,6}},
		{4, pix_normal, big_endian, op_unpack,
			.e.m8 = {0xb, 0xa, 0x9, 0x8, 0x7, 0x6, 0x5, 0x4}},
		{5, pix_normal, big_endian, op_unpack,
			.e.m8 = {0x17, 0x0a, 0x0c, 0x07, 0x0c, 0x15, 0x01, 0x12}},
		{6, pix_normal, big_endian, op_unpack,
			.e.m8 = {0x2e, 0x29, 0x21, 0x36, 0x15, 0x03, 0x08, 0x10}},
		{7, pix_normal, big_endian, op_unpack,
			.e.m8 = {0x5d, 0x26, 0x0e, 0x65, 0x21, 0x48, 0x21, 0x7e}},
		{8, pix_normal, big_endian, op_unpack,
			.e.m8 = {0xba, 0x98, 0x76, 0x54, 0x32, 0x10, 0xfe, 0xdc}},

		{9, pix_normal, big_endian, op_unpack,
			.e.m16 = {0x0175, 0x0061, 0x01b2, 0x0143}},
		{10, pix_normal, big_endian, op_unpack,
			.e.m16 = {0x02ea, 0x0187, 0x0195, 0x0032}},
		{11, pix_normal, big_endian, op_unpack,
			.e.m16 = {0x05d4, 0x061d, 0x04a8, 0x0321}},
		{12, pix_normal, big_endian, op_unpack,
			.e.m16 = {0x0ba9, 0x0876, 0x0543, 0x0210}},
		{13, pix_normal, big_endian, op_unpack,
			.e.m16 = {0x1753, 0x01d9, 0x0a19, 0x010f}},
		{14, pix_normal, big_endian, op_unpack,
			.e.m16 = {0x2ea6, 0x0765, 0x10c8, 0x10fe}},
		{15, pix_normal, big_endian, op_unpack,
			.e.m16 = {0x5d4c, 0x1d95, 0x0642, 0x0fed}},
		// 16-, 32-, and 64-bits assumes native endianness

		{20, pix_normal, big_endian, op_pack,
			.e.m16 = {0xba98, 0x6543, 0x10fe, 0xcba9}},
		{24, pix_normal, big_endian, op_pack,
			.e.m16 = {0xba98, 0x5432, 0xfedc, 0x9876}},
		{28, pix_normal, big_endian, op_pack,
			.e.m16 = {0xba98, 0x4321, 0xdcba, 0x6543}},
		};
	for (size_t i = 0; i < ARRAY_LEN(up); ++i) {
		kay &= test_unpack_synth(up + i, data.mem);
	}
	const struct test_unpack_params single[] = {
		{1, pix_normal, little_endian, op_unpack,
			.e.m8 = {0,1,0,1, 1,1,0,1}},
		{1, pix_inverted, little_endian, op_unpack,
			.e.m8 = {1,0,1,0, 0,0,1,0}},
		{4, pix_normal, little_endian, op_unpack,
			.e.m8 = {0xa, 0xb, 0x8, 0x9, 0x6, 0x7, 0x4, 0x5}},
	};
	for (size_t i = 0; i < ARRAY_LEN(single); ++i) {
		kay &= test_unpack_single(single + i, data.mem);
	}
	const double mem[4] = {1, 2, 3, 4};
	const struct test_unpack_params floats[] = {
		{64, pix_float, big_endian, op_pack,
			.e.mf = {1,2}},
	};
	for (size_t i = 0; i < ARRAY_LEN(floats); ++i) {
		kay &= test_unpack_single(floats + i, mem);
	}
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
static bool time_tests(void) {
	test_name(__func__);
	bool kay = true;

	puts("Is utc_to_epoch() not broken-down?");
	puts("\tyear\tmonth\tday\thour\tminute\tsecond\tunix\tresult");
	const struct time_test_params dates[] = {
		{0, 1970, 1, 1, 0, 0, 0},
		{999999999, 2001, 9, 9, 1, 46, 39},
		{2147483648, 2038, 1, 19, 3, 14, 8},
	};
	for (size_t i = 0; i < ARRAY_LEN(dates); ++i) {
		kay &= to_epoch_test(dates + i);
	}
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
		"%i\t%i\t"
		"%i\t%ji\t%ji\n",
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

	puts("Integer positive mod");
	puts("\tfn\tx\ty\texpect\tfn(x,y)\tfn(x+y,y)");
	const int8_t xm = -13;
	const int8_t ym = 7;
	const struct test_twice_i modp[] = {
		{"imod", 1, imod(xm, ym), imod(xm + ym, ym)},
		{"lmod", 1, imod(xm, ym), imod(xm + ym, ym)},
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
	return kay;
}

typedef uint32_t (*bit32_fn_t)(uint32_t x);
struct bit_test_table {
	const char name[8];
	bit32_fn_t fn;
	uint32_t arg, expect;
};
static uint32_t minws_bits(uint32_t depth) {
	return bit_min_wordsize_bits((uint32_t)depth);
}
static uint32_t minws_log2(uint32_t depth) {
	return bit_min_wordsize_log2((uint32_t)depth);
}
static void aer_uxx(uint32_t arg, uint32_t expect, uint32_t result) {
	printf("\t%" PRIu32 "\t" FULL_X32 "\t" FULL_X32 "\n",
		arg, expect, result);
}
static void aer_xuu(uint32_t arg, uint32_t expect, uint32_t result) {
	printf("\t" FULL_X32 "\t%" PRIu32 "\t%" PRIu32 "\n",
		arg, expect, result);
}
static void aer_uuu(uint32_t arg, uint32_t expect, uint32_t result) {
	printf("\t%" PRIu32 "\t%" PRIu32 "\t%" PRIu32 "\n",
		arg, expect, result);
}
static bool bit_test_run(const struct bit_test_table *t,
void (print_fn)(uint32_t a, uint32_t e, uint32_t r)) {
	const uint32_t result = t->fn(t->arg);
	const bool ok = t->expect == result;
	printf("%s\t%s", ok_str(ok), t->name);
	print_fn(t->arg, t->expect, result);
	return ok;
}
static bool bit_tests(void) {
	test_name(__func__);
	bool kay = true;

	puts("Count of leading and trailing bits");
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
	};
	for (size_t i = 0; i < ARRAY_LEN(bcp); ++i) {
		kay &= bit_test_run(bcp + i, aer_xuu);
	}
	puts("");

	puts("Set n bits");
	puts("\tfn\tinput\texpected\tfn(input)");
	const struct bit_test_table bsp[] = {
		// 0 is UB, but that's fine cause 0 is useless, unlike 32
		{"set32", bit_set32, 1, 1},
		{"set32", bit_set32, 9, 0x1ff},
		{"set32", bit_set32, 32, 0xffffffff},
	};
	for (size_t i = 0; i < ARRAY_LEN(bsp); ++i) {
		kay &= bit_test_run(bsp + i, aer_uxx);
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
	return kay;
}

int main(void) {
	const bool kay = bit_tests()
		& common_tests()
		& endian_tests()
		& math_tests()
		& time_tests()
		& palette_tests()
		& pix_layout_tests()
		& unpack_tests();
	return kay ? 0 : 1;
}
