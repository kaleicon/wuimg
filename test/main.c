// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <string.h>

#include "misc/common.h"
#include "misc/endian.h"
#include "misc/math.h"
#include "raster/pix.h"

static void test_name(const char *str) {
	fputs("---=== ", stdout);
	fputs(str, stdout);
	fputs(" ===---\n", stdout);
}
static const char * ok_str(const bool ok) {
	return ok ? "" : "!!!!!!";
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

static bool minmax_u_test(const char *fn, uint8_t r, uintmax_t a, uintmax_t b) {
	const bool ok = (r == a) & (a == b);
	printf("%s\t%s\t%i\t%ju\t%ju\n", ok_str(ok), fn, r, a, b);
	return ok;
}
static bool minmax_i_test(const char *fn, int8_t r, intmax_t a, intmax_t b) {
	const bool ok = (r == a) & (a == b);
	printf("%s\t%s\t%i\t%ji\t%ji\n", ok_str(ok), fn, r, a, b);
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

	puts("Do the min/max functions work?");
	puts("\tfn\texpect\tfn(x,y)\tfn(y,x)");
	const uint8_t ulo = 9;
	const uint8_t uhi = 12;
	const struct {
		const char name[7];
		uint8_t result;
		uintmax_t a, b;
	} up[] = {
		{  "umin", ulo,   umin(ulo, uhi),   umin(uhi, ulo)},
		{  "umax", uhi,   umax(ulo, uhi),   umax(uhi, ulo)},
		{ "zumin", ulo,  zumin(ulo, uhi),  zumin(uhi, ulo)},
		{ "zumax", uhi,  zumax(ulo, uhi),  zumax(uhi, ulo)},
		{"u32min", ulo, u32min(ulo, uhi), u32min(uhi, ulo)},
		{"u32max", uhi, u32max(ulo, uhi), u32max(uhi, ulo)},
	};
	for (size_t i = 0; i < ARRAY_LEN(up); ++i) {
		kay &= minmax_u_test(up[i].name, up[i].result, up[i].a, up[i].b);
	}
	const int8_t ilo = -1;
	const int8_t ihi = 108;
	const struct {
		const char name[7];
		int8_t result;
		intmax_t a, b;
	} ip[] = {
		{"imin", ilo, imin(ilo, ihi), imin(ihi, ilo)},
		{"imax", ihi, imax(ilo, ihi), imax(ihi, ilo)},
		{"lmin", ilo, lmin(ilo, ihi), lmin(ihi, ilo)},
		{"lmax", ihi, lmax(ilo, ihi), lmax(ihi, ilo)},
	};
	for (size_t i = 0; i < ARRAY_LEN(ip); ++i) {
		kay &= minmax_i_test(ip[i].name, ip[i].result, ip[i].a, ip[i].b);
	}
	puts("");

	puts("Do the ulog2 functions work?");
	puts("\tfn\tvalue\texpect\tresult");
	const struct {
		uint8_t val, expect;
	} logp[] = {
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
static uint32_t buf_end32(const void *buf, enum endianness e) {
	return buf_endian32(buf, e);
}
static uint32_t buf_end32b(const void *buf) {
	return buf_endian32b(buf);
}
static uint32_t buf_end32l(const void *buf) {
	return buf_endian32l(buf);
}
#define FULL_X32 "0x%08" PRIx32
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
	printf("%s\t%s%u%c"
		"\t{%02x,%02x,%02x,%02x}"
		"\t" FULL_X32 "\t" FULL_X32 "\t" FULL_X32
		"\t{%02x,%02x,%02x,%02x}\n",
		ok_str(ok),
		prefix, p->bits, (end == big_endian ? 'b' : 'l'),
		blob[0], blob[1], blob[2], blob[3],
		e, v1, v2,
		minv[0], minv[1], minv[2], minv[3]);
	return ok;
}
static bool end_test_battery(const struct endian_params *p, const uint8_t *blob) {
	return end_test(p, blob, "", big_endian, &p->end)
		& end_test(p, blob, "", little_endian, &p->end)
		& end_test(p, blob, "buf", big_endian, &p->buf)
		& end_test(p, blob, "buf", little_endian, &p->buf);
}
static bool endf32_val_test(const uint8_t *blob, const enum endianness e,
const float expect, float v1, float v2) {
	const bool ok = (expect == v1) & (v1 == v2);
	printf("%s\tf32%c"
		"\t{%02x,%02x,%02x,%02x}"
		"\t%.12f\t%.12f\t%.12f\n",
		ok_str(ok), (e == big_endian) ? 'b' : 'l',
		blob[0], blob[1], blob[2], blob[3],
		expect, v1, v2);
	return ok;
}
static bool endf32_test(const void *blob, const enum endianness e,
const float expect) {
	const uint32_t f32 = mem_to_u32(blob);
	return endf32_val_test(blob, e, expect,
			(e == big_endian ? endianf32b : endianf32l)(f32),
			endianf32(f32, e))
		& endf32_val_test(blob, e, expect,
			(e == big_endian ? buf_endianf32b : buf_endianf32l)(blob),
			buf_endianf32(blob, e));
}
static bool which_end_test(const uint8_t *blob) {
	const enum endianness cpu = which_end();
	const uint32_t val = buf_endian32(blob, cpu);
	const bool ok = !memcmp(&val, blob, sizeof(val));

	uint8_t mval[sizeof(val)];
	memcpy(mval, &val, sizeof(val));
	printf("%s"
		"\t{%02x,%02x,%02x,%02x}"
		"\t{%02x,%02x,%02x,%02x}\t%s\n",
		ok_str(ok),
		blob[0], blob[1], blob[2], blob[3],
		mval[0], mval[1], mval[2], mval[3],
		endian_str(cpu));
	return ok;
}
static bool endian_tests(void) {
	test_name(__func__);
	bool kay = true;

	puts("Do endian functions work? Are they revertible?");
	puts("\tfn\tinput memory\texpected val"
		"\tfn(input)\tfn(input, enum)\tfn(fn(input))");
	static const uint8_t NUM_SEQ[] = {
		0x11, 0x22, 0x33, 0x44,
	};
	static const struct endian_params params[] = {
		{16, 0x1122, 0x2211,
			{end16b, end16l, end16},
			{buf_end16b, buf_end16l, buf_end16}},
		{32, 0x11223344, 0x44332211,
			{end32b, end32l, end32},
			{buf_end32b, buf_end32l, buf_end32}},
	};
	for (size_t i = 0; i < ARRAY_LEN(params); ++i) {
		kay &= end_test_battery(params + i, NUM_SEQ);
	}
	puts("");

	puts("Does endianf32() work?");
	puts("\tfn\tinput memory\texpected val\tfn(input)\tfn(input, enum)");
	const float tau = (float)(M_PI * 2.0);
	const uint32_t tau_l = buf_endian32l(&tau);
	const uint32_t tau_b = buf_endian32b(&tau);
	kay &= endf32_test(&tau_l, little_endian, tau);
	kay &= endf32_test(&tau_b, big_endian, tau);
	puts("");

	puts("Does which_end() work? Output must match the input order");
	puts("\tinput memory\toutput memory\tcpu endianess");
	kay &= which_end_test(NUM_SEQ);
	puts("");
	return kay;
}

int main(void) {
	const bool kay = endian_tests() & math_tests() & pix_layout_tests();
	return kay ? 0 : 1;
}
