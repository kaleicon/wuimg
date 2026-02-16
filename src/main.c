// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2019 kaleido
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "dec.h"
#include "display.h"
#include "events.h"
#include "extract.h"
#include "filesystem.h"
#include "fmtmap.h"
#include "opts.h"
#include "write.h"
#include "misc/file.h"
#include "misc/math.h"
#include "misc/mparser.h"
#include "misc/time.h"

enum work_mode {
	mode_guess = 0,
	mode_error,
	mode_help = 'h',
	mode_keys = 'k',
	mode_formats = 'f',
	mode_envs = 'e',
	mode_directory = 'd',
	mode_sole = 's',
	mode_archive = 'a',
	mode_test = 't',
	mode_write = 'w',
};

struct file_list {
	bool dynamic;
	size_t nr;
	char **name;
};

struct sole_mode_args {
	struct wuptr raw;
	const struct fmt_desc *fmt;
};

struct test_mode_args {
	unsigned int iters;
	unsigned int warmup;
	bool metadata;
};

struct program_mode {
	enum work_mode type;
	union mode_args {
		struct sole_mode_args sole;
		struct test_mode_args test;
		struct write_args write;
	} arg;
};

static void list_remove_entry(struct file_list *entries, long pos) {
	if (entries->dynamic) {
		free(entries->name[pos]);
	}
	entries->name[pos] = NULL;
}

static void list_free(struct file_list *entries) {
	for (size_t i = 0; i < entries->nr; ++i) {
		free(entries->name[i]);
	}
	free(entries->name);
}

static int lsign(const long i) {
	return i < 0 ? -1 : 1;
}

static void pos_print(const size_t i, const struct file_list *entries) {
	FILE *out = stdout;
	fprintf(out, "%zu/%zu, ", i+1, entries->nr);
	term_print_convert(entries->name[i], out);
	fputc('\n', out);
}

static void conv_get_row(void *state, size_t y, void *restrict tgt) {
	gl_reader_read_row(state, y, tgt);
}
static const char * conv_set_image(void *state, const struct wuimg *dst,
const struct wuimg *src) {
	return gl_reader_set(state, dst, src);
}

static enum wu_error convert_files(const struct file_list *entries,
const struct write_args *args) {
	struct window_offscreen window = {0};
	struct gl_reader_context reader = {0};
	struct wu_conf conf = conf_no_window();
	const char *err = display_offscreen_setup(&window, &reader, &conf);
	if (err) {
		term_line_key_val("Couldn't initialize offscreen context", err,
			stderr);
		return 1;
	}

	struct write_writer writer = {
		.state = &reader,
		.set_image = conv_set_image,
		.get_row = conv_get_row,
	};
	const int r = write_filelist(args, &writer, (int)entries->nr,
		entries->name, &conf);
	window_offscreen_terminate(&window);
	return r;
}

static enum wu_error test_with(const struct file_list *entries,
struct test_mode_args args) {
	FILE *out = stdout;
	if (args.metadata) {
		args.warmup = 1;
		args.iters = 0;
	} else {
		fprintf(out,
			"Testing %u times with %u extra runs for warmup.\n\n",
			args.iters, args.warmup);
	}

	struct wudec_image image = {
		.conf = conf_no_window(),
	};

	enum wu_error result = wu_ok;
	size_t failures = 0;
	watch_t grand_total = 0;
	for (size_t i = 0; i < entries->nr; ++i) {
		watch_t taken = 0;
		const unsigned it = args.warmup + args.iters;

		result = wu_ok;
		for (unsigned j = 0; j < it && result == wu_ok; ++j) {
			wudec_src_filename(&image, entries->name[i]);
			const watch_t watch = watch_look();
			do {
				struct wuimg *img;
				result = wudec_iter(&image, &img);
			} while (result == wu_ok);
			if (result == wu_no_change) {
				result = wu_ok;
				if (args.metadata) {
					fputs("File: ", out);
					term_print_convert(image.file.name, out);
					image_file_print(&image.file, out, 3, true);
					fputc('\n', out);
				} else {
					taken += watch_elapsed(watch) * (j >= args.warmup);
				}
			}
			wudec_recycle_conf(&image);
		}

		if (result == wu_ok) {
			if (args.iters) {
				grand_total += taken;
				fprintf(out, "Average: %" PRIu64 " ",
					taken / args.iters);
				term_print_convert(entries->name[i], out);
				fputc('\n', out);
			}
		} else {
			fputs("Error in ", out);
			term_print_convert(entries->name[i], out);
			fprintf(out, ": %s\n", wu_error_str(result));
			++failures;
		}
	}

	fputc('\n', out);
	if (entries->nr * args.iters > 1) {
		fprintf(out, "Total: %" PRIu64 "\n", grand_total);
	}
	fprintf(out, "%zu successful, %zu failed\n", entries->nr - failures,
		failures);
	return result;
}

static void pos_print_archive(long idx, const struct extract_iter *iter,
const char *archive_name) {
	FILE *out = stdout;
	fprintf(out, "%ld/%ld%s, ", idx + 1, iter->total,
		iter->seen_it_all ? "" : "?");
	term_print_convert(archive_name, out);
	fputc('/', out);
	term_print_convert(iter->name, out);
	fputc('\n', out);
}

static enum wu_error run_with_archive(const char *archive_name) {
	struct extract_iter iter;
	if (!extract_init(&iter, archive_name)) {
		fputs("Failed to open ", stderr);
		term_print_convert(archive_name, stderr);
		fputc('\n', stderr);
		return wu_open_error;
	}

	struct window_context window = {
		.pub.image.conf = conf_load(),
	};

	struct term_restore tr;
	if (!display_setup(&window, &tr)) {
		return wu_display_error;
	}

	struct wudec_image *image = &window.pub.image;
	struct wu_event *event = &window.pub.event;
	enum wu_error result = wu_ok;
	long idx = 0;
	bool decoded_once = false;
	while (!event->exit && extract_file(&iter, idx, decoded_once)) {
		const int direction = lsign(event->cycle);
		idx = lmod(iter.idx, iter.total);

		pos_print_archive(idx, &iter, archive_name);

		wudec_src_file(image, iter.cur, iter.name, true, true);
		result = display_loop(&window, true, false);
		wudec_recycle_state(image);
		putchar('\n');
		if (result == wu_ok) {
			decoded_once = true;
		} else {
			event->cycle = direction;
		}
		idx += event->cycle;
	}

	display_end(&window, &tr);
	extract_free(&iter);
	return result;
}

static enum wu_error run_with_list(struct file_list *entries, long idx,
const bool interpret_stdin, const struct sole_mode_args *args) {
	struct window_context window = {
		.pub.image.conf = conf_load(),
	};

	struct term_restore tr;
	if (!display_setup(&window, &tr)) {
		return wu_display_error;
	}

	FILE *stdin_tmp = NULL;
	struct wudec_image *image = &window.pub.image;
	struct wu_event *event = &window.pub.event;
	enum wu_error result = wu_ok;
	size_t remaining = entries->nr;
	while (!event->exit && remaining) {
		const int direction = lsign(event->cycle);
		while (!entries->name[idx]) {
			idx = lmod(idx + direction, (long)entries->nr);
		}
		pos_print((size_t)idx, entries);

		bool free_entry = false;
		const char *name = entries->name[idx];
		if (interpret_stdin && !strcmp("-", name)) {
			if (!stdin_tmp) {
				stdin_tmp = file_from_stdin();
			}
			if (stdin_tmp) {
				rewind(stdin_tmp);
				wudec_src_file(image, stdin_tmp, name, true, false);
			} else {
				free_entry = true;
			}
			name = NULL;
		} else {
			wudec_src_filename(image, name);
		}
		if (args) {
			if (args->raw.ptr) {
				wudec_src_auto_desc(image, &args->raw);
			} else {
				wudec_src_format(image, args->fmt);
			}
		}

		if (!free_entry) {
			result = display_loop(&window, remaining > 1, true);
			wudec_recycle_state(image);
			if (result != wu_ok) {
				free_entry = true;
			} else if (event->rm == rm_yes) {
				unlink(name);
				puts("File deleted.");
				free_entry = true;
			}
		}
		if (free_entry) {
			list_remove_entry(entries, idx);
			--remaining;
			event->cycle = 1;
		}
		putchar('\n');

		idx = lmod(idx + event->cycle, (long)entries->nr);
	}

	display_end(&window, &tr);
	if (stdin_tmp) {
		fclose(stdin_tmp);
	}
	return result;
}

static enum wu_error from_argv(const size_t argc, char **argv,
const struct program_mode *mode) {
	if (argc) {
		struct file_list entries = {
			.dynamic = false,
			.nr = argc,
			.name = argv,
		};

		switch (mode->type) {
		case mode_test: return test_with(&entries, mode->arg.test);
		case mode_write: return convert_files(&entries, &mode->arg.write);
		default: break;
		}
		return run_with_list(&entries, 0, true, &mode->arg.sole);
	}
	term_line_put("ERROR: No files given", stderr);
	return 1;
}

static enum wu_error from_path(const char *name) {
	size_t start_idx;
	errno = 0;
	const watch_t start = watch_look();
	struct file_list entries = {
		.dynamic = true,
		.name = fs_filter_sort(name, &entries.nr, &start_idx),
	};
	watch_report("Filenames sorted", start, report_all);

	enum wu_error result;
	if (entries.name) {
		result = run_with_list(&entries, (long)start_idx, false, NULL);
		list_free(&entries);
	} else {
		if (errno) {
			perror("Error while filtering images");
		} else {
			fputs("ERROR: ", stderr);
			term_print_convert(name[0] ? name : ".", stderr);
			fputs(" is neither a valid file or directory with"
				"identifiable images.\n", stderr);
		}
		result = wu_open_error;
	}
	return result;
}

static const struct opts sole_opts[] = {
	{'r', "", "STRING",
		"\t\tRead raw data using settings from STRING. Example:\n"
		"\t\t\t'w:320 h:240 channels:4 bitdepth:8 layout:bgra'\n"
		"\t\tOne may also perform rudimentary reads and seeks:\n"
		"\t\t\t'endian:little c:1 b:1 w:<u16> h:<u16> skip:0x80'\n"
		"\t\tA complete description is yet to be written..."},
	{'t', "type", "ID",
		"\t\tForce input decoder. ID must be one of the decoders\n"
		"\t\tlisted with `--fmts`."},
};

static const char * sole_args(const int argc, char *const *argv, int *idx,
struct sole_mode_args *args) {
	*args = (struct sole_mode_args){0};
	for (;;) {
		const uint8_t c = opts_next(argc, argv, idx, sole_opts,
			ARRAY_LEN(sole_opts));
		switch (c) {
		case 'r':
			args->raw = wuptr_str(argv[*idx]);
			break;
		case 't':
			args->fmt = fmtmap_by_name(argv[*idx]);
			if (!args->fmt) {
				return "unknown decoder";
			}
			break;
		default: return NULL;
		}
		++*idx;
	}
	return NULL;
}

static const struct opts test_opts[] = {
	{'m', "", "",
		"\t\tPrint full metadata for each file. Decode only once."},
	{'n', "", "N",
		"\t\tDecode each file N times. Default is 1."},
	{'w', "", "N",
		"\t\tDecode N times before measuring. Default is 0."},
};

static const char * test_args(const int argc, char *const *argv, int *idx,
struct test_mode_args *args) {
	*args = (struct test_mode_args) {
		.iters = 1,
		.warmup = 0,
	};
	for (;;) {
		const uint8_t c = opts_next(argc, argv, idx, test_opts,
			ARRAY_LEN(test_opts));
		unsigned *dst;
		switch (c) {
		case 'm': args->metadata = true; ++*idx; continue;
		case 'n': dst = &args->iters; break;
		case 'w': dst = &args->warmup; break;
		default: return NULL;
		}

		const char *arg = argv[*idx];
		struct mparser mp = mp_mem(strlen(arg), arg);
		uintmax_t tmp;
		if (mp_scan_uint_unsafe(&mp, &tmp) && !mp_next_char_unsafe(&mp)) {
			*dst = (unsigned)tmp;
			++*idx;
		} else {
			return "bad number argument";
		}
	}
	return NULL;
}

enum global_opt_c {
	go_done = 0,
	go_h = 'h',
	go_fmts = 0x80,
	go_keys,
	go_envs,
};

static const struct opts global_opts[] = {
	{go_h, "help", "",
		"\t\tYou are here."},
	{go_fmts, "fmts", "",
		"\t\tPrint supported formats."},
	{go_keys, "keys", "",
		"\t\tPrint keybinds."},
	{go_envs, "envs", "",
		"\t\tPrint recognized environment variables."},
};

#define DIRECTORY_MODE "directory"
#define SOLE_MODE "sole"
#define ARCHIVE_MODE "archive"
#define WRITE_MODE "write"
#define TEST_MODE "test"

static void print_help(FILE *out) {
	opts_help("Usage:\n"
		"\t" WU_CANON_NAME "\t(read images from \".\")\n"
		"\t" WU_CANON_NAME " DIR\t(read from DIR)\n"
		"\t" WU_CANON_NAME " FILE\t(read from the parent of FILE, starting with FILE)\n"
		"\t" WU_CANON_NAME " FILE FILE...\t(read only the files given)\n"
		"\t" WU_CANON_NAME " -\t(read image from stdin)\n"
		"\t" WU_CANON_NAME " MODE [OPTIONS]... [--] [PATH]...\t(explicit mode)\n"

		"\n"
		"Program info:",
		global_opts, ARRAY_LEN(global_opts), out);

	opts_help(
		"\n"
		"Operation modes (all exclusive, may be abbreviated):\n"
		"\t" DIRECTORY_MODE "\n"
		"\t\tDisplay images from PATH if it is a directory, from its\n"
		"\t\tparent if it is a file, or from the current directory if\n"
		"\t\tmissing. Assumed when zero or one paths are given.\n"

		"\t" SOLE_MODE " [switches]\n"
		"\t\tRead only the file(s) given, in the order given. Assumed\n"
		"\t\twhen more than one path, or '-' (stdin), is given.\n"

		"\t" ARCHIVE_MODE "\n"
		"\t\tDisplay any images inside FILE, which must be an archive\n"
		"\t\ttype supported by libarchive.\n"

		"\t" TEST_MODE " [switches]\n"
		"\t\tTry decoding each FILE, measuring the elapsed time.\n"

		"\t" WRITE_MODE " [switches]\n"
		"\t\tTranscode the given images, with sub-images and animation\n"
		"\t\tframes on separate files if not supported by the target.\n"
		"\t\tOutput names are written to stdout.\n"
		"\t\tThis converter uses an OpenGL context for rendering.\n"
		"\t\tRefer to `wuconv` for a software converter.\n"

		"\n"
		SOLE_MODE " switches:",
		sole_opts, ARRAY_LEN(sole_opts), out);

	opts_help(
		"\n"
		TEST_MODE " switches:",
		test_opts, ARRAY_LEN(test_opts), out);

	write_help(
		"\n"
		WRITE_MODE " switches:", out);
}

static int get_mode(const int argc, char *const *argv, struct program_mode *mode) {
	int read = 0;
	const char *arg = argv[read];
	const size_t arglen = strlen(arg);
	if (arglen && read < argc) {
		const bool mode_match = !strncmp(arg, ARCHIVE_MODE, arglen)
			|| !strncmp(arg, WRITE_MODE, arglen)
			|| !strncmp(arg, TEST_MODE, arglen)
			|| !strncmp(arg, SOLE_MODE, arglen)
			|| !strncmp(arg, DIRECTORY_MODE, arglen);
		if (mode_match) {
			mode->type = (enum work_mode)arg[0];
			++read;
		}

		const char *err = NULL;
		switch (mode->type) {
		case mode_sole:
		case mode_guess:
			;int prev = read;
			err = sole_args(argc, argv, &read, &mode->arg.sole);
			if (read > prev) {
				mode->type = mode_sole;
			}
			break;
		case mode_test:
			err = test_args(argc, argv, &read, &mode->arg.test);
			break;
		case mode_write:
			err = write_args(argc, argv, &read, &mode->arg.write);
			break;
		default:
			break;
		}
		if (err) {
			fprintf(stderr, "Error while parsing switch '%s': ",
				argv[read - 1]);
			term_line_put(err, stderr);
			mode->type = mode_error;
		} else {
			const enum global_opt_c c = opts_next(argc, argv,
				&read, global_opts, ARRAY_LEN(global_opts));
			switch (c) {
			case go_h: mode->type = mode_help; break;
			case go_fmts: mode->type = mode_formats; break;
			case go_keys: mode->type = mode_keys; break;
			case go_envs: mode->type = mode_envs; break;
			case go_done: break;
			}
		}
	}
	return read;
}

int main(const int argc, char *argv[]) {
	if (argc <= 1) {
		return from_path("");
	}

	struct program_mode mode = {.type = mode_guess};
	int read = 1;
	read += get_mode(argc - read, argv + read, &mode);
	if (read > argc) {
		fatal_bug(__func__, "Excess arguments read.");
	}
	if (read < argc && !strcmp("--", argv[read])) {
		++read;
	}

	const size_t remaining = (size_t)(argc - read);
	if (mode.type == mode_guess) {
		if (!remaining || (remaining == 1 && strcmp("-", argv[read]))) {
			mode.type = mode_directory;
		} else {
			mode.type = mode_sole;
		}
	}

	switch (mode.type) {
	case mode_error:
		return 1;
	case mode_help:
		print_help(stderr);
		return 0;
	case mode_keys:
		event_print_keys(stdout);
		return 0;
	case mode_formats:
		fmtmap_print_known(stdout);
		return 0;
	case mode_envs:
		fputs(WU_ENV_VARIABLES, stderr);
		fputc('\n', stderr);
		fputs(WINDOW_ENV_VARIABLES, stderr);
		return 0;
	case mode_sole:
	case mode_test:
	case mode_write:
		return from_argv(remaining, argv + read, &mode);
	case mode_directory:
		return from_path(remaining ? argv[read] : "");
	case mode_archive:
		if (!remaining) {
			break;
		}
		return run_with_archive(argv[read]);
	case mode_guess:
		fatal_bug(__func__, "Unreachable case reached. Well done.");
		return 1;
	}
	term_line_put("ERROR: Expected at least one path.", stderr);
	return 1;
}
