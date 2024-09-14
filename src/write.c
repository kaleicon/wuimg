// SPDX-License-Identifier: 0BSD
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <unistd.h>

#include "dec.h"
#include "filesystem.h"
#include "term.h"
#include "write.h"

#include "enc/pam.h"

struct write_path {
	struct wustr parent;
	struct wustr file;
	size_t name_base;
	int dirfd;
	bool with_idx;
};

typedef void (*row_fn_t)(struct write_writer *write, struct wu_state *state,
	struct wuimg *out, size_t y);


void write_writer_terminate(struct write_writer *writer) {
	if (writer->gl_initialized) {
		window_offscreen_terminate(&writer->window);
	}
}

static void rowcpy(struct write_writer *write, struct wu_state *state,
struct wuimg *in, size_t y) {
	(void)state;
	uint8_t *row = write->out.data;
	memcpy(row, in->data + y*wuimg_stride(in), wuimg_stride(&write->out));

	const bool swz = (in->channels >= 3 && in->layout != pix_rgba)
		|| (in->channels < 3 && in->layout != pix_gray);
	if (swz) {
		const size_t pix_size = in->bitdepth/8 * in->channels;
		for (size_t x = 0; x < in->w; ++x) {
			uint8_t *d = row + x*pix_size;
			pix_layout_swizzle(d, in->bitdepth/8, in->channels, in->layout);
		}
	}
}

static void get_gl_row(struct write_writer *write, struct wu_state *state,
struct wuimg *in, size_t y) {
	(void)in;
	gl_reader_read_row(&write->gl, &write->out, state, y);
}

static void write_pam(struct write_writer *write, row_fn_t row_fn,
struct wuimg *in, struct wu_state *state, FILE *ofp) {
	struct wuimg *out = &write->out;
	pam_write_header(out, ofp);
	for (size_t y = 0; y < out->h; ++y) {
		(*row_fn)(write, state, in, y);
		pam_write_row(out, ofp);
	}
}

static bool writer_gl_init(struct write_writer *writer,
struct wu_conf *conf) {
	if (!writer->gl_initialized) {
		if (window_offscreen_setup(&writer->window)) {
			if (gl_context_setup(&writer->gl, conf)) {
				gl_reader_bind(&writer->gl);
				writer->gl_initialized = true;
				term_line_put("Initialized GL renderer", stderr);
			} else {
				write_writer_terminate(writer);
			}
		}
	}
	return writer->gl_initialized;
}

static const char * init_gl_renderer(struct write_writer *writer,
struct wuimg *in, struct image_context *image) {
	struct wu_conf *conf = &image->conf;
	if (writer_gl_init(writer, conf)) {
		if (gl_texture_upload(&writer->gl, in, conf) != gl_upload_fail) {
			return gl_reader_set(&writer->gl, &writer->out,
				&image->state, in);
		}
		return "Failed to upload to texture";
	}
	return "Failed to initialize GL renderer";
}

static const char * write_sub_img(struct write_writer *writer,
struct image_context *image, struct wuimg *in, FILE *ofp) {
	struct wuimg *out = &writer->out;
	memset(out, 0, sizeof(*out));

	const char *err_msg = NULL;
	row_fn_t row_fn;
	if (pam_can_cpy(out, in)) {
		row_fn = rowcpy;
	} else {
		err_msg = init_gl_renderer(writer, in, image);
		if (err_msg) {
			return err_msg;
		}
		row_fn = get_gl_row;
	}

	if (wuimg_verify(out) == wu_ok) {
		out->data = malloc(wuimg_stride(out));
		if (out->data) {
			write_pam(writer, row_fn, in, &image->state, ofp);
		} else {
			err_msg = "Failed to allocate row memory";
		}
	} else {
		err_msg = "Image failed verification";
	}
	wuimg_free(out);
	return err_msg;
}

static const size_t SUFFIX_SPACE = sizeof(int)*3*2 + 8 + 4;

static FILE * create_file(struct write_path *path, const struct wu_state *state,
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

static FILE * get_file(const struct write_args *args, struct write_path *path,
const struct image_context *image, struct wuimg *in) {
	if (args->stdout) {
		return stdout;
	}
	return create_file(path, &image->state, args->overwrite, in->frames);
}

static void free_write_path(struct write_path *path) {
	close(path->dirfd);
	wustr_free(&path->parent);
	wustr_free(&path->file);
}

static void print_write_path(const struct write_path *path, FILE *out) {
	wustr_print(&path->parent, out);
	wustr_print(&path->file, out);
}

static void print_write_error(const struct write_path *path, const char *msg,
FILE *out) {
	fputs("Failed to write to ", out);
	print_write_path(path, out);
	fputs(": ", out);
	term_line_put(msg, out);
}

static bool set_write_path(struct write_path *out, const char *outdir,
const struct image_context *image) {
	struct fs_path path;
	out->dirfd = fs_get_parent_dir(&path, outdir ? outdir : image->name,
		false);
	if (out->dirfd >= 0) {
		fs_path_set_file(&path, wuptr_str(image->name));
		out->name_base = path.file.len;
		if (wustr_malloc(&out->file, out->name_base + SUFFIX_SPACE)) {
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

bool write_image(struct image_context *image, struct write_writer *writer,
const struct write_args *args) {
	struct write_path path;
	bool all_ok = true;
	errno = 0;
	if (set_write_path(&path, args->outdir, image)) {
		struct wuimg *cur;
		enum wu_error err;
		while (wu_ok == (err = dec_iter(image, &cur))) {
			FILE *ofp = get_file(args, &path, image, cur);
			if (!ofp) {
				print_write_error(&path, strerror(errno), stderr);
				all_ok = false;
				if (args->stdout) {
					break;
				}
				continue;
			}
			const char *msg = write_sub_img(writer, image, cur, ofp);
			fclose(ofp);
			if (msg) {
				print_write_error(&path, msg, stderr);
				all_ok = false;
			}
			if (args->stdout) {
				break;
			} else if (!msg) {
				print_write_path(&path, stdout);
				fputc(args->null ? 0 : '\n', stdout);
			}
		}
		switch (err) {
		case wu_no_change: case wu_ok:
			break;
		default:
			fprintf(stderr, "Error decoding %s: %s\n", image->name,
				wu_error_message(err));
			all_ok = false;
		}
		dec_free_image(image);
		free_write_path(&path);
	} else {
		perror("Failed to open output directory");
		all_ok = false;
	}
	return all_ok;
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
		case 'h': return -1;
		default:
			return idx;
		}
		++idx;
	}
	return idx;
}
