#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <locale.h>

#include <archive.h>
#include <archive_entry.h>

#include "extract.h"
#include "common.h"
#include "dec.h"

void free_archive_iter(struct archive_iter *iter) {
	for (size_t i = 0; i < iter->pos; ++i) {
		free(iter->entry[i].name);
		if (iter->entry[i].tmp) {
			fclose(iter->entry[i].tmp);
		}
	}
	free(iter->entry);
	if (iter->ra) {
		archive_read_free(iter->ra);
	}
}

static la_int64_t tmp_extract(FILE *tmp, struct archive *r,
struct archive_entry *entry) {
	const void *buf;
	size_t size;
	la_int64_t off;
	while (archive_read_data_block(r, &buf, &size, &off) == ARCHIVE_OK) {
		fwrite(buf, 1, size, tmp);
	}

	if (off) {
		struct timespec times[2];
		if (archive_entry_atime_is_set(entry)) {
			times[0].tv_sec = archive_entry_atime(entry);
		} else {
			times[0].tv_nsec = UTIME_OMIT;
		}

		if (archive_entry_mtime_is_set(entry)) {
			times[1].tv_sec = archive_entry_mtime(entry);
		} else {
			times[1].tv_nsec = UTIME_OMIT;
		}
		futimens(fileno(tmp), times);
	}
	return off;
}

void remove_archive_entry(struct tmp_file *entry) {
	free(entry->name);
	fclose(entry->tmp);
	entry->name = NULL;
	entry->tmp = NULL;
}

static bool ok_case(struct archive_iter *iter, struct archive_entry *entry) {
	const char *name = archive_entry_pathname(entry);
	const bool reg_probably_nonempty =
		((archive_entry_filetype(entry) & AE_IFMT) == AE_IFREG)
		&& (
			!archive_entry_size_is_set(entry)
			|| archive_entry_size(entry)
		);

	if (reg_probably_nonempty && known_extension(name)) {
		errno = 0;
		FILE *tmp = tmpfile();
		if (!tmp) {
			perror("Failed to create temp file");
			return false;
		}

		const la_int64_t written = tmp_extract(tmp, iter->ra, entry);
		if (written) {
			if (!grow_buffer(&iter->entry, &iter->alloc, iter->pos,
			sizeof(*iter->entry)) ) {
				return false;
			}

			iter->entry[iter->pos] = (struct tmp_file) {
				.name = strdup(name),
				.tmp = tmp,
			};
			++iter->pos;
		} else {
			fclose(tmp);
		}
	}
	return true;
}

static bool next_archive_entry(struct archive_iter *iter) {
	struct archive_entry *entry;
	const int r = archive_read_next_header(iter->ra, &entry);
	switch (r) {
	case ARCHIVE_WARN:
		printf("libarchive warning: %s\n",
			archive_error_string(iter->ra));
		// Fallthrough
	case ARCHIVE_OK:
		return ok_case(iter, entry);
	case ARCHIVE_RETRY:
		break;
	case ARCHIVE_FATAL:
		printf("libarchive error: %s\n",
			archive_error_string(iter->ra));
		// Fallthrough
	case ARCHIVE_EOF:
		archive_read_free(iter->ra);
		iter->ra = NULL;
		return r == ARCHIVE_EOF;
	}
	return true;
}

struct tmp_file * get_archive_file(struct archive_iter *iter, const int idx) {
	while (iter->ra && (idx < 0 || iter->pos <= (size_t)idx)) {
		if (!next_archive_entry(iter)) {
			return NULL;
		}
	}
	if (!iter->pos) {
		return NULL;
	}
	const int pos = imod(idx, (int)iter->pos);
	struct tmp_file *file = iter->entry + pos;
	if (file->tmp) {
		rewind(file->tmp);
	}
	return file;
}

bool init_archive_iter(struct archive_iter *iter, const char *filename) {
	setlocale(LC_CTYPE, "");
	struct archive *ra = archive_read_new();
	if (!ra) {
		return false;
	}

	archive_read_support_filter_all(ra);
	archive_read_support_format_all(ra);
	if (archive_read_open_filename(ra, filename, BUFSIZ) != ARCHIVE_OK) {
		archive_read_free(ra);
		return false;
	}

	const size_t alloc = 256;
	*iter = (struct archive_iter) {
		.ra = ra,
		.pos = 0,
		.alloc = alloc,
		.entry = calloc(alloc, sizeof(*iter->entry)),
	};
	if (!iter->entry) {
		archive_read_free(ra);
		return false;
	}
	return (bool)get_archive_file(iter, 0);
}
