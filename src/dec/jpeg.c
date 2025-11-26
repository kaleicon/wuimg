// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2021 kaleido
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>

#include "wudefs.h"
#include "misc/math.h"
#include "misc/metadata.h"
#include "misc/wustr.h"

#include <jpeglib.h>

enum marker_type {
	unknown_marker = 0,
	exif_marker = metadata_exif,
	xmp_marker = metadata_xmp,
	icc_marker,
	mpo_marker,
};

struct marker_info {
	enum marker_type type;
	size_t data_start;
};

struct icc_assembler {
	unsigned char total;
	unsigned char seen;
	unsigned int acc;
	struct icc_shard {
		unsigned char *ptr; // library mem
		unsigned short len;
	} *shards;
};

struct jpeg_state {
	struct jpeg_decompress_struct dinfo;
	struct jpeg_error_mgr jerr;
	long file_offset;
	long *soi_offsets;

	jmp_buf jmp;
};

static void joutput_message(struct jpeg_common_struct *dinfo) {
	struct image_file *infile = dinfo->client_data;
	char msg[JMSG_LENGTH_MAX];
	(*dinfo->err->format_message)(dinfo, msg);
	image_file_strerror_append(infile, msg);
}

static void jerror_exit(struct jpeg_common_struct *dinfo) {
	struct image_file *infile = dinfo->client_data;
	struct jpeg_state *js = infile->dec_state;
	longjmp(js->jmp, wu_decoding_error);
}

static void end_jpeg(struct image_file *infile) {
	struct jpeg_state *js = infile->dec_state;
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

static size_t search_file_offsets(FILE *ifp, struct jpeg_state *js) {
	struct wugrow grow = wugrow_init(sizeof(*js->soi_offsets));

	fseek(ifp, 3, SEEK_SET);
	enum jpeg_parse_state {
		jpeg_parse_normal = 0,
		jpeg_parse_marker = 1,
		jpeg_parse_length = 2,
	} state = jpeg_parse_marker;
	for (int c; (c = getc(ifp)) != EOF;) {
		switch (state) {
		case jpeg_parse_marker:
			switch (c) {
			case 0xD8:
				if (!wugrow_recheck(&grow)) {
					return 0;
				}
				js->soi_offsets = grow.ptr;
				js->soi_offsets[grow.pos] = ftell(ifp) - 2;
				++grow.pos;
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
			break;
		case jpeg_parse_length:
			;long len = marker_len(ifp, c);
			if (len == EOF) {
				return grow.pos + 1;
			}
			fseek(ifp, len, SEEK_CUR);
			state = jpeg_parse_normal;
			break;
		case jpeg_parse_normal:
			if (c == 0xFF) {
				state = jpeg_parse_marker;
			}
			break;
		}
	}
	return grow.pos + 1;
}

static void assemble_icc(struct wuimg *img, struct icc_assembler *icc) {
	if (icc->total == icc->seen) {
		unsigned char *data = malloc(icc->acc);
		if (data) {
			unsigned int pos = 0;
			for (unsigned char i = 0; i < icc->total; ++i) {
				const struct icc_shard *sh = icc->shards + i;
				memcpy(data + pos, sh->ptr, sh->len);
				pos += sh->len;
			}
			color_space_set_icc_owned(&img->cs, data, pos);
		}
	}
}

static bool add_icc_shard(struct icc_assembler *icc,
const struct jpeg_marker_struct *mk) {
	if (mk->data_length < 14) {
		return false;
	}

	const unsigned char seq = mk->data[12];
	const unsigned char total = mk->data[13];
	if (icc->shards) {
		if (icc->total != total) {
			return false;
		}
	} else {
		if (!total) {
			return false;
		}
		icc->shards = calloc(total, sizeof(*icc->shards));
		if (!icc->shards) {
			return false;
		}
		icc->total = total;
	}
	if (seq == 0 || seq > total) {
		return false;
	}
	struct icc_shard *sh = icc->shards + seq - 1;
	if (sh->ptr) {
		return false;
	}
	sh->ptr = mk->data + 14;
	sh->len = (unsigned short)(mk->data_length - 14);
	icc->acc += sh->len;
	icc->seen += 1;
	return true;
}

static bool markercmp(const struct jpeg_marker_struct *mk,
const unsigned char *ch, const size_t len) {
	if (len + 2 < mk->data_length) {
		return !memcmp(mk->data, ch, len);
	}
	return false;
}

static struct marker_info identify_marker(const struct jpeg_marker_struct *mk) {
	// Implied nulls are relevant
	const unsigned char exif[] = "Exif\0";
	const unsigned char xmp[] = "http://ns.adobe.com/xap/1.0/";
	const unsigned char mpo[] = "MPF";
	const unsigned char icc[] = "ICC_PROFILE";

	struct marker_info info = {0, 0};
	if ( markercmp(mk, exif, sizeof(exif)) ) {
		info.type = exif_marker;
		info.data_start = sizeof(exif);
	} else if ( markercmp(mk, xmp, sizeof(xmp)) ) {
		info.type = xmp_marker;
		info.data_start = sizeof(xmp);
	} else if ( markercmp(mk, icc, sizeof(icc)) ) {
		info.type = icc_marker;
		info.data_start = sizeof(icc);
	} else if ( markercmp(mk, mpo, sizeof(mpo)) ) {
		info.type = mpo_marker;
	}
	return info;
}

static struct wu_st parse_markers(const struct jpeg_marker_struct *mk,
struct wutree *metadata, struct image_file *infile, struct jpeg_state *js,
struct icc_assembler *icc, const bool is_first) {
	const struct marker_info info = identify_marker(mk);
	switch (info.type) {
	case xmp_marker:
	case exif_marker:
		;const bool ok = metadata_parse((enum metadata_type)info.type,
			mk->data + info.data_start,
			mk->data_length - info.data_start, metadata);
		if (ok) {
			return WU_OK;
		}
		break;
	case icc_marker:
		if (add_icc_shard(icc, mk)) {
			return WU_OK;
		}
		break;
	case mpo_marker:
		if (!is_first) {
			/* MPO offsets are relative to the MPO marker, so we
			 * need to parse the whole file again. */
			const size_t nr = search_file_offsets(infile->ifp, js);
			if (nr > 1) {
				if (!realloc_sub_images(infile, nr)) {
					return WUERR_HERE(wu_alloc_error);
				}
			}
		}
		return WU_OK;
	default:
		break;
	}

	struct wutree *branch = tree_add_branch(metadata, "Marker");
	if (branch) {
		char app[] = "APPXXX";
		sprintf(app + 3, "%hhu", (uint8_t)(mk->marker - JPEG_APP0));

		tree_add_leaf_utf8(branch, "Type", app);
		tree_bud_leaf_u(branch, "Size", mk->data_length);
		tree_add_leaf_len(branch, "Data start",
			wuptr_mem(mk->data, zumin(12, mk->data_length)), NULL);
	}
	return WU_OK;
}

static struct wu_st iter_markers(const struct jpeg_marker_struct *mk,
struct image_file *infile, struct wuimg *img, struct jpeg_state *js,
const bool is_first) {
	struct icc_assembler icc = {0};
	struct wutree *metadata = wuimg_get_metadata(img);
	if (!img->metadata) {
		return WUERR_HERE(wu_alloc_error);
	}
	struct wu_st st = {0}; /* using WU_OK causes a "clobbered"
		compiler warning, due to setjmp() */
	while (mk) {
		if (mk->marker == JPEG_COM) {
			tree_add_leaf_len(metadata, "Comment",
				wuptr_mem(mk->data, mk->data_length), NULL);
		} else {
			st = parse_markers(mk, metadata, infile, js, &icc,
				is_first);
			if (!wu_isok(st)) {
				break;
			}
		}
		mk = mk->next;
	}
	if (icc.shards) {
		if (wu_isok(st)) {
			assemble_icc(img, &icc);
		}
		free(icc.shards);
	}
	return st;
}

static void decode_raw(struct wuimg *img,
struct jpeg_decompress_struct *dinfo) {
	const unsigned dct_h = (unsigned)dinfo->max_v_samp_factor * DCTSIZE;

	// Pointers to each row of a block.
	unsigned char *lum[DCTSIZE*MAX_SAMP_FACTOR];
	unsigned char *cb[DCTSIZE*MAX_SAMP_FACTOR];
	unsigned char *cr[DCTSIZE*MAX_SAMP_FACTOR];
	unsigned char *key[DCTSIZE*MAX_SAMP_FACTOR];
	unsigned char **comps[4] = {lum, cb, cr, key};

	for (size_t lines = 0; lines < img->h;) {
		for (size_t z = 0; z < img->channels; ++z) {
			const struct plane_info *p = img->u.planes->p + z;
			unsigned char *start = p->ptr
				+ p->stride * lines / p->y.subsamp;
			for (size_t y = 0; y < dct_h; ++y) {
				comps[z][y] = start + p->stride * y;
			}
		}
		lines += jpeg_read_raw_data(dinfo, comps, dct_h);
	}
}

static bool set_colorspace(struct wuimg *img,
const struct jpeg_decompress_struct *dinfo) {
	img->alpha = img->channels == 4 ? alpha_key : alpha_ignore;
	switch (dinfo->jpeg_color_space) {
	case JCS_YCCK:
		img->cs.invert = true;
		// fallthrough
	case JCS_YCbCr:
		img->cs.matrix = cicp_matrix_bt601_7;
		break;
	case JCS_GRAYSCALE:
	case JCS_UNKNOWN:
	case JCS_RGB:
	case JCS_CMYK:
		break;
	default:
		break;
	}

	struct image_planes *planes = wuimg_plane_init(img);
	if (!planes) {
		return false;
	}
	planes->v_pad = align_from_int(DCTSIZE);
	const jpeg_component_info *nfo = dinfo->comp_info;
	for (uint8_t i = 0; i < img->channels; ++i) {
		// Subsampling factors will be between [1, 4]
		// Siting is always midpoint
		const int xsamp = dinfo->max_h_samp_factor
			/ nfo[i].h_samp_factor;
		const int ysamp = dinfo->max_v_samp_factor
			/ nfo[i].v_samp_factor;
		planes->p[i].x.subsamp = (uint8_t)xsamp;
		planes->p[i].y.subsamp = (uint8_t)ysamp;
	}
	return true;
}

static struct wu_st decode_img(struct image_file *infile,
const struct wu_conf *conf, const int i) {
	struct jpeg_state *js = infile->dec_state;
	const int val = setjmp(js->jmp);
	if (val) {
		return WUERR_HERE((enum wu_error)val);
	}

	struct jpeg_decompress_struct *dinfo = &js->dinfo;

	const long pos = i ? js->soi_offsets[i-1] : js->file_offset;
	fseek(infile->ifp, pos, SEEK_SET);
	jpeg_stdio_src(dinfo, infile->ifp);

	struct wuimg *img = infile->sub_img + i;
	jpeg_save_markers(dinfo, JPEG_COM, 0xFFFF);
	for (int m = 0xE0; m <= 0xEF; ++m) {
		switch (m) {
		case 0xE0: case 0xE8: case 0xEE:
			continue;
		default:
			jpeg_save_markers(dinfo, m, 0xFFFF);
		}
	}
	jpeg_read_header(dinfo, TRUE);

	dinfo->raw_data_out = TRUE;
	dinfo->out_color_space = dinfo->jpeg_color_space;
	dinfo->do_block_smoothing = FALSE;
	dinfo->dct_method = conf->jpeg_fast_dct
		? JDCT_FASTEST : JDCT_DEFAULT;

	jpeg_calc_output_dimensions(dinfo);
	img->w = dinfo->output_width;
	img->h = dinfo->output_height;
	if (wuimg_exceeds_limit(img, conf)) {
		return WUERR_HERE(wu_exceeds_size_limit);
	}

	jpeg_start_decompress(dinfo);

	img->channels = (unsigned char)dinfo->output_components;
	img->bitdepth = 8;
	wuimg_align(img, DCTSIZE);
	if (!set_colorspace(img, dinfo)) {
		return WUERR_HERE(wu_alloc_error);
	}

	struct wu_st st = WUERR_CHECK(wuimg_alloc(img));
	if (wu_isok(st)) {
		decode_raw(img, dinfo);

		if (dinfo->marker_list) {
			st = iter_markers(dinfo->marker_list, infile, img,
				js, i == 0);
		}
		jpeg_finish_decompress(dinfo);
		if (wu_isok(st) && img->metadata) {
			wuimg_exif_orientation(img,
				metadata_orientation(img->metadata));
		}
	}
	return st;
}

static struct wu_st event_jpeg(struct image_file *infile,
const struct wu_conf *conf, struct wu_state *state,
const enum image_event ev) {
	return (ev == ev_subcycle)
		? decode_img(infile, conf, state->idx)
		: WU_NO_CHANGE;
}

static long header_skip(int c) {
	switch (c) {
	case 'E': return 0x15; // Esm Software PIX
	case 0x80: return 0x200; // Ricoh J6I
	}
	return 0;
}

static struct wu_st init_jpeg(struct image_file *infile,
const struct wu_conf *_c) {
	(void)_c;
	struct jpeg_state *js = infile->dec_state;
	const int c = fgetc(infile->ifp);
	js->file_offset = header_skip(c);
	js->dinfo.client_data = infile;
	js->dinfo.err = jpeg_std_error(&js->jerr);
	js->jerr.error_exit = jerror_exit;
	js->jerr.output_message = joutput_message;
	js->soi_offsets = NULL;
	jpeg_create_decompress(&js->dinfo);
	return WU_OK;
}

const struct image_fn jpeg_fn = {
	.alloc_single = true,
	.state_size = sizeof(struct jpeg_state),
	.init = init_jpeg,
	.event = event_jpeg,
	.end = end_jpeg,
};
