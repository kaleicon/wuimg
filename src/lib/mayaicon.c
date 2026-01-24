// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2026 kaleido
#include <stdlib.h>
#include <string.h>

#include "misc/common.h"
#include "raster/fmt.h"
#include "lib/mayaicon.h"

/* Maya Icons or Swatches
https://forum.highend3d.com/t/renderwindoweditor-snm/3411/6
*/

struct wu_st mayaicon_load(struct mayaicon_desc *desc, struct wuimg *img) {
	++desc->idx;
	return fmt_load_raster_st(img, desc->ifp);
}

struct wu_st mayaicon_set_next(struct mayaicon_desc *desc, struct wuimg *img) {
	/* MayaIcon image header:
		Offset  Type    Name
		0       u16     NameLen
		2       u16     Width
		4       u16     Height
		6       u16     ???
		8       char    Name[NameLen]
	*/
	uint16_t buf[4];
	if (!fread(buf, sizeof(buf), 1, desc->ifp)) {
		return WUERR_HERE(wu_unexpected_eof);
	}
	img->w = endian16b(buf[1]);
	img->h = endian16b(buf[2]);
	img->channels = 4;
	img->bitdepth = 8;
	struct wutree *metadata = wuimg_get_metadata(img);
	if (metadata) {
		uint16_t namelen = endian16b(buf[0]);
		uint8_t *namebuf = small_malloc(namelen, 1);
		if (namebuf) {
			const struct wuptr name = (struct wuptr) {
				.ptr = namebuf,
				.len = fread(namebuf, 1, namelen, desc->ifp),
			};
			tree_add_leaf_len(metadata, "Name", name, NULL);
			free(namebuf);
		}
	}
	return WU_OK;
}

struct wu_st mayaicon_init(struct mayaicon_desc *desc, FILE *ifp) {
	/* MayaIcons header:
		Offset  Type    Name
		0       char    Magic[9]  // "MayaIcons"
		9       u32     ???       // Always 2 (format version?)
		13      char    Magic[8]  // "Swatches"
		21      u32     NrImages
		25
	*/
	const uint8_t magic[] = {
		'M', 'a', 'y', 'a', 'I', 'c', 'o', 'n', 's',
		0, 0, 0, 2,
		'S', 'w', 'a', 't', 'c', 'h', 'e', 's',
	};
	uint8_t hdr[25];
	if (!fread(hdr, sizeof(hdr), 1, ifp)) {
		return WUERR_HERE(wu_unexpected_eof);
	}
	*desc = (struct mayaicon_desc) {
		.ifp = ifp,
		.nr = buf_endian32b(hdr + 21),
	};
	return memcmp(hdr, magic, sizeof(*hdr))
		? WUERR_HERE(wu_invalid_signature)
		: WU_OK;
}
