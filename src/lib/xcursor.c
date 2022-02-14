#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "../common.h"
#include "xcursor.h"

/* Though this format is simple enough, its only written spec is the quite
 * unhelpful Xcursor(3) man page, so some reading of the libXcursor code is
 * required.
   https://gitlab.freedesktop.org/xorg/lib/libxcursor

 * Some general points:
 *  · The format is little endian
 *  · All fields are unsigned
 *  · libXcursor has limits for the image size, comment length, and TOC length.
 *    Only the image limit is enshrined on the man page, but we've copied all
 *    of them here for a lack of better options.
*/

// Limits are inclusive
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

size_t xcursor_get_chunk_data(struct xcursor_desc *desc,
struct xcursor_chunk *chunk, void *restrict dst) {
	fseek(desc->ifp, chunk->pos, SEEK_SET);
	return fread(dst, 1, chunk->len, desc->ifp);
}

enum lib_fail xcursor_get_chunk(struct xcursor_desc *desc,
struct xcursor_chunk *chunk, const uint32_t i) {
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
	struct xcursor_toc *entry = desc->toc + i;
	uint32_t buf[9];

	size_t elems;
	switch (entry->type) {
	case xcursor_chunk_comment: elems = 5; break;
	case xcursor_chunk_image: elems = 9; break;
	default: return lib_invalid_header;
	}
	const size_t size = elems * sizeof(*buf);

	fseek(desc->ifp, entry->pos, SEEK_SET);
	if (!fread(buf, size, 1, desc->ifp)) {
		return lib_unexpected_eof;
	}

	const uint32_t header_size = endian32(buf[0], little_endian);
	chunk->type = type_to_enum(endian32(buf[1], little_endian));
	const uint32_t subtype = endian32(buf[2], little_endian);
	const uint32_t version = endian32(buf[3], little_endian);

	if (header_size != size || chunk->type != entry->type
	|| subtype != entry->subtype || version != 1) {
		return lib_invalid_header;
	}

	chunk->pos = ftell(desc->ifp);
	switch (chunk->type) {
	case xcursor_chunk_comment:
		switch (subtype) {
		case xcursor_comment_copyright:
		case xcursor_comment_license:
		case xcursor_comment_other:
			break;
		default:
			return lib_invalid_header;
		}

		chunk->u.comment = (struct xcursor_comment) {
			.type = subtype,
		};
		chunk->len = endian32(buf[4], little_endian);
		if (chunk->len > XCURSOR_STR_LIMIT) {
			return lib_invalid_header;
		}
		break;
	case xcursor_chunk_image:
		;struct xcursor_image *image = &chunk->u.image;
		*image = (struct xcursor_image) {
			.r = {
				.w = endian32(buf[4], little_endian),
				.h = endian32(buf[5], little_endian),
				.ch = 4,
				.bitdepth = 8,
				.layout = pix_bgra,
			},
			.xhot = endian32(buf[6], little_endian),
			.yhot = endian32(buf[7], little_endian),
			.delay = endian32(buf[8], little_endian),
		};
		const size_t max = zumax(image->r.w, image->r.h);
		if (!max || max > XCURSOR_DIM_LIMIT) {// || max != subtype) {
			return lib_invalid_header;
		}
		raster_normalize(&image->r);
		chunk->len = raster_size(&image->r);
		break;
	}
	return lib_ok;
}

static enum lib_fail load_toc(struct xcursor_desc *desc) {
	/* TOC structure:
		Offset  Size    Name
		0       DWORD   ChunkType
		4       DWORD   SubType
		8       DWORD   Location  // Absolute position of chunk in file
		12
	*/

	errno = 0;
	const size_t size = sizeof(*desc->toc) * desc->ntoc;
	desc->toc = malloc(size);
	if (!desc->toc) {
		return lib_alloc_error;
	}

	if (!fread(desc->toc, size, 1, desc->ifp)) {
		return lib_unexpected_eof;
	}

	for (uint32_t i = 0; i < desc->ntoc; ++i) {
		struct xcursor_toc *entry = desc->toc + i;
		loop_endian32((uint32_t *)entry, little_endian,
			3);
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
	return lib_ok;
}

enum lib_fail xcursor_parse_header(struct xcursor_desc *desc,
uint32_t max_entries) {
	/* File header (after magic bytes)
		Offset  Size    Name
		0       DWORD   HeaderBytes     // 16
		4       DWORD   FileVersion     // 0x00010000 (means 1.0)
		8       DWORD   NrOfEntries
		12              TableOfContents
	*/
	uint32_t header[3];
	if (!fread(header, sizeof(header), 1, desc->ifp)) {
		return lib_unexpected_eof;
	}

	if (endian32(header[0], little_endian) != 16) {
		return lib_invalid_header;
	}
	const uint32_t version = endian32(header[1], little_endian);
	if (version != 0x00010000) {
		return lib_invalid_header;
	}

	if (!max_entries) {
		max_entries = XCURSOR_TOC_LIMIT;
	}
	desc->ntoc = endian32(header[2], little_endian);
	if (desc->ntoc && desc->ntoc <= max_entries) {
		return load_toc(desc);
	}
	return lib_invalid_header;
}

enum lib_fail xcursor_open_file(struct xcursor_desc *desc, FILE *ifp) {
	const uint8_t sig[] = {'X', 'c', 'u', 'r'};
	const enum lib_fail st = lib_sigcmp(sig, sizeof(sig), ifp);
	if (st == lib_ok) {
		*desc = (struct xcursor_desc) {
			.ifp = ifp,
		};
	}
	return st;
}
