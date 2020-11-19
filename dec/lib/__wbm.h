#ifndef LIB_WBM
#define LIB_WBM

#include <stdio.h>

enum wbm_dir_id {
	wbm_size_info = 0x10,
};

struct wbm_desc {
	FILE *ifp;
	unsigned w, h;
	unsigned char bpp;
};
