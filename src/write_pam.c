#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <errno.h>

#include <fcntl.h>
#include <unistd.h>

#include "dec.h"
#include "term.h"
#include "filesystem.h"
#include "write_pam.h"
#include "raster/endian.h"

void write_writer_terminate(struct write_writer *writer) {
	window_offscreen_terminate(&writer->window);
}

static void pam_write_row(void *out, const uint8_t depth, const size_t len,
FILE *ofp) {
	if (which_end() != big_endian) {
		if (depth == 16) {
			endian_loop16(out, big_endian, len);
		}
	}
	fwrite(out, depth/8, len, ofp);
}

static void pam_write_tuple(const uint8_t ch, FILE *ofp) {
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

static void pam_write_header(const size_t w, const size_t h, const uint8_t ch,
const uint8_t bd, FILE *ofp) {
	const unsigned short maxval = (bd > 8) ? USHRT_MAX : UCHAR_MAX;
	fprintf(ofp,
		"P7\n"
		"WIDTH %zu\n"
		"HEIGHT %zu\n"
		"DEPTH %hhu\n"
		"MAXVAL %hu\n",
		w, h, ch, maxval);
	pam_write_tuple(ch, ofp);
	fputs("ENDHDR\n", ofp);
}

static const char * pam_write(struct gl_context *gl,
const struct gl_reader *reader, struct wu_state *state, FILE *ofp) {
	uint8_t *buf = malloc(reader->len);
	if (buf) {
		pam_write_header(reader->w, reader->h, reader->ch, reader->bd, ofp);
		for (size_t y = 0; y < reader->h; ++y) {
			gl_reader_read_row(gl, state, reader, buf, y);
			pam_write_row(buf, reader->bd, reader->w * reader->ch, ofp);
		}
		return NULL;
	}
	return "Failed to allocate row memory";
}

static FILE * get_file(const struct wu_state *state,
struct write_out *out, const bool overwrite, const bool anim, const char *ext) {
	char *suffix = out->name + out->base_len;
	const size_t rem = sizeof(out->name) - out->base_len;

	const int prec = 5;
	int chars;
	if (anim) {
		chars = snprintf(suffix, rem, "_%.*d:%.*d.%s", prec, state->idx,
			prec, state->frame, ext);
	} else if (out->with_idx) {
		chars = snprintf(suffix, rem, "_%.*d.%s", prec, state->idx, ext);
	} else {
		chars = snprintf(suffix, rem, ".%s", ext);
	}

	errno = 0;
	FILE *ofp = NULL;
	if (chars > 0 && (size_t)chars < rem) {
		int flags = O_WRONLY | O_CREAT | O_TRUNC;
		if (!overwrite) {
			flags |= O_EXCL;
		}

		const int fd = openat(out->dirfd, out->name, flags,
			S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
		if (fd != -1) {
			ofp = fdopen(fd, "wb");
			if (!ofp) {
				close(fd);
			}
		}
	}
	return ofp;
}

static bool set_out_dir(const struct image_context *image,
struct write_out *out, const char *outdir, struct fs_path *path) {
	if (!outdir) {
		outdir = image->name;
	}
	out->dirfd = fs_get_parent_dir(path, outdir, false);
	if (out->dirfd >= 0) {
		if (outdir != image->name) {
			fs_path_set_file(path, wuptr_str(image->name));
		}
		out->base_len = path->file.len;
		if (out->base_len < sizeof(out->name)) {
			memcpy(out->name, path->file.ptr, out->base_len);
			out->with_idx = image->file.nr > 1;
			return true;
		}
		close(out->dirfd);
		fs_path_free(path);
	}
	return false;
}

static const char * write_sub_img(struct write_writer *writer,
const struct write_args *args, struct wu_state *state,
struct raw_img *img) {
	FILE *ofp;
	if (args->stdout) {
		ofp = stdout;
	} else {
		ofp = get_file(state, &writer->out, args->overwrite,
			img->frames, "pam");
	}

	const char *err_msg = NULL;
	if (ofp) {
		struct gl_context *gl = &writer->gl;
		if (gl_texture_upload(gl, img) != gl_upload_fail) {
			struct gl_reader reader;
		 	if (gl_reader_set(gl, &reader, state, img)) {
				err_msg = pam_write(gl, &reader, state, ofp);
			} else {
				err_msg = "Failed to set framebuffer";
			}
		} else {
			err_msg = "Failed to upload to texture";
		}
		fclose(ofp);
	} else if (errno) {
		err_msg = "Failed to open output file";
	} else {
		err_msg = "Filename too long";
	}
	return err_msg;
}

enum wu_error write_image(struct image_context *image,
struct write_writer *writer, const struct write_args *args) {
	struct raw_img *img;
	enum wu_error err = dec_iter_image(image, &img);
	if (err == wu_ok) {
		struct fs_path path;
		errno = 0;
		if (set_out_dir(image, &writer->out, args->outdir, &path)) {
			do {
				const char *msg = write_sub_img(writer, args,
					&image->state, img);
				if (msg) {
					term_line_put(msg, stderr);
				}
				if (args->stdout) {
					break;
				} else if (!msg) {
					wustr_print(&path.parent, stdout);
					fputs(writer->out.name, stdout);
					fputc(args->null ? 0 : '\n', stdout);
				}
				err = dec_iter_image(image, &img);
			} while (err == wu_ok);

			fs_path_free(&path);
			close(writer->out.dirfd);
		} else {
			perror("Failed to open output directory");
			err = wu_open_error;
		}
	}
	dec_free_image(image);

	if (err == wu_no_change) {
		return wu_ok;
	}
	return err;
}

bool write_writer_init(struct write_writer *writer, struct wu_conf *wuconf) {
	*writer = (struct write_writer){0};
	if (window_offscreen_setup(&writer->window)) {
		if (gl_context_setup(&writer->gl, wuconf)) {
			gl_reader_bind(&writer->gl);
			return true;
		}
		write_writer_terminate(writer);
	}
	return false;
}

int write_args(const int argc, char **argv, struct write_args *args) {
	int idx = 0;
	*args = (struct write_args){0};
	while (idx < argc) {
		const char *arg = argv[idx];
		if (arg[0] == '-' && arg[1] && !arg[2]) {
			switch (arg[1]) {
			case 'f': args->overwrite = true; break;
			case 'o':
				if (idx + 1 >= argc) {
					return idx;
				}
				++idx;
				args->outdir = argv[idx];
				break;
			case 's': args->stdout = true; break;
			case 'z': args->null = true; break;
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
