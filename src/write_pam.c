// SPDX-License-Identifier: 0BSD
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <fcntl.h>
#include <unistd.h>

#include "dec.h"
#include "term.h"
#include "filesystem.h"
#include "write_pam.h"
#include "misc/endian.h"

struct write_path {
	struct wustr parent;
	struct wustr file;
	size_t name_base;
	int dirfd;
	bool with_idx;
};

void write_writer_terminate(struct write_writer *writer) {
	window_offscreen_terminate(&writer->window);
}

static void pam_write_row(void *out, const uint8_t depth, const size_t len,
FILE *ofp) {
	if (depth == 16) {
		endian_loop16(out, big_endian, len);
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

static const size_t SUFFIX_SPACE = sizeof(int)*3*2 + 8 + 4;

static FILE * get_file(const struct wu_state *state, struct write_path *path,
const bool overwrite, const bool anim) {
	const char ext[] = "pam";
	char *suffix = (char *)path->file.str + path->name_base;
	const size_t rem = SUFFIX_SPACE;

	const int prec = 5;
	int w;
	if (anim) {
		w = snprintf(suffix, rem, "_%.*d:%.*d.%s", prec, state->idx,
			prec, state->frame, ext);
	} else if (path->with_idx) {
		w = snprintf(suffix, rem, "_%.*d.%s", prec, state->idx, ext);
	} else {
		w = snprintf(suffix, rem, ".%s", ext);
	}

	FILE *ofp = NULL;
	if (w > 0 && (size_t)w < rem) {
		path->file.len = path->name_base + (size_t)w;
		errno = 0;
		const int fd = openat(path->dirfd, (char *)path->file.str,
			O_WRONLY | O_CREAT | O_TRUNC | (overwrite ? 0 : O_EXCL),
			S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
		if (fd >= 0) {
			ofp = fdopen(fd, "wb");
			if (!ofp) {
				close(fd);
			}
		}
	}
	return ofp;
}

static const char * write_sub_img(struct write_writer *writer,
const struct write_args *args, struct write_path *path, struct wu_state *state,
struct wuimg *img, const struct wu_conf *wuconf) {
	FILE *ofp;
	if (args->stdout) {
		ofp = stdout;
	} else {
		ofp = get_file(state, path, args->overwrite, img->frames);
	}

	const char *err_msg = NULL;
	if (ofp) {
		struct gl_context *gl = &writer->gl;
		if (gl_texture_upload(gl, img, wuconf) != gl_upload_fail) {
			struct gl_reader reader;
		 	if (gl_reader_set(gl, &reader, state, img)) {
				err_msg = pam_write(gl, &reader, state, ofp);
			} else {
				err_msg = "Failed to set framebuffer";
			}
		} else {
			err_msg = "Failed to upload to texture";
		}
		if (!args->stdout) {
			fclose(ofp);
		}
	} else {
		err_msg = strerror(errno);//"Failed to open output file";
	}
	return err_msg;
}

static void free_write_path(struct write_path *path) {
	close(path->dirfd);
	wustr_free(&path->parent);
	wustr_free(&path->file);
}

static void print_write_path(const struct write_path *path, const char newline,
FILE *out) {
	wustr_print(&path->parent, out);
	wustr_print(&path->file, out);
	fputc(newline, out);
}

static bool set_write_path(struct write_path *out, const char *outdir,
const struct image_context *image) {
	struct fs_path path;
	out->dirfd = fs_get_parent_dir(&path, outdir ? outdir : image->name,
		false);
	if (out->dirfd >= 0) {
		fs_path_set_file(&path, wuptr_str(image->name));
		out->name_base = path.file.len;
		if (wustr_malloc(&out->file, out->name_base + SUFFIX_SPACE)){
			memcpy(out->file.str, path.file.ptr, out->name_base);
			out->parent = path.parent;
			out->with_idx = image->file.nr > 1;
			return true;
		}
		close(out->dirfd);
		fs_path_free(&path);
	}
	return false;
}

enum wu_error write_image(struct image_context *image,
struct write_writer *writer, const struct write_args *args) {
	struct wuimg *img;
	enum wu_error err = dec_iter(image, &img);
	if (err == wu_ok) {
		struct write_path path;
		errno = 0;
		if (set_write_path(&path, args->outdir, image)) {
			do {
				const char *msg = write_sub_img(writer, args,
					&path, &image->state, img, &image->conf);
				if (msg) {
					term_line_put(msg, stderr);
				}
				if (args->stdout) {
					break;
				} else if (!msg) {
					print_write_path(&path,
						args->null ? 0 : '\n',stdout);
				}
				err = dec_iter(image, &img);
			} while (err == wu_ok);
			free_write_path(&path);
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
		switch (short_opt(argv[idx])) {
		case 'd':
			if (idx + 1 >= argc) {
				return idx;
			}
			++idx;
			args->outdir = argv[idx];
			break;
		case 'f': args->overwrite = true; break;
		case 's': args->stdout = true; break;
		case 'z': args->null = true; break;
		default:
			return idx;
		}
		++idx;
	}
	return idx;
}
