// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#include <string.h>

#include "lib/cisrle.h"

/*
https://web.archive.org/web/20140721001738/http://staticweb.rasip.fer.hr/research/compress/algorithms_run-length_coding.htm#examples
*/

static const uint8_t ESC = 0x1b;

const char * cis_resolution_str(enum cis_resolution res) {
	switch (res) {
	case cis_high_res: return "High";
	case cis_medium_res: return "Medium";
	}
	return "???";
}

struct wu_st cis_decode(const struct cis_desc *desc, struct wuimg *img) {
	if (wuimg_alloc_noverify(img)) {
		const struct wuptr src = desc->data;
		const size_t dst_len = wuimg_size(img);
		size_t d = 0;
		bool on = desc->swap;
		for (size_t s = 0; s < src.len; ++s) {
			size_t c = src.ptr[s];
			if (c >= 0x20) {
				c -= 0x20;
				if (dst_len - d < c) {
					c = dst_len - d;
				}
				memset(img->data + d, on, c);
				d += c;
				on = !on;
				if (d == dst_len) {
					break;
				}
			} else if (c == ESC) {
				d = dst_len;
				break;
			}
		}
		return wuerr_partial(d, dst_len);
	}
	return WUERR_HERE(wu_alloc_error);
}

static void skip_black(struct cis_desc *desc) {
	/* Some files begin with 2 or 4 black rows, switch to white
	 * inmediately, and are truncated at the bottom. If so, skip
	 * the black section so that they're displayed fully.
	 * Often, the last row will be partially garbled. Still better than
	 * four rows of nothing, maybe. */
	size_t d = 0;
	size_t s = 0;
	while (desc->data.len - s >= 2) {
		size_t a = desc->data.ptr[s];
		++s;
		size_t b = desc->data.ptr[s];
		d += a - 0x20;
		if (a == 0x7e && b == 0x20) {
			++s;
			continue;
		} else if (b > 0x20 && (d == 1024 || d == 512)) {
			desc->data.ptr += s;
			desc->data.len -= s;
			desc->swap = true;
		}
		break;
	}
}

struct wu_st cis_parse(struct cis_desc *desc, struct wuimg *img,
const struct wuptr mem, const bool try_fix) {
	/* CompuServe RLE header:
		Offset  Type    Name
		0       u8      Magic[2]   # 0x1b, 'G'
		2       u8      Resolution # 'H' or 'M'
		3
	*/

	const uint8_t magic[2] = {ESC, 'G'};
	if (mem.len > 3) {
		if (!memcmp(mem.ptr, magic, sizeof(magic))) {
			*desc = (struct cis_desc) {
				.data.ptr = mem.ptr + 3,
				.data.len = mem.len - 3,
				.res = mem.ptr[2],
			};
			img->w = desc->res == cis_high_res ? 256 : 128;
			img->h = desc->res == cis_high_res ? 192 : 96;
			img->channels = 1;
			img->bitdepth = 8;
			img->bitrange = 1;
			if (img->w == 256 && try_fix) {
				skip_black(desc);
			}
			return wuimg_verify_st(img);
		}
		return WUERR_HERE(wu_invalid_header);
	}
	return WUERR_HERE(wu_unexpected_eof);
}
