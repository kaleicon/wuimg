#ifndef LIB_WBM
#define LIB_WBM

#include <stdio.h>

#include "../raster/lib.h"

enum wbm_section_id {
	wbm_image_info = 0x10,
	wbm_image_data = 0x11,
	wbm_image_palette = 0x12,
	wbm_image_mask = 0x13,
};

struct wbm_section {
	enum wbm_section_id id:8;
	uint8_t fmt;
	uint16_t __pad;
	uint32_t offset;
	uint32_t decomp_size;
	uint32_t comp_size;
};

struct wbm_desc {
	FILE *ifp;
	struct raster_desc r;
	struct wbm_dir {
		uint8_t count;
		struct wbm_section *sections;
	} dir;
	int raster_idx;
	int mask_idx;
	uint8_t depth;
};

void wbm_cleanup(struct wbm_desc *desc);

size_t wbm_decode(const struct wbm_desc *desc, void *restrict dst);

enum lib_fail wbm_parse_header(struct wbm_desc *desc);

enum lib_fail wbm_open_file(struct wbm_desc *desc, FILE *ifp);

#endif /* LIB_WBM */
