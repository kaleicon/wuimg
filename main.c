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
#include "events.h"
#include "dec.h"
#include "extract.h"
#include "write_pam.h"
#include "conf.h"

enum work_mode {
	guess = -1,
	help = 'h',
	keys = 'k',
	formats = 'f',
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

struct file_list {
	bool dynamic;
	size_t nr;
	char **name;
};

static int idirection(const int i) {
	return i > 0 ? 1 : -1;
}

static enum wu_error decode_with_stats(struct image_file *infile,
const struct wu_conf *conf, const char *filename, const bool print_meta,
long *timeinfo) {
	struct timespec start;
	clock_start(&start);
	const enum wu_error result = decode_image(infile, conf, filename);
	const long diff = clock_nanodiff(&start);
	if (timeinfo) {
		*timeinfo = diff;
	}

	if (result == wu_ok) {
		if (print_meta) {
			clock_start(&start);
			tree_print(&infile->metadata, 80, 24);
			printf("printed in %ld nanoseconds\n",
				clock_nanodiff(&start));
			print_image_information(infile);
		}
		printf("Decoded in %ld nanoseconds\n", diff);
	} else {
		printf("Error %d: %s.\n", result, wu_error_message(result));
		if (infile->err_msg) {
			printf("Library message: \"%s\"\n", infile->err_msg);
		}
		printf("Failed in %ld nanoseconds\n", diff);
	}
	return result;
}

static enum wu_error test_with(const struct file_list *entries,
const struct program_mode *mode) {
	struct wu_conf conf = load_config();
	conf.max_img_size = USHRT_MAX;
	conf.fb = (struct display_dims) {conf.max_img_size, conf.max_img_size};

	int iters;
	if (mode->type == benchmark && mode->arg.iters > 0) {
		iters = mode->arg.iters;
	} else {
		iters = 1;
	}

	enum wu_error result = wu_ok;
	long grand_total = 0;
	for (size_t i = 0; i < entries->nr; ++i) {
		long sum = 0;

		const char *name = entries->name[i];
		printf("%zu/%zu, %s\n", i + 1, entries->nr, name);

		for (int j = 0; j < iters; ++j) {
			struct image_file file = {0};
			long spent = 0;
			result = decode_with_stats(&file, &conf, name, false,
				&spent);
			if (mode->type == writeout && result == wu_ok) {
				write_to_file(&file, name, &mode->arg.write);
			}
			free_image_file(&file);
			if (result != wu_ok) {
				break;
			}
			sum += spent;
		}

		if (iters > 1 && result == wu_ok) {
			printf("Average time: %ld nanoseconds\n\n", sum / iters);
		}
		grand_total += sum;
	}
	printf("Grand total: %ld nanoseconds\n", grand_total);
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
		.conf = load_config(),
	};
	struct term_restore tr;
	if (!setup_display(&control, &tr)) {
		return wu_unknown_error;
	}

	enum wu_error result = wu_ok;
	size_t deleted = 0;
	int idx = 0;
	int direction = 1;
	do {
		struct tmp_file *entry = get_archive_file(&iter, idx);
		if (!entry) {
			break;
		} else if (!entry->tmp) {
			idx += direction;
			continue;
		}
		idx = imod(idx, (int)iter.pos);

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
			const bool sole_entry = iter.ra ? false
				: (iter.pos == 1);
			const bool ok = display_loop(&file, &control,
				entry->name, sole_entry);
			if (!ok || control.event.rm == yes_rm) {
				free_entry = true;
			}
		} else {
			free_entry = true;
		}

		file.ifp = NULL;
		free_image_file(&file);
		if (free_entry) {
			remove_archive_entry(entry);
			++deleted;
			control.event.file.cycle = direction;
		}
		putchar('\n');

		idx += control.event.file.cycle;
		direction = idirection(control.event.file.cycle);
	} while (control.event.program != close_window
		&& (iter.ra || deleted < iter.pos));

	free_archive_iter(&iter);
	end_display(&tr);
	return result;
}

static void free_file_list_entry(struct file_list *entries, int pos) {
	if (entries->dynamic) {
		free(entries->name[pos]);
	}
	entries->name[pos] = NULL;
}

static void free_file_list(struct file_list *entries) {
	for (size_t i = 0; i < entries->nr; ++i) {
		free(entries->name[i]);
	}
	free(entries->name);
}

static enum wu_error run_with_list(struct file_list *entries, int idx) {
	struct window_control control = {
		.conf = load_config(),
	};
	struct term_restore tr;
	if (!setup_display(&control, &tr)) {
		return wu_unknown_error;
	}

	enum wu_error result = wu_ok;
	size_t remaining = entries->nr;
	int direction = 1;
	do {
		const char *name = entries->name[idx];
		if (!name) {
			idx = imod(idx + direction, (int)entries->nr);
			continue;
		}

		printf("%d/%zu, %s\n", idx + 1, entries->nr, name);

		bool free_entry = false;
		struct image_file file = {0};
		result = decode_with_stats(&file, &control.conf, name, true,
			NULL);
		if (result == wu_ok) {
			const bool ok = display_loop(&file, &control, name,
				remaining == 1);
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

		free_image_file(&file);
		if (free_entry) {
			free_file_list_entry(entries, idx);
			--remaining;
			control.event.file.cycle = direction;
		}
		putchar('\n');

		idx = imod(idx + control.event.file.cycle, (int)entries->nr);
		direction = idirection(control.event.file.cycle);
	} while (control.event.program != close_window && remaining);

	end_display(&tr);
	return result;
}

static enum wu_error from_argv(const size_t argc, char **argv,
const struct program_mode *mode) {
	struct file_list entries = {
		.dynamic = false,
		.nr = argc,
		.name = argv,
	};

	sort_dec_tables();
	if (mode->type == writeout || mode->type == benchmark) {
		return test_with(&entries, mode);
	}
	return run_with_list(&entries, 0);
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
	struct file_list entries = {
		.dynamic = true,
		.name = filter_directory(dirname, first_name, &entries.nr),
	};
	if (entries.name) {
		setlocale(LC_COLLATE, "");
		qsort(entries.name, entries.nr, sizeof(*entries.name),
			sort_strcoll);

		int starting_pos = 0;
		if (first_name) {
			char **loc = bsearch(&path, entries.name, entries.nr,
				sizeof(*entries.name), sort_strcoll);
			if (loc) {
				starting_pos = (int)(loc - entries.name);
			}
		}

		result = run_with_list(&entries, starting_pos);
		free_file_list(&entries);
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

#define KEYS_MODE "keys"
#define HELP_MODE "help"
#define FMTS_MODE "formats"
#define DIRECTORY_MODE "directory"
#define SOLE_MODE "sole"
#define ARCHIVE_MODE "archive"
#define WRITE_MODE "write"
#define BENCHMARK_MODE "benchmark"

static void print_help() {
	fputs("Usage:\n"
		"\t" WU_CANON_NAME "\t(read from \".\")\n"
		"\t" WU_CANON_NAME " DIR\t(read from DIR)\n"
		"\t" WU_CANON_NAME " FILE\t(read the parent of FILE, starting with FILE)\n"
		"\t" WU_CANON_NAME " FILE FILE [FILE ...]\t(read only FILEs)\n"
		"\t" WU_CANON_NAME " MODE [OPTS] [--] PATH [...]\t(explicit mode)\n"
		"\n"

		"Work mode (all exclusive, may be abbreviated):\n"
		"\t-h | --" HELP_MODE "\n"
		"\t\tYou are here.\n"

		"\t-k | --" KEYS_MODE "\n"
		"\t\tPrint keybinds.\n"

		"\t-k | --" FMTS_MODE "\n"
		"\t\tPrint supported formats.\n"

		"\t" DIRECTORY_MODE "\n"
		"\t\tRead from every directory and from the parent of every\n"
		"\t\tfile given, or \".\" when none. Assumed when zero or\n"
		"\t\tone paths are given.\n"

		"\t" SOLE_MODE "\n"
		"\t\tRead only the file(s) given, in the order given.\n"
		"\t\tAssumed when more than one path is given.\n"

		"\t" WRITE_MODE " [switches]\n"
		"\t\tDecode FILE to FILE(_id).pam. See below for switches.\n"

		"\t" BENCHMARK_MODE " [n]\n"
		"\t\tBenchmark decoding time for each FILE n times, or 1 if\n"
		"\t\tunspecified.\n"
		"\n"

		"Write switches:\n"
		"\t-o BASENAME\n"
		"\t\tUse BASENAME for output instead of the input name.\n"

		"\t-f\n"
		"\t\tForce overwriting.\n"

		"\t-r\n"
		"\t\tCauses the raster data to be written \"raw\", and the\n"
		"\t\tPAM header to be tweaked just enough to make it\n"
		"\t\tinspectable by eye. It is what's sent to the card sans\n"
		"\t\talignment.\n",
		stderr);
}

static int get_mode(const int argc, char **argv, struct program_mode *mode) {
	mode->type = guess;

	int idx = 0;
	const char *arg = argv[idx];
	const size_t arglen = strlen(arg);
	if (idx < argc) {
		const bool match = !strncmp(arg, ARCHIVE_MODE, arglen)
			|| !strncmp(arg, WRITE_MODE, arglen)
			|| !strncmp(arg, BENCHMARK_MODE, arglen)
			|| !strncmp(arg, SOLE_MODE, arglen)
			|| !strncmp(arg, DIRECTORY_MODE, arglen);

		if (match) {
			mode->type = arg[0];
		} else {
			if (!strncmp(arg, HELP_MODE, arglen)
			|| !strcmp(arg, "-h") || !strcmp(arg, "--" HELP_MODE)) {
				mode->type = help;
			} else if (!strncmp(arg, KEYS_MODE, arglen)
			|| !strcmp(arg, "-k") || !strcmp(arg, "--" KEYS_MODE)) {
				mode->type = keys;
			} else if (!strncmp(arg, FMTS_MODE, arglen)
			|| !strcmp(arg, "-f") || !strcmp(arg, "--" FMTS_MODE)) {
				mode->type = formats;
			}
			return idx;
		}

		++idx;
		if (idx < argc) {
			arg = argv[idx];
			switch (mode->type) {
			case benchmark:
				/* sscanf will match a filename starting with
				 * a number, so we'll ask for an extra
				 * character which won't be matched if it is
				 * null. */
				;char last;
				const int matched = sscanf(arg, "%d%c",
					&mode->arg.iters, &last);
				if (matched == 1) {
					++idx;
				} else {
					mode->arg.iters = 1;
				}
				break;
			case writeout:
				idx += read_write_args(argc - idx, argv + idx,
					&mode->arg.write);
				break;
			default:
				break;
			}
		}
	}
	return idx;
}

int main(const int argc, char *argv[]) {
	if (argc <= 1) {
		return from_path("");
	}

	int idx = 1;
	struct program_mode mode = {0};
	idx += get_mode(argc - idx, argv + idx, &mode);
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
		print_help();
		return 0;
	case keys:
		print_keys();
		return 0;
	case formats:
		print_known_formats();
		return 0;
	case sole:
	case benchmark:
	case writeout:
		if (remaining) {
			return from_argv(remaining, argv + idx, &mode);
		}
		fputs("ERROR: Expected at least one path.\n", stderr);
		return 1;
	case directory:
		if (!remaining) {
			return from_path("");
		} else if (argv[idx][0]) {
			return from_path(argv[idx]);
		}
		fputs("ERROR: Null argument.\n", stderr);
		return 1;
	case archive:
		return run_with_archive(argv[idx]);
	case guess:
		break;
	}
	fputs("???: Unreachable case reached. Well done.\n", stderr);
	return 1;
}
