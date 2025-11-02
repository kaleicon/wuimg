// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#ifndef LIB_FAX
#define LIB_FAX

#include "raster/wuimg.h"

enum fax_coding {
	fax_none = 0,
	fax_mh = 1,
	fax_mr = 2,
	fax_mmr = 3,
};

const char * fax_coding_str(enum fax_coding coding);

struct wu_st g3_decode(struct wuimg *img, struct wuptr mem,
enum fax_coding std, enum endianness order);

struct wu_st g3_1d_decode(struct wuimg *img, struct wuptr mem,
enum endianness order);

bool g3_identify(struct wuimg *img, struct wuptr mem,
enum fax_coding *out_std, enum endianness *out_end);


/* ZyXEL fax */
struct wu_st zyxel_decode(struct wuimg *img, struct wuptr fax_data);

struct wu_st zyxel_parse(struct wuimg *img, struct wuptr mem,
struct wuptr *fax_data);


/* QFX - Quick Link II */
struct qfx_desc {
	struct mparser mp;
	uint16_t nr_pages;
	const uint8_t *page_offsets;
	struct wuptr page_data;
};

struct wu_st qfx_load_page(const struct qfx_desc *desc, struct wuimg *img);

struct wu_st qfx_set_page(struct qfx_desc *desc, struct wuimg *img, uint16_t i);

struct wu_st qfx_parse(struct qfx_desc *desc, struct wuptr mem);


/* IFF-FAXX */
enum gphd_compression {
	gphd_comp_none = 255, // binary file (raw, maybe???)
	gphd_comp_1d = 0, // mh
	gphd_comp_2d = 1, // mr
	gphd_comp_2du = 2, // uncompressed READ
	gphd_comp_2dm = 3, // mmr
};

enum gphd_ph {
	gphd_ph_unlimited = 0,
	gphd_ph_a4 = 1,
	gphd_ph_b4 = 2,
};

enum gphd_scan_time { // VR-std/VR-fine scan times
	gphd_st_0_0ms = 0,
	gphd_st_5_5ms = 1,
	gphd_st_10_5ms = 2,
	gphd_st_10_10ms = 3,
	gphd_st_20_10ms = 4,
	gphd_st_20_20ms = 5,
	gphd_st_40_20ms = 6,
	gphd_st_40_40ms = 7,
};

static const unsigned GPHD_BITRATE_FACTOR = 2400;

struct faxx_gphd {
	uint16_t page_num;
	uint8_t id[22];
	uint8_t bitrate;
	enum gphd_ph page_height:8;
	bool error_correction;
	bool binary_transfer;
};

struct faxx_desc {
	struct mparser mp;
	uint16_t w, h;
	uint16_t line_mm;
	uint16_t vertical_res;
	enum fax_coding compression:8;
	bool has_fxhd;
	bool has_gphd;
	bool is_fax3;
	struct faxx_gphd gphd;
	uint32_t len;
};

const char * gphd_ph_str(enum gphd_ph ph);

struct wu_st faxx_decode(const struct faxx_desc *desc, struct wuimg *img);

void faxx_set_image(const struct faxx_desc *desc, struct wuimg *img);

struct wu_st faxx_init(struct faxx_desc *desc, struct wuptr mem);


/* APF - Async Professional Fax */
struct apf_desc {
	struct mparser mp;
	struct wuptr page_data;
	time_t timestamp;
	uint32_t cur_page;
	uint16_t nr_pages;
	bool line_len;
	bool high_res;
	uint8_t station_id[20];
};

struct wu_st apf_load_page(const struct apf_desc *desc, struct wuimg *img);

struct wu_st apf_next_page(struct apf_desc *desc, struct wuimg *img);

struct wu_st apf_parse(struct apf_desc *desc, const struct wuptr mem);

#endif /* LIB_FAX */
