#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <jpeglib.h>

#include "wudefs.h"
#include "common.h"

struct mp_img_attr {
	u_int32_t parent:1;
	u_int32_t child:1;
	u_int32_t representative:1;
	u_int32_t __reserved:2;
	u_int32_t format:3;
	u_int32_t __type_reservedA:4;
	u_int32_t type_info:4;
	u_int32_t __type_reservedB:12;
	u_int32_t type_sub:4;
};

struct mp_file_info {
	enum endianness order;
	u_int32_t nr;
	struct mp_img_attr attr;
};

static u_int16_t read_MP_entry(const JOCTET *entry, struct mp_file_info *mpf) {
	int len = (int) endian_uint16(entry, mpf->order);
	for (int i = 0; i < len; i += 16) {
		struct mp_img_attr *restrict attr = &(mpf->attr);
		u_int32_t attr_src = endian_uint32(entry, mpf->order);
		memcpy(attr, &attr_src, sizeof(mpf->attr));
		printf("\nIs parent: %u\n"
			"Is child: %u\n"
			"Is representative: %u\n"
			"Reserved bits: %#x\n"
			"Data format: %#x\n"
			"Type code:\n"
			"|-Reserved bytes A:%#x\n"
			"|-Type info: %#x\n"
			"|-Reserved bytes B:%#x\n"
			"|-Sub type: %#x\n",
			attr->parent, attr->child, attr->representative,
			attr->__reserved, attr->format, attr->__type_reservedA,
			attr->type_info, attr->__type_reservedB,
			attr->type_sub);
	}
	return (u_int16_t) len;
}

static u_int32_t read_MP_index(const JOCTET *restrict data, unsigned int len,
struct mp_file_info *mpf) {
	u_int16_t count = endian_uint16(data, mpf->order);
	data += 2; len -= 2;

	const unsigned char format_version[] = {0x30, 0x31, 0x30, 0x30};
	u_int16_t entry_len = 0;
	mpf->nr = 0;
	for (u_int16_t i = 0; i < count; ++i) {
		if (data[0] == 0xB0) {
			switch (data[1]) {
			case 0x00:
				if (memcmp(data+8, format_version, 4)) {
					puts("Unknown MPO version.");
					return 0;
				}
				entry_len = 4 + 8;
				break;
			case 0x01:
				mpf->nr = endian_uint16(data+8, mpf->order);
				entry_len = 4 + 8;
				break;
			case 0x02:
				entry_len = read_MP_entry(data+6, mpf);
			}
		} else {
			break;
		}
		data += entry_len;
	}
	return 1;
}

static u_int32_t read_MP_header(const JOCTET *restrict data,
struct mp_file_info *mpf) {
	const unsigned char l_endian_id[] = {0x49, 0x49, 0x2A, 0x00};
	const unsigned char b_endian_id[] = {0x4D, 0x4D, 0x00, 0x2A};
	if (!memcmp(data, l_endian_id, sizeof(l_endian_id))) {
		mpf->order = little_endian;
	} else if (!memcmp(data, b_endian_id, sizeof(b_endian_id))) {
		mpf->order = big_endian;
	} else {
		return 0;
	}
	return endian_uint32(data + 4, mpf->order);
}

static void read_MP(const JOCTET *data, unsigned int len) {
	struct mp_file_info mpf;
	u_int32_t offset = read_MP_header(data, &mpf);
	if (offset < 8 || offset > len) {
		return;
	}
	data += offset; len -= offset;

	read_MP_index(data, len, &mpf);
}

static long marker_len(FILE *f, int first_byte) {
	long i = (long)(first_byte<<8);
	int c = getc(f);
	if (c != EOF) {
		return i + (long)c - 2; // Length specifier includes itself.
	}
	return EOF;
}

static size_t parse_jpeg(FILE *fp, long *s_offsets, size_t alloc) {
	enum jpeg_parse_state {
		normal = 0,
		marker = 1,
		payload = 2,
		app2 = 3,
	} state = marker; // Signature check left us on the second 0xFF marker.
	int found_mpo_marker = 0;
	s_offsets[0] = 0;
	size_t nr = 1;
	long i = ftell(fp) + 1;
	for (int c; (c = getc(fp)) != EOF; ++i) {
		if (state == marker) {
			switch (c) {
			case 0xD8:
				if (nr == alloc) {
					alloc += alloc / 2;
					size_t newsize = alloc * sizeof(long);
					s_offsets = realloc(s_offsets, newsize);
				}
				s_offsets[nr] = i - 1;
				++nr;
				state = normal;
				break;
			case 0xD9:
				if (!found_mpo_marker) {
					puts(".mpo file doesn't have an APP2 "
						"marker. Will treat as JPEG.");
					return 1;
				}
				found_mpo_marker = 0; //Fallthrough
			case 0x00: case 0x01:
			case 0xD0: case 0xD1: case 0xD2: case 0xD3:
			case 0xD4: case 0xD5: case 0xD6: case 0xD7:
			case 0xDA:
			case 0xFF:
				state = normal;
				break;
			case 0xE2:
				state = app2;
				break;
			default:
				state = payload;
			}
		} else if (state == payload) {
			long len = marker_len(fp, c);
			if (len != EOF) {
				i += len;
				fseek(fp, i, SEEK_SET);
				state = normal;
				--i;
			} else {
				break;
			}
		} else if (state == app2) {
			long len = marker_len(fp, c);
			if (len != EOF) {
				size_t buf_len = 4;
				unsigned char id_buf[buf_len];
				if (fread(id_buf, 1, buf_len, fp) != buf_len) {
					break;
				} else if (!memcmp(id_buf, "MPF", buf_len)) {
					found_mpo_marker = 1;
				}
				i += len;
				fseek(fp, i, SEEK_SET);
				state = normal;
				--i;
			} else {
				break;
			}
		} else if (c == 0xFF) {
			state = marker;
		}
	}
	return nr;
}

static bool has_mpo_ext(const char *name) {
	const char *ext = strrchr(name, '.') + 1;
	return !strcmp(ext, "mpo");
}

static bool is_valid_jpeg(FILE *f) {
	return getc(f) == 0xFF && getc(f) == 0xD8 && getc(f) == 0xFF;
}

enum wu_error_type jpeg_dec(struct image_file *infile) {
	FILE *ifp = fopen(infile->name, "rb");
	if (!ifp) {
		infile->err_msg = strerror(errno);
		return wu_open_error;
	} else if (!is_valid_jpeg(ifp)) {
		infile->err_msg = strdup("Not a JPEG or Exif file.");
		fclose(ifp);
		return wu_invalid_sig;
	}

	const enum file_type {
		jpeg = 0,
		mpo = 1,
	} type = has_mpo_ext(infile->name);

	long *soi_offsets = NULL;
	if (type == mpo) {
		const size_t alloc_size = 2;
		soi_offsets = malloc(sizeof(long) * alloc_size);
		infile->nr = parse_jpeg(ifp, soi_offsets, alloc_size);
	} else {
		soi_offsets = calloc(1, sizeof(long));
		infile->nr = 1;
	}

	alloc_sub_images(infile, infile->nr);

	struct jpeg_decompress_struct dinfo;
	struct jpeg_error_mgr jerr;
	dinfo.err = jpeg_std_error(&jerr);
	jpeg_create_decompress(&dinfo);

	for (size_t i = 0; i < infile->nr; ++i) {
		fseek(ifp, soi_offsets[i], SEEK_SET);
		jpeg_stdio_src(&dinfo, ifp);

		jpeg_save_markers(&dinfo, JPEG_COM, 0xFFFF);
		if (type == mpo) {
			jpeg_save_markers(&dinfo, 0xE2, 0xFFFF);
		}
		jpeg_read_header(&dinfo, TRUE);

		// libjpeg resets these after reading the header
		dinfo.dct_method = JDCT_FASTEST;
		dinfo.do_block_smoothing = FALSE;
		if (dinfo.max_v_samp_factor == 1) { // segfault otherwise
			dinfo.do_fancy_upsampling = FALSE;
		}
		printf("-Subsampling: %dx%d\n", dinfo.max_h_samp_factor,
			dinfo.max_v_samp_factor);
		jpeg_start_decompress(&dinfo);

		struct raw_img *restrict img = &infile->sub_img[i];
		img->w = dinfo.output_width;
		img->h = dinfo.output_height;
		img->channels = (unsigned char)dinfo.output_components;
		img->bitdepth = 8;
		const size_t row_stride = img->w * img->channels;
		img->data = malloc(row_stride * img->h);
		if (type == mpo && infile->nr > 1) {
			img->id = id_template("mpo", i);
		}

		for (size_t j = 0; dinfo.output_scanline < dinfo.output_height;) {
			JSAMPROW row_ptr = img->data + j;
			j += row_stride * jpeg_read_scanlines(&dinfo, &row_ptr,
				(unsigned int)dinfo.rec_outbuf_height);
		}

		jpeg_saved_marker_ptr mk = dinfo.marker_list;
		while (mk) {
			if (mk->marker == JPEG_COM) {
				puts("-Found JPEG comment:");
				print_unsafe_data(mk->data, mk->data_length);
			} else if (mk->marker == 0xE2 && mk->data_length > 20
			&& !memcmp(mk->data, "MPF", 4)) {
				read_MP(mk->data + 4, mk->data_length - 4);
			}
			mk = mk->next;
		}

		jpeg_finish_decompress(&dinfo);
	}
	jpeg_destroy_decompress(&dinfo);
	free(soi_offsets);

	fclose(ifp);
	return 0;
}
