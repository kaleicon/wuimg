#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gif_lib.h>

#include "../wudefs.h"
#include "../common.h"
#include "../raster/pal.h"
#include "../raster/composite.h"

enum disposal_mode {
	first_frame = -1,
	unspecified = DISPOSAL_UNSPECIFIED,
	dispose_do_not = DISPOSE_DO_NOT,
	dispose_background = DISPOSE_BACKGROUND,
	dispose_previous = DISPOSE_PREVIOUS,
};

struct gif_frame {
	GifByteType *raster;
	struct raster_pal *palette;
	int alpha_idx;
};

struct gif_disposal_prev {
	bool written;
	bool rolling;
	int num_of_disposals;
	unsigned char *buf;
};

struct gif_state {
	GifFileType *gif_file;
	GraphicsControlBlock *gcb;
	struct gif_disposal_prev previous;
	int idx;
	bool opaque_first_frame;
	struct raster_pal global_pal;
	struct raster_pal local_pal;
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
	free(ds->previous.buf);
	free(ds->gcb);
	free(ds);

	infile->dec_state = NULL;
	infile->events = 0;
}

static void copy_stride(unsigned char *restrict out,
const GifByteType *restrict raster, const struct raster_pal *pal,
const size_t stride, const unsigned char ch) {
	if (ch == 1) {
		memcpy(out, raster, stride);
	} else {
		raster_pal_expand(out, raster, pal, stride, 1, 1, ch, 8);
	}
}

static void alpha_pal(unsigned char *restrict out,
const GifByteType *restrict raster, const struct raster_pal *pal,
size_t len, const unsigned char ch, const unsigned char alpha_idx) {
	const GifByteType *alpha;
	while ((alpha = memchr(raster, alpha_idx, len)) != NULL) {
		const size_t stride = (size_t)(alpha - raster);
		copy_stride(out, raster, pal, stride, ch);
		out += (stride+1) * ch;
		raster += stride + 1;
		len -= stride + 1;

		while (len && *raster == alpha_idx) {
			out += ch;
			++raster;
			--len;
		}
	}
	copy_stride(out, raster, pal, len, ch);
}

static void palette_to_color(void *restrict out,
const GifByteType *restrict raster, const void *pal, const size_t len,
const unsigned char ch, const int alpha_idx) {
	if (alpha_idx == -1) {
		copy_stride(out, raster, pal, len, ch);
	} else {
		const unsigned char alpha = (unsigned char)alpha_idx;
		alpha_pal(out, raster, pal, len, ch, alpha);
	}
}

static void composite_gif_frame(struct raw_img *img,
const struct frame_info *geom, const GifByteType *restrict raster,
const struct raster_pal *palette, const int trans) {
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

static void expand_palette(struct raster_pal *pal,
const ColorMapObject *gif_map, const int alpha_idx) {
	raster_pal_from_rgb8(pal, gif_map->Colors, (size_t)gif_map->ColorCount);
	if (alpha_idx != -1) {
		pal->color[alpha_idx].a = 0x00;
	}
}

static bool should_cache_prev(struct gif_state *ds) {
	return ds->previous.num_of_disposals > 1 || !ds->previous.written;
}

static enum wu_error gif_dec_frame(struct raw_img *img, struct gif_state *ds) {
	const GraphicsControlBlock *gcb = ds->gcb + ds->idx;

	const size_t image_size = img->w * img->h * img->channels;
	if (!img->data) {
		img->data = malloc(image_size);
		if (!img->data) {
			return wu_alloc_error;
		}
	}

	const int trans = gcb->TransparentColor;
	int fill = 0;
	if (img->u.palette) {
		fill = (trans > -1) ? trans : ds->gif_file->SBackGroundColor;
	}

	if (ds->idx == 0) {
		if (!ds->opaque_first_frame) {
			memset(img->data, fill, image_size);
		}
		if (gcb->DisposalMode == dispose_previous && should_cache_prev(ds)) {
			memset(ds->previous.buf, fill, image_size);
			ds->previous.written = true;
			ds->previous.rolling = true;
		}
	} else {
		if (gcb->DisposalMode == dispose_previous) {
			if (should_cache_prev(ds) && !ds->previous.rolling) {
				memcpy(ds->previous.buf, img->data, image_size);
				ds->previous.written = true;
				ds->previous.rolling = true;
			}
		} else {
			ds->previous.rolling = false;
		}

		switch (gcb[-1].DisposalMode) {
		case dispose_background:;
			int p = ds->idx - 1;
			composite_clear(img, img->frames->f + p, fill);
			break;
		case dispose_previous:
			memcpy(img->data, ds->previous.buf, image_size);
			break;
		case dispose_do_not:
		case unspecified:
			break;
		}
	}

	const SavedImage *gif_image = ds->gif_file->SavedImages + ds->idx;
	struct raster_pal *pal;
	if (gif_image->ImageDesc.ColorMap) {
		pal = &ds->local_pal;
		expand_palette(pal, gif_image->ImageDesc.ColorMap, trans);
	} else {
		pal = &ds->global_pal;
	}
	composite_gif_frame(img, img->frames->f + ds->idx,
		gif_image->RasterBits, pal, trans);

	++ds->idx;
	return wu_ok;
}

static enum wu_error gif_frame_iter(struct image_file *infile,
const struct wu_state *state) {
	struct gif_state *ds = infile->dec_state;

	if (state->frame < ds->idx) {
		ds->idx = 0;
	}
	while (ds->idx <= state->frame) {
		const enum wu_error err = gif_dec_frame(infile->sub_img, ds);
		if (err != wu_ok) {
			clean_gif_state(infile);
			return err;
		}
	}
	return wu_ok;
}

enum wu_error gif_callback(struct image_file *infile,
const struct wu_conf *wuconf, struct wu_state *state,
const enum image_event event) {
	(void)wuconf;
	if (event == ev_frame) {
		return gif_frame_iter(infile, state);
	} else if (event == 0) {
		clean_gif_state(infile);
	}
	return wu_no_change;
}

static struct frame_info gif_desc_to_frame(const GifImageDesc *desc,
const GraphicsControlBlock *gcb) {
	return (struct frame_info) {
		.x = (size_t)desc->Left,
		.y = (size_t)desc->Top,
		.w = (size_t)desc->Width,
		.h = (size_t)desc->Height,
		.msec = gcb->DelayTime * 10,
	};
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

static bool gather_info(struct image_file *infile,
struct gif_state *ds, bool *uses_local_palette) {
	GifFileType *gif_file = ds->gif_file;

	const int count = gif_file->ImageCount;

	struct raw_img *img = infile->sub_img;
	struct image_frames *frames = raw_img_alloc_frames(img, (size_t)count);
	if (!frames) {
		return false;
	}

	ds->gcb = malloc(sizeof(*ds->gcb) * (size_t)count);
	if (!ds->gcb) {
		return false;
	}

	int global_colors = -1;
	if (gif_file->SColorMap) {
		global_colors = gif_file->SColorMap->ColorCount;
	}

	const int default_delay = 10;
	enum disposal_mode dispose = first_frame;
	for (int i = 0; i < count; ++i) {
		GraphicsControlBlock *gcb = ds->gcb + i;
		SavedImage *image = gif_file->SavedImages + i;

		const int block_count = image->ExtensionBlockCount;
		const int gcb_status = read_extensions(block_count,
			image->ExtensionBlocks, gcb, &infile->metadata);
		if (gcb_status != GIF_OK) {
			gcb->DisposalMode = unspecified;
			gcb->UserInputFlag = 0;
			gcb->DelayTime = default_delay;
			gcb->TransparentColor = NO_TRANSPARENT_COLOR;
		} else if (gcb->DelayTime == 0) {// && gcb->UserInputFlag == 0) {
			gcb->DelayTime = default_delay;
		}

		const GifImageDesc *desc = &image->ImageDesc;
		frames->f[i] = gif_desc_to_frame(desc, gcb);
		if (desc->ColorMap) {
			*uses_local_palette = true;
		}

		if (gcb->DisposalMode == dispose_previous) {
			++ds->previous.num_of_disposals;
		}

		int alpha_idx = gcb->TransparentColor;
		// Some quick tests for alpha.
		if (img->channels == 3 && alpha_idx != -1) {
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
				if (!memchr(image->RasterBits, alpha_idx, len)) {
					puts("alpha index not in raster");
					alpha_idx = -1;
				}
			}

			/* For a frame disposed to background, check if the
			 * area is fully covered. */
			if (dispose == dispose_background && alpha_idx == -1) {
				if (!is_covered(desc, &image[i-1].ImageDesc)) {
					img->channels = 4;
				}
			}

			dispose = gcb->DisposalMode;
			gcb->TransparentColor = alpha_idx;
		}

		if (i == 0) {
			const int canvas = gif_file->SWidth * gif_file->SHeight;
			const int frame = desc->Width * desc->Height;
			if (frame < canvas || alpha_idx != -1) {
				ds->opaque_first_frame = false;
				img->channels = 4;
			}
		}
	}
	return true;
}

static int dgif_input_fn(GifFileType *gif_file, GifByteType *out, int len) {
	FILE *ifp = gif_file->UserData;
	return (int)fread(out, 1, (size_t)len, ifp);
}

enum wu_error gif_dec(struct image_file *infile,
const struct wu_conf *wuconf) {
	struct gif_state *ds = calloc(1, sizeof(*ds));
	if (!ds) {
		return wu_alloc_error;
	}

	int error = 0;
	GifFileType *gif_file = DGifOpen(infile->ifp, dgif_input_fn, &error);
	if (error) {
		image_file_error_append(infile, GifErrorString(error));
		free(ds);
		return map_error_to_wu(error);
	}
	ds->gif_file = gif_file;
	infile->dec_state = ds;

	error = DGifSlurp(gif_file);
	if (error != GIF_OK) {
		image_file_error_append(infile, GifErrorString(gif_file->Error));
	}

	const unsigned int max_dim = (unsigned int)imax(gif_file->SWidth,
		gif_file->SHeight);
	if (max_dim > wuconf->max_img_size) {
		clean_gif_state(infile);
		return wu_exceeds_size_limit;
	}

	if (gif_file->AspectByte) {
		const char fmt[] = "%g (byte = %hhu)";
		char buf[sizeof(fmt)*2];

		const double ratio = (gif_file->AspectByte + 15.0f)/64.0f;
		const size_t w = (size_t)sprintf(buf, fmt, ratio,
			gif_file->AspectByte);
		tree_sprout_measured_leaf(&infile->metadata,
			"Pixel aspect ratio", buf, w);
	}

	struct raw_img *img = alloc_sub_images(infile, 1);
	if (!img) {
		clean_gif_state(infile);
		return wu_alloc_error;
	}

	img->w = (size_t)gif_file->SWidth;
	img->h = (size_t)gif_file->SHeight;
	img->channels = 3 + !wuconf->anim_space_over_speed;
	img->bitdepth = 8;

	bool uses_local_palette = false;
	ds->opaque_first_frame = true;
	if (!gather_info(infile, ds, &uses_local_palette)) {
		clean_gif_state(infile);
		return wu_alloc_error;
	}

	if (gif_file->SColorMap) {
		struct raster_pal *loc = &ds->global_pal;
		if (!uses_local_palette) {
			void *hold = malloc(sizeof(*loc));
			if (hold) {
				loc = raw_img_set_palette(img, hold);
				img->channels = 1;
			}
		}
		const int alpha_idx = ds->gcb->TransparentColor;
		expand_palette(loc, gif_file->SColorMap, alpha_idx);

		const int bg = gif_file->SBackGroundColor;
		if (bg > -1 && bg < gif_file->SColorMap->ColorCount) {
			infile->bg = loc->color[bg];
		}
	}

	if (ds->previous.num_of_disposals) {
		ds->previous.buf = malloc(img->w * img->h * img->channels);
		if (!ds->previous.buf) {
			return wu_alloc_error;
		}
	}

	const enum wu_error err = gif_dec_frame(img, ds);
	if (err == wu_ok && img->frames) {
		infile->events = ev_frame;
	} else {
		clean_gif_state(infile);
	}
	return err;
}
