#ifndef DEC
#define DEC

#include "wudefs.h"

struct file_class {
	char *name;
	enum wu_error_type (*func)(struct image_file *);
};

enum wu_error_type decode_image(struct image_file *infile,
struct file_class *entry);

struct file_class * find_images(const char *dirname,
size_t *nr_of_entries);

#endif /* DEC */
