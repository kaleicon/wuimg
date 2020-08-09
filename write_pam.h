#ifndef WRITE_PAM
#define WRITE_PAM

#include "wudefs.h"

struct write_args {
	bool expand;
	bool overwrite;
};

void write_to_file(const struct image_file *infile, const char *filename,
struct write_args args);

#endif /* WRITE_PAM */
