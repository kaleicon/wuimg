#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "../common.h"
#include "../raster/composite.h"
#include "../raster/unpack.h"

#include "tga.h"

void tga_cleanup(struct tga_desc *desc) {
	free(desc->meta);
	free(desc->map.pal);
	free(desc->r.palette);
}

static unsigned char * raw_process(const struct raster_desc *raster,
void *restrict data) {
	if (raster->bitdepth == 16) {
		const size_t dims = raster->w * raster->h;
		uint16_t *words = data;
		for (size_t i = 0; i < dims; ++i) {
			words[i] = endian16(words[i], little_endian);
			if (raster->ch == 3) {
				words[i] |= (1 << 15);
			}
		}
	}
	return data;
}

static unsigned char * raw_load(FILE *ifp, const struct raster_desc *raster) {
	void *data = lib_load_rast(ifp, raster);
	if (data) {
		return raw_process(raster, data);
	}
	return NULL;
}

unsigned char * tga_decode_stamp(const struct tga_desc *desc,
size_t *restrict width, size_t *restrict height) {
	fseek(desc->ifp, desc->meta->stamp_offset, SEEK_SET);
	unsigned char dims[2];
	if (fread(dims, 1, sizeof(dims), desc->ifp) != sizeof(dims)
	|| dims[0] == 0 || dims[1] == 0) {
		return NULL;
	}

	*width = dims[0];
	*height = dims[1];

	struct raster_desc stamp_desc = {
		.w = *width,
		.h = *height,
		.ch = desc->r.ch,
		.bitdepth = desc->r.bitdepth,
	};
	raster_normalize(&stamp_desc);
	return raw_load(desc->ifp, &stamp_desc);
}

static void rle_decode(unsigned char *restrict output,
const unsigned char *restrict output_limit, const unsigned char *restrict rle,
const unsigned char *restrict rle_limit, const size_t pixel_size) {
	do {
		const unsigned char packet = *rle;
		const size_t len = (packet & 0x7f) + 1U;
		if (output + len * pixel_size > output_limit) {
			break;
		}

		++rle;
		if (packet & 0x80) {
			color_set(output, rle, pixel_size, len);
			rle += pixel_size;
		} else {
			if (rle + len * pixel_size > rle_limit) {
				break;
			}
			memcpy(output, rle, len * pixel_size);
			rle += len * pixel_size;
		}
		output += pixel_size * len;
	} while (rle + pixel_size < rle_limit);

	const ptrdiff_t diff = (output_limit - output);
	if (diff > 0) {
		printf("Missing %zd bytes\n", diff);
//		color_set(output, rle - pixel_size, pixel_size, (size_t)diff);
	}
}

static unsigned char * rle_load(const struct tga_desc *desc) {
	const size_t dims = desc->r.w * desc->r.h;
	const size_t bytedepth = ((size_t)desc->depth + 7) / 8;

	fseek(desc->ifp, 0, SEEK_END);
	const long end = ftell(desc->ifp);
	const size_t file_len = (size_t)(end - desc->data_start);

	const size_t packet_len = 1 + bytedepth;
	// E.g. 0x80 0x00, 0x80 0x00 ...
	const size_t pathological_rle = dims * packet_len;


	const size_t out_len = dims * bytedepth;
	unsigned char *out = malloc(out_len);
	if (!out) {
		return NULL;
	}

	const size_t rle_len = zumin(pathological_rle, file_len);
	unsigned char *rle = malloc(rle_len);
	if (!rle) {
		free(out);
		return NULL;
	}

	fseek(desc->ifp, desc->data_start, SEEK_SET);
	const size_t read = fread(rle, 1, rle_len, desc->ifp);
	if (read != rle_len) {
		puts(RASTER_EOF);
	}

	rle_decode(out, out + out_len, rle, rle + read, bytedepth);
	free(rle);
	return raw_process(&desc->r, out);
}

unsigned char * tga_decode(const struct tga_desc *desc) {
	switch (desc->type) {
	case tga_no_image_data:
		break;
	case tga_colormap_data:
	case tga_truecolor_data:
	case tga_monochrome_data:
		fseek(desc->ifp, desc->data_start, SEEK_SET);
		return raw_load(desc->ifp, &desc->r);
	case tga_colormap_rle:
	case tga_truecolor_rle:
	case tga_monochrome_rle:
		return rle_load(desc);
	}
	return NULL;
}

struct raster_pal * tga_take_extra_palette(struct tga_desc *desc) {
	struct raster_pal *pal = desc->map.pal;
	desc->map.pal = NULL;
	return pal;
}

static bool read_extension_area(struct tga_desc *desc) {
	struct tga_metadata *meta = desc->meta;
	unsigned char buf[24];

	const uint16_t area_len = 495;
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

	len = 12;
	read = fread(buf, 1, len, desc->ifp);
	if (read != len) {
		return false;
	}
	meta->has_timestamp = memchk(buf, 0, read);
	if (meta->has_timestamp) {
		meta->timestamp = (struct utc_time) {
			.mon = buf_endian16(buf, little_endian),
			.day = buf_endian16(buf + 2, little_endian),
			.year = buf_endian16(buf + 4, little_endian),
			.hour = buf_endian16(buf + 6, little_endian),
			.min = buf_endian16(buf + 8, little_endian),
			.sec = buf_endian16(buf + 10, little_endian),
		};
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
	read = fread(buf, 1, len, desc->ifp);
	if (read != len) {
		return false;
	}
	meta->software.version_letter = (char)buf[0];
	meta->software.version_number = buf_endian16(buf + 1, little_endian);
	memcpy(&meta->key_color, buf + 3, 4);
	meta->pixel_numerator = buf_endian16(buf + 7, little_endian);
	meta->pixel_denominator = buf_endian16(buf + 9, little_endian);
	meta->gamma_numerator = buf_endian16(buf + 11, little_endian);
	meta->gamma_denominator = buf_endian16(buf + 13, little_endian);
	//meta->color_offset = buf_endian16(buf + 15, little_endian);
	meta->stamp_offset = buf_endian16(buf + 19, little_endian);
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

	const char signature[] = "TRUEVISION-XFILE."; // null is important
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

static enum lib_fail load_colormap(struct tga_desc *desc) {
	struct raster_pal *pal = malloc(sizeof(*pal));
	if (!pal) {
		return lib_alloc_error;
	}
	switch (desc->type) {
	case tga_colormap_data:
	case tga_colormap_rle:
		desc->map.pal = pal;
		break;
	default:
		desc->r.palette = pal;
		break;
	}

	struct tga_colormap *map = &desc->map;
	const size_t colormap_len = map->len - map->offset;
	const size_t elems = zumin(colormap_len, 256);
	const size_t elem_size = (map->depth + 7U) / 8;

	unsigned char *buf = (unsigned char *)(pal + 1) - (elems * elem_size);

	if (map->offset) {
		fseek(desc->ifp, map->offset * (long)elem_size, SEEK_CUR);
	}

	fread(buf, elem_size, elems, desc->ifp);
	if (colormap_len > 256) {
		const long rem = (long)((colormap_len - elems) * elem_size);
		fseek(desc->ifp, rem, SEEK_CUR);
	}

	switch (map->depth) {
	case 15:
	case 16:
		;uint16_t *wbuf = (uint16_t *)buf;
		for (size_t i = 0; i < elems; ++i) {
			wbuf[i] = endian16(wbuf[i], little_endian) ^ (1 << 15);
		}
		unpack_strip(pal, wbuf, elems, 1, 1, pix_packing_1555,
			op_expand, 16);
		break;
	case 24:
		raster_pal_from_rgb8(pal, buf, elems);
	}
	return lib_ok;
}

static enum lib_fail validate_header(struct tga_desc *desc,
const uint8_t cm_type, const uint8_t type, const uint16_t cm_start,
const uint16_t cm_len, const uint8_t cm_depth, const uint16_t width,
const uint16_t height, const uint8_t depth, const uint8_t img_desc) {
	switch (type) {
	case tga_no_image_data:
		return lib_tga_no_image_data;
	case tga_truecolor_data:
	case tga_monochrome_data:
	case tga_truecolor_rle:
	case tga_monochrome_rle:
		if (cm_type == 0) {
			break;
		}
		return lib_invalid_header;
	case tga_colormap_data:
	case tga_colormap_rle:
		if (cm_type == 1 && cm_start < cm_len && depth == 8) {
			switch (cm_depth) {
			case 15: case 16: case 24: case 32:
				break;
			default:
				return lib_invalid_header;
			}

			desc->map.offset = cm_start;
			desc->map.len = cm_len;
			desc->map.depth = cm_depth;
			break;
		}
		return lib_invalid_header;
	default:
		return lib_unsupported_format;
	}

	if (!width || !height) {
		return lib_invalid_header;
	}

	desc->r = (struct raster_desc) {
		.w = width,
		.h = height,
	};
	desc->type = type;
	desc->depth = depth;
	desc->attr_bits = img_desc & 0x07;
	desc->orientation = (img_desc >> 4) & 0x03;

	if (desc->attr_bits) {
		switch (depth) {
		case 16:
			if (desc->attr_bits != 1) {
				return lib_invalid_header;
			}
			break;
		case 32:
			if (desc->attr_bits != 8) {
				return lib_invalid_header;
			}
			break;
		default:
			return lib_invalid_header;
		}
	}

	switch (depth) {
	case 8:
		desc->r.ch = 1;
		desc->r.bitdepth = 8;
		break;
	case 15:
	case 16:
		desc->r.ch = 1;
		desc->r.bitdepth = 16;
		desc->r.layout = pix_argb;
		desc->r.attr = pix_packing_1555;
		break;
	case 24:
		desc->r.ch = 3;
		desc->r.bitdepth = 8;
		desc->r.layout = pix_bgra;
		break;
	case 32:
		desc->r.ch = 4;
		desc->r.bitdepth = 8;
		desc->r.layout = pix_bgra;
		break;
	default:
		return lib_invalid_header;
	}
	return lib_ok;
}

enum lib_fail tga_parse_header(struct tga_desc *desc) {
	/* TGA header
		Offset  Size    Name
		0       BYTE    IDLength        // Size of Image ID field
		1       BYTE    ColorMapType
		2       BYTE    ImageType
		3       WORD    CMapStart
		5       WORD    CMapLength
		7       BYTE    CMapDepth       // 15,16,24[1]
		8       WORD    XOffset
		10      WORD    YOffset
		12      WORD    Width
		14      WORD    Height
		16      BYTE    PixelDepth      // 8,15,16,24,32[2]
		17      BYTE    ImageDescriptor
		|
		|       Bits
		|       0-3     Number of alpha bits // [3]
		|       4       Horizontal flip      // [4]
		|       5       Vertical flip        // [4]
		|       6-7     Reserved
		|
		18              ImageID;

	 * [1] 15-bit means A1R5G5B5 (MSB to LSB) packing with Alpha ignored.
	 *     16-bit means the Alpha bit is used, but 0 means opaque and 1
	 *     transparent, which is the opposite of how pixel data is treated.
	 * [2] 15-bit and 16-bit are equivalent for pixel data. Whether the
	 *     Alpha bit is used depends solely on the attribute bits field.
	 * [3] Also called attribute bits. Should be 1 for 16-bit images and
	 *     8 for 32-bit images. Documentation is not clear on whether other
	 *     values are valid.
	 * [4] Raster is stored bottom-up, left to right.
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
		load_colormap(desc);
	}

	raster_normalize(&desc->r);
	desc->data_start = ftell(desc->ifp);
	return lib_ok;
}

enum lib_fail tga_open_file(FILE *ifp, struct tga_desc *desc,
const bool read_metadata) {
	desc->ifp = ifp;
	desc->map.len = 0;
	desc->map.pal = NULL;
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
