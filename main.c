#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <time.h>

#include <epoxy/gl.h>
#include <GLFW/glfw3.h>

#include "wudefs.h"
#include "common.h"
#include "opengl.h"
#include "window.h"
#include "dec.h"

struct dir_imgs {
	struct file_class *files;
	size_t nr;
};

__attribute__((unused))static int write_pam(const struct image_file *infile) {
	const char *tuples[] = {"GRAYSCALE", "GRAYSCALE_ALPHA",
					"RGB", "RGB_ALPHA"};

	const char *ext = ".pam";
	size_t namelen = (size_t)(strrchr(infile->name, '.') - infile->name);
	char *outname = malloc(namelen + strlen(ext));

	for (size_t i = 0; i < infile->nr; ++i) {
		struct raw_img *img = &(infile->sub_img[i]);

		if (img->id == NULL) {
			sprintf(outname, "%.*s%s", (int)namelen, infile->name,
				ext);
		} else {
			size_t size = namelen + strlen(img->id) + 1;
			outname = realloc(outname, size);
			sprintf(outname, "%.*s_%s%s", (int)namelen,
				infile->name, img->id, ext);
		}

		FILE *ofp = fopen(outname, "wb");
		if (ofp) {
			fprintf(ofp,
				"P7\n"
				"WIDTH %u\n"
				"HEIGHT %u\n"
				"DEPTH %hhu\n"
				"MAXVAL 255\n"
				"TUPLTYPE %s\n"
				"ENDHDR\n",
				img->w, img->h, img->channels,
				tuples[img->channels - 1]);
			fwrite(img->data, img->channels, img->w * img->h, ofp);
			fclose(ofp);
		}
	}
	return 0;
}

static void free_dir_images(struct dir_imgs *entries) {
	for (size_t i = 0; i < entries->nr; ++i) {
		free(entries->files[i].name);
	}
	free(entries->files);
}

static int str_struct_coll(const void *restrict s1, const void *restrict s2) {
	const char *name = (const char *)s1;
	const struct file_class *entry = (const struct file_class *)s2;
	return strcoll(name, entry->name);
}

static int struct_coll(const void *restrict s1, const void *restrict s2) {
	const struct file_class *f1 = (const struct file_class *)s1;
	const struct file_class *f2 = (const struct file_class *)s2;
	return strcoll(f1->name, f2->name);
}

static size_t read_directory(const char *name, struct dir_imgs *entries) {
	char *dirname = NULL;
	const char *slash = strrchr(name, '/');
	if (slash) {
		size_t len = (size_t)(slash - name + 1);
		dirname = strndup(name, len);
	}

	entries->files = find_images(dirname, &entries->nr);
	if (dirname) {
		free(dirname);
	}
	if (entries->files) {
		qsort(entries->files, entries->nr, sizeof(struct file_class),
			struct_coll);
		const struct file_class *cur = bsearch(name, entries->files,
			entries->nr, sizeof(struct file_class), str_struct_coll);
		if (cur) {
			return (size_t)(cur - entries->files);
		}
	}
	return 0;
}

#define BG (float)(1.0/8.0)
static bool window_loop(GLFWwindow *window, struct window_control *control,
const struct image_file *file, const int eternal) {
	int idx = 0;
	int shown = 0;
	const struct raw_img *img = file->sub_img;
	int remaining = img[0].msec;
	const int refresh_int = 1000 / get_monitor_refresh_rate();

	for (;;) {
		idx = iwrapadd(idx, control->cycle_sub_img, (int)file->nr);
		control->cycle_sub_img = iclamp(control->cycle_sub_img, -1, 1);

		if (idx != shown) {
			if (control->anim == playing) {
				int safety_counter = 0;
				while (img[idx].msec < refresh_int) {
					++safety_counter;
					if (safety_counter >= (int)file->nr) {
						control->anim = none;
						break;
					}
					idx = (idx + 1) % (int)file->nr;
				}
				remaining += img[idx].msec;
			}

			if (!update_window(window, control, file, idx)) {
				continue;
			}
			shown = idx;
		}
		control->cycle_sub_img = 0;
		do {
			if (control->cycle) {
				if (!control->cycle_wait && !eternal) {
					return true;
				}
			} else if (glfwWindowShouldClose(window) == GLFW_TRUE) {
				return false;
			}
			glClearColor(BG, BG, BG, 0.5);
			glClear(GL_COLOR_BUFFER_BIT);
			glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_BYTE, 0);
			glfwSwapBuffers(window);
			if (control->anim == playing) {
				remaining -= refresh_int;
				if (remaining < refresh_int) {
					++control->cycle_sub_img;
					break;
				}
				glfwPollEvents();
			} else {
				glfwWaitEvents();
			}
		} while (!control->cycle_sub_img);
	}
}

static bool setup_display(GLFWwindow **window, struct window_control *control,
struct gl_context *context) {
	*window = create_window();
	if (!*window) {
		puts("Failed to create window.");
		return false;
	}

	if (!setup_opengl(context)) {
		puts("Failed to setup OpenGL context.");
		glfwTerminate();
		return false;
	}

	setup_window(*window, control);
	control->context = context;
	return true;
}

static void help(const char *prog) {
	printf("Usage: %s FILE [...]\n", prog);
}

int main(int argc, const char *argv[]) {
	if (argc < 2 || !strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
		help(argv[0]);
		return 0;
	}

	struct dir_imgs entries = {NULL, 0};
	int pos = (int)read_directory(argv[1], &entries);
	if (!entries.files) {
		return 1;
	}

	GLFWwindow *window;
	struct window_control control;
	struct gl_context context;
	if (!setup_display(&window, &control, &context)) {
		free_dir_images(&entries);
		return 1;
	}

	struct timespec before, after;

	struct image_file file;
	memset(&file, 0, sizeof(struct image_file));
	while (glfwWindowShouldClose(window) == GLFW_FALSE) {
		if (!entries.files[pos].name) {
			pos = iwrapadd(pos, control.cycle, (int)entries.nr);
			continue;
		}

		clock_gettime(CLOCK_REALTIME, &before);
		enum wu_error_type result = decode_image(&file,
			&entries.files[pos]);
		if (result == wu_ok) {
			clock_gettime(CLOCK_REALTIME, &after);
			update_window(window, &control, &file, 0);

			printf("\n%d/%zu, %s\n", pos + 1, entries.nr,
				file.name);
			printf("Decoded in %lu nanoseconds.\n",
				timespec_nanodiff(&before, &after));
			print_image_information(&file);
//			write_pam(&file);
//			return 0;
		} else {
			printf("\nError %d when decoding %s: %s.\n"
				"Library message: \"%s\"\n",
				result, file.name, wu_error_message(result),
				file.err_msg);
			free(entries.files[pos].name);
			memset(&entries.files[pos], 0,
				sizeof(struct file_class));
		}

		if (!window_loop(window, &control, &file, (entries.nr == 1))) {
			break;
		}

		free_image_file(&file);
		pos = iwrapadd(pos, control.cycle, (int)entries.nr);
		control.cycle = iclamp(control.cycle, -1, 1);
	}

	free_dir_images(&entries);
	delete_gl_context(&context);
	glfwTerminate();
	return 0;
}
