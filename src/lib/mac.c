// SPDX-License-Identifier: 0BSD
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "misc/decomp.h"
#include "misc/endian.h"
#include "misc/file.h"
#include "misc/math.h"
#include "raster/fmt.h"
#include "mac.h"

time_t mac_time_to_unix(const mac_time_t time) {
	return (time_t)time - 2082844800;
}

static size_t get_rle_len(const struct mac_desc *desc, const size_t dims) {
	const long start = 512 + 128 * desc->has_macbin_header;
	fseek(desc->ifp, start, SEEK_SET);
	const size_t size = file_remaining(desc->ifp);
	// E.g. 0x00 0x?? 0x00 0x?? ...
	return zumin(size, dims*2);
}

size_t mac_decode(const struct mac_desc *desc, struct wuimg *main) {
	size_t written = 0;
	const size_t dst_len = wuimg_size(main);
	const size_t rle_len = get_rle_len(desc, dst_len);
	if (rle_len && wuimg_alloc_noverify(main)) {
		int8_t *rle = malloc(rle_len);
		if (rle) {
			written = decomp_packbits(main->data, dst_len, rle,
				fread(rle, 1, rle_len, desc->ifp));
			free(rle);
		}
	}
	return written;
}

size_t mac_patterns_load(const struct mac_desc *desc, struct wuimg *pats) {
	if (wuimg_alloc_noverify(pats)) {
		fseek(desc->ifp, 4U + 128 * desc->has_macbin_header, SEEK_SET);
		return fmt_load_raster(pats, desc->ifp);
	}
	return 0;
}

enum wu_error mac_get_sizes(struct wuimg *main, struct wuimg *pats) {
	main->w = 576;
	main->h = 720;
	main->channels = 1;
	main->bitdepth = 1;
	main->attr = pix_inverted;
	enum wu_error st = wuimg_verify(main);
	if (st == wu_ok && pats) {
		pats->w = 8;
		pats->h = 8*38;
		pats->channels = 1;
		pats->bitdepth = 1;
		pats->attr = pix_inverted;
		st = wuimg_verify(pats);
	}
	return st;
}

static enum wu_error read_mac_header(unsigned char header[static 4],
struct mac_desc *desc) {
	/* MacPaint header:
		0       DWORD   Version      // 0, 2, 3, rarely 1 I'm told
		4       QWORD   Patterns[38] // Used by MacPaint for Version > 0
		308     BYTE    Pad[204]
		512
	*/

	const uint32_t version = buf_endian32(header, big_endian);
	if (version > 3) {
		return wu_invalid_header;
	}
	desc->version = (uint8_t)version;
	desc->has_patterns = (bool)version;
	return wu_ok;
}

static void read_macbin_header(const unsigned char data[static 128],
struct mac_binary_header *macbin) {
	macbin->name_len = data[1];
	memcpy(macbin->name, data + 2, macbin->name_len);
	memcpy(macbin->type, data + 65, sizeof(macbin->type));
	memcpy(macbin->creator, data + 69, sizeof(macbin->creator));
	macbin->attributes = data[73];
	macbin->window.y = buf_endian16(data + 75, big_endian);
	macbin->window.x = buf_endian16(data + 77, big_endian);
	macbin->window.id = buf_endian16(data + 77, big_endian);
	macbin->protection = data[81];
	macbin->time.created = buf_endian32(data + 91, big_endian);
	macbin->time.modified = buf_endian32(data + 95, big_endian);
}

enum wu_error mac_open_file(struct mac_desc *desc, FILE *ifp) {
	/* The thing with identifying MacPaint files is that they come in two
	 * equivalent varieties, that are each harder to identify than the
	 * other. One is the file as created by the MacPaint software, whose
	 * sole identifying feature is that it starts with a DWORD version
	 * number ranging from 0 to 3, with a value of 0 indicating that the
	 * next 304 bytes are unimportant, and any other value indicating that
	 * they contain pattern data, also unimportant for decoding. Following
	 * this are 204 bytes of padding, this always being unimportant. In
	 * other words you have to know it is a MacPaint file to know it is a
	 * MacPaint file. The second is this very same file but wrapped in the
	 * MacBinary format.

	 * The MacBinary format consists of a header 128-bytes in length
	 * prepended to a classic MacOS "file" (actually two files, only
	 * they're called "forks" meaning the names are all backwards)
	 * containing its metadata to make it transferable to other platforms
	 * and back. The format, however, may come as version I or II, both of
	 * them identifying themselves in the stream as version 0. Most fields
	 * except for three randomly located reserved bytes may contain any
	 * value. In theory, the whole 128-bytes header can be zero and still
	 * be valid, same as the 512-bytes MacPaint header. Our only other clue
	 * in this situation would be that MacPaint files supposedly contain no
	 * data in the resource fork, making the ResourceForkLen field always
	 * zero, but since I don't know anything about the workings of MacOS or
	 * MacPaint and it's technologically infeasible to test all their
	 * possible states, I can't verify this advice, making it as useless as
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

	desc->ifp = ifp;
	unsigned char header[128 + 4];
	if (!fread(header, sizeof(header), 1, desc->ifp)) {
		return wu_unexpected_eof;
	}

	const uint32_t data_fork_size = buf_endian32(header + 83, big_endian);
	const uint32_t res_fork_size = buf_endian32(header + 87, big_endian);
	const uint32_t max_fork_size = 0x007fffff;
	desc->has_macbin_header = header[0] == 0
		&& header[1] != 0 && header[1] < 64
		&& memchr(header + 2, 0, header[1]) == NULL
		&& header[74] == 0
		&& header[82] == 0
		&& (data_fork_size || res_fork_size)
		&& data_fork_size <= max_fork_size
		&& res_fork_size <= max_fork_size;

	long offset = 0;
	if (desc->has_macbin_header) {
		read_macbin_header(header, &desc->macbin);
		offset = 128;
	}
	return read_mac_header(header + offset, desc);
}
