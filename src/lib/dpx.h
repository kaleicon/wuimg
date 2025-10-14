// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2022 kaleido
#ifndef LIB_DPX
#define LIB_DPX

#include "raster/wuimg.h"
#include "misc/endian.h"

enum dpx_signal {
	dpx_signal_undefined = 0,
	dpx_signal_ntsc = 1,
	dpx_signal_pal = 2,
	dpx_signal_pal_m = 3,
	dpx_signal_secam = 4,
	dpx_signal_525_interlace_4_3 = 50,
	dpx_signal_625_interlace_4_3 = 51,
	dpx_signal_525_interlace_16_9 = 100,
	dpx_signal_625_interlace_16_9 = 101,
	dpx_signal_1050_interlace_16_9 = 150,
	dpx_signal_1125_interlace_16_9 = 151,
	dpx_signal_1250_interlace_16_9 = 152,
	dpx_signal_525_progressive_16_9 = 200,
	dpx_signal_625_progressive_16_9 = 201,
	dpx_signal_787_5_progressive_16_9 = 202,
};

enum dpx_element_type {
	dpx_element_user1 = 0,
	dpx_element_user2 = 150,
	dpx_element_user3 = 151,
	dpx_element_user4 = 152,
	dpx_element_user5 = 153,
	dpx_element_user6 = 154,
	dpx_element_user7 = 155,
	dpx_element_user8 = 156,

	dpx_element_r = 1,
	dpx_element_g = 2,
	dpx_element_b = 3,
	dpx_element_a = 4,

	dpx_element_y = 6,
	dpx_element_chroma = 7,

	dpx_element_depth = 8,
	dpx_element_composite_video = 9,

	dpx_element_rgb = 50,
	dpx_element_rgba = 51,
	dpx_element_abgr = 52,

	dpx_element_uyvy = 100,
	dpx_element_uyavya = 101,
	dpx_element_uyv = 102,
	dpx_element_uyva = 103,
};

enum dpx_oetf {
	dpx_oetf_user = 0,
	dpx_oetf_printing_density = 1,
	dpx_oetf_linear = 2,
	dpx_oetf_logarithmic = 3,
	dpx_oetf_unspecified = 4,
	dpx_oetf_smpte_240m = 5,
	dpx_oetf_ccir_709_1 = 6,
	dpx_oetf_ccir_601_2_system_b_g = 7,
	dpx_oetf_ccir_601_2_system_m = 8,
	dpx_oetf_ntsc = 9,
	dpx_oetf_pal = 10,
	dpx_oetf_z_linear = 11,
	dpx_oetf_z_homogeneous = 12,
};

enum dpx_primaries {
	dpx_primaries_user = 0,
	dpx_primaries_printing_density = 1,
	dpx_primaries_unspecified = 4,
	dpx_primaries_smpte_240m = 5,
	dpx_primaries_ccir_709_1 = 6,
	dpx_primaries_ccir_601_2_system_b_g = 7,
	dpx_primaries_ccir_601_2_system_m = 8,
	dpx_primaries_ntsc = 9,
	dpx_primaries_pal = 10,
};

enum dpx_pack {
	dpx_pack_normal = 0,
	dpx_pack_a = 1,
	dpx_pack_b = 2,
};

struct dpx_industry_television {
	bool interlaced;
	uint8_t field;
	enum dpx_signal signal:8;
	uint32_t time_code;
	uint32_t user_bits;
	float horz_rate, vert_rate;
	float frame_hz;
	float time_offset;
	float gamma;
	float black_level;
	float black_gain;
	float breakpoint;
	float white_level;
	float integration;
};

struct dpx_industry_film {
	uint32_t prefix;
	uint16_t count;
	uint8_t manufacturer, type, perf_offset;
	uint32_t frame_num, total_frames, held_count;
	float frame_rate;
	float shutter_angle;
	char format[32];
	char frame_id[32];
	char slate_info[100];
};

struct dpx_industry {
	struct dpx_industry_film film;
	struct dpx_industry_television tv;
};

struct dpx_generic_source {
	uint32_t x, y;
	float x_center, y_center;
	uint32_t w, h;
	time_t date;
	char filename[100];
	char input_device[32];
	char input_sn[32];
	struct dpx_erosion {
		uint16_t left, right, top, bottom;
	} erosion;
	uint32_t horz_aspect, vert_aspect;
	float w_mm, h_mm;
};

struct dpx_element {
	enum pix_attr attr:8;
	enum dpx_element_type type:8;
	enum dpx_pack pack:8;
	bool rle;
	uint32_t offset;
	uint32_t line_pad;
	char description[32];
};

struct dpx_generic_image {
	uint32_t w, h;
	uint8_t orientation;
	uint8_t nb_elem;
	enum dpx_oetf oetf:8;
	enum dpx_primaries primaries:8;
	uint8_t bitdepth;
	struct dpx_element elem[8];
};

struct dpx_generic_file {
	uint32_t user_size;
	time_t date;
	uint32_t encrypt;
	bool is_new_frame;
	char name[100];
	char creator[100];
	char project[200];
	char copyright[200];
};

struct dpx_generic {
	struct dpx_generic_file file;
	struct dpx_generic_image image;
	struct dpx_generic_source src;
};

enum dpx_version {
	dpx_v1 = '1',
	dpx_v2 = '2',
};

struct dpx_desc {
	FILE *ifp;
	enum endianness endian:8;
	enum dpx_version version:8;
	bool has_industry;
	struct dpx_generic generic;
	struct dpx_industry industry;
};

struct wu_st dpx_decode(const struct dpx_desc *desc, struct wuimg *img,
uint8_t i);

struct wu_st dpx_set_image(const struct dpx_desc *desc, struct wuimg *img,
uint8_t i);

struct wu_st dpx_parse(struct dpx_desc *desc, FILE *ifp);

#endif /* LIB_DPX */
