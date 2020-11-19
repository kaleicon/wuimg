#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "../../common.h"
#include "common/unpack.h"
#include "sun.h"

void sun_cleanup(struct sun_desc *desc) {
	free(desc->colormap.map);
}

unsigned int sun_get_row_alignment(struct sun_desc *desc) {
	switch (desc->bitdepth) {
	case 8:
		if (desc->colormap.map) {
			break;
		}
		// Fallthrough
	case 24:
	case 32:
		return 2;
	}
	return 2 - desc->w % 2;
}

static unsigned char * standard_decode_expand(const struct sun_desc *desc,
unsigned char *raster) {
	size_t out_len = desc->w * desc->h;
	if (desc->colormap.map || !desc->colormap.len) {
		out_len *= desc->ch;
	}
	unsigned char *output = malloc(out_len);
	if (!output) {
		free(raster);
		return NULL;
	}

	if (desc->colormap.map) {
		strip_colormap(output, raster, desc->colormap.map, desc->w,
			desc->h, 2, 3, desc->bitdepth);
	} else if (desc->colormap.len) { /* File has a colormap but the caller
		has taken it. */
		strip_unpack(output, raster, desc->w, desc->h, 2,
			unpack, desc->bitdepth);
	} else {
		// Do 4bit files with no colormap exist?
		strip_unpack(output, raster, desc->w, desc->h, 2,
			expand_invert, desc->bitdepth);
	}

	free(raster);
	return output;
}

static unsigned char * standard_decode(const struct sun_desc *desc,
unsigned char *raster) {
	switch (desc->bitdepth) {
	case 8:
		if (!desc->colormap.map) {
			break;
		}
		// Fallthrough
	case 4:
	case 1:
		return standard_decode_expand(desc, raster);
	}
	return raster;
}

static unsigned char * run_length_loop(unsigned char *restrict raster,
unsigned char *restrict rle, const unsigned char *restrict raster_limit,
const unsigned char *restrict rle_limit) {
#define MEMCHR_LOOP

#ifdef MEMCHR_LOOP
	for (;;) {
		const size_t limit = zumin((size_t)(rle_limit - rle),
			(size_t)(raster_limit - raster));
		unsigned char *new_rle = memchr(rle, 0x80, limit);
		if (new_rle) {
			const size_t read = (size_t)(new_rle - rle);
			if (read) {
				memcpy(raster, rle, read);
				raster += read;
			}

			if (new_rle + 1 == rle_limit) {
				break;
			}
			const size_t run_count = *(new_rle + 1);
			if (run_count) {
				if (raster + run_count + 1 > raster_limit
				|| new_rle + 2 == rle_limit) {
					break;
				}
				const int run_val = *(new_rle + 2);
				memset(raster, run_val, run_count + 1);
				raster += run_count + 1;
				rle = new_rle + 3;
			} else {
				*raster = 0x80;
				++raster;
				rle = new_rle + 2;
			}
		} else {
			memcpy(raster, rle, limit);
			raster += limit;
			break;
		}
	}
	return raster;
#else
	do {
		const unsigned char flag = *rle;
		if (flag == 0x80) {
			if (rle + 1 == rle_limit) {
				break;
			}
			const unsigned int run_count = *(rle + 1);
			if (run_count) {
				if (raster + run_count + 1 > raster_limit
				|| rle + 2 == rle_limit) {
					break;
				}
				const unsigned char run_val = *(rle + 2);
				memset(raster, run_val, run_count + 1);
				raster += run_count + 1;
				rle += 3;
			} else {
				*raster = flag;
				++raster;
				rle += 2;
			}
		} else {
			*raster = flag;
			++raster;
			++rle;
		}
	} while (raster < raster_limit && rle < rle_limit);
	return raster;
#endif /* MEMCHR_LOOP */
}

static unsigned char * rle_decode(const struct sun_desc *desc,
unsigned char *rle_data) {
	const size_t raster_len = desc->scan_len * desc->h;
	unsigned char *raster = malloc(raster_len);
	if (!raster) {
		free(rle_data);
		return NULL;
	}

	const unsigned char * raster_end = run_length_loop(raster, rle_data,
		raster + raster_len, rle_data + desc->data_len);
	free(rle_data);
	if (raster_end < raster + raster_len - 1) { // -1 due to alignment
		puts("SUN warning: Run-length decoding didn't fill the whole "
			"buffer. Output may contain garbage.");
	}
	return standard_decode(desc, raster);
}

unsigned char * sun_decode(const struct sun_desc *desc) {
	unsigned char *data = malloc(desc->data_len);
	if (!data) {
		return NULL;
	}

	size_t len;
	if (desc->bitdepth == 32) {
		// Expensive convert from 0BGR to BGR0
		len = desc->data_len - 1;
		fseek(desc->ifp, 1, SEEK_CUR);
	} else {
		len = desc->data_len;
	}
	const size_t read = fread(data, 1, len, desc->ifp);
	if (read != len) {
		puts(RASTER_EOF);
	}

	switch (desc->type) {
	case sun_old:
	case sun_standard:
	case sun_rgb:
		return standard_decode(desc, data);
	case sun_byte_encoded:
		return rle_decode(desc, data);
	default:
		return data;
	}
}

struct colormap * sun_take_colormap(struct sun_desc *desc) {
	struct colormap *map = desc->colormap.map;
	desc->colormap.map = NULL;
	return map;
}

static enum lib_fail interleave_colormap(struct sun_desc *desc) {
	struct colormap *map = malloc(sizeof(*desc->colormap.map) * 256);
	if (!map) {
		return lib_alloc_error;
	}

	const size_t entries = 1U << desc->bitdepth;
	unsigned char *buf = malloc(entries * 3);
	if (!buf) {
		free(map);
		return lib_alloc_error;
	}

	const size_t read = fread(buf, 3, entries, desc->ifp);
	if (read != entries) {
		free(buf);
		free(map);
		return lib_unexpected_eof;
	}

	for (size_t i = 0; i < entries; ++i) {
		map[i].r = buf[i];
		map[i].g = buf[i + entries];
		map[i].b = buf[i + entries * 2];
		map[i].a = 0xff;
	}
	free(buf);
	desc->colormap.map = map;
	return lib_ok;
}

static bool validate_file_size(struct sun_desc *desc) {
	const long start = ftell(desc->ifp);
	fseek(desc->ifp, 0, SEEK_END);
	const long end = ftell(desc->ifp);
	size_t file_size = (size_t)(end - start);
	if (file_size <= desc->colormap.len) {
		return false;
	} else {
		file_size -= desc->colormap.len;
	}

	const size_t raster_len = desc->scan_len * desc->h;
	if (desc->type == sun_byte_encoded) {
		// E.g. 0x80 0x00 0x80 0x00... -> 0x80 0x80...
		const size_t pathological_rle = raster_len * 2;
		desc->data_len = zumin(file_size, pathological_rle);
	} else {
		if (file_size < raster_len) {
			//return false;
			puts("SUN warning: Raster length is shorter than "
				"expected. Output will contain some garbage.");
		}
		desc->data_len = raster_len;
	}

	fseek(desc->ifp, start, SEEK_SET);
	return true;
}

static enum lib_fail validate_header(struct sun_desc *desc,
const uint32_t width, const uint32_t height, const uint32_t bitdepth,
const uint32_t type, const uint32_t cm_type, const uint32_t cm_len) {
	if (!width || !height) {
		return lib_invalid_header;
	}

	switch (bitdepth) {
	case 1: case 4: case 8: case 24: case 32:
		break;
	default:
		return lib_invalid_header;
	}

	switch (type) {
	case sun_old:
	case sun_standard:
	case sun_byte_encoded:
	case sun_rgb:
		break;
	case sun_tiff:
	case sun_iff:
		return lib_sun_unsupported_type;
	case sun_experimental:
		return lib_sun_experimental_type;
	default:
		return lib_invalid_header;
	}

	switch (cm_type) {
	case sun_no_colormap:
		if (cm_len) {
			return lib_invalid_header;
		}
		break;
	case sun_rgb_colormap:
		if (bitdepth > 8 || cm_len != (1U << bitdepth) * 3) {
			return lib_invalid_header;
		}
		break;
	case sun_raw_colormap:
		return lib_sun_uses_raw_colormap;
	default:
		return lib_invalid_header;
	}

	desc->w = width;
	desc->h = height;
	desc->bitdepth = (unsigned char)bitdepth;
	desc->type = (enum sun_type)type;
	desc->colormap.len = cm_len;
	return lib_ok;
}

enum lib_fail sun_parse_header(struct sun_desc *desc) {
	/* SUN header (after magic bytes)
		Offset  Size    Name
		0       DWORD   Width;
		4       DWORD   Height;
		8       DWORD   Depth;          // Bits per pixel
		12      DWORD   Length;         // Size of image data
		16	DWORD	Type;           // Type of raster file
		20	DWORD	ColorMapType;
		24	DWORD	ColorMapLen;
	*/

	uint32_t header[7];
	if (fread(header, 1, sizeof(header), desc->ifp) != sizeof(header)) {
		return lib_unexpected_eof;
	}

	const enum lib_fail fail = validate_header(desc,
		endian32(header[0], big_endian),
		endian32(header[1], big_endian),
		endian32(header[2], big_endian),
		endian32(header[4], big_endian),
		endian32(header[5], big_endian),
		endian32(header[6], big_endian));
	if (fail) {
		return fail;
	}


	desc->scan_len = (uint32_t)scanline_length(desc->w, desc->bitdepth, 2);
	if (!validate_file_size(desc)) {
		return lib_unexpected_eof;
	}

	if (desc->colormap.len) {
		desc->ch = 3;
		return interleave_colormap(desc);
	} else {
		desc->colormap.map = NULL;
		if (desc->bitdepth <= 8) {
			desc->ch = 1;
		} else {
			desc->ch = 3;
		}
		return lib_ok;
	}
}

enum lib_fail sun_open_file(FILE *ifp, struct sun_desc *desc) {
	unsigned char sig[4];
	enum lib_fail status;
	if (fscanf(ifp, "%4c", sig) == 1) {
		if (!memcmp(sig, "\x59\xa6\x6a\x95", sizeof(sig))) {
			desc->ifp = ifp;
			return lib_ok;
		} else {
			status = lib_invalid_signature;
		}
	} else {
		status = lib_unexpected_eof;
	}
	return status;
}
