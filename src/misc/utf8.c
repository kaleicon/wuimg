// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2022 kaleido
#include <string.h>

#include <uchardet/uchardet.h>
#include <unicode/ucnv.h>

#include "misc/utf8.h"

static bool convert_str(UConverter *from, UConverter *to, const char *data,
const size_t len, struct wustr *out, UErrorCode *err) {
	const size_t s = (size_t)UCNV_GET_MAX_BYTES_FOR_STRING(len,
		ucnv_getMaxCharSize(to));
	if (wustr_malloc(out, s)) {
		char *pos = (char *)out->str;
		ucnv_convertEx(to, from, &pos, pos + out->len, &data,
			data + len, NULL, NULL, NULL, NULL, false, true, err);
		out->len = (size_t)(pos - (char *)out->str);
		if (U_SUCCESS(*err)) {
			return true;
		}
		wustr_free(out);
	}
	return false;
}

enum trit utf8_convert(const char *restrict data, const size_t len,
struct wustr *out, const char *restrict enc) {
	*out = (struct wustr) {0};
	if (!len) {
		return trit_false;
	}

	UErrorCode err = U_ZERO_ERROR;
	UConverter *from = NULL;
	if (enc) {
		from = ucnv_open(enc, &err);
	} else {
		uchardet_t ud = uchardet_new();
		if (ud) {
			const int error = uchardet_handle_data(ud, data, len);
			if (!error) {
				uchardet_data_end(ud);
				enc = uchardet_get_charset(ud);
				/* No one mentions that deleting the context
				 * also invalidates the charset string. */
				if (enc[0]) {
					if (!strcmp(enc, "ASCII")
					|| !strcmp(enc, "UTF-8")) {
						uchardet_delete(ud);
						return trit_false;
					} else {
						from = ucnv_open(enc, &err);
					}
				}
			}
			uchardet_delete(ud);
		} else {
			return trit_what;
		}
	}

	enum trit res = trit_what;
	if (from) {
		UConverter *to = ucnv_open("UTF-8", &err);
		if (to) {
			if (convert_str(from, to, data, len, out, &err)) {
				res = trit_true;
			}
			ucnv_close(to);
		}
		ucnv_close(from);
	}
	return res;
}
