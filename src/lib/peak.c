// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2026 kaleido
#include <string.h>

#include "misc/math.h"
#include "lib/peak.h"

/* Graph only the upper PEAK_BITS per channel. Image width will thus be
 * `channels << (PEAK_BITS)`.
 * Should not exceed 8 to avoid underflow for 8bit data. */
static const uint8_t PEAK_BITS = 8;

static struct wu_st peak_set_dims(struct wuimg *img, const size_t channels,
const size_t samples) {
	img->w = channels << PEAK_BITS;
	img->h = samples;
	img->channels = 1;
	img->bitdepth = 8;
	img->rotate = 3;
	return WU_OK;
}

static struct wu_st peak_header_check(struct peak_desc *desc, const uint8_t *magic,
const size_t magic_len, const size_t header_len, const struct wuptr mem) {
	*desc = (struct peak_desc) {
		.mem = {
			.len = mem.len - header_len,
			.ptr = mem.ptr + header_len,
		}
	};
	if (mem.len < header_len) {
		return WUERR_HERE(wu_unexpected_eof);
	} else if (memcmp(mem.ptr, magic, magic_len)) {
		return WUERR_HERE(wu_invalid_signature);
	}
	return WU_OK;
}

static void peak_graph_write(uint8_t *dst, unsigned peak_lo, unsigned peak_hi,
const unsigned depth) {
	const unsigned sign_bit = 1u << (depth - 1);
	// map into absolute terms
	peak_lo ^= sign_bit;
	peak_hi ^= sign_bit;
	// ensure order, convert peak_hi to extent
	peak_hi = umax(peak_hi, peak_lo) - peak_lo;
	// round peak_hi upwards
	peak_hi += (1u << (depth - PEAK_BITS)) - 1;
	// divide
	peak_lo >>= depth - PEAK_BITS;
	peak_hi >>= depth - PEAK_BITS;
	memset(dst + peak_lo, 0xff, peak_hi);
}

struct wu_st peak_graph(const struct peak_desc *desc, struct wuimg *img) {
	const size_t samples = zumin(img->h, desc->mem.len/desc->sample_size);
	const unsigned depth = desc->high_depth ? 16 : 8;
	const unsigned lo_off = (unsigned)desc->lo_after << desc->high_depth;
	const unsigned hi_off = (unsigned)!desc->lo_after << desc->high_depth;
	for (size_t y = 0; y < samples; ++y) {
		const uint8_t *src = desc->mem.ptr
			+ y*desc->sample_size + desc->sample_offset;
		for (size_t z = 0; z < desc->channels; ++z) {
			uint8_t *dst = img->data
				+ ((y*desc->channels + z) << PEAK_BITS);
			unsigned peak_lo, peak_hi;
			if (desc->high_depth) {
				peak_lo = buf_endian16l(src + z*4 + lo_off);
				peak_hi = buf_endian16l(src + z*4 + hi_off);
			} else {
				peak_lo = src[z*2 + lo_off];
				peak_hi = src[z*2 + hi_off];
			}
			peak_graph_write(dst, peak_lo, peak_hi, depth);
		}
	}
	return wuerr_partial(samples, img->h);
}

/* Peak Graphical Waveform
 * Used by Adobe Audition/Cool Edit. */
struct wu_st peak_init(struct peak_desc *desc, struct wuimg *img,
const struct wuptr mem) {
	/* PK header:
		Offset  Type    Name
		0       u8      Signature[4]
		4       u32     Depth?       # always 0x10?
		8       s32     Samples      # bias of -1
		12      u32     ???          # Hz?
		16      u32     ???          # always 0?
		20      s32     ???          # always < Samples?
		24      s32     ???          # always -1?
		28      u32     ???          # always 0?
		32      u8      ???[28]
		60      u16     Channels
		62      u16     ???          # 0x110 + channels?
		64      struct  Sample[Samples]

	 * Sample struct:
		0       u32     Timestamp?
		4       s16     Peaks[Channels][2]  # Lo then Hi
	*/
	const uint8_t magic[8] = {
		0xf1, 0x06, 0, 0,
		0, 1, 0, 0,
	};
	struct wu_st st = peak_header_check(desc, magic, sizeof(magic), 64, mem);
	if (!wu_isok(st)) {
		return st;
	}
	const uint8_t *hdr = mem.ptr;
	const int32_t samples = (int32_t)buf_endian32l(hdr + 8);
	if (samples < 0) {
		return wuerr(wu_invalid_header, "nr samples <= 0");
	}
	desc->channels = buf_endian16l(hdr + 60);
	desc->sample_offset = 4u;
	desc->sample_size = desc->channels*2u*2u + desc->sample_offset;
	desc->high_depth = true;
	return peak_set_dims(img, desc->channels, (uint32_t)samples + 1);
}


/* Reapeaks */
struct wu_st rpkn_init(struct peak_desc *desc, struct wuimg *img,
const struct wuptr mem) {
	/* RPKN header:
		Offset  Type    Name
		0       u8      Magic[4]
		4       u8      Channels?
		5       u8      ???
		6       u32     Hz?
		10      u8      ???[12]
		22      u32     ValidSamples?
		26      u32     ???
		30      u32     Blocks?
		34      u32     Hz?
		38      u32     ???
		42      s16     Peaks[][Channels][2]  # Hi then Lo

	 * There may be extra samples seemingly unrelated to the source audio.
	 * The Blocks field may have something to do with that.
	*/
	const uint8_t magic[4] = {'R', 'P', 'K', 'N'};
	struct wu_st st = peak_header_check(desc, magic, sizeof(magic), 42, mem);
	if (!wu_isok(st)) {
		return st;
	}
	const uint8_t *hdr = mem.ptr;
	desc->channels = hdr[4];
	desc->sample_size = desc->channels*2u*2u;
	desc->high_depth = true;
	desc->lo_after = true;
	return peak_set_dims(img, desc->channels, buf_endian32l(hdr + 22));
}


/* Sound Forge Peak Data */
struct wu_st sfpk_init(struct peak_desc *desc, struct wuimg *img,
const struct wuptr mem) {
	/* SFK header:
		Offset  Type    Name
		0       u8      Magic[4]
		4       u32     Version?    # always 1?
		8       u32     HeaderSize  # 0x40
		12      u32     ???
		16      u32     Depth
		20      u32     Channels?
		24      u32     Divisor
		28      u32     Timespan?
		32      u32     ???         # always 0?
		36      u32     ???
		40      u8      ???[24]     # always 0?
		64      s16     Peaks[][Channels][2]  # Lo then Hi
	*/
	const uint8_t magic[12] = {
		'S', 'F', 'P', 'K',
		0x01, 0, 0, 0,
		0x40, 0, 0, 0,
	};
	struct wu_st st = peak_header_check(desc, magic, sizeof(magic), 64, mem);
	if (!wu_isok(st)) {
		return st;
	}
	const uint8_t *hdr = mem.ptr;
	const uint32_t depth = buf_endian32l(hdr + 16);
	switch (depth) {
	case 8: case 16:
		desc->high_depth = depth == 16;
		break;
	default: return wuerr(wu_invalid_header, "depth != 8 or 16");
	}
	const uint32_t divisor = buf_endian32l(hdr + 24);
	const uint32_t timespan = buf_endian32l(hdr + 28);
	if (!divisor) {
		return wuerr(wu_int_overflow, "time divisor == 0");
	}
	desc->channels = buf_endian32l(hdr + 20);
	desc->sample_size = (desc->channels*2u) << desc->high_depth;
	return peak_set_dims(img, desc->channels, zuceildiv(timespan, divisor));
}
