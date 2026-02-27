// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2020 kaleido
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

#include "misc/decomp.h"
#include "misc/endian.h"
#include "misc/file.h"
#include "misc/math.h"
#include "misc/mem.h"
#include "misc/time.h"
#include "raster/bitfield.h"
#include "raster/fmt.h"
#include "raster/unpack.h"

#include "tga.h"

/*
https://web.archive.org/web/20230729203913/https://www.dca.fee.unicamp.br/~martino/disciplinas/ea978/tgaffs.pdf
*/

const char * tga_attr_type_str(const enum tga_attr_type type) {
	switch (type) {
	case tga_attr_ignore: return "No Alpha";
	case tga_attr_undefined_ignorable: return "Undefined, ignorable";
	case tga_attr_undefined_important: return "Undefined, should be retained";
	case tga_attr_useful_alpha: return "Useful Alpha data";
	case tga_attr_associated_alpha: return "Associated Alpha";
	}
	return "???";
}

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
	palette_unref(desc->map.pal);
}

static struct wu_st tga_load_raster(const struct tga_desc *desc,
struct wuimg *img) {
	return wuerr_partial(
		fmt_load_raster_swap(img, desc->ifp, little_endian),
		wuimg_size(img));
}

struct wu_st tga_load_stamp(const struct tga_desc *desc, struct wuimg *stamp) {
	fseek(desc->ifp, desc->meta.stamp_offset + 2, SEEK_SET);
	return tga_load_raster(desc, stamp);
}

static struct wu_st tga_rle_load(const struct tga_desc *desc, struct wuimg *img) {
	const size_t dims = img->w * img->h;
	const size_t bytedepth = ((size_t)desc->depth + 7) / 8;

	const size_t file_len = file_remaining(desc->ifp);
	const size_t packet_len = 1 + bytedepth;
	// E.g. 0x80 0x00, 0x80 0x00 ...
	const size_t pathological_rle = dims * packet_len;

	const size_t dst_len = dims * bytedepth;
	const size_t rle_len = zumin(pathological_rle, file_len);
	unsigned char *rle = malloc(rle_len);
	size_t written = 0;
	if (rle) {
		written = decomp_topbitrle(img->data, dst_len, rle,
			fread(rle, 1, rle_len, desc->ifp), bytedepth);
		free(rle);
		if (img->bitdepth == 16) {
			endian_loop16((uint16_t *)img->data, little_endian, dims);
		}
	}
	return wuerr_partial(written, dst_len);
}

struct wu_st tga_decode(const struct tga_desc *desc, struct wuimg *img) {
	fseek(desc->ifp, desc->data_start, SEEK_SET);
	switch (desc->type) {
	case tga_no_image_data:
		break;
	case tga_colormap_data:
	case tga_truecolor_data:
	case tga_monochrome_data:
		return tga_load_raster(desc, img);
	case tga_colormap_rle:
	case tga_truecolor_rle:
	case tga_monochrome_rle:
		return tga_rle_load(desc, img);
	}
	return wuerr(wu_invalid_params,
		"tried to decode with type == 'tga_no_image_data'");
}

static struct wu_st tga_set_img_dims(struct tga_desc *desc, struct wuimg *img,
const size_t w, const size_t h) {
	img->w = w;
	img->h = h;
	img->channels = 1;
	img->bitdepth = 8;
	img->layout = pix_bgra;
	img->alpha = alpha_ignore;
	const bool h_flip = desc->img_desc & 0x10;
	const bool v_flip = desc->img_desc & 0x20;
	img->rotate ^= h_flip << 1;
	img->mirror = (h_flip ^ v_flip ^ 1);
	switch (desc->depth) {
	case 8:
		if (desc->map.use) {
			wuimg_palette_set(img, palette_ref(desc->map.pal));
		} else {
			img->layout = pix_gray;
		}
		break;
	case 15:
	case 16:
		img->bitdepth = 16;
		if (!wuimg_bitfield_from_id(img, 0x1555)) {
			return WUERR_HERE(wu_alloc_error);
		}
		break;
	case 24:
	case 32:
		img->channels = desc->depth / 8;
		break;
	default:
		return wuerr(wu_invalid_header, "bad image bitdepth");
	}

	if (desc->ext_area) {
		struct tga_metadata *meta = &desc->meta;
		if (desc->img_desc & 0xf) {
			switch (meta->attr) {
			case tga_attr_ignore:
			case tga_attr_undefined_ignorable:
			case tga_attr_undefined_important:
				img->alpha = alpha_ignore;
				break;
			case tga_attr_useful_alpha:
			case tga_attr_associated_alpha:
				img->alpha = alpha_associated;
				break;
			}
		}
		wuimg_aspect_ratio(img,
			meta->pixel_ratio.num, meta->pixel_ratio.den);
		if (meta->gamma.num && meta->gamma.den) {
			color_space_set_gamma(&img->cs,
				(double)meta->gamma.num / meta->gamma.den);
		}
	}
	return WU_OK;
}

struct wu_st tga_img_info(struct tga_desc *desc, struct wuimg *img) {
	return tga_set_img_dims(desc, img, desc->w, desc->h);
}

struct wu_st tga_parse_stamp(struct tga_desc *desc, struct wuimg *stamp) {
	uint8_t dims[2];
	fseek(desc->ifp, desc->meta.stamp_offset, SEEK_SET);
	if (!fread(dims, sizeof(dims), 1, desc->ifp)) {
		return WUERR_HERE(wu_unexpected_eof);
	}
	return tga_set_img_dims(desc, stamp, dims[0], dims[1]);
}

static bool tga_parse_extension_area(struct tga_desc *desc) {
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
	if (read != len || buf_endian16l(buf) != area_len) {
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
		buf_endian16l(buf + 4),
		buf_endian16l(buf),
		buf_endian16l(buf + 2),
		buf_endian16l(buf + 6),
		buf_endian16l(buf + 8),
		buf_endian16l(buf + 10)
	);

	len = sizeof(meta->job.name);
	read = fread(meta->job.name, 1, len, desc->ifp);
	if (read != len || meta->job.name[len - 1] != 0) {
		return false;
	}

	if (!fread(buf, 6, 1, desc->ifp)) {
		return false;
	}
	meta->job.hour = buf_endian16l(buf);
	meta->job.minute = buf_endian16l(buf + 2);
	meta->job.second = buf_endian16l(buf + 4);

	len = sizeof(meta->software.id);
	read = fread(meta->software.id, 1, len, desc->ifp);
	if (read != len || meta->software.id[len - 1] != 0) {
		return false;
	}

	if (!fread(buf, 28, 1, desc->ifp)) {
		return false;
	}
	meta->software.version_number = buf_endian16l(buf);
	meta->software.version_letter = (char)buf[2];
	memcpy(&meta->key_color, buf + 3, sizeof(meta->key_color));
	pix_layout_swizzle_buf(&meta->key_color, 1, sizeof(meta->key_color),
		pix_rgba, pix_bgra);

	meta->pixel_ratio = (struct tga_ratio) {
		.num = buf_endian16l(buf + 7),
		.den = buf_endian16l(buf + 9),
	};
	meta->gamma = (struct tga_ratio) {
		.num = buf_endian16l(buf + 11),
		.den = buf_endian16l(buf + 13),
	};
	meta->color_correction_offset = buf_endian16l(buf + 15);
	meta->stamp_offset = buf_endian32l(buf + 19);
	meta->attr = buf[27];
	desc->ext_area = true;
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
	if (file_tail(footer, sizeof(footer), 1, desc->ifp)) {
		// ending null is important
		const char sig[] = "TRUEVISION-XFILE.";
		if (!memcmp(footer + 8, sig, sizeof(sig))) {
			const uint32_t extension_off = buf_endian32l(footer);
			if (extension_off) {
				fseek(desc->ifp, (long)extension_off, SEEK_SET);
				return tga_parse_extension_area(desc);
			}
		}
	}
	return false;
}

static struct wu_st tga_load_colormap(struct tga_desc *desc) {
	struct palette *pal = palette_new();
	if (!pal) {
		return WUERR_HERE(wu_alloc_error);
	}
	struct tga_colormap *map = &desc->map;
	map->pal = pal;

	const size_t colormap_len = map->len - map->offset;
	const size_t elems = zumin(colormap_len, 256);
	const size_t elem_size = (map->depth + 7U) / 8;
	uint8_t *buf = (uint8_t *)pal->color + ((4 - elem_size)*elems);

	fseek(desc->ifp, map->offset * (long)elem_size, SEEK_CUR);
	if (!fread(buf, elem_size * elems, 1, desc->ifp)) {
		return WUERR_HERE(wu_unexpected_eof);
	}
	fseek(desc->ifp, (long)((colormap_len - elems) * elem_size), SEEK_CUR);
	switch (map->depth) {
	case 15:
	case 16:
		;struct bitfield bf;
		bitfield_from_id(&bf, 0x1555, 16);
		bitfield_unpack(&bf, pal->color, buf, elems);
		break;
	case 24:
		palette_from_rgb8(pal, buf, elems);
		break;
	}
	return WU_OK;
}

struct wu_st tga_parse_header(struct tga_desc *desc, FILE *ifp) {
	/* TGA header
		Offset  Size    Name
		0       BYTE    IDLength        // Size of Image ID field
		1       BYTE    ColorMapType
		2       BYTE    ImageType
		3       WORD    CMapStart
		5       WORD    CMapLength
		7       BYTE    CMapDepth       // 15,16,24,32[1]
		8       WORD    XOffset
		10      WORD    YOffset
		12      WORD    Width
		14      WORD    Height
		16      BYTE    PixelDepth      // 8,15,16,24,32[2]
		17      BYTE    ImageDescriptor
		|
		|       Bits
		|       0-3     Number of attr bits  // [3]
		|       4       Horizontal flip      //
		|       5       Vertical flip        // [4]
		|       6-7     Reserved
		|
		18

	 * [1] For colormaps, 15-bit means X1R5G5B5 (MSB to LSB) packing.
	 *     16-bit means the top bit is used but for interrupts in the VDA
	 *     or VDA/D cards.
	 * [2] For pixel data, 15-bit and 16-bit are equivalent. Whether the
	 *     top bit is used depends solely on the attribute bits field.
	 * [3] Should be 0 or 1 for 16-bit images and 0 or 8 for 32-bit images.
	 *     These bits are sometimes used for Alpha, but unless the
	 *     extension area specifies so, there's no telling the intended use.
	 * [4] Relative to the raster being stored bottom-first.
	*/

	desc->ifp = ifp;
	desc->map = (struct tga_colormap){0};
	uint8_t header[18];
	if (!fread(header, sizeof(header), 1, desc->ifp)) {
		return WUERR_HERE(wu_unexpected_eof);
	}

	desc->type = header[2];
	desc->x = buf_endian16l(header + 8);
	desc->y = buf_endian16l(header + 10);
	desc->w = buf_endian16l(header + 12);
	desc->h = buf_endian16l(header + 14);
	desc->depth = header[16];
	desc->img_desc = header[17];
	desc->map.depth = header[7];

	const uint8_t cm_type = header[1];
	if (cm_type > 1) {
		return wuerr(wu_invalid_header, "unknown colormap type");
	}

	switch (desc->type) {
	case tga_no_image_data:
		return wuerr(wu_no_image_data, NULL);
	case tga_colormap_data:
	case tga_colormap_rle:
		if (cm_type != 1) {
			return wuerr(wu_invalid_header,
				"colormap type != 1 in paletted image");
		} else if (desc->depth != 8) {
			return wuerr(wu_invalid_header,
				"paletted image with depth != 8");
		}
		desc->map.use = true;
		break;
	case tga_truecolor_data:
	case tga_truecolor_rle:
		// Supposedly, these might still include an unused palette
		break;
	case tga_monochrome_data:
	case tga_monochrome_rle:
		if (cm_type != 0) {
			return wuerr(wu_invalid_header,
				"colormap type != 0 in monochrome image");
		} else if (desc->depth != 8) {
			return wuerr(wu_invalid_header,
				"grayscale image with depth != 8");
		}
		break;
	default:
		return wuerr(wu_invalid_header, "unknown image type");
	}
	if (cm_type == 1) {
		switch (desc->map.depth) {
		case 15: case 16: case 24: case 32:
			desc->map.offset = buf_endian16l(header + 3);
			desc->map.len = buf_endian16l(header + 5);
			if (!desc->map.len || desc->map.offset > desc->map.len) {
				return wuerr(wu_invalid_header,
					"bad colormap start and end");
			}
			break;
		default:
			return wuerr(wu_invalid_header, "bad colormap depth");
		}
	}

	switch (desc->img_desc & 0xf) {
	case 0:
		break;
	case 1:
		if (desc->depth != 16) {
			return wuerr(wu_invalid_header,
				"1 attr bit and bitdepth is not 16");
		}
		break;
	case 8:
		if (desc->depth != 32) {
			return wuerr(wu_invalid_header,
				"8 attr bits and bitdepth is not 32");
		}
		break;
	default:
		return wuerr(wu_invalid_header, "bad number of attr bits");
	}

	desc->meta.id_len = header[0];
	if (fread(desc->meta.id, 1, desc->meta.id_len, desc->ifp) < desc->meta.id_len) {
		return WUERR_HERE(wu_unexpected_eof);
	}

	struct wu_st st = WU_OK;
	if (desc->map.len) {
		st = tga_load_colormap(desc);
	}
	desc->data_start = ftell(desc->ifp);
	return st;
}
