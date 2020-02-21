#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib_pi.h"
#include "common.h"

static unsigned char * default_palette_16() {
/*	Pattern is

	Pos | 0| 1| 2| 3| 4| 5| 6| 7| 8| 9| a| b| c| d| e| f
	-----------------------------------------------------
	R   |00|00|77|77|00|00|77|77|00|00|ff|ff|00|00|ff|ff
	G   |00|00|00|00|77|77|77|77|00|00|00|00|ff|ff|ff|ff
	B   |00|77|00|77|00|77|00|77|00|ff|00|ff|00|ff|00|ff
*/
	const unsigned char table1[] = {0, 0x77, 0x77, 0, 0x77};
	const unsigned char table2[] = {0, 0xFF, 0xFF, 0, 0xFF};

	unsigned char *restrict pal = malloc(16 * 3);
	size_t i = 0;
	for (; i < 8; ++i) {
		pal[i*3  ] = table1[i & 2];
		pal[i*3+1] = table1[i & 4];
		pal[i*3+2] = table1[i & 1];
	}
	for (; i < 16; ++i) {
		pal[i*3  ] = table2[i & 2];
		pal[i*3+1] = table2[i & 4];
		pal[i*3+2] = table2[i & 1];
	}
	return pal;
}

int pi_parse_full_header(const unsigned char *data, size_t size,
struct pi_decompress_info *pinfo) {
	data += 2; // Skip signature;
	size -= 2;

	/* Comment */
	pinfo->comment = data;
	data = memchr(data, 0x1a, size);
	if (!data) {
		return 1;
	}
	pinfo->comment_len = (size_t)(data - pinfo->comment);
	size -= pinfo->comment_len;

	/* Dummy bytes */
	pinfo->dummy_bytes = data + 1;
	data = memchr(pinfo->dummy_bytes, 0x00, size);
	if (!data) {
		return 1;
	}
	pinfo->dummy_len = (size_t)(data - pinfo->dummy_bytes);
	size -= pinfo->dummy_len;

	if (size <= 10) {
		return 1;
	}
	/* Self-explanatory */
	++data;
	pinfo->palette_mode = data[0];
	pinfo->screen_ratio_num = data[1];
	pinfo->screen_ratio_den = data[2];
	pinfo->nr_of_planes = data[3];
	if (pinfo->nr_of_planes != 4 && pinfo->nr_of_planes != 8) {
		return 1;
	}
	memcpy((char *)pinfo->saver_model, data + 4, 4);
	pinfo->reserved_area_len = (unsigned int)endian_uint16(data + 8, big_endian);
	data += 10;
	size -= 10;
	if (size < pinfo->reserved_area_len) {
		return 1;
	}
	pinfo->reserved_area = data;
	data += pinfo->reserved_area_len;
	size -= pinfo->reserved_area_len;

	if (size < 4) {
		return 1;
	}
	pinfo->width = endian_uint16(data, big_endian);
	pinfo->height = endian_uint16(data + 2, big_endian);
	data += 4;
	size -= 4;

	if (pinfo->nr_of_planes == 4) {
		if (pinfo->palette_mode & 0x80) {
			pinfo->palette = default_palette_16();
		} else if (size >= 16*3) {
			pinfo->palette = data;
			data += 16*3;
			size -= 16*3;
		} else {
			return 1;
		}
	} else {
		printf("Unsupported number of planes (8)\n");
		return 1;
	}

	pinfo->pix = data;
	pinfo->pix_len = size;
	return 0;
}

int pi_check_sig(const unsigned char *data) {
	return data[0] == 'P' && data[1] == 'i';
}
