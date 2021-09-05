#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <time.h>
#include <errno.h>

#include "wudefs.h"
#include "common.h"
#include "colorimetry.h"
#include "dec.h"

static void print_help(const char *prog) {
	printf("Usage: %s average|popular|vibrant RES FILE\n"
		"\"RES\" is the resolution at which to gather samples.\n",
		prog);
}

int main(const int argc, const char *argv[]) {
	if (argc < 4) {
		print_help(argv[0]);
		return 0;
	}

	enum background_source src;
	if (!strcmp(argv[1], "average")) {
		src = average;
	} else if (!strcmp(argv[1], "popular")) {
		src = popular;
	} else if (!strcmp(argv[1], "vibrant")) {
		src = vibrant;
	} else {
		printf("Unrecognized enum %s\n", argv[1]);
		return 1;
	}

	size_t resolution = 0;
	if (!sscanf(argv[2], "%zu", &resolution) || !resolution) {
		puts("Invalid resolution.");
		return 1;
	}

	struct image_context image = {
		.name = argv[3],
		.conf.max_img_size = USHRT_MAX / 4,
	};

	const enum wu_error result = decode_image(&image);
	if (result == wu_ok) {
		float bg[3] = {0};
		struct timespec start;
		clock_start(&start);
		const int samples = get_image_color(bg, image.file.sub_img, src,
			resolution);
		printf("%d samples taken in %ld nanoseconds.\n",
			samples, clock_nanodiff(&start));
		printf("Colors: r=%f g=%f b=%f\n", bg[0], bg[1], bg[2]);
	}
	free_image_file(&image.file);
	return 0;
}
