#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "common.h"
#include "wustr.h"
#include "dec_conf.h"
#include "dec_fmtmap.h"

/* This file is #included by the code output of dec_fmtmap_sort_quine.c */

static int fmaskmagiccmp(const void *restrict m1, const void *restrict m2) {
	const unsigned char *restrict magic1 = m1;
	const struct file_magic *restrict magic2 = m2;
	int diff = 0;
	for (size_t i = 0; i < MAX_MAG_LEN; ++i) {
		const unsigned char m = magic2->and_mask[i];
		diff = (magic1[i] & m) - (magic2->bytes[i] & m);
		if (diff) {
			break;
		}
	}
	return diff;
}

static int fextcmp(const void *restrict e1, const void *restrict e2) {
	const char *restrict ext1 = e1;
	const struct file_ext *restrict ext2 = e2;
	return memcmp(ext1, ext2->ext, MAX_EXT_LEN);
}

static const struct file_magic * search_magic(FILE *ifp) {
	unsigned char in[sizeof(magic_map->bytes)] = {0};
	if (fread(in, 1, MAX_MAG_LEN, ifp) >= MIN_MAG_LEN) {
		return bsearch(in, magic_map, ARRAY_LEN(magic_map),
			sizeof(*magic_map), fmaskmagiccmp);
	}
	return NULL;
}

static const struct file_ext * search_extension(const struct wustr name) {
	const size_t check = zumin(name.len, MAX_EXT_LEN + 1 /* dot */);
	const size_t start = name.len - check;
	const char *ext = memchr(name.str + start, '.', check);
	if (ext) {
		++ext;
		const size_t len = (size_t)(name.str + name.len - ext);
		if (len >= MIN_EXT_LEN) {
			char l_ext[sizeof(extension_map->ext)] = {0};
			for (size_t i = 0; i < len; ++i) {
				l_ext[i] = (char)tolower(ext[i]);
			}
			return bsearch(l_ext, extension_map,
				ARRAY_LEN(extension_map),
				sizeof(*extension_map), fextcmp);
		}
	}
	return NULL;
}

enum format_id identify_image(FILE *ifp, const char *filename) {
	const struct file_ext *ext = search_extension(wustr_from_str(filename));
	if (ext && ext->id != fmt_unknown) {
		return ext->id;
	}

	const struct file_magic *magic = search_magic(ifp);
	rewind(ifp);
	if (magic && magic->id != fmt_unknown) {
		return magic->id;
	}
	return fmt_unknown;
}

bool known_extension(const struct wustr filename) {
	return (bool)search_extension(filename);
}

void print_map_data(void) {
	printf("Known extensions: %zu\n", ARRAY_LEN(extension_map));
	for (size_t i = 0; i < ARRAY_LEN(extension_map); ++i) {
		fputs(extension_map[i].ext, stdout);
		if (i + 1 < ARRAY_LEN(extension_map)) {
			fputs(", ", stdout);
		} else {
			break;
		}
	}

	printf("\n\nKnown magic sequences: %zu\n", ARRAY_LEN(magic_map));
}
