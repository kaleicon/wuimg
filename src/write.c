// SPDX-License-Identifier: 0BSD
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

#include "enc/pam.h"

struct write_path {
	struct wustr parent;
	struct wustr file;
	size_t name_base;
	int dirfd;
	bool with_idx;
};

static size_t write_pam(const struct wuimg *dst, FILE *ofp,
struct write_writer *writer) {
	pam_write_header(dst, ofp);
	size_t w = 0;
	for (size_t y = 0; y < dst->h; ++y) {
		w += pam_write_row(writer->get_row(writer->state, y), dst, ofp);
	}
	return w;
}

static const char * write_sub_img(const struct wuimg *src, FILE *ofp,
struct write_writer *writer) {
	const char *err_msg = NULL;
	struct wuimg dst = {0};
	pam_best_fit(&dst, src);
	if (wuimg_verify(&dst) == wu_ok) {
		const watch_t w = watch_look();
		err_msg = writer->set_image(writer->state, &dst, src);
		if (!err_msg) {
			write_pam(&dst, ofp, writer);
			watch_report("Converted", w, report_info);
		}
		writer->close(writer->state);
	} else {
		err_msg = "Output image failed verification. This is likely a"
			" programmer oversight.";
	}
	return err_msg;
}

static const size_t SUFFIX_SPACE = sizeof(int)*3*2 // index and frame number
	+ sizeof(uint32_t)*3*2 // frame time numerator and denominator
	+ 5 // delimiters and extension dot
	+ 3 // extension
	+ 1; // ending nul

static FILE * create_file(struct write_path *path, const struct wu_state *state,
const bool overwrite, const struct image_frames *frames) {
	const char ext[] = "pam";
	char *suffix = (char *)path->file.str + path->name_base;
	const size_t rem = SUFFIX_SPACE;

	const int prec = 5;
	int w;
	if (frames) {
		const struct frame_time sec = frames->f[state->frame].sec;
		w = snprintf(suffix, rem,
			"_%.*d:%.*d:%" PRIu32 ":%" PRIu32 ".%s",
			prec, state->idx,
			prec, state->frame,
			sec.num, sec.den,
			ext);
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

bool write_image(struct image_context *image, const struct write_args *args,
struct write_writer *writer) {
	struct wuimg *cur;
	enum wu_error err = dec_iter(image, &cur);
	if (err != wu_ok) {
		dec_free(image);
		fprintf(stderr, "Error while opening %s: %s\n", image->name,
			wu_error_message(err));
		return false;
	}

	struct write_path path;
	errno = 0;
	bool all_ok = set_write_path(&path, args->outdir, image);
	if (all_ok) {
		do {
			FILE *ofp = get_file(args, &path, image, cur);
			if (!ofp) {
				print_write_error(&path, strerror(errno), stderr);
				all_ok = false;
				if (args->stdout) {
					break;
				}
				continue;
			}
			const char *msg = write_sub_img(cur, ofp, writer);
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
		} while (wu_ok == (err = dec_iter(image, &cur)));
		switch (err) {
		case wu_no_change: case wu_ok:
			break;
		default:
			fprintf(stderr, "Error while processing %s: %s\n",
				image->name, wu_error_message(err));
			all_ok = false;
		}
		free_write_path(&path);
	} else {
		perror("Failed to open output directory");
	}
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
