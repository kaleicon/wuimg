#ifndef WRITE_PAM
#define WRITE_PAM

#include "wudefs.h"

struct write_args {
	const char *outname;
	bool raw;
	bool overwrite;
};

void write_to_file(const struct image_file *infile, const char *filename,
const struct write_args *args);

int read_write_args(int argc, char **argv, struct write_args *args);

#endif /* WRITE_PAM */
