#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "../../common.h"
#include "common/unpack.h"
#include "common/composite.h"

#include "tga.h"

void tga_cleanup(struct tga_desc *desc) {
	free(desc->meta);
	free(desc->map.entry);
}

static unsigned char * expand_555(unsigned char *restrict output,
unsigned char *restrict source, const bool alpha) {
	/* TGA packs 16bit data as
		'ARRRRRGG GGGBBBBB'
	 * but as the format is little-endian, the actual order in the file is
		'GGGBBBBB ARRRRRGG'
	 * Also, whether 15bit or 16bit, color is always packed as 555. */
	const int scale = (0xff << 16) / 31 + 1;
	const int b = source[0] & 0x1f;
	const int g = (source[1] << 3 | source[0] >> 5) & 0x1f;
	const int r = (source[1] >> 2) & 0x1f;
	output[0] = (unsigned char)((b * scale) >> 16);
	output[1] = (unsigned char)((g * scale) >> 16);
	output[2] = (unsigned char)((r * scale) >> 16);
	if (alpha) {
		output[3] = (source[1] & 0x80) ? 0x00 : 0xff;
		return output + 4;
	}
	return output + 3;
}

static unsigned char * expand_16bit(const struct tga_desc *desc,
unsigned char *restrict data) {
	const size_t dims = desc->w * desc->h;
	unsigned char *output = malloc(dims * desc->ch);
	if (!output) {
		free(data);
		return NULL;
	}

	unsigned char *pos = output;
	for (size_t i = 0; i < dims; ++i) {
		pos = expand_555(pos, data + i*2, desc->ch == 4);
	}
	free(data);
	return output;
}

static unsigned char * expand_colormap(const struct tga_desc *desc,
unsigned char *restrict data) {
	unsigned char *output = malloc(desc->w * desc->h * 3);
	if (!output) {
		free(data);
		return NULL;
	}

	strip_colormap(output, data, desc->map.entry, desc->w, desc->h, 1, 3, 8);
	free(data);
	return output;
}

static unsigned char * raw_decode(const struct tga_desc *desc,
unsigned char *restrict data) {
	switch (desc->type & 0x03) {
	case colormap_data:
		if (desc->map.entry) {
			return expand_colormap(desc, data);
		}
		break;
	case truecolor_data:
		if (desc->bytedepth == 2 && desc->expand_16bit) {
			return expand_16bit(desc, data);
		}
		break;
	case monochrome_data:
		break;
	}
	return data;
}

unsigned char * tga_decode_stamp(const struct tga_desc *desc,
unsigned int *width, unsigned int *height) {
	fseek(desc->ifp, desc->meta->stamp_offset, SEEK_SET);
	unsigned char dims[2];
	if (fread(dims, 1, 2, desc->ifp) != 2 || dims[0] == 0 || dims[1] == 0) {
		return NULL;
	}

	*width = dims[0];
	*height = dims[1];

	const size_t data_len = (*width) * (*height) * desc->bytedepth;
	unsigned char *data = malloc(data_len);
	if (!data) {
		return NULL;
	}

	fread(data, 1, data_len, desc->ifp);
	const struct tga_desc stamp_desc = {
		.expand_16bit = desc->expand_16bit,
		.type = desc->type,
		.w = *width,
		.h = *height,
		.bytedepth = desc->bytedepth,
		.ch = desc->ch,
		.map.entry = desc->map.entry,
	};
	return raw_decode(&stamp_desc, data);
}

static void run_length_loop_multi(unsigned char *restrict output,
const unsigned char *restrict output_limit, const unsigned char *restrict rle,
const unsigned char *restrict rle_limit, const unsigned char bytes) {
	do {
		const unsigned char packet = *rle;
		const unsigned int len = ((packet & 0x7f) + 1U);// * bytes;
		if (output + len * bytes >= output_limit) {
			break;
		}

		++rle;
		if (packet & 0x80) {
			output = color_set(output, rle, len, bytes);
			rle += bytes;
		} else {
			if (rle + len * bytes >= rle_limit) {
				break;
			}
			memcpy(output, rle, len * bytes);
			output += len * bytes;
			rle += len * bytes;
		}
	} while (rle + bytes < rle_limit);

	/* Apparently, the last value is to be repeated if the RLE stream
	 * finishes early. */
	const ptrdiff_t diff = (output_limit - output) / bytes;
	if (diff > 0) {
		color_set(output, rle - bytes, (size_t)diff, bytes);
	}
}

static void run_length_loop_single(unsigned char *restrict output,
const unsigned char *restrict output_limit, const unsigned char *restrict rle,
const unsigned char *restrict rle_limit) {
	while (rle + 1 < rle_limit) {
		const unsigned char packet = *rle;
		const unsigned int len = (packet & 0x7f) + 1U;
		if (output + len >= output_limit) {
			break;
		}

		++rle;
		if (packet & 0x80) {
			const unsigned char val = *rle;
			memset(output, val, len);
			++rle;
		} else {
			if (rle + len >= rle_limit) {
				break;
			}
			memcpy(output, rle, len);
			rle += len;
		}
		output += len;
	}
	const uintptr_t diff = (uintptr_t)(output_limit - output);
	if (diff) {
		memset(output, *(rle - 1), diff);
	}
}

static unsigned char * rle_decode(const struct tga_desc *desc,
unsigned char *restrict rle) {
	const size_t dims = desc->w * desc->h * desc->bytedepth;
	unsigned char *output = malloc(dims);
	if (!output) {
		free(rle);
		return NULL;
	}

	if (desc->bytedepth == 1) {
		run_length_loop_single(output, output + dims, rle,
			rle + desc->data_len);
	} else {
		run_length_loop_multi(output, output + dims, rle,
			rle + desc->data_len, desc->bytedepth);
	}
	free(rle);
	return raw_decode(desc, output);
}

unsigned char * tga_decode(const struct tga_desc *desc) {
	unsigned char *data = malloc(desc->data_len);
	if (!data) {
		return NULL;
	}

	fseek(desc->ifp, desc->data_start, SEEK_SET);
	const size_t read = fread(data, 1, desc->data_len, desc->ifp);
	if (read != desc->data_len) {
		puts(RASTER_EOF);
	}

	switch (desc->type) {
	case colormap_data:
	case truecolor_data:
	case monochrome_data:
		return raw_decode(desc, data);
	case colormap_rle:
	case truecolor_rle:
	case monochrome_rle:
		return rle_decode(desc, data);
	default:
		free(data);
		return NULL;
	}
}

struct tga_color_entry * tga_take_palette(struct tga_desc *desc) {
	struct tga_color_entry *e = desc->map.entry;
	desc->map.entry = NULL;
	return e;
}

static bool read_extension_area(struct tga_desc *desc) {
	struct tga_metadata *meta = desc->meta;
	unsigned char buf[24];

	const size_t area_len = 495;
	size_t len = 1;
	size_t read = fread(buf, 2, len, desc->ifp);
	if (read != len || buf_endian16(buf, little_endian) != area_len) {
		return false;
	}

	len = sizeof(meta->author.name);
	read = fread(meta->author.name, 1, len, desc->ifp);
	if (read != len || meta->author.name[len - 1] != 0) {
		return false;
	}

	len = sizeof(meta->author.comment);
	read = fread(meta->author.comment, 1, len, desc->ifp);
	if (read != len || meta->author.comment[len - 1] != 0) {
		return false;
	}

	len = 6;
	read = fread(buf, 2, len, desc->ifp);
	if (read != len) {
		return false;
	}
	if (!memchk(buf, 0, read * 2)) { // Check month and day
		meta->has_stamp = true;
		meta->stamp = (struct utc_time) {
			.mon = buf_endian16(buf, little_endian),
			.day = buf_endian16(buf + 2, little_endian),
			.year = buf_endian16(buf + 4, little_endian),
			.hour = buf_endian16(buf + 6, little_endian),
			.min = buf_endian16(buf + 8, little_endian),
			.sec = buf_endian16(buf + 10, little_endian),
		};
	} else {
		meta->has_stamp = false;
	}

	len = sizeof(meta->job.name);
	read = fread(meta->job.name, 1, len, desc->ifp);
	if (read != len || meta->job.name[len - 1] != 0) {
		return false;
	}

	len = 3;
	read = fread(buf, 2, len, desc->ifp);
	if (read != len) {
		return false;
	}
	meta->job.hour = buf_endian16(buf, little_endian);
	meta->job.minute = buf_endian16(buf + 2, little_endian);
	meta->job.second = buf_endian16(buf + 4, little_endian);

	len = sizeof(meta->software.id);
	read = fread(meta->software.id, 1, len, desc->ifp);
	if (read != len || meta->software.id[len - 1] != 0) {
		return false;
	}

	len = 23;
	read = fread(buf + 1, 1, len, desc->ifp);
	if (read != len) {
		return false;
	}
	meta->software.version_letter = (char)buf[1];
	meta->software.version_number = buf_endian16(buf + 2, little_endian);
	memcpy(&meta->key_color, buf + 4, 4);
	meta->pixel_numerator = buf_endian16(buf + 8, little_endian);
	meta->pixel_denominator = buf_endian16(buf + 10, little_endian);
	meta->gamma_numerator = buf_endian16(buf + 12, little_endian);
	meta->gamma_denominator = buf_endian16(buf + 14, little_endian);
	//meta->color_offset = buf_endian16(buf + 16, little_endian);
	meta->stamp_offset = buf_endian16(buf + 20, little_endian);
	return true;
}

bool tga_parse_footer(struct tga_desc *desc) {
	/* TGA footer
		Offset  Size    Name
		-26     DWORD   ExtensionOffset;
		-22     DWORD   DeveloperOffset;
		-18     CHAR    Signature[18];
	*/
	unsigned char footer[26];
	fseek(desc->ifp, -(long)(sizeof(footer)), SEEK_END);
	fread(footer, 1, sizeof(footer), desc->ifp);

	const char signature[] = "TRUEVISION-XFILE.";
	if (!memcmp(footer + 8, signature, sizeof(signature))) {
		if (!desc->meta) {
			desc->meta = calloc(1, sizeof(*desc->meta));
			if (!desc->meta) {
				return false;
			}
		}

		const unsigned int extension_off = buf_endian32(footer,
			little_endian);
		if (extension_off) {
			fseek(desc->ifp, (long)extension_off, SEEK_SET);
			return read_extension_area(desc);
		}
	}
	return false;
}

static enum lib_fail validate_filesize(struct tga_desc *desc) {
	fseek(desc->ifp, 0, SEEK_END);
	const long end = ftell(desc->ifp);

	const size_t file_len = (size_t)(end - desc->data_start);
	const size_t dims = desc->w * desc->h;
	if (desc->type & 0x08) {
		const size_t packet_len = desc->bytedepth + 1U;
		// E.g. 0x80 0x00, 0x80 0x00 ...
		const size_t pathological_rle = dims * packet_len;
		desc->data_len = zumin(pathological_rle, file_len);
	} else {
		desc->data_len = dims * desc->bytedepth;
		if (file_len < desc->data_len) {
			return lib_unexpected_eof;
		}
	}
	return lib_ok;
}

static enum lib_fail load_colormap(FILE *ifp, struct tga_colormap *map) {
	map->entry = malloc(sizeof(*map->entry) * 256);
	if (!map->entry) {
		return lib_alloc_error;
	}

	map->bytedepth = (unsigned char)((map->bitdepth + 7) / 8);
	if (map->offset) {
		fseek(ifp, map->offset * map->bytedepth, SEEK_CUR);
	}

	size_t elems;
	const size_t colormap_len = map->len - map->offset;
	if (colormap_len > 256) {
		elems = 256;
		fread(map->entry, map->bytedepth, elems, ifp);
		const long rem = (long)(colormap_len - elems) * map->bytedepth;
		fseek(ifp, rem, SEEK_CUR);
	} else {
		elems = colormap_len;
		fread(map->entry, map->bytedepth, elems, ifp);
	}

	if (map->bitdepth == 32) {
		return lib_ok;
	}

	unsigned char *pixel = (unsigned char *)map->entry;
	const bool has_alpha = map->bitdepth == 16;
	size_t i = elems;
	do {
		--i;
		const size_t p_off = i * map->bytedepth;
		if (map->bitdepth == 24) {
			map->entry[i].b = pixel[p_off];
			map->entry[i].g = pixel[p_off + 1];
			map->entry[i].r = pixel[p_off + 2];
			map->entry[i].a = 0xff;
		} else {
			expand_555((unsigned char *)(map->entry + i),
				pixel + p_off, has_alpha);
			if (!has_alpha) {
				map->entry[i].a = 0xff;
			}
		}
	} while (i != 0);
	return lib_ok;
}

static enum lib_fail validate_header(struct tga_desc *desc,
const uint8_t cm_type, const uint8_t type, const uint16_t cm_start,
const uint16_t cm_len, const uint8_t cm_depth, const uint16_t width,
const uint16_t height, const uint8_t bitdepth, const uint8_t img_desc) {
	switch (type) {
	case no_image_data:
		return lib_tga_no_image_data;
	case truecolor_data:
	case monochrome_data:
	case truecolor_rle:
	case monochrome_rle:
		if (cm_type == 0) {
			break;
		}
		return lib_invalid_header;
	case colormap_data:
	case colormap_rle:
		if (cm_type == 1 && cm_start < cm_len && bitdepth == 8) {
			switch (cm_depth) {
			case 15: case 16: case 24: case 32:
				break;
			default:
				return lib_invalid_header;
			}

			desc->map.offset = cm_start;
			desc->map.len = cm_len;
			desc->map.bitdepth = cm_depth;
			break;
		}
		return lib_invalid_header;
	default:
		return lib_unsupported_format;
	}

	if (!width || !height) {
		return lib_invalid_header;
	}

	desc->w = width;
	desc->h = height;
	desc->type = type;
	desc->bitdepth = bitdepth;
	desc->bytedepth = (unsigned char)((desc->bitdepth + 7) / 8);
	desc->attr_bits = img_desc & 0x07;
	desc->orientation = (img_desc >> 4) & 0x03;

	switch (bitdepth) {
	case 8:
		desc->ch = 1;
		break;
	case 15: case 16:
		desc->ch = desc->attr_bits ? 4 : 3;
		break;
	case 24:
		desc->ch = 3;
		break;
	case 32:
		desc->ch = 4;
		break;
	default:
		return lib_invalid_header;
	}

	return lib_ok;
}

enum lib_fail tga_parse_header(struct tga_desc *desc) {
	/* TGA header
		Offset  Size    Name
		0       BYTE    IDLength;       // Size of Image ID field
		1       BYTE    ColorMapType;
		2       BYTE    ImageType;
		3       WORD    CMapStart;
		5       WORD    CMapLength;
		7       BYTE    CMapDepth;
		8       WORD    XOffset;
		10      WORD    YOffset;
		12      WORD    Width;
		14      WORD    Height;
		16      BYTE    PixelDepth;
		17      BYTE    ImageDescriptor;
		18              ImageID;
	*/

	uint8_t header[18];
	if (fread(header, 1, sizeof(header), desc->ifp) != sizeof(header)) {
		return lib_unexpected_eof;
	}

	const enum lib_fail fail = validate_header(desc,
		header[1], header[2],
		buf_endian16(header + 3, little_endian),
		buf_endian16(header + 5, little_endian),
		header[7],
		buf_endian16(header + 12, little_endian),
		buf_endian16(header + 14, little_endian),
		header[16], header[17]);
	if (fail) {
		return fail;
	}

	if (desc->meta) {
		desc->meta->id_len = header[0];
		const size_t read = fread(desc->meta->id, 1,
			desc->meta->id_len, desc->ifp);
		if (read != desc->meta->id_len) {
			return lib_unexpected_eof;
		}
	} else {
		fseek(desc->ifp, header[0], SEEK_CUR);
	}

	if (desc->map.len) {
		load_colormap(desc->ifp, &desc->map);
	}

	desc->data_start = ftell(desc->ifp);
	return validate_filesize(desc);
}

enum lib_fail tga_open_file(FILE *ifp, struct tga_desc *desc,
const bool read_metadata) {
	desc->ifp = ifp;
	desc->expand_16bit = true;
	desc->map.len = 0;
	desc->map.entry = NULL;
	if (read_metadata) {
		desc->meta = calloc(1, sizeof(*desc->meta));
		if (!desc->meta) {
			return lib_alloc_error;
		}
	} else {
		desc->meta = NULL;
	}
	return lib_ok;
}
