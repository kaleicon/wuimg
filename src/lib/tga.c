#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

#include "../common.h"
#include "../raster/pix.h"
#include "../raster/unpack.h"

#include "tga.h"

void tga_cleanup(struct tga_desc *desc) {
	free(desc->r.palette);
	free(desc->meta.stamp.palette);
}

static size_t raw_process(const struct raster_desc *raster, void *restrict data,
const size_t bytes) {
	if (raster->bitdepth == 16) {
		uint16_t *words = data;
		for (size_t i = 0; i < bytes/2; ++i) {
			words[i] = endian16(words[i], little_endian);
			if (raster->ch == 3) {
				words[i] |= (1 << 15);
			}
		}
	}
	return bytes;
}

static size_t raw_load(FILE *ifp, const struct raster_desc *raster,
uint8_t *restrict dst) {
	return raw_process(raster, dst, fread(dst, 1, raster_size(raster), ifp));
}

size_t tga_decode_stamp(const struct tga_desc *desc, void *restrict dst) {
	fseek(desc->ifp, desc->meta.stamp_offset + 2, SEEK_SET);
	return raw_load(desc->ifp, &desc->meta.stamp, dst);
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
			pix_set(output, rle, pixel_size, len);
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
}

static size_t rle_load(const struct tga_desc *desc, uint8_t *restrict dst) {
	const size_t dims = desc->r.w * desc->r.h;
	const size_t bytedepth = ((size_t)desc->depth + 7) / 8;

	fseek(desc->ifp, 0, SEEK_END);
	const long end = ftell(desc->ifp);
	const size_t file_len = (size_t)(end - desc->data_start);

	const size_t packet_len = 1 + bytedepth;
	// E.g. 0x80 0x00, 0x80 0x00 ...
	const size_t pathological_rle = dims * packet_len;


	const size_t rle_len = zumin(pathological_rle, file_len);
	unsigned char *rle = malloc(rle_len);
	if (rle) {
		fseek(desc->ifp, desc->data_start, SEEK_SET);
		const size_t read = fread(rle, 1, rle_len, desc->ifp);

		const size_t dst_len = dims * bytedepth;
		rle_decode(dst, dst + dst_len, rle, rle + read,
			bytedepth);
		free(rle);
		return raw_process(&desc->r, dst, dst_len);
	}
	return 0;

}

size_t tga_decode(const struct tga_desc *desc, void *restrict dst) {
	switch (desc->type) {
	case tga_no_image_data:
		break;
	case tga_colormap_data:
	case tga_truecolor_data:
	case tga_monochrome_data:
		fseek(desc->ifp, desc->data_start, SEEK_SET);
		return raw_load(desc->ifp, &desc->r, dst);
	case tga_colormap_rle:
	case tga_truecolor_rle:
	case tga_monochrome_rle:
		return rle_load(desc, dst);
	}
	return 0;
}

struct raster_pal * tga_take_extra_palette(struct tga_desc *desc) {
	struct raster_pal *pal = desc->map.pal;
	desc->map.pal = NULL;
	return pal;
}

static bool read_extension_area(struct tga_desc *desc) {
	struct tga_metadata *meta = &desc->meta;
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
	if (!fread(buf, len, 1, desc->ifp)) {
		return false;
	}
	meta->has_timestamp = memchk(buf, 0, len);
	if (meta->has_timestamp) {
		meta->timestamp = utc_to_epoch(
			buf_endian16(buf + 4, little_endian),
			buf_endian16(buf, little_endian),
			buf_endian16(buf + 2, little_endian),
			buf_endian16(buf + 6, little_endian),
			buf_endian16(buf + 8, little_endian),
			buf_endian16(buf + 10, little_endian)
		);
	}

	len = sizeof(meta->job.name);
	read = fread(meta->job.name, 1, len, desc->ifp);
	if (read != len || meta->job.name[len - 1] != 0) {
		return false;
	}

	if (!fread(buf, 6, 1, desc->ifp)) {
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

	if (!fread(buf, 23, 1, desc->ifp)) {
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

	if (meta->stamp_offset) {
		fseek(desc->ifp, meta->stamp_offset, SEEK_SET);
		len = 2;
		read = fread(buf, 1, len, desc->ifp);
		if (read != len || !buf[0] || !buf[1]) {
			meta->stamp_offset = 0;
			return false;
		}
		meta->stamp = desc->r;
		meta->stamp.w = buf[0];
		meta->stamp.h = buf[1];
		if (desc->r.palette) {
			meta->stamp.palette = memdup(desc->r.palette,
				sizeof(*desc->r.palette));
			if (!meta->stamp.palette) {
				meta->stamp_offset = 0;
				return false;
			}
		}
	}
	return true;
}

bool tga_parse_footer(struct tga_desc *desc) {
	/* TGA footer
		Offset  Size    Name
		-26     DWORD   ExtensionOffset;
		-22     DWORD   DeveloperOffset;
		-18     CHAR    Signature[18];
		EOF
	*/
	unsigned char footer[26];
	fseek(desc->ifp, -(long)(sizeof(footer)), SEEK_END);
	fread(footer, 1, sizeof(footer), desc->ifp);

	const char signature[] = "TRUEVISION-XFILE."; // null is important
	if (!memcmp(footer + 8, signature, sizeof(signature))) {
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
		desc->r.palette = pal;
		break;
	default:
		desc->map.pal = pal;
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

	if (!fread(buf, elem_size * elems, 1, desc->ifp)) {
		return lib_unexpected_eof;
	}
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
		unpack_strip(pal, wbuf, elems, 16, pix_packing_1555, op_expand);
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
		if (cm_type) {
			return lib_invalid_header;
		}
		break;
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
		return lib_invalid_header;
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

	desc->r.ch = 1;
	desc->r.layout = pix_bgra;
	desc->r.bitdepth = 8;
	switch (depth) {
	case 15:
	case 16:
		desc->r.bitdepth = 16;
		desc->r.attr = pix_packing_1555;
		break;
	case 8:
		if (!desc->map.len) {
			desc->r.layout = pix_gray;
		}
		break;
	case 24:
	case 32:
		desc->r.ch = depth / 8;
		break;
	default:
		return lib_invalid_header;
	}
	return lib_ok;
}

enum lib_fail tga_parse_header(struct tga_desc *desc, FILE *ifp) {
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
		18

	 * [1] For colormaps, 15-bit means A1R5G5B5 (MSB to LSB) packing with
	 *     Alpha ignored.
	 *     16-bit means the Alpha bit is used, but 0 means opaque and 1
	 *     transparent, which is the opposite of how pixel data is treated.
	 * [2] For pixel data, 15-bit and 16-bit are equivalent. Whether the
	 *     Alpha bit is used depends solely on the attribute bits field.
	 * [3] Most docs non-indicatively call them attribute bits and say
	 *     nothing about them. Should be 0 or 1 for 15/16-bit images and
	 *     0 or 8 for 32-bit images. Plenty of software seems to ignore
	 *     the value, though.
	 * [4] Remember that the raster is stored bottom-up, left to right.
	*/

	desc->ifp = ifp;
	desc->map = (struct tga_colormap){0};
	desc->meta.stamp.palette = NULL;

	uint8_t header[18];
	if (!fread(header, sizeof(header), 1, desc->ifp)) {
		return lib_unexpected_eof;
	}

	enum lib_fail fail = validate_header(desc,
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

	desc->meta.id_len = header[0];
	if (desc->meta.id_len) {
		if (!fread(desc->meta.id, desc->meta.id_len, 1, desc->ifp)) {
			return lib_unexpected_eof;
		}
	}

	if (desc->map.len) {
		fail = load_colormap(desc);
		if (fail != lib_ok) {
			return fail;
		}
	}

	raster_normalize(&desc->r);
	desc->data_start = ftell(desc->ifp);
	return lib_ok;
}
