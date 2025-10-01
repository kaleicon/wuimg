// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#include <stdbool.h>
#include <string.h>

#include "misc/common.h"
#include "raster/pix.h"

struct pix_layout_names {
	enum pix_layout l;
	const char name[5];
};

static const char * ok_str(const bool ok) {
	return ok ? "-" : "!!!!!!";
}

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
		// GrayAlpha but on a palette. This name is really stretching it
		{PIX_LAYOUT_PACK(0, 0, 0, 3), "r  g"},
	};
	puts(__func__);
	bool ok = true;

	puts("Does swizzling to RGBA and back work?");
	puts("\torig\tlinear\tback");
	for (size_t i = 0; i < ARRAY_LEN(names); ++i) {
		ok &= swz_test(names[i]);
	}
	puts("");

	puts("Do enum values match their names?");
	puts("\tenum\trepr");
	for (size_t i = 0; i < ARRAY_LEN(names); ++i) {
		ok &= layout_name_test(names[i]);
	}
	return ok;
}

int main(void) {
	const bool ok = pix_layout_tests();
	return ok ? 0 : 1;
}
