#ifndef DEC
#define DEC

#include "wudefs.h"

typedef enum wu_error (*dec_func_t)(struct image_file *,
	const struct wu_conf *wuconf);

enum wu_error decode_image(struct image_file *infile,
const struct wu_conf *wuconf, const char *filename);

char ** filter_images(const char *dirname, const char *init_name,
size_t *nr_of_entries);

void sort_dec_tables(void);

#endif /* DEC */
