// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2020 kaleido
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "misc/decomp.h"
#include "misc/endian.h"
#include "misc/file.h"
#include "misc/math.h"
#include "raster/fmt.h"
#include "mac.h"

static const size_t MAC_PATTERNS_BYTES = 8*38;

time_t mac_time_to_unix(const mac_time_t time) {
	return (time_t)time - 2082844800;
}

struct wu_st mac_decode(const struct mac_desc *desc, struct wuimg *img,
const bool patterns) {
	if (patterns) {
		memcpy(img->data, desc->pat.ptr, desc->pat.len);
		return WU_OK;
	}
	const size_t dst_len = wuimg_size(img);
	const size_t written = decomp_packbits(img->data, dst_len,
		(int8_t *)desc->rle.ptr, desc->rle.len);
	return wuerr_partial(written, dst_len);
}

void mac_get_dims(struct wuimg *img, const bool patterns) {
	img->w = patterns ? 8 : 576;
	img->h = patterns ? MAC_PATTERNS_BYTES : 720;
	img->channels = 1;
	img->bitdepth = 1;
	img->cs.invert = true;
}

static struct wu_st read_mac_header(struct mac_desc *desc, struct mparser *mp) {
	/* MacPaint header:
		0       DWORD   Version      // 0, 2, 3, rarely 1 I'm told
		4       QWORD   Patterns[38] // Used when Version > 0
		308     BYTE    Pad[204]
		512
	*/

	const uint8_t *header = mp_slice(mp, 512);
	if (!header) {
		return WUERR_HERE(wu_unexpected_eof);
	}
	const uint32_t version = buf_endian32b(header);
	if (version > 3) {
		return wuerr(wu_invalid_header, "MacPaint version > 3");
	}
	desc->version = (uint8_t)version;
	desc->has_patterns = (bool)version;
	if (desc->has_patterns) {
		desc->pat = wuptr_mem(header + 4, MAC_PATTERNS_BYTES);
	}
	desc->rle = mp_remaining(mp);
	return desc->rle.len ? WU_OK : WUERR_HERE(wu_unexpected_eof);
}

static void read_macbin_header(const unsigned char data[static 128],
struct mac_binary_header *macbin) {
	macbin->name_len = data[1];
	memcpy(macbin->name, data + 2, macbin->name_len);
	memcpy(macbin->type, data + 65, sizeof(macbin->type));
	memcpy(macbin->creator, data + 69, sizeof(macbin->creator));
	macbin->attributes = data[73];
	macbin->window.y = buf_endian16b(data + 75);
	macbin->window.x = buf_endian16b(data + 77);
	macbin->window.id = buf_endian16b(data + 77);
	macbin->protection = data[81];
	macbin->time.created = buf_endian32b(data + 91);
	macbin->time.modified = buf_endian32b(data + 95);
}

struct wu_st mac_open_file(struct mac_desc *desc, const struct wuptr mem) {
	/* The thing with identifying MacPaint files is that they come in two
	 * equivalent varieties, that are each harder to identify than the
	 * other. One is the file as created by the MacPaint software, whose
	 * sole identifying feature is that it starts with a DWORD version
	 * number ranging from 0 to 3, with a value of 0 indicating that the
	 * next 304 bytes are unimportant, and any other value indicating that
	 * they contain pattern data. Following this are 204 bytes of padding,
	 * this always being unimportant. In other words you have to know it is
	 * a MacPaint file to know it is a MacPaint file. The second variety is
	 * this very same file but wrapped in the MacBinary format.

	 * The MacBinary format consists of a header 128-bytes in length
	 * prepended to a classic MacOS file (which in turn is made up of a
	 * data and a resource fork) containing its metadata to make it
	 * transferable to other platforms and back. The format, however, may
	 * come as version I or II, both of them identifying themselves in the
	 * stream as version 0. Most fields except for two randomly located
	 * reserved bytes may contain any value. In theory, the whole 128-bytes
	 * header can be zero and still be valid, just like the 512-bytes
	 * MacPaint header.
	 * The only other possible clue in this situation would be that
	 * MacPaint files supposedly contain no data in the resource fork,
	 * meaning the ResourceForkLen field should always be zero, but since I
	 * don't know anything about the workings of MacOS or MacPaint and it's
	 * technologically infeasible to test all their possible states, I
	 * can't verify this advice, making it as useless as
	 * the rest of the spec.

	 * In conclusion: Even if the first 640 bytes are all zero, it might be
	 * a valid MacBinary + MacPaint file. */

	/* MacBinary header:
		Offset  Size    Name
		0       BYTE    Version         // Always 0
		1       BYTE    FileNameLen     // Size of file name (0 to 63)
		2       BYTE    FileName[63]    // File name
		65      DWORD   FileType        // Type of Macintosh file
		69      DWORD   FileCreator     // ID of the creator program
		73      BYTE    FileFlags       // File attribute flags
		74      BYTE    Reserved1
		75      WORD    FileVertPos     // Vertical pos in window
		77      WORD    FileHorzPos     // Horizontal pos in window
		79      WORD    WindowId        // Window or folder ID
		81      BYTE    Protected       // File protection (1 = protected)
		82      BYTE    Reserved2
		83      DWORD   DataForkLen     // Size of data fork in bytes
		87      DWORD   ResourceForkLen // Size of resource fork
		91      DWORD   CreationStamp   // Seconds since 1904-01-01
		95      DWORD   ModificationStamp
		99      WORD    GetInfoLength   // GetInfo message length

	MacBinary II extra fields:
		101     BYTE    FinderFlags
		102     BYTE    Reserved3[14]
		116     DWORD   UnpackedLen
		120     WORD    SecondHeadLen
		122     BYTE    UploadVersion
		123     BYTE    ReadVersion
		124     WORD    CRCValue
		126     BYTE    Reserved4[2]
		128
	*/

	struct mparser mp = mp_wuptr(mem);
	const size_t MACBIN_HEADER_SIZE = 128;
	const uint8_t *header = mp_slice(&mp, MACBIN_HEADER_SIZE);
	if (!header) {
		return WUERR_HERE(wu_unexpected_eof);
	}

	const uint32_t data_fork_size = buf_endian32b(header + 83);
	const uint32_t res_fork_size = buf_endian32b(header + 87);
	const uint32_t max_fork_size = 0x007fffff;
	desc->has_macbin_header = header[0] == 0
		&& header[1] != 0 && header[1] < 64
		&& memchr(header + 2, 0, header[1]) == NULL
		&& header[74] == 0
		&& header[82] == 0
		&& (data_fork_size || res_fork_size)
		&& data_fork_size <= max_fork_size
		&& res_fork_size <= max_fork_size;

	size_t offset = 0;
	if (desc->has_macbin_header) {
		read_macbin_header(header, &desc->macbin);
		offset = MACBIN_HEADER_SIZE;
	}
	mp.pos = offset;
	return read_mac_header(desc, &mp);
}
