#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../common.h"
#include "../raster/lib.h"
#include "mac.h"

static size_t rle_decode(uint8_t *restrict out, const size_t dims,
const signed char *restrict rle, const size_t rle_len) {
	size_t p = 0;
	size_t i = 0;
	while (i < rle_len - 1 && p < dims) {
		const size_t left = dims - p;
		size_t cnt;
		if (rle[i] < 0) {
			cnt = zumin(left, (size_t)(1 - rle[i]));
			++i;
			memset(out + p, (unsigned char)rle[i], cnt);
			++i;
		} else {
			cnt = zumin(rle_len - i, zumin(left, 1 + (size_t)rle[i]));
			++i;
			memcpy(out + p, rle + i, cnt);
			i += cnt;
		}
		p += cnt;
	}
	return p;
}

static size_t get_rle_len(const struct mac_desc *desc,
const size_t pathological_rle) {
	const long start = 512 + 128 * desc->has_macbin_header;
	fseek(desc->ifp, 0, SEEK_END);
	const long end = ftell(desc->ifp);
	fseek(desc->ifp, start, SEEK_SET);

	const size_t diff = (size_t)(end - start);
	// E.g. 0x00 0x?? 0x00 0x?? ...
	return zumin(diff, pathological_rle);
}

unsigned char * mac_decode(const struct mac_desc *desc) {
	const size_t dims = desc->rast.w * desc->rast.h / 8;
	const size_t rle_len = get_rle_len(desc, dims * 2);
	if (!rle_len) {
		return NULL;
	}

	unsigned char *out = malloc(dims);
	if (!out) {
		return NULL;
	}

	signed char *rle = malloc(rle_len);
	if (!rle) {
		free(out);
		return NULL;
	}

	const size_t read = fread(rle, 1, rle_len, desc->ifp);
	if (!read) {
		free(out);
		free(rle);
		return NULL;
	}

	const size_t written = rle_decode(out, dims, rle, read);
	free(rle);
	if (written < dims) {
		puts(RASTER_EOF);
	}
	return out;
}

unsigned char * mac_pattern_unpack(const struct mac_desc *desc) {
	fseek(desc->ifp, 4U + 128 * desc->has_macbin_header, SEEK_SET);
	return lib_load_rast(desc->ifp, &desc->patterns);
}

static enum lib_fail read_mac_header(unsigned char *header,
struct mac_desc *desc) {
	/* MacPaint header:
		0       DWORD   Version         // 0, 2, 3, possibly 1 too I'm told
		4       QWORD   Patterns[38]    // Used by MacPaint
		308     BYTE    Pad[204]
		512
	*/

	desc->version = buf_endian32(header, big_endian);
	if (desc->version > 3) {
		return lib_invalid_header;
	}

	const long offset = 512 + 128 * desc->has_macbin_header;
	fseek(desc->ifp, offset, SEEK_SET);
	if (ftell(desc->ifp) != offset) {
		return lib_unexpected_eof;
	}

	desc->patterns = (struct raster_desc) {
		.w = 8,
		.h = 8 * 38,
		.ch = 1,
		.bitdepth = 1,
	};
	raster_normalize(&desc->patterns);
	desc->rast = (struct raster_desc) {
		.w = 576,
		.h = 720,
		.ch = 1,
		.bitdepth = 1,
	};
	raster_normalize(&desc->rast);
	return lib_ok;
}

static void read_macbin_header(const unsigned char *restrict data,
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

enum lib_fail mac_open_file(struct mac_desc *desc, FILE *ifp) {
	/* The thing with identifying MacPaint files is that they come in two
	 * equivalent varieties, that are each harder to identify than the
	 * other. One is the file as created by the MacPaint software, whose
	 * sole identifying feature is that it starts with a DWORD version
	 * number that is either 0 or 2, with a value of 0 indicating that the
	 * next 304 bytes are unimportant, and a value of 2 indicating that
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

	 * In conclusion: Even if the first 640 bytes are all zero, it counts
	 * as a valid MacBinary + MacPaint file, and we should accept it. */

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

	unsigned char header[128 + 4];
	if (fread(header, 1, sizeof(header), ifp) != sizeof(header)) {
		return lib_unexpected_eof;
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
	desc->ifp = ifp;
	return read_mac_header(header + offset, desc);
}
