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

char ** compact_entries(struct file_entry *entries, const size_t nr) {
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

static bool pathcat(struct file_entry *e, const struct wustr *dir,
const struct wustr *file, struct natfrm_data *natbuf) {
	// Assumes dir->data is empty or has a trailing slash
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

struct file_entry * fs_filter_dir(const struct path *path, size_t *nr,
struct file_entry *init_key) {
	const struct wustr dir = wustr_const(path->dir);
	DIR *dir_iter = opendir(dir.len ? dir.str : ".");
	if (!dir_iter) {
		return NULL;
	}

	struct natfrm_data natbuf;
	if (!natfrm_data_init(&natbuf, 256)) {
		closedir(dir_iter);
		return NULL;
	}

	*nr = 64;
	struct file_entry *entries = malloc(*nr * sizeof(*entries));
	if (!entries) {
		natfrm_data_free(&natbuf);
		closedir(dir_iter);
		return NULL;
	}

	bool success = true;
	size_t idx = 0;
	struct wustr init = {0};
	if (path->file) {
		init = wustr_from_str(path->file);
		success = pathcat(entries, &dir, &init, &natbuf);
		if (!success) {
			free(entries);
			natfrm_data_free(&natbuf);
			closedir(dir_iter);
			return NULL;
		}
		*init_key = *entries;
		++idx;
	} else {
		*init_key = (struct file_entry){0};
	}

	const struct dirent *file;
	while ( (file = readdir(dir_iter)) ) {
#ifdef _DIRENT_HAVE_D_TYPE
		switch (file->d_type) {
		case DT_REG:
		case DT_LNK:
		case DT_UNKNOWN:
			break;
		default:
			continue;
		}
#endif
#ifdef _DIRENT_HAVE_D_NAMLEN
		const struct wustr name = {.len = (size_t)file->d_namlen,
			.data = file->d_name};
#else
		const struct wustr name = wustr_from_str(file->d_name);
#endif
		if (wustr_eq(&init, &name)) {
			init.len = 0;
			continue;
		}

		if (known_extension(name)) {
			success = grow_buffer(&entries, nr, idx,
				sizeof(*entries));
			if (success) {
				success = pathcat(entries + idx, &dir, &name,
					&natbuf);
				if (success) {
					++idx;
				} else {
					break;
				}
			} else {
				break;
			}
		}
	}
	closedir(dir_iter);
	natfrm_data_free(&natbuf);
	*nr = idx;
	if (idx == 0) {
		free(entries);
		return NULL;
	}
	if (!success) {
		for (size_t i = 0; i < idx; ++i) {
			free(entries[i].name);
			natfrm_free(entries[i].frm);
		}
		free(entries);
		return NULL;
	}
	return entries;
}

void free_path_list(struct path_list *list) {
	for (size_t i = 0; i < list->len; ++i) {
		free(list->paths[i].dir);
	}
	free(list->paths);
}

static int get_path_components(const char *name, struct path *path) {
	errno = 0;
	path->file = NULL;
	if (!name[0]) {
		path->dir = wustr_strdup("");
		return errno;
	} else if (!strcmp(".", name) || !strcmp("./", name)) {
		path->dir = wustr_strdup("./");
		return errno;
	}

	struct stat statbuf;
	if (stat(name, &statbuf) == -1) {
		return errno;
	}

	errno = 0;
	if (S_ISDIR(statbuf.st_mode)) {
		const size_t namelen = strlen(name);
		if (name[namelen - 1] == '/') {
			path->dir = wustr_memdup(name, namelen);
		} else {
			path->dir = wustr_malloc(namelen + 1);
			if (path->dir) {
				memcpy(path->dir->str, name, namelen);
				path->dir->str[namelen] = '/';
				path->dir->str[namelen+1] = 0;
			}
		}
	} else {
		const char *slash = strrchr(name, '/');
		if (slash) {
			const size_t dir_len = (size_t)(slash - name + 1);
			path->file = slash + 1;
			path->dir = wustr_memdup(name, dir_len);
		} else {
			path->file = name;
			path->dir = wustr_strdup("");
		}
	}
	return errno;
}

int names_to_paths(char **names, size_t n, struct path_list *list) {
	errno = 0;
	list->paths = malloc(sizeof(*list->paths) * n);
	if (list->paths) {
		for (list->len = 0; list->len < n; ++list->len) {
			struct path *outpath = list->paths + list->len;
			const char *name = names[list->len];
			const int err = get_path_components(name, outpath);
			if (err) {
				free_path_list(list);
				return err;
			}
		}
	}
	return errno;
}
