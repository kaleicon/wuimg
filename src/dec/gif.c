// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2019 kaleido
#include <stdlib.h>
#include <string.h>

#include <gif_lib.h>

#include "wudefs.h"
#include "misc/common.h"
#include "misc/math.h"
#include "raster/pal.h"
#include "raster/compost.h"

struct gif_restore {
	unsigned char *buf;
	struct compost frame;
	int dispose;
};

struct gif_state {
	GifFileType *gif_file;
	GraphicsControlBlock *gcb;
	struct gif_restore restore;
	struct palette global_pal;
	struct palette local_pal;
};

static enum wu_error map_gif_error(const int e) {
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

static void end_gif(struct image_file *infile) {
	struct gif_state *ds = infile->dec_state;
	DGifCloseFile(ds->gif_file, NULL);
	free(ds->restore.buf);
	free(ds->gcb);
}

static void get_gif_palette(struct palette *pal,
const ColorMapObject *gif_map) {
	palette_from_rgb8(pal, gif_map->Colors, (size_t)gif_map->ColorCount);
}

static struct compost get_gif_region(const struct GifImageDesc *desc) {
	return (struct compost) {
		.x = (size_t)desc->Left, .y = (size_t)desc->Top,
		.w = (size_t)desc->Width, .h = (size_t)desc->Height,
	};
}

static struct wu_st render_gif_frame(struct wuimg *img, struct gif_state *ds,
const int idx) {
	const GraphicsControlBlock *gcb = ds->gcb + idx;
	const SavedImage *gif_image = ds->gif_file->SavedImages + idx;
	const GifImageDesc *desc = &gif_image->ImageDesc;
	img->anim->sec.num = (uint32_t)gcb->DelayTime;
	if (!img->anim->keyframe[idx]) {
		if (idx == 0) {
			memset(img->data, 0, wuimg_size(img));
			ds->restore.dispose = DISPOSAL_UNSPECIFIED;
			img->anim->dt = (struct compost) {
				.w = img->w, .h = img->h,
			};
		} else {
			bool dispose = false;
			switch (ds->restore.dispose) {
			case DISPOSE_BACKGROUND:
				dispose = true;
				compost_clear(&ds->restore.frame, img);
				break;
			case DISPOSE_PREVIOUS:
				dispose = true;
				compost_overwrite(&ds->restore.frame, img,
					ds->restore.buf);
				break;
			}
			if (dispose) {
				compost_affect(&img->anim->dt,
					&ds->restore.frame);
			}
		}
	}

	const struct compost cur = get_gif_region(desc);
	compost_affect(&img->anim->dt, &cur);
	ds->restore.dispose = gcb->DisposalMode;
	ds->restore.frame = cur;
	if (gcb->DisposalMode == DISPOSE_PREVIOUS) {
		compost_extract(&ds->restore.frame, ds->restore.buf, img);
	}

	struct palette *pal;
	if (desc->ColorMap) {
		pal = &ds->local_pal;
		get_gif_palette(pal, desc->ColorMap);
	} else {
		pal = &ds->global_pal;
	}
	compost_pal_expand_idx_ignore(&cur, img, gif_image->RasterBits,
		gcb->TransparentColor, pal);
	return WU_OK;
}

static int read_gif_extensions(const int count, ExtensionBlock *ext,
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

static struct wu_st gather_gif_info(struct image_file *infile,
struct gif_state *ds, int *pal_num) {
	GifFileType *gif_file = ds->gif_file;

	const size_t count = (size_t)gif_file->ImageCount;
	struct wuimg *img = infile->sub_img;
	if (!wuimg_anim_init(img, count, 0, 100)) {
		return WUERR_HERE(wu_alloc_error);
	}

	ds->gcb = small_malloc(count, sizeof(*ds->gcb));
	if (!ds->gcb) {
		return WUERR_HERE(wu_alloc_error);
	}

	const int default_delay = 10;
	int restore_w = 0;
	int restore_h = 0;
	for (size_t i = 0; i < count; ++i) {
		GraphicsControlBlock *gcb = ds->gcb + i;
		SavedImage *image = gif_file->SavedImages + i;

		const int block_count = image->ExtensionBlockCount;
		const int gcb_status = read_gif_extensions(block_count,
			image->ExtensionBlocks, gcb, &infile->metadata);
		if (gcb_status != GIF_OK) {
			gcb->DisposalMode = DISPOSAL_UNSPECIFIED;
			gcb->UserInputFlag = 0;
			gcb->DelayTime = default_delay;
			gcb->TransparentColor = NO_TRANSPARENT_COLOR;
		}

		const GifImageDesc *desc = &image->ImageDesc;
		const struct compost cur = get_gif_region(desc);
		const bool valid_frame = wuimg_anim_frame_set_checked(img, i,
			&cur,
			gcb->TransparentColor == NO_TRANSPARENT_COLOR
				&& img->w == (size_t)desc->Width
				&& img->h == (size_t)desc->Height);
		if (!valid_frame) {
			return WUERR_HERE(wu_invalid_header);
		}

		if (gcb->DisposalMode == DISPOSE_PREVIOUS) {
			restore_w = imax(restore_w, desc->Width);
			restore_h = imax(restore_h, desc->Height);
		}

		if (desc->ColorMap) {
			*pal_num += 1;
		}
	}
	ds->restore.buf = malloc((size_t)restore_w * (size_t)restore_h * img->channels);
	return ds->restore.buf ? WU_OK : WUERR_HERE(wu_alloc_error);
}

static int dgif_input_fn(GifFileType *gif_file, GifByteType *out, int len) {
	FILE *ifp = gif_file->UserData;
	return (int)fread(out, 1, (size_t)len, ifp);
}

static GifWord gmax(const GifWord x, const GifWord y) {
	return x > y ? x : y;
}

static bool actual_canvas_size(struct wuimg *img, const GifFileType *gif_file) {
	GifWord w = gif_file->SWidth;
	GifWord h = gif_file->SHeight;
	for (int i = 0; i < gif_file->ImageCount; ++i) {
		const GifImageDesc *desc = &gif_file->SavedImages[i].ImageDesc;
		const GifWord ww = desc->Left + desc->Width;
		const GifWord hh = desc->Top + desc->Height;
		if (ww < 0 || hh < 0) {
			return false;
		}
		w = gmax(w, ww);
		h = gmax(h, hh);
	}
	img->w = (size_t)w;
	img->h = (size_t)h;
	return true;
}

static struct wu_st init_gif(struct image_file *infile) {
	struct gif_state *ds = infile->dec_state;
	int error = 0;
	GifFileType *gif_file = DGifOpen(infile->ifp, dgif_input_fn, &error);
	if (error) {
		image_file_strerror_append(infile, GifErrorString(error));
		return WUERR_HERE(map_gif_error(error));
	}
	ds->gif_file = gif_file;

	error = DGifSlurp(gif_file);
	if (error != GIF_OK) {
		image_file_strerror_append(infile, GifErrorString(gif_file->Error));
		return WUERR_HERE(wu_invalid_header);
	}

	struct wuimg *img = infile->sub_img;
	if (!actual_canvas_size(img, gif_file)) {
		return WUERR_HERE(wu_invalid_header);
	}

	img->channels = 4;
	img->bitdepth = 8;
	if (gif_file->AspectByte) {
		wuimg_aspect_ratio(img, gif_file->AspectByte + 15, 64);
	}

	int pal_num = 0;
	struct wu_st st = gather_gif_info(infile, ds, &pal_num);
	if (!wu_isok(st)) {
		return st;
	}

	if (gif_file->SColorMap) {
		++pal_num;

		struct palette *pal = &ds->global_pal;
		get_gif_palette(pal, gif_file->SColorMap);

		const int bg = gif_file->SBackGroundColor;
		if (bg > -1 && bg < gif_file->SColorMap->ColorCount) {
			infile->bg = pal->color[bg];
		}
	}
	tree_bud_leaf_d(&infile->metadata, "Palettes", pal_num);
	return WU_OK;
}

static struct wu_st event_gif(struct image_file *infile,
struct wu_state *state, const enum image_event event) {
	switch (event) {
	case ev_metadata:
		return init_gif(infile);
	case ev_subcycle:
	case ev_frame:
		;struct gif_state *ds = infile->dec_state;
		struct wuimg *img = infile->sub_img;
		wuimg_anim_seek_nearest(img, state->frame);
		struct image_anim *anim = img->anim;
		while (anim->cur < state->frame) {
			++anim->cur;
			const struct wu_st st = render_gif_frame(img, ds,
				anim->cur);
			if (!wu_isok(st)) {
				return st;
			}
		}
		return WU_OK;
	default: break;
	}
	return WU_NO_CHANGE;
}

const struct image_fn gif_fn = {
	.alloc_single = true,
	.alloc_on_subcycle = true,
	.state_size = sizeof(struct gif_state),
	.event = event_gif,
	.end = end_gif,
};
