// SPDX-License-Identifier: 0BSD
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
#include "write.h"
#include "misc/math.h"

enum work_mode {
	guess = 0,
	help = 'h',
	keys = 'k',
	formats = 'f',
	directory = 'd',
	sole = 's',
	archive = 'a',
	test = 't',
	writeout = 'w',
};

struct file_list {
	bool dynamic;
	size_t nr;
	char **name;
};

struct test_mode_args {
	unsigned int iters;
	unsigned int warmup;
	bool metadata;
};

struct program_mode {
	enum work_mode type;
	union mode_args {
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
	printf("%zu/%zu, %s\n", i+1, entries->nr, entries->name[i]);
}

static FILE * save_stdin(void) {
	FILE *tmp = tmpfile();
	if (tmp) {
		unsigned char buf[BUFSIZ];
		size_t read;
		while ( (read = fread(buf, 1, sizeof(buf), stdin)) ) {
			fwrite(buf, 1, read, tmp);
		}
		rewind(tmp);
	}
	return tmp;
}

static struct wu_conf conv_conf(struct wu_conf conf) {
	conf.svg_window_adapt = false;
	return conf;
}

static enum wu_error convert_files(const struct file_list *entries,
const struct write_args *args) {
	struct image_context image = {
		.conf = conv_conf(conf_load()),
	};
	FILE *stdin_cpy = NULL;
	struct write_writer writer = {0};
	size_t w = 0;
	for (size_t i = 0; i < entries->nr; ++i) {
		const char *name = entries->name[i];
		if (!strcmp("-", name)) {
			stdin_cpy = save_stdin();
			if (!stdin_cpy) {
				term_line_put("Failed to save stdin", stderr);
				continue;
			}
			dec_src_file(&image, stdin_cpy, "stdin", false, false);
		} else {
			dec_src_filename(&image, name);
		}

		const bool no_prob = write_image(&image, &writer, args);
		w += no_prob;
		image_reset(&image);
		if (args->stdout) {
			break;
		}
		if (stdin_cpy) {
			stdin_cpy = NULL;
		}
	}
	write_writer_terminate(&writer);
	return w != entries->nr;
}

static enum wu_error test_with(const struct file_list *entries,
struct test_mode_args args) {
	if (args.metadata) {
		args.warmup = 1;
		args.iters = 0;
	} else {
		printf("Testing %u times with %u extra runs for warmup.\n\n",
			args.iters, args.warmup);
	}

	struct image_context image = {
		.conf = conv_conf(conf_load()),
	};

	enum wu_error result = wu_ok;
	size_t failures = 0;
	watch_t grand_total = 0;
	for (size_t i = 0; i < entries->nr; ++i) {
		watch_t taken = 0;
		const unsigned it = args.warmup + args.iters;

		result = wu_ok;
		for (unsigned j = 0; j < it && result == wu_ok; ++j) {
			image_reset(&image);
			dec_src_filename(&image, entries->name[i]);
			const watch_t watch = watch_look();
			do {
				struct wuimg *img;
				result = dec_iter(&image, &img);
			} while (result == wu_ok);
			if (result == wu_no_change) {
				result = wu_ok;
				if (args.metadata) {
					image_file_print(&image.file, 3, true);
				} else {
					taken += watch_elapsed(watch) * (j >= args.warmup);
				}
			}
			dec_free_image(&image);
		}

		if (result == wu_ok) {
			if (args.iters) {
				grand_total += taken;
				printf("Average: %" PRIu64 " %s\n",
					taken / args.iters, entries->name[i]);
			}
		} else {
			printf("Error in %s: %s\n", entries->name[i],
				wu_error_message(result));
			++failures;
		}
	}
	putchar('\n');

	if (entries->nr * args.iters > 1) {
		printf("Total: %" PRIu64 "\n", grand_total);
	}
	printf("%zu successful, %zu failed\n", entries->nr - failures,
		failures);
	return result;
}

static enum wu_error run_with_archive(const char *archive_name) {
	struct extract_iter iter;
	if (!extract_init(&iter, archive_name)) {
		fprintf(stderr, "Failed to open %s\n", archive_name);
		return wu_open_error;
	}

	struct window_context window = {
		.pub.image.conf = conf_load(),
	};

	struct term_restore tr;
	if (!display_setup(&window, &tr)) {
		return wu_display_error;
	}

	struct image_context *image = &window.pub.image;
	struct wu_event *event = &window.pub.event;
	enum wu_error result = wu_ok;
	long idx = 0;
	while (!event->exit && extract_file(&iter, idx)) {
		const int direction = lsign(event->cycle);
		idx = lmod(iter.idx, iter.total);

		printf("%ld/%ld%s, %s/%s\n", idx + 1, iter.total,
			iter.seen_it_all ? "" : "?", archive_name, iter.name);

		image_reset(image);
		dec_src_file(image, iter.cur, iter.name, true, true);
		result = display_loop(&window, true, false);
		dec_free_image(image);
		putchar('\n');
		if (result != wu_ok) {
			event->cycle = direction;
		}
		idx += event->cycle;
	}

	display_end(&window, &tr);
	extract_free(&iter);
	return result;
}

static enum wu_error run_with_list(struct file_list *entries, long idx,
const bool interpret_stdin) {
	struct window_context window = {
		.pub.image.conf = conf_load(),
	};

	struct term_restore tr;
	if (!display_setup(&window, &tr)) {
		return wu_display_error;
	}

	FILE *stdin_tmp = NULL;
	struct image_context *image = &window.pub.image;
	struct wu_event *event = &window.pub.event;
	enum wu_error result = wu_ok;
	size_t remaining = entries->nr;
	while (!event->exit && remaining) {
		const int direction = lsign(event->cycle);
		while (!entries->name[idx]) {
			idx = lmod(idx + direction, (long)entries->nr);
		}
		pos_print((size_t)idx, entries);
		image_reset(image);

		bool free_entry = false;
		const char *name = entries->name[idx];
		if (interpret_stdin && !strcmp("-", name)) {
			if (!stdin_tmp) {
				stdin_tmp = save_stdin();
			}
			if (stdin_tmp) {
				rewind(stdin_tmp);
				dec_src_file(image, stdin_tmp, name, true, false);
			} else {
				free_entry = true;
			}
		} else {
			dec_src_filename(image, name);
		}

		if (!free_entry) {
			result = display_loop(&window, remaining > 1, true);
			dec_free_image(image);
			if (result != wu_ok) {
				free_entry = true;
			} else if (event->rm == rm_yes) {
				unlink(image->name);
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
		case test: return test_with(&entries, mode->arg.test);
		case writeout: return convert_files(&entries, &mode->arg.write);
		default: break;
		}
		return run_with_list(&entries, 0, true);
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
	watch_report("Filenames sorted", start, report_whocares);

	enum wu_error result;
	if (entries.name) {
		result = run_with_list(&entries, (long)start_idx, false);
		list_free(&entries);
	} else {
		if (errno) {
			perror("Error while filtering images");
		} else {
			fprintf(stderr, "ERROR: %s is neither a valid file or "
				"directory with identifiable images.\n",
				name[0] ? name : ".");
		}
		result = wu_open_error;
	}
	return result;
}

#define HELP_SHORT "-h"
#define KEYS_SHORT "-k"
#define FMTS_SHORT "-f"
#define HELP_LONG "--help"
#define KEYS_LONG "--keys"
#define FMTS_LONG "--fmts"
#define DIRECTORY_MODE "directory"
#define SOLE_MODE "sole"
#define ARCHIVE_MODE "archive"
#define WRITE_MODE "write"
#define TEST_MODE "test"

static void print_help(void) {
	puts("Usage:\n"
		"\t" WU_CANON_NAME "\t(read images from \".\")\n"
		"\t" WU_CANON_NAME " DIR\t(read from DIR)\n"
		"\t" WU_CANON_NAME " FILE\t(read from the parent of FILE, starting with FILE)\n"
		"\t" WU_CANON_NAME " FILE FILE...\t(read only the files given)\n"
		"\t" WU_CANON_NAME " -\t(read image from stdin)\n"
		"\t" WU_CANON_NAME " MODE [OPTIONS]... [--] [PATH]...\t(explicit mode)\n"

		"\n"
		"Program info:\n"
		"\t" HELP_SHORT " | " HELP_LONG "\n"
		"\t\tYou are here.\n"

		"\t" KEYS_SHORT " | " KEYS_LONG "\n"
		"\t\tPrint keybinds.\n"

		"\t" FMTS_SHORT " | " FMTS_LONG "\n"
		"\t\tPrint supported formats.\n"

		"\n"
		"Work modes (all exclusive, may be abbreviated):\n"
		"\t" DIRECTORY_MODE "\n"
		"\t\tDisplay images from PATH if it is a directory, from its\n"
		"\t\tparent if it is a file, or from the current directory if\n"
		"\t\tmissing. Assumed when zero or one paths are given.\n"

		"\t" SOLE_MODE "\n"
		"\t\tRead only the file(s) given, in the order given. Assumed\n"
		"\t\twhen more than one path, or \"-\" (stdin), is given.\n"

		"\t" ARCHIVE_MODE "\n"
		"\t\tDisplay any images inside FILE, which must be an archive\n"
		"\t\tformat supported by libarchive.\n"

		"\t" WRITE_MODE " [switches]\n"
		"\t\tDecode FILE to FILE(_sub#:frame#).pam, with each sub-image\n"
		"\t\tand animation frame on separate files. Output names are\n"
		"\t\twritten to stdout one per line.\n"

		"\t" TEST_MODE " [switches]\n"
		"\t\tTry decoding each FILE, while measuring the elapsed time.\n"

		"\n"
		WRITE_MODE " switches:\n"
		"\t-d OUTDIR\n"
		"\t\tWrite files to OUTDIR instead of the file's parent.\n"

		"\t-f\n"
		"\t\tForce overwriting output file(s).\n"

		"\t-s\n"
		"\t\tWrite only the initial sub-image to stdout.\n"

		"\t-z\n"
		"\t\tUse null as line terminator when printing filenames.\n"

		"\n"
		TEST_MODE " switches:\n"
		"\t-n N\n"
		"\t\tDecode each file N times. Default is 1.\n"

		"\t-w N\n"
		"\t\tDecode N times for warmup before measuring. Default is 0\n"

		"\t-m\n"
		"\t\tPrint full metadata for each file. Decode only once."
	);
}

static int test_args(const int argc, char **argv, struct test_mode_args *args) {
	*args = (struct test_mode_args) {
		.iters = 1,
		.warmup = 0,
	};
	int read = 0;
	while (read < argc) {
		unsigned int *ptr;
		switch (short_opt(argv[read])) {
		case 'n': ptr = &args->iters; break;
		case 'w': ptr = &args->warmup; break;
		case 'm': args->metadata = true; ++read; continue;
		case 'h': return -1;
		default: return read;
		}

		struct mparser mp = mp_mem(strlen(argv[read+1]), argv[read+1]);
		long tmp;
		if (mp_scan_uint_unsafe(&mp, &tmp) && !mp_next_char_unsafe(&mp)) {
			*ptr = (unsigned)tmp;
			read += 2;
		} else {
			break;
		}
	}
	return read;
}

static int get_mode(const int argc, char **argv, struct program_mode *mode) {
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
		} else {
			if (!strcmp(arg, HELP_SHORT)
			|| !strcmp(arg, HELP_LONG)) {
				mode->type = help;
			} else if (!strcmp(arg, KEYS_SHORT)
			|| !strcmp(arg, KEYS_LONG)) {
				mode->type = keys;
			} else if (!strcmp(arg, FMTS_SHORT)
			|| !strcmp(arg, FMTS_LONG)) {
				mode->type = formats;
			}
			return 0;
		}

		++read;
		int r = 0;
		switch (mode->type) {
		case test:
			r = test_args(argc - read, argv + read,
				&mode->arg.test);
			break;
		case writeout:
			r = write_args(argc - read, argv + read,
				&mode->arg.write);
			break;
		default:
			break;
		}
		if (r < 0) {
			mode->type = help;
			return 0;
		}
		read += r;
	}
	return read;
}

int main(const int argc, char *argv[]) {
	if (argc <= 1) {
		return from_path("");
	}

	struct program_mode mode = {.type = guess};
	int read = 1;
	read += get_mode(argc - read, argv + read, &mode);
	if (read > argc) {
		fatal_bug(__func__, "Excess arguments read.");
	}
	if (read < argc && !strcmp("--", argv[read])) {
		++read;
	}

	const size_t remaining = (size_t)(argc - read);
	if (mode.type == guess) {
		if (!remaining || (remaining == 1 && strcmp("-", argv[read]))) {
			mode.type = directory;
		} else {
			mode.type = sole;
		}
	}

	switch (mode.type) {
	case help:
		print_help();
		return 0;
	case keys:
		print_keys();
		return 0;
	case formats:
		print_known_formats();
		return 0;
	case sole:
	case test:
	case writeout:
		return from_argv(remaining, argv + read, &mode);
	case directory:
		return from_path(remaining ? argv[read] : "");
	case archive:
		if (!remaining) {
			break;
		}
		return run_with_archive(argv[read]);
	case guess:
		fatal_bug(__func__, "Unreachable case reached. Well done.");
		return 1;
	}
	term_line_put("ERROR: Expected at least one path.", stderr);
	return 1;
}
