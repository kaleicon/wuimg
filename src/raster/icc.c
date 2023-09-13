// SPDX-License-Identifier: 0BSD
#include "icc.h"
#include "term.h"

void icc_profile_free(struct icc_profile *icc) {
	if (icc->transform) {
		cmsDeleteTransform(icc->transform);
	}
	if (icc->in) {
		cmsCloseProfile(icc->in);
	}
}

static cmsUInt32Number read_fn(struct _cms_io_handler *io, void *buf,
const cmsUInt32Number size, const cmsUInt32Number nmemb) {
	struct mp_parser *mp = io->stream;
	const void *block = mp_next_slice(mp, size*nmemb);
	if (block) {
		memcpy(buf, block, size*nmemb);
		return nmemb;
	}
	return 0;
}

static cmsBool seek_fn(struct _cms_io_handler *io, const cmsUInt32Number off) {
	struct mp_parser *mp = io->stream;
	if (off <= mp->len) {
		mp->pos = off;
		return true;
	}
	return false;
}

static cmsBool close_fn(struct _cms_io_handler *io) {
	struct mp_parser *mp = io->stream;
	free((void *)mp->mem);
	return true;
}

static cmsUInt32Number tell_fn(struct _cms_io_handler *io) {
	const struct mp_parser *mp = io->stream;
	return (cmsUInt32Number)mp->pos;
}

static void err_fn(cmsContext id, cmsUInt32Number err, const char *text) {
	(void)id; (void)err;
	term_line_put(text, stderr);
}

static void init_profile(void) {
	cmsSetLogErrorHandler(err_fn);
}

bool icc_profile_mem_copy(struct icc_profile *icc, const void *data,
const size_t len) {
	init_profile();
	icc->in = cmsOpenProfileFromMem(data, (cmsUInt32Number)len);
	return (bool)icc->in;
}

bool icc_profile_mem_own(struct icc_profile *icc, void *data,
const size_t len) {
	init_profile();
	icc->mp = mp_parser_mem(len, data);
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
