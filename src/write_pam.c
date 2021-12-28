#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
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

static void network_fwrite(void *out, const size_t depth, const size_t buflen,
FILE *ofp) {
	if (which_end() != big_endian) {
		if (depth == 16) {
			loop_endian16(out, big_endian, buflen / 2);
		}
	}
	fwrite(out, 1, buflen, ofp);
}

static void write_pam_tuple(const size_t ch, FILE *ofp) {
	const char *tupl;
	switch (ch) {
	case 1: tupl = "GRAYSCALE"; break;
	case 2: tupl = "GRAYSCALE_ALPHA"; break;
	case 3: tupl = "RGB"; break;
	case 4: tupl = "RGB_ALPHA"; break;
	default: return;
	}
	fprintf(ofp, "TUPLTYPE %s\n", tupl);
}

static void write_pam_header(const size_t w, const size_t h, const size_t ch,
const size_t bd, FILE *ofp) {
	const size_t maxval = (bd > 8) ? USHRT_MAX : UCHAR_MAX;
	fprintf(ofp,
		"P7\n"
		"WIDTH %zu\n"
		"HEIGHT %zu\n"
		"DEPTH %zu\n"
		"MAXVAL %zu\n",
		w, h, ch, maxval);
	write_pam_tuple(ch, ofp);
	fputs("ENDHDR\n", ofp);
}

static void write_expand(const struct raw_img *img, FILE *ofp,
const enum unpack_op op, const bool swizzle) {
	uint8_t outch = img->channels;
	uint8_t outdepth = img->bitdepth;
	size_t buflen = 0;
	if (img->u.palette) {
		outdepth = 8;
		outch = 4;
		buflen = img->w * outch;
	} else if (op) {
		outdepth = unpack_depth(img->attr, op, img->bitdepth);
		if (!outdepth) {
			fputs("Error: Unsupported depth conversion.", stderr);
			return;
		}
		buflen = scanline_length(img->w * img->channels, outdepth, 1);
	}

	uint8_t *linebuf = NULL;
	if (buflen) {
		linebuf = malloc(buflen);
		if (!linebuf) {
			fputs("ERROR: Out of memory.\n", stderr);
			return;
		}
	}

	const size_t instride = raw_img_stride(img);
	write_pam_header(img->w, img->h, outch, outdepth, ofp);
	for (size_t y = 0; y < img->h; ++y) {
		unsigned char *src = img->data + instride*y;
		if (img->u.palette) {
			raster_pal_expand(linebuf, src, img->u.palette, img->w,
				1, 1, 4, img->bitdepth);
			src = linebuf;
		} else if (op) {
			unpack_strip(linebuf, src, img->w * img->channels, 1, 1,
				img->attr, op, img->bitdepth);
			src = linebuf;
		}

		if (swizzle) {
			strip_swizzle(src, src, img->w, 1, img->channels,
				outdepth, 1, img->layout, pix_rgba);
		}
		network_fwrite(src, outdepth, buflen, ofp);
	}
	free(linebuf);
}

static void write_raw(const struct raw_img *img, FILE *ofp) {
	size_t w = img->w;
	size_t bd = img->bitdepth;
	if (img->bitdepth > 16) {
		w *= bd / 8;
		bd = 8;
	} else if (img->bitdepth < 8) {
		size_t div = 8 / bd;
		w = (w + (div - 1)) / div;
		bd *= div;
	}

	write_pam_header(w, img->h, img->channels, bd, ofp);
	const size_t line = w * img->channels * bd / 8;
	const size_t stride = scanline_length(line, 8, img->alignment);
	for (size_t y = 0; y < img->h; ++y) {
		network_fwrite(img->data + stride*y, bd, line, ofp);
	}
}

static void write_sub_img(const struct raw_img *img, FILE *ofp,
const bool raw_output) {
	enum unpack_op op = op_noop;
	bool swizzle = false;
	if (!raw_output) {
		if (img->mode == image_mode_palette) {
			op = op_unpack;
		} else {
			switch (img->attr) {
			case pix_normal:
			case pix_inverted:
				if (img->bitdepth < 8) {
					op = op_expand;
				} else if (img->bitdepth > 16) {
					op = op_pack;
				}
				break;
			case pix_packing_332:
			case pix_packing_1555:
				op = op_expand;
				break;
			default:
				return;
			}
		}

		if (img->channels > 2 && img->layout != pix_rgba) {
			swizzle = true;
		}
	}

	if (op || swizzle) {
		write_expand(img, ofp, op, swizzle);
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
		if (img[i].attr == pix_float) {
			fputs("Error: Float output unsupported.\n", stderr);
			continue;
		} else if (img[i].mode == image_mode_planar) {
			fputs("Error: Planar output unsupported.\n", stderr);
			continue;
		}

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

int write_args(const int argc, char **argv, struct write_args *args) {
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
