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
extern const struct enc_fn pam_enc;

static const struct enc_info ENC_TABLE[] = {
	// Entry 0 will be the default
	{"pam", &pam_enc},
#ifdef WU_ENABLE_JPEGXL
	{"jxl", &jpegxl_enc},
#endif
};

struct write_file {
	struct wustr parent;
	struct wustr file;
	size_t name_base;
	FILE *ofp;
	int dirfd;
	bool with_idx;
	bool passthrough; // Skip conversion, pass original image to encoder
	void *enc_state;
	struct wuimg dst;
};

static int find_codec(const char *ext) {
	for (int i = 0; i < (int)ARRAY_LEN(ENC_TABLE); ++i) {
		if (!strncmp(ext, ENC_TABLE[i].ext, sizeof(ENC_TABLE[i].ext))) {
			return i;
		}
	}
	return -1;
}

static const char * write_frame(const struct enc_fn *enc,
const struct wuimg *dst, const struct wuimg *src, struct write_file *out,
struct write_writer *writer, const int frame) {
	const char *msg = out->passthrough
		? NULL : writer->set_image(writer->state, dst, src);
	if (!msg && (frame == 0 || !enc->anim)) {
		msg = enc->init(out->enc_state, dst, src, out->ofp);
	}
	if (!msg) {
		size_t w = 0;
		if (!out->passthrough) {
			const size_t stride = wuimg_stride(dst);
			for (size_t y = 0; y < dst->h; ++y) {
				uint8_t *tgt = dst->data
					+ (enc->write_row ? 0 : stride*y);
				writer->get_row(writer->state, y, tgt);
				if (enc->write_row) {
					w += enc->write_row(out->enc_state, dst,
						out->ofp, tgt);
				}
			}
		}
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
	return msg;
}

static const char * write_sub_img(const struct wuimg *src,
struct write_file *out, struct write_writer *writer, const struct enc_fn *enc,
const int frame) {
	const char *err_msg = NULL;
	if (frame == 0) {
		out->dst = (struct wuimg){0};
		out->passthrough = enc->best_fit(&out->dst, src);
		if (getenv("WU_DEBUG")) {
			fprintf(stderr, "passthrough: %s\n",
				out->passthrough ? "yes" : "no");
		}
		if (!out->passthrough) {
			if (wuimg_verify(&out->dst) == wu_ok) {
				const size_t rows = enc->write_row ? 1 : out->dst.h;
				out->dst.data = malloc(wuimg_stride(&out->dst) * rows);
				if (!out->dst.data) {
					err_msg = "Output image allocation failure";
				}
			} else {
				err_msg = "Output image failed verification."
					" This is likely a programmer oversight.";
			}
		}
	}
	if (!err_msg) {
		const watch_t w = watch_look();
		const struct wuimg *dst = out->passthrough ? src : &out->dst;
		err_msg = write_frame(enc, dst, src, out, writer, frame);
		watch_report("Converted", w, report_info);
	}
	return err_msg;
}

static const size_t SUFFIX_SPACE = sizeof(int)*3*2 // index and frame number
	+ sizeof(uint32_t)*3*2 // frame time numerator and denominator
	+ 5 // delimiters and extension dot
	+ sizeof(ENC_TABLE->ext) // extension
	+ 1; // ending nul

static FILE * create_file(struct write_file *out, const struct wu_state *state,
const bool overwrite, const struct image_frames *frames, const char ext[static 4]) {
	char *suffix = (char *)out->file.str + out->name_base;
	const size_t rem = SUFFIX_SPACE;

	const int prec = 5;
	const int ext_len = sizeof(ENC_TABLE->ext);
	int w;
	if (frames) {
		const struct frame_time sec = frames->f[state->frame].sec;
		w = snprintf(suffix, rem,
			"_%.*d:%.*d:%" PRIu32 ":%" PRIu32 ".%.*s",
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
		errno = 0;
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

static const char * get_file(struct write_file *out,
const struct write_args *args, const struct image_context *image,
struct wuimg *src) {
	const char *msg = NULL;
	if (!out->ofp) {
		if (args->stdout) {
			out->ofp = stdout;
		} else {
			const bool supports_anim = ENC_TABLE[args->codec].enc->anim;
			errno = 0;
			out->ofp = create_file(out, &image->state, args->overwrite,
				supports_anim ? NULL : src->frames,
				ENC_TABLE[args->codec].ext);
			msg = out->ofp ? NULL : strerror(errno);
		}
	}
	return msg;
}

static bool close_file(struct write_file *out, const struct write_args *args,
const struct image_context *image, const struct wuimg *src, const bool failed) {
	const bool final_frame = failed
		|| (size_t)(image->state.frame + 1) == wuimg_frames_nr(src);
	const struct enc_fn *enc = ENC_TABLE[args->codec].enc;
	if (final_frame || (!enc->anim && args->stdout)) {
		wuimg_free(&out->dst);
	}
	if (final_frame || !enc->anim) {
		if (enc->end) {
			enc->end(out->enc_state);
		}
		if (out->ofp) {
			fclose(out->ofp);
			out->ofp = NULL;
			return true;
		}
	}
	return false;
}

static void free_write_file(struct write_file *out) {
	if (out->dirfd >= 0) {
		close(out->dirfd);
	}
	wustr_free(&out->parent);
	wustr_free(&out->file);
	free(out->enc_state);
}

static void print_write_file(const struct write_file *out, FILE *ofp) {
	wustr_print(&out->parent, ofp);
	wustr_print(&out->file, ofp);
}

static void print_write_error(const struct write_file *out, const char *msg,
FILE *ofp) {
	fputs("Failed to write to ", ofp);
	print_write_file(out, ofp);
	fputs(": ", ofp);
	term_line_put(msg, ofp);
}

static bool init_write_file(struct write_file *out,
const struct write_args *args, const struct image_context *image) {
	struct fs_path path;
	out->dirfd = fs_get_parent_dir(&path,
		args->outdir ? args->outdir : image->name, false);
	if (out->dirfd >= 0) {
		fs_path_set_file(&path, wuptr_str(image->name));
		out->name_base = path.file.len;
		out->parent = path.parent;
		if (wustr_malloc(&out->file, out->name_base + SUFFIX_SPACE)) {
			memcpy(out->file.str, path.file.ptr, out->name_base);
			out->with_idx = image->file.nr > 1;
			out->enc_state = malloc(ENC_TABLE[args->codec].enc->state_size);
			return out->enc_state;
		}
	}
	return false;
}

static void print_dec_error(const enum wu_error e, const char *what,
const struct image_context *image) {
	fprintf(stderr, "Error while %s %s: ", what, image->name);
	image_file_error_print(&image->file, e, stderr);
}

bool write_image(struct image_context *image, const struct write_args *args,
struct write_writer *writer) {
	struct wuimg *cur;
	enum wu_error err = dec_iter(image, &cur);
	if (err != wu_ok) {
		print_dec_error(err, "opening", image);
		dec_free(image);
		return false;
	}

	struct write_file out = {0};
	errno = 0;
	bool all_ok = init_write_file(&out, args, image);
	if (all_ok) {
		do {
			const char *msg = get_file(&out, args, image, cur);
			if (!msg) {
				msg = write_sub_img(cur, &out, writer,
					ENC_TABLE[args->codec].enc,
					image->state.frame);
			}
			bool print = close_file(&out, args, image, cur, msg);
			if (msg) {
				print_write_error(&out, msg, stderr);
				all_ok = false;
				break;
			}
			if (print) {
				if (args->stdout) {
					break;
				}
				print_write_file(&out, stdout);
				fputc(args->null ? 0 : '\n', stdout);
			}
		} while (wu_ok == (err = dec_iter(image, &cur)));
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
	dec_free(image);
	return all_ok;
}

int write_filelist(const struct write_args *args, struct write_writer *writer,
const int len, char **names, const struct wu_conf *conf) {
	struct image_context image = {
		.conf = conf ? *conf : conf_no_window(),
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
			dec_src_file(&image, stdin_cpy, "stdin", false, false);
		} else {
			dec_src_filename(&image, name);
		}

		ok += write_image(&image, args, writer);
		image_reset(&image);
		if (args->stdout) {
			break;
		}
	}
	return ok != len;
}

const char write_description[] =
	"\tConvert each FILE to FILE[_sub:frame:num:den].pam, with sub-images\n"
	"\tand animation frames on separate files. Output names are written\n"
	"\tto stdout.\n"
	"\tFor images with multiple sub-images, output names contain the\n"
	"\tsub-image index.\n"
	"\tWhen a sub-image is an animation, name additionally contains the\n"
	"\tframe index, then the frame duration in seconds expressed as\n"
	"\tnumerator and denominator.\n"
;

const char write_switches[] =
	"\t-d OUTDIR\n"
	"\t\tWrite files to OUTDIR instead of the file's parent.\n"

	"\t-e ENCODER\n"
	"\t\tOutput format. Supported encoders are\n"
#ifdef WU_ENABLE_JPEGXL
	"\t\t* jxl\n"
#endif
	"\t\t* pam\n"

	"\t-f\n"
	"\t\tForce overwriting output file(s).\n"

	"\t-s\n"
	"\t\tWrite only the initial sub-image to stdout.\n"

	"\t-z\n"
	"\t\tUse null as line terminator when printing filenames.\n";

int write_args(const int argc, char **argv, struct write_args *args) {
	int idx = 0;
	*args = (struct write_args){0};
	while (idx < argc) {
		switch (short_opt(argv[idx])) {
		case 'h': return -1;
		case 'd':
			if (idx + 1 >= argc) {
				return idx;
			}
			++idx;
			args->outdir = argv[idx];
			break;
		case 'e':
			if (idx + 1 >= argc) {
				return idx;
			}
			++idx;
			args->codec = find_codec(argv[idx]);
			if (args->codec < 0) {
				return -1;
			}
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
