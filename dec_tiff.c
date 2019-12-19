#include <stdlib.h>
#include <string.h>

#include <tiffio.h>

#include "wudefs.h"

static void rev_abgr(unsigned char *data, const uint32 *raster,
const size_t dims) {
	for (size_t i = 0; i < dims; ++i) {
		data[i*4] = (unsigned char)TIFFGetR(raster[i]);
		data[i*4 + 1] = (unsigned char)TIFFGetG(raster[i]);
		data[i*4 + 2] = (unsigned char)TIFFGetB(raster[i]);
		data[i*4 + 3] = (unsigned char)TIFFGetA(raster[i]);
	}
}

enum wu_error_type tiff_dec(struct image_file *infile) {
	TIFF* tif = TIFFOpen(infile->name, "r");
	if (!tif) {
		return wu_open_error;
	}

	struct raw_img *img = alloc_sub_images(infile,
		TIFFNumberOfDirectories(tif));

	size_t i = 0;
	do {
		TIFFRGBAImage tifimg;
		char emsg[1024];
		if (!TIFFRGBAImageBegin(&tifimg, tif, 0, emsg)) {
			infile->err_msg = strdup(emsg);
			return wu_invalid_params;
		}

		img[i].w = tifimg.width;
		img[i].h = tifimg.height;
		img[i].channels = 4;
		img[i].bitdepth = 8;
		const size_t dims = img[i].w * img[i].h;
		img[i].data = malloc(dims * img[i].channels);
		uint32 *raster = _TIFFmalloc((tmsize_t)(dims * sizeof(uint32)));
		if (!img[i].data || !raster) {
			return wu_alloc_error;
		}

		const int result = TIFFRGBAImageGet(&tifimg, raster,
			tifimg.width, tifimg.height);
		if (!result) {
			_TIFFfree(raster);
			return wu_decoding_error;
		}

		rev_abgr(img[i].data, raster, dims);
		_TIFFfree(raster);
		TIFFRGBAImageEnd(&tifimg);
		++i;
	} while (TIFFReadDirectory(tif) && i < infile->nr);

	TIFFClose(tif);
	return wu_ok;
}
