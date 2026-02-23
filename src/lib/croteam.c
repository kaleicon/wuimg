// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#include <string.h>

#include "raster/fmt.h"

#include "lib/croteam.h"

size_t tbn_read_animadat(struct tbn_desc *desc,
char data[static TBN_ANIMADAT_LEN]) {
	/* ANIM structure:
		Offset  Type    Name
		0       u8      ID[4]        // "ADAT"
		4       u32     Version?     // Always 1?
		8       u8      Text[]?      // "OnlyAnim", "DEFAULT_ANIMATION"
		??      u8      Garbage?     // Has size 32 together with Text
		40      u8      ???[4]
		44      u32     MapLen       // [*]
		48      u32     Map[MapLen]  // [*]
	 * [*] These may specify frame display order, but that's just a guess
	 *     since all samples have MapLen == NbFrames, and contain every
	 *     number from 0 to MapLen-1 in increasing order in Map.
	 *     TODO: Take this section into account when decoding if that turns
	 *     out to be the case. */
	fseek(desc->ifp, (long)(desc->anim_off), SEEK_SET);

	const uint8_t anima[12] = "ANIMADAT\x01\0\0\0";
	if (fmt_sigcmp(anima, sizeof(anima), desc->ifp) == wu_ok) {
		return fread(data, 1, TBN_ANIMADAT_LEN, desc->ifp);
	}
	return 0;
}

/* Some textures have all alpha set to zero despite containing useful data.
 * Others, like shadows and fonts, depend solely on alpha to display anything
 * meaningful. And animations can be expected to have fully blank frames now
 * and then.
 * We thus disable alpha for single frame textures, and enable it if any pixel
 * has non-zero alpha. */
static void check_alpha(void *restrict data, const size_t bytes,
void *restrict ptr) {
	enum alpha_interpretation *alpha = ptr;
	if (*alpha == alpha_ignore) {
		uint32_t *row = data;
		uint32_t acc = 0;
		for (size_t pix = 0; pix < bytes/4; ++pix) {
			acc |= row[pix];
		}
		uint8_t accb[sizeof(acc)];
		memcpy(accb, &acc, sizeof(acc));
		*alpha = accb[3] ? alpha_unassociated : alpha_ignore;
	}
}

struct wu_st tbn_frame(struct tbn_desc *desc, struct wuimg *img, uint32_t i) {
	const size_t size = wuimg_size(img);
	fseek(desc->ifp, (long)(40 + i*size), SEEK_SET);
	if (img->frames->nr == 1 && img->channels == 4) {
		enum alpha_interpretation a = img->alpha;
		const size_t r = fmt_load_raster_callback(img,
			desc->ifp, check_alpha, &a);
		img->alpha = a;
		return wuerr_partial(r, size);
	}
	return fmt_load_raster_st(img, desc->ifp);
}

struct wu_st tbn_init(struct tbn_desc *desc, struct wuimg *img, FILE *ifp) {
	/* TBN header:
		Offset  Type    Name
		0       u8      VersionTag[4]
		4       u32     Version       // Always 4
		8       u8      DataTag[4]
		12      u32     Flags?        // [*]
		16      u32     Width
		20      u32     Height
		24      u32     ???
		28      u32     SHR
		32      u32     NbFrames
		36      u8      ImageTag[4]
		40      u8      Image[NbFrames][Height >> SHR][Width >> SHR]
	 * [*] Bit 0: RGBA if set, RGB otherwise
	 *     Bit 1: ???
	 *     Bit 2-31: Always 0
	*/
	const uint8_t magic[12] = "TVER\x04\0\0\0TDAT";
	struct wu_st st = wuerr(fmt_sigcmp(magic, sizeof(magic), ifp), NULL);
	if (wu_isok(st)) {
		uint32_t hdr[7];
		if (fread(hdr, sizeof(hdr), 1, ifp)) {
			*desc = (struct tbn_desc) {
				.ifp = ifp,
				.flags = endian32(hdr[0], little_endian),
				.xres = endian32(hdr[1], little_endian),
				.yres = endian32(hdr[2], little_endian),
				.unknown = endian32(hdr[3], little_endian),
				.shr = endian32(hdr[4], little_endian),
			};
			const uint32_t frames = endian32(hdr[5], little_endian);
			img->w = desc->xres >> desc->shr;
			img->h = desc->yres >> desc->shr;
			img->channels = (desc->flags & 1) ? 4 : 3;
			img->bitdepth = 8;
			img->alpha = frames > 1
				? alpha_unassociated : alpha_ignore;
			if (wuimg_frames_init(img, frames)) {
				for (uint32_t i = 0; i < frames; ++i) {
					wuimg_frame_set(img, i, 0, 0, img->w,
						img->h, 1, 12, true);
				}
				desc->anim_off = img->w*img->h*img->channels
					* frames + 40;
				st = WU_OK;
			} else {
				st = WUERR_HERE(wu_alloc_error);
			}
		} else {
			st = WUERR_HERE(wu_unexpected_eof);
		}
	}
	return st;
}
