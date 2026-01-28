// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2022 kaleido
#include <unistd.h>

#include "dec.h"
#include "fmtmap.h"

__AFL_FUZZ_INIT()

int main(int argc, char **argv) {
	struct wudec_image image = {
		.conf = conf_default(),
	};
	image.conf.max_img_size = 1920;
	const struct fmt_desc *fmt = NULL;
	if (argc > 1) {
		fmt = fmtmap_by_name(argv[1]);
		if (!fmt) {
			return 1;
		} else if (fmt->is_auto) {
			return 2;
		}
	}

#ifdef __AFL_HAVE_MANUAL_CONTROL
	__AFL_INIT();
#endif

	unsigned char *buf = __AFL_FUZZ_TESTCASE_BUF;
	while (__AFL_LOOP(1 << 15)) {
		const ssize_t len = __AFL_FUZZ_TESTCASE_LEN;
		wudec_src_mem(&image, wuptr_mem(buf, (size_t)len), NULL);
		wudec_src_format(&image, fmt);
		enum wu_error err;
		do {
			struct wuimg *img;
			err = wudec_iter(&image, &img);
		} while (err == wu_ok);
		wudec_recycle_conf(&image);
	}
	return 0;
}
