// SPDX-License-Identifier: 0BSD
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

#include "misc/common.h"
#include "misc/endian.h"
#include "misc/file.h"
#include "misc/math.h"
#include "misc/mem.h"
#include "raster/bitfield.h"
#include "raster/fmt.h"
#include "raster/unpack.h"

#include "tga.h"

/*
https://web.archive.org/web/20230729203913/https://www.dca.fee.unicamp.br/~martino/disciplinas/ea978/tgaffs.pdf
*/

struct tga_ratio {
	uint16_t num, den;
};

const char * tga_type_str(const enum tga_image_type type) {
	switch (type) {
	case tga_no_image_data: return "No data";
	case tga_colormap_data: return "Colormap, uncompressed";
	case tga_truecolor_data: return "True color, uncompressed";
	case tga_monochrome_data: return "Monochrome, uncompressed";
	case tga_colormap_rle: return "Colormap, compressed";
	case tga_truecolor_rle: return "True color, compressed";
	case tga_monochrome_rle: return "Monochrome, compressed";
	}
	return "???";
}

void tga_cleanup(struct tga_desc *desc) {
	free(desc->map.extra_pal);
}

size_t tga_decode_stamp(const struct tga_desc *desc, struct wuimg *stamp) {
	if (wuimg_alloc_noverify(stamp)) {
		fseek(desc->ifp, desc->meta.stamp_offset + 2, SEEK_SET);
		return fmt_load_raster_swap(stamp, desc->ifp, little_endian);
	}
	return 0;
}

static void rle_decode(unsigned char *restrict output,
const unsigned char *restrict output_limit, const unsigned char *restrict rle,
const unsigned char *restrict rle_limit, const size_t pixel_size) {
	do {
		const unsigned char packet = *rle;
		const size_t len = (packet & 0x7f) + 1U;
		const size_t bytes = len*pixel_size;
		if (output + bytes > output_limit) {
			break;
		}

		++rle;
		if (packet & 0x80) {
			memwordset(output, rle, pixel_size, len);
			rle += pixel_size;
		} else {
			if (rle + bytes > rle_limit) {
				break;
			}
			memcpy(output, rle, bytes);
			rle += bytes;
		}
		output += bytes;
	} while (rle + pixel_size < rle_limit);
}

static size_t rle_load(const struct tga_desc *desc, struct wuimg *img) {
	const size_t dims = img->w * img->h;
	const size_t bytedepth = ((size_t)desc->depth + 7) / 8;

	const size_t file_len = file_remaining(desc->ifp);
	const size_t packet_len = 1 + bytedepth;
	// E.g. 0x80 0x00, 0x80 0x00 ...
	const size_t pathological_rle = dims * packet_len;

	const size_t rle_len = zumin(pathological_rle, file_len);
	unsigned char *rle = malloc(rle_len);
	if (rle) {
		const size_t read = fread(rle, 1, rle_len, desc->ifp);

		const size_t dst_len = dims * bytedepth;
		rle_decode(img->data, img->data + dst_len, rle, rle + read,
			bytedepth);
		free(rle);
		if (img->bitdepth == 16) {
			endian_loop16((uint16_t *)img->data, little_endian, dims);
		}
		return dst_len;
	}
	return 0;

}

size_t tga_decode(const struct tga_desc *desc, struct wuimg *img) {
	if (wuimg_alloc_noverify(img)) {
		fseek(desc->ifp, desc->data_start, SEEK_SET);
		switch (desc->type) {
		case tga_no_image_data:
			break;
		case tga_colormap_data:
		case tga_truecolor_data:
		case tga_monochrome_data:
			return fmt_load_raster_swap(img, desc->ifp, little_endian);
		case tga_colormap_rle:
		case tga_truecolor_rle:
		case tga_monochrome_rle:
			return rle_load(desc, img);
		}
	}
	return 0;
}

struct raster_pal * tga_take_extra_palette(struct tga_desc *desc) {
	struct raster_pal *pal = desc->map.extra_pal;
	desc->map.extra_pal = NULL;
	return pal;
}

enum wu_error tga_parse_stamp(const struct tga_desc *desc,
struct wuimg *main, struct wuimg *stamp) {
	uint8_t dims[2];
	fseek(desc->ifp, desc->meta.stamp_offset, SEEK_SET);
	if (!fread(dims, sizeof(dims), 1, desc->ifp)) {
		return wu_unexpected_eof;
	}

	if (wuimg_clone(stamp, main)) {
		stamp->w = dims[0];
		stamp->h = dims[1];
		return wuimg_verify(stamp);
	}
	return wu_alloc_error;
}

static bool read_extension_area(struct tga_desc *desc, struct wuimg *img) {
	/* Extension area:
		Offset  Size    Name
		0       WORD    ExtensionSize          // Must be 495
		2       CHAR    AuthorName[41]
		43      CHAR    AuthorComment[324]
		367     WORD    StampMonth
		369     WORD    StampDay
		371     WORD    StampYeat
		373     WORD    StampHour
		375     WORD    StampMinute
		377     WORD    StampSecond
		379     CHAR    JobName[41]
		420     WORD    JobHour
		422     WORD    JobMinute
		424     WORD    JobSecond
		426     CHAR    SoftwareID[41]
		467     WORD    VersionNumber
		469     CHAR    VersionLetter
		470     DWORD   KeyColor               // BGRA order
		474     WORD    PixelRatioWidth
		476     WORD    PixelRatioHeight
		478     WORD    GammaNum
		480     WORD    GammaDen
		482     DWORD   ColorCorrectionOffset
		486     DWORD   PostageStampOffset
		490     DWORD   ScanLineOffset
		494     BYTE    AlphaAttribute
		495
	*/
	struct tga_metadata *meta = &desc->meta;
	unsigned char buf[28];

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
	meta->timestamp = utc_to_epoch(
		buf_endian16(buf + 4, little_endian),
		buf_endian16(buf, little_endian),
		buf_endian16(buf + 2, little_endian),
		buf_endian16(buf + 6, little_endian),
		buf_endian16(buf + 8, little_endian),
		buf_endian16(buf + 10, little_endian)
	);

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

	if (!fread(buf, 28, 1, desc->ifp)) {
		return false;
	}
	meta->software.version_number = buf_endian16(buf, little_endian);
	meta->software.version_letter = (char)buf[2];
	memcpy(&meta->key_color, buf + 3, sizeof(meta->key_color));
	pix_layout_swizzle(&meta->key_color, 1, sizeof(meta->key_color),
		pix_bgra);

	wuimg_aspect_ratio(img, buf_endian16(buf + 7, little_endian),
		buf_endian16(buf + 9, little_endian));

	const struct tga_ratio gamma = {
		.num = buf_endian16(buf + 11, little_endian),
		.den = buf_endian16(buf + 13, little_endian),
	};
	if (gamma.num && gamma.den) {
		color_space_set_gamma(&img->cs, (double)gamma.num / gamma.den);
	}
	meta->color_correction_offset = buf_endian16(buf + 15, little_endian);
	meta->stamp_offset = buf_endian32(buf + 19, little_endian);

	switch (buf[27]) {
	case 0: // No alpha
	case 3: // Useful alpha data
		break;
	case 1: // Undefined non-essential alpha data
	case 2: // Undefined essential alpha data
		img->alpha = alpha_ignore;
		break;
	case 4: // Pre-multiplied alpha
		img->alpha = alpha_associated;
		break;
	}
	return true;
}

bool tga_parse_footer(struct tga_desc *desc, struct wuimg *img) {
	/* TGA footer
		Offset  Size    Name
		-26     DWORD   ExtensionOffset;
		-22     DWORD   DeveloperOffset;
		-18     CHAR    Signature[18];
		EOF
	*/
	unsigned char footer[26];
	if (file_tail(footer, sizeof(footer), 1, desc->ifp)) {
		// ending null is important
		const char sig[] = "TRUEVISION-XFILE.";
		if (!memcmp(footer + 8, sig, sizeof(sig))) {
			const unsigned int extension_off = buf_endian32(footer,
				little_endian);
			if (extension_off) {
				fseek(desc->ifp, (long)extension_off, SEEK_SET);
				return read_extension_area(desc, img);
			}
		}
	}
	return false;
}

static enum wu_error load_colormap(struct tga_desc *desc, struct wuimg *img) {
	struct raster_pal *pal = malloc(sizeof(*pal));
	if (!pal) {
		return wu_alloc_error;
	}
	switch (desc->type) {
	case tga_colormap_data:
	case tga_colormap_rle:
		wuimg_palette_set(img, pal);
		break;
	default:
		desc->map.extra_pal = pal;
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
		return wu_unexpected_eof;
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
		struct bitfield bf;
		bitfield_from_id(&bf, 0x1555, 16);
		bitfield_unpack(&bf, pal, wbuf, elems);
		break;
	case 24:
		raster_pal_from_rgb8(pal, buf, elems);
		break;
	}
	return wu_ok;
}

static enum wu_error validate_header(struct tga_desc *desc, struct wuimg *img,
const uint8_t cm_type, const uint8_t type, const uint16_t cm_start,
const uint16_t cm_len, const uint8_t cm_depth, const uint16_t width,
const uint16_t height, const uint8_t depth, const uint8_t img_desc) {
	switch (type) {
	case tga_no_image_data:
		return wu_no_image_data;
	case tga_truecolor_data:
	case tga_monochrome_data:
	case tga_truecolor_rle:
	case tga_monochrome_rle:
		if (cm_type) {
			return wu_invalid_header;
		}
		break;
	case tga_colormap_data:
	case tga_colormap_rle:
		if (cm_type == 1 && cm_start < cm_len && depth == 8) {
			switch (cm_depth) {
			case 15: case 16: case 24: case 32:
				break;
			default:
				return wu_invalid_header;
			}

			desc->map.offset = cm_start;
			desc->map.len = cm_len;
			desc->map.depth = cm_depth;
			break;
		}
		return wu_invalid_header;
	default:
		return wu_invalid_header;
	}

	if (width < 1 || height < 1) {
		return wu_invalid_header;
	}

	desc->type = type;
	desc->depth = depth;

	const uint8_t attr_bits = img_desc & 0x07;
	switch (attr_bits) {
	case 0: break;
	case 1:
		if (depth != 16) {
			return wu_invalid_header;
		}
		break;
	case 8:
		if (depth != 32) {
			return wu_invalid_header;
		}
		break;
	default:
		return wu_invalid_header;
	}

	img->w = width;
	img->h = height;
	img->channels = 1;
	img->bitdepth = 8;
	img->layout = pix_bgra;
	img->alpha = attr_bits ? alpha_unassociated : alpha_ignore;
	const bool h_flip = img_desc & 0x10;
	const bool v_flip = img_desc & 0x20;
	img->rotate ^= h_flip << 1;
	img->mirror = (h_flip ^ v_flip ^ 1);

	switch (depth) {
	case 8:
		if (!desc->map.len) {
			img->layout = pix_gray;
		}
		break;
	case 15:
	case 16:
		img->bitdepth = 16;
		if (!wuimg_bitfield_init_from_id(img, 0x1555)) {
			return wu_alloc_error;
		}
		break;
	case 24:
	case 32:
		img->channels = depth / 8;
		break;
	default:
		return wu_invalid_header;
	}
	return wuimg_verify(img);
}

enum wu_error tga_parse_header(struct tga_desc *desc, struct wuimg *img,
FILE *ifp) {
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
		|       4       Horizontal flip      //
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
	 * [4] Relative to the raster being stored bottom-up.
	*/

	desc->ifp = ifp;
	desc->map = (struct tga_colormap){0};

	uint8_t header[18];
	if (!fread(header, sizeof(header), 1, desc->ifp)) {
		return wu_unexpected_eof;
	}

	enum wu_error fail = validate_header(desc, img,
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
			return wu_unexpected_eof;
		}
	}

	if (desc->map.len) {
		fail = load_colormap(desc, img);
		if (fail != wu_ok) {
			return fail;
		}
	}

	desc->data_start = ftell(desc->ifp);
	return wu_ok;
}
