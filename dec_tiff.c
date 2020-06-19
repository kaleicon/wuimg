#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <tiffio.h>

#include "wudefs.h"
#include "common.h"
#include "common_unpack.h"

struct tifftag {
	ttag_t tag;
	const char *name;
};

static void print_metadata_tags(TIFF *tif) {
	const struct tifftag tag[] = {
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
				true);
	 	}
	}
}

// Default and safe libtiff decoding.
static enum wu_error libtiff_decode(TIFF *tif, char * restrict * err_msg,
struct raw_img *img) {
	TIFFRGBAImage tifimg;
	char emsg[1024];
	if (!TIFFRGBAImageBegin(&tifimg, tif, 0, emsg)) {
		*err_msg = strdup(emsg);
		return wu_unsupported_feature;
	}

	printf("TIFF info:\n"
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

	const size_t dims = img->w * img->h;
	uint32 *raster = _TIFFmalloc((tmsize_t)(dims * sizeof(uint32)));
	if (!raster) {
		TIFFRGBAImageEnd(&tifimg);
		return wu_alloc_error;
	}

	const int result = TIFFRGBAImageGet(&tifimg, raster,
		tifimg.width, tifimg.height);
	TIFFRGBAImageEnd(&tifimg);
	if (!result) {
		_TIFFfree(raster);
		return wu_decoding_error;
	}

	img->data = (unsigned char *)raster;
	return wu_ok;
}

static void copy_tile(unsigned char *restrict data, unsigned char *restrict buf,
const size_t img_width, const size_t pad_width, const size_t tile_width,
const size_t height, const unpack_func_t func) {
	for (size_t i = 0; i < height; ++i) {
		if (func) {
			(*func)(data, buf, tile_width, 1, 1);
		} else {
			memcpy(data, buf, tile_width);
		}
		data += img_width;
		buf += pad_width;
	}
}

static enum wu_error unpack_tiles(TIFF *tif, struct raw_img *img,
const uint32 tiles, const tsize_t buflen, const uint32 tile_width,
const uint32 tile_height, const unpack_func_t func) {
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
				width, height, func);
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
const uint32 strips, const tsize_t buflen, const unpack_func_t func) {
	unsigned char *restrict buf = _TIFFmalloc(buflen);
	if (!buf) {
		return wu_alloc_error;
	}
	uint32 rows_per_strip;
	TIFFGetFieldDefaulted(tif, TIFFTAG_ROWSPERSTRIP, &rows_per_strip);

	unsigned char *restrict data = img->data;
	for (uint32 st = 0; st < strips; ++st) {
		/* This function returns -1 in case of errors, but even libtiff
		 * seems to ignore it when decoding text.tif, one of its test
		 * images. */
		TIFFReadEncodedStrip(tif, st, buf, buflen);

		size_t height;
		if (st == strips - 1 && img->h % rows_per_strip) {
			height = img->h % rows_per_strip;
		} else {
			height = rows_per_strip;
		}
		data = (*func)(data, buf, img->w * img->channels, height, 1);
	}
	_TIFFfree(buf);
	return wu_ok;
}

static unsigned char * load_palette(TIFF *tif, unsigned char bps) {
	u_int16_t *red, *green, *blue;
	if (TIFFGetField(tif, TIFFTAG_COLORMAP, &red, &green, &blue)) {
		struct colormap *pal = malloc(
			sizeof(struct colormap) * (size_t)(1 << bps));
		if (pal) {
			for (int i = 0; i < (1 << bps); ++i) {
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
static unpack_func_t select_unpack_func(const uint16 photometric,
const unsigned char channels, const unsigned char bits_per_sample) {
	switch (photometric) {
	case PHOTOMETRIC_MINISWHITE:
		switch (bits_per_sample) {
		case 1: return strip_invert1;
		case 2: return strip_invert2;
		case 4: return strip_invert4;
		}
		break;
	case PHOTOMETRIC_MINISBLACK:
	case PHOTOMETRIC_RGB:
		switch (bits_per_sample) {
		case 1: return strip_expand1;
		case 2: return strip_expand2;
		case 4:
			if (channels == 4) {
				// GL_RGBA4 with GL_UNSIGNED_SHORT_4_4_4_4
				return NULL;
			}
			return strip_expand4;
		}
		break;
	case PHOTOMETRIC_PALETTE:
		switch (bits_per_sample) {
		case 1: return strip_unpack1;
		case 2: return strip_unpack2;
		case 4: return strip_unpack4;
		}
	}
	return NULL;
}

static enum wu_error nih_decode(TIFF *tif, struct raw_img *img,
const uint16 photometric, const unsigned char bps) {
	// NULL means we can use the data as is.
	const unpack_func_t func = select_unpack_func(photometric,
		img->channels, bps);
	if (func) {
		img->bitdepth = 8;
	} else {
		img->bitdepth = bps;
	}

	if (photometric == PHOTOMETRIC_PALETTE) {
		img->palette = load_palette(tif, bps);
		if (!img->palette) {
			return wu_alloc_error;
		}
		img->true_channels = 3;
	}

	const size_t datasize = img->w * img->h * img->channels
		* img->bitdepth / 8;
	img->data = malloc(datasize);
	if (!img->data) {
		return wu_alloc_error;
	}

	if (TIFFIsTiled(tif)) {
		const uint32 tiles = TIFFNumberOfTiles(tif);
		const tsize_t tile_size = TIFFTileSize(tif);
		uint32 tile_width, tile_height;
		TIFFGetField(tif, TIFFTAG_TILEWIDTH, &tile_width);
		TIFFGetField(tif, TIFFTAG_TILELENGTH, &tile_height);
		return unpack_tiles(tif, img, tiles, tile_size, tile_width,
			tile_height, func);
	} else {
		const uint32 strips = TIFFNumberOfStrips(tif);
		const tsize_t strip_size = TIFFStripSize(tif);
		if (func) {
			return unpack_strips(tif, img, strips, strip_size,
				func);
		} else {
			direct_strips_to_img(tif, img, strips, strip_size);
			if (photometric == PHOTOMETRIC_MINISWHITE) {
				strip_invert8(img->data, datasize);
			}
		}
	}
	return wu_ok;
}

static bool check_support(TIFF *tif, const uint16 photometric,
const uint16 samples_per_pixel, const uint16 bits_per_pixel) {
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
	case 1: case 2: case 4: case 8: case 16: case 32:
		break;
	default:
		return false;
	}

	uint16 planar;
	TIFFGetFieldDefaulted(tif, TIFFTAG_PLANARCONFIG, &planar);
	if (planar != PLANARCONFIG_CONTIG) {
		return false;
	}

	uint16 sampleformat;
	TIFFGetFieldDefaulted(tif, TIFFTAG_SAMPLEFORMAT, &sampleformat);
	switch (sampleformat) {
	case SAMPLEFORMAT_UINT:
		break;
	default:
		return false;
	}

	return true;
}

enum wu_error tiff_dec(struct image_file *infile,
const struct wu_conf *wuconf) {
	const int fd = fileno(infile->ifp);
	// libtiff insists on having the filename for some of its errors.
	TIFF *tif = TIFFFdOpen(fd, "", "r");
	if (!tif) {
		return wu_open_error;
	}

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

		uint16 photometric, spp, bps;
		TIFFGetField(tif, TIFFTAG_PHOTOMETRIC, &photometric);
		TIFFGetFieldDefaulted(tif, TIFFTAG_SAMPLESPERPIXEL, &spp);
		TIFFGetFieldDefaulted(tif, TIFFTAG_BITSPERSAMPLE, &bps);

		bool do_it_ourselves;
		if (wuconf->tiff_use_homegrown_unpackers) {
			do_it_ourselves = check_support(tif, photometric, spp,
				bps);
		} else {
			do_it_ourselves = false;
		}

		switch (do_it_ourselves) {
		case true:
			img[i].channels = (unsigned char)spp;
			status = nih_decode(tif, &img[i], photometric,
				(unsigned char)bps);
			if (status == wu_ok) {
				break;
			} else {
				puts("Native unpacking routine failed,"
					" falling back on libtiff.");
				free(img[i].data);
			}
			// Fallthrough
		default:
			status = libtiff_decode(tif, &infile->err_msg, &img[i]);
		}

		if (status) {
			continue;
		}

		++i;
	} while (TIFFReadDirectory(tif) && i < infile->nr);

	if (i < infile->nr) {
		realloc_sub_images(infile, i);
	}

	print_metadata_tags(tif);
	TIFFCleanup(tif);
	return status;
}
