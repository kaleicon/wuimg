// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2026 kaleido
#include <string.h>

#include "misc/iff.h"
#include "lib/riff.h"

struct wu_st riffpal_load(struct riffpal_desc *desc, struct wuimg *img) {
	return wuerr_partial(fread(img->data, 4, desc->entries, desc->ifp),
		desc->entries);
}

static struct wu_st data_read(struct iff_state *iff, void *user,
const struct iff_chunk chunk) {
	/* "data" chunk struct:
		Offset  Type    Name
		0       u16     Version
		2       u16     Entries
		4       RGBX    PalEntry[Entries]
	*/
	(void)iff;

	struct riffpal_desc *desc = user;
	uint16_t hdr[2];
	if (chunk.len < sizeof(hdr)) {
		return wuerr(wu_invalid_header, "too short \"data\" chunk");
	} else if (!fread(hdr, sizeof(hdr), 1, desc->ifp)) {
		return WUERR_HERE(wu_unexpected_eof);
	}
	const uint16_t version = endian16l(hdr[0]);
	desc->entries = endian16l(hdr[1]);
	if (version != 0x300) {
		return wuerr(wu_unsupported_feature, "version != 0x300");
	} else if (desc->entries > 0x100) {
		return wuerr(wu_invalid_header, "nr of entries > 256");
	} else if (!desc->entries) {
		return wuerr(wu_no_image_data, "nr of entries == 0");
	}
	return WU_OK;
}

static struct wu_st plth_read(struct iff_state *iff, void *user,
const struct iff_chunk chunk) {
	(void)iff; (void)user; (void)chunk;
	iff->table += 1;
	return wuerr(wu_unsupported_feature, "extended palettes unsupported");
}

static const struct iff_table RIFFPAL_TABLE[] = {
	{FOURCC('p', 'l', 't', 'h'), plth_read},
	{FOURCC('d', 'a', 't', 'a'), data_read},
	{FOURCC('y', 'u', 'v', 'p'), data_read},
};

struct wu_st riffpal_init(struct riffpal_desc *desc, struct wuimg *img,
FILE *ifp) {
	/* RIFF RGB palette outline:
		RIFF "PAL "
			"plth" (if present, this is a extended palette)
			"data"

	 * YUV/XYZ palette outline:
		RIFF "PAL "
			"plth"
			"yuvp"
	*/
	uint32_t hdr[3];
	if (!fread(hdr, sizeof(hdr), 1, ifp)) {
		return WUERR_HERE(wu_unexpected_eof);
	}
	const uint8_t riff[4] = {'R', 'I', 'F', 'F'};
	const uint8_t pal[4] = {'P', 'A', 'L', ' '};
	if (memcmp(hdr, riff, sizeof(riff))
	|| memcmp(hdr + 2, pal, sizeof(pal))) {
		return wuerr(wu_invalid_signature,
			"not a RIFF Palette");
	}

	img->w = 16;
	img->h = 16;
	img->channels = 4;
	img->bitdepth = 8;
	img->alpha = alpha_ignore;

	desc->ifp = ifp;
	struct iff_state iff = {
		.table = RIFFPAL_TABLE,
		.table_len = 2,
		.endian = little_endian,
		.id_endian = big_endian,
		.user = desc,
	};
	return iff_next_FILE(&iff, ifp, (struct iff_chunk){0});
}
