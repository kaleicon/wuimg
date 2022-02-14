#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <errno.h>

#include <fcntl.h>
#include <unistd.h>

#include "dec.h"
#include "filesystem.h"
#include "write_pam.h"

static const char OUTDIR_FAIL[] = "Failed to open output directory";

static void pam_write_row(void *out, const uint8_t depth, const size_t len,
FILE *ofp) {
	if (which_end() != big_endian) {
		if (depth == 16) {
			loop_endian16(out, big_endian, len);
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

static FILE * get_file(const struct wu_state *state,
struct write_out *out, const bool overwrite, const bool anim) {
	char *suffix = out->name + out->base_len;
	const size_t rem = sizeof(*out->name) - out->base_len;
	const char ext[] = "pam";

	const int prec = 4;
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
struct write_out *out, const char *outdir) {
	if (!outdir) {
		outdir = image->name;
	}
	struct fs_path path;
	out->dirfd = fs_get_parent_dir(outdir, &path);
	if (out->dirfd >= 0) {
		out->base_len = path.file.len;
		if (out->base_len < sizeof(out->name)) {
			memcpy(out->name, path.file.ptr, out->base_len);
			fs_path_free(&path);
			out->with_idx = image->file.nr > 1;
			return true;
		}
		close(out->dirfd);
		fs_path_free(&path);
	}
	return false;
}

static bool pam_write(struct wu_state *state, struct write_out *out,
struct gl_context *gl, const struct gl_reader *reader, uint8_t *restrict data,
const bool overwrite, const bool frame_nr) {
	FILE *ofp = get_file(state, out, overwrite, frame_nr);
	if (!ofp) {
		if (errno) {
			perror("Failed to open output file");
		} else {
			fputs("Filename too long\n", stderr);
		}
		return false;
	}

	pam_write_header(reader->w, reader->h, reader->ch, reader->bd, ofp);
	for (size_t y = 0; y < reader->h; ++y) {
		gl_reader_read_row(gl, state, reader, data, y);
		pam_write_row(data, reader->bd, reader->w * reader->ch, ofp);
	}
	fclose(ofp);
	return true;
}

static const char * try_write(struct wu_state *state, struct write_out *out,
struct gl_context *gl, const struct raw_img *img, const bool overwrite) {
	struct gl_reader reader;
 	if (gl_reader_set(gl, state, &reader, img)) {
		uint8_t *data = malloc(reader.len);
		if (data) {
			pam_write(state, out, gl, &reader, data, overwrite,
				img->frames);
			free(data);
		} else {
			return "Failed to allocate row memory\n";
		}
	} else {
		return "Failed to set framebuffer\n";
	}
	return NULL;
}

bool write_current(const struct image_context *image, struct gl_context *gl) {
	struct write_out out;
	if (!set_out_dir(image, &out, NULL)) {
		perror(OUTDIR_FAIL);
		return false;
	}

	struct wu_state state = image->state;
	struct raw_img *img = image->file.sub_img + state.idx;

	gl_reader_bind(gl);

	const char *err = try_write(&state, &out, gl, img, false);
	close(out.dirfd);

	gl_reader_unbind();
	gl_viewport(gl, &image->conf.fb);
	if (err) {
		fputs(err, stderr);
		return false;
	}
	return true;
}

static void write_sub_img(struct write_writer *writer,
const struct write_args *args, struct wu_state *state,
const struct raw_img *img) {
	const char *err = NULL;
	if (gl_texture_upload(&writer->gl, img) != gl_upload_fail) {
		err = try_write(state, &writer->out, &writer->gl, img,
			args->overwrite);
	} else {
		err = "Failed to upload to texture\n";
	}

	if (err) {
		fputs(err, stderr);
	}
}

enum wu_error write_image(struct image_context *image,
struct write_writer *writer, const struct write_args *args) {
	const struct raw_img *img;
	enum wu_error err = dec_iter_image(image, &img);
	if (err != wu_ok) {
		return err;
	}

	errno = 0;
	if (!set_out_dir(image, &writer->out, args->outdir)) {
		perror(OUTDIR_FAIL);
		return wu_open_error;
	}

	do {
		write_sub_img(writer, args, &image->state, img);
		err = dec_iter_image(image, &img);
	} while (err == wu_ok);
	dec_free_image(image);
	close(writer->out.dirfd);

	if (err == wu_no_change) {
		return wu_ok;
	}
	return err;
}

void write_writer_terminate(struct write_writer *writer) {
	window_offscreen_terminate(&writer->window);
}

bool write_writer_init(struct write_writer *writer, struct wu_conf *wuconf) {
	*writer = (struct write_writer){0};
	if (window_offscreen_setup(&writer->window)) {
		if (gl_context_setup(&writer->gl, wuconf)) {
			gl_reader_bind(&writer->gl);
			return true;
		}
		window_offscreen_terminate(&writer->window);
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
			case 'r': args->raw = true; break;
			case 'o':
				if (idx + 1 >= argc) {
					return idx;
				}
				++idx;
				args->outdir = argv[idx];
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
