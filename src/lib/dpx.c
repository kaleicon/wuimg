// SPDX-License-Identifier: 0BSD
#include "misc/time.h"
#include "dpx.h"

/* This format is a great example of how to do file headers.
 * Too bad the raster storage ruined it. */

struct elem_info {
	uint8_t ch;
	enum pix_layout layout:8;
	bool ycbcr;
	bool subsampled;
};

static enum cicp_matrix guess_matrix(const enum dpx_primaries pri) {
	switch (pri) {
	case dpx_primaries_user:
	case dpx_primaries_printing_density:
		break;
	case dpx_primaries_unspecified:
		return cicp_matrix_unspecified;
	case dpx_primaries_smpte_240m:
		return cicp_matrix_smpte_st_240;
	case dpx_primaries_ccir_709_1:
		return cicp_matrix_bt709_6;
	case dpx_primaries_ccir_601_2_system_b_g:
	case dpx_primaries_ccir_601_2_system_m:
		return cicp_matrix_bt601_7;
	case dpx_primaries_ntsc:
		return cicp_matrix_bt601_7;
	case dpx_primaries_pal:
		return cicp_matrix_bt470_6_system_b_g;
	}
	return cicp_matrix_chroma_derived_nonconstant; // Best effort
}

static enum cicp_transfer dpx_to_cicp_transfer(const enum dpx_oetf oetf) {
	switch (oetf) {
	case dpx_oetf_user:
	case dpx_oetf_printing_density:
	case dpx_oetf_logarithmic:
	case dpx_oetf_z_linear:
	case dpx_oetf_z_homogeneous:
		break;
	case dpx_oetf_unspecified:
		return cicp_transfer_unspecified;
	case dpx_oetf_linear:
		return cicp_transfer_linear;
	case dpx_oetf_smpte_240m:
		return cicp_transfer_smpte_st_240;
	case dpx_oetf_ccir_709_1:
		return cicp_transfer_bt709_6;
	case dpx_oetf_ccir_601_2_system_b_g:
	case dpx_oetf_ccir_601_2_system_m:
		// Guessing
		return cicp_transfer_bt601_7;
	case dpx_oetf_ntsc:
		return cicp_transfer_bt470_6_system_m;
	case dpx_oetf_pal:
		return cicp_transfer_bt470_6_system_b_g;
	}
	return 0;
}

static enum cicp_primaries dpx_to_cicp_primaries(const enum dpx_primaries pri) {
	switch (pri) {
	case dpx_primaries_user:
	case dpx_primaries_printing_density:
	case dpx_primaries_unspecified:
		break;
	case dpx_primaries_smpte_240m:
		return cicp_primaries_smpte_st_240;
	case dpx_primaries_ccir_709_1:
		return cicp_primaries_bt709_6;
	case dpx_primaries_ccir_601_2_system_m:
	case dpx_primaries_ccir_601_2_system_b_g:
		// Guessing
		return cicp_primaries_bt601_7;
	case dpx_primaries_ntsc:
		return cicp_primaries_bt470_6_system_m;
	case dpx_primaries_pal:
		return cicp_primaries_bt470_6_system_b_g;
	}
	return 0;
}

static bool element_info(const enum dpx_element_type type,
struct elem_info *n) {
	switch (type) {
	case dpx_element_user1:
	case dpx_element_user2:
	case dpx_element_user3:
	case dpx_element_user4:
	case dpx_element_user5:
	case dpx_element_user6:
	case dpx_element_user7:
	case dpx_element_user8:
	case dpx_element_composite_video:
		break;
	case dpx_element_r:
		*n = (struct elem_info) {
			.ch = 1,
			.layout = pix_rgba,
		};
		return true;
	case dpx_element_g:
		*n = (struct elem_info) {
			.ch = 1,
			.layout = pix_gbra,
		};
		return true;
	case dpx_element_b:
		*n = (struct elem_info) {
			.ch = 1,
			.layout = pix_bgra,
		};
		return true;
	case dpx_element_a:
	case dpx_element_y:
	case dpx_element_depth:
		*n = (struct elem_info) {
			.ch = 1,
			.layout = pix_gray,
		};
		return true;
	case dpx_element_chroma:
		*n = (struct elem_info) {
			.ch = 2,
			.layout = pix_gbra,
			.ycbcr = true,
		};
		return true;
	case dpx_element_rgb:
	case dpx_element_rgba:
	case dpx_element_abgr:
		*n = (struct elem_info) {
			.ch = (type == dpx_element_rgb) ? 3 : 4,
			.layout = (type == dpx_element_abgr)
				? pix_abgr : pix_rgba,
		};
		return true;
	case dpx_element_uyvy:
	case dpx_element_uyv:
		*n = (struct elem_info) {
			.ch = 3,
			.layout = pix_grba,
			.ycbcr = true,
			.subsampled = (type == dpx_element_uyvy),
		};
		return true;
	case dpx_element_uyavya:
	case dpx_element_uyva:
		*n = (struct elem_info) {
			.ch = 4,
			.layout = pix_grba,
			.ycbcr = true,
			.subsampled = (type == dpx_element_uyavya),
		};
		return true;
	}
	return false;
}

static void unpack10(uint16_t *dst, const uint32_t w, const size_t len,
const uint8_t shr, const uint16_t mask, const uint32_t scale) {
	for (uint8_t z = 0; z < len; ++z) {
		const uint32_t val = mask & (w >> (shr + (20 - 10*z)));
		dst[z] = (uint16_t)((val * scale) >> 16);
	}
}

static size_t process10(const struct dpx_desc *desc, struct wuimg *img,
const struct dpx_element *elem) {
	const size_t items = (img->w * img->channels);
	const size_t whole = items / 3;
	const size_t remain = items % 3;

	const size_t len = whole + (bool)remain;
	const size_t instride = len * 4 + elem->line_pad;
	const size_t insize = instride * img->h;
	uint8_t *buf = malloc(insize);
	if (!buf) {
		return 0;
	}

	const size_t read = fread(buf, 1, insize, desc->ifp);

	/* Three 10-bit components are grouped in 30-bits of a 32-bit word.
	 * Earlier components appear in lower bits.
	 * Method A: Group stored in the 30 most-significant bits
	 * Method B: Group stored in the 30 least-significant bits
	*/
	const uint16_t mask = (1 << 10) - 1;
	const uint32_t scale = (0xffffu << 16) / mask + 1;
	const uint8_t shr = (elem->pack == dpx_pack_a) ? 2 : 0;
	uint16_t *dst = (uint16_t *)img->data;
	for (size_t y = 0; y < img->h; ++y) {
		const uint32_t *src = (uint32_t *)(buf + y*instride);
		for (size_t x = 0; x < whole; ++x) {
			unpack10(dst, endian32(src[x], desc->endian), 3, shr,
				mask, scale);
			dst += 3;
		}
		if (remain) {
			unpack10(dst, endian32(src[whole], desc->endian),
				remain, shr, mask, scale);
			dst += remain;
		}
	}
	free(buf);
	return read;
}

static size_t process16(const struct dpx_desc *desc, uint16_t *data,
const struct dpx_element *elem, const size_t read) {
	switch (elem->pack) {
	case dpx_pack_normal:
		endian_loop16(data, desc->endian, read);
		break;
	case dpx_pack_a:
		// Data is in the 12 most-significant bits
		for (size_t i = 0; i < read; ++i) {
			const uint16_t w = endian16(data[i], desc->endian);
			data[i] = w | w >> 12;
		}
		break;
	case dpx_pack_b:
		// Data is in the 12 least-significant bits
		for (size_t i = 0; i < read; ++i) {
			const uint16_t w = endian16(data[i], desc->endian);
			data[i] = (uint16_t)(w << 4 | w >> 8);
		}
	}
	return read;
}

static size_t process_rows(const struct dpx_desc *desc, struct wuimg *img,
const struct dpx_element *elem) {
	const size_t stride = wuimg_stride(img);
	size_t total = 0;
	for (size_t y = 0; y < img->h; ++y) {
		uint8_t *dst = img->data + y*stride;
		const size_t read = fread(dst, 1, stride, desc->ifp);
		switch (img->bitdepth) {
		case 16:
			process16(desc, (uint16_t *)dst, elem, read/2);
			break;
		case 32:
			endian_loop32((uint32_t *)dst, desc->endian, read/4);
			break;
		case 64:
			endian_loop64((uint64_t *)dst, desc->endian, read/8);
			break;
		}
		total += read;
		if (read < stride) {
			break;
		}
		fseek(desc->ifp, elem->line_pad, SEEK_CUR);
	}
	return total;
}

size_t dpx_decode(const struct dpx_desc *desc, struct wuimg *img,
const uint8_t i) {
	if (!wuimg_alloc_noverify(img)) {
		return 0;
	}
	const struct dpx_element *elem = desc->generic.image.elem + i;
	fseek(desc->ifp, elem->offset, SEEK_SET);

	if (desc->generic.image.bitdepth == 10 && elem->pack != dpx_pack_normal) {
		return process10(desc, img, elem);
	}
	return process_rows(desc, img, elem);
}

enum wu_error dpx_set_image(const struct dpx_desc *desc, struct wuimg *img,
const uint8_t i) {
	const struct dpx_generic_image *image = &desc->generic.image;
	img->w = image->w;
	img->h = image->h;
	const uint8_t o = image->orientation;
	if (o <= 0x07) {
		img->rotate = (o << 1) & 2;
		img->rotate |= (o >> 2) & 1;
		img->mirror = (o & 1);
		img->mirror ^= (o >> 1) & 1;
	}

	const struct dpx_element *elem = image->elem + i;
	if (elem->rle) {
		return wu_unsupported_feature;
	}
	struct elem_info nfo;
	if (!element_info(elem->type, &nfo) || nfo.subsampled) {
		return wu_unsupported_feature;
	}
	img->attr = elem->attr;
	img->channels = nfo.ch;
	img->layout = nfo.layout;
	img->bitdepth = image->bitdepth;
	wuimg_align(img, 4);
	switch (img->bitdepth) {
	case 10:
		wuimg_align(img, 2);
		// fallthrough
	case 12:
		if (elem->pack != dpx_pack_normal) {
			img->bitdepth = 16;
		}
	}
	img->cs.transfer = dpx_to_cicp_transfer(image->oetf);
	img->cs.primaries = dpx_to_cicp_primaries(image->primaries);
	if (nfo.ycbcr) {
		img->cs.matrix = guess_matrix(image->primaries);
	}

	const struct dpx_generic_source *src = &desc->generic.src;
	wuimg_aspect_ratio(img, src->horz_aspect, src->vert_aspect);
	return wuimg_verify(img);
}

static time_t read_date(FILE *in) {
	/* Format:  yyyy:mm:dd:hh:mm:ssTIMEZ
	 *          0---------1---------2---- (24 bytes)
	 * Where TIMEZ is either "Z" or [+-]hh or [+-]hhmm
	 */
	char buf[24];
	if (!fread(buf, sizeof(buf), 1, in)) {
		return 0;
	}

	int year, month, day, hour, min, sec, hz, mz;
	const int m = sscanf(buf, "%4d:%2d:%2d:%2d:%2d:%2d%3d%2d",
		&year, &month, &day, &hour, &min, &sec, &hz, &mz);
	switch (m) {
	case 8:
		min += (hz >= 0) ? mz : -mz;
		// fallthrough
	case 7:
		hour += hz;
		break;
	case 6:
		;const size_t tz_pos = 24 - 5;
		const char utc[5] = "Z";
		if (memcmp(buf + tz_pos, utc, sizeof(utc))) {
			return 0;
		}
		break;
	default: return 0;
	}
	return utc_to_epoch(year, month, day, hour, min, sec);
}

static enum wu_error television_parse(struct dpx_desc *desc) {
	/* Television header (after prev):
		Offset  Size    Name
		0       u32     TimeCode
		4       u32     UserBits
		8       u8      Interlaced
		9       u8      FieldNumber
		10      u8      VideoSignal
		11      u8      _Padding
		12      f32     HorzSampleRate
		16      f32     VertSampleRate
		20      f32     FrameRateHz
		24      f32     TimeOffset
		28      f32     Gamma
		32      f32     BlackLevel
		36      f32     BlackGain
		40      f32     Breakpoint
		44      f32     WhiteLevel
		48      f32     IntegrationTimes
		52      f32     Reserved[76]
		128
	*/
	uint8_t buf[52];
	if (!fread(buf, sizeof(buf), 1, desc->ifp)) {
		return wu_unexpected_eof;
	}

	desc->industry.tv = (struct dpx_industry_television) {
		.time_code = buf_endian32(buf, desc->endian),
		.user_bits = buf_endian32(buf + 4, desc->endian),
		.interlaced = buf[8],
		.field = buf[9],
		.signal = buf[10],
		.horz_rate = buf_endianf32(buf + 12, desc->endian),
		.vert_rate = buf_endianf32(buf + 16, desc->endian),
		.frame_hz = buf_endianf32(buf + 20, desc->endian),
		.time_offset = buf_endianf32(buf + 24, desc->endian),
		.gamma = buf_endianf32(buf + 28, desc->endian),
		.black_level = buf_endianf32(buf + 32, desc->endian),
		.black_gain = buf_endianf32(buf + 36, desc->endian),
		.breakpoint = buf_endianf32(buf + 40, desc->endian),
		.white_level = buf_endianf32(buf + 44, desc->endian),
		.integration = buf_endianf32(buf + 48, desc->endian),
	};
	return wu_ok;
}

static enum wu_error film_parse(struct dpx_desc *desc) {
	/* Film header (after prev):
		Offset  Size    Name
		0       char    Manufacturer[2]
		2       char    Type[2]
		4       char    PerfOffset[2]
		6       char    PrefixCode[6]
		12      char    CountCode[4]
		16      char    Format[32]
		48      u32     FrameNum
		52      u32     TotalFrames
		56      u32     HeldCount
		60      f32     FrameRateSec
		64      f32     ShutterAngle
		68      char    FrameID[32]
		100     char    SlateInfo[100]
		200     char    Reserved[56]
		256
	*/
	char codes[16];
	if (!fread(codes, sizeof(codes), 1, desc->ifp)) {
		return wu_unexpected_eof;
	}

	struct dpx_industry_film *film = &desc->industry.film;
	const int m = sscanf(codes, "%2hhu%2hhu%2hhu%6u%4hu",
		&film->manufacturer, &film->type, &film->perf_offset,
		&film->prefix, &film->count);
	if (m != 5) {
		film->manufacturer = 0;
		film->type = 0;
		film->perf_offset = 0;
		film->prefix = 0;
		film->count = 0;
	}

	uint32_t buf[5];
	if (!fread(film->format, sizeof(film->format), 1, desc->ifp)
	|| !fread(buf, sizeof(buf), 1, desc->ifp)) {
		return wu_unexpected_eof;
	}
	film->frame_num = endian32(buf[0], desc->endian);
	film->total_frames = endian32(buf[1], desc->endian);
	film->held_count = endian32(buf[2], desc->endian);
	film->frame_rate = endianf32(buf[3], desc->endian);
	film->shutter_angle = endianf32(buf[4], desc->endian);
	if (!fread(film->frame_id, sizeof(film->frame_id), 1, desc->ifp)
	|| !fread(film->slate_info, sizeof(film->slate_info), 1, desc->ifp)) {
		return wu_unexpected_eof;
	}
	fseek(desc->ifp, 56, SEEK_CUR);
	return television_parse(desc);
}

static enum wu_error source_parse(struct dpx_desc *desc) {
	/* Source header (after prev):
		Offset  Size    Name
		0       u32     XOffset
		4       u32     YOffset
		8       f32     XCenter
		12      f32     YCenter
		16      u32     OrigWidth
		20      u32     OrigHeight
		24      char    SourceFilename[100]
		124     char    SourceDate[24]
		148     char    InputName[32]
		180     char    InputSerialNumber[32]
		212     u16     LeftErosion
		214     u16     RightErosion
		216     u16     TopErosion
		218     u16     BottomErosion
		220     u32     HorzAspectRatio
		224     u32     VertAspectRatio
		228     f32     XScanSize             // v2 only
		232     f32     YScanSize             // v2 only
		236     u8      Reserved[20]
		256
	*/
	uint32_t buf[6];
	if (!fread(buf, sizeof(buf), 1, desc->ifp)) {
		return wu_unexpected_eof;
	}

	struct dpx_generic_source *src = &desc->generic.src;
	src->x = endian32(buf[0], desc->endian);
	src->y = endian32(buf[1], desc->endian);
	src->x_center = endianf32(buf[2], desc->endian);
	src->y_center = endianf32(buf[3], desc->endian);
	src->w = endian32(buf[4], desc->endian);
	src->h = endian32(buf[5], desc->endian);
	if (!fread(src->filename, sizeof(src->filename), 1, desc->ifp)) {
		return wu_unexpected_eof;
	}
	src->date = read_date(desc->ifp);
	if (!fread(src->input_device, sizeof(src->input_device), 1, desc->ifp)
	|| !fread(src->input_sn, sizeof(src->input_sn), 1, desc->ifp)) {
		return wu_unexpected_eof;
	}

	uint16_t frame[12];
	if (!fread(frame, sizeof(frame), 1, desc->ifp)) {
		return wu_unexpected_eof;
	}
	src->erosion.left = endian16(frame[0], desc->endian);
	src->erosion.right = endian16(frame[1], desc->endian);
	src->erosion.top = endian16(frame[2], desc->endian);
	src->erosion.bottom = endian16(frame[3], desc->endian);
	src->horz_aspect = buf_endian32(frame + 4, desc->endian);
	src->vert_aspect = buf_endian32(frame + 6, desc->endian);
	if (desc->version == dpx_v2) {
		src->w_mm = buf_endianf32(frame + 8, desc->endian);
		src->h_mm = buf_endianf32(frame + 10, desc->endian);
	}

	if (desc->has_industry) {
		fseek(desc->ifp, 20, SEEK_CUR);
		if (film_parse(desc) != wu_ok) {
			desc->has_industry = false;
		}
	}
	return wu_ok;
}

static enum wu_error read_element(struct dpx_desc *desc,
struct dpx_generic_image *image, const uint8_t i) {
	/* Element struct:
		Offset  Size    Name
		0       u32     IsSigned
		4       u32     LowData
		8       flt32   LowQuantity
		12      u32     HighData
		16      flt32   HighQuantity
		20      u8      ComponentType
		21      u8      Transfer        // [1]
		22      u8      Primaries       // [1]
		23      u8      Bitdepth        // [1]
		24      u16     Packing
		26      u16     IsRLE
		28      u32     ComponentOffset // From beginning of file
		32      u32     LinePadding
		36      u32     ImagePadding
		40      char    Description[32]
		72

	* [1] Must be the same for all elements.
	*    (Why you put it in each struct then?)
	*/
	uint8_t buf[40];
	if (!fread(buf, sizeof(buf), 1, desc->ifp)) {
		return wu_unexpected_eof;
	}
	struct dpx_element *el = image->elem + i;
	*el = (struct dpx_element) {
		.attr = buf_endian32(buf, desc->endian) ? pix_signed : pix_normal,
		.type = buf[20],
		.rle = buf_endian16(buf + 26, desc->endian),
		.offset = buf_endian32(buf + 28, desc->endian),
		.line_pad = buf_endian32(buf + 32, desc->endian),
	};
	if (!fread(el->description, sizeof(el->description), 1, desc->ifp)) {
		return wu_unexpected_eof;
	}

	const enum dpx_oetf oetf = buf[21];
	const enum dpx_primaries pri = buf[22];
	const uint8_t bitdepth = buf[23];
	const enum dpx_pack pack = buf_endian16(buf + 24, desc->endian);
	switch (pack) {
	case dpx_pack_normal:
	case dpx_pack_a:
	case dpx_pack_b:
		el->pack = pack;
		break;
	default:
		return wu_invalid_header;
	}
	if (i) {
		if (oetf != image->oetf || pri != image->primaries
		|| bitdepth != image->bitdepth) {
			return wu_invalid_header;
		}
	} else {
		image->oetf = oetf;
		image->primaries = pri;
		image->bitdepth = bitdepth;
		switch (image->bitdepth) {
		case 1: case 8: case 16:
			el->pack = dpx_pack_normal;
			break;
		case 10: case 12:
			break;
		case 32: case 64:
			el->pack = dpx_pack_normal;
			el->attr = pix_float;
			break;
		default:
			return wu_invalid_header;
		}
	}
	struct elem_info nfo;
	return element_info(el->type, &nfo) ? wu_ok : wu_invalid_header;
}

static enum wu_error image_info_parse(struct dpx_desc *desc) {
	/* Image header (after prev):
		Offset  Size    Name
		0       u16     Orientation
		2       u16     NbElement
		4       u32     Width
		8       u32     Height
		12      struct  Element[8]
		588     u8      Reserved[52]
		640
	*/
	uint16_t buf[6];
	if (!fread(buf, sizeof(buf), 1, desc->ifp)) {
		return wu_unexpected_eof;
	}

	struct dpx_generic_image *image = &desc->generic.image;
	const uint16_t o = endian16(buf[0], desc->endian);
	const uint16_t elems = endian16(buf[1], desc->endian);
	if (o > 7 || elems < 1 || elems > 8) {
		return wu_invalid_header;
	}
	image->orientation = (uint8_t)o;
	image->w = buf_endian32(buf + 2, desc->endian);
	image->h = buf_endian32(buf + 4, desc->endian);

	uint8_t nb = 0;
	for (int e = 0; e < elems; ++e) {
		const enum wu_error st = read_element(desc, image, nb);
		switch (st) {
		case wu_ok:
			++nb;
			break;
		case wu_unexpected_eof:
			return st;
		default:
			break;
		}
	}
	if (!nb) {
		return wu_invalid_header;
	}
	image->nb_elem = nb;
	fseek(desc->ifp, (8 - elems)*72 + 52, SEEK_CUR);
	return source_parse(desc);
}

static enum wu_error file_info_parse(struct dpx_desc *desc) {
	/* File header (after magic bytes):
		Offset  Size    Name
		0       u32     ImageOffset
		4       char    Version[8]
		12      u32     FileSize
		16      u32     IsNewFrame
		20      u32     GenericSize
		24      u32     IndustrySize
		28      u32     UserSize
		32      char    Filename[100]
		132     char    Date[24]
		156     char    Creator[100]
		256     char    Project[200]
		456     char    Copyright[200]
		656     u32     EncryptKey
		660     u8      Reserved[104]
		764
	*/
	uint32_t buf[8];
	if (!fread(buf, sizeof(buf), 1, desc->ifp)) {
		return wu_unexpected_eof;
	}

	struct dpx_generic_file *file = &desc->generic.file;
	const char v1[] = "V1.0";
	const char v2[] = "V2.0";
	const char *str = (char *)(buf + 1);
	if (!strcmp(str, v1) || !strcmp(str, v2)) {
		desc->version = (uint8_t)str[1];
	} else {
		return wu_unsupported_feature;
	}

	file->is_new_frame = buf[4];
	if (endian32(buf[5], desc->endian) != 768 + 640 + 256) {
		return wu_invalid_header;
	}
	desc->has_industry = (endian32(buf[6], desc->endian) == 256 + 128);
	file->user_size = endian32(buf[7], desc->endian);

	if (!fread(file->name, sizeof(file->name), 1, desc->ifp)) {
		return wu_unexpected_eof;
	}
	file->date = read_date(desc->ifp);
	if (!fread(file->creator, sizeof(file->creator), 1, desc->ifp)
	|| !fread(file->project, sizeof(file->project), 1, desc->ifp)
	|| !fread(file->copyright, sizeof(file->copyright), 1, desc->ifp)
	|| !fread(buf, sizeof(*buf), 1, desc->ifp)) {
		return wu_unexpected_eof;
	}
	file->encrypt = endian32(buf[0], desc->endian);
	fseek(desc->ifp, 104, SEEK_CUR);
	return image_info_parse(desc);
}

enum wu_error dpx_parse(struct dpx_desc *desc) {
	/* DPX header overview, all structures contiguous:
		GenericHeader
			FileInfo
			ImageInfo
			OrientationInfo
		IndustryHeader
			FilmInfo
			TelevisionInfo
		UserData

	 * Of note is that unused fields are set to all ones.
	*/
	return file_info_parse(desc);
}

enum wu_error dpx_open(struct dpx_desc *desc, FILE *ifp) {
	uint8_t buf[4];
	if (fread(buf, sizeof(buf), 1, ifp)) {
		if (!memcmp(buf, "XPDS", sizeof(buf))) {
			desc->endian = little_endian;
		} else if (!memcmp(buf, "SDPX", sizeof(buf))) {
			desc->endian = big_endian;
		} else {
			return wu_invalid_header;
		}
		desc->ifp = ifp;
		return wu_ok;
	}
	return wu_unexpected_eof;
}
