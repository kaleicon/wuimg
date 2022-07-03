#include <stdlib.h>
#include <string.h>
#include <limits.h>

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

static void get_metadata_tags(TIFF *tif, struct wu_tree *tree) {
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
		image_file_error_append(infile, emsg);
		return wu_unsupported_feature;
	}

	image_file_error_append(infile, "Using libtiff high-level interface");
	tifimg.req_orientation = tifimg.orientation;
	img->w = tifimg.width;
	img->h = tifimg.height;
	img->channels = 4;
	img->bitdepth = 8;
	if (!tifimg.alpha) {
		img->alpha = alpha_ignore;
	}

	enum wu_error st = raw_img_alloc(img);
	if (st == wu_ok) {
		st = TIFFRGBAImageGet(&tifimg, (uint32_t *)img->data,
			tifimg.width, tifimg.height)
			? wu_ok : wu_decoding_error;
	}
	TIFFRGBAImageEnd(&tifimg);
	return st;
}

static void single_tile(unsigned char *restrict dst,
const struct tile_info *tiles, const size_t width, const size_t height,
const size_t dst_stride, const enum pix_attr attr, const enum unpack_op op,
const uint16_t bps) {
	for (size_t h = 0; h < height; ++h) {
		const unsigned char *src = tiles->buf + tiles->stride * h;
		unpack_or_copy_strip(dst, src, width, (uint8_t)bps,
			attr, op);
		dst += dst_stride;
	}
}

static enum wu_error read_tiles(TIFF *tif, struct raw_img *img,
const struct tiff_info *info, const enum unpack_op op) {
	struct tile_info tiles;
	if (TIFFGetField(tif, TIFFTAG_TILEWIDTH, &tiles.width) != 1
	|| TIFFGetField(tif, TIFFTAG_TILELENGTH, &tiles.height) != 1) {
		return wu_invalid_header;
	}

	tiles.len = TIFFTileSize(tif);
	tiles.buf = malloc((size_t)tiles.len);
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

	const size_t dst_stride = scanline_length(img->w * comps,
		img->bitdepth, 1);
	unsigned char *restrict dst = img->data;
	uint32_t ts = 0;
	for (uint32_t p = 0; p < info->planes; ++p) {
		size_t height = tiles.height;
		for (uint32_t y = 0; y < tiles.per_col; ++y) {
			if (y == tiles.per_col - 1) {
				height = img->h - tiles.height*y;
			}

			size_t src_width = tiles.width;
			for (uint32_t x = 0; x < tiles.per_row; ++x) {
				if (x == tiles.per_row - 1) {
					src_width = tiles.end_width;
				}

				TIFFReadEncodedTile(tif, ts, tiles.buf, tiles.len);
				single_tile(dst + x*tiles.stride, &tiles,
					src_width * comps, height, dst_stride,
					img->attr, op, info->bps);
				++ts;
			}
			dst += dst_stride * height;
		}
	}
	free(tiles.buf);
	if (op != op_noop && img->attr == pix_inverted) {
		img->attr = pix_normal;
	}
	return wu_ok;
}

static enum wu_error read_strips(TIFF *tif, struct raw_img *img,
const struct tiff_info *info) {
	const tsize_t buflen = TIFFStripSize(tif);

	const uint32_t strips = TIFFNumberOfStrips(tif) / info->planes;
	uint32_t strip_height;
	TIFFGetFieldDefaulted(tif, TIFFTAG_ROWSPERSTRIP, &strip_height);
	const size_t end_height = img->h - strip_height * (strips - 1);

	unsigned char *data = img->data;
	for (uint32_t p = 0; p < info->planes; ++p) {
		const size_t width = (info->planes > 1)
			? img->u.planes->p[p].w
			: img->w * img->channels;
		const size_t stride = scanline_length(width, img->bitdepth, 1);
		for (uint32_t st = 0; st < strips; ++st) {
			const size_t height = (st == strips - 1)
				? end_height
				: strip_height;
			const uint32_t n = strips * p + st;
			TIFFReadEncodedStrip(tif, n, data, buflen);
			data += stride * height;
		}
	}
	return wu_ok;
}

static struct raster_pal * load_palette(TIFF *tif, uint16_t bps) {
	uint16_t *red, *green, *blue;
	if (TIFFGetField(tif, TIFFTAG_COLORMAP, &red, &green, &blue) == 1) {
		struct raster_pal *pal = malloc(sizeof(*pal));
		if (pal) {
			const size_t len = 1U << bps;
			for (size_t i = 0; i < len; ++i) {
				pal->color[i].r = (unsigned char)(red[i] >> 8);
				pal->color[i].g = (unsigned char)(green[i] >> 8);
				pal->color[i].b = (unsigned char)(blue[i] >> 8);
			}
		}
		return pal;
	}
	return NULL;
}

static enum wu_error nih_decode(TIFF *tif, struct raw_img *img,
struct tiff_info *info) {
	img->channels = (unsigned char)info->spp;
	enum unpack_op op;
	if (info->is_tiled && info->bps % 8) {
		op = op_expand;
		img->bitdepth = (info->bps > 8) ? 16 : 8;
	} else {
		op = op_noop;
		img->bitdepth = (unsigned char)info->bps;
	}

	if (info->planes > 1) {
		if (!raw_img_plane_init(img)) {
			return wu_alloc_error;
		}
	}

	switch (info->photometric) {
	case PHOTOMETRIC_MINISWHITE:
		img->attr = pix_inverted;
		break;
	case PHOTOMETRIC_PALETTE:
		if (!raw_img_set_palette(img, load_palette(tif, info->bps))) {
			return wu_alloc_error;
		}
		img->alpha = alpha_ignore;
		break;
	case PHOTOMETRIC_YCBCR:
		raw_img_plane_subsamp(img, (uint8_t)info->ycbcr.sampx,
			(uint8_t)info->ycbcr.sampy);
		img->cs.matrix = cicp_matrix_bt601_7;
		break;
	}
	if (info->sfmt == SAMPLEFORMAT_IEEEFP) {
		// What about float + miniswhite?
		img->attr = pix_float;
	} else if (info->sfmt == SAMPLEFORMAT_INT) {
		img->attr = pix_signed;
	}
	uint16_t cnt;
	uint16_t *types;
	if (TIFFGetField(tif, TIFFTAG_EXTRASAMPLES, &cnt, &types) && cnt == 1) {
		switch (types[0]) {
		case EXTRASAMPLE_UNASSALPHA:
			img->alpha = alpha_unassociated;
			break;
		case EXTRASAMPLE_ASSOCALPHA:
			img->alpha = alpha_associated;
			break;
		}
	}

	const enum wu_error st = raw_img_alloc(img);
	if (st == wu_ok) {
		return (info->is_tiled)
			? read_tiles(tif, img, info, op)
			: read_strips(tif, img, info);
	}
	return st;
}

static bool check_support(const struct tiff_info *info) {
	if (!info->spp || !info->bps || info->bps > 64) {
		return false;
	}

	switch (info->photometric) {
	case PHOTOMETRIC_MINISWHITE:
	case PHOTOMETRIC_MINISBLACK:
		if (info->spp > 2) {
			return false;
		}
		break;
	case PHOTOMETRIC_YCBCR:
		if (info->planar != PLANARCONFIG_SEPARATE || info->is_tiled
		|| info->ycbcr.pos != YCBCRPOSITION_CENTERED) {
			return false;
		}
		// fallthrough
	case PHOTOMETRIC_RGB:
	case PHOTOMETRIC_MASK:
		if (info->spp > 4) {
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

	switch (info->sfmt) {
	case SAMPLEFORMAT_IEEEFP:
		switch (info->bps) {
		case 16: case 32: case 64:
			break;
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

static bool get_tiff_info(TIFF *tif, struct tiff_info *info) {
	if (TIFFGetField(tif, TIFFTAG_PHOTOMETRIC, &info->photometric) != 1) {
		puts("Image lacks photometric info.");
		return false;
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

	get_metadata_tags(tif, &infile->metadata);
	if (!alloc_sub_images(infile, TIFFNumberOfDirectories(tif))) {
		TIFFCleanup(tif);
		return wu_alloc_error;
	}

	size_t i = 0;
	do {
		uint32_t w, h;
		const bool ok = TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &w) == 1
			&& TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &h) == 1;
		if (!ok) {
			puts("Failed to get image dimensions");
			continue;
		}

		struct raw_img *img = infile->sub_img + i;
		img->w = w;
		img->h = h;
		if (raw_img_exceeds_limit(img, wuconf)) {
			continue;
		}

		struct tiff_info info;
		if (!get_tiff_info(tif, &info)) {
			continue;
		}
		bool do_it_ourselves;
		if (wuconf->tiff_use_homegrown_unpacker) {
			do_it_ourselves = check_support(&info);
		} else {
			do_it_ourselves = false;
		}

		enum wu_error status;
		switch ((int)do_it_ourselves) {
		case true:
			status = nih_decode(tif, img, &info);
			if (status == wu_ok) {
				break;
			}
			raw_img_clear(img);
			puts("Native unpacking routine failed, falling back "
				"on libtiff.");
			// fallthrough
		default:
			status = libtiff_decode(tif, infile, img);
			if (status != wu_ok) {
				raw_img_clear(img);
				continue;
			}
		}

		++i;
	} while (TIFFReadDirectory(tif) && i < infile->nr);

	TIFFCleanup(tif);
	return image_file_total_decoded(infile, i);
}
