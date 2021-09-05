#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <fcntl.h>
#include <unistd.h>

#include "wudefs.h"
#include "common.h"
#include "raster/unpack.h"

#include "write_pam.h"

struct filename_template {
	char *restrict name;
	size_t base_len;
	const char *restrict ext;
	size_t ext_len;
};

static void network_fwrite(void *out, const unsigned char bitdepth,
const size_t bytestride, FILE *ofp) {
	if (bitdepth == 16) {
		loop_endian16(out, big_endian, bytestride / 2);
	}
	fwrite(out, 1, bytestride, ofp);
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
	size_t stride = scanline_length(img->w * img->channels, img->bitdepth,
		img->alignment);

	write_pam_header(img, ofp, img->channels);
	const struct raster_desc desc = {
		.w = img->w,
		.h = 1,
		.ch = img->channels,
		.bitdepth = img->bitdepth,
		.layout = img->layout,
	};
	for (size_t y = 0; y < img->h; ++y) {
		unsigned char *src = img->data + stride*y;
/*		if (img->palette) {
			raster_pal_expand(expand_buf, src, img->palette, img->w,
				1, 1, img->channels);
		} else if (img->bitdepth == pix_rgb332) {
			strip_expand332(expand_buf, src, img->w, 1, 1);
		} else if (img->bitdepth < 8) {
			strip_unpack(expand_buf, src, img->w * img->channels,
				1, 1, op_expand, img->bitdepth);
		} else {
			memcpy(expand_buf, src, stride);
		}*/

		if (img->channels > 2 && img->layout != pix_rgba) {
			strip_swizzle(expand_buf, src, &desc, pix_rgba);
		} else {
			memcpy(expand_buf, src, stride);
		}
		network_fwrite(expand_buf, img->bitdepth, buflen, ofp);
	}
}

static void write_raw(const struct raw_img *img, FILE *ofp) {
	unsigned int channels;
	if ((img->bitdepth == 4 && img->channels == 4)
	|| img->bitdepth == pix_argb1555) {
		channels = 2;
	} else {
		channels = img->channels;
	}

	write_pam_header(img, ofp, channels);
	const size_t bytedepth = (img->bitdepth + 7U) / 8;
	const size_t stride = img->w * channels * bytedepth;
	const size_t scanline = scanline_length(stride, 8, img->alignment);
	for (size_t y = 0; y < img->h; ++y) {
		network_fwrite(img->data + scanline*y, img->bitdepth, stride,
			ofp);
	}
}

static void write_sub_img(const struct raw_img *img, FILE *ofp,
const bool raw_output) {
	if (img->bitdepth > 16 || img->attr & pix_float) {
		fputs("Error: Unsupported output depth.\n", stderr);
		return;
	}

	unsigned char *expand_buf = NULL;
	size_t buflen = 0;
	if (!raw_output) {
		if (img->bitdepth == pix_argb1555) {
			fputs("argb1555 unimplemented for now.\n",
				stderr);
			return;
		}
		if (img->palette || (img->channels > 2 && img->layout != pix_rgba)
		|| img->bitdepth < 8) {
			buflen = img->w * img->channels;
			expand_buf = malloc(buflen);
			if (!expand_buf) {
				fputs("ERROR: Out of memory.\n", stderr);
				return;
			}
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
	int flags = O_WRONLY | O_CREAT | O_TRUNC;
	if (!overwrite) {
		flags |= O_EXCL;
	}

	FILE *ofp = NULL;
	const int fd = open(outname, flags, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
	if (fd != -1) {
		ofp = fdopen(fd, "wb");
		if (!ofp) {
			close(fd);
		}
	}
	return ofp;
}

static void id_replace(struct filename_template *tpl, const char *restrict id) {
	size_t pos = tpl->base_len;
	if (id) {
		tpl->name[pos] = '_';
		++pos;

		const size_t id_len = strlen(id);
		memcpy(tpl->name + pos, id, id_len);
		pos += id_len;
	}
	memcpy(tpl->name + pos, tpl->ext, tpl->ext_len);
}

static bool name_template(const struct image_file *file,
const char *restrict filename, const char *restrict optname,
struct filename_template *tpl) {
	const struct raw_img *img = file->sub_img;
	size_t id_max = 0;
	for (size_t i = 0; i < file->nr; ++i) {
		if (img[i].id) {
			id_max = zumax(id_max, strlen(img[i].id));
		}
	}

	if (optname) {
		filename = optname;
		tpl->base_len = strlen(filename);
	} else {
		const char *dot = strrchr(filename, '.');
		if (dot) {
			tpl->base_len = (size_t)(dot - filename);
		} else {
			tpl->base_len = strlen(filename);
		}
	}

	const size_t sep_len = 1;
	tpl->name = malloc(tpl->base_len + sep_len + id_max + tpl->ext_len);
	if (tpl->name) {
		memcpy(tpl->name, filename, tpl->base_len);
	}
	return (bool)tpl->name;
}

void write_to_file(const struct image_file *infile, const char *filename,
const struct write_args *args) {
	const char ext[] = ".pam";
	struct filename_template tpl = {
		.ext = ext,
		.ext_len = sizeof(ext),
	};
	if (!name_template(infile, filename, args->outname, &tpl)) {
		fputs("ERROR: Out of memory.\n", stderr);
		return;
	}

	const struct raw_img *img = infile->sub_img;
	for (size_t i = 0; i < infile->nr; ++i) {
		id_replace(&tpl, img[i].id);
		errno = 0;
		FILE *ofp = create_file(tpl.name, args->overwrite);
		if (ofp) {
			write_sub_img(img + i, ofp, args->raw);
			fclose(ofp);
		} else {
			char errstr[1024];
			strerror_r(errno, errstr, sizeof(errstr));
			fprintf(stderr, "Failed to open %s for writing: %s\n",
				tpl.name, errstr);
		}
	}
	free(tpl.name);
}

int read_write_args(const int argc, char **argv, struct write_args *args) {
	int idx = 0;
	*args = (struct write_args){0};
	while (idx < argc) {
		const char *arg = argv[idx];
		if (arg[0] == '-' && arg[1] && !arg[2]) {
			switch (arg[1]) {
			case 'f': args->overwrite = true; break;
			case 'r': args->raw = true; break;
			case 'o':
				++idx;
				if (idx < argc) {
					args->outname = argv[idx];
				}
				break;
			default:
				return idx;
			}
			++idx;
		} else {
			break;
		}
	}
	return idx;
}
