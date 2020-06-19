#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <jpeglib.h>

#include "wudefs.h"
#include "common.h"

struct jpeg_state {
	size_t soi_alloc;
	long *soi_offsets;
	struct jpeg_decompress_struct dinfo;
	struct jpeg_error_mgr jerr;
};

static void clean_jpeg_state(struct jpeg_state *js) {
	jpeg_destroy_decompress(&js->dinfo);
	free(js->soi_offsets);
	free(js);
}

static long marker_len(FILE *f, int first_byte) {
	long i = first_byte << 8;
	long c = getc(f);
	if (c != EOF) {
		return i + c - 2; // Length specifier includes itself.
	}
	return EOF;
}

static size_t search_soi_offsets(FILE *ifp, struct jpeg_state *js) {
	fseek(ifp, 3, SEEK_SET);
	enum jpeg_parse_state {
		normal = 0,
		marker = 1,
		length = 2,
	} state = marker;

	size_t alloc = js->soi_alloc;
	size_t idx = 1;
	for (int c; (c = getc(ifp)) != EOF;) {
		if (state == marker) {
			switch (c) {
			case 0xD8:
				if (idx == alloc) {
					alloc += alloc / 4;
					long *hold = realloc(js->soi_offsets,
						alloc * sizeof(long));
					if (!hold) {
						break;
					}
					js->soi_offsets = hold;
				}
				const long i = ftell(ifp);
				js->soi_offsets[idx] = i - 2;
				++idx;
				state = normal;
				break;
			case 0x00: case 0x01:
			case 0xD0: case 0xD1: case 0xD2: case 0xD3:
			case 0xD4: case 0xD5: case 0xD6: case 0xD7:
			case 0xD9:
			case 0xDA:
			case 0xFF:
				state = normal;
				break;
			default:
				state = length;
			}
		} else if (state == length) {
			long len = marker_len(ifp, c);
			if (len == EOF) {
				break;
			}
			fseek(ifp, len, SEEK_CUR);
			state = normal;
		} else if (c == 0xFF) {
			state = marker;
		}
	}
	js->soi_alloc = alloc;
	return idx;
}

static bool is_mpo(const JOCTET *data, unsigned int len) {
	if (len < 4) {
		return false;
	}
	return !memcmp(data, "MPF", 4);
}

static enum wu_error decode_loop(struct image_file *infile,
const struct wu_conf *wuconf, struct jpeg_state *js) {
	struct raw_img *img = infile->sub_img;
	struct jpeg_decompress_struct *dinfo = &js->dinfo;

	enum wu_error status = wu_ok;
	for (size_t i = 0; i < infile->nr && status == wu_ok; ++i) {
		fseek(infile->ifp, js->soi_offsets[i], SEEK_SET);
		jpeg_stdio_src(dinfo, infile->ifp);

		jpeg_save_markers(dinfo, JPEG_COM, 0xFFFF);
		if (i == 0) {
			jpeg_save_markers(dinfo, 0xE2, 0xFFFF);
		}
		jpeg_read_header(dinfo, TRUE);

		dinfo->do_block_smoothing = FALSE;
		if (wuconf->jpeg_fast_dct) {
			dinfo->dct_method = JDCT_FASTEST;
		}
		// segfault if not 1
		if (wuconf->jpeg_fast_upsamp && dinfo->max_v_samp_factor == 1) {
			dinfo->do_fancy_upsampling = FALSE;
		}

		jpeg_start_decompress(dinfo);

		img[i].w = dinfo->output_width;
		img[i].h = dinfo->output_height;
		if (zumax(img[i].w, img[i].h) > wuconf->max_img_size) {
			status = wu_exceeded_size_limit;
			jpeg_finish_decompress(dinfo);
			break;
		}
		img[i].channels = (unsigned char)dinfo->output_components;
		img[i].bitdepth = 8;

		const size_t stride = img[i].w * img[i].channels;
		img[i].data = malloc(stride * img[i].h);
		if (!img[i].data) {
			status = wu_alloc_error;
			jpeg_finish_decompress(dinfo);
			break;
		}
		if (i > 0) {
			img[i].id = id_template("mpo", i);
		}

		JSAMPROW row_ptr = img[i].data;
		while (dinfo->output_scanline < dinfo->output_height) {
			row_ptr += stride * jpeg_read_scanlines(dinfo, &row_ptr,
				(unsigned int)dinfo->rec_outbuf_height);
		}

		jpeg_saved_marker_ptr mk = dinfo->marker_list;
		while (mk) {
			if (mk->marker == JPEG_COM) {
				print_unsafe_data(mk->data, mk->data_length,
					NULL, true);
			} else if (i == 0 && mk->marker == 0xE2
			&& is_mpo(mk->data, mk->data_length)) {
				/* MPO offsets are relative to the MPO marker,
				 * so we need to search for it again */
				const size_t nr = search_soi_offsets(
					infile->ifp, js);
				img = realloc_sub_images(infile, nr);
				if (!img) {
					status = wu_alloc_error;
					break;
				}
			}
			mk = mk->next;
		}

		jpeg_finish_decompress(dinfo);
	}
	return status;
}

enum wu_error jpeg_dec(struct image_file *infile,
const struct wu_conf *wuconf) {
	struct jpeg_state *js = malloc(sizeof(struct jpeg_state));
	if (!js) {
		return wu_alloc_error;
	}

	js->soi_alloc = 4;
	js->soi_offsets = calloc(js->soi_alloc, sizeof(*js->soi_offsets));
	if (!js->soi_offsets) {
		free(js);
		return wu_alloc_error;
	}

	if (!alloc_sub_images(infile, 1)) {
		clean_jpeg_state(js);
		return wu_alloc_error;
	}

	js->dinfo.err = jpeg_std_error(&js->jerr);
	jpeg_create_decompress(&js->dinfo);

	const enum wu_error status = decode_loop(infile, wuconf, js);
	clean_jpeg_state(js);
	return status;
}
