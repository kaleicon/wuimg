#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <fcntl.h>

#include "wudefs.h"
#include "common.h"
#include "common_unpack.h"

#include "write_pam.h"

static void network_fwrite(void *out, const unsigned char bitdepth,
const size_t bytestride, FILE *ofp) {
	if (bitdepth == 16) {
		loop_endian16(out, big_endian, bytestride / 2);
	}
	fwrite(out, 1, bytestride, ofp);
}

static void bgra_rgba_swap(void *raster, const size_t width,
const size_t channels, const size_t bitdepth) {
	const size_t bytedepth = (bitdepth + 7U) / 8;
	if (bytedepth == 1) {
		unsigned char *buf = raster;
		for (size_t pix = 0; pix < width * channels; pix += channels) {
			unsigned char tmp = buf[pix];
			buf[pix] = buf[pix+2];
			buf[pix+2] = tmp;
		}
	} else {
		unsigned short *buf = raster;
		for (size_t pix = 0; pix < width * channels; pix += channels) {
			unsigned short tmp = buf[pix];
			buf[pix] = buf[pix+2];
			buf[pix+2] = tmp;
		}
	}
}

static void write_pam_header(const struct raw_img *img, FILE *ofp,
const unsigned channels) {
	const unsigned int bytedepth = (img->bitdepth + 7U) / 8;
	const unsigned int maxval = (1U << (bytedepth * 8)) - 1;
	const char *tuples[] = {"GRAYSCALE", "GRAYSCALE_ALPHA",
		"RGB", "RGB_ALPHA"};
	fprintf(ofp,
		"P7\n"
		"WIDTH %zu\n"
		"HEIGHT %zu\n"
		"DEPTH %u\n"
		"MAXVAL %u\n"
		"TUPLTYPE %s\n"
		"ENDHDR\n",
		img->w, img->h, channels, maxval,
		tuples[channels - 1]);
}

static void write_expand(const struct raw_img *img, FILE *ofp,
unsigned char *restrict expand_buf, const size_t buflen) {
	size_t stride;
	if (img->palette) {
		stride = img->w + img->alignment - 1;
	} else {
		stride = scanline_length(img->w * img->channels, img->bitdepth,
			img->alignment);
	}

	write_pam_header(img, ofp, img->channels);
	for (size_t y = 0; y < img->h; ++y) {
		unsigned char *src = img->data + stride*y;
		if (img->palette) {
			strip_colormap(expand_buf, src, img->palette, img->w,
				1, 1, img->channels, img->bitdepth);
		} else if (img->bitdepth == bgra4444) {
			strip_unpack(expand_buf, src, img->w * img->channels,
				1, 1, expand, img->bitdepth);
		} else if (img->bitdepth == rgb332) {
			strip_expand332(expand_buf, src, img->w, 1, 1);
		} else {
			memcpy(expand_buf, src, stride);
		}

		if (img->layout == bgra) {
			bgra_rgba_swap(expand_buf, img->w, img->channels,
				img->bitdepth);
		}
		network_fwrite(expand_buf, img->bitdepth, buflen, ofp);
	}
}

static void write_raw(const struct raw_img *img, FILE *ofp) {
	unsigned int channels;
	if (img->palette || img->bitdepth == rgb332) {
		channels = 1;
	} else if (img->bitdepth == bgra4444 || img->bitdepth == bgra5551) {
		channels = 2;
	} else {
		channels = img->channels;
	}

	write_pam_header(img, ofp, channels);
	const unsigned int bytedepth = (img->bitdepth + 7U) / 8;
	const size_t stride = img->w * channels * bytedepth;
	const size_t scanline = scanline_length(stride, 8, img->alignment);
	for (size_t y = 0; y < img->h; ++y) {
		network_fwrite(img->data + scanline*y, img->bitdepth, stride,
			ofp);
	}
}

static void write_sub_img(const struct raw_img *img, FILE *ofp,
const bool expand) {
	unsigned char *expand_buf = NULL;
	size_t buflen = 0;
	if (expand) {
		if (img->palette || img->layout == bgra
		|| img->bitdepth == bgra4444 || img->bitdepth == rgb332) {
			buflen = img->w * img->channels;
			expand_buf = malloc(buflen);
			if (!expand_buf) {
				fputs("ERROR: Out of memory.\n", stderr);
				return;
			}
		} else if (img->bitdepth == bgra5551) {
			fputs("bgra5551 unimplemented for now.\n",
				stderr);
			return;
		}
	}

	if (expand_buf) {
		write_expand(img, ofp, expand_buf, buflen);
		free(expand_buf);
	} else {
		write_raw(img, ofp);
	}
}

static FILE * create_file(const char *outname, const bool overwrite) {
	const char mode[] = "wb";
	if (overwrite) {
		return fopen(outname, mode);
	} else {
		const int fd = open(outname, O_WRONLY | O_CREAT | O_EXCL,
			S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
		if (fd != -1) {
			return fdopen(fd, mode);
		}
		return NULL;
	}
}

static char * ext_replace(const char *filename, const char *id,
const char *ext) {
	const size_t idlen = id ? strlen(id) : 0;
	const size_t extlen = strlen(ext);
	const char *extpos = strrchr(filename, '.');
	size_t baselen;
	if (extpos && strcmp(ext, extpos)) {
		baselen = (size_t)(extpos - filename);
	} else {
		baselen = strlen(filename);
	}
	char *outname = malloc(baselen + idlen + extlen);
	if (outname) {
		memcpy(outname, filename, baselen);
		if (idlen) {
			memcpy(outname + baselen, id, idlen);
		}
		memcpy(outname + idlen + baselen, ext, extlen + 1 /*NULL*/);
	}
	return outname;
}

void write_to_file(const struct image_file *infile, const char *filename,
const struct write_args args) {
	const char ext[] = ".pam";
	for (size_t i = 0; i < infile->nr; ++i) {
		const struct raw_img *img = infile->sub_img;
		errno = 0;
		char *outname = ext_replace(filename, img[i].id, ext);
		if (outname) {
			FILE *ofp = create_file(outname, args.overwrite);
			if (ofp) {
				write_sub_img(img + i, ofp, args.expand);
				fclose(ofp);
			} else {
				fprintf(stderr, "Error while opening %s for "
					"writing: %s\n",
					outname, strerror(errno));
			}
			free(outname);
		} else {
			fprintf(stderr, "ERROR: Out of memory.\n");
		}
	}
}
