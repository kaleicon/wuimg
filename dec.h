#ifndef DEC
#define DEC

#include "wudefs.h"

bool known_extension(const char *filename);

enum wu_error callback_image(struct image_file *infile,
const struct wu_conf *wuconf, struct wu_state *state, enum image_event event);

enum wu_error decode_image(struct image_file *infile,
const struct wu_conf *wuconf, const char *filename);

char ** filter_directory(const char *restrict dirname,
const char *restrict init_name, size_t *nr);

void sort_dec_tables(void);

void print_known_formats(void);

#endif /* DEC */
