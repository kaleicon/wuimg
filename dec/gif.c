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

struct gif_disposal_prev {
	unsigned char *buf;
	int n;
	int idx;
	bool written;
	bool rolling;
};

struct gif_state {
	GifFileType *gif_file;
	GraphicsControlBlock *gcb;
	struct gif_disposal_prev previous;
	int idx;
	unsigned char comps;
	bool opaque_first_frame;
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
	free(ds->previous.buf);
	free(ds->gcb);
	free(ds);

	infile->dec_state = NULL;
	infile->events = 0;
}

static void copy_stride(unsigned char *restrict out,
const GifByteType *restrict raster, const struct colormap *pal,
const size_t stride, const unsigned char ch) {
	if (ch == 1) {
		memcpy(out, raster, stride);
	} else {
		strip_colormap(out, raster, pal, stride, 1, 1, ch, 8);
	}
}

static void alpha_pal(unsigned char *restrict out,
const GifByteType *restrict raster, const struct colormap *pal,
size_t len, const unsigned char ch, const unsigned char alpha_idx) {
	const GifByteType *restrict alpha;
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

static void composite_color_frame(struct raw_img *img,
const struct gif_frame *frame, const unsigned char ch) {
	const struct anim_frame *geom = &frame->geom;

	size_t offset = (geom->y * img->w + geom->x) * ch;
	size_t raster_offset = 0;
	const GifByteType *raster = frame->raster;
	for (size_t i = 0; i < geom->h; ++i) {
		palette_to_color(img->data + offset, raster + raster_offset,
			frame->palette, geom->w, ch, frame->alpha_idx);
		raster_offset += geom->w;
		offset += img->w * ch;
	}
}

static void expand_palette(const ColorMapObject *gif_map,
struct colormap *out_pal, int alpha_idx) {
	const GifColorType *pal = gif_map->Colors;
	for (int i = 0; i < gif_map->ColorCount; ++i) {
		memcpy(out_pal + i, pal + i, 3);
		out_pal[i].a = 0xff;
	}
	if (alpha_idx > -1) {
		out_pal[alpha_idx].a = 0x00;
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
		expand_palette(gif_image[i].ImageDesc.ColorMap, ds->local_pal,
			fr->alpha_idx);
		fr->palette = ds->local_pal;
	} else {
		fr->palette = ds->global_pal;
	}
}

static bool cache_prevbuf(struct gif_state *ds) {
	return ds->previous.n > 1 || !ds->previous.written;
}

static enum wu_error gif_dec_frame(struct raw_img *img,
const struct wu_conf *wuconf, struct gif_state *ds) {
	const GraphicsControlBlock *gcb = ds->gcb + ds->idx;
	int i = 0;
	if (wuconf->cache_frames) {
		i = ds->idx;
		if (img[i].data) {
			return wu_ok;
		} else if (i > 0 && img[0].palette) {
			const size_t size = sizeof(ds->global_pal);
			img[i].palette = memdup(img[0].palette, size);
			if (!img[i].palette) {
				return wu_alloc_error;
			}
		}
	} else {
		img[i].msec = gcb->DelayTime * 10;
	}

	const unsigned char comps = ds->comps;
	const size_t image_size = img[i].w * img[i].h * img[i].channels;
	if (!img[i].data) {
		img[i].data = malloc(image_size);
		if (!img[i].data) {
			return wu_alloc_error;
		}
	}

	const int fill = img[i].palette ? ds->gif_file->SBackGroundColor : 0;
	if (ds->idx == 0) {
		if (!ds->opaque_first_frame) {
			memset(img[i].data, fill, image_size);
		}
		if (gcb->DisposalMode == dispose_previous && cache_prevbuf(ds)) {
			memset(ds->previous.buf, fill, image_size);
			ds->previous.written = true;
			ds->previous.rolling = true;
		}
	} else {
		if (gcb->DisposalMode == dispose_previous) {
			if (cache_prevbuf(ds) && !ds->previous.rolling) {
				memcpy(ds->previous.buf, img[i].data, image_size);
				ds->previous.written = true;
				ds->previous.rolling = true;
			}
		} else {
			ds->previous.rolling = false;
		}

		int p = ds->idx - 1;
		switch (gcb[-1].DisposalMode) {
		case dispose_background:;
			const struct anim_frame geom = gif_desc_to_frame(
				&ds->gif_file->SavedImages[p].ImageDesc);
			if (wuconf->cache_frames) {
				copy_unaffected(img + i, img[p].data, &geom);
			}
			composite_clear(img + i, &geom, fill);
			break;
		case dispose_previous:
			memcpy(img[i].data, ds->previous.buf, image_size);
			break;
		case dispose_do_not:
		case unspecified:
			if (wuconf->cache_frames) {
				memcpy(img[i].data, img[p].data, image_size);
			}
		}
	}
	struct gif_frame frame;
	init_gif_frame(&frame, ds);
	composite_color_frame(img + i, &frame, comps);

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
		ds->idx = 0;
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
				ds->idx = 0;
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

static GraphicsControlBlock * gather_info(GifFileType *gif_file,
struct gif_state *ds, bool *uses_local_palette, struct wu_tree *tree) {
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

	int prev_disposals = 0;
	const int DEFAULT_DELAY = 10;
	enum disposal_mode dispose = first_frame;
	SavedImage *image = gif_file->SavedImages;
	for (int i = 0; i < count; ++i) {
		const int block_count = image[i].ExtensionBlockCount;
		const int gcb_status = read_extensions(block_count,
			image[i].ExtensionBlocks, gcb + i, tree);
		if (gcb_status != GIF_OK) {
			gcb[i].DisposalMode = unspecified;
			gcb[i].UserInputFlag = 0;
			gcb[i].DelayTime = DEFAULT_DELAY;
			gcb[i].TransparentColor = NO_TRANSPARENT_COLOR;
		} else if (gcb[i].DelayTime == 0 && gcb[i].UserInputFlag == 0) {
			gcb[i].DelayTime = DEFAULT_DELAY;
		}

		const GifImageDesc *desc = &image[i].ImageDesc;
		if (desc->ColorMap) {
			*uses_local_palette = true;
		}

		if (gcb[i].DisposalMode == dispose_previous) {
			++prev_disposals;
		}

		int alpha_idx = gcb[i].TransparentColor;
		// Some quick tests for alpha.
		if (ds->comps == 3 && alpha_idx != -1) {

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
					puts("alpha index not in raster");
					alpha_idx = -1;
				}
			}

			/* For a frame disposed to background, check if the
			 * area is fully covered. */
			if (dispose == dispose_background && alpha_idx == -1) {
				if (!is_covered(desc, &image[i-1].ImageDesc)) {
					ds->comps = 4;
				}
			}

			dispose = gcb[i].DisposalMode;
			gcb[i].TransparentColor = alpha_idx;
		}

		if (i == 0) {
			const int canvas = gif_file->SWidth * gif_file->SHeight;
			const int frame = desc->Width * desc->Height;
			if (frame < canvas || alpha_idx != -1) {
				ds->opaque_first_frame = false;
				ds->comps = 4;
			}
		}
	}

	if (prev_disposals) {
		puts("HAS PREV DISPOSALS AAAAAAAAAAAA");
		const size_t dims = (size_t)gif_file->SWidth
			* (size_t)gif_file->SHeight * ds->comps;
		ds->previous.buf = malloc(dims);
		if (!ds->previous.buf) {
			free(gcb);
			return NULL;
		}
		ds->previous.n = prev_disposals;
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

	infile->is_animation = (gif_file->ImageCount > 1);
	infile->nr = wuconf->cache_frames ? (size_t)gif_file->ImageCount : 1;

	bool uses_local_palette = false;
	ds->opaque_first_frame = true;
	ds->comps = 3 + !wuconf->anim_space_over_speed;
	ds->gcb = gather_info(gif_file, ds, &uses_local_palette,
		&infile->metadata);
	if (!ds->gcb) {
		free(infile->err_msg);
		clean_gif_state(infile);
		return wu_alloc_error;
	}

	struct raw_img *img = alloc_sub_images(infile, infile->nr);
	if (!img) {
		clean_gif_state(infile);
		return wu_alloc_error;
	}

	if (gif_file->SColorMap) {
		struct colormap *loc = ds->global_pal;
		if (!uses_local_palette) {
			void *hold = malloc(sizeof(ds->global_pal));
			if (hold) {
				loc = hold;
				img[0].palette = hold;
				ds->comps = 1;
			}
		}
		const int alpha_idx = ds->gcb[0].TransparentColor;
		expand_palette(gif_file->SColorMap, loc, alpha_idx);

		const int bg = gif_file->SBackGroundColor;
		if (bg > -1 && bg < gif_file->SColorMap->ColorCount) {
			memcpy(infile->bg, loc + bg, sizeof(infile->bg));
		}
	}

	for (size_t i = 0; i < infile->nr; ++i) {
		img[i].w = (size_t)gif_file->SWidth;
		img[i].h = (size_t)gif_file->SHeight;
		img[i].channels = ds->comps;
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
