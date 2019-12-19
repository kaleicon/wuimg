#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gif_lib.h>

#include "wudefs.h"
#include "common.h"
#include "anim_common.h"

enum disposal_mode {
	first_frame = -1,
	unspecified = DISPOSAL_UNSPECIFIED,
	dispose_do_not = DISPOSE_DO_NOT,
	dispose_background = DISPOSE_BACKGROUND,
	dispose_previous = DISPOSE_PREVIOUS,
};

// For straight copying
struct gif_fast_palette {
	unsigned char red;
	unsigned char green;
	unsigned char blue;
	unsigned char alpha; // Always 0xff
};

struct gif_frame {
	GifImageDesc *desc;
	GifByteType *raster;
	struct gif_fast_palette *palette;
	int alpha_idx;
	int __padding__;
};

static enum wu_error_type map_error_to_wu(const int e) {
	switch (e) {
//	case D_GIF_SUCCEEDED:
//		return "All OK";
	case D_GIF_ERR_OPEN_FAILED:
		return wu_open_error;
	case D_GIF_ERR_READ_FAILED:
		return wu_open_error;
	case D_GIF_ERR_NOT_GIF_FILE:
		return wu_invalid_sig;
	case D_GIF_ERR_NO_SCRN_DSCR:
		return wu_invalid_header;
	case D_GIF_ERR_NO_IMAG_DSCR:
		return wu_invalid_header;
	case D_GIF_ERR_NO_COLOR_MAP:
		return wu_invalid_header;
	case D_GIF_ERR_WRONG_RECORD:
		return wu_invalid_header;
	case D_GIF_ERR_DATA_TOO_BIG:
		return wu_decoding_error;
	case D_GIF_ERR_NOT_ENOUGH_MEM:
		return wu_alloc_error;
	case D_GIF_ERR_CLOSE_FAILED:
		return wu_unknown_error;
	case D_GIF_ERR_NOT_READABLE:
		return wu_open_error;
	case D_GIF_ERR_IMAGE_DEFECT:
		return wu_decoding_error;
	case D_GIF_ERR_EOF_TOO_SOON:
		return wu_unexpected_eof;
	}
	return wu_unknown_error;
}

static void palette_to_color(unsigned char *restrict out,
const struct gif_frame *frame, const size_t len, const size_t ch) {
	const GifByteType *raster = frame->raster;
	const struct gif_fast_palette *pal = frame->palette;

	// This is the exact same code in both cases. Do not merge them.
	if (ch == 3) {
		if (frame->alpha_idx == -1) {
			for (size_t j = 0; j < len; ++j) {
				memcpy(out + j*ch, &pal[raster[j]], ch);
			}
		} else {
			const unsigned char alpha = (unsigned char)frame->alpha_idx;
			for (size_t j = 0; j < len; ++j) {
				if (raster[j] != alpha) {
					memcpy(out + j*ch, &pal[raster[j]], ch);
				}
			}
		}
	} else if (ch == 4) {
		if (frame->alpha_idx == -1) {
			for (size_t j = 0; j < len; ++j) {
				memcpy(out + j*ch, &pal[raster[j]], ch);
			}
		} else {
			const unsigned char alpha = (unsigned char)frame->alpha_idx;
			for (size_t j = 0; j < len; ++j) {
				if (raster[j] != alpha) {
					memcpy(out + j*ch, &pal[raster[j]], ch);
				}
			}
		}
	}
}

// (size_t)
static void composite_frame(struct raw_img *img, struct gif_frame *frame) {
	const GifImageDesc *desc = frame->desc;
	size_t offset = ((size_t)desc->Top * img->w + (size_t)desc->Left)
		* img->channels;

	if ((size_t)desc->Width == img->w) {
		palette_to_color(img->data + offset, frame,
			(size_t)(desc->Width * desc->Height), img->channels);
	} else {
		for (int i = 0; i < desc->Height; ++i) {
			palette_to_color(img->data + offset, frame,
				(size_t)desc->Width, img->channels);
			frame->raster += desc->Width;
			offset += img->w * img->channels;
		}
	}
}

// (size_t)(int)
static void composite_clear(struct raw_img *img, const GifImageDesc *desc) {
	size_t offset = ((size_t)desc->Top * img->w + (size_t)desc->Left)
		* img->channels;

	if ((size_t)desc->Width == img->w) {
		memset(img->data + offset, 0,
			(size_t)(desc->Width * desc->Height) * img->channels);
	} else {
		for (int i = 0; i < desc->Height; ++i) {
			memset(img->data + offset, 0,
				(size_t)desc->Width * img->channels);
			offset += img->w * img->channels;
		}
	}
}

// (size_t)((size_t)(int)((unsigned char)((size_t)(float)((long long int:18))))
static void _copy_unaffected(struct raw_img *img, const unsigned char *prev,
const GifImageDesc *desc) {
	size_t start = ((size_t)desc->Top * img->w + (size_t)desc->Left)
		* img->channels;
	const size_t skip_stride = (size_t)desc->Width * img->channels;
	const size_t copy_stride = (img->w - (size_t)desc->Width)
		* img->channels;
	const size_t end = (img->w * img->h - ((size_t)(desc->Height - 1)
		* img->w + (size_t)desc->Width)) * img->channels - start;

	memcpy(img->data, prev, start);
	if (copy_stride == 0) {
		start += (size_t)(desc->Width * desc->Height) * img->channels;
	} else {
		start += skip_stride;
		for (int i = 0; i < desc->Height - 1; ++i) {
			memcpy(img->data + start, prev + start, copy_stride);
			start += img->w * img->channels;
		}
	}
	memcpy(img->data + start, prev + start, end);
}

static void optimize_palette(const ColorMapObject *gif_map,
struct gif_fast_palette *opt_pal) {
	const GifColorType *pal = gif_map->Colors;
	for (int i = 0; i < gif_map->ColorCount; ++i) {
		memcpy(&opt_pal[i], &pal[i], 3);
		opt_pal[i].alpha = 0xff;
	}
}

static int read_extensions(const int count, ExtensionBlock *ext,
GraphicsControlBlock *gcb) {
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
			print_unsafe_data(ext[j].Bytes, len);
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

#define DEFAULT_DELAY 10
static unsigned int compute_properties(GifFileType *gif_file,
GraphicsControlBlock **file_gcb) {
	const int count = gif_file->ImageCount;
	GraphicsControlBlock *gcb = malloc(sizeof(GraphicsControlBlock)
		* (size_t)count);
	int global_colors = -1;
	if (gif_file->SColorMap) {
		global_colors = gif_file->SColorMap->ColorCount;
	}

	enum disposal_mode dispose = first_frame;
	unsigned int channels = 3;
	SavedImage *image = gif_file->SavedImages;
	for (int i = 0; i < count; ++i) {
		const int block_count = image[i].ExtensionBlockCount;
		const int gcb_status = read_extensions(block_count,
			image[i].ExtensionBlocks, &gcb[i]);
		if (gcb_status != GIF_OK) {
			gcb[i].DisposalMode = unspecified;
			gcb[i].UserInputFlag = 0;
			gcb[i].DelayTime = DEFAULT_DELAY;
			gcb[i].TransparentColor = NO_TRANSPARENT_COLOR;
		} else if (gcb[i].DelayTime == 0 && gcb[i].UserInputFlag == 0) {
			gcb[i].DelayTime = DEFAULT_DELAY;
		}

		/* Exhaustive tests for alpha.
		 * Operating on three channels instead of four speeds up
		 * mallocing and decoding significatively. Knowing a defined
		 * alpha index isn't actually used gets rid of branches. */
		const GifImageDesc *desc = &image[i].ImageDesc;
		if (channels == 3) {
			int alpha_idx = gcb[i].TransparentColor;
			if (desc->ColorMap) {
				if (alpha_idx > desc->ColorMap->ColorCount) {
					puts("alpha outside local palette");
					alpha_idx = -1;
				}
			} else if (alpha_idx > global_colors) {
				// Warn because I've never seen this happen
				puts("alpha outside global palette");
				alpha_idx = -1;
			}

			if (alpha_idx != -1) {
				const size_t len = (size_t)(desc->Width
					* desc->Height);
				if (!memchr(image[i].RasterBits, alpha_idx, len)) {
					alpha_idx = -1;
				}
			}

			if (dispose == dispose_background && alpha_idx == -1) {
				if (!is_covered(desc, &image[i-1].ImageDesc)) {
					channels = 4;
				} else {
					puts("Background was covered, this"
						" rare test worked.");
				}
			}
			dispose = gcb[i].DisposalMode;
			gcb[i].TransparentColor = alpha_idx;

			if (i == 0) {
				const int canvas = gif_file->SWidth
					* gif_file->SHeight;
				const int frame = desc->Width * desc->Height;
				if (frame < canvas) {
					channels = 4;
				} else if (alpha_idx != -1) {
					channels = 4;
				}
			}
		}
	}

	*file_gcb = gcb;
	return channels;
}

enum wu_error_type gif_dec(struct image_file *infile) {
	int error = 0;
	GifFileType *gif_file = DGifOpenFileName(infile->name, &error);
	if (error) {
		infile->err_msg = strdup(GifErrorString(error));
		return map_error_to_wu(error);
	}

	error = DGifSlurp(gif_file);
	if (error != GIF_OK) {
		infile->err_msg = strdup(GifErrorString(gif_file->Error));
		return map_error_to_wu(gif_file->Error);
	}

	GraphicsControlBlock *gcb;
	const unsigned int channels = compute_properties(gif_file, &gcb);

	alloc_sub_images(infile, (size_t)gif_file->ImageCount);
	if (infile->nr > 1) {
		infile->is_animation = true;
	}

	if (gif_file->AspectByte) {
		printf("-Pixel aspect ratio: %f ((n + 15.0)/64.0, n = %d)\n",
			((float)gif_file->AspectByte + 15.0)/64.0,
			gif_file->AspectByte);
	}

	/* There's disagreement on how to interpret the background color field.
	 * Supposedly, it is the solid color of the "canvas" on which the
	 * contained images are displayed. However, some programs render it
	 * transparent instead, web browsers notably. In the interests of
	 * speed we will do the same, and save us the expense of sorting out
	 * how the alpha index and the background index are meant to interact
	 * when both a global palette and a local palette are present. */

	struct gif_fast_palette *global_fast_pal = NULL;
	struct gif_fast_palette *local_fast_pal = malloc(
		sizeof(struct gif_fast_palette) * 255);
	if (gif_file->SColorMap) {
		global_fast_pal = malloc(sizeof(struct gif_fast_palette) * 255);
		optimize_palette(gif_file->SColorMap, global_fast_pal);
	}

	enum disposal_mode dispose = first_frame;
	int prev = 0;
	GifImageDesc *dispose_bg_desc = NULL;
	const size_t canvas_size = (size_t)(gif_file->SWidth * gif_file->SHeight);
	struct raw_img *img = infile->sub_img;
	for (int i = 0; i < gif_file->ImageCount; ++i) {
		img[i].data = malloc(canvas_size * channels);
		img[i].w = (unsigned int)gif_file->SWidth;
		img[i].h = (unsigned int)gif_file->SHeight;
		img[i].channels = (unsigned char)channels;
		/* Supossedly, the field SColorResolution tells how many bits
		 * the "palette used to create the original image" had.
		 * This value, being only informational, is often wrong. */
		img[i].bitdepth = 8;
		img[i].msec = gcb[i].DelayTime * 10;

		SavedImage *gif_image = gif_file->SavedImages;
		struct gif_frame frame = {
			.desc = &gif_image[i].ImageDesc,
			.raster = gif_image[i].RasterBits,
			.palette = global_fast_pal,
			.alpha_idx = gcb[i].TransparentColor,
		};

		if (frame.desc->ColorMap) {
			optimize_palette(frame.desc->ColorMap,
				local_fast_pal);
			frame.palette = local_fast_pal;
		}

		switch (dispose) {
		case first_frame:
			if (channels == 4) {
				memset(img[i].data, 0, canvas_size * channels);
			}
			composite_frame(&img[i], &frame);
			break;
		case dispose_background:
			_copy_unaffected(&img[i], img[prev].data,
				dispose_bg_desc);
			composite_clear(&img[i], dispose_bg_desc);
			composite_frame(&img[i], &frame);
			break;
		case dispose_do_not:
		case dispose_previous:
		case unspecified:
			memcpy(img[i].data, img[prev].data,
				canvas_size * channels);
			composite_frame(&img[i], &frame);
		}

		dispose = gcb[i].DisposalMode;
		switch (dispose) {
		case dispose_background:
			dispose_bg_desc = frame.desc;
			// Fallthrough
		case dispose_do_not:
		case unspecified:
			prev = i;
			break;
		default:
			break;
		}
	}

	free(global_fast_pal);
	free(local_fast_pal);
	free(gcb);
	DGifCloseFile(gif_file, &error);
	if (error) {
		infile->err_msg = strdup(GifErrorString(error));
		return map_error_to_wu(error);
	}
	return wu_ok;
}
