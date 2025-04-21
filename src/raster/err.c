// SPDX-License-Identifier: 0BSD
#include <stddef.h>
#include "raster/err.h"

const char * wu_error_str(const enum wu_error err) {
	switch (err) {
	case wu_no_change:
		return "Nothing was done so nothing failed. "
			"If you're reading this, it's a bug.";
	case wu_ok:
		return "All OK";
	case wu_alloc_error:
		return "Memory allocation error";
	case wu_open_error:
		return "Failed to open file for reading";
	case wu_unknown_file_type:
		return "Unknown file format";
	case wu_unexpected_eof:
		return "Unexpected End Of File";
	case wu_invalid_signature:
		return "Corrupted or invalid file format signature";
	case wu_invalid_header:
		return "Corrupted or invalid format header";
	case wu_invalid_params:
		return "Invalid decoding parameters. "
			"This is most likely a bug in our code.";
	case wu_unsupported_feature:
		return "Unsupported feature in image";
	case wu_samples_wanted:
		return "Format feature not supported due to a lack of samples."
			" Please consider reporting this!";
	case wu_uncertain_validity:
		return "Decoder is unsure if this file is valid or not."
			" Please consider reporting this!";
	case wu_no_image_data:
		return "Header-only file with no image data";
	case wu_exceeds_size_limit:
		return "Image exceeds the configured or display dimension limit";
	case wu_int_overflow:
		return "Integer overflow";
	case wu_decoding_error:
		return "Failed to decode image";
	case wu_string_parse_error:
		return "Failed to parse string";
	case wu_display_error:
		return "Error ocurred during display";
	case wu_unknown_error:
		return "Purposely unspecified error o.O";
	}
	return "An unknown and unforeseen problem occurred. Things are bad. "
		"Pray for my soul.";
}

struct wu_st wuerr(const enum wu_error err, const char *msg) {
	return (struct wu_st){.st = err, .msg = msg};
}

struct wu_st wuerr_partial(const size_t written, const size_t max) {
	return wuerr(written ? wu_ok : wu_decoding_error,
		written == max ? NULL : "truncated stream");
}

struct wu_st wuok(void) {
	return wuerr(wu_ok, NULL);
}

bool wu_isok(const struct wu_st st) {
	return st.st == wu_ok;
}
