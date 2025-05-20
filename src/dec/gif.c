// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2019 kaleido
#include <gif_lib.h>

#include "wudefs.h"
#include "misc/common.h"
#include "misc/math.h"
#include "raster/pal.h"
#include "raster/compost.h"

struct gif_disposal_prev {
	bool written;
	bool rolling;
	int num_of_disposals;
	unsigned char *buf;
};

struct gif_state {
	GifFileType *gif_file;
	GraphicsControlBlock *gcb;
	size_t image_size;
	struct gif_disposal_prev previous;
	bool opaque_first_frame;
	struct palette global_pal;
	struct palette local_pal;
};

static enum wu_error map_error_to_wu(const int e) {
	switch (e) {
	case D_GIF_SUCCEEDED:
		return wu_ok;
	case D_GIF_ERR_OPEN_FAILED:
	case D_GIF_ERR_READ_FAILED:
	case D_GIF_ERR_NOT_READABLE:
		return wu_open_error;
	case D_GIF_ERR_NOT_GIF_FILE:
		return wu_invalid_signature;
	case D_GIF_ERR_NO_SCRN_DSCR:
	case D_GIF_ERR_NO_IMAG_DSCR:
	case D_GIF_ERR_NO_COLOR_MAP:
	case D_GIF_ERR_WRONG_RECORD:
		return wu_invalid_header;
	case D_GIF_ERR_DATA_TOO_BIG:
	case D_GIF_ERR_IMAGE_DEFECT:
		return wu_decoding_error;
	case D_GIF_ERR_NOT_ENOUGH_MEM:
		return wu_alloc_error;
	case D_GIF_ERR_CLOSE_FAILED:
		return wu_unknown_error;
	case D_GIF_ERR_EOF_TOO_SOON:
		return wu_unexpected_eof;
	}
	return wu_unknown_error;
}

static void gif_end(struct image_file *infile) {
	struct gif_state *ds = infile->dec_state;
	DGifCloseFile(ds->gif_file, NULL);
	free(ds->previous.buf);
	free(ds->gcb);
}

static void copy_stride(unsigned char *restrict out,
const GifByteType *restrict raster, const struct palette *pal,
const size_t stride, const uint8_t ch) {
	if (ch == 1) {
		memcpy(out, raster, stride);
	} else {
		palette_expand(out, raster, pal, stride, 8);
	}
}

static void palette_to_color(unsigned char *restrict out,
const GifByteType *restrict raster, const struct palette *pal, size_t len,
const unsigned char ch, const int alpha_idx) {
	if (alpha_idx != -1) {
		for (;;) {
			const GifByteType *alpha = memchr(raster, alpha_idx,
				len);
			if (!alpha) {
				break;
			}
			const size_t stride = (size_t)(alpha - raster);
			copy_stride(out, raster, pal, stride, ch);
			out += (stride+1) * ch;
			raster += (stride+1);
			len -= (stride+1);

			while (len && *raster == alpha_idx) {
				out += ch;
				++raster;
				--len;
			}
		}
	}
	copy_stride(out, raster, pal, len, ch);
}

static void compost_gif_frame(struct wuimg *img,
const struct compost *geom, const GifByteType *restrict raster,
const struct palette *palette, const int trans) {
	const unsigned char ch = img->channels;

	size_t offset = (geom->y * img->w + geom->x) * ch;
	size_t raster_offset = 0;
	for (size_t i = 0; i < geom->h; ++i) {
		palette_to_color(img->data + offset, raster + raster_offset,
			palette, geom->w, ch, trans);
		raster_offset += geom->w;
		offset += img->w * ch;
	}
}

static void get_palette(struct palette *pal,
const ColorMapObject *gif_map, const int alpha_idx) {
	palette_from_rgb8(pal, gif_map->Colors, (size_t)gif_map->ColorCount);
	if (alpha_idx != -1) {
		pal->color[alpha_idx].a = 0x00;
	}
}

static struct compost get_region(const struct GifImageDesc *desc) {
	return (struct compost) {
		.x = (size_t)desc->Left, .y = (size_t)desc->Top,
		.w = (size_t)desc->Width, .h = (size_t)desc->Height,
	};
}

static bool should_cache_prev(struct gif_state *ds) {
	return ds->previous.num_of_disposals > 1 || !ds->previous.written;
}

static enum wu_error render_frame(struct wuimg *img, struct gif_state *ds,
const int idx) {
	const GraphicsControlBlock *gcb = ds->gcb + idx;
	const int trans = gcb->TransparentColor;
	const int fill = (img->mode == image_mode_palette) ? trans : 0;
	if (idx == 0) {
		if (!ds->opaque_first_frame) {
			memset(img->data, fill, ds->image_size);
		}
		if (gcb->DisposalMode == DISPOSE_PREVIOUS && should_cache_prev(ds)) {
			memset(ds->previous.buf, fill, ds->image_size);
			ds->previous.written = true;
			ds->previous.rolling = true;
		}
	} else {
		if (gcb->DisposalMode == DISPOSE_PREVIOUS) {
			if (should_cache_prev(ds) && !ds->previous.rolling) {
				memcpy(ds->previous.buf, img->data,
					ds->image_size);
				ds->previous.written = true;
				ds->previous.rolling = true;
			}
		} else {
			ds->previous.rolling = false;
		}

		switch (gcb[-1].DisposalMode) {
		case DISPOSE_BACKGROUND:
			;const SavedImage *prev_image = ds->gif_file->SavedImages
				+ idx - 1;
			const struct compost prev = get_region(
				&prev_image->ImageDesc);
			compost_clear(img->data, img->w, img->channels, fill,
				&prev);
			break;
		case DISPOSE_PREVIOUS:
			memcpy(img->data, ds->previous.buf, ds->image_size);
			break;
		case DISPOSE_DO_NOT:
		case DISPOSAL_UNSPECIFIED:
			break;
		}
	}

	const SavedImage *gif_image = ds->gif_file->SavedImages + idx;
	const GifImageDesc *desc = &gif_image->ImageDesc;
	const struct compost reg = get_region(desc);
	struct palette *pal;
	if (img->mode == image_mode_palette) {
		pal = img->u.palette;
	} else if (desc->ColorMap) {
		pal = &ds->local_pal;
		get_palette(pal, desc->ColorMap, trans);
	} else {
		pal = &ds->global_pal;
	}
	compost_gif_frame(img, &reg, gif_image->RasterBits, pal, trans);
	return wu_ok;
}

static enum wu_error gif_callback(struct image_file *infile,
const struct wu_conf *wuconf, struct wu_state *state,
const enum image_event event) {
	(void)wuconf;
	if (event != ev_frame) {
		return wu_no_change;
	}
	struct gif_state *ds = infile->dec_state;
	struct wuimg *img = infile->sub_img;
	int idx = wuimg_frame_prev_nearest(img, img->frames->current,
		state->frame);
	while (idx <= state->frame) {
		const enum wu_error err = render_frame(infile->sub_img, ds, idx);
		++idx;
		if (err != wu_ok) {
			return err;
		}
	}
	img->frames->current = state->frame;
	return wu_ok;
}

static int read_extensions(const int count, ExtensionBlock *ext,
GraphicsControlBlock *gcb, struct wutree *tree) {
	int status = GIF_ERROR;
	for (int j = 0; j < count; ++j) {
		const int func = ext[j].Function;
		const size_t len = (size_t)ext[j].ByteCount;
		switch (func) {
		case COMMENT_EXT_FUNC_CODE:
			tree_add_leaf_len(tree, "Comment",
				wuptr_mem(ext[j].Bytes, len), NULL);
			break;
		case GRAPHICS_EXT_FUNC_CODE:
			status = DGifExtensionToGCB(len, ext[j].Bytes, gcb);
			break;
		default:
			break;
		}
	}
	return status;
}

static enum wu_error gather_info(struct image_file *infile,
struct gif_state *ds, int *pal_num, bool *enable_paletted_mode) {
	GifFileType *gif_file = ds->gif_file;

	const size_t count = (size_t)gif_file->ImageCount;
	struct wuimg *img = infile->sub_img;
	if (!wuimg_frames_init(img, count)) {
		return wu_alloc_error;
	}

	ds->gcb = small_malloc(count, sizeof(*ds->gcb));
	if (!ds->gcb) {
		return wu_alloc_error;
	}

	const int default_delay = 10;
	for (size_t i = 0; i < count; ++i) {
		GraphicsControlBlock *gcb = ds->gcb + i;
		SavedImage *image = gif_file->SavedImages + i;

		const int block_count = image->ExtensionBlockCount;
		const int gcb_status = read_extensions(block_count,
			image->ExtensionBlocks, gcb, &infile->metadata);
		if (gcb_status != GIF_OK) {
			gcb->DisposalMode = DISPOSAL_UNSPECIFIED;
			gcb->UserInputFlag = 0;
			gcb->DelayTime = default_delay;
			gcb->TransparentColor = NO_TRANSPARENT_COLOR;
		} else if (gcb->DelayTime <= 0) {// && gcb->UserInputFlag == 0) {
			gcb->DelayTime = default_delay;
		}

		const GifImageDesc *desc = &image->ImageDesc;
		size_t x = 0, y = 0, w = img->w, h = img->h;
		if (i) {
			x = (size_t)desc->Left;
			y = (size_t)desc->Top;
			w = (size_t)desc->Width;
			h = (size_t)desc->Height;
			switch (gcb[-1].DisposalMode) {
			case DISPOSE_BACKGROUND:
			case DISPOSE_PREVIOUS:
				;const GifImageDesc *prev =
					&gif_file->SavedImages[i-1].ImageDesc;
				x = zumin(x, (size_t)prev->Left);
				y = zumin(y, (size_t)prev->Top);
				w = zumax(w, (size_t)prev->Width);
				h = zumax(h, (size_t)prev->Height);
			}
		}

		const bool valid_frame = wuimg_frame_set(img, i,
			x, y, w, h,
			(uint32_t)gcb->DelayTime, 100,
			gcb->TransparentColor == NO_TRANSPARENT_COLOR);
		if (!valid_frame) {
			return wu_invalid_header;
		}
		if (desc->ColorMap) {
			*pal_num += 1;
		}

		if (gcb->DisposalMode == DISPOSE_PREVIOUS) {
			++ds->previous.num_of_disposals;
		}

		if (i == 0) {
			ds->opaque_first_frame = img->frames->f[0].keyframe;
		} else {
			*enable_paletted_mode = *enable_paletted_mode
				&& gcb->TransparentColor == gcb[-1].TransparentColor;
		}
	}
	return wu_ok;
}

static int dgif_input_fn(GifFileType *gif_file, GifByteType *out, int len) {
	FILE *ifp = gif_file->UserData;
	return (int)fread(out, 1, (size_t)len, ifp);
}

static GifWord gmax(const GifWord x, const GifWord y) {
	return x > y ? x : y;
}

static enum wu_error actual_canvas_size(struct wuimg *img,
const GifFileType *gif_file, const struct wu_conf *conf) {
	GifWord w = gif_file->SWidth;
	GifWord h = gif_file->SHeight;
	for (int i = 0; i < gif_file->ImageCount; ++i) {
		const GifImageDesc *desc = &gif_file->SavedImages[i].ImageDesc;
		const GifWord ww = desc->Left + desc->Width;
		const GifWord hh = desc->Top + desc->Height;
		if (ww < 0 || hh < 0) {
			return wu_invalid_header;
		}
		w = gmax(w, ww);
		h = gmax(h, hh);
	}
	img->w = (size_t)w;
	img->h = (size_t)h;
	return wuimg_exceeds_limit(img, conf) ? wu_exceeds_size_limit : wu_ok;
}

static enum wu_error gif_dec(struct image_file *infile,
const struct wu_conf *wuconf) {
	struct gif_state *ds = infile->dec_state;
	int error = 0;
	GifFileType *gif_file = DGifOpen(infile->ifp, dgif_input_fn, &error);
	if (error) {
		image_file_strerror_append(infile, GifErrorString(error));
		return map_error_to_wu(error);
	}
	ds->gif_file = gif_file;

	error = DGifSlurp(gif_file);
	if (error != GIF_OK) {
		image_file_strerror_append(infile, GifErrorString(gif_file->Error));
		return wu_invalid_header;
	}

	struct wuimg *img = infile->sub_img;
	enum wu_error st = actual_canvas_size(img, gif_file, wuconf);
	if (st != wu_ok) {
		return wu_exceeds_size_limit;
	}

	img->channels = 4;
	img->bitdepth = 8;
	if (gif_file->AspectByte) {
		wuimg_aspect_ratio(img, gif_file->AspectByte + 15, 64);
	}

	int pal_num = 0;
	bool enable_paletted_mode = true;
	ds->opaque_first_frame = true;
	st = gather_info(infile, ds, &pal_num,
		&enable_paletted_mode);
	if (st != wu_ok) {
		return st;
	}

	if (gif_file->SColorMap) {
		++pal_num;

		struct palette *pal;
		int trans;
		if (pal_num == 1 && enable_paletted_mode) {
			img->channels = 1;
			pal = wuimg_palette_init(img);
			if (!pal) {
				return wu_alloc_error;
			}
			trans = ds->gcb[0].TransparentColor;
		} else {
			pal = &ds->global_pal;
			trans = -1;
		}
		get_palette(pal, gif_file->SColorMap, trans);

		const int bg = gif_file->SBackGroundColor;
		if (bg > -1 && bg < gif_file->SColorMap->ColorCount) {
			infile->bg = pal->color[bg];
		}
	}
	tree_bud_leaf_d(&infile->metadata, "Palettes", pal_num);

	st = wuimg_alloc(img);
	if (st != wu_ok) {
		return st;
	}
	ds->image_size = wuimg_size(img);

	if (ds->previous.num_of_disposals) {
		ds->previous.buf = malloc(ds->image_size);
		if (!ds->previous.buf) {
			return wu_alloc_error;
		}
	}
	return render_frame(img, ds, 0);
}

const struct image_fn gif_fn = {
	.alloc_single = true,
	.state_size = sizeof(struct gif_state),
	.dec = gif_dec,
	.callback = gif_callback,
	.end = gif_end,
};
