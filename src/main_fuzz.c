// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2022 kaleido
#include <unistd.h>

#include "dec.h"
#include "fmtmap.h"

__AFL_FUZZ_INIT()

int main(int argc, char **argv) {
	struct image_context image = {
		.conf = conf_default(),
	};
	image.conf.max_img_size = 2048;

	const struct image_fn *fn = NULL;
	if (argc > 1) {
		const struct fmt_desc *fmt = fmtmap_by_name(argv[1]);
		if (!fmt) {
			return 1;
		} else if (fmt->is_auto) {
			return 2;
		}
		fn = fmt->dec.fn;
	}

#ifdef __AFL_HAVE_MANUAL_CONTROL
	__AFL_INIT();
#endif

	unsigned char *buf = __AFL_FUZZ_TESTCASE_BUF;
	while (__AFL_LOOP(1 << 14)) {
		const ssize_t len = __AFL_FUZZ_TESTCASE_LEN;
		dec_src_mem(&image, wuptr_mem(buf, (size_t)len), NULL, fn);
		enum wu_error err;
		do {
			struct wuimg *img;
			err = dec_iter(&image, &img);
		} while (err == wu_ok);
		dec_free(&image);
		image_reset(&image);
	}
	return 0;
}
