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

	int chars;
	if (anim) {
		chars = snprintf(suffix, rem, "_%d:%d.%s", state->idx,
			state->frame, ext);
	} else if (out->omit_idx) {
		chars = snprintf(suffix, rem, ".%s", ext);
	} else {
		chars = snprintf(suffix, rem, "_%d.%s", state->idx, ext);
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
			memcpy(out->name, path.file.str, out->base_len);
			fs_path_free(&path);
			out->omit_idx = image->file.nr == 1;
			return true;
		}
		close(out->dirfd);
		fs_path_free(&path);
	}
	return false;
}

static bool try_write(struct wu_state *state, struct write_out *out,
struct gl_context *gl, struct gl_reader *reader, uint8_t *restrict data,
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

/*void write_current(struct image_context *image, struct gl_context *gl) {
	struct write_out out;
	if (!set_out_dir(image, &writer->out, NULL)) {
		perror("Failed to open output directory");
		return;
	}

	struct wu_state *state = &image->state;
	struct raw_img *img = image->file.sub_img + state->idx;

	struct gl_reader reader;
	gl_reader_enable(gl, &reader);

 	if (gl_reader_set(gl, state, &reader, img)) {
		uint8_t *data = malloc(reader.len);
		if (data) {
			try_write(state, &out, gl, &reader, data, false,
				img->frames);
			free(data);
		} else {
			fputs("Failed to allocate row memory\n", stderr);
		}
	} else {
		fputs("Failed to set framebuffer\n", stderr);
	}
	close(out.dirfd);
	gl_reader_disable(gl, &reader);
}*/

static void write_sub_img(struct image_context *image,
struct write_writer *writer, const struct write_args *args) {
	struct image_file *file = &image->file;
	struct wu_state *state = &image->state;
	struct raw_img *img = file->sub_img + state->idx;

	struct gl_reader reader;
 	if (!gl_reader_set(&writer->gl, state, &reader, img)) {
		fputs("Failed to set framebuffer\n", stderr);
		return;
	}

	uint8_t *data = malloc(reader.len);
	if (!data) {
		fputs("Failed to allocate row memory\n", stderr);
		return;
	}

	const int frames = (int)(img->frames ? img->frames->nr : 1);
	for (state->frame = 0; state->frame < frames; ++state->frame) {
		if (state->frame > 0) {
			const enum wu_error err = dec_callback_image(image,
				ev_frame);
			if (err > wu_ok) {
				fprintf(stderr, "%s\n", wu_error_message(err));
				break;
			}
		}

		if (gl_texture_upload(&writer->gl, img) == gl_upload_fail) {
			continue;
		}
		try_write(state, &writer->out, &writer->gl, &reader, data,
			args->overwrite, img->frames);
	}
	free(data);
}

void write_image(struct image_context *image, struct write_writer *writer,
const struct write_args *args) {
	errno = 0;
	if (!set_out_dir(image, &writer->out, args->outdir)) {
		perror("Failed to open output directory");
		return;
	}

	struct wu_state *state = &image->state;
	*state = (struct wu_state) {
		.zoom = 1,
	};

	for (state->idx = 0; state->idx < (int)image->file.nr; ++state->idx) {
		write_sub_img(image, writer, args);
	}
	close(writer->out.dirfd);
}

void write_writer_terminate(struct write_writer *writer) {
	window_offscreen_terminate(&writer->window);
}

bool write_writer_init(struct write_writer *writer, struct wu_conf *wuconf) {
	*writer = (struct write_writer){0};
	if (window_offscreen_setup(&writer->window)) {
		if (gl_context_setup(&writer->gl, wuconf)) {
			gl_reader_enable(&writer->gl, &writer->reader);
			return true;
		}
		window_offscreen_terminate(&writer->window);
	}
	return false;
}

int write_args(const int argc, char **argv, struct write_args *args) {
	int idx = 0;
	memset(args, 0, sizeof(*args));
	while (idx < argc) {
		const char *arg = argv[idx];
		if (arg[0] == '-' && arg[1] && !arg[2]) {
			switch (arg[1]) {
			case 'f': args->overwrite = true; break;
			case 'r': args->raw = true; break;
			case 'o':
				++idx;
				if (idx < argc) {
					args->outdir = argv[idx];
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
