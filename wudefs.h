#ifndef WUDEFS
#define WUDEFS

#include <stdbool.h>

#define WU_CANON_NAME "wu"

struct raw_img {
	unsigned char *data;
	char *id;

	unsigned int w, h;

	unsigned char channels;
	unsigned char bitdepth;
	unsigned char true_channels;
	unsigned char true_bitdepth;

	int msec;
};

struct image_file {
	const char *restrict name;
	char *restrict err_msg;
	struct raw_img *sub_img;
	size_t nr;
	bool is_animation;
//	int __padding__;
};

enum wu_error_type {
	wu_ok,
	wu_alloc_error,
	wu_unknown_file_type,
	wu_invalid_params,
	wu_open_error,
	wu_unexpected_eof,
	wu_invalid_sig,
	wu_invalid_header,
	wu_unsupported_feature,
	wu_decoding_error,
	wu_unknown_error,
};

const char * wu_error_message(enum wu_error_type err);

void print_image_information(const struct image_file *file);

void normalize_sub_images(struct image_file *file);

void fit_sub_images(struct image_file *file, const size_t nr);

struct raw_img * alloc_sub_images(struct image_file *file, const size_t nr);

void free_image_file(struct image_file *file);

#endif /* WUDEFS */
