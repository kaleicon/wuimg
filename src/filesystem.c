#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>

#include "common.h"
#include "dec_fmtmap.h"
#include "wustr.h"
#include "sort.h"
#include "filesystem.h"

struct path {
	struct wustr_mut dir;
	const char *file;
};

char ** fs_compact_entries(struct file_entry *entries, const size_t nr) {
	char **names = malloc(sizeof(*names) * nr);
	if (names) {
		for (size_t i = 0; i < nr; ++i) {
			natfrm_free(entries[i].frm);
			names[i] = entries[i].name;
		}
		free(entries);
	}
	return names;
}

static bool pathcat(struct file_entry *e, const struct wustr_mut *dir,
const struct wustr *file, struct natfrm_data *natbuf) {
	// dir->str is empty or has a trailing slash
	e->name = malloc(dir->len + file->len + 1);
	if (e->name) {
		memcpy(e->name, dir->str, dir->len);
		memcpy(e->name + dir->len, file->str, file->len + 1);
		e->frm = natfrm_str(e->name + dir->len, natbuf);
		if (e->frm) {
			return true;
		}
		free(e->name);
	}
	return false;
}

struct file_entry * iter_dir(const struct path *path, DIR *dir, size_t *nr,
struct file_entry *init_key) {
	const size_t max_name_len = 256;
	struct natfrm_data natbuf;
	natfrm_data_init(&natbuf, max_name_len);

	*nr = 128;
	struct file_entry *list = malloc(*nr * sizeof(*list));
	if (!list) {
		return NULL;
	}

	size_t idx = 0;
	struct wustr init = {0};
	if (path->file) {
		init = wustr_from_str(path->file);
		if (!pathcat(list, &path->dir, &init, &natbuf)) {
			free(list);
			return NULL;
		}
		*init_key = *list;
		++idx;
	} else {
		*init_key = (struct file_entry){0};
	}

	bool ok = true;
	const struct dirent *entry;
	while ( ok && (entry = readdir(dir)) ) {
#ifdef _DIRENT_HAVE_D_TYPE
		switch (entry->d_type) {
		case DT_REG:
		case DT_LNK:
		case DT_UNKNOWN:
			break;
		default:
			continue;
		}
#endif
#ifdef _DIRENT_HAVE_D_NAMLEN
		const struct wustr name = {.len = (size_t)entry->d_namlen,
			.str = entry->d_name};
#else
		const struct wustr name = wustr_from_str(entry->d_name);
#endif
		if (wustr_eq(&init, &name)) {
			init.len = 0;
			continue;
		}

		if (known_extension(name)) {
			ok = grow_buffer(&list, nr, idx, sizeof(*list));
			if (ok) {
				ok = pathcat(list + idx, &path->dir, &name,
					&natbuf);
				if (ok) {
					++idx;
				}
			}
		}
	}

	if (idx == 0 || !ok) {
		for (size_t i = 0; i < idx; ++i) {
			free(list[i].name);
			natfrm_free(list[i].frm);
		}
		free(list);
		return NULL;
	}
	*nr = idx;
	return list;
}

struct file_entry * fs_filter_dir(const char *name, size_t *nr,
struct file_entry *init_key) {
	struct path path = {0};
	DIR *dir = NULL;
	errno = 0;
	if (name[0]) {
		dir = opendir(name);
		if (dir) {
			const size_t namelen = strlen(name);
			const size_t dirlen = namelen + (name[dirlen-1] != '/');
			if (wustr_malloc(&path.dir, dirlen)) {
				memcpy(path.dir.str, name, namelen);
				path.dir.str[dirlen-1] = '/';
				path.dir.str[dirlen] = 0;
			}
		} else if (errno == ENOTDIR) {
			const char *slash = strrchr(name, '/');
			if (slash) {
				++slash;
				const size_t dirlen = (size_t)(slash - name);
				path.file = slash;
				wustr_memdup(&path.dir, name, dirlen);
			} else {
				path.file = name;
				wustr_strdup(&path.dir, "");
			}
			if (path.dir.str) {
				dir = opendir(path.dir.str[0]
					? path.dir.str : ".");
			}
		}
	} else {
		dir = opendir(".");
		wustr_strdup(&path.dir, "");
	}

	struct file_entry *entries = NULL;
	if (dir) {
		if (path.dir.str) {
			entries = iter_dir(&path, dir, nr, init_key);
		}
		closedir(dir);
	}
	wustr_free(&path.dir);
	return entries;
}
