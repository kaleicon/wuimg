#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <locale.h>
#include <errno.h>
#include <limits.h>

#include "wudefs.h"
#include "common.h"
#include "dec.h"
#include "events.h"
#include "display.h"
#include "term.h"
#include "extract.h"
#include "write_pam.h"
#include "conf.h"
#include "filesystem.h"

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
		unsigned int iters;
		struct write_args write;
	} arg;
};

struct image_list {
	bool dynamic;
	size_t nr;
	char **name;
};

static void image_list_remove_entry(struct image_list *entries, long pos) {
	if (entries->dynamic) {
		free(entries->name[pos]);
	}
	entries->name[pos] = NULL;
}

static void image_list_free(struct image_list *entries) {
	for (size_t i = 0; i < entries->nr; ++i) {
		free(entries->name[i]);
	}
	free(entries->name);
}

static int lsign(const long i) {
	return i < 0 ? -1 : 1;
}

static enum wu_error decode_with_stats(struct image_context *image,
const bool print_meta, const bool print_time, double *timeinfo) {
	const clock_t start = clock();
	const enum wu_error result = decode_image(image);
	const double diff = clock_ellapsed(start);
	if (timeinfo) {
		*timeinfo = diff;
	}

	const struct image_file *infile = &image->file;
	if (result == wu_ok) {
		if (print_meta) {
			image_file_print(infile, 0);
		}
		if (print_time) {
			printf("Decoded in %f seconds\n", diff);
		}
	} else {
		printf("Decoding error: %s\n", wu_error_message(result));
		if (infile->errors.str) {
			printf("Library message: %s\n", infile->errors.str);
		}
		printf("Failed in %f seconds\n", diff);
	}
	return result;
}

static enum wu_error test_with(const struct image_list *entries,
const struct program_mode *mode) {
	struct image_context image;
	image.conf = conf_load();

	unsigned int iters = 1;
	unsigned int warmup = 0;
	if (mode->type == benchmark) {
		iters = mode->arg.iters;
		warmup = 3;
		printf("Benchmarking %u times with %u tries for warmup.\n\n",
			iters, warmup);
	}

	enum wu_error result = wu_ok;
	double grand_total = 0;
	for (size_t i = 0; i < entries->nr; ++i) {
		double sum = 0;

		image.name = entries->name[i];
		printf("%zu/%zu, %s\n", i + 1, entries->nr, image.name);

		for (unsigned int j = 0; j < iters + warmup; ++j) {
			const bool counting = j >= warmup;
			double spent = 0;

			image.file = (struct image_file){0};
			result = decode_with_stats(&image, false, counting,
				&spent);
			if (mode->type == writeout && result == wu_ok) {
				write_to_file(&image.file, image.name,
					&mode->arg.write);
			}
			image_file_free(&image.file);
			if (result != wu_ok) {
				break;
			}
			if (counting) {
				sum += spent;
			}
		}

		if (iters > 1 && result == wu_ok) {
			printf("Average time: %f seconds\n\n", sum / iters);
		}
		grand_total += sum;
	}
	if (entries->nr * iters > 1) {
		printf("Grand total: %f seconds\n", grand_total);
	}
	return result;
}

static enum wu_error run_with_archive(const char *archive_name) {
	struct extract_iter iter;
	if (!extract_iter_init(&iter, archive_name)) {
		fprintf(stderr, "Failed to open %s\n", archive_name);
		return wu_open_error;
	}

	struct window_context window = {
		.pub.image.conf = conf_load(),
	};

	struct term_restore tr;
	if (!display_setup(&window, &tr)) {
		return wu_unknown_error;
	}

	struct image_context *image = &window.pub.image;
	struct wu_event *event = &window.pub.event;
	enum wu_error result = wu_ok;
	size_t deleted = 0;
	long idx = 0;
	long direction = 1;
	do {
		struct extract_file *entry = extract_file_get(&iter, idx);
		if (!entry) {
			break;
		} else if (!entry->tmp) {
			idx += direction;
			continue;
		}
		idx = lmod(idx, (long)iter.grow.pos);

		printf("%ld/", idx + 1);
		if (iter.ra) {
			putchar('?');
		} else {
			printf("%zu", iter.grow.pos);
		}
		printf(", %s/%s\n", archive_name, entry->name);

		image->name = entry->name;
		image->file = (struct image_file){.ifp = entry->tmp};
		bool free_entry = false;
		result = decode_with_stats(image, true, true, NULL);
		if (result == wu_ok) {
			const bool sole_entry = iter.ra ? false : (iter.grow.pos == 1);
			const bool ok = display_loop(&window, sole_entry);
			if (!ok || event->rm == yes_rm) {
				free_entry = true;
			}
		} else {
			free_entry = true;
		}

		image->file.ifp = NULL;
		image_file_free(&image->file);
		if (free_entry) {
			extract_file_free(entry);
			++deleted;
			event->cycle = 1;
		}
		putchar('\n');

		idx += event->cycle;
		direction = lsign(event->cycle);
	} while (event->program != close_window
	&& (iter.ra || deleted < iter.grow.pos));

	display_end(&window, &tr);
	extract_iter_free(&iter);
	return result;
}

static enum wu_error run_with_list(struct image_list *entries, long idx) {
	struct window_context window = {
		.pub.image.conf = conf_load(),
	};

	struct term_restore tr;
	if (!display_setup(&window, &tr)) {
		return wu_unknown_error;
	}

	struct image_context *image = &window.pub.image;
	struct wu_event *event = &window.pub.event;
	enum wu_error result = wu_ok;
	size_t remaining = entries->nr;
	long direction = 1;
	do {
		while (!entries->name[idx]) {
			idx = lmod(idx + direction, (long)entries->nr);
		}
		image->name = entries->name[idx];
		image->file = (struct image_file){0};
		printf("%ld/%zu, %s\n", idx + 1, entries->nr, image->name);

		bool free_entry = false;
		result = decode_with_stats(image, true, true, NULL);
		if (result == wu_ok) {
			const bool ok = display_loop(&window, remaining == 1);
			if (!ok) {
				free_entry = true;
			} else if (event->rm == yes_rm) {
				unlink(image->name);
				puts("File deleted.");
				free_entry = true;
			}
		} else {
			free_entry = true;
		}

		image_file_free(&image->file);
		if (free_entry) {
			image_list_remove_entry(entries, idx);
			--remaining;
			event->cycle = 1;
		}
		putchar('\n');

		idx = lmod(idx + event->cycle, (long)entries->nr);
		direction = lsign(event->cycle);
	} while (event->program != close_window && remaining);

	display_end(&window, &tr);
	return result;
}

static enum wu_error from_argv(const size_t argc, char **argv,
const struct program_mode *mode) {
	struct image_list entries = {
		.dynamic = false,
		.nr = argc,
		.name = argv,
	};

	if (mode->type == writeout || mode->type == benchmark) {
		return test_with(&entries, mode);
	}
	return run_with_list(&entries, 0);
}

static enum wu_error from_path(const char *name) {
	const clock_t start = clock();
	setlocale(LC_COLLATE, "");

	size_t start_idx;
	errno = 0;
	struct image_list entries = {
		.dynamic = true,
		.name = fs_filter_sort(name, &entries.nr, &start_idx),
	};

	enum wu_error result;
	if (entries.name) {
		printf("dir processed in %f\n", clock_ellapsed(start));
		result = run_with_list(&entries, (long)start_idx);
		image_list_free(&entries);
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
#define RECURSIVE_MODE "recursive"
#define ARCHIVE_MODE "archive"
#define WRITE_MODE "write"
#define BENCHMARK_MODE "benchmark"

static void print_help() {
	puts("Usage:\n"
		"\t" WU_CANON_NAME "\t(read images from \".\")\n"
		"\t" WU_CANON_NAME " DIR\t(read from DIR)\n"
		"\t" WU_CANON_NAME " FILE\t(read from the parent of FILE, starting with FILE)\n"
		"\t" WU_CANON_NAME " FILE FILE...\t(read only FILEs)\n"
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

		"Work mode (all exclusive, may be abbreviated):\n"
		"\t" DIRECTORY_MODE "\n"
		"\t\tDisplay images from PATH if it is a directory, from its\n"
		"\t\tparent if it is a file, or from the current directory if\n"
		"\t\tmissing. Assumed when zero or one paths are given. Paths\n"
		"\t\tafter the first are ignored.\n"

		"\t" SOLE_MODE "\n"
		"\t\tRead only the file(s) given, in the order given.\n"
		"\t\tAssumed when more than one path is given.\n"

		"\t" WRITE_MODE " [switches]\n"
		"\t\tDecode FILE to FILE(_id).pam. See below for switches.\n"

		"\t" BENCHMARK_MODE " [n]\n"
		"\t\tBenchmark decoding time for each FILE n times, or 1 if\n"
		"\t\tunspecified.\n"

		"\t" ARCHIVE_MODE "\n"
		"\t\tExtract and display images from FILE, which must be an\n"
		"\t\tarchive file supported by libarchive.\n"
		"\n"

		"Write switches:\n"
		"\t-f\n"
		"\t\tOverwrite output file(s).\n"

		"\t-i N\n"
		"\t\tWrite only subimage N. By default, all subimages are\n"
		"\t\twritten, or only the first one (0) if writing to stdout.\n"

		"\t-o BASENAME\n"
		"\t\tUse BASENAME for output(s) instead of the input name.\n"
		"\t\tIf \"-\", write the Nth subimage (see -i) to stdout.\n"

		"\t-r\n"
		"\t\tSkip the conversion to 8/16 bits; write the data \"raw\"\n"
		"\t\tinstead and tweak the PAM header to make it eyeable.\n"
		"\t\tIntended as a curiosity, really. It is what's sent to\n"
		"\t\tthe card sans alignment.");
}

static int get_mode(const int argc, char **argv, struct program_mode *mode) {
	int idx = 0;
	const char *arg = argv[idx];
	const size_t arglen = strlen(arg);
	if (idx < argc) {
		const bool mode_match = !strncmp(arg, ARCHIVE_MODE, arglen)
			|| !strncmp(arg, WRITE_MODE, arglen)
			|| !strncmp(arg, BENCHMARK_MODE, arglen)
			|| !strncmp(arg, SOLE_MODE, arglen)
			|| !strncmp(arg, DIRECTORY_MODE, arglen);

		if (mode_match) {
			mode->type = arg[0];
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
				const int matched = sscanf(arg, "%u%c",
					&mode->arg.iters, &last);
				if (matched == 1) {
					++idx;
				} else {
					mode->arg.iters = 1;
				}
				break;
			case writeout:
				idx += write_args(argc - idx, argv + idx,
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
	struct program_mode mode = {.type = guess};
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
		if (remaining) {
			return from_path(argv[idx]);
		}
		return from_path("");
	case archive:
		return run_with_archive(argv[idx]);
	case guess:
		break;
	}

	fputs("???: Unreachable case reached. Well done.\n", stderr);
	return wu_unknown_error;
}
