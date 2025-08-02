// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2019 kaleido
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include <tiffio.h>

#include "wudefs.h"
#include "misc/common.h"
#include "misc/math.h"
#include "misc/metadata.h"
#include "raster/unpack.h"

struct tiff_info {
	uint16_t photometric, spp, bps, sfmt;
	uint16_t planar;
	bool is_tiled;
	bool is_subsampled;
	uint32_t planes;
	struct tiff_ycbcr {
		float coef[3];
		uint16_t sampx, sampy;
		uint16_t pos;
	} ycbcr;
};

struct tile_info {
	uint32_t per_row, per_col;
	uint32_t width, height;
	uint32_t end_width;
	size_t stride;
	size_t end_stride;
	unsigned char *buf;
	tsize_t len;
};

static void end_tiff(struct image_file *infile) {
	TIFFCleanup(infile->dec_state);
}

static void get_metadata_tags(TIFF *tif, struct wuimg *img) {
	struct tifftag {
		ttag_t tag;
		const char *name;
	} tag[] = {
		{TIFFTAG_IMAGEDESCRIPTION, "Image description"},
		{TIFFTAG_MAKE, "Make"},
		{TIFFTAG_MODEL, "Model"},
		{TIFFTAG_SOFTWARE, "Software"},
		{TIFFTAG_DATETIME, "Datetime"},
		{TIFFTAG_ARTIST, "Artist"},
		{TIFFTAG_HOSTCOMPUTER, "Host computer"},
		{TIFFTAG_COPYRIGHT, "Copyright"},

		{TIFFTAG_DOCUMENTNAME, "Document name"},
		{TIFFTAG_PAGENAME, "Page name"},
	};

	struct wutree *tree = wuimg_get_metadata(img);
	if (!tree) {
		return;
	}

	for (size_t i = 0; i < ARRAY_LEN(tag); ++i){
		char *field;
		if (TIFFGetField(tif, tag[i].tag, &field)) {
			tree_add_leaf(tree, tag[i].name, field, NULL);
	 	}
	}

	uint32_t data_len;
	void *data;
	if (TIFFGetField(tif, TIFFTAG_XMLPACKET, &data_len, &data)) {
		metadata_parse(metadata_xmp, data, data_len, tree);
	}
	if (TIFFGetField(tif, TIFFTAG_RICHTIFFIPTC, &data_len, &data)) {
		metadata_parse(metadata_iptc, data, data_len, tree);
	}
}

// Default and safe libtiff decoding.
static struct wu_st libtiff_decode(TIFF *tif, struct image_file *infile,
const struct wu_conf *conf, struct wuimg *img) {
	TIFFRGBAImage tifimg;
	char emsg[1024];
	if (!TIFFRGBAImageBegin(&tifimg, tif, 0, emsg)) {
		image_file_strerror_append(infile, emsg);
		return WUERR_HERE(wu_unsupported_feature);
	}

	image_file_strerror_append(infile, "Using libtiff high-level interface");
	tifimg.req_orientation = tifimg.orientation;
	img->w = tifimg.width;
	img->h = tifimg.height;
	img->channels = 4;
	img->bitdepth = 8;
	if (!tifimg.alpha) {
		img->alpha = alpha_ignore;
	}

	struct wu_st st = WUERR_CHECK(wuimg_alloc_limit(img, conf));
	if (wu_isok(st)) {
		st = TIFFRGBAImageGet(&tifimg, (uint32_t *)img->data,
			tifimg.width, tifimg.height)
			? wuok() : WUERR_HERE(wu_decoding_error);
	}
	TIFFRGBAImageEnd(&tifimg);
	return st;
}



static void single_tile(unsigned char *restrict dst,
const struct tile_info *tiles, const size_t width, const size_t height,
const size_t dst_stride, const enum pix_attr attr, const enum unpack_op op,
const uint16_t bps) {
	for (size_t y = 0; y < height; ++y) {
		const unsigned char *src = tiles->buf + tiles->stride * y;
		unpack_strip(dst, src, width, (uint8_t)bps, attr, op, NULL);
		dst += dst_stride;
	}
}

static struct wu_st read_tiles(TIFF *tif, struct wuimg *img,
const struct tiff_info *info, const enum unpack_op op) {
	struct tile_info tiles;
	if (TIFFGetField(tif, TIFFTAG_TILEWIDTH, &tiles.width) != 1
	|| TIFFGetField(tif, TIFFTAG_TILELENGTH, &tiles.height) != 1) {
		return WUERR_HERE(wu_invalid_header);
	}

	tiles.len = TIFFTileSize(tif);
	tiles.buf = malloc((size_t)tiles.len);
	if (!tiles.buf) {
		return WUERR_HERE(wu_alloc_error);
	}

	const uint32_t comps = info->spp / info->planes;
	tiles.per_row = (uint32_t)zuceildiv(img->w, tiles.width);
	tiles.per_col = (uint32_t)zuceildiv(img->h, tiles.height);
	tiles.end_width = (uint32_t)img->w - tiles.width * (tiles.per_row - 1);
	tiles.stride = strip_base(tiles.width * comps, (uint8_t)info->bps);
	tiles.end_stride = strip_base(tiles.end_width * comps,
		(uint8_t)info->bps);

	const size_t u_stride = strip_base(tiles.width*comps, img->bitdepth);
	const size_t dst_stride = strip_base(img->w * comps, img->bitdepth);
	unsigned char *restrict dst = img->data;
	uint32_t ts = 0;
	for (uint32_t p = 0; p < info->planes; ++p) {
		for (uint32_t y = 0; y < tiles.per_col; ++y) {
			const size_t height = (y == tiles.per_col - 1)
				? img->h - tiles.height*y
				: tiles.height;

			for (uint32_t x = 0; x < tiles.per_row; ++x) {
				const size_t width = comps * ((x + 1 == tiles.per_row)
					? tiles.end_width : tiles.width);

				TIFFReadEncodedTile(tif, ts, tiles.buf, tiles.len);
				single_tile(dst + x*u_stride, &tiles,
					width, height, dst_stride, img->attr,
					op, info->bps);
				++ts;
			}
			dst += dst_stride * height;
		}
	}
	free(tiles.buf);
	if (op != op_noop) {
		img->attr = pix_normal;
	}
	return wuok();
}

static struct wu_st read_strips(TIFF *tif, struct wuimg *img,
const struct tiff_info *info) {
	const tsize_t buflen = TIFFStripSize(tif);

	const uint32_t total_strips = TIFFNumberOfStrips(tif);
	uint32_t rows_per_strip;
	TIFFGetFieldDefaulted(tif, TIFFTAG_ROWSPERSTRIP, &rows_per_strip);

	const uint32_t strips = total_strips / info->planes;
	const size_t stride = strip_base(img->w * (img->channels / info->planes),
		img->bitdepth);
	unsigned char *data = img->data;
	for (uint32_t p = 0; p < info->planes; ++p) {
		for (uint32_t st = 0; st < strips; ++st) {
			const size_t h = (st + 1 == strips)
				? img->h - rows_per_strip * st
				: rows_per_strip;
			const uint32_t n = strips * p + st;
			TIFFReadEncodedStrip(tif, n, data, buflen);
			data += stride * h;
		}
	}
	return wuok();
}

static bool load_palette(TIFF *tif, struct wuimg *img, uint16_t bps) {
	uint16_t *red, *green, *blue;
	if (TIFFGetField(tif, TIFFTAG_COLORMAP, &red, &green, &blue) == 1) {
		struct palette *pal = wuimg_palette_init(img);
		if (pal) {
			const size_t len = 1U << bps;
			for (size_t i = 0; i < len; ++i) {
				pal->color[i].r = (unsigned char)(red[i] >> 8);
				pal->color[i].g = (unsigned char)(green[i] >> 8);
				pal->color[i].b = (unsigned char)(blue[i] >> 8);
				pal->color[i].a = 0xff;
			}
		}
		return pal;
	}
	return NULL;
}

static struct wu_st get_color_info(TIFF *tif, struct wuimg *img,
const struct tiff_info *info) {
	uint16_t cnt;
	uint16_t *types;
	if (TIFFGetField(tif, TIFFTAG_EXTRASAMPLES, &cnt, &types) == 1 && cnt == 1) {
		switch (types[0]) {
		case EXTRASAMPLE_UNASSALPHA:
			img->alpha = alpha_unassociated;
			break;
		case EXTRASAMPLE_ASSOCALPHA:
			img->alpha = alpha_associated;
			break;
		}
	}

	if (info->sfmt == SAMPLEFORMAT_IEEEFP) {
		img->attr = pix_float;
	} else if (info->sfmt == SAMPLEFORMAT_INT) {
		img->attr = pix_signed;
	}

	switch (info->photometric) {
	case PHOTOMETRIC_MINISWHITE:
		img->attr = pix_inverted;
		break;
	case PHOTOMETRIC_PALETTE:
		if (!load_palette(tif, img, info->bps)) {
			return WUERR_HERE(wu_alloc_error);
		}
		img->bitrange = 8;
		break;
	case PHOTOMETRIC_YCBCR:
		img->cs.matrix = cicp_matrix_bt601_7;
		img->cs.limited = true;
		break;
	case PHOTOMETRIC_SEPARATED:
		img->attr = pix_inverted;
		img->alpha = alpha_key;
		break;
	}

	uint32_t len;
	void *data;
	if (TIFFGetField(tif, TIFFTAG_ICCPROFILE, &len, &data) == 1) {
		return color_space_set_icc_copy(&img->cs, data, len)
			? wuok() : WUERR_HERE(wu_alloc_error);
	}

	float *w;
	if (TIFFGetField(tif, TIFFTAG_WHITEPOINT, &w) == 1) {
		color_space_set_primaries_whitepoint(&img->cs, w[0], w[1]);
	}
	float *rgb;
	if (TIFFGetField(tif, TIFFTAG_PRIMARYCHROMATICITIES, &rgb) == 1) {
		color_space_set_primaries_rgb(&img->cs,
			rgb[0], rgb[1], rgb[2], rgb[3], rgb[4], rgb[5]);
	}
	float *refbw;
	if (TIFFGetField(tif, TIFFTAG_REFERENCEBLACKWHITE, &refbw) == 1) {
		img->cs.limited = refbw[0] >= 15.0;
	}
	return wuok();
}

static struct wu_st nih_decode(TIFF *tif, struct wuimg *img,
const struct wu_conf *conf, struct tiff_info *info) {
	uint32_t w, h;
	if (!TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &w)
	|| !TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &h)) {
		return wuerr(wu_invalid_header,
			"failed to get image dimensions");
	}
	img->w = w;
	img->h = h;
	img->channels = (unsigned char)info->spp;
	enum unpack_op op;
	if (info->is_tiled && info->bps % 8) {
		op = (info->bps > 16) ? op_pack : op_unpack;
		img->bitdepth = (info->bps > 8) ? 16 : 8;
		img->bitrange = (info->bps > 16) ? 16 : (uint8_t)info->bps;
	} else {
		op = op_noop;
		img->bitdepth = (unsigned char)info->bps;
	}

	if (info->planes > 1 && !wuimg_plane_init(img)) {
		return WUERR_HERE(wu_alloc_error);
	}

	uint16_t orientation;
	TIFFGetFieldDefaulted(tif, TIFFTAG_ORIENTATION, &orientation);
	wuimg_exif_orientation(img, orientation);

	struct wu_st st = get_color_info(tif, img, info);
	if (wu_isok(st)) {
		enum wu_error err = wuimg_alloc_limit(img, conf);
		if (err == wu_ok) {
			return (info->is_tiled)
				? read_tiles(tif, img, info, op)
				: read_strips(tif, img, info);
		}
		st = WUERR_HERE(err);
	}
	return st;
}

static bool check_support(const struct tiff_info *info) {
	/* Subsampled data is stored interleaved, and that's too much trouble
	 * for a somewhat rare case. */
	if (!info->spp || !info->bps || info->bps > 64 || info->is_subsampled) {
		return false;
	}

	switch (info->photometric) {
	case PHOTOMETRIC_MINISWHITE:
	case PHOTOMETRIC_MINISBLACK:
		if (info->spp > 2) {
			return false;
		}
		break;
	case PHOTOMETRIC_MASK:
		if (info->spp != 1 || info->bps != 1) {
			return false;
		}
		break;
	case PHOTOMETRIC_YCBCR:
	case PHOTOMETRIC_RGB:
		if (info->spp > 4) {
			return false;
		}
		break;
	case PHOTOMETRIC_PALETTE:
		if (info->spp != 1 || info->bps > 8) {
			return false;
		}
		break;
	case PHOTOMETRIC_SEPARATED: // CMYK
		if (info->spp != 4) {
			return false;
		}
		break;
	default:
		return false;
	}

	switch (info->sfmt) {
	case SAMPLEFORMAT_IEEEFP:
		switch (info->bps) {
		case 16: case 32: case 64:
			break;
		case 24:
			// 1 sign bit, 7 exponent bits, 16 mantissa bits
			// https://web.archive.org/web/20250108130838/http://chriscox.org/TIFFTN3d1.pdf
			return false;
		default:
			return false;
		}
		break;
	case SAMPLEFORMAT_UINT:
	case SAMPLEFORMAT_INT:
	case SAMPLEFORMAT_VOID:
		break;
	default:
		return false;
	}
	return true;
}

static const char * get_tiff_info(TIFF *tif, struct tiff_info *info) {
	if (TIFFGetField(tif, TIFFTAG_PHOTOMETRIC, &info->photometric) != 1) {
		return "Image lacks photometric info.";
	}
	TIFFGetFieldDefaulted(tif, TIFFTAG_SAMPLESPERPIXEL, &info->spp);
	TIFFGetFieldDefaulted(tif, TIFFTAG_BITSPERSAMPLE, &info->bps);
	TIFFGetFieldDefaulted(tif, TIFFTAG_SAMPLEFORMAT, &info->sfmt);
	TIFFGetFieldDefaulted(tif, TIFFTAG_PLANARCONFIG, &info->planar);
	info->is_tiled = TIFFIsTiled(tif);
	info->planes = (info->planar == PLANARCONFIG_CONTIG) ? 1 : info->spp;
	if (info->photometric == PHOTOMETRIC_YCBCR) {
		TIFFGetFieldDefaulted(tif, TIFFTAG_YCBCRCOEFFICIENTS,
			info->ycbcr.coef);
		TIFFGetFieldDefaulted(tif, TIFFTAG_YCBCRSUBSAMPLING,
			&info->ycbcr.sampx, &info->ycbcr.sampy);
		TIFFGetFieldDefaulted(tif, TIFFTAG_YCBCRPOSITIONING,
			&info->ycbcr.pos);
		info->is_subsampled = info->ycbcr.sampx > 1
			|| info->ycbcr.sampy > 1;
	} else {
		info->is_subsampled = false;
	}
	return NULL;
}

static struct wu_st get_dir(struct image_file *infile,
const struct wu_conf *conf, TIFF *tif, struct wuimg *img, const tdir_t i) {
	if (!TIFFSetDirectory(tif, (tdir_t)i)) {
		return wuerr(wu_invalid_header, "couldn't set TIFF directory");
	}

	struct tiff_info info;
	const char *err = get_tiff_info(tif, &info);
	if (err) {
		return wuerr(wu_invalid_header, err);
	}
	const bool do_it_ourselves = conf->tiff_use_homegrown_unpacker
		&& check_support(&info);

	struct wu_st st;
	switch ((int)do_it_ourselves) {
	case true:
		st = nih_decode(tif, img, conf, &info);
		if (wu_isok(st)) {
			break;
		}
		wuimg_clear(img);
		image_file_strerror_append(infile, "Native unpacking "
			"failed, falling back on libtiff.");
		// fallthrough
	case false:
		st = libtiff_decode(tif, infile, conf, img);
	}

	if (wu_isok(st)) {
		get_metadata_tags(tif, img);
	}
	return st;
}

static struct wu_st event_tiff(struct image_file *infile,
const struct wu_conf *conf, struct wu_state *state, const enum image_event ev) {
	TIFF *tif = infile->dec_state;
	const tdir_t idx = (tdir_t)state->idx;
	struct wuimg *img = infile->sub_img + idx;
	return (ev == ev_subcycle)
		? get_dir(infile, conf, tif, img, idx)
		: wuerr(wu_no_change, NULL);
}

static struct wu_st init_tiff(struct image_file *infile,
const struct wu_conf *_c) {
	(void)_c;
	const int fd = fileno(infile->ifp);
	// libtiff insists on knowing the filename for some of its errors.
	TIFF *tif = TIFFFdOpen(fd, "", "r");
	if (!tif) {
		return WUERR_HERE(wu_open_error);
	}

	infile->dec_state = tif;
	return alloc_sub_images(infile, TIFFNumberOfDirectories(tif))
		? wuok() : WUERR_HERE(wu_alloc_error);
}

const struct image_fn tiff_fn = {
	.init = init_tiff,
	.event = event_tiff,
	.end = end_tiff,
};
