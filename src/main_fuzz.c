// SPDX-License-Identifier: 0BSD
#include <unistd.h>

#include "dec.h"

#ifndef __AFL_FUZZ_TESTCASE_LEN
ssize_t fuzz_len;
unsigned char fuzz_buf[1024000];
#define __AFL_FUZZ_TESTCASE_LEN fuzz_len
#define __AFL_FUZZ_TESTCASE_BUF fuzz_buf
#define __AFL_FUZZ_INIT() void sync(void);
#define __AFL_LOOP(x) \
	((fuzz_len = read(0, fuzz_buf, sizeof(fuzz_buf))) > 0 ? 1 : 0)
#define __AFL_INIT() sync()

#endif // !__AFL_FUZZ_TESTCASE_LEN

__AFL_FUZZ_INIT()

int main(void) {
	struct image_context image = {
		.conf = conf_default(),
	};
	image.conf.max_img_size = 2048;

#ifdef __AFL_HAVE_MANUAL_CONTROL
	__AFL_INIT();
#endif

	unsigned char *buf = __AFL_FUZZ_TESTCASE_BUF;
	while (__AFL_LOOP(1 << 14)) {
		const ssize_t len = __AFL_FUZZ_TESTCASE_LEN;
		dec_src_mem(&image, wuptr_mem(buf, len), NULL, NULL);
		enum wu_error err;
		do {
			struct wuimg *img;
			err = dec_iter(&image, &img);
		} while (err == wu_ok);
		dec_free_image(&image);
		image_reset(&image);
	}
	return 0;
}
