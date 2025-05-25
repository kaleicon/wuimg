// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2022 kaleido
#include "icc.h"
#include "misc/term.h"

cmsUInt32Number icc_fmt_colorspace(const uint8_t ch, const uint8_t bytedepth,
const enum alpha_interpretation alpha, const uint8_t colorspace) {
	const bool has_alpha = ch % 2 == 0;
	return (cmsUInt32Number)(PREMUL_SH(alpha == alpha_associated)
		| FLOAT_SH(bytedepth == 4)
		| EXTRA_SH(has_alpha)
		| COLORSPACE_SH(colorspace)
		| CHANNELS_SH(ch - has_alpha)
		| BYTES_SH(bytedepth));
}

cmsUInt32Number icc_fmt(const uint8_t ch, const uint8_t bytedepth,
const enum alpha_interpretation alpha) {
	return icc_fmt_colorspace(ch, bytedepth, alpha, PT_ANY);
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

void icc_profile_free(struct icc_profile *icc) {
	if (icc->in) {
		cmsCloseProfile(icc->in);
	}
}

bool icc_profile_mem_own(struct icc_profile *icc, void *data,
const size_t len) {
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
	return (bool)icc->in;
}

bool icc_profile_mem_copy(struct icc_profile *icc, const void *data,
const size_t len) {
	/* Memory is probably owned by the decoder, but we want a copy in case
	 * we need to encode into a format that supports ICC profiles. */
	void *cpy = malloc(len);
	if (cpy) {
		memcpy(cpy, data, len);
		return icc_profile_mem_own(icc, cpy, len);
	}
	return false;
}
