// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#include "misc/decomp.h"
#include "misc/endian.h"
#include "misc/mem.h"
#include "lwi.h"

const char * lwi_field_str(const enum lwi_field field) {
	switch (field) {
	case lwi_zero: return "00?";
	case lwi_image: return "Image";
	case lwi_end: return "End?";
	case lwi_tool: return "Tool";
	case lwi_source: return "Source";
	case lwi_author: return "Author";
	case lwi_copyright: return "Copyright";
	case lwi_timestamp: return "Date";
	}
	return "???";
}

struct wu_st lwi_decode(struct mparser mp, struct wuimg *img) {
	if (!wuimg_alloc_noverify(img)) {
		return WUERR_HERE(wu_alloc_error);
	}

	const size_t dims = img->w*img->h;
	const struct wuptr src = mp_remaining(&mp);
	return wuerr_partial(
		decomp_topbyterle(img->data, dims, src.ptr, src.len, 3),
		dims);
}

static struct wu_st lwi_setup(struct mparser *mp, struct wuimg *img) {
	const uint8_t *hdr = mp_slice(mp, 9);
	if (hdr) {
		img->w = buf_endian32(hdr, big_endian);
		img->h = buf_endian32(hdr + 4, big_endian);
		img->channels = 3;
		img->bitdepth = 8;
		hdr += 8;
		for (size_t i = 0; i < 2; ++i) {
			const uint8_t field = hdr[0];
			mp_seek_cur(mp, 12);
			switch (field) {
			case 0x00:
				hdr = mp_slice(mp, 1);
				if (!hdr) {
					return WUERR_HERE(wu_unexpected_eof);
				}
				continue;
			case 0x11:
				return wuimg_verify_st(img);
			}
			break;
		}
		return WUERR_HERE(wu_invalid_header);
	}
	return WUERR_HERE(wu_unexpected_eof);

}

struct wu_st lwi_next_field(struct mparser *mp, struct wuimg *img,
enum lwi_field *type, struct wuptr *data) {
	for (size_t i = 0; i < 2; ++i) {
		const uint8_t *hdr = mp_slice(mp, 2);
		if (!hdr) {
			return WUERR_HERE(wu_unexpected_eof);
		}
		size_t len = hdr[1];
		*type = hdr[0];
		switch (*type) {
		case lwi_tool:
		case lwi_source:
		case lwi_author:
		case lwi_copyright:
		case lwi_timestamp:
			*data = mp_avail(mp, len);
			return WU_OK;
		case lwi_image:
			mp_seek_cur(mp, -1);
			return lwi_setup(mp, img);
		case lwi_end:
			mp_seek_cur(mp, 3); // 13 00 00 00 01
			continue;
		case lwi_zero:
			mp_seek_cur(mp, 2); // 00 00 00 01
			continue;
		}
		return wuerr(wu_uncertain_validity, "unknown field type");
	}
	return wuerr(wu_uncertain_validity, "weird field order");
}
