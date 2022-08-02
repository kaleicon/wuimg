#ifndef LIB_MAG
#define LIB_MAG

#include "wudefs.h"

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

struct mag_comment {
	uint8_t *data;
	size_t text_len;
	size_t area_len;
};

struct mag_msx {
	enum mag_msx_screen screen:8;
	bool interlace;
};

struct mag_desc {
	FILE *ifp;
	uint8_t model[4];
	enum mag_model_code code:8;
	struct mag_msx msx;
	size_t row_dwords, dwords;
	struct mag_comment comment;
	long null_pos;
	struct mag_section flag_a, flag_b, color;
	struct raster_pal *yjk_pal;
};

const char * mag_msx_screen_str(enum mag_msx_screen flag);

const char * mag_model_code_str(enum mag_model_code code);

void mag_cleanup(struct mag_desc *desc);

size_t mag_decode(const struct mag_desc *desc, struct raw_img *img);

enum wu_error mag_parse(struct mag_desc *desc, struct raw_img *img);

enum wu_error mag_open(struct mag_desc *desc, FILE *ifp);

#endif /* LIB_MAG */
