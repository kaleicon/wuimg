#ifndef WU_EXTRACT
#define WU_EXTRACT

#include <stdio.h>
#include <stdbool.h>

#include <archive.h>

struct tmp_file {
	char *name;
	FILE *tmp;
};

struct archive_iter {
	struct archive *ra;
	size_t pos;
	size_t alloc;
	struct tmp_file *entry;
};

void free_archive_iter(struct archive_iter *iter);

void remove_archive_entry(struct tmp_file *entry);

struct tmp_file * get_archive_entry(struct archive_iter *iter, int idx);

bool init_archive_iter(struct archive_iter *iter, const char *filename);

#endif /* WU_EXTRACT */
