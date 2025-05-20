// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2021 kaleido
#ifndef LIB_BMP
#define LIB_BMP

#include "misc/common.h"
#include "misc/endian.h"
#include "misc/wustr.h"
#include "raster/bitfield.h"
#include "raster/wuimg.h"

enum dib_os2_compression {
	os2_no_compression = 0,
	os2_8bit_rle = 1,
	os2_4bit_rle = 2,
	os2_1d_huffman = 3,
	os2_24bit_rle = 4,
};

enum dib_compression {
	dib_no_compression = 0,
	dib_8bit_rle = 1,
	dib_4bit_rle = 2,
	dib_bitfield = 3,
};

enum dib_order {
	dib_bottom_up,
	dib_top_down,
};

enum dib_type {
	dib_core_header = 12,
	dib_os2_2x_bitmap_header_min = 16,
	dib_info_header = 40,
	dib_v2_info_header = 52,
	dib_v3_info_header = 56,
	dib_os2_2x_bitmap_header = 64,
	dib_v4_header = 108,
	dib_v5_header = 124,
};

typedef uint32_t dib_cie_t;
typedef uint32_t dib_gamma_t;

struct dib_ciexyz {
	dib_cie_t x, y, z;
};

struct dib_gamma {
	dib_gamma_t r, g, b;
};

enum dib_rendering_intent {
	dib_gm_unset = 0,
	dib_gm_abs_colorimetric = 1,
	dib_gm_business = 1 << 1,
	dib_gm_graphics = 1 << 2,
	dib_gm_images = 1 << 3,
};

enum dib_lcs_type {
	dib_lcs_calibrated_rgb = 0,
	dib_lcs_srgb = FOURCC('s', 'R', 'G', 'B'),
	dib_lcs_windows_color_space = FOURCC('W', 'i', 'n', ' '),
	dib_profile_linked = FOURCC('L', 'I', 'N', 'K'),
	dib_profile_embedded = FOURCC('M', 'B', 'E', 'D'),
};

struct dib_lcs {
	enum dib_lcs_type type;
	struct dib_ciexyz r, g, b;
	struct dib_gamma gamma;
	uint32_t profile_off;
	enum dib_rendering_intent intent;
};

struct dib_desc {
	FILE *ifp;
	bool bmp_header;
	enum trit is_os2:8;

	unsigned char depth;
	enum dib_type type:8;
	enum dib_order order:8;
	enum dib_compression compression:8;
	uint32_t pal_entries;
	struct bitfield bf;
	struct dib_lcs lcs;

	size_t size;
};

const char * dib_compression_str(enum dib_compression comp);

const char * dib_type_str(const struct dib_desc *desc);

bool dib_get_linked_profile_name(const struct dib_desc *desc,
struct wustr *name);

bool dib_decode(const struct dib_desc *desc, struct wuimg *img);

struct wu_st dib_parse_header(struct dib_desc *desc, struct wuimg *img);

struct wu_st dib_open_file(struct dib_desc *desc, FILE *ifp, bool is_bmp,
enum trit is_os2);


/* ICO functions */
enum ico_type {
	ico_icon = 1,
	ico_cursor = 2,
};

struct ico_image {
	uint16_t x, y;
	uint32_t size, offset;
};

struct ico_desc {
	struct dib_desc dib;
	struct ico_image *images;
	uint16_t count;
	enum ico_type type:16;
};

const char * ico_type_str(enum ico_type);

void ico_cleanup(struct ico_desc *desc);

bool ico_decode(struct ico_desc *desc, struct wuimg *img);

struct wu_st ico_set_image(struct ico_desc *desc, struct wuimg *img, uint16_t i);

struct wu_st ico_parse_header(struct ico_desc *desc);

struct wu_st ico_open_file(struct ico_desc *desc, FILE *ifp);


#include "dec_enable.def"
#ifdef WU_ENABLE_BMZ
struct bmz_desc {
	uint8_t *restrict buf;
	struct dib_desc bmp;
};

void bmz_cleanup(struct bmz_desc *desc);

struct wu_st bmz_open(struct bmz_desc *desc, struct mparser mp);
#endif /* WU_ENABLE_BMZ */

#endif /* LIB_BMP */
