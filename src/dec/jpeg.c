#include <stdio.h>
#include <setjmp.h>

#include <jpeglib.h>

#include "wudefs.h"
#include "common/math.h"
#include "common/wustr.h"
#include "metadata.h"

enum marker_type {
	unknown_marker = 0,
	exif_marker = exif_metadata,
	xmp_marker = xmp_metadata,
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
		if (state == jpeg_parse_marker) {
			switch (c) {
			case 0xD8:
				if (!wugrow_recheck(&js->soi_offsets, &grow)) {
					return 0;
				}
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
	return grow.pos + 1;
}

static void assemble_icc(struct raw_img *img, struct icc_assembler *icc) {
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

static enum wu_error parse_markers(const struct jpeg_marker_struct *mk,
struct wu_tree *metadata, struct image_file *infile, struct jpeg_state *js,
struct icc_assembler *icc, const bool is_first) {
	const struct marker_info info = identify_marker(mk);
	switch (info.type) {
	case xmp_marker:
	case exif_marker:
		;const bool ok = standard_metadata((enum metadata_type)info.type,
			mk->data + info.data_start,
			mk->data_length - info.data_start, metadata);
		if (ok) {
			return wu_ok;
		}
		break;
	case icc_marker:
		if (add_icc_shard(icc, mk)) {
			return wu_ok;
		}
		break;
	case mpo_marker:
		if (!is_first) {
			/* MPO offsets are relative to the MPO marker, so we
			 * need to parse the whole file again. */
			const size_t nr = search_file_offsets(infile->ifp, js);
			if (nr > 1) {
				if (!realloc_sub_images(infile, nr)) {
					return wu_alloc_error;
				}
			}
		}
		return wu_ok;
	default:
		break;
	}

	struct wu_tree *branch = tree_add_branch(metadata, "Marker");
	if (branch) {
		char app[] = "APPXXX";
		sprintf(app + 3, "%hhu", (uint8_t)(mk->marker - JPEG_APP0));

		tree_add_leaf_utf8(branch, "Type", app);
		tree_bud_leaf(branch, "Size",
			(struct wu_leaf){
				.val.u = mk->data_length,
				.type = wu_leaf_unsigned
			});
		tree_add_leaf_len(branch, "Data start", mk->data,
			zumin(12, mk->data_length), NULL);
	}
	return wu_ok;
}

static enum wu_error iter_markers(const struct jpeg_marker_struct *mk,
struct image_file *infile, struct raw_img *img, struct jpeg_state *js,
const bool is_first) {
	struct icc_assembler icc = {0};
	enum wu_error status = wu_ok;
	struct wu_tree *metadata = raw_img_get_metadata(img);
	if (!img->metadata) {
		return wu_alloc_error;
	}
	while (mk) {
		if (mk->marker == JPEG_COM) {
			tree_add_leaf_len(metadata, "Comment", mk->data,
				mk->data_length, NULL);
		} else {
			status = parse_markers(mk, metadata, infile, js, &icc,
				is_first);
			if (status != wu_ok) {
				break;
			}
		}
		mk = mk->next;
	}
	if (icc.shards) {
		if (status == wu_ok) {
			assemble_icc(img, &icc);
		}
		free(icc.shards);
	}
	return status;
}

static void decode_raw(struct raw_img *img,
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

static bool set_colorspace(struct raw_img *img,
const struct jpeg_decompress_struct *dinfo) {
	img->alpha = alpha_key;
	switch (dinfo->jpeg_color_space) {
	case JCS_YCCK:
	case JCS_YCbCr:
	case JCS_GRAYSCALE:
		img->cs.matrix = cicp_matrix_bt601_7;
		break;
	case JCS_UNKNOWN:
	case JCS_RGB:
	case JCS_CMYK:
		break;
	default:
		break;
	}

	struct image_planes *planes = raw_img_plane_init(img);
	if (!planes) {
		return false;
	}
	planes->v_pad = align_from_int(DCTSIZE);
	const jpeg_component_info *nfo = dinfo->comp_info;
	for (uint8_t i = 0; i < img->channels; ++i) {
		const int xsamp = dinfo->max_h_samp_factor
			/ nfo[i].h_samp_factor;
		const int ysamp = dinfo->max_v_samp_factor
			/ nfo[i].v_samp_factor;
		planes->p[i].x.subsamp = (uint8_t)xsamp;
		planes->p[i].y.subsamp = (uint8_t)ysamp;
	}
	return true;
}

static enum wu_error decode_img(struct image_file *infile,
const struct wu_conf *wuconf, const int i) {
	struct jpeg_state *js = infile->dec_state;
	const int val = setjmp(js->jmp);
	if (val) {
		return (enum wu_error)val;
	}

	struct jpeg_decompress_struct *dinfo = &js->dinfo;

	const long pos = i ? js->soi_offsets[i-1] : 0;
	fseek(infile->ifp, pos, SEEK_SET);
	jpeg_stdio_src(dinfo, infile->ifp);

	struct raw_img *img = infile->sub_img + i;
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
	if (wuconf->jpeg_fast_dct) {
		dinfo->dct_method = JDCT_FASTEST;
	}

	jpeg_start_decompress(dinfo);

	img->w = dinfo->output_width;
	img->h = dinfo->output_height;
	img->channels = (unsigned char)dinfo->output_components;
	img->bitdepth = 8;
	raw_img_align(img, DCTSIZE);
	if (!set_colorspace(img, dinfo)) {
		return wu_alloc_error;
	}
	if (raw_img_exceeds_limit(img, wuconf)) {
		return wu_exceeds_size_limit;
	}

	enum wu_error status = raw_img_alloc(img);
	if (status != wu_ok) {
		return status;
	}
	decode_raw(img, dinfo);

	if (dinfo->marker_list) {
		status = iter_markers(dinfo->marker_list, infile, img, js,
			i == 0);
	}
	jpeg_finish_decompress(dinfo);
	if (status == wu_ok && img->metadata) {
		raw_img_exif_orientation(img,
			metadata_orientation(img->metadata));
	}
	return status;
}

enum wu_error jpeg_callback(struct image_file *infile,
const struct wu_conf *wuconf, struct wu_state *state,
const enum image_event ev) {
	if (ev) {
		if (!infile->sub_img[state->idx].data) {
			return decode_img(infile, wuconf, state->idx);
		}
	} else {
		clean_jpeg_state(infile);
	}
	return wu_no_change;
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
	js->jerr.error_exit = jerror_exit;
	js->jerr.output_message = joutput_message;
	js->soi_offsets = NULL;

	jpeg_create_decompress(&js->dinfo);

	const enum wu_error status = decode_img(infile, wuconf, 0);
	if (status == wu_ok && infile->nr > 1) {
		infile->events = ev_subcycle;
	} else {
		clean_jpeg_state(infile);
	}
	return status;
}
