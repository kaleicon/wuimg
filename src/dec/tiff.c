#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <tiffio.h>

#include "../wudefs.h"
#include "../common.h"
#include "../metadata.h"
#include "../raster/unpack.h"

struct tiff_info {
	uint16_t photometric, spp, bps, sfmt;
	uint16_t planar;
	bool is_tiled;
	uint32_t planes;
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

static void print_metadata_tags(TIFF *tif, struct wu_tree *tree) {
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

	for (size_t i = 0; i < ARRAY_LEN(tag); ++i){
		char *field;
		if (TIFFGetField(tif, tag[i].tag, &field)) {
			tree_sprout_unsafe_leaf(tree, tag[i].name, field,
				strlen(field));
	 	}
	}

	uint32_t data_len;
	void *data;
	if (TIFFGetField(tif, TIFFTAG_XMLPACKET, &data_len, &data)) {
		standard_metadata(xmp_metadata, data, data_len, tree);
	}
	if (TIFFGetField(tif, TIFFTAG_RICHTIFFIPTC, &data_len, &data)) {
		standard_metadata(iptc_metadata, data, data_len, tree);
	}
}

// Default and safe libtiff decoding.
static enum wu_error libtiff_decode(TIFF *tif, struct image_file *infile,
struct raw_img *img) {
	TIFFRGBAImage tifimg;
	char emsg[1024];
	if (!TIFFRGBAImageBegin(&tifimg, tif, 0, emsg)) {
		infile->err_msg = strdup(emsg);
		return wu_unsupported_feature;
	}

	puts("Using libtiff high-level interface.");
	tifimg.req_orientation = tifimg.orientation;
	img->w = tifimg.width;
	img->h = tifimg.height;
	img->channels = 4;
	img->bitdepth = 8;
	switch (tifimg.photometric) {
	case PHOTOMETRIC_MINISWHITE:
	case PHOTOMETRIC_MINISBLACK:
	case PHOTOMETRIC_RGB:
		if (tifimg.samplesperpixel != 2) {
			img->true_channels = (unsigned char)
				(umin(img->channels, tifimg.samplesperpixel));
		}
		break;
	default:
		if (!tifimg.alpha) {
			img->true_channels = 3;
		}
	}

	const size_t dims = img->w * img->h * img->channels;
	void *raster = malloc(dims);
	if (!raster) {
		TIFFRGBAImageEnd(&tifimg);
		return wu_alloc_error;
	}

	const int result = TIFFRGBAImageGet(&tifimg, raster, tifimg.width,
		tifimg.height);
	TIFFRGBAImageEnd(&tifimg);
	if (!result) {
		free(raster);
		return wu_decoding_error;
	}

	img->data = raster;
	return wu_ok;
}

static enum wu_error interleave_planes(struct raw_img *img, const size_t len) {
	unsigned char *out = malloc(len);
	if (!out) {
		return wu_alloc_error;
	}

	strip_interleave(out, img->data, img->w*img->h, img->channels, img->bitdepth);
	free(img->data);
	img->data = out;
	return wu_ok;
}

static void single_tile(unsigned char *restrict data,
const struct tile_info *tiles, const size_t width, const size_t height,
const size_t img_stride, const size_t cur_stride, const enum unpack_op op,
const uint16_t bps) {
	for (size_t h = 0; h < height; ++h) {
		unsigned char *dst = data + img_stride * h;
		const unsigned char *src = tiles->buf + tiles->stride * h;
		if (op) {
			strip_unpack(dst, src, width, 1, 1, op, bps);
		} else {
			memcpy(dst, src, cur_stride);
		}
	}
}

static enum wu_error unpack_tiles(TIFF *tif, struct raw_img *img,
const struct tiff_info *info, const enum unpack_op op) {
	struct tile_info tiles;
	const bool ok = TIFFGetField(tif, TIFFTAG_TILEWIDTH, &tiles.width) == 1
		&& TIFFGetField(tif, TIFFTAG_TILELENGTH, &tiles.height) == 1;
	if (!ok) {
		return wu_invalid_header;
	}

	tiles.len = TIFFTileSize(tif);
	tiles.buf = _TIFFmalloc(tiles.len);
	if (!tiles.buf) {
		return wu_alloc_error;
	}

	const uint32_t comps = info->spp / info->planes;
	tiles.per_row = ((uint32_t)img->w + tiles.width - 1) / tiles.width;
	tiles.per_col = ((uint32_t)img->h + tiles.height - 1) / tiles.height;
	tiles.end_width = (uint32_t)img->w - tiles.width * (tiles.per_row - 1);
	tiles.stride = scanline_length(tiles.width * comps, info->bps, 1);
	tiles.end_stride = scanline_length(tiles.end_width * comps,
		info->bps, 1);

	const size_t img_stride = scanline_length(img->w * comps,
		img->bitdepth, 1);
	const size_t normal_stride = scanline_length(tiles.width * comps,
		img->bitdepth, 1);
	const size_t end_stride = scanline_length(tiles.end_width * comps,
		img->bitdepth, 1);

	unsigned char *restrict data = img->data;
	uint32_t ts = 0;
	for (uint32_t p = 0; p < info->planes; ++p) {
		for (uint32_t y = 0; y < tiles.per_col; ++y) {
			const size_t height = (y == tiles.per_col - 1)
				? img->h - tiles.height*y
				: tiles.height;
			for (uint32_t x = 0; x < tiles.per_row; ++x) {
				TIFFReadEncodedTile(tif, ts, tiles.buf, tiles.len);

				size_t width;
				size_t stride;
				size_t out_stride;
				if (x == tiles.per_row - 1) {
					width = tiles.end_width;
					stride = tiles.end_stride;
					out_stride = end_stride;
				} else {
					width = tiles.width;
					stride = tiles.stride;
					out_stride = normal_stride;
				}
				single_tile(data, &tiles, width * comps, height,
					img_stride, stride, op, info->bps);
				data += out_stride;
				++ts;
			}
			data += img_stride * (height - 1);
		}
	}
	_TIFFfree(tiles.buf);
	return wu_ok;
}

static enum wu_error unpack_strips(TIFF *tif, struct raw_img *img,
const struct tiff_info *info, const enum unpack_op op) {
	const tsize_t buflen = TIFFStripSize(tif);
	unsigned char *restrict buf; // Only used when op != op_noop
	if (op) {
		buf = _TIFFmalloc(buflen);
		if (!buf) {
			return wu_alloc_error;
		}
	}

	const uint32_t strips = TIFFNumberOfStrips(tif) / info->planes;
	uint32_t rows_per_strip;
	TIFFGetFieldDefaulted(tif, TIFFTAG_ROWSPERSTRIP, &rows_per_strip);
	const size_t end_row = img->h - rows_per_strip * (strips - 1);

	const size_t width = img->w * img->channels / info->planes;
	const size_t stride = scanline_length(width, img->bitdepth, 1);
	size_t offset = 0;
	for (uint32_t p = 0; p < info->planes; ++p) {
		for (uint32_t st = 0; st < strips; ++st) {
			const size_t rows = (st == strips - 1)
				? end_row
				: rows_per_strip;
			const uint32_t n = strips * p + st;
			if (op) {
				/* This function returns -1 in case of errors,
				 * but even libtiff seems to ignore it */
				TIFFReadEncodedStrip(tif, n, buf, buflen);
				strip_unpack(img->data + offset, buf, width,
					rows, 1, op, info->bps);
			} else {
				TIFFReadEncodedStrip(tif, n, img->data + offset,
					buflen);
			}
			offset += stride * rows;
		}
	}

	if (op) {
		_TIFFfree(buf);
	}
	return wu_ok;
}

static struct raster_pal * load_palette(TIFF *tif, uint16_t bps) {
	uint16_t *red, *green, *blue;
	if (TIFFGetField(tif, TIFFTAG_COLORMAP, &red, &green, &blue) == 1) {
		struct raster_pal *pal = malloc(sizeof(*pal) * 256);
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

static enum unpack_op select_filter(const struct tiff_info *info) {
	enum unpack_op op = op_noop;
	if (info->bps < 8) {
		switch (info->photometric) {
		case PHOTOMETRIC_MINISWHITE:
			if (info->is_tiled || info->planar != PLANARCONFIG_CONTIG) {
				op = op_expand_invert;
			}
			break;
		case PHOTOMETRIC_MINISBLACK:
		case PHOTOMETRIC_RGB:
			if (info->is_tiled || info->planar != PLANARCONFIG_CONTIG) {
				op = op_expand;
			}
			break;
		}
	}
	return op;
}

static enum wu_error nih_decode(TIFF *tif, struct raw_img *img,
struct tiff_info *info) {
	if (info->photometric == PHOTOMETRIC_PALETTE) {
		img->palette = load_palette(tif, info->bps);
		if (!img->palette) {
			return wu_alloc_error;
		}
	}
	img->channels = (unsigned char)info->spp;
	img->bitdepth = (unsigned char)info->bps;
	if (info->sfmt == SAMPLEFORMAT_IEEEFP) {
		img->attr |= pix_float;
	}

	TIFFGetFieldDefaulted(tif, TIFFTAG_PLANARCONFIG, &info->planar);
	info->is_tiled = TIFFIsTiled(tif);
	info->planes = info->planar == PLANARCONFIG_CONTIG ? 1 : info->spp;

	const enum unpack_op op = select_filter(info);
	if (op) {
		img->bitdepth = (unsigned char)imax(info->bps, 8);
	} else {
		if (info->photometric == PHOTOMETRIC_MINISWHITE) {
			img->attr |= pix_inverted;
		}
	}

	const size_t stride = raw_img_addbuf(img);
	if (!stride) {
		return wu_alloc_error;
	}

	enum wu_error status = wu_ok;
	if (info->is_tiled) {
		status = unpack_tiles(tif, img, info, op);
	} else {
		status = unpack_strips(tif, img, info, op);
	}
	if (status == wu_ok && info->planar != PLANARCONFIG_CONTIG) {
		status = interleave_planes(img, stride * img->h);
	}
	return status;
}

static bool check_support(const struct tiff_info *info) {
	switch (info->photometric) {
	case PHOTOMETRIC_MINISWHITE:
	case PHOTOMETRIC_MINISBLACK:
		if (info->spp != 1 && info->spp != 2) {
			return false;
		}
		break;
	case PHOTOMETRIC_RGB:
		if (info->spp != 3 && info->spp != 4) {
			return false;
		}
		break;
	case PHOTOMETRIC_PALETTE:
		if (info->spp != 1 || info->bps > 8) {
			return false;
		}
		break;
	default:
		return false;
	}

	switch (info->bps) {
	case 1: case 2: case 4: case 8: case 24:
		// With TIFF, literally anything can happen
		if (info->sfmt == SAMPLEFORMAT_IEEEFP) {
			return false;
		}
		break;
	case 16: case 32: case 64:
		break;
	default:
		return false;
	}

	switch (info->sfmt) {
	case SAMPLEFORMAT_UINT:
	case SAMPLEFORMAT_INT:
	case SAMPLEFORMAT_IEEEFP:
	case SAMPLEFORMAT_VOID:
		break;
	default:
		return false;
	}
	return true;
}

enum wu_error tiff_dec(struct image_file *infile,
const struct wu_conf *wuconf) {
	const int fd = fileno(infile->ifp);
	// libtiff insists on knowing the filename for some of its errors.
	TIFF *tif = TIFFFdOpen(fd, "", "r");
	if (!tif) {
		return wu_open_error;
	}

	print_metadata_tags(tif, &infile->metadata);
	struct raw_img *img = alloc_sub_images(infile,
		TIFFNumberOfDirectories(tif));

	enum wu_error status;
	size_t i = 0;
	do {
		const bool ok = TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &img[i].w) == 1
			&& TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &img[i].h) == 1;
		if (!ok) {
			puts("Failed to get image dimensions");
			status = wu_invalid_header;
			continue;
		} else if (zumax(img[i].w, img[i].h) > wuconf->max_img_size) {
			status = wu_exceeds_size_limit;
			continue;
		}

		struct tiff_info info;
		if (TIFFGetField(tif, TIFFTAG_PHOTOMETRIC, &info.photometric) != 1) {
			puts("Image lacks photometric info.");
			status = wu_invalid_header;
			continue;
		}
		TIFFGetFieldDefaulted(tif, TIFFTAG_SAMPLESPERPIXEL, &info.spp);
		TIFFGetFieldDefaulted(tif, TIFFTAG_BITSPERSAMPLE, &info.bps);
		TIFFGetFieldDefaulted(tif, TIFFTAG_SAMPLEFORMAT, &info.sfmt);

		bool do_it_ourselves;
		if (wuconf->tiff_use_homegrown_unpacker) {
			do_it_ourselves = check_support(&info);
		} else {
			do_it_ourselves = false;
		}

		switch ((int)do_it_ourselves) {
		case true:
			status = nih_decode(tif, img + i, &info);
			if (status == wu_ok) {
				break;
			}
			raw_img_clear(img + i);
			puts("Native unpacking routine failed, falling back "
				"on libtiff.");
			// fallthrough
		default:
			status = libtiff_decode(tif, infile, img + i);
			if (status != wu_ok) {
				raw_img_clear(img + i);
				continue;
			}
		}

		++i;
	} while (TIFFReadDirectory(tif) && i < infile->nr);

	TIFFCleanup(tif);
	if (status == wu_ok && i < infile->nr) {
		realloc_sub_images(infile, i);
	}
	return status;
}
