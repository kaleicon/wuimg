#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wbm.h"

struct wbm_transcriptor {
	uint8_t type;
	uint8_t quant_size;
	size_t offsets[8];
};

void wbm_cleanup(struct wbm_desc *desc) {
	free(desc->dir.sections);
	raster_free(&desc->r);
}

static size_t decode_section_data(const struct wbm_section *section, FILE *ifp,
uint8_t *restrict dst, const size_t dst_len, uint8_t quant_size) {
	bool plain = false;
	if (!quant_size) {
		quant_size = 1;
		plain = true;
	}

	struct wbm_transcriptor ts;
	struct wbm_retriever rt;
	const uint8_t transcriptor = (section->fmt & 1) | ((section->fmt >> 2) & 2);
	const uint8_t retriever = (section->fmt >> 1) & 3;
	switch (transcriptor) {
	case 0: case 2:
		ts->type = 0;
		break;
	case 3:
		return 0;
	case 1:
		ts->type = 1;
		break;
	}
}

static size_t get_section_data(const struct wbm_section *section, FILE *ifp,
uint8_t *restrict dst, const size_t dst_len, const uint8_t quant_size) {
	if (section->decomp_size < dst_len) {
		return 0;
	}

	fseek(ifp, (long)section->offset, SEEK_SET);
	if ((section->fmt & 0x80) || !section->comp_size) {
		return fread(dst, 1, section->decomp_size, ifp);
	}
	return decode_section_data(section, ifp, dst, dst_len);
}

static void join_mask(const struct wbm_desc *desc, uint8_t *restrict dst,
const uint8_t *restrict color, const uint8_t *restrict alpha) {
	const size_t dst_stride = scanline_length(desc->r.w, 32, 4);
	const size_t color_stride = scanline_length(desc->r.w, 24, 4);
	const size_t alpha_stride = scanline_length(desc->r.w, 8, 4);
	for (size_t y = 0; y < desc->r.h; ++y) {
		uint8_t *d = dst + dst_stride;
		const uint8_t *c = color + color_stride;
		const uint8_t *a = alpha + alpha_stride;
		for (size_t x = 0; x < desc->r.w; ++x) {
			memcpy(d + x*4, c + x*3, 3);
			d[x*4 + 3] = a[x];
		}
	}
}

size_t wbm_decode(const struct wbm_desc *desc, void *restrict dst) {
	const size_t dst_len = raster_size(&desc->r);
	uint8_t *restrict ptr;
	size_t ptr_len;
	if (desc->mask_idx >= 0) {
		ptr_len = scanline_length(desc->r.w, desc->depth, 4) * desc->r.h;
		ptr = (uint8_t *)dst + dst_len - ptr_len;
	} else {
		ptr_len = dst_len;
		ptr = dst;
	}

	size_t written = get_section_data(
		desc->dir.sections + desc->raster_idx, desc->ifp, ptr, ptr_len);

	if (desc->mask_idx >= 0) {
		const size_t alpha_len = scanline_length(desc->r.w, 1, 4) * desc->r.h;
		uint8_t *alpha = malloc(alpha_len);
		if (!alpha) {
			return 0;
		}
		written += get_section_data(desc->dir.sections + desc->mask_idx,
			desc->ifp, alpha, alpha_len);
		join_mask(desc, dst, ptr, alpha);
		free(alpha);
	}
	return written;
}

static enum lib_fail read_palette(struct wbm_desc *desc,
const struct wbm_section *section) {
	if (section->decomp_size % 3) {
		return lib_invalid_header;
	}

	desc->r.palette = malloc(sizeof(*desc->r.palette));
	if (!desc->r.palette) {
		return lib_alloc_error;
	}

	uint8_t *buf = (uint8_t *)(desc->r.palette + 1) - section->decomp_size;
	const size_t read = get_section_data(section, desc->ifp, buf,
		section->decomp_size);
	if (read != desc->decomp_size) {
		return lib_unexpected_eof;
	}
	for (size_t i = 0; i < desc->decomp_size/3; ++i) {
		pal->color[i].r = buf[i*3];
		pal->color[i].g = buf[i*3+1];
		pal->color[i].b = buf[i*3+2];
		pal->color[i].a = 0xff;
	}
	return lib_ok;
}

static enum lib_fail read_metadata(struct wbm_desc *desc,
const struct wbm_section *section) {
	uint8_t metadata[0x10];
	const size_t read = get_section_data(section, desc->ifp, metadata,
		sizeof(metadata));
	if (read != sizeof(metadata)) {
		return lib_unexpected_eof;
	}

	switch (metadata[0x0c]) {
	case 8: case 24: case 32:
		desc->depth = metadata[0x0c];
		break;
	default:
		return lib_invalid_header;
	}
	desc->r = (struct raster_desc) {
		.w = buf_endian16(metadata + 4, little_endian),
		.h = buf_endian16(metadata + 6, little_endian),
		.ch = (uint8_t)(desc->depth / 8),
		.bitdepth = 8,
		.alignment = 4,
		.layout = (desc->depth == 8) ? pix_gray : pix_bgra,
	};
	return lib_ok;
}

static enum lib_fail load_sections(struct wbm_desc *desc) {
	const size_t dir_len = desc->dir.count * sizeof(*desc->dir.sections);
	desc->dir.sections = malloc(dir_len);
	if (!desc->dir.sections) {
		return lib_alloc_error;
	}

	if (!fread(desc->dir.sections, dir_len, 1, desc->ifp)) {
		return lib_unexpected_eof;
	}

	for (uint8_t i = 0; i < desc->dir.count; ++i) {
		struct wbm_section *s = desc->dir.sections + i;
		s->offset = endian32(s->offset, little_endian);
		s->decomp_size = endian32(s->decomp_size, little_endian);
		s->comp_size = endian32(s->comp_size, little_endian);
		switch (s->id) {
		case wbm_image_info:
			;const enum lib_fail st = read_metadata(desc, s);
			if (st != lib_ok) {
				return st;
			}
			break;
		case wbm_image_data:
			desc->raster_idx = i;
			break;
		case wbm_image_palette:
			;const enum lib_fail st = read_palette(desc, s);
			if (st != lib_ok) {
				return st;
			}
			break;
		case wbm_image_mask:
			desc->mask_idx = i;
			break;
		}
	}
	if (desc->raster_idx < 0) {
		return lib_invalid_header;
	}
	if (desc->mask_idx >= 0) {
		if (desc->r.ch != 3) {
			// Warn to see if this ever happens
			printf("Mask with %d channel count\n", desc->r.ch);
			return lib_invalid_header;
		}
		desc->r.ch = 4;
	}
	return raster_normalize(&desc->r) ? lib_ok : lib_int_overflow;
}

enum lib_fail wbm_parse_header(struct wbm_desc *desc) {
	/* WPX BMP header (after signature):
		Offset  Size    Name
		0       DWORD   ???
		4       BYTE    Version // Always 1
		5       BYTE    ???
		6       BYTE    DirCount
		7       BYTE    DirSize
		8
	*/

	uint8_t buf[8];
	if (fread(buf, 1, sizeof(buf), desc->ifp) != sizeof(buf)) {
		return lib_unexpected_eof;
	}

	const uint8_t con = buf[4];
	desc->dir.count = buf[6];
	const uint8_t dir_size = buf[7];
	if (con != 1 || !desc->dir.count || dir_size != sizeof(*desc->dir.sections)) {
		return lib_invalid_header;
	}
	return load_sections(desc);
}

enum lib_fail wbm_open_file(struct wbm_desc *desc, FILE *ifp) {
	const uint8_t sig[] = {'W', 'P', 'X', 0x1a, 'B', 'M', 'P', 0};
	const enum lib_fail st = lib_sigcmp(sig, sizeof(sig), ifp);
	if (st == lib_ok) {
		*desc = (struct wbm_desc) {
			.ifp = ifp,
			.raster_idx = -1,
			.mask_idx = -1,
		};
		return lib_ok;
	}
	return st;
}
