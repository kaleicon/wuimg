// SPDX-License-Identifier: 0BSD
#ifndef LIB_MAG
#define LIB_MAG

#include "raster/wuimg.h"

enum mag_screen_flags {
	mag_screen_200_rows = 1,
	mag_screen_8_colors = 1 << 1,
	mag_screen_digital = 1 << 2,
	mag_screen_256_colors = 1 << 7,
};

enum mag_screen_mode {
	mag_screen_mode_pc98_standard = 0,
	mag_screen_mode_msx_sc7 = mag_screen_200_rows,
	mag_screen_mode_vm98 = mag_screen_8_colors,
	mag_screen_mode_pc88_analog_pal = mag_screen_8_colors | mag_screen_200_rows,
	mag_screen_mode_pc98_old = mag_screen_digital | mag_screen_8_colors,
	mag_screen_mode_pc88_standard =
		mag_screen_digital | mag_screen_8_colors | mag_screen_200_rows,
	mag_screen_mode_msx_sc8 = mag_screen_256_colors | mag_screen_200_rows,
};

enum mag_msx_screen {
	mag_msx2_screen7 = 0x0,
	mag_msx2_screen8 = 0x1,

	mag_msx2p_screen10 = 0x2,
	mag_msx2p_screen11 = 0x3,
	mag_msx2p_screen12 = 0x4,

	mag_msx2_screen5 = 0x5,
	mag_msx2_screen6 = 0x6,
};

enum mag_model_code {
	mag_model_msx = 0x03,
//	mag_model_x1tb = 0x1c, // ???
	mag_model_98sa = 0x62,
	mag_model_x68k = 0x68,
	mag_model_mps_new = 0x70,
	mag_model_pc88 = 0x88,
	mag_model_mac = 0x99,
	mag_model_mps = 0xff,
};

struct mag_section {
	uint32_t size, off;
};

struct mag_msx {
	enum mag_msx_screen screen:8;
	bool interlace;
};

struct mag_desc {
	FILE *ifp;
	uint8_t model[4];
	enum mag_model_code code:8;
	enum mag_screen_mode screen_mode:8;
	struct mag_msx msx;
	size_t row_dwords;
	struct wustr comm;
	long null_pos;
	struct mag_section flag_a, flag_b, color;
	struct palette *yae;
};

const char * mag_screen_mode_str(enum mag_screen_mode mode);

const char * mag_msx_screen_str(enum mag_msx_screen flag);

const char * mag_model_code_str(enum mag_model_code code);

void mag_cleanup(struct mag_desc *desc);

size_t mag_decode(const struct mag_desc *desc, struct wuimg *img);

enum wu_error mag_parse(struct mag_desc *desc, struct wuimg *img);

enum wu_error mag_open(struct mag_desc *desc, FILE *ifp);

#endif /* LIB_MAG */
