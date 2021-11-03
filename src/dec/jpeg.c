#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <setjmp.h>

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
	jmp_buf jmp;
};

__attribute__((unused))
static void joutput_message(struct jpeg_common_struct *dinfo) {
	struct image_file *infile = dinfo->client_data;
	char msg[JMSG_LENGTH_MAX];
	(*dinfo->err->format_message)(dinfo, msg);
	image_file_error_append(infile, msg);
}

__attribute__((unused))
static void jerror_exit(struct jpeg_common_struct *dinfo) {
	struct image_file *infile = dinfo->client_data;
	struct jpeg_state *js = infile->dec_state;
	longjmp(js->jmp, wu_decoding_error);
}

static void clean_jpeg_state(struct image_file *infile) {
	struct jpeg_state *js = infile->dec_state;
	jpeg_destroy_decompress(&js->dinfo);
	free(js->soi_offsets);
	free(js);
	infile->dec_state = NULL;
	infile->events = 0;
}

static long marker_len(FILE *f, int first_byte) {
	long i = first_byte << 8;
	long c = getc(f);
	if (c != EOF) {
		return i + c - 2; // Length specifier includes itself.
	}
	return EOF;
}

static size_t search_file_offsets(FILE *ifp, struct jpeg_state *js) {
	size_t alloc = 16;
	js->soi_offsets = malloc(alloc * sizeof(*js->soi_offsets));
	if (!js->soi_offsets) {
		return 0;
	}

	fseek(ifp, 3, SEEK_SET);
	enum jpeg_parse_state {
		jpeg_parse_normal = 0,
		jpeg_parse_marker = 1,
		jpeg_parse_length = 2,
	} state = jpeg_parse_marker;
	size_t idx = 0;
	for (int c; (c = getc(ifp)) != EOF;) {
		if (state == jpeg_parse_marker) {
			switch (c) {
			case 0xD8:
				if (!grow_buffer(&js->soi_offsets, &alloc,
				idx, sizeof(*js->soi_offsets)) ) {
					return 0;
				}
				js->soi_offsets[idx] = ftell(ifp) - 2;
				++idx;
				state = jpeg_parse_normal;
				break;
			case 0x00: case 0x01:
			case 0xD0: case 0xD1: case 0xD2: case 0xD3:
			case 0xD4: case 0xD5: case 0xD6: case 0xD7:
			case 0xD9:
			case 0xDA:
			case 0xFF:
				state = jpeg_parse_normal;
				break;
			default:
				state = jpeg_parse_length;
			}
		} else if (state == jpeg_parse_length) {
			long len = marker_len(ifp, c);
			if (len == EOF) {
				break;
			}
			fseek(ifp, len, SEEK_CUR);
			state = jpeg_parse_normal;
		} else if (c == 0xFF) {
			state = jpeg_parse_marker;
		}
	}
	return idx + 1;
}

static bool markercmp(const struct jpeg_marker_struct *mk,
const unsigned char *ch, const size_t len) {
	if (len < mk->data_length) {
		return !memcmp(mk->data, ch, len);
	}
	return false;
}

static struct marker_info identify_marker(const struct jpeg_marker_struct *mk) {
	// Implied nulls are relevant
	const unsigned char exif[] = "Exif\0";
	const unsigned char xmp[] = "http://ns.adobe.com/xap/1.0/";
	const unsigned char mpo[] = "MPF";

	struct marker_info info = {0, 0};
	if ( markercmp(mk, exif, sizeof(exif)) ) {
		info.type = exif_marker;
		info.data_start = sizeof(exif);
	} else if ( markercmp(mk, xmp, sizeof(xmp)) ) {
		info.type = xmp_marker;
		info.data_start = sizeof(xmp);
	} else if ( markercmp(mk, mpo, sizeof(mpo)) ) {
		info.type = mpo_marker;
	}
	return info;
}

static enum wu_error parse_markers(const struct jpeg_marker_struct *mk,
struct image_file *infile, struct jpeg_state *js, bool first_image) {
	const struct marker_info info = identify_marker(mk);
	struct wu_tree *metadata = &infile->metadata;
	switch (info.type) {
	case xmp_marker:
	case exif_marker:
		standard_metadata((enum metadata_type)info.type,
			mk->data + info.data_start,
			mk->data_length - info.data_start, metadata);
		break;
	case mpo_marker:
		if (first_image) {
			/* MPO offsets are relative to the MPO marker, so we
			 * need to parse the whole file again. */
			;const size_t nr = search_file_offsets(infile->ifp, js);
			if (nr > 1) {
				if (!realloc_sub_images(infile, nr)) {
					return wu_alloc_error;
				}
			}
		}
		break;
	default:
		;struct wu_tree *branch = tree_sprout_branch(metadata,
			"Marker");
		if (branch) {
			char app[] = "APPXXX";
			sprintf(app + 3, "%hhu", mk->marker - JPEG_APP0);

			tree_sprout_leaf(branch, "Type", app);
			tree_bud_leaf(branch, "Size",
				(struct wu_leaf){
					.val.u = mk->data_length,
					.type = wu_leaf_unsigned
				});
			tree_sprout_unsafe_leaf(branch, "Data start", mk->data,
				zumin(12, mk->data_length));
		}
		break;
	}
	return wu_ok;
}

__attribute__((unused))
static void decode_raw(struct raw_img *img,
struct jpeg_decompress_struct *dinfo) {
	struct yuva_info info;
	raw_img_yuva_info(img, &info);

	const unsigned dct_h = (unsigned)dinfo->max_v_samp_factor * DCTSIZE;

	// Pointers to each row of a block.
	unsigned char *y[DCTSIZE*MAX_SAMP_FACTOR];
	unsigned char *cb[DCTSIZE*MAX_SAMP_FACTOR];
	unsigned char *cr[DCTSIZE*MAX_SAMP_FACTOR];
	unsigned char **comps[3] = {y, cb, cr};

	const size_t div = (img->subsamp & 0x03) + 1;
	for (size_t lines = 0; lines < img->h;) {
		for (size_t z = 0; z < img->channels; ++z) {
			size_t stride, y_offset;
			if (z == 0) {
				stride = info.ya.stride;
				y_offset = lines;
			} else {
				stride = info.uv.stride;
				y_offset = lines / div;
			}
			unsigned char *start = info.yuva[z] + stride * y_offset;
			for (size_t y = 0; y < dct_h; ++y) {
				comps[z][y] = start + stride * y;
			}
		}

		const size_t read = jpeg_read_raw_data(dinfo, comps, dct_h);
		lines += read;
	}
}
__attribute__((unused))
static bool use_raw(struct raw_img *img, struct jpeg_decompress_struct *dinfo) {
	if (dinfo->jpeg_color_space != JCS_YCbCr) {
		return false;
	}
	switch (dinfo->num_components) {
	case 1: return true;
	case 3:
		;jpeg_component_info *nfo = dinfo->comp_info;
		int h_samp[3];
		int v_samp[3];
		for (int i = 0; i < dinfo->num_components; ++i) {
			h_samp[i] = nfo[i].h_samp_factor;
			v_samp[i] = nfo[i].v_samp_factor;
		}
		const int max_h = dinfo->max_h_samp_factor;
		const int max_v = dinfo->max_v_samp_factor;
		if (h_samp[0] != max_h || v_samp[0] != max_v) {
			return false;
		}
		if (h_samp[1] != h_samp[2] || v_samp[1] != v_samp[2]) {
			return false;
		}
		const int h = max_h / h_samp[1];
		const int v = max_v / v_samp[1];
		img->subsamp = (enum pix_subsampling)
			( ((h - 1) << 2) | (v - 1) );
		return true;
	}
	return false;
}

static enum wu_error decode_img(struct image_file *infile,
const struct wu_conf *wuconf, const int i, const bool partial_decode) {
	struct jpeg_state *js = infile->dec_state;
	const int val = setjmp(js->jmp);
	if (val) {
		return (enum wu_error)val;
	}

	struct jpeg_decompress_struct *dinfo = &js->dinfo;

	const long pos = i ? js->soi_offsets[i-1] : 0;
	fseek(infile->ifp, pos, SEEK_SET);
	jpeg_stdio_src(dinfo, infile->ifp);

	struct raw_img *img = infile->sub_img;
	const bool first_decode = !img[i].data;
	if (first_decode) {
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
	}
	jpeg_read_header(dinfo, TRUE);

	/* Although YUV output sounds like a good idea, we lose the ability to
	 * decode a downscaled version. No idea how libjpeg does it, but I
	 * don't want to clutter this file with that. Also, we need to pad each
	 * plane vertically, but our routines weren't made for that case, so
	 * commented out this remains. */
//	img[i].yuva = use_raw(img + i, dinfo);
//	dinfo->raw_data_out = img[i].yuva;

	dinfo->do_block_smoothing = FALSE;
	if (wuconf->jpeg_fast_dct) {
		dinfo->dct_method = JDCT_FASTEST;
	}
	// segfault when max_v_samp_factor != 1
	if (wuconf->jpeg_fast_upsamp && dinfo->max_v_samp_factor == 1) {
		dinfo->do_fancy_upsampling = FALSE;
	}
//	dinfo->out_color_space = dinfo->jpeg_color_space;

	if (partial_decode) {
		const unsigned jw = dinfo->image_width;
		const unsigned jh = dinfo->image_height;
		size_t f = image_fit_factor(wuconf, jw, jh, 8, true);
		if (!f) {
			return wu_exceeds_size_limit;
		}
		dinfo->scale_denom = 1 << zulog2(f);
	}
	img[i].dec_scale = 1.0f / (float)dinfo->scale_denom;

	jpeg_start_decompress(dinfo);

//	if (img[i].yuva) {
//		img[i].w = dinfo->comp_info->width_in_blocks * DCTSIZE;
//		img[i].h = dinfo->comp_info->height_in_blocks * DCTSIZE;
//	} else {
		img[i].w = dinfo->output_width;
		img[i].h = dinfo->output_height;
//	}
	if (zumax(img[i].w, img[i].h) > wuconf->max_img_size) {
		return wu_exceeds_size_limit;
	}
	img[i].channels = (unsigned char)dinfo->output_components;
	img[i].bitdepth = 8;
	img[i].alignment = 1; // DCTSIZE for raw data, anything otherwise.

	if (img[i].data) { // Previous downscale
		free(img[i].data);
	}
	const size_t stride = raw_img_addbuf(img + i);
	if (!stride) {
		return wu_alloc_error;
	}

//	if (img[i].yuva) {
//		decode_raw(img + i, dinfo);
//	} else {
		unsigned char *row_ptr = img[i].data;
		while (dinfo->output_scanline < dinfo->output_height) {
			row_ptr += stride * jpeg_read_scanlines(dinfo, &row_ptr,
				(unsigned int)dinfo->rec_outbuf_height);
		}
//	}

	enum wu_error status = wu_ok;
	jpeg_saved_marker_ptr mk = dinfo->marker_list;
	while (mk) {
		if (mk->marker == JPEG_COM) {
			tree_sprout_unsafe_leaf(&infile->metadata,
				"Comment", mk->data, mk->data_length);
		} else {
			status = parse_markers(mk, infile, js, i == 0);
			if (status != wu_ok) {
				break;
			}
		}
		mk = mk->next;
	}

	jpeg_finish_decompress(dinfo);
	return status;
}

static bool file_done(struct image_file *infile) {
	const struct raw_img *img = infile->sub_img;
	for (size_t i = 0; i < infile->nr; ++i) {
		if (!img[i].data || img[i].dec_scale < 1) {
			return false;
		}
	}
	return true;
}

enum wu_error jpeg_callback(struct image_file *infile,
const struct wu_conf *wuconf, struct wu_state *state,
const enum image_event ev) {
	enum wu_error status = wu_no_change;
	if (ev) {
		const int idx = state->idx;
		bool partial_decode = false;
		if (ev & ev_upscale) {
			if (state->zoom > 1) {
				state->zoom *= infile->sub_img[idx].dec_scale;
			} else if (!(ev & ev_subcycle)) {
				return wu_no_change;
			}
		}
		status = decode_img(infile, wuconf, idx, partial_decode);
	}

	if (status > wu_ok || !ev || (status == wu_ok && file_done(infile))) {
		clean_jpeg_state(infile);
	}
	return status;
}

enum wu_error jpeg_dec(struct image_file *infile,
const struct wu_conf *wuconf) {
	if (!alloc_sub_images(infile, 1)) {
		return wu_alloc_error;
	}

	struct jpeg_state *js = malloc(sizeof(*js));
	if (!js) {
		return wu_alloc_error;
	}

	infile->dec_state = js;

	js->dinfo.client_data = infile;
	js->dinfo.err = jpeg_std_error(&js->jerr);
//	js->jerr.error_exit = jerror_exit;
//	js->jerr.output_message = joutput_message;
	js->soi_offsets = NULL;

	jpeg_create_decompress(&js->dinfo);

	const enum wu_error status = decode_img(infile, wuconf, 0,
		wuconf->partial_decode);
	if (status == wu_ok && !file_done(infile)) {
		infile->events = ev_subcycle | ev_upscale;
		return status;
	}
	clean_jpeg_state(infile);
	return status;
}
