// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2021 kaleido
#include <stdlib.h>
#include <string.h>

#include "xcursor.h"
#include "misc/common.h"
#include "misc/endian.h"
#include "misc/math.h"
#include "raster/fmt.h"

/* Though this format is simple enough, its only written spec is the mildly
 * unhelpful Xcursor(3) man page, so some reading of the libXcursor code is
 * required.
   https://gitlab.freedesktop.org/xorg/lib/libxcursor

 * Important points:
 *  · The format is little endian
 *  · All fields are unsigned
 *  · libXcursor has limits for the image size, comment length, and TOC length.
 *    Only the image limit is enshrined on the man page, but we've copied all
 *    of them here for a lack of better options.
*/

static const uint32_t XCURSOR_TOC_LIMIT = 0x10000;
static const uint32_t XCURSOR_STR_LIMIT = 0x1000000;
static const uint32_t XCURSOR_DIM_LIMIT = 0x7fff;

static enum xcursor_chunk_type type_to_enum(const uint32_t type) {
	const uint32_t id = type & 0xffff;
	if (0xffff - id == (type >> 16)) {
		return id;
	}
	return 0;
}

const char * xcursor_comment_type_str(enum xcursor_comment_type type) {
	switch (type) {
	case xcursor_comment_copyright: return "Copyright";
	case xcursor_comment_license: return "License";
	case xcursor_comment_other: return "Comment";
	}
	return "???";
}

void xcursor_free(struct xcursor_desc *desc) {
	free(desc->toc);
}

size_t xcursor_get_chunk_data(const struct xcursor_desc *desc,
const struct xcursor_chunk *chunk, void *restrict dst) {
	fseek(desc->ifp, chunk->pos, SEEK_SET);
	return fread(dst, 1, chunk->len, desc->ifp);
}

static struct wu_st common_xcur_chunk(const struct xcursor_desc *desc,
const struct xcursor_toc *entry, struct xcursor_chunk *chunk,
const size_t elems, uint32_t *buf) {
	/* Common chunk structure:
		Offset  Size    Name
		0       DWORD   HeaderSize
		4       DWORD   ChunkType  // Must match the TOC field
		8       DWORD   SubType    // Must match the TOC field
		12      DWORD   Version    // 1 for both chunk types
		16

	 * Comment chunk structure:
		HeaderSize must be 20
		SubType must be one of 1 (copyright), 2 (license), or 3 (other)
		String must be encoded as UTF-8 and not include the ending null
			Offset  Size    Name
			16      DWORD   StringLength
			20      CHAR[]  String

	 * Image chunk structure:
		HeaderSize must be 36
		SubType is the nominal image size. That is, Max(Width, Height)
		Width and Height must be <= 0x7fff
		XHot and YHot must be <= Width and Height, respectively
		ARGBPixels are little-endian uint32, so the byte order is BGRA
			Offset  Size    Name
			16      DWORD   Width
			20      DWORD   Height
			24      DWORD   XHot
			28      DWORD   YHot
			32      DWORD   Delay      // In milliseconds
			36      DWORD[] ARGBPixels
	*/

	const size_t size = elems * sizeof(*buf);
	fseek(desc->ifp, entry->pos, SEEK_SET);
	if (!fread(buf, size, 1, desc->ifp)) {
		return WUERR_HERE(wu_unexpected_eof);
	}

	const uint32_t header_size = endian32l(buf[0]);
	chunk->type = type_to_enum(endian32l(buf[1]));
	const uint32_t subtype = endian32l(buf[2]);
	const uint32_t version = endian32l(buf[3]);

	const char *msg = NULL;
	if (header_size != size) {
		msg = "unexpected chunk header size";
	} else if (chunk->type != entry->type) {
		msg = "chunk type doesn't match that of TOC index";
	} else if (subtype != entry->subtype) {
		msg = "chunk subtype doesn't match that of TOC index";
	} else if (version != 1) {
		msg = "chunk version != 1";
	}
	if (msg) {
		return wuerr(wu_invalid_header, msg);
	}
	chunk->pos = ftell(desc->ifp);
	return WU_OK;
}

struct wu_st xcursor_get_image_info(const struct xcursor_desc *desc,
const struct xcursor_toc *entry, struct xcursor_chunk *chunk,
struct wuimg *img) {
	uint32_t buf[9];
	struct wu_st st = common_xcur_chunk(desc, entry, chunk, ARRAY_LEN(buf),
		buf);
	if (wu_isok(st)) {
		img->w = endian32l(buf[4]);
		img->h = endian32l(buf[5]);
		img->channels = 4;
		img->bitdepth = 8;
		img->layout = pix_bgra;
		chunk->u.image = (struct xcursor_image) {
			.xhot = endian32l(buf[6]),
			.yhot = endian32l(buf[7]),
			.delay = endian32l(buf[8]),
		};
		const size_t max = zumax(img->w, img->h);
		if (!max || max > XCURSOR_DIM_LIMIT) {// || max != subtype) {
			return wuerr(wu_invalid_header,
				"image exceeds xcursor size limit");
		}
		st = wuimg_verify_st(img);
		if (wu_isok(st)) {
			chunk->len = wuimg_size(img);
		}
	}
	return st;
}

struct wu_st xcursor_get_comment_info(const struct xcursor_desc *desc,
const struct xcursor_toc *entry, struct xcursor_chunk *chunk) {
	uint32_t buf[5];
	struct wu_st st = common_xcur_chunk(desc, entry, chunk, ARRAY_LEN(buf),
		buf);
	if (wu_isok(st)) {
		switch (entry->subtype) {
		case xcursor_comment_copyright:
		case xcursor_comment_license:
		case xcursor_comment_other:
			break;
		default:
			return wuerr(wu_invalid_header, "unknown entry subtype");
		}

		chunk->u.comment = (struct xcursor_comment) {
			.type = entry->subtype,
		};
		chunk->len = endian32l(buf[4]);
		if (chunk->len > XCURSOR_STR_LIMIT) {
			return wuerr(wu_exceeds_size_limit,
				"comment exceeds xcursor size limit");
		}
	}
	return st;
}

static struct wu_st load_xcur_toc(struct xcursor_desc *desc) {
	/* TOC structure:
		Offset  Size    Name
		0       DWORD   ChunkType
		4       DWORD   SubType
		8       DWORD   Location  // Absolute position of chunk in file
		12
	*/

	desc->toc = small_malloc(desc->ntoc, sizeof(*desc->toc));
	if (!desc->toc) {
		return WUERR_HERE(wu_alloc_error);
	}

	if (!fread(desc->toc, desc->ntoc * sizeof(*desc->toc), 1, desc->ifp)) {
		return WUERR_HERE(wu_unexpected_eof);
	}

	for (uint32_t i = 0; i < desc->ntoc; ++i) {
		struct xcursor_toc *entry = desc->toc + i;
		endian_loop32((uint32_t *)entry, little_endian, 3);
		entry->type = type_to_enum(entry->type);
		switch (entry->type) {
		case xcursor_chunk_comment:
			desc->comments += 1;
			break;
		case xcursor_chunk_image:
			desc->images += 1;
			break;
		}
	}
	return WU_OK;
}

struct wu_st xcursor_parse_header(struct xcursor_desc *desc, FILE *ifp,
uint32_t limit) {
	/* Xcursor header:
		Offset  Size    Name
		0       BYTE    Signature[4]
		0       DWORD   HeaderBytes     // 16
		4       DWORD   FileVersion     // 0x00010000 (means 1.0)
		8       DWORD   NrOfEntries
		12              TableOfContents
	*/
	*desc = (struct xcursor_desc) {
		.ifp = ifp,
	};
	const uint8_t sig[] = {'X', 'c', 'u', 'r'};
	uint32_t header[4];
	if (!fread(header, sizeof(header), 1, ifp)) {
		return WUERR_HERE(wu_unexpected_eof);
	} else if (memcmp(header, sig, sizeof(sig))) {
		return WUERR_HERE(wu_invalid_signature);
	}

	const uint32_t size = endian32l(header[1]);
	const uint32_t version = endian32l(header[2]);
	desc->ntoc = endian32l(header[3]);

	if (!limit) {
		limit = XCURSOR_TOC_LIMIT;
	}
	if (size != 16) {
		return wuerr(wu_invalid_header, "xcursor header size != 16");
	} else if (version != 0x10000) {
		return wuerr(wu_invalid_header, "xcursor version != 1.0");
	} else if (!desc->ntoc) {
		return wuerr(wu_no_image_data, "no chunks in xcur file");
	} else if (desc->ntoc > limit) {
		return wuerr(wu_exceeds_size_limit,
			"nr of TOC entries exceeds limit");
	}
	return load_xcur_toc(desc);
}
