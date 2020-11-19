#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gif_lib.h>

#include "../wudefs.h"
#include "../common.h"
#include "lib/common/unpack.h"
#include "lib/common/composite.h"

enum disposal_mode {
	first_frame = -1,
	unspecified = DISPOSAL_UNSPECIFIED,
	dispose_do_not = DISPOSE_DO_NOT,
	dispose_background = DISPOSE_BACKGROUND,
	dispose_previous = DISPOSE_PREVIOUS,
};

struct gif_frame {
	struct anim_frame geom;
	GifByteType *raster;
	struct colormap *palette;
	int alpha_idx;
};

struct gif_state {
	GifFileType *gif_file;
	GraphicsControlBlock *gcb;
	struct anim_frame dispose_bg_geom;
	enum disposal_mode frame_dispose;
	int idx;
	int dispose_prev_idx;
	struct colormap global_pal[256];
	struct colormap local_pal[256];
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

static void clean_gif_state(struct image_file *infile) {
	struct gif_state *ds = infile->dec_state;
	DGifCloseFile(ds->gif_file, NULL);
	free(ds->gcb);
	free(ds);

	infile->dec_state = NULL;
	infile->events = 0;
}

static void rewind_gif_state(struct gif_state *ds) {
	ds->frame_dispose = first_frame;
	ds->idx = 0;
	ds->dispose_prev_idx = 0;
}

static void alpha_pal(unsigned char *restrict out,
const GifByteType *restrict raster, const struct colormap *pal,
size_t len, const unsigned char ch, const int alpha_idx) {
	const GifByteType *restrict alpha;
	while ((alpha = memchr(raster, alpha_idx, len)) != NULL) {
		const size_t stride = (size_t)(alpha - raster);
		strip_colormap(out, raster, pal, stride, 1, 1, ch, 8);
		// +1 because we already know the next byte is transparent
		out += (stride + 1) * ch;
		raster += stride + 1;
		len -= stride + 1;
		while (len && *raster == alpha_idx) {
			out += ch;
			++raster;
			--len;
		}
	}
	strip_colormap(out, raster, pal, len, 1, 1, ch, 8);
}

static void palette_to_color(void *restrict out,
const GifByteType *restrict raster, const void *pal, const size_t len,
const unsigned char ch, const int alpha_idx) {
	if (alpha_idx == -1) {
		strip_colormap(out, raster, pal, len, 1, 1, ch, 8);
	} else {
		if (ch == 4) {
			alpha_pal(out, raster, pal, len, 4, alpha_idx);
		} else {
			alpha_pal(out, raster, pal, len, 3, alpha_idx);
		}
	}
}

static void composite_frame(struct raw_img *img,
const struct gif_frame *frame) {
	const struct anim_frame *geom = &frame->geom;
	unsigned char *offset = img->data
		+ ((geom->y * img->w + geom->x) * img->channels);

//	if (geom->w == img->w) {
//		palette_to_color(offset, frame->raster, frame->palette,
//			geom->w * geom->h, img->channels, frame->alpha_idx);
//	} else {
		unsigned char *raster_offset = frame->raster;
		for (size_t i = 0; i < geom->h; ++i) {
			palette_to_color(offset, raster_offset, frame->palette,
				geom->w, img->channels, frame->alpha_idx);
			raster_offset += geom->w;
			offset += img->w * img->channels;
		}
//	}
}

static void expand_palette(const ColorMapObject *gif_map,
struct colormap *out_pal) {
	const GifColorType *pal = gif_map->Colors;
	for (int i = 0; i < gif_map->ColorCount; ++i) {
		memcpy(out_pal + i, pal + i, 3);
		out_pal[i].a = 0xff;
	}
}

static struct anim_frame gif_desc_to_frame(const GifImageDesc *desc) {
	return (struct anim_frame) {
		.x = (size_t)desc->Left,
		.y = (size_t)desc->Top,
		.w = (size_t)desc->Width,
		.h = (size_t)desc->Height
	};
}

static void init_gif_frame(struct gif_frame *fr, struct gif_state *ds) {
	const int i = ds->idx;
	const SavedImage *gif_image = ds->gif_file->SavedImages;

	fr->geom = gif_desc_to_frame(&gif_image[i].ImageDesc);
	fr->raster = gif_image[i].RasterBits;
	fr->alpha_idx = ds->gcb[i].TransparentColor;
	if (gif_image[i].ImageDesc.ColorMap) {
		expand_palette(gif_image[i].ImageDesc.ColorMap, ds->local_pal);
		fr->palette = ds->local_pal;
	} else {
		fr->palette = ds->global_pal;
	}
}

static enum wu_error gif_dec_frame(struct raw_img *img,
const struct wu_conf *wuconf, struct gif_state *ds) {
	int i;
	if (wuconf->cache_frames) {
		i = ds->idx;
		if (img[i].data) {
			return wu_ok;
		}
	} else {
		i = 0;
		img[i].msec = ds->gcb[ds->idx].DelayTime * 10;
	}

	struct gif_frame frame;
	init_gif_frame(&frame, ds);

	const size_t image_size = img[i].w * img[i].h * img[i].channels;
	if (!img[i].data) {
		img[i].data = malloc(image_size);
		if (!img[i].data) {
			return wu_alloc_error;
		}
	}

	const int prev = ds->dispose_prev_idx;
	switch (ds->frame_dispose) {
	case first_frame:
		if (img[i].channels == 4) {
			memset(img[i].data, 0, image_size);
		}
		composite_frame(&img[i], &frame);
		break;
	case dispose_background:
		if (wuconf->cache_frames) {
			copy_unaffected(&img[i], img[prev].data,
				&ds->dispose_bg_geom);
		}
		composite_clear(&img[i], &ds->dispose_bg_geom);
		composite_frame(&img[i], &frame);
		break;
	case dispose_do_not:
	case dispose_previous:
	case unspecified:
		if (wuconf->cache_frames) {
			memcpy(img[i].data, img[prev].data, image_size);
		}
		composite_frame(&img[i], &frame);
	}

	switch (ds->gcb[ds->idx].DisposalMode) {
	case dispose_background:
		ds->dispose_bg_geom = frame.geom;
		// Fallthrough
	case dispose_do_not:
	case unspecified:
		ds->dispose_prev_idx = ds->idx;
		break;
	case dispose_previous:
	case first_frame:
		break;
	}

	ds->frame_dispose = ds->gcb[ds->idx].DisposalMode;
	++ds->idx;

	return wu_ok;
}

static enum wu_error gif_frame_iter(struct image_file *infile,
const struct wu_conf *wuconf, struct wu_state *state) {
	struct gif_state *ds = infile->dec_state;

	const int image_count = ds->gif_file->ImageCount;
	int iters = imod(state->sub.cycle, image_count);
	if (iters > image_count - ds->idx) {
		iters -= image_count - ds->idx;
		rewind_gif_state(ds);
	}
	for (int i = 0; i < iters; ++i) {
		const enum wu_error err = gif_dec_frame(infile->sub_img,
			wuconf, ds);
		if (err != wu_ok) {
			clean_gif_state(infile);
			return err;
		} else if (ds->idx >= image_count) {
			if (wuconf->cache_frames) {
				clean_gif_state(infile);
				break;
			} else {
				rewind_gif_state(ds);
			}
		}
	}
	return wu_ok;
}


enum wu_error gif_callback(struct image_file *infile,
const struct wu_conf *wuconf, struct wu_state *state,
const enum image_event event) {
	if (event == sub_cycle) {
		return gif_frame_iter(infile, wuconf, state);
	} else if (event == 0) {
		clean_gif_state(infile);
	}
	return wu_ok;
}

static int read_extensions(const int count, ExtensionBlock *ext,
GraphicsControlBlock *gcb, struct wu_tree *tree) {
	enum functions {
		continue_extension = CONTINUE_EXT_FUNC_CODE,
		comment = COMMENT_EXT_FUNC_CODE,
		graphics = GRAPHICS_EXT_FUNC_CODE,
		plaintext = PLAINTEXT_EXT_FUNC_CODE,
		application = APPLICATION_EXT_FUNC_CODE,
	};

	int status = GIF_ERROR;
	for (int j = 0; j < count; ++j) {
		const enum functions func = ext[j].Function;
		const size_t len = (size_t)ext[j].ByteCount;
		switch (func) {
		case comment:
			tree_sprout_unsafe_leaf(tree, "Comment", ext[j].Bytes,
				len);
			break;
		case graphics:
			status = DGifExtensionToGCB(len, ext[j].Bytes, gcb);
			break;
		default:
			break;
		}
	}
	return status;
}

static bool is_covered(const GifImageDesc *restrict desc,
const GifImageDesc *restrict prev) {
	const int w_diff = desc->Width - prev->Width;
	const int h_diff = desc->Height - prev->Height;
	const int x_diff = desc->Left - prev->Left;
	const int y_diff = desc->Top - prev->Top;
	return (w_diff - x_diff >= 0) && (h_diff - y_diff >= 0);
}

static GraphicsControlBlock * read_properties(GifFileType *gif_file,
int *channels, struct wu_tree *tree) {
	const int count = gif_file->ImageCount;
	GraphicsControlBlock *gcb = malloc(sizeof(GraphicsControlBlock)
		* (size_t)count);
	if (!gcb) {
		return NULL;
	}

	int global_colors = -1;
	if (gif_file->SColorMap) {
		global_colors = gif_file->SColorMap->ColorCount;
	}

	const int DEFAULT_DELAY = 10;
	enum disposal_mode dispose = first_frame;
	SavedImage *image = gif_file->SavedImages;
	for (int i = 0; i < count; ++i) {
		const int block_count = image[i].ExtensionBlockCount;
		const int gcb_status = read_extensions(block_count,
			image[i].ExtensionBlocks, &gcb[i], tree);
		if (gcb_status != GIF_OK) {
			gcb[i].DisposalMode = unspecified;
			gcb[i].UserInputFlag = 0;
			gcb[i].DelayTime = DEFAULT_DELAY;
			gcb[i].TransparentColor = NO_TRANSPARENT_COLOR;
		} else if (gcb[i].DelayTime == 0 && gcb[i].UserInputFlag == 0) {
			gcb[i].DelayTime = DEFAULT_DELAY;
		}

		// Exhaustive tests for alpha.
		const GifImageDesc *desc = &image[i].ImageDesc;
		if (*channels == 3) {
			int alpha_idx = gcb[i].TransparentColor;

			// Check if alpha_idx points to an unused index
			if (desc->ColorMap) {
				if (alpha_idx > desc->ColorMap->ColorCount) {
					/* Warn because I want to see if this
					 * ever happens */
					puts("alpha outside local palette");
					alpha_idx = -1;
				}
			} else if (alpha_idx > global_colors) {
				puts("alpha outside global palette");
				alpha_idx = -1;
			}

			/* Check if the transparent index is actually used.
			 * This has catched some cases. */
			if (alpha_idx != -1) {
				const size_t len = (size_t)(desc->Width
					* desc->Height);
				if (!memchr(image[i].RasterBits, alpha_idx, len)) {
					alpha_idx = -1;
				}
			}

			/* For a frame disposed to background, check if the
			 * area is fully covered. */
			if (dispose == dispose_background && alpha_idx == -1) {
				if (!is_covered(desc, &image[i-1].ImageDesc)) {
					*channels = 4;
				}
			}
			dispose = gcb[i].DisposalMode;
			gcb[i].TransparentColor = alpha_idx;

			if (i == 0) {
				const int canvas = gif_file->SWidth
					* gif_file->SHeight;
				const int frame = desc->Width * desc->Height;
				if (frame < canvas) {
					*channels = 4;
				} else if (alpha_idx != -1) {
					*channels = 4;
				}
			}
		}
	}

	return gcb;
}

static int dgif_input_fn(GifFileType *gif_file, GifByteType *out, int len) {
	FILE *ifp = gif_file->UserData;
	return (int)fread(out, 1, (size_t)len, ifp);
}


enum wu_error gif_dec(struct image_file *infile,
const struct wu_conf *wuconf) {
	struct gif_state *ds = calloc(1, sizeof(struct gif_state));
	if (!ds) {
		return wu_alloc_error;
	}

	int error = 0;
	GifFileType *gif_file = DGifOpen(infile->ifp, dgif_input_fn, &error);
	if (error) {
		infile->err_msg = strdup(GifErrorString(error));
		free(ds);
		return map_error_to_wu(error);
	}
	ds->gif_file = gif_file;
	infile->dec_state = ds;

	error = DGifSlurp(gif_file);
	if (error != GIF_OK) {
		infile->err_msg = strdup(GifErrorString(gif_file->Error));
	}

	const unsigned int max_dim = (unsigned int)imax(gif_file->SWidth,
		gif_file->SHeight);
	if (max_dim > wuconf->max_img_size) {
		if (infile->err_msg) {
			free(infile->err_msg);
			infile->err_msg = NULL;
		}
		clean_gif_state(infile);
		return wu_exceeded_size_limit;
	}

	if (gif_file->AspectByte) {
		const char fmt[] = "%g (byte = %hhu)";
		char buf[sizeof(fmt)*2];

		const double ratio = (double)gif_file->AspectByte + 15.0f/64.0f;
		const size_t w = (size_t)sprintf(buf, fmt, ratio,
			gif_file->AspectByte);
		tree_sprout_measured_leaf(&infile->metadata,
			"Pixel aspect ratio", buf, (size_t)w);
	}

	infile->is_animation = (gif_file->ImageCount > 1);
	infile->nr = wuconf->cache_frames ? (size_t)gif_file->ImageCount : 1;

	int channels = (!infile->is_animation || wuconf->anim_space_over_speed)
		? 3 : 4;
	ds->gcb = read_properties(gif_file, &channels, &infile->metadata);
	if (!ds->gcb) {
		free(infile->err_msg);
		clean_gif_state(infile);
		return wu_alloc_error;
	}

	if (gif_file->SColorMap) {
		expand_palette(gif_file->SColorMap, ds->global_pal);
		const int bg = gif_file->SBackGroundColor;
		if (bg >= 0 && bg < gif_file->SColorMap->ColorCount) {
			memcpy(infile->bg, ds->global_pal + bg,
				sizeof(infile->bg));
		}
	}

	rewind_gif_state(ds);
	struct raw_img *img = alloc_sub_images(infile, infile->nr);
	if (!img) {
		clean_gif_state(infile);
		return wu_alloc_error;
	}

	for (size_t i = 0; i < infile->nr; ++i) {
		img[i].w = (size_t)gif_file->SWidth;
		img[i].h = (size_t)gif_file->SHeight;
		img[i].channels = (unsigned char)channels;
		img[i].bitdepth = 8;
		img[i].msec = ds->gcb[i].DelayTime * 10;
	}

	enum wu_error err = gif_dec_frame(infile->sub_img, wuconf, ds);
	if (err == wu_ok && infile->is_animation) {
		infile->events = sub_cycle;
	} else {
		clean_gif_state(infile);
	}
	return err;
}
