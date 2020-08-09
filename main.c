#include <stdio.h>
#include <string.h>
#include <time.h>
#include <locale.h>
#include <errno.h>
#include <limits.h>

#include <sys/stat.h>

#include "wudefs.h"
#include "common.h"
#include "display.h"
#include "dec.h"
#include "extract.h"
#include "write_pam.h"

#include "conf.c" // Default program configuration defined here

enum work_mode {
	guess = -1,
	help = 'h',
	directory = 'd',
	sole = 's',
	archive = 'a',
	benchmark = 'b',
	writeout = 'w',
};

struct program_mode {
	enum work_mode type;
	union mode_args {
		int iters;
		struct write_args write;
	} arg;
};

static enum wu_error decode_with_stats(struct image_file *infile,
const struct wu_conf *conf, const char *filename, const bool print_meta,
long *timeinfo) {
	struct timespec before, after;
	clock_gettime(CLOCK_REALTIME, &before);
	const enum wu_error result = decode_image(infile, conf, filename);
	clock_gettime(CLOCK_REALTIME, &after);

	const long diff = timespec_nanodiff(before, after);
	if (timeinfo) {
		*timeinfo = diff;
	}

	if (result == wu_ok) {
		if (print_meta) {
			print_image_information(infile);
		}
		printf("Decoded in %ld nanoseconds.\n", diff);
	} else {
		printf("Error %u: %s.\n", result, wu_error_message(result));
		if (infile->err_msg) {
			printf("Library message: \"%s\"\n", infile->err_msg);
		}
		printf("Failed in %ld nanoseconds.\n", diff);
	}
	return result;
}

static enum wu_error test_with(const struct file_list *entries,
const struct program_mode mode) {
	struct wu_conf conf = default_config();
	conf.max_img_size = USHRT_MAX / 4;
	conf.fb = (struct display_dims) {conf.max_img_size, conf.max_img_size};

	int iters;
	if (mode.type == benchmark && mode.arg.iters > 0) {
		iters = mode.arg.iters;
	} else {
		iters = 1;
	}

	enum wu_error result = wu_ok;
	for (size_t i = 0; i < entries->nr; ++i) {
		long sum = 0;
		long spent = 0;

		const char *name = entries->name[i];
		printf("%zu/%zu, %s\n", i + 1, entries->nr, name);

		for (int j = 0; j < iters; ++j) {
			struct image_file file = {0};
			result = decode_with_stats(&file, &conf, name, false,
				&spent);
			if (mode.type == writeout && result == wu_ok) {
				write_to_file(&file, name, mode.arg.write);
			}
			free_image_file(&file);
			if (result != wu_ok) {
				break;
			}
			sum += spent;
		}

		if (iters > 1 && result == wu_ok) {
			printf("Average time: %ld nanoseconds\n", sum / iters);
		}
		putchar('\n');
	}
	return result;
}

static enum wu_error run_with_archive(const char *archive_name) {
	sort_dec_tables();

	struct archive_iter iter;
	if (!init_archive_iter(&iter, archive_name)) {
		printf("Failed to open %s\n", archive_name);
		return wu_open_error;
	}

	struct window_control control = {
		.conf = default_config(),
	};
	struct term_restore tr;
	if (!setup_display(&control, &tr)) {
		return wu_unknown_error;
	}

	enum wu_error result = wu_ok;
	int idx = 0;
	size_t deleted = 0;
	do {
		struct tmp_file *entry = get_archive_entry(&iter, idx);
		if (!entry) {
			break;
		} else if (!entry->tmp) {
			idx += control.file.cycle;
			continue;
		}
		idx = iwrap(idx, (int)iter.pos);

		printf("%d/", idx + 1);
		if (iter.ra) {
			putchar('?');
		} else {
			printf("%zu", iter.pos);
		}
		printf(", %s/%s\n", archive_name, entry->name);

		bool free_entry = false;
		struct image_file file = {.ifp = entry->tmp};
		result = decode_with_stats(&file, &control.conf, entry->name,
			true, NULL);
		if (result == wu_ok) {
			const bool ok = display_loop(&control, &file,
				entry->name, false);
			if (!ok || control.event.rm == yes_rm) {
				free_entry = true;
			}
		} else {
			free_entry = true;
		}
		putchar('\n');
		if (free_entry) {
			remove_archive_entry(entry);
			++deleted;
		}
		file.ifp = NULL;
		free_image_file(&file);

		idx += control.file.cycle;
		if (control.file.cycle < 0) {
			control.file.cycle = -1;
		} else {
			control.file.cycle = 1;
		}
	} while (control.event.program != close_window && deleted < iter.pos);
	free_archive_iter(&iter);
	end_display(&control, &tr);
	return result;
}

static enum wu_error run_with_list(struct file_list *entries, int pos) {
	struct window_control control = {
		.conf = default_config(),
	};
	struct term_restore tr;
	if (!setup_display(&control, &tr)) {
		return wu_unknown_error;
	}

	enum wu_error result = wu_ok;
	size_t remaining = entries->nr;
	do {
		const char *name = entries->name[pos];
		if (!name) {
			pos = iwrap(pos + control.file.cycle, (int)entries->nr);
			continue;
		}

		printf("%d/%zu, %s\n", pos + 1, entries->nr, name);

		bool free_entry = false;
		struct image_file file = {0};
		result = decode_with_stats(&file, &control.conf, name, true,
			NULL);
		if (result == wu_ok) {
			const bool ok = display_loop(&control, &file, name,
				entries->nr == 1);
			if (!ok) {
				free_entry = true;
			} else if (control.event.rm == yes_rm) {
				remove(name);
				puts("File deleted.");
				free_entry = true;
			}
		} else {
			free_entry = true;
		}
		if (free_entry) {
			free(entries->name[pos]);
			entries->name[pos] = NULL;
			--remaining;
			if (!control.file.cycle) {
				control.file.cycle = 1;
			}
		}
		putchar('\n');

		free_image_file(&file);
		pos = iwrap(pos + control.file.cycle, (int)entries->nr);
		// Preserve direction in case of errors
		if (control.file.cycle < 0) {
			control.file.cycle = -1;
		} else {
			control.file.cycle = 1;
		}
	} while (control.event.program != close_window && remaining);

	end_display(&control, &tr);
	return result;
}

static enum wu_error from_list(const size_t argc, const char *argv[],
const struct program_mode mode) {
	errno = 0;
	struct file_list *entries = flex_malloc(sizeof(*entries), argc,
		sizeof(*entries->name));
	if (!entries) {
		perror("Error allocating list");
		return wu_alloc_error;
	}

	enum wu_error result = wu_ok;
	for (size_t i = 0; i < argc; ++i) {
		errno = 0;
		entries->name[i] = strdup(argv[i]);
		if (!entries->name[i]) {
			result = wu_alloc_error;
			break;
		}
	}

	if (result == wu_ok) {
		entries->nr = argc;
		sort_dec_tables();
		if (mode.type == writeout || mode.type == benchmark) {
			result = test_with(entries, mode);
		} else {
			result = run_with_list(entries, 0);
		}
	} else {
		perror("Error while building list");
	}
	free_file_list(entries);
	return result;
}

static char * get_path_components(const char *path, const char **filename) {
	if (!path[0]) {
		return strdup("");
	} else if (!strcmp(".", path) || !strcmp("./", path)) {
		return strdup("./");
	} else {
		struct stat statbuf;
		if (stat(path, &statbuf) == -1) {
			return NULL;
		}

		if (S_ISREG(statbuf.st_mode)) {
			const char *slash = strrchr(path, '/');
			if (slash) {
				*filename = slash + 1;
				const size_t dir_len = (size_t)(slash - path + 1);
				return strndup(path, dir_len);
			} else {
				*filename = path;
				return strdup("");
			}
		} else if (S_ISDIR(statbuf.st_mode)) {
			const size_t pathlen = strlen(path);
			if (path[pathlen - 1] == '/') {
				return strdup(path);
			} else {
				char *dirname = malloc(pathlen + 1);
				if (dirname) {
					memcpy(dirname, path, pathlen);
					memcpy(dirname + pathlen, "/", 2);
				}
				return dirname;
			}
		} else {
			return NULL;
		}
	}
}

static int sort_strcoll(const void *s1, const void *s2) {
	const char * const *n1 = s1;
	const char * const *n2 = s2;
	return strcoll(*n1, *n2);
}

static enum wu_error from_path(const char *path) {
	const char *first_name = NULL;
	errno = 0;
	char *dirname = get_path_components(path, &first_name);
	if (!dirname) {
		if (errno) {
			fprintf(stderr, "Error while parsing path (%s): %s\n",
				path, strerror(errno));
		} else {
			fprintf(stderr, "ERROR: %s is not a regular file or "
				"directory.\n", path);
		}
		return wu_open_error;
	}

	sort_dec_tables();
	errno = 0;
	enum wu_error result;
	struct file_list *entries = filter_directory(dirname, first_name);
	if (entries) {
		setlocale(LC_COLLATE, "");
		qsort(entries->name, entries->nr, sizeof(*entries->name),
			sort_strcoll);

		int starting_pos = 0;
		if (first_name) {
			char **loc = bsearch(&path, entries->name, entries->nr,
				sizeof(*entries->name), sort_strcoll);
			if (loc) {
				starting_pos = (int)(loc - entries->name);
			}
		}

		result = run_with_list(entries, starting_pos);
		free_file_list(entries);
	} else {
		if (errno) {
			perror("Error while filtering images");
		} else {
			fprintf(stderr, "ERROR: No images were found at %s\n",
				dirname[0] ? dirname : ".");
		}
		result = wu_open_error;
	}
	free(dirname);
	return result;
}

#define HELP_MODE "help"
#define DIRECTORY_MODE "directory"
#define SOLE_MODE "sole"
#define ARCHIVE_MODE "archive"
#define WRITEOUT_MODE "writeout"
#define BENCHMARK_MODE "benchmark"

static int print_help(const char *prog) {
	fprintf(stderr, "Usage:\n"
		"\t%1$s\t(read from \".\")\n"
		"\t%1$s DIR\t(read from DIR)\n"
		"\t%1$s FILE\t(read the directory of FILE, starting with FILE)\n"
		"\t%1$s FILE FILE [FILE ...]\t(read only FILEs)\n"
		"\t%1$s MODE [MODE_ARG] [OPTS] [--] PATH [...]\t(explicit mode)\n"
		"\n"

		"Work mode (all exclusive, may be abbreviated):\n"
		"\t" HELP_MODE " | -h | --help\n"
		"\t\tYou are here.\n"

		"\t" DIRECTORY_MODE "\n"
		"\t\tRead from every directory and from the parent of every\n"
		"\t\tfile given, or \".\" when none. Assumed when zero or\n"
		"\t\tone paths are given.\n"

		"\t" SOLE_MODE "\n"
		"\t\tRead only the file(s) given, in the order given.\n"
		"\t\tAssumed when more than one than one path is given.\n"

		"\t" WRITEOUT_MODE "\n"
		"\t\tDecode FILE to FILE(_id).pam. Assumed when the '-o'\n"
		"\t\tswitch is given.\n"

		"\t" BENCHMARK_MODE " [n]\n"
		"\t\tBenchmark decoding time for each FILE n times, or 1 if\n"
		"\t\tunspecified.\n"
		"\n"

		"Switches:\n"
		"\t-o BASENAME\n"
		"\t\tWriteout mode only. Use BASENAME for output files\n"
		"\t\tinstead of deriving it from the input.\n"

		"\t-f\n"
		"\t\tWriteout mode only. Forces overwriting.\n"

		"\t-r\n"
		"\t\tWriteout mode only. Causes the raster data to be written\n"
		"\t\t\"raw\", and the PAM header to be tweaked just enough to\n"
		"\t\tmake it inspectable by eye. It is what's sent to the\n"
		"\t\tcard sans alignment.\n"

		"\t-p\n"
		"\t\tWriteout mode only. For paletted images, also write the\n"
		"\t\tpalette to FILE_palette.pam.\n"

		, prog);
	return 0;
}

struct program_mode get_mode(int *idx, const int argc, const char *argv[]) {
	struct program_mode mode = {.type = guess};
	const char *arg = argv[*idx];
	const size_t arglen = strlen(arg);

	if (*idx < argc && arg[0]) {
		if (!strncmp(arg, DIRECTORY_MODE, arglen)
		|| !strncmp(arg, SOLE_MODE, arglen)
		|| !strncmp(arg, ARCHIVE_MODE, arglen)
		|| !strncmp(arg, WRITEOUT_MODE, arglen)
		|| !strncmp(arg, BENCHMARK_MODE, arglen)) {
			mode.type = arg[0];
		} else if (!strncmp(arg, HELP_MODE, arglen)
		|| !strcmp(arg, "-h") || !strcmp(arg, "--help")) {
			mode.type = help;
			return mode;
		} else {
			return mode;
		}

		++(*idx);
		if (*idx < argc) {
			arg = argv[*idx];
			switch (mode.type) {
			case benchmark:
				if (sscanf(arg, "%d", &mode.arg.iters) == 1) {
					++(*idx);
				} else {
					mode.arg.iters = 1;
				}
				break;
			case writeout:
				if ((arg[0] == '0' || arg[0] == '1')
				&& !arg[1]) {
					++(*idx);
					mode.arg.write.expand = arg[0] == '1';
				} else {
					mode.arg.write.expand = true;
				}
				break;
			default:
				break;
			}
		}
	}
	return mode;
}

int main(const int argc, const char *argv[]) {
	if (argc <= 1) {
		return (int)from_path("");
	}

	int idx = 1;
	struct program_mode mode = get_mode(&idx, argc, argv);
	if (idx < argc) {
		if (!strcmp("--", argv[idx])) {
			++idx;
		}
	}
	if (idx >= argc) {
		fputs("???: I just don't know what went wrong.\n", stderr);
		return 1;
	}

	const size_t remaining = (size_t)(argc - idx);
	if (mode.type == guess) {
		if (remaining > 1) {
			mode.type = sole;
		} else {
			mode.type = directory;
		}
	}

	switch (mode.type) {
	case help:
		return print_help(argv[0]);
	case sole:
	case benchmark:
	case writeout:
		if (remaining) {
			return (int)from_list(remaining, argv + idx, mode);
		}
		fputs("ERROR: Expected at least one path.\n", stderr);
		return 1;
	case directory:
		if (!remaining) {
			return (int)from_path("");
		} else if (argv[idx][0]) {
			return (int)from_path(argv[idx]);
		}
		fputs("ERROR: Null argument.\n", stderr);
		return 1;
	case archive:
		return (int)run_with_archive(argv[idx]);
	case guess:
		break;
	}
	fputs("???: Unreachable case reached.\n", stderr);
	return 1;
}
