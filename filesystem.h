#ifndef WU_FILESYSTEM
#define WU_FILESYSTEM

#include "wustr.h"
#include "sort.h"

struct file_entry {
	char *name;
	struct natfrm *frm;
};

struct path {
	struct wustr_mut *dir;
	const char *file;
};

struct path_list {
	size_t len;
	struct path *paths;
};

char ** compact_entries(struct file_entry *entries, const size_t nr);

struct file_entry * fs_filter_dir(const struct path *path, size_t *nr,
struct file_entry *init_key);

void free_path_list(struct path_list *list);

int names_to_paths(char **names, size_t n, struct path_list *list);

#endif /* WU_FILESYSTEM */
