#ifndef LIB_PI
#define LIB_PI

struct pi_decompress_info {
	const unsigned char *restrict pix;
	size_t pix_len;
	const unsigned char *restrict comment;
	size_t comment_len;
	const unsigned char *restrict dummy_bytes;
	size_t dummy_len;
	const unsigned char *restrict reserved_area;
	unsigned int reserved_area_len;
	const unsigned char saver_model[4];

	unsigned short width;
	unsigned short height;
	unsigned char palette_mode; // Only 0x80 supported
	unsigned char nr_of_planes; // Only 4 is supported
	unsigned char screen_ratio_num; // Read but not used
	unsigned char screen_ratio_den; // Read but not used
	const unsigned char *restrict palette;
};

int pi_parse_full_header(const unsigned char *data, size_t size,
struct pi_decompress_info *pinfo);

int pi_check_sig(const unsigned char *file);

#endif /* LIB_PI */
