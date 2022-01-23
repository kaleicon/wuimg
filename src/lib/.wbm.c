#include <stdio.h>

static lib_fail parse_header(struct wbm_desc *desc) {
	/* WPX BMP header (after signature):
		Offset  Size    Name
		4       BYTE    ???; // Always 1
		5       BYTE    ???;
		6       BYTE    DirCount;
		7       BYTE    DirSize;
	*/

	uint8_t buf[8];
	if (fread(buf, 1, sizeof(buf), desc->ifp) != sizeof(buf)) {
		return lib_unexpected_eof;
	}

	const uint8_t con = buf[4];
	const uint8_t dir_count = buf[6];
	const uint8_t dir_size = buf[7];
	if (con != 1 || !dir_count || dir_size < 0x10) {
		return lib_invalid_header;
	}

	const size_t dir_len = dir_count * dir_size;
	uint8_t *dir = malloc(dir_len);
	if (!dir) {
		return lib_alloc_error;
	}

	if (fread(dir, 1, dir_len, desc->ifp) != dir_len)) {
		free(dir);
		return lib_unexpected_eof;
	}


enum lib_fail wbm_read_header(FILE *ifp, struct wbm_desc *desc) {
	const uint8_t sig[] = {'W', 'P', 'X', 0x1a, 'B', 'M', 'P', 0};
	uint8_t buf[sizeof(sig)];

	if (fread(buf, 1, sizeof(buf), ifp) == sizeof(buf)) {
		if (!memcmp(sig, buf, sizeof(buf)) {
			desc->ifp = ifp;
			return parse_header(desc);
		}
		return lib_invalid_signature;
	}
	return lib_unexpected_eof;
}
