// SPDX-License-Identifier: 0BSD
#include <locale.h>
#include <stdlib.h>
#include <fcntl.h>

#include <archive.h>
#include <archive_entry.h>

#include "misc/common.h"
#include "misc/math.h"
#include "misc/mem.h"
#include "misc/wustr.h"
#include "fmtmap.h"
#include "term.h"

#include "extract.h"

static bool open_archive(struct extract_iter *iter) {
	iter->ra = archive_read_new();
	if (!iter->ra) {
		return false;
	}
	archive_read_support_filter_all(iter->ra);
	archive_read_support_format_all(iter->ra);
	iter->idx = -1;
	lseek(iter->fd, 0, SEEK_SET);
	return archive_read_open_fd(iter->ra, iter->fd, BUFSIZ) == ARCHIVE_OK;
}

void extract_free(struct extract_iter *iter) {
	free(iter->name);
	if (iter->cur) {
		fclose(iter->cur);
	}
	if (iter->ra) {
		archive_read_free(iter->ra);
	}
	close(iter->fd);
}

static bool tmp_extract(FILE *tmp, struct archive *r,
struct archive_entry *entry) {
	rewind(tmp);
	fflush(tmp);
	const int fd = fileno(tmp);
	ftruncate(fd, 0);
	if (archive_read_data_into_fd(r, fd) != ARCHIVE_FATAL) {
		struct timespec times[2];
		if (archive_entry_atime_is_set(entry)) {
			times[0].tv_sec = archive_entry_atime(entry);
			times[0].tv_nsec = 0;
		} else {
			times[0].tv_nsec = UTIME_OMIT;
		}

		if (archive_entry_mtime_is_set(entry)) {
			times[1].tv_sec = archive_entry_mtime(entry);
			times[1].tv_nsec = 0;
		} else {
			times[1].tv_nsec = UTIME_OMIT;
		}
		futimens(fd, times);
		lseek(fd, 0, SEEK_SET);
		return true;
	}
	return false;
}

static enum trit get_entry(struct extract_iter *iter, struct archive_entry *entry) {
	const struct wuptr name = wuptr_str(archive_entry_pathname(entry));
	const bool regular_and_probably_nonempty =
		( AE_IFREG == (archive_entry_filetype(entry) & AE_IFMT) )
		&& (
			!archive_entry_size_is_set(entry)
			|| archive_entry_size(entry)
		);

	if (regular_and_probably_nonempty && fmtmap_known_extension(name)) {
		if (!tmp_extract(iter->cur, iter->ra, entry)) {
			return trit_false;
		}
		free(iter->name);
		iter->name = memdup(name.ptr, name.len + 1);
		++iter->idx;
		iter->total = lmax(iter->total, iter->idx + 1);
	}
	return trit_true;
}

static enum trit next_entry(struct extract_iter *iter) {
	struct archive_entry *entry;
	const int r = archive_read_next_header(iter->ra, &entry);
	switch (r) {
	case ARCHIVE_WARN:
		term_line_key_val("libarchive warning",
			archive_error_string(iter->ra), stderr);
		// fallthrough
	case ARCHIVE_OK:
		return get_entry(iter, entry);
	case ARCHIVE_RETRY:
		break;
	case ARCHIVE_FATAL:
		term_line_key_val("libarchive error",
			archive_error_string(iter->ra), stderr);
		return trit_false;
	case ARCHIVE_EOF:
		return trit_what;
	}
	return trit_true;
}

bool extract_file(struct extract_iter *iter, long idx) {
	if (iter->seen_it_all) {
		idx = lmod(idx, iter->total);
	}
	if (iter->idx == idx) {
		rewind(iter->cur);
		return true;
	} else if (iter->idx > idx) {
		archive_read_free(iter->ra);
		open_archive(iter);
	}
	while (idx < 0 || iter->idx < idx) {
		switch (next_entry(iter)) {
		case trit_false:
			return false;
		case trit_true:
			continue;
		case trit_what:
			iter->seen_it_all = true;
			if (iter->total < 1) {
				return false;
			} else if (idx >= 0) {
				return extract_file(iter, idx);
			}
			break;
		}
		break;
	}
	return true;
}

bool extract_init(struct extract_iter *iter, const char *filename) {
	setlocale(LC_CTYPE, "");
	*iter = (struct extract_iter) {
		.cur = tmpfile(),
		.fd = open(filename, O_RDONLY),
	};
	if (iter->fd >= 0 && iter->cur && open_archive(iter)) {
		return true;
	}
	extract_free(iter);
	return false;
}
