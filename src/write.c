// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2024 kaleido
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <unistd.h>

#include "dec.h"
#include "filesystem.h"
#include "fmtmap.h"
#include "opts.h"
#include "write.h"
#include "misc/common.h"
#include "misc/file.h"
#include "misc/term.h"
#include "misc/time.h"

#include "enc.h"

#include "dec_enable.def"

struct enc_info {
	const char ext[4];
	const struct enc_fn *enc;
};

#ifdef WU_ENABLE_JPEGXL
extern const struct enc_fn jpegxl_enc;
#endif
extern const struct enc_fn raw_enc;
extern const struct enc_fn pam_enc;

static const struct enc_info ENC_TABLE[] = {
	// Entry 0 will be the default
	{"pam", &pam_enc},
#ifdef WU_ENABLE_JPEGXL
	{"jxl", &jpegxl_enc},
#endif
	{"raw", &raw_enc},
};

struct write_file {
	struct wustr parent;
	struct wustr file;
	size_t name_base;
	const struct enc_fn *enc;
	FILE *ofp;
	int dirfd;
	bool with_idx;
	bool passthrough; // Skip conversion, pass original image to encoder
	void *enc_state;
	struct wuimg dst;
};

static bool is_last_frame(const struct wu_state *state, const struct wuimg *src) {
	return (size_t)state->frame + 1 == wuimg_anim_nr(src);
}
static bool write_should_open_file(const struct write_file *out,
const struct wudec_image *image) {
	const struct wu_state *state = &image->state;
	switch (out->enc->support) {
	case enc_subimg:
		if (state->idx == 0) {
	case enc_anim:
			if (state->frame == 0) {
	case enc_single:
				return true;
			}
		}
		break;
	}
	return false;
}
static bool write_should_close_file(const struct write_file *out,
const struct wudec_image *image, const struct wuimg *src) {
	const struct wu_state *state = &image->state;
	switch (out->enc->support) {
	case enc_subimg:
		if ((size_t)state->idx + 1 == image->file.nr) {
	case enc_anim:
			if (is_last_frame(state, src)) {
	case enc_single:
				return true;
			}
		}
		break;
	}
	return false;
}

static const char * actually_write_frame(struct write_file *out,
struct write_writer *writer, const struct wudec_image *image,
const struct wuimg *dst, const struct wuimg *src) {
	const watch_t watch = watch_look();
	const char *msg = out->passthrough
		? NULL : writer->set_image(writer->state, dst, src);
	if (!msg) {
		const struct enc_fn *enc = out->enc;
		size_t w = 0;
		if (!out->passthrough) {
			const size_t stride = wuimg_stride(dst);
			for (size_t y = 0; y < dst->h; ++y) {
				uint8_t *tgt = dst->data
					+ (enc->write_row ? 0 : stride*y);
				writer->get_row(writer->state, y, tgt);
				if (enc->write_row) {
					w += enc->write_row(out->enc_state,
						dst, out->ofp, tgt);
				}
			}
		}
		const int frame = image->state.frame;
		if (out->passthrough || !enc->write_row) {
			w = enc->write_frame(out->enc_state, dst,
				out->ofp, frame);
		}
		if (!w) {
			msg = "No data written";
		}
	}
	if (!out->passthrough && writer->close) {
		writer->close(writer->state);
	}
	watch_report("Converted", watch, report_all);
	return msg;
}

static void write_close_encoder(struct write_file *out,
const struct wudec_image *image, const struct wuimg *src, const bool failed) {
	if (failed || write_should_close_file(out, image, src)) {
		out->enc->end(out->enc_state);
	}
}
static const char * write_prepare_encoder(struct write_file *out,
const struct wudec_image *image, const struct wuimg *dst, const struct wuimg *src) {
	if (write_should_open_file(out, image)) {
		return out->enc->init(out->enc_state, dst, src, out->ofp);
	}
	return NULL;
}

static void write_clear_dst_img(struct write_file *out,
const struct wudec_image *image, const struct wuimg *src, const bool failed) {
	if (failed || out->enc->support == enc_single
	|| is_last_frame(&image->state, src)) {
		wuimg_free(&out->dst);
	}
}
static const char * write_prepare_dst_img(struct write_file *out,
const struct wudec_image *image, const struct wuimg *src,
const struct wuimg **tgt) {
	*tgt = src;
	const char *err_msg = NULL;
	if (image->state.frame == 0) {
		struct wuimg *dst = &out->dst;
		*dst = (struct wuimg){0};
		out->passthrough = out->enc->best_fit(dst, src);
		if (getenv("WU_DEBUG")) {
			fprintf(stderr, "passthrough: %s\n",
				out->passthrough ? "yes" : "no");
		}
		if (!out->passthrough) {
			*tgt = dst;
			if (wuimg_verify(dst) == wu_ok) {
				size_t rows = out->enc->write_row ? 1 : dst->h;
				dst->data = malloc(wuimg_stride(dst) * rows);
				if (!dst->data) {
					err_msg = "Output image allocation failure";
				}
			} else {
				err_msg = "Output image failed verification."
					" This is likely a programmer oversight.";
			}
		}
	}
	return err_msg;
}

static const size_t DECIMAL_LEN = 3;
static const size_t SUFFIX_LEN = sizeof(int)*DECIMAL_LEN*2 // index and frame number
	+ sizeof(uint32_t)*DECIMAL_LEN*2 // frame time numerator and denominator
	+ 5 // delimiters and extension dot
	+ sizeof(ENC_TABLE->ext) // extension
	+ 1; // ending nul

static FILE * create_file(struct write_file *out, const struct wu_state *state,
const bool overwrite, const struct image_anim *anim, const char ext[static 4]) {
	char *suffix = (char *)out->file.str + out->name_base;
	const size_t rem = SUFFIX_LEN;

	const int prec = 5;
	const int ext_len = sizeof(ENC_TABLE->ext);
	int w;
	if (anim) {
		const struct frame_time sec = anim->sec;
		w = snprintf(suffix, rem,
			"_%.*d.%.*d.%" PRIu32 ".%" PRIu32 ".%.*s",
			prec, state->idx,
			prec, state->frame,
			sec.num, sec.den,
			ext_len, ext);
	} else if (out->with_idx) {
		w = snprintf(suffix, rem, "_%.*d.%.*s", prec, state->idx,
			ext_len, ext);
	} else {
		w = snprintf(suffix, rem, ".%.*s", ext_len, ext);
	}

	FILE *ofp = NULL;
	if (w > 0 && (size_t)w < rem) {
		out->file.len = out->name_base + (size_t)w;
		const int fd = openat(out->dirfd, (char *)out->file.str,
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

static void print_write_file(const struct write_file *out, FILE *ofp) {
	if (isatty(fileno(ofp))) {
		term_print_convert((const char *)out->parent.str, ofp);
		term_print_convert((const char *)out->file.str, ofp);
	} else {
		wustr_print(&out->parent, ofp);
		wustr_print(&out->file, ofp);
	}
}

static void write_close_file(struct write_file *out,
const struct wudec_image *image, struct wuimg *src, const bool failed,
const bool null) {
	if (failed || write_should_close_file(out, image, src)) {
		fclose(out->ofp);
		if (!failed) {
			print_write_file(out, stdout);
			fputc(null ? 0 : '\n', stdout);
		}
	}
}
static const char * write_prepare_file(struct write_file *out,
const struct write_args *args, const struct wudec_image *image,
struct wuimg *src) {
	const char *msg = NULL;
	if (write_should_open_file(out, image)) {
		errno = 0;
		if (args->stdout) {
			out->ofp = stdout;
		} else {
			const bool supports_anim = out->enc->support >= enc_anim;
			out->ofp = create_file(out, &image->state,
				args->overwrite,
				supports_anim ? NULL : src->anim,
				ENC_TABLE[args->codec].ext);
		}
		if (out->ofp) {
			setbuf(out->ofp, NULL);
		} else {
			msg = strerror(errno);
		}
	}
	return msg;
}

static const char * write_frame(struct write_file *out,
const struct write_args *args, struct write_writer *writer,
const struct wudec_image *image, struct wuimg *src) {
	const char *msg = write_prepare_file(out, args, image, src);
	if (!msg) {
		const struct wuimg *tgt;
		msg = write_prepare_dst_img(out, image, src, &tgt);
		if (!msg) {
			msg = write_prepare_encoder(out, image, tgt, src);
			if (!msg) {
				msg = actually_write_frame(out, writer, image,
					tgt, src);
			}
			write_close_encoder(out, image, src, msg);
		}
		write_clear_dst_img(out, image, &out->dst, msg);
		write_close_file(out, image, src, msg, args->null);
	}
	return msg;
}

static void print_write_error(const struct write_file *out, const char *msg,
FILE *ofp) {
	fputs("Failed to write to ", ofp);
	print_write_file(out, ofp);
	fputs(": ", ofp);
	term_line_put(msg, ofp);
}

static void free_write_file(struct write_file *out) {
	if (out->dirfd >= 0) {
		close(out->dirfd);
	}
	wustr_free(&out->parent);
	wustr_free(&out->file);
	free(out->enc_state);
}

static bool init_write_file(struct write_file *out,
const struct write_args *args, const struct wudec_image *image) {
	struct fs_path path;
	out->enc = ENC_TABLE[args->codec].enc;
	out->dirfd = fs_get_dir_or_parent(&path,
		args->outdir ? args->outdir : image->file.name,
		!args->outdir);
	if (out->dirfd >= 0) {
		fs_path_set_file(&path, wuptr_str(image->file.name));
		out->name_base = path.file.len;
		out->parent = path.parent;
		if (wustr_malloc(&out->file, out->name_base + SUFFIX_LEN)) {
			memcpy(out->file.str, path.file.ptr, out->name_base);
			out->with_idx = (out->enc->support != enc_subimg)
				&& (image->file.nr > 1);
			out->enc_state = calloc(out->enc->state_size, 1);
			return out->enc_state;
		}
	}
	return false;
}

static void print_dec_error(const enum wu_error e, const char *what,
const struct wudec_image *image) {
	fprintf(stderr, "Error while %s %s: ", what, image->file.name);
	image_file_error_print(&image->file, e, stderr);
}

static bool write_image(struct wudec_image *image, const struct write_args *args,
struct write_writer *writer) {
	struct wuimg *cur;
	enum wu_error err = wudec_iter(image, &cur);
	if (err != wu_ok) {
		print_dec_error(err, "opening", image);
		return false;
	}

	struct write_file out = {0};
	errno = 0;
	bool all_ok = init_write_file(&out, args, image);
	if (all_ok) {
		do {
			const char *msg = write_frame(&out, args, writer,
				image, cur);
			if (msg) {
				print_write_error(&out, msg, stderr);
				all_ok = false;
				break;
			} else if (args->stdout) {
				break;
			}
		} while (wu_ok == (err = wudec_iter(image, &cur)));
		switch (err) {
		case wu_no_change: case wu_ok:
			break;
		default:
			print_dec_error(err, "processing", image);
			all_ok = false;
		}
	} else {
		perror("Writer state setup failed");
	}
	free_write_file(&out);
	return all_ok;
}

int write_filelist(const struct write_args *args, struct write_writer *writer,
const int len, char **names, const struct wu_conf *conf) {
	struct wudec_image image = {
		.conf = conf ? *conf : conf_default(),
	};

	int ok = 0;
	for (int i = 0; i < len; ++i) {
		const char *name = names[i];
		if (!strcmp("-", name)) {
			FILE *stdin_cpy = file_from_stdin();
			if (!stdin_cpy) {
				term_line_put("Failed to save stdin", stderr);
				continue;
			}
			wudec_src_file(&image, stdin_cpy, "stdin", false, false);
		} else {
			wudec_src_filename(&image, name);
		}
		wudec_src_format(&image, args->fmt);
		ok += write_image(&image, args, writer);
		wudec_recycle_conf(&image);
		if (args->stdout) {
			break;
		}
	}
	return ok != len;
}

const char write_description[] =
	"\tTranscode each FILE into the selected format, with output names\n"
	"\twritten to stdout. Exact behavior depends on input and output formats:\n\n"
	"\t * If the input contains multiple sub-images, each is written to a\n"
	"\t   different file (\"FILE\" -> \"FILE_#subindex.ext\").\n\n"
	"\t * If the output doesn't support animations, each frame is written\n"
	"\t   to a different file, containing the sub-image index, frame number,\n"
	"\t   and the duration in seconds as numerator and denominator\n"
	"\t   (\"FILE\" -> \"FILE_#subindex.#frame.#num.#den.ext\").\n\n"
	"\t * Input colorspace is preserved when supported by the output, otherwise\n"
	"\t   the image is converted to sRGB.\n"
;

enum write_opts_c {
	wo_done = 0,
	wo_d = 'd',
	wo_e = 'e',
	wo_f = 'f',
	wo_s = 's',
	wo_t = opts_global_type,
	wo_z = 'z',
};

static const struct opts write_opts[] = {
	{'d', "", "OUTDIR",
		"\t\tWrite files to OUTDIR instead of the file's parent."},
	{'e', "", "ENCODER",
		"\t\tOutput format. Supported encoders are\n"
#ifdef WU_ENABLE_JPEGXL
		"\t\t* jxl: animation, ICC profiles, floating-point data\n"
#endif
		"\t\t* pam\n"
		"\t\t* raw: dumps all decoded data senselessly"},
	{'f', "force", "",
		"\t\tForce overwriting output file(s)."},
	{'s', "stdout", "",
		"\t\tWrite only the initial sub-image to stdout."},
	OPTS_TYPE,
	{'z', "null", "",
		"\t\tUse null as line terminator when printing filenames."},
};

static int find_codec(const char *ext) {
	for (int i = 0; i < (int)ARRAY_LEN(ENC_TABLE); ++i) {
		if (!strncmp(ext, ENC_TABLE[i].ext, sizeof(ENC_TABLE[i].ext))) {
			return i;
		}
	}
	return -1;
}

const char * write_args(const int argc, char *const *argv, int *idx,
struct write_args *args) {
	*args = (struct write_args){0};
	for (;;) {
		const enum write_opts_c c = opts_next(argc, argv, idx,
			write_opts, ARRAY_LEN(write_opts));
		switch (c) {
		case wo_d:
			args->outdir = argv[*idx];
			break;
		case wo_e:
			args->codec = find_codec(argv[*idx]);
			if (args->codec < 0) {
				return OPTS_UNKNOWN_ENCODER;
			}
			break;
		case wo_f: args->overwrite = true; break;
		case wo_s: args->stdout = true; break;
		case wo_t:
			args->fmt = fmtmap_by_name(argv[*idx]);
			if (!args->fmt) {
				return OPTS_UNKNOWN_DECODER;
			}
			break;
		case wo_z: args->null = true; break;
		case wo_done:
			return NULL;
		}
		++*idx;
	}
	return NULL;
}

void write_help(const char *preamble, FILE *out) {
	opts_help(preamble, write_opts, ARRAY_LEN(write_opts), out);
}
