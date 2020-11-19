#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <jpeglib.h>

#include "../wudefs.h"
#include "../common.h"
#include "../metadata.h"

enum marker_type {
	unknown_marker = 0,
	exif_marker = exif_metadata,
	xmp_marker = xmp_metadata,
	mpo_marker,
};

struct marker_info {
	enum marker_type type;
	size_t data_start;
};

struct jpeg_state {
	struct jpeg_decompress_struct dinfo;
	struct jpeg_error_mgr jerr;
	long *soi_offsets;
};

static void clean_jpeg_state(struct jpeg_state *js) {
	jpeg_destroy_decompress(&js->dinfo);
	free(js->soi_offsets);
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
	enum jpeg_parse_state {
		normal = 0,
		marker = 1,
		length = 2,
	} state = marker;

	size_t alloc = 16;
	js->soi_offsets = malloc(alloc * sizeof(*js->soi_offsets));
	if (!js->soi_offsets) {
		return 0;
	}

	fseek(ifp, 3, SEEK_SET);
	size_t idx = 0;
	for (int c; (c = getc(ifp)) != EOF;) {
		if (state == marker) {
			switch (c) {
			case 0xD8:
				if (!grow_buffer(&js->soi_offsets, &alloc,
				idx, sizeof(*js->soi_offsets)) ) {
					return 0;
				}
				js->soi_offsets[idx] = ftell(ifp) - 2;
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
	return idx + 1;
}

static struct marker_info identify_marker(jpeg_saved_marker_ptr mk) {
	// The implied null is relevant for all of these
	const unsigned char exif[] = "Exif\0";
	const unsigned char xmp[] = "http://ns.adobe.com/xap/1.0/";
	const unsigned char mpo[] = "MPF";

	struct marker_info info = {0, 0};
	if (mk->data_length > sizeof(xmp)) {
		if ( !memcmp(mk->data, mpo, sizeof(mpo)) ) {
			info.type = mpo_marker;
			info.data_start = sizeof(mpo);
		} else if ( !memcmp(mk->data, exif, sizeof(exif)) ) {
			info.type = exif_marker;
			info.data_start = sizeof(exif);
		} else if ( !memcmp(mk->data, xmp, sizeof(xmp)) ) {
			info.type = xmp_marker;
			info.data_start = sizeof(xmp);
		}
	}
	return info;
}

static enum wu_error parse_markers(jpeg_saved_marker_ptr mk,
struct image_file *infile, struct jpeg_state *js) {
	printf("Found marker type 0x%.2X. ", mk->marker);
	if (mk->data_length > 4) {
		print_unsafe_data("Starts with", mk->data, 4, stdout);
	} else {
		putchar('\n');
	}

	const struct marker_info info = identify_marker(mk);
	switch (info.type) {
	case xmp_marker:
	case exif_marker:
		standard_metadata((enum metadata_type)info.type,
			mk->data + info.data_start,
			mk->data_length - info.data_start, &infile->metadata);
		break;
	case mpo_marker:
		;
		/* MPO offsets are relative to the MPO marker, so we need to
		 * search the whole file. */
		const size_t nr = search_soi_offsets(infile->ifp, js);
		if (nr > 1) {
			if (!realloc_sub_images(infile, nr)) {
				return wu_alloc_error;
			}
		}
		break;
	default:
		break;
	}
	return wu_ok;
}

static enum wu_error decode_loop(struct image_file *infile,
const struct wu_conf *wuconf, struct jpeg_state *js) {
	struct jpeg_decompress_struct *dinfo = &js->dinfo;

	enum wu_error status = wu_ok;
	for (size_t i = 0; i < infile->nr && status == wu_ok; ++i) {
		if (i != 0) {
			fseek(infile->ifp, js->soi_offsets[i-1], SEEK_SET);
		}
		jpeg_stdio_src(dinfo, infile->ifp);

		jpeg_save_markers(dinfo, JPEG_COM, 0xFFFF);
		if (i == 0) {
			for (int m = 0xE1; m <= 0xEF; ++m) {
				switch (m) {
				case 0xE0: case 0xE8: case 0xEE:
					continue;
				default:
					jpeg_save_markers(dinfo, m, 0xFFFF);
				}
			}
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

		/* sub_img array is reallocated if an mpo marker is found, so
		 * the pointer must be copied inside the loop. */
		struct raw_img *img = infile->sub_img;
		img[i].w = dinfo->output_width;
		img[i].h = dinfo->output_height;
		if (zumax(img[i].w, img[i].h) > wuconf->max_img_size) {
			status = wu_exceeded_size_limit;
			break;
		}
		img[i].channels = (unsigned char)dinfo->output_components;
		img[i].bitdepth = 8;

		const size_t stride = img[i].w * img[i].channels;
		img[i].data = malloc(stride * img[i].h);
		if (!img[i].data) {
			status = wu_alloc_error;
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
				tree_sprout_unsafe_leaf(&infile->metadata,
					"Comment", mk->data, mk->data_length);
			} else if (i == 0) {
				status = parse_markers(mk, infile, js);
				if (status != wu_ok) {
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
	struct jpeg_state js;
	if (!alloc_sub_images(infile, 1)) {
		return wu_alloc_error;
	}

	js.dinfo.err = jpeg_std_error(&js.jerr);
	jpeg_create_decompress(&js.dinfo);
	js.soi_offsets = NULL;

	const enum wu_error status = decode_loop(infile, wuconf, &js);
	clean_jpeg_state(&js);
	return status;
}
