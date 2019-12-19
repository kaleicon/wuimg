#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gif_lib.h>

#include "wudefs.h"
#include "common.h"

struct gif_frame {
	GifImageDesc *desc;
	GifByteType *raster;
	GifColorType *palette;
	int alpha_idx;
	int __padding__;
};

static enum wu_error_type map_error_to_wu(int e) {
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
	const GifColorType *pal = frame->palette;

	if (frame->alpha_idx == -1) {
		for (size_t j = 0; j < len; ++j) {
			const GifByteType idx = raster[j];
			out[j*ch] = pal[idx].Red;
			out[j*ch + 1] = pal[idx].Green;
			out[j*ch + 2] = pal[idx].Blue;
			if (ch == 4) {
				out[j*ch + 3] = 0xff;
			}
		}
	} else {
		const unsigned char alpha_idx = (unsigned char)frame->alpha_idx;
		for (size_t j = 0; j < len; ++j) {
			const GifByteType idx = raster[j];
			if (alpha_idx != idx) {
				out[j*ch] = pal[idx].Red;
				out[j*ch + 1] = pal[idx].Green;
				out[j*ch + 2] = pal[idx].Blue;
				if (ch == 4) {
					out[j*ch + 3] = 0xff;
				}
			}
		}
	}
}

static void composite_color(struct raw_img *img, const GifImageDesc *desc,
const unsigned char *restrict color) {
	size_t offset = ((size_t)desc->Top * img->w + (size_t)desc->Left)
		* img->channels;

	for (int i = 0; i < desc->Height; ++i) {
		memset(img->data + offset, color[0], (size_t)(desc->Width * img->channels));
//		color_set(img->data + offset, color,
//			(size_t)desc->Width, img->channels);
		offset += img->w * img->channels;
	}
}

static void composite_frame(struct raw_img *img, struct gif_frame *frame) {
	const GifImageDesc *desc = frame->desc;
	size_t offset = ((size_t)desc->Top * img->w + (size_t)desc->Left)
		* img->channels;

	for (int i = 0; i < desc->Height; ++i) {
		palette_to_color(img->data + offset, frame,
			(size_t)desc->Width, img->channels);
		frame->raster += desc->Width;
		offset += img->w * img->channels;
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

	int status = GIF_OK;
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

static unsigned char compute_properties(const GifFileType *gif_file,
GraphicsControlBlock **file_gcb) {
	const size_t images = (size_t)gif_file->ImageCount;
	GraphicsControlBlock *gcb = malloc(sizeof(GraphicsControlBlock) * images);

	int has_alpha = 0;
	for (size_t i = 0; i < images; ++i) {
		SavedImage *image = gif_file->SavedImages;
		if (i == 0) {
			GifImageDesc *desc = &image[i].ImageDesc;
			int canvas_size = gif_file->SWidth * gif_file->SHeight;
			int image_size = desc->Width * desc->Height;
			if (canvas_size > image_size) {
				has_alpha = 1;
			}
		}

		const int count = image[i].ExtensionBlockCount;
		const int status = read_extensions(count,
			image[i].ExtensionBlocks, &gcb[i]);
		if (status != GIF_OK) {
			gcb[i].DisposalMode = DISPOSAL_UNSPECIFIED;
			gcb[i].UserInputFlag = 0;
			gcb[i].DelayTime = 0;
			gcb[i].TransparentColor = NO_TRANSPARENT_COLOR;
		} else if (!has_alpha) {
			if (gcb[i].DisposalMode == DISPOSE_BACKGROUND) {
				has_alpha = 1;
			}
		}
	}

	*file_gcb = gcb;
	return (unsigned char)(3 + has_alpha);
}

enum wu_error_type gif_dec(struct image_file *infile) {
	int error = 0;
	GifFileType *gif_file = DGifOpenFileName(infile->name, &error);
	if (error) {
		infile->err_msg = strdup(GifErrorString(error));
		return map_error_to_wu(error);
	}

	int success = DGifSlurp(gif_file);
	if (success != GIF_OK) {
		infile->err_msg = strdup(GifErrorString(gif_file->Error));
		return map_error_to_wu(gif_file->Error);
	}

	GraphicsControlBlock *gcb;
	const unsigned char channels = compute_properties(gif_file, &gcb);

	if (gif_file->ImageCount > 1) {
		infile->is_animation = 1;
	}

	alloc_sub_images(infile, (size_t)gif_file->ImageCount);

	if (gif_file->AspectByte) {
		printf("-Pixel aspect ratio: %f ((n + 15.0)/64.0, n = %d)\n",
			((float)gif_file->AspectByte + 15.0)/64.0,
			gif_file->AspectByte);
	}

	/* There's disagreement on how to interpret the background color field.
	 * Supposedly, it is the solid color of the "canvas" on which the
	 * contained images are displayed. However, some programs make it
	 * transparent instead. Browsers seem to always do this, image viewers
	 * in qt5 seem to respect it UNLESS the first image has transparency.
	 * Same for mpv. Anything else, I can't be bothered to try.
	 *
	 * Because most gif files are probably first seen on a browser anyway,
	 * we will leave it transparent too. This allows us to use plain calloc
	 * and memset and to save on program logic. Very convenient. */
	const unsigned char bg_color[] = {0, 0, 0, 0};

	/* Keep this around in case we ever change our mind. */
/*	if (gif_file->SColorMap) {
		int bg = gif_file->SBackGroundColor;
		GifColorType *colors = &gif_file->SColorMap->Colors[bg];
		bg_color[0] = colors->Red;
		bg_color[1] = colors->Green;
		bg_color[2] = colors->Blue;
	}
*/
	enum disposal_types {
		first_frame = -1,
		unspecified = DISPOSAL_UNSPECIFIED,
		dispose_do_not = DISPOSE_DO_NOT,
		dispose_background = DISPOSE_BACKGROUND,
		dispose_previous = DISPOSE_PREVIOUS,
	} dispose = first_frame;
	int prev = 0;
	GifImageDesc *dispose_bg_desc = NULL;
	const size_t canvas_size = (size_t)(gif_file->SWidth * gif_file->SHeight);
	struct raw_img *img = infile->sub_img;
	for (int i = 0; i < gif_file->ImageCount; ++i) {
		if (i == 0) {
			img[i].data = calloc(canvas_size * channels, sizeof(char));
		} else {
			img[i].data = malloc(canvas_size * channels);
		}
		img[i].w = (unsigned int)gif_file->SWidth;
		img[i].h = (unsigned int)gif_file->SHeight;
		img[i].channels = channels;
		/* Supossedly, the field SColorResolution tells how many bits
		 * the "palette used to create the original image" had.
		 * This value, being only informational, is often wrong. */
		img[i].bitdepth = 8;
		img[i].msec = gcb[i].DelayTime * 10;

		SavedImage *gif_image = gif_file->SavedImages;
		struct gif_frame frame = {
			.raster = gif_image[i].RasterBits,
			.desc = &gif_image[i].ImageDesc,
			.palette = NULL,
			.alpha_idx = gcb[i].TransparentColor,
		};

		if (frame.desc->ColorMap) {
			frame.palette = frame.desc->ColorMap->Colors;
		} else {
			frame.palette = gif_file->SColorMap->Colors;
		}

		switch (dispose) {
		case first_frame:
//			color_set(img[i].data, bg_color, canvas_size, channels);
			composite_frame(&img[i], &frame);
			break;
		case dispose_background:
			memcpy(img[i].data, img[prev].data,
				canvas_size * channels);
			composite_color(&img[i], dispose_bg_desc, bg_color);
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

	free(gcb);
	DGifCloseFile(gif_file, &error);
	if (error) {
		infile->err_msg = strdup(GifErrorString(error));
		return map_error_to_wu(error);
	}
	return wu_ok;
}
