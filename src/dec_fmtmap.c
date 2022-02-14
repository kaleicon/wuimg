/*

The output of dec_comptime -f goes here

*/

#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "common.h"
#include "dec_fmtmap.h"

static int fmaskmagiccmp(const void *restrict m1, const void *restrict m2) {
	const unsigned char *restrict magic1 = m1;
	const struct fmt_magic *restrict magic2 = m2;
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
	const struct fmt_ext *restrict ext2 = e2;
	return memcmp(ext1, ext2->ext, MAX_EXT_LEN);
}

static const struct fmt_magic * search_magic(FILE *ifp) {
	unsigned char magic[sizeof(magic_map->bytes)] = {0};
	const size_t read = fread(magic, 1, MAX_MAG_LEN, ifp);
	fseek(ifp, -(long)read, SEEK_CUR);
	if (read >= MIN_MAG_LEN) {
		return bsearch(magic, magic_map, ARRAY_LEN(magic_map),
			sizeof(*magic_map), fmaskmagiccmp);
	}
	return NULL;
}

static const struct fmt_ext * search_extension(const struct wuptr name) {
	const size_t max_check = zumin(name.len, MAX_EXT_LEN + 1 /* dot */);
	const unsigned char *end = name.ptr + name.len;
	const unsigned char *ext = memrchr(end - max_check, '.', max_check);
	if (ext) {
		++ext;
		const size_t len = (size_t)(end - ext);
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

int fmtmap_identify_file(FILE *ifp, const char *filename) {
	/* Some formats must be handled specially (i.e. RAW formats which are
	 * actually TIFF), so we search by extension first.
	 * Formats that should be identified by their magic sequence will
	 * return -1. */
	const struct fmt_ext *ext = search_extension(wuptr_str(filename));
	if (ext && ext->id != -1) {
		return ext->id;
	}

	const struct fmt_magic *magic = search_magic(ifp);
	if (magic) {
		return magic->id;
	}
	return -1;
}

bool fmtmap_known_extension(const struct wuptr filename) {
	return (bool)search_extension(filename);
}

void fmtmap_print_data(void) {
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
