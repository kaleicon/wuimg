#ifndef WU_FILESYSTEM
#define WU_FILESYSTEM

#include "wustr.h"
#include "sort.h"

struct file_entry {
	char *name;
	struct natfrm *frm;
};

char ** fs_compact_entries(struct file_entry *entries, const size_t nr);

struct file_entry * fs_filter_dir(const char *name, size_t *nr,
struct file_entry *init_key);

#endif /* WU_FILESYSTEM */
