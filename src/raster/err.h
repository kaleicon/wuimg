// SPDX-License-Identifier: 0BSD
#ifndef WUERRCODE
#define WUERRCODE
#include <stdbool.h>

enum wu_error {
	wu_no_change = -1, // For callbacks
	wu_ok = 0,
	wu_alloc_error,
	wu_open_error,
	wu_unknown_file_type,
	wu_unexpected_eof,
	wu_invalid_signature,
	wu_invalid_header,
	wu_unsupported_feature,
	wu_samples_wanted,
	wu_uncertain_validity,
	wu_no_image_data,
	wu_exceeds_size_limit,
	wu_int_overflow,
	wu_decoding_error,
	wu_invalid_params,
	wu_string_parse_error,
	wu_display_error,
	wu_unknown_error,
};

struct wu_st {
	enum wu_error st;
	const char *msg;
};

const char * wu_error_str(enum wu_error err);

struct wu_st wuerr(enum wu_error err, const char *msg);

#define TOSTR(x) TOSTR2(x)
#define TOSTR2(x) #x
#define WUERR_HERE(err) wuerr((err), __FILE__ ":" TOSTR(__LINE__) );

struct wu_st wuerr_partial(size_t written, size_t max);

struct wu_st wuok(void);

bool wu_isok(struct wu_st st);

#endif /* WUERRCODE */
