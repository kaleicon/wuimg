#ifndef WU_EXTRACT
#define WU_EXTRACT

#include <stdio.h>
#include <stdbool.h>

#include <archive.h>

struct extract_file {
	char *name;
	FILE *tmp;
};

struct extract_iter {
	struct archive *ra;
	size_t pos;
	size_t alloc;
	struct extract_file *entry;
};

void extract_iter_free(struct extract_iter *iter);

void extract_file_free(struct extract_file *entry);

struct extract_file * extract_file_get(struct extract_iter *iter, long idx);

struct extract_file * extract_iter_init(struct extract_iter *iter,
const char *filename);

#endif /* WU_EXTRACT */
