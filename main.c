#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <time.h>
#include <unistd.h>
#include <locale.h>
#include <errno.h>
#include <limits.h>

#include <sys/stat.h>

#include <epoxy/gl.h>

#include "wudefs.h"
#include "common.h"
#include "opengl.h"
#include "window.h"
#include "dec.h"

#include "conf.c" // Program configuration defined here

struct dir_images {
	char **names;
	size_t nr;
	size_t remaining;
};

union mode_args {
	unsigned int iters;
	enum write_format {
		raw,
		pam,
	} format;
};

static void write_raw(const struct image_file *infile, const char *filename,
enum write_format format) {
	(void)format;

	const char ext[] = ".pam";
	const size_t extlen = sizeof(ext);
	const size_t namelen = (size_t)(strrchr(filename, '.') - filename);
	char *outname = malloc(namelen + extlen);

	memcpy(outname, filename, namelen);
	memcpy(outname + namelen, ext, extlen);

	const struct raw_img *img = infile->sub_img;
	FILE *ofp = fopen(outname, "wb");
	if (ofp) {
		const char *tuples[] = {"GRAYSCALE", "GRAYSCALE_ALPHA",
			"RGB", "RGB_ALPHA"};

		size_t i = 0;
		const unsigned char depth = img[i].palette ? 1 : img[i].channels;
		fprintf(ofp,
			"P7\n"
			"WIDTH %zu\n"
			"HEIGHT %zu\n"
			"DEPTH %hhu\n"
			"MAXVAL %u\n"
			"TUPLTYPE %s\n"
			"ENDHDR\n",
			img[i].w, img[i].h, depth,
			(1U << img[i].bitdepth) - 1,
			tuples[depth - 1]);
		fwrite(img[i].data, depth, img[i].w * img[i].h, ofp);
		fclose(ofp);
	}
	free(outname);
}

static void free_dir_images(const struct dir_images *entries) {
	for (size_t i = 0; i < entries->nr; ++i) {
		free(entries->names[i]);
	}
	free(entries->names);
}

static void free_entry(struct dir_images *entries, const int pos) {
	free(entries->names[pos]);
	entries->names[pos] = NULL;
	--entries->remaining;
}

static enum wu_error decode_with_stats(struct image_file *infile,
const struct wu_conf *conf, const char *filename, const bool print_meta) {
	struct timespec before, after;
	clock_gettime(CLOCK_REALTIME, &before);
	const enum wu_error result = decode_image(infile, conf, filename);
	clock_gettime(CLOCK_REALTIME, &after);

	if (result == wu_ok) {
		printf("Decoded in %ld nanoseconds.\n",
			timespec_nanodiff(&before, &after));
		if (print_meta) {
			print_image_information(infile);
		}
	} else {
		printf("Failed in %ld nanoseconds.\nError %d: %s.\n",
			timespec_nanodiff(&before, &after), result,
			wu_error_message(result));
		if (infile->err_msg) {
			printf("Library message: \"%s\"\n", infile->err_msg);
		}
	}
	return result;
}

static enum wu_error test_with(const struct dir_images *entries,
const char mode, const union mode_args arg) {
	struct wu_conf conf;
	set_user_conf(&conf);
	conf.max_img_size = USHRT_MAX / 4;

	struct image_file file;
	memset(&file, 0, sizeof(file));

	enum wu_error result = wu_ok;
	for (size_t i = 0; i < entries->nr; ++i) {
		const char *name = entries->names[i];
		printf("%zu/%zu, %s\n", i + 1, entries->nr, name);
		if (mode == 'w') {
			result = decode_with_stats(&file, &conf, name, false);
			if (result == wu_ok) {
				write_raw(&file, name, arg.format);
			}
			free_image_file(&file);
		} else if (mode == 'b') {
			for (unsigned int j = 0; j < arg.iters; ++j) {
				result = decode_with_stats(&file, &conf, name, false);
				free_image_file(&file);
				if (result != wu_ok) {
					break;
				}
			}
			putchar('\n');
		}
	}
	return result;
}

static int run_with(struct dir_images *entries, int pos) {
	struct window_control control;
	set_user_conf(&control.conf);
	if (!setup_display(&control)) {
		return 1;
	}

	entries->remaining = entries->nr;
	enum wu_error result = wu_ok;
	struct image_file file;
	memset(&file, 0, sizeof(file));
	while (control.event.program != close_window && entries->remaining) {
		const char *name = entries->names[pos];
		if (!name) {
			pos = iwrapadd(pos, control.state.cycle,
				(int)entries->nr);
			continue;
		}

		printf("%d/%zu, %s\n", pos + 1, entries->nr, name);
		result = decode_with_stats(&file, &control.conf, name, true);
		if (result == wu_ok) {
			memset(&control.event, 0, sizeof(control.event));
			control.state.cycle = 0;
			const bool ok = window_loop(&control, &file, name,
				entries->nr == 1);

			printf(CLEAR_LINE);
			if (!ok) {
				free_entry(entries, pos);
				control.state.cycle = 1;
			} else if (control.event.rm == yes_rm) {
				unlink(name);
				puts("File deleted.");
				free_entry(entries, pos);
				control.state.cycle = 1;
			}
		} else {
			free_entry(entries, pos);
			if (!control.state.cycle) {
				control.state.cycle = 1;
			}
		}
		putchar('\n');

		free_image_file(&file);
		pos = iwrapadd(pos, control.state.cycle, (int)entries->nr);
		control.state.cycle = iclamp(control.state.cycle, -1, 1);
	}

	end_display(&control);
	return result;
}

static int from_list(const int argc, const char *argv[], const char mode,
const union mode_args arg) {
	struct dir_images entries = {
		.names = calloc((size_t)argc, sizeof(char *)),
		.nr = (size_t)argc,
	};

	for (int i = 0; i < argc; ++i) {
		entries.names[i] = strdup(argv[i]);
		if (!entries.names[i]) {
			fprintf(stderr, "ERROR: Out of memory.\n");
			free_dir_images(&entries);
			return 1;
		}
	}

	sort_dec_tables();
	int result;
	if (mode == 'w' || mode == 'b') {
		result = test_with(&entries, mode, arg);
	} else {
		result = run_with(&entries, 0);
	}
	free_dir_images(&entries);
	return result;
}

static char * get_path_components(const char *path, const char **filename) {
	if (path[0] == '\0') {
		return strdup("");
	} else if (!strcmp(".", path) || !strcmp("./", path)) {
		return strdup("./");
	} else {
		struct stat statbuf;
		if (stat(path, &statbuf) == -1) {
			fprintf(stderr, "ERROR: Couldn't stat %s\n", path);
			return NULL;
		}

		if (S_ISREG(statbuf.st_mode)) {
			const char *slash = strrchr(path, '/');
			if (slash) {
				const size_t file_start =
					(size_t)(slash - path + 1);
				*filename = slash + 1; //path + file_start;
				return strndup(path, file_start);
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
			fprintf(stderr, "ERROR: %s is not a regular file nor a "
				"directory.\n", path);
			return NULL;
		}
	}
}

static int sort_strcoll(const void *s1, const void *s2) {
	const char * const *n1 = s1;
	const char * const *n2 = s2;
	return strcoll(*n1, *n2);
}

static int from_path(const char *path) {
	const char *first_name = NULL;
	errno = 0;
	char *dirname = get_path_components(path, &first_name);
	if (!dirname) {
		if (errno == ENOMEM) {
			fprintf(stderr, "ERROR: Out of memory.\n");
		} else {
			fprintf(stderr, "ERROR: %s is not a valid path.\n", path);
		}
		return 1;
	}

	int result;
	struct dir_images entries;
	sort_dec_tables();
	errno = 0;
	entries.names = filter_images(dirname, first_name, &entries.nr);
	if (entries.names) {
		setlocale(LC_COLLATE, "");
		qsort(entries.names, entries.nr, sizeof(*entries.names),
			sort_strcoll);

		int starting_pos = 0;
		if (first_name) {
			char **loc = bsearch(&path, entries.names, entries.nr,
				sizeof(*entries.names), sort_strcoll);
			if (loc) {
				starting_pos = (int)(loc - entries.names);
			}
		}

		result = run_with(&entries, starting_pos);
		free_dir_images(&entries);
	} else {
		if (errno) {
			fprintf(stderr, "ERROR: %s\n", strerror(errno));
		} else {
			fprintf(stderr, "ERROR: No images were found at %s\n",
				dirname[0] == '\0' ? "." : dirname);
		}
		result = 1;
	}
	free(dirname);
	return result;
}

static int print_help(const char *prog) {
	fprintf(stderr, "Usage:\n"
		"\t%1$s\n"
		"\t%1$s [--] DIR\n"
		"\t%1$s [--] FILE\n"
		"\t%1$s [--] FILE FILE [...]\n"
		"\t%1$s -s [--] FILE [...]\n"
		"\t%1$s -b [n] [--] FILE [...]\n"
		"\t%1$s -w [--] FILE\n"
		"\n"

		"Work mode (all exclusive):\n"
		"\t-h | --help\n"
		"\t\tYou are here.\n"

		"\t-s\n"
		"\t\tLoad only the file(s) given. This is the same as passing\n"
		"\t\tmultiple files without this switch.\n"

		"\t-b [n]\n"
		"\t\tBenchmark decoding time for each FILE n times, or 1 if\n"
		"\t\tunspecified.\n"

		"\t-w [raw|pam]\n"
		"\t\tDecode FILE and write the data that would have been sent\n"
		"\t\tto the card to FILE(_id).pam\n",

		prog);
	return 0;
}

int main(const int argc, const char *argv[]) {
/*
	printf("context: %zu, display: %zu, state: %zu, conf: %zu, event: %zu\n"
		"control: %zu\n",
		sizeof(struct gl_context), sizeof(struct window_geometry),
		sizeof(struct wu_state), sizeof(struct wu_conf),
		sizeof(struct wu_event), sizeof(struct window_control));

	printf("image_file: %zu, raw_img: %zu\n",
		sizeof(struct image_file), sizeof(struct raw_img));
*/

	if (argc <= 1) {
		return from_path("");
	}

	char mode = 0;
	union mode_args arg = {0};
	int idx = 1;
	if (argv[idx][0] == '-') {
		const char *opt = argv[idx];
		if (opt[1] == 'h' || !strcmp("--help", opt)) {
			return print_help(argv[0]);
		} else {
			mode = opt[1];
		}
		++idx;

		switch (mode) {
		case 'b':
			if (argc > idx && sscanf(argv[idx], "%u", &arg.iters) == 1) {
				++idx;
			} else {
				arg.iters = 1;
			}
			break;
		case 's':
		case 'w':
			arg.format = raw;
			if (argc > idx) {
				if (!strcmp("pam", argv[idx])) {
					arg.format = pam;
					++idx;
				} else if (!strcmp("raw", argv[idx])) {
					++idx;
				}
			}
			break;
		case '-':
			if (!opt[2]) { // "--"
				break;
			}
			// Fallthrough
		default:
			fprintf(stderr, "ERROR: Unknown switch '%2$s'\n"
				"(Use '%1$s -- %2$s' or '%1$s ./%2$s' to "
				"open a file or dir named '%2$s')\n",
				argv[0], argv[idx]);
			return 1;
		}
	}

	if (argc > idx && !strcmp("--", argv[idx])) {
		++idx;
	}

	if (argc <= idx) {
		fprintf(stderr, "ERROR: Expected more arguments.\n");
		return 1;
	}

	const int remaining = argc - idx;
	if (mode == 's' || mode == 'b' || mode == 'w' || remaining > 1) {
		return from_list(remaining, argv + idx, mode, arg);
	} else {
		if (argv[idx][0] != '\0') {
			return from_path(argv[idx]);
		}
		fprintf(stderr, "ERROR: null argument.\n");
		return 1;
	}
}
