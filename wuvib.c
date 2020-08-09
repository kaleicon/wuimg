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
	printf("Usage: %s average|popular|vibrant FILE.pnm\n", prog);
}

int main(const int argc, const char *argv[]) {
	if (argc < 3) {
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
		print_help(argv[0]);
		return 1;
	}

	const struct wu_conf conf = {
		.max_img_size = USHRT_MAX,
	};

	sort_dec_tables();
	struct image_file file = {0};
	const enum wu_error result = decode_image(&file, &conf, argv[2]);
	if (result == wu_ok) {
		float bg[3] = {0};
		struct timespec before, after;
		clock_gettime(CLOCK_REALTIME, &before);
		const int samples = get_image_color(bg, file.sub_img, src, USHRT_MAX);
		clock_gettime(CLOCK_REALTIME, &after);
		printf("Average of %d samples taken in %ld nanoseconds.\n",
			samples, timespec_nanodiff(before, after));
		printf("Colors: r=%f g=%f b=%f\n", bg[0], bg[1], bg[2]);
	}
	free_image_file(&file);
	return 0;
}
