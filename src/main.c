#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <locale.h>
#include <errno.h>
#include <limits.h>

#include "wudefs.h"
#include "dec.h"
#include "display.h"
#include "events.h"
#include "extract.h"
#include "write_pam.h"
#include "filesystem.h"

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

static enum wu_error decode_with_stats(struct image_context *image) {
	const clock_t start = clock();
	const enum wu_error result = dec_decode_image(image);
	const double diff = clock_ellapsed(start);

	const struct image_file *infile = &image->file;
	const char *what = "Failed";
	if (result == wu_ok) {
		image_file_print(infile, 0);
		what = "Decoded";
	} else {
		term_line_key_val("Decoding error",
			wu_error_message(result), stdout);
		if (infile->errors.str) {
			fputs("Library message: ", stdout);
			wustr_print(&infile->errors, stdout);
		}
	}
	fprintf(stderr, "%s in %f seconds\n", what, diff);
	return result;
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

static enum wu_error convert_files(const struct file_list *entries,
const struct write_args *args) {
	struct image_context image = {
		.conf = conf_load(),
	};

	struct write_writer writer;
	if (!write_writer_init(&writer, &image.conf)) {
		return wu_display_error;
	}

	enum wu_error status = wu_ok;
	image.name = entries->name[0];
	if (!strcmp("-", image.name)) {
		image.name = "stdin"; // Save the user some trouble
		image.file.ifp = save_stdin();
		if (!image.file.ifp) {
			term_line_put("Failed to save stdin", stderr);
			status = wu_open_error;
		}
	}

	if (status == wu_ok) {
		status = write_image(&image, &writer, args);
	}
	if (status != wu_ok) {
		fprintf(stderr, "Failed to write %s: %s\n",
			image.name, wu_error_message(status));
	}
	write_writer_terminate(&writer);
	return status;
}

static enum wu_error test_iter(struct image_context *image, double *spent) {
	const clock_t start = clock();
	enum wu_error err;
	do {
		struct raw_img *img;
		err = dec_iter_image(image, &img);
	} while (err == wu_ok);
	*spent = clock_ellapsed(start);
	dec_free_image(image);
	if (err == wu_no_change) {
		return wu_ok;
	}
	return err;
}

static enum wu_error test_with(const struct file_list *entries,
const struct test_mode_args args) {
	printf("Testing %u times with %u extra runs for warmup.\n\n",
		args.iters, args.warmup);

	struct image_context image = {
		.conf = conf_load(),
	};

	enum wu_error result = wu_ok;
	size_t failures = 0;
	double grand_total = 0;
	for (size_t i = 0; i < entries->nr; ++i) {
		double sum = 0;
		image.name = entries->name[i];
		for (unsigned int j = 0; j < args.warmup + args.iters; ++j) {
			const bool counting = (j >= args.warmup);
			double spent;
			image_reset(&image);
			result = test_iter(&image, &spent);
			if (result != wu_ok) {
				break;
			} else if (counting) {
				sum += spent;
			}
		}

		if (result == wu_ok) {
			printf("Average: %f ", sum / args.iters);
		} else {
			printf("Error: %s ", wu_error_message(result));
			++failures;
		}
		puts(image.name);

		grand_total += sum;
	}
	putchar('\n');
	if (entries->nr * args.iters > 1) {
		printf("Total: %f\n", grand_total);
	}
	printf("%zu successful, %zu failed\n", entries->nr - failures,
		failures);
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
		return wu_display_error;
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

		image_reset(image);
		image->name = entry->name;
		image->file.ifp = entry->tmp;

		bool free_entry = false;
		result = decode_with_stats(image);
		if (result == wu_ok) {
			const bool sole_entry = iter.ra
				? false : (iter.grow.pos == 1);
			const bool ok = display_loop(&window, sole_entry);
			if (!ok || event->rm == trit_true) {
				free_entry = true;
			}
		} else {
			free_entry = true;
		}

		image->file.ifp = NULL;
		dec_free_image(image);
		if (free_entry) {
			extract_file_free(entry);
			++deleted;
			event->cycle = 1;
		}
		putchar('\n');

		idx += event->cycle;
		direction = lsign(event->cycle);
	} while (event->program != wu_program_exit
	&& (iter.ra || deleted < iter.grow.pos));

	display_end(&window, &tr);
	extract_iter_free(&iter);
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
	long direction = 1;
	do {
		while (!entries->name[idx]) {
			idx = lmod(idx + direction, (long)entries->nr);
		}
		pos_print((size_t)idx, entries);
		image_reset(image);
		image->name = entries->name[idx];

		bool free_entry = false;
		if (interpret_stdin && !strcmp("-", image->name)) {
			if (!stdin_tmp) {
				stdin_tmp = save_stdin();
			}
			if (stdin_tmp) {
				rewind(stdin_tmp);
				image->file.ifp = stdin_tmp;
			} else {
				free_entry = true;
			}
		}

		if (!free_entry) {
			result = decode_with_stats(image);
			if (result == wu_ok) {
				const bool ok = display_loop(&window, remaining == 1);
				if (!ok) {
					free_entry = true;
				} else if (event->rm == trit_true) {
					unlink(image->name);
					puts("File deleted.");
					free_entry = true;
				}
			} else {
				free_entry = true;
			}
			if (image->file.ifp == stdin_tmp) {
				image->file.ifp = NULL;
			}
			dec_free_image(image);
		}

		if (free_entry) {
			list_remove_entry(entries, idx);
			--remaining;
			event->cycle = 1;
		}
		putchar('\n');

		idx = lmod(idx + event->cycle, (long)entries->nr);
		direction = lsign(event->cycle);
	} while (event->program != wu_program_exit && remaining);

	display_end(&window, &tr);
	if (stdin_tmp) {
		fclose(stdin_tmp);
	}
	return result;
}

static enum wu_error from_argv(const size_t argc, char **argv,
const struct program_mode *mode) {
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

static enum wu_error from_path(const char *name) {
	setlocale(LC_COLLATE, "");

	size_t start_idx;
	errno = 0;
	struct file_list entries = {
		.dynamic = true,
		.name = fs_filter_sort(name, &entries.nr, &start_idx),
	};

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
#define STDIN_MODE "-"

static void print_help() {
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
		"\t\tmissing. Assumed when zero or one paths are given. Paths\n"
		"\t\tafter the first are ignored.\n"

		"\t" SOLE_MODE "\n"
		"\t\tRead only the file(s) given, in the order given. Assumed\n"
		"\t\twhen more than one path, or \"-\" (stdin), is given.\n"

		"\t" ARCHIVE_MODE "\n"
		"\t\tExtract and display images from FILE, which must be an\n"
		"\t\tarchive file supported by libarchive.\n"

		"\t" WRITE_MODE " [switches]\n"
		"\t\tDecode FILE to FILE(_sub#:frame#).pam, with each sub-image\n"
		"\t\tand animation frame on separate files. Names are written\n"
		"\t\tto stdout one per line.\n"

		"\t" TEST_MODE " [switches]\n"
		"\t\tMeasure decoding time for each FILE.\n"

		"\n"
		WRITE_MODE " switches:\n"
		"\t-f\n"
		"\t\tForce overwriting output file(s).\n"

		"\t-o OUTDIR\n"
		"\t\tWrite files onto OUTDIR instead of the file's parent.\n"

		"\t-s\n"
		"\t\tWrite only the initial sub-image to stdout.\n"

		"\t-z\n"
		"\t\tUse null as line terminator.\n"

		"\n"
		TEST_MODE " switches:\n"
		"\t-t N\n"
		"\t\tDecode each file N times. Default is 1.\n"

		"\t-w N\n"
		"\t\tBefore measuring, decode each file N times for warmup.\n"
		"\t\tDefault is 0.\n");
}

static int test_args(const int argc, char **argv, struct test_mode_args *args) {
	*args = (struct test_mode_args) {
		.iters = 1,
		.warmup = 0,
	};
	int read = 0;
	while (read < argc - 1) {
		const char *arg = argv[read];
		if (arg[0] == '-' && arg[1] && !arg[2]) {
			unsigned int *ptr;
			switch (arg[1]) {
			case 't': ptr = &args->iters; break;
			case 'w': ptr = &args->warmup; break;
			default: return read;
			}
			// %c doesn't match null bytes
			const unsigned int val = *ptr;
			char last;
			if (sscanf(argv[read+1], "%u%c", ptr, &last) == 1) {
				read += 2;
			} else {
				*ptr = val;
				break;
			}
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
			return 0;
		}

		++read;
		switch (mode->type) {
		case test:
			read += test_args(argc - read, argv + read,
				&mode->arg.test);
			break;
		case writeout:
			read += write_args(argc - read, argv + read,
				&mode->arg.write);
			break;
		default:
			break;
		}
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
		term_line_put("BUG: Excess arguments read.", stderr);
		return 1;
	}

	if (read != argc && !strcmp("--", argv[read])) {
		++read;
	}

	const size_t remaining = (size_t)(argc - read);
	if (mode.type == guess) {
		if (remaining) {
			if (remaining > 1 || !strcmp("-", argv[read])) {
				mode.type = sole;
			} else {
				mode.type = directory;
			}
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
	case test:
	case writeout:
		return from_argv(remaining, argv + read, &mode);
	case directory:
		if (!remaining) {
			return from_path("");
		}
		return from_path(argv[read]);
	case archive:
		if (!remaining) {
			break;
		}
		return run_with_archive(argv[read]);
	case guess:
		term_line_put("BUG: Unreachable case reached. Well done.", stderr);
		return 1;
	}
	term_line_put("ERROR: Expected at least one path.", stderr);
	return 1;
}
