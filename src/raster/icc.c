// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2022 kaleido
#include <lcms2.h>
#include <lcms2_plugin.h>

#include "icc.h"
#include "misc/mem.h"
#include "misc/mparser.h"
#include "misc/term.h"

struct icc_file {
	int refs;
	struct mparser mp;
	struct _cms_io_handler io;
	cmsHPROFILE in;
};

uint32_t icc_fmt_colorspace(const uint8_t ch, const uint8_t bytedepth,
const enum alpha_interpretation alpha, const uint8_t colorspace) {
	const bool has_alpha = (alpha & alpha_one) == 0;
	return (uint32_t)(PREMUL_SH(alpha == alpha_associated)
		| FLOAT_SH(bytedepth == sizeof(float))
		| EXTRA_SH(has_alpha)
		| COLORSPACE_SH(colorspace)
		| CHANNELS_SH(ch - has_alpha)
		| BYTES_SH(bytedepth));
}

struct wuptr icc_file_get_data(const struct icc_file *icc) {
	return (struct wuptr){.ptr = icc->mp.mem, .len = icc->mp.len};
}

uint32_t icc_fmt(const uint8_t ch, const uint8_t bytedepth,
const enum alpha_interpretation alpha) {
	return icc_fmt_colorspace(ch, bytedepth, alpha,
		alpha == alpha_key ? PT_CMYK : PT_RGB);
}

struct icc_transform * icc_file_create_transform(const struct icc_file *icc,
const struct icc_profile *out, const uint32_t in_fmt, const uint32_t out_fmt) {
	return cmsCreateTransform(icc->in, in_fmt, (cmsHPROFILE)out, out_fmt,
		INTENT_PERCEPTUAL, cmsFLAGS_COPY_ALPHA);
}

static cmsUInt32Number read_fn(struct _cms_io_handler *io, void *buf,
const cmsUInt32Number size, const cmsUInt32Number nmemb) {
	struct mparser *mp = io->stream;
	const void *block = mp_slice(mp, size*nmemb);
	if (block) {
		memcpy(buf, block, size*nmemb);
		return nmemb;
	}
	return 0;
}

static cmsBool seek_fn(struct _cms_io_handler *io, const cmsUInt32Number off) {
	struct mparser *mp = io->stream;
	if (off <= mp->len) {
		mp->pos = off;
		return true;
	}
	return false;
}

static cmsBool close_fn(struct _cms_io_handler *io) {
	struct mparser *mp = io->stream;
	free((void *)mp->mem);
	return true;
}

static cmsUInt32Number tell_fn(struct _cms_io_handler *io) {
	const struct mparser *mp = io->stream;
	return (cmsUInt32Number)mp->pos;
}

static void err_fn(cmsContext id, cmsUInt32Number err, const char *text) {
	(void)id; (void)err;
	term_line_put(text, stderr);
}

static void init_profile(void) {
	cmsSetLogErrorHandler(err_fn);
}

void icc_file_unref(struct icc_file *icc) {
	if (icc->refs) {
		--icc->refs;
	} else {
		cmsCloseProfile(icc->in);
		free(icc);
	}
}

struct icc_file * icc_file_ref(struct icc_file *icc) {
	++icc->refs;
	return icc;
}

struct icc_file * icc_file_mem_own(void *data, const size_t len) {
	struct icc_file *icc = calloc(1, sizeof(*icc));
	if (icc) {
		/* Use a custom IO handler, as cmsOpenProfileFromMem() creates
		 * a memory copy. */
		init_profile();
		icc->mp = mp_mem(len, data);
		icc->io = (struct _cms_io_handler) {
			.stream = &icc->mp,
			//.ContextID = icc->ctx,
			.ReportedSize = (cmsUInt32Number)len,

			.Read = read_fn,
			.Seek = seek_fn,
			.Close = close_fn,
			.Tell = tell_fn,
		};
		icc->in = cmsOpenProfileFromIOhandler2THR(NULL, &icc->io, false);
		if (!icc->in) {
			free(icc);
			return NULL;
		}
	}
	return icc;
}

struct icc_file * icc_file_mem_copy(const void *data, const size_t len) {
	/* Memory is probably owned by the decoder, but we want a copy in case
	 * we need to encode into a format that supports ICC profiles. */
	void *cpy = memdup(data, len);
	if (cpy) {
		return icc_file_mem_own(cpy, len);
	}
	return false;
}
