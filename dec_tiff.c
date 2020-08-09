#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <tiffio.h>

#include "wudefs.h"
#include "common.h"
#include "common_unpack.h"

static void print_metadata_tags(TIFF *tif, FILE *meta_stream) {
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
			print_unsafe_data(field, strlen(field), tag[i].name,
				true, meta_stream);
	 	}
	}
}

// Default and safe libtiff decoding.
static enum wu_error libtiff_decode(TIFF *tif, struct image_file *infile,
struct raw_img *img) {
	TIFFRGBAImage tifimg;
	char emsg[256];
	if (!TIFFRGBAImageBegin(&tifimg, tif, 0, emsg)) {
		infile->err_msg = strdup(emsg);
		return wu_unsupported_feature;
	}

	fprintf(infile->meta.fp, "TIFF info:\n"
		"\tisTiled: %d\n"
		"\tisContig: %d\n"
		"\tAlpha: %d\n"
		"\tBits per sample: %hu\n"
		"\tSamples per pixel: %hu\n"
		"\tOrientation: %hu\n"
		"\tPhotometric: %hu\n",
		TIFFIsTiled(tif),
		tifimg.isContig, tifimg.alpha, tifimg.bitspersample,
		tifimg.samplesperpixel, tifimg.orientation,
		tifimg.photometric);

	tifimg.req_orientation = tifimg.orientation;
	img->channels = 4;
	img->bitdepth = 8;
	switch (tifimg.photometric) {
	case PHOTOMETRIC_MINISWHITE:
	case PHOTOMETRIC_MINISBLACK:
	case PHOTOMETRIC_RGB:
		if (tifimg.samplesperpixel != 2) {
			img->true_channels = (unsigned char)
				tifimg.samplesperpixel;
		}
		break;
	default:
		if (!tifimg.alpha) {
			img->true_channels = 3;
		}
	}

	const size_t dims = img->w * img->h * img->channels;
	void *raster = malloc(dims * sizeof(uint32));
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

	const size_t ch = img->channels;
	const size_t bytedepth = img->bitdepth / 8;
	const size_t dist = len / ch;
	const unsigned char *restrict data = img->data;
	for (size_t i = 0; i < img->w * img->h * bytedepth; i += bytedepth) {
		for (size_t k = 0; k < ch; ++k) {
			memcpy(out + i*ch + k, data + dist*k + i, bytedepth);
		}
	}
	free(img->data);
	img->data = out;
	return wu_ok;
}

static void copy_tile(unsigned char *restrict data,
const unsigned char *restrict buf, const size_t img_width,
const size_t pad_width, const size_t tile_width, const size_t height,
const unsigned char bps, const enum unpack_op op) {
	for (size_t i = 0; i < height; ++i) {
		if (op) {
			strip_unpack(data, buf, tile_width, 1, 1, op, bps);
		} else {
			memcpy(data, buf, tile_width);
		}
		data += img_width;
		buf += pad_width;
	}
}

static enum wu_error unpack_tiles(TIFF *tif, struct raw_img *img,
const uint32 tiles, const tsize_t buflen, const uint32 tile_width,
const uint32 tile_height, const unsigned char bps, const enum unpack_op op) {
	unsigned char *restrict buf = _TIFFmalloc(buflen);
	if (!buf) {
		return wu_alloc_error;
	}

	const uint32 tiles_per_row = ((uint32)img->w + tile_width - 1)
		/ tile_width;
	const uint32 tiles_per_col = ((uint32)img->h + tile_height - 1)
		/ tile_height;
	const uint32 bottom_row_start = tiles - tiles_per_row;

	unsigned char *restrict data = img->data;
	const uint32 mult = (uint32)(img->channels * img->bitdepth / 8);

	for (uint32 col = 0; col < tiles_per_col; ++col) {
		for (uint32 row = 0; row < tiles_per_row; ++row) {
			const uint32 ts = tiles_per_row*col + row;

			TIFFReadEncodedTile(tif, ts, buf, buflen);

			size_t width = tile_width * mult;
			size_t height = tile_height;
			if (ts % tiles_per_row == 0 && img->w % tile_width) {
				width = img->w % tile_width * mult;
			}
			if (ts >= bottom_row_start && img->h % tile_height) {
				height = img->h % tile_height;
			}

			copy_tile(data, buf, img->w * mult, tile_width * mult,
				width, height, bps, op);
			data += width;
		}
		data += img->w * (tile_height - 1) * mult;
	}
	_TIFFfree(buf);
	return wu_ok;
}

static void direct_strips_to_img(TIFF *tif, struct raw_img *img,
const uint32 strips, const tsize_t buflen) {
	tsize_t offset = 0;
	for (uint32 st = 0; st < strips; ++st) {
		TIFFReadEncodedStrip(tif, st, img->data + offset, buflen);
		offset += buflen;
	}
}

static enum wu_error unpack_strips(TIFF *tif, struct raw_img *img,
const uint32 planes, const uint32 strips, const tsize_t buflen,
const uint32 samples, const unsigned char bps, const enum unpack_op op) {
	unsigned char *restrict buf = _TIFFmalloc(buflen);
	if (!buf) {
		return wu_alloc_error;
	}
	uint32 rows_per_strip;
	TIFFGetFieldDefaulted(tif, TIFFTAG_ROWSPERSTRIP, &rows_per_strip);

	void *restrict data = img->data;
	for (uint32 p = 0; p < planes; ++p) {
		for (uint32 st = 0; st < strips; ++st) {
			uint32 pos = st + strips * p;

			/* This function returns -1 in case of errors, but even
			 * libtiff seems to ignore it */
			TIFFReadEncodedStrip(tif, pos, buf, buflen);

			size_t height;
			if (st == strips - 1 && img->h % rows_per_strip) {
				height = img->h % rows_per_strip;
			} else {
				height = rows_per_strip;
			}
			data = strip_unpack(data, buf, img->w * samples / planes,
				height, 1, op, bps);
		}
	}
	_TIFFfree(buf);
	return wu_ok;
}

static unsigned char * load_palette(TIFF *tif, unsigned char bps) {
	u_int16_t *red, *green, *blue;
	if (TIFFGetField(tif, TIFFTAG_COLORMAP, &red, &green, &blue)) {
		struct colormap *pal = malloc(sizeof(struct colormap) * 256);
		if (pal) {
			const size_t len = 1U << bps;
			for (size_t i = 0; i < len; ++i) {
				pal[i].r = (unsigned char)(red[i] >> 8);
				pal[i].g = (unsigned char)(green[i] >> 8);
				pal[i].b = (unsigned char)(blue[i] >> 8);
				pal[i].a = 0xff;
			}
		}
		return (unsigned char *)pal;
	}
	return NULL;
}
static enum unpack_op select_filter(const uint16 photometric,
const unsigned char channels, const unsigned char bits_per_sample,
const uint16 planar, const bool is_floating) {
	enum unpack_op op = noop;
	switch (bits_per_sample) {
	case 1: case 2: case 4: case 24:
		switch (photometric) {
		case PHOTOMETRIC_MINISWHITE:
			op = expand_invert;
			break;
		case PHOTOMETRIC_MINISBLACK:
		case PHOTOMETRIC_RGB:
			if (bits_per_sample == 4 && channels == 4
			&& planar == PLANARCONFIG_CONTIG) {
				// GL_RGBA4 with GL_UNSIGNED_SHORT_4_4_4_4
				op = noop;
			} else {
				op = expand;
			}
			break;
		case PHOTOMETRIC_PALETTE:
			op = unpack;
		}
		break;
	case 64:
		op = is_floating ? pack_float : pack;
		break;
	}
	return op;
}

static enum wu_error nih_decode(TIFF *tif, struct raw_img *img,
const uint16 photometric, const unsigned char spp, const unsigned char bps,
const uint16 sampleformat) {
	uint16 planar;
	TIFFGetFieldDefaulted(tif, TIFFTAG_PLANARCONFIG, &planar);

	const bool is_floating = (sampleformat == SAMPLEFORMAT_IEEEFP);

	// NULL means we can use the data as is.
	const enum unpack_op op = select_filter(photometric, spp, bps, planar,
		is_floating);
	if (op) {
		if (bps == 24) {
			img->bitdepth = 32;
		} else {
			img->bitdepth = (unsigned char)iclamp(bps, 8, 32);
		}
	} else {
		img->bitdepth = bps;
	}

	if (photometric == PHOTOMETRIC_PALETTE) {
		img->channels = 3;
		img->palette = load_palette(tif, bps);
		if (!img->palette) {
			return wu_alloc_error;
		}
	} else {
		img->channels = (unsigned char)spp;
	}
	img->float_data = is_floating;

	const size_t datasize = img->w * img->h * img->channels
		* img->bitdepth / 8;
	img->data = malloc(datasize*4);
	if (!img->data) {
		free(img->palette);
		img->palette = NULL;
		return wu_alloc_error;
	}

	enum wu_error status;
	if (TIFFIsTiled(tif)) {
		const uint32 tiles = TIFFNumberOfTiles(tif);
		const tsize_t tile_size = TIFFTileSize(tif);
		uint32 tile_width, tile_height;
		TIFFGetField(tif, TIFFTAG_TILEWIDTH, &tile_width);
		TIFFGetField(tif, TIFFTAG_TILELENGTH, &tile_height);
		status = unpack_tiles(tif, img, tiles, tile_size, tile_width,
			tile_height, bps, op);
	} else {
		const uint32 strips = TIFFNumberOfStrips(tif);
		const tsize_t strip_size = TIFFStripSize(tif);
		uint32 planes;
		if (planar != PLANARCONFIG_CONTIG) {
			planes = spp;
		} else {
			planes = 1;
		}
		if (op) {
			status = unpack_strips(tif, img, planes, strips/planes,
				strip_size, spp, bps, op);
		} else {
			direct_strips_to_img(tif, img, strips, strip_size);
			if (photometric == PHOTOMETRIC_MINISWHITE) {
				strip_invert8(img->data, datasize);
			}
			status = wu_ok;
		}
	}

	if (status == wu_ok && planar != PLANARCONFIG_CONTIG) {
		status = interleave_planes(img, datasize);
	}
	return status;
}

static bool check_support(const uint16 photometric,
const uint16 samples_per_pixel, const uint16 bits_per_pixel,
const uint16 sampleformat) {
	switch (photometric) {
	case PHOTOMETRIC_MINISWHITE:
	case PHOTOMETRIC_MINISBLACK:
		if (samples_per_pixel != 1 && samples_per_pixel != 2) {
			return false;
		}
		break;
	case PHOTOMETRIC_RGB:
		if (samples_per_pixel != 3 && samples_per_pixel != 4) {
			return false;
		}
		break;
	case PHOTOMETRIC_PALETTE:
		if (samples_per_pixel != 1 || bits_per_pixel > 8) {
			return false;
		}
		break;
	default:
		return false;
	}

	switch (bits_per_pixel) {
	case 1: case 2: case 4: case 8: case 24:
		if (sampleformat == SAMPLEFORMAT_IEEEFP) {
			return false;
		}
		break;
	case 16: case 32: case 64:
		break;
	default:
		return false;
	}

	switch (sampleformat) {
	case SAMPLEFORMAT_UINT:
	case SAMPLEFORMAT_INT:
	case SAMPLEFORMAT_IEEEFP:
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

	print_metadata_tags(tif, infile->meta.fp);
	struct raw_img *img = alloc_sub_images(infile,
		TIFFNumberOfDirectories(tif));

	enum wu_error status;
	size_t i = 0;
	do {
		TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &img[i].w);
		TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &img[i].h);
		if (zumax(img[i].w, img[i].h) > wuconf->max_img_size) {
			status = wu_exceeded_size_limit;
			continue;
		}

		uint16 photometric, spp, bps, sfmt;
		TIFFGetField(tif, TIFFTAG_PHOTOMETRIC, &photometric);
		TIFFGetFieldDefaulted(tif, TIFFTAG_SAMPLESPERPIXEL, &spp);
		TIFFGetFieldDefaulted(tif, TIFFTAG_BITSPERSAMPLE, &bps);
		TIFFGetFieldDefaulted(tif, TIFFTAG_SAMPLEFORMAT, &sfmt);

		bool do_it_ourselves;
		if (wuconf->tiff_use_homegrown_unpacker) {
			do_it_ourselves = check_support(photometric, spp, bps,
				sfmt);
		} else {
			do_it_ourselves = false;
		}

		switch ((int)do_it_ourselves) {
		case true:
			status = nih_decode(tif, img + i, photometric,
				(unsigned char)spp, (unsigned char)bps, sfmt);
			if (status == wu_ok) {
				break;
			}
			free(img->palette);
			free(img->data);
			img->palette = NULL;
			img->data = NULL;
			puts("Native unpacking routine failed, falling back "
				"on libtiff.");
			// Fallthrough
		default:
			status = libtiff_decode(tif, infile, &img[i]);
			if (status != wu_ok) {
				free(img->palette);
				free(img->data);
				img->palette = NULL;
				img->data = NULL;
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
