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
#include "wustr.h"
#include "dec.h"
#include "term.h"

void extract_iter_free(struct extract_iter *iter) {
	for (size_t i = 0; i < iter->grow.pos; ++i) {
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

void extract_file_free(struct extract_file *entry) {
	free(entry->name);
	fclose(entry->tmp);
	entry->name = NULL;
	entry->tmp = NULL;
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

static bool ok_case(struct extract_iter *iter, struct archive_entry *entry) {
	const struct wuptr name = wuptr_str(archive_entry_pathname(entry));
	const bool regular_and_probably_nonempty =
		( AE_IFREG == (archive_entry_filetype(entry) & AE_IFMT) )
		&& (
			!archive_entry_size_is_set(entry)
			|| archive_entry_size(entry)
		);

	if (regular_and_probably_nonempty && fmtmap_known_extension(name)) {
		errno = 0;
		FILE *tmp = tmpfile();
		if (!tmp) {
			perror("Failed to create temp file");
			return false;
		}

		const la_int64_t written = tmp_extract(tmp, iter->ra, entry);
		if (written) {
			if (!wugrow_recheck(&iter->entry, &iter->grow)) {
				fclose(tmp);
				return false;
			}

			char *nname = memdup(name.ptr, name.len + 1);
			if (!nname) {
				fclose(tmp);
				return false;
			}
			iter->entry[iter->grow.pos] = (struct extract_file) {
				.name = nname,
				.tmp = tmp,
			};
			++iter->grow.pos;
		} else {
			fclose(tmp);
		}
	}
	return true;
}

static bool next_archive_entry(struct extract_iter *iter) {
	struct archive_entry *entry;
	const int r = archive_read_next_header(iter->ra, &entry);
	switch (r) {
	case ARCHIVE_WARN:
		term_line_key_val("libarchive warning",
			archive_error_string(iter->ra), stderr);
		// fallthrough
	case ARCHIVE_OK:
		return ok_case(iter, entry);
	case ARCHIVE_RETRY:
		break;
	case ARCHIVE_FATAL:
		term_line_key_val("libarchive error",
			archive_error_string(iter->ra), stderr);
		// fallthrough
	case ARCHIVE_EOF:
		archive_read_free(iter->ra);
		iter->ra = NULL;
		return r == ARCHIVE_EOF;
	}
	return true;
}

struct extract_file * extract_file_get(struct extract_iter *iter, long idx) {
	while (iter->ra && (idx < 0 || (size_t)idx >= iter->grow.pos)) {
		if (!next_archive_entry(iter)) {
			return NULL;
		}
	}

	if (iter->grow.pos) {
		idx = lmod(idx, (long)iter->grow.pos);
		struct extract_file *file = iter->entry + idx;
		if (file->tmp) {
			rewind(file->tmp);
		}
		return file;
	}
	return NULL;
}

bool extract_iter_init(struct extract_iter *iter, const char *filename) {
	setlocale(LC_CTYPE, "");
	*iter = (struct extract_iter) {
		.ra = archive_read_new(),
		.grow = wugrow_init(sizeof(*iter->entry)),
	};
	if (iter->ra) {
		archive_read_support_filter_all(iter->ra);
		archive_read_support_format_all(iter->ra);
		if (archive_read_open_filename(iter->ra, filename, BUFSIZ)
		== ARCHIVE_OK) {
			return true;
		}
		archive_read_free(iter->ra);
	}
	return false;
}
