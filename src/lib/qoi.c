#include "qoi.h"
#include "raster/fmt.h"
#include "raster/mem.h"

enum qoi_ops {
	qoi_op_index = 0,
	qoi_op_diff = 1,
	qoi_op_luma = 2,
	qoi_op_run = 3,

	qoi_op_rgb = 62,
	qoi_op_rgba = 63,
};

static uint8_t hash_pxl(const struct pix_rgba8 p) {
	return (p.r*3 + p.g*5 + p.b*7 + p.a*11) & 0x3f;
}

static void witness_pxl(struct pix_rgba8 seen[static 64],
const struct pix_rgba8 cur) {
	memcpy(seen + hash_pxl(cur), &cur, sizeof(cur));
}

size_t qoi_decode(struct mp_parser *mp, struct raw_img *img) {
	const size_t dst_len = raw_img_size(img);
	// Add 1 byte of padding so we can use a faster 4-byte memcpy
	img->data = malloc(dst_len + (bool)(img->channels == 3));
	if (!img->data) {
		return 0;
	}

	const struct wuptr src = mp_next_remaining(mp, SIZE_MAX);
	struct pix_rgba8 seen[64] = {0};
	size_t s = 0;
	size_t d = 0;
	struct pix_rgba8 cur = {0, 0, 0, 255};
	while (d < dst_len && s < src.len) {
		const uint8_t c = src.ptr[s];
		++s;
		const uint8_t arg = c & 0x3f;
		switch (c >> 6) {
		case qoi_op_index:
			memcpy(&cur, seen + arg, sizeof(cur));
			break;
		case qoi_op_diff:
			cur.r += (uint8_t)((arg >> 4) - 2);
			cur.g += (uint8_t)(((arg >> 2) & 0x03) - 2);
			cur.b += (uint8_t)((arg & 0x03) - 2);
			witness_pxl(seen, cur);
			break;
		case qoi_op_luma:
			if (s >= src.len) {
				return d;
			}
			const uint8_t rb = src.ptr[s];
			++s;
			const int g = arg - 32 - 8;
			cur.r += (uint8_t)(g + (rb >> 4));
			cur.g += (uint8_t)(g + 8);
			cur.b += (uint8_t)(g + (rb & 0x0f));
			witness_pxl(seen, cur);
			break;
		case qoi_op_run:
			;uint8_t ch;
			switch (arg) {
			case qoi_op_rgb:
				ch = 3;
				if (s + ch > src.len) {
					return d;
				}
				memcpy(&cur, src.ptr + s, ch);
				s += ch;
				break;
			case qoi_op_rgba:
				ch = 4;
				if (s + ch > src.len) {
					return d;
				}
				memcpy(&cur, src.ptr + s, ch);
				s += ch;
				break;
			default:
				;const size_t run = arg + 1;
				if (d + run*img->channels > dst_len) {
					return d;
				}
				memwordset(img->data + d, &cur, img->channels, run);
				d += run * img->channels;
				continue;
			}
			witness_pxl(seen, cur);
			break;
		}
		memcpy(img->data + d, &cur, 4);
		d += img->channels;
	}
	return d;
}

enum wu_error qoi_parse(struct mp_parser *mp, struct raw_img *img) {
	/* QOI header (after magic bytes):
		Offset  Size    Name
		0       u32     Width
		4       u32     Height
		8       u8      Channels
		9       u8      IsLinearRGB
		10
	*/
	const uint8_t *header = mp_next_slice(mp, 10);
	if (!header) {
		return wu_unexpected_eof;
	}

	img->w = buf_endian32(header, big_endian);
	img->h = buf_endian32(header + 4, big_endian);
	img->channels = header[8];
	img->bitdepth = 8;
	if (header[9]) {
		img->cs.transfer = cicp_transfer_linear;
	}
	switch (img->channels) {
	case 3: case 4: return raw_img_verify(img);
	}
	return wu_invalid_header;
}

enum wu_error qoi_open(struct mp_parser *mp) {
	const uint8_t magic[4] = "qoif";
	return fmt_sigcmp_mem(magic, sizeof(magic), mp);
}
