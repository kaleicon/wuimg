// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2026 kaleido
#include <stdlib.h>
#include <string.h>

#include "misc/common.h"
#include "misc/iff.h"
#include "misc/math.h"
#include "raster/fmt.h"

#include "lib/cdi.h"

/* CD-i IFF image
 * This format is simply a dump of the CD-i video memory.
 * Green Book spec (video encodings in Chapter V):
http://www.icdia.co.uk/docs/funcspec.html
 * IFF chunks info:
http://www.icdia.co.uk/authoring/IFF.docs.pdf

 * TODO: Needs testing with RGB555, RL3, PLTE, and DYUV_EACH images.
*/

const char * cdi_dyuv_start_str(enum cdi_dyuv_start type) {
	switch (type) {
	case cdi_dyuv_one: return "One";
	case cdi_dyuv_each: return "Each";
	}
	return "???";
}

const char * cdi_model_str(enum cdi_model model) {
	switch (model) {
	case cdi_rgb888: return "RGB888";
	case cdi_rgb555: return "RGB555";
	case cdi_dyuv: return "DYUV";
	case cdi_clut8: return "CLUT8";
	case cdi_clut7: return "CLUT7";
	case cdi_clut4: return "CLUT4";
	case cdi_clut3: return "CLUT3";
	case cdi_rl7: return "RL7";
	case cdi_rl3: return "RL3";
	case cdi_plte: return "PLTE";
	}
	return "???";
}

void cdi_cleanup(struct cdi_desc *desc) {
	if (desc->model == cdi_dyuv && desc->dyuv_start == cdi_dyuv_each) {
		free(desc->yuvs.each);
	}
}

static struct wu_st plte_to_rgba(struct pix_rgba8 *dst, FILE *ifp) {
	uint16_t hdr[2];
	if (!fread(hdr, sizeof(hdr), 1, ifp)) {
		return WUERR_HERE(wu_unexpected_eof);
	}

	const unsigned off = endian16b(hdr[0]);
	const unsigned entries = endian16b(hdr[1]);
	if (!entries || entries > 0x100 || off >= 0x100
	|| entries + off > 0x100) {
		return wuerr(wu_invalid_header,
			"bad palette offset/nr of entries");
	}

	/* Keep the unset portions transparent */
	dst += off;
	uint8_t *buf = (uint8_t *)dst + entries;
	const size_t r = fread(buf, 3, entries, ifp);
	for (size_t i = 0; i < r; ++i) {
		dst[i] = (struct pix_rgba8) {
			.r = buf[i*3],
			.g = buf[i*3+1],
			.b = buf[i*3+2],
			.a = 0xff,
		};
	}
	return wuerr_partial(r, entries);
}

static struct wu_st cdi_rl7_decode(struct cdi_desc *desc, struct wuimg *img) {
	size_t dst_stride = wuimg_stride(img);
	size_t dst_len = dst_stride * img->h;
	/* RL7 streams are always smaller than the expanded 8-bit data, so put
	 * it at the end of the image buffer and decode in place. */
	size_t src_len = zumin(desc->data_len, dst_len);
	uint8_t *dst = img->data;
	uint8_t *src = dst + dst_len - src_len;
	size_t r = fread(src, 1, src_len, desc->ifp);
	size_t s = 0;

	size_t written = 0;
	for (size_t y = 0; y < img->h && s < r; ++y) {
		uint8_t *row = dst + y*dst_stride;
		size_t x = 0;
		while (x < img->w && s < r) {
			uint8_t val = src[s];
			++s;
			size_t len = 1;
			if (val >> 7) {
				if (s >= r) {
					break;
				}
				len = src[s];
				++s;
				size_t rem = img->w - x;
				/* if len == 0, repeat until the end of line.
				 * len == 1 is invalid, but treat it as 0 for
				 * simplicity */
				if (len < 2 || len > rem) {
					len = rem;
				}
				val &= 0x7f;
			}
			memset(row + x, val, len);
			x += len;
		}
		written += x;
	}
	return wuerr_partial(written, img->w * img->h);
}

static unsigned dyuv_quant(unsigned b) {
	const uint8_t q[16] = {
		0, 1, 4, 9,
		16, 27, 44, 79,

		256-128, 256-79, 256-44, 256-27,
		256-16, 256-9, 256-4, 256-1,
	};
	return q[b & 0xf];
}

static struct wu_st cdi_dyuv_decode(struct cdi_desc *desc, struct wuimg *img) {
	const size_t src_stride = strip_length(img->w, 8, img->align_sh);
	uint8_t *buf = malloc(src_stride);
	if (!buf) {
		return WUERR_HERE(wu_alloc_error);
	}

	struct plane_info *p = img->u.planes->p;
	const bool many_yuvs = desc->dyuv_start != cdi_dyuv_one;
	uint8_t *yuvs = many_yuvs ? desc->yuvs.each : desc->yuvs.one;
	size_t written = 0;
	for (size_t ypos = 0; ypos < img->h; ++ypos) {
		size_t r = fread(buf, 1, src_stride, desc->ifp);
		uint8_t *yp = p[0].ptr + p[0].stride*ypos;
		uint8_t *up = p[1].ptr + p[1].stride*ypos;
		uint8_t *vp = p[2].ptr + p[2].stride*ypos;
		unsigned y = yuvs[0], u = yuvs[1], v = yuvs[2];
		yuvs += many_yuvs*3;
		for (size_t x = 0; x < r/2; ++x) {
			uint8_t uy = buf[x*2];
			y += dyuv_quant(uy);
			u += dyuv_quant(uy >> 4);
			yp[x*2] = (uint8_t)y;
			up[x] = (uint8_t)u;

			uint8_t vy = buf[x*2+1];
			y += dyuv_quant(vy);
			v += dyuv_quant(vy >> 4);
			yp[x*2+1] = (uint8_t)y;
			vp[x] = (uint8_t)v;
		}
		written += r;
	}
	free(buf);
	return wuerr_partial(written, src_stride*img->h);
}

struct wu_st cdi_load(struct cdi_desc *desc) {
	struct wuimg *img = desc->img;
	switch (desc->model) {
	case cdi_rgb888:
	case cdi_rgb555:
	case cdi_clut8:
	case cdi_clut7:
	case cdi_clut4:
	case cdi_clut3:
		return fmt_load_raster_st(img, desc->ifp);
	case cdi_plte:
		return plte_to_rgba((struct pix_rgba8 *)img->data, desc->ifp);
	case cdi_rl7:
		return cdi_rl7_decode(desc, img);
	case cdi_dyuv:
		return cdi_dyuv_decode(desc, img);
	case cdi_rl3:
		break;
	}
	return wuerr(wu_invalid_params, "attempted to decode unknown encoding");
}

static struct wu_st idat_cdi(struct iff_state *iff, void *user,
const struct iff_chunk chunk) {
	(void)iff;
	struct cdi_desc *desc = user;
	if (desc->needs_plte && desc->img->mode != image_mode_palette) {
		return wuerr(wu_invalid_header, "missing PLTE chunk");
	} else if (desc->model == cdi_dyuv
	&& desc->dyuv_start == cdi_dyuv_each && !desc->yuvs.each) {
		return wuerr(wu_invalid_header, "missing YUVS chunk");
	}
	desc->data_len = chunk.len;
	return WU_OK;
}

static struct wu_st plte_cdi(struct iff_state *iff, void *user,
const struct iff_chunk chunk) {
	/* PLTE struct:
		Offset  Type    Name
		0       u16     Offset
		2       u16     Entries
		4       struct  RGB[Entries]
	*/
	struct cdi_desc *desc = user;
	if (desc->model == cdi_plte) { // will load later
		return WU_OK;
	} else if (desc->img->mode == image_mode_palette) {
		return wuerr(wu_invalid_header, "repeated PLTE chunks");
	}

	struct palette *pal = wuimg_palette_init(desc->img);
	if (!pal) {
		return WUERR_HERE(wu_alloc_error);
	}

	struct wu_st st = plte_to_rgba(pal->color, desc->ifp);
	if (st.msg) {
		return WUERR_HERE(wu_unexpected_eof);
	} else if (!wu_isok(st)) {
		return st;
	}
	return iff_next_FILE(iff, desc->ifp, chunk);
}

static struct wu_st ipar_cdi(struct iff_state *iff, void *user,
const struct iff_chunk chunk) {
	/* IPAR struct:
		Offset  Type    Name
		0       s16     XOff
		2       s16     YOff
		4       u16     XRatio
		6       u16     YRatio
		8       u16     SrcWidth
		10      u16     SrcHeight
		12      u16     XHotspot
		14      u16     YHotspot
		16      rgb24   TransparentColor
		19      rgb24   MaskColor
		22
	*/
	struct cdi_desc *desc = user;
	uint8_t hdr[22];
	if (chunk.len != sizeof(hdr)) {
		return wuerr(wu_invalid_header, "IPAR size != 0x16");
	} else if (!fread(hdr, sizeof(hdr), 1, desc->ifp)) {
		return WUERR_HERE(wu_unexpected_eof);
	}
	desc->ipar = (struct cdi_ipar) {
		.off = {
			(int16_t)buf_endian16b(hdr),
			(int16_t)buf_endian16b(hdr + 2),
		},
		.src = {
			buf_endian16b(hdr + 8),
			buf_endian16b(hdr + 10),
		},
		.hotspot = {
			buf_endian16b(hdr + 12),
			buf_endian16b(hdr + 14),
		},
		.trans = {.r = hdr[16], .g = hdr[17], .b = hdr[18]},
		.mask = {.r = hdr[19], .g = hdr[20], .b = hdr[21]},
	};
	wuimg_aspect_ratio(desc->img,
		buf_endian16b(hdr + 4), buf_endian16b(hdr + 6));
	return iff_next_FILE(iff, desc->ifp, chunk);
}

static struct wu_st user_cdi(struct iff_state *iff, void *user,
const struct iff_chunk chunk) {
	// Arbitrary data
	struct cdi_desc *desc = user;
	desc->has_user_data = true;
	desc->user_len = chunk.len;
	return iff_skip_FILE(iff, desc->ifp, chunk);
}

static struct wu_st yuvs_cdi(struct iff_state *iff, void *user,
const struct iff_chunk chunk) {
	/* YUVS struct:
		Offset  Type    Name
		0       yuv24   DYUVStart[Height]
	*/
	struct cdi_desc *desc = user;
	const size_t h = desc->img->h;
	const size_t size = h*3;
	if (chunk.len != size) {
		return wuerr(wu_invalid_header, "YUVS chunk size != height*3");
	} else if (desc->yuvs.each) {
		return wuerr(wu_invalid_header, "repeated YVUS chunks");
	}
	desc->yuvs.each = malloc(size);
	if (!desc->yuvs.each) {
		return WUERR_HERE(wu_alloc_error);
	} else if (!fread(desc->yuvs.each, size, 1, desc->ifp)) {
		return WUERR_HERE(wu_unexpected_eof);
	}
	return iff_next_FILE(iff, desc->ifp, chunk);
}

static const struct iff_table CDI_CHUNKS[] = {
	// Never reorder this
	{FOURCC('P', 'L', 'T', 'E'), plte_cdi},
	{FOURCC('I', 'P', 'A', 'R'), ipar_cdi},
	{FOURCC('U', 'S', 'E', 'R'), user_cdi},
	{FOURCC('I', 'D', 'A', 'T'), idat_cdi},
	{FOURCC('Y', 'U', 'V', 'S'), yuvs_cdi},
};

static struct wu_st ihdr_cdi(struct iff_state *iff, void *user,
const struct iff_chunk chunk) {
	/* IHDR struct:
		Offset  Type    Name
		0       u16     Width
		2       u16     LineStride
		4       u16     Height
		6       u16     CodingMethod
		8       u16     Depth
		10      u8      DYUVType
		12      yuv24   DYUVStart
		14
	*/
	struct cdi_desc *desc = user;
	uint8_t hdr[14];
	if (chunk.len != sizeof(hdr)) {
		return wuerr(wu_invalid_header, "IHDR size != 14");
	} else if (!fread(hdr, sizeof(hdr), 1, desc->ifp)) {
		return WUERR_HERE(wu_unexpected_eof);
	}

	struct wuimg *img = desc->img;
	img->w = buf_endian16b(hdr);
	img->h = buf_endian16b(hdr + 4);
	img->channels = 1;
	img->bitdepth = 8;
	img->align_sh = 2;
	img->cs.limited = true;

	iff->table = CDI_CHUNKS;
	iff->table_len = ARRAY_LEN(CDI_CHUNKS) - 2; // exclude IDAT and YUVS
	const enum cdi_model model = buf_endian16b(hdr + 6);
	const uint16_t depth = buf_endian16b(hdr + 8);
	switch (model) {
	case cdi_rgb888:
		if (depth != 24) {
			return wuerr(wu_invalid_header, "depth != 24 for RGB888");
		}
		img->channels = 3;
		++iff->table; // exclude PLTE, include IDAT
		break;
	case cdi_rgb555:
		if (depth != 16) {
			return wuerr(wu_invalid_header, "depth != 16 for RGB555");
		}
		img->bitdepth = 16;
		if (!wuimg_bitfield_from_id(img, 0x555)) {
			return WUERR_HERE(wu_alloc_error);
		}
		++iff->table; // exclude PLTE, include IDAT
		break;
	case cdi_dyuv:
		if (depth != 8) {
			return wuerr(wu_invalid_header, "depth != 8 for DYUV");
		}
		// exclude PLTE, include IDAT
		++iff->table;
		desc->dyuv_start = hdr[10];
		switch (desc->dyuv_start) {
		case cdi_dyuv_one:
			memcpy(desc->yuvs.one, hdr + 11, sizeof(desc->yuvs.one));
			break;
		case cdi_dyuv_each:
			// include YUVS
			++iff->table_len;
			break;
		default:
			return wuerr(wu_invalid_header,
				"unknown YUV Start encoding");
		}
		img->channels = 3;
		img->cs.matrix = cicp_matrix_bt470_6_system_b_g;
		if (!wuimg_plane_init(img)) {
			return WUERR_HERE(wu_alloc_error);
		}
		wuimg_plane_subsamp(img, 2, 1);
		break;
	case cdi_clut8:
	case cdi_clut7:
	case cdi_rl7:
		if (depth != 8) {
			return wuerr(wu_invalid_header,
				"depth != 8 for CLUT8/CLUT7/RLE7 image");
		}
		desc->needs_plte = true;
		++iff->table_len; // include IDAT
		break;
	case cdi_clut4:
	case cdi_clut3:
		if (depth != 4) {
			return wuerr(wu_invalid_header, "depth != 4 for CLUT4/3 image");
		}
		img->bitdepth = 4;
		desc->needs_plte = true;
		++iff->table_len; // include IDAT
		break;
	case cdi_rl3:
		return wuerr(wu_samples_wanted, "image is RL3 encoded");
	case cdi_plte:
		img->w = 16;
		img->h = 16;
		img->channels = 4;
		desc->needs_plte = true;
		break;
	default:
		return wuerr(wu_invalid_header, "unknown encoding");
	}
	desc->model = model;
	return iff_next_FILE(iff, desc->ifp, chunk);
}

static const struct iff_table CDI_START_TABLE = {
	FOURCC('I', 'H', 'D', 'R'), ihdr_cdi,
};

struct wu_st cdi_init(struct cdi_desc *desc, struct wuimg *img, FILE *ifp) {
	/* CD-i IFF outline:
		FORM "IMAG"
			"IHDR"
			"IPAR"?
			"YUVS"?
			"USER"?
			"PLTE" | "IDAT" (one or both)
	*/
	const uint8_t form[4] = {'F', 'O', 'R', 'M'};
	const uint8_t imag[4] = {'I', 'M', 'A', 'G'};
	uint8_t hdr[12];
	if (!fread(hdr, sizeof(hdr), 1, ifp)) {
		return WUERR_HERE(wu_unexpected_eof);
	} else if (memcmp(hdr, form, sizeof(form))
	|| memcmp(hdr + 8, imag, sizeof(imag))) {
		return wuerr(wu_invalid_signature, "not a CD-i IFF image");
	}
	*desc = (struct cdi_desc) {
		.ifp = ifp,
		.img = img,
	};
	struct iff_state iff = {
		.table = &CDI_START_TABLE,
		.table_len = 1,
		.align_sh = 1, // just assuming
		.user = desc,
	};
	return iff_next_FILE(&iff, ifp, (struct iff_chunk){0});
}
