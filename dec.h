#ifndef DEC
#define DEC

#include "wudefs.h"

struct file_list {
	size_t nr;
	char *name[];
};

bool known_extension(const char *filename);

enum wu_error callback_image(struct image_file *infile,
const struct wu_conf *wuconf, struct wu_state *state, enum image_event event);

enum wu_error decode_image(struct image_file *infile,
const struct wu_conf *wuconf, const char *filename);

void free_file_list(struct file_list *files);

struct file_list * filter_directory(const char *dirname, const char *init_name);

void sort_dec_tables(void);

#endif /* DEC */
