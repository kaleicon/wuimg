#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <limits.h>
#include <unistd.h>

#include <unicode/ucol.h>
#include <unicode/uiter.h>

#include "common.h"
#include "dec.h"
#include "wustr.h"
#include "filesystem.h"

struct lenstr {
	uint16_t len;
	uint8_t str[];
};

// https://web.archive.org/web/20210414144035/http://site.icu-project.org/charts/collation-icu4c48-glibc
static const size_t EXPAND_FACTOR = 4;
static const size_t POOL_SIZE = 1 << 14;

struct keypool {
	/* We use memory pools to store sorting keys quickly and compactly.
	 * Since it's not possible to know the key size before-hand, using
	 * malloc on each would be wasteful and slow.
	 * Since we store pointers to the keys, using a single memory area
	 * would invalidate them on realloc. */
	struct wugrow grow;
	size_t used; // Space used on the current buffer
	unsigned char **buf;
};

struct fs_dir {
	struct fs_path path;

	struct wugrow entries_grow;
	struct fs_entry {
		struct lenstr *key;
		char *name;
	} *entries;

	struct keypool pool;
};

struct collator {
	UCollator *coll;
	UCharIterator iter;
	UErrorCode err;
};


void fs_path_free(struct fs_path *path) {
	wustr_free(&path->parent);
}

static bool fs_path_set_empty_dir(struct fs_path *path) {
	return wustr_memdup(&path->parent, "", 0);
}

static bool fs_path_set_dir(struct fs_path *path, const struct wuptr dir) {
	if (!dir.len) {
		return fs_path_set_empty_dir(path);
	}

	const size_t len = dir.len + (dir.ptr[dir.len - 1] != '/');
	if (wustr_malloc(&path->parent, len)) {
		unsigned char *str = path->parent.str;
		memcpy(str, dir.ptr, dir.len);
		str[len-1] = '/';
		str[len] = 0;
		return true;
	}
	return false;
}

bool set_path(struct fs_path *path, const struct wuptr name, bool with_dir) {
	const unsigned char *slash = memrchr(name.ptr, '/', name.len);
	if (slash) {
		++slash;
		const size_t dirlen = (size_t)(slash - name.ptr);
		path->file = (struct wuptr) {
			.len = name.len - dirlen,
			.ptr = slash,
		};
		if (with_dir) {
			return fs_path_set_dir(path, wuptr_mem(name.ptr, dirlen));
		}
		return true;
	}
	path->file = name;
	if (with_dir) {
		return fs_path_set_empty_dir(path);
	}
	return true;
}

void fs_path_set_file(struct fs_path *path, const struct wuptr name) {
	set_path(path, name, false);
}

static bool fs_path_set_path(struct fs_path *path, const struct wuptr name) {
	return set_path(path, name, true);
}

void keypool_free(struct keypool *pool) {
	for (size_t i = 0; i < pool->grow.pos; ++i) {
		free(pool->buf[i]);
	}
	free(pool->buf);
}

static char ** fs_dir_names(struct fs_dir *dir) {
	keypool_free(&dir->pool);

	char **names = (char **)dir->entries;
	for (size_t i = 0; i < dir->entries_grow.pos; ++i) {
		names[i] = dir->entries[i].name;
	}
	char **hold = realloc(names, dir->entries_grow.pos * sizeof(*names));
	return hold ? hold : names;
}

static void fs_dir_free(struct fs_dir *dir) {
	keypool_free(&dir->pool);
	for (size_t i = 0; i < dir->entries_grow.pos; ++i) {
		free(dir->entries[i].name);
	}
	free(dir->entries);
}

static struct wuptr dirent_wuptr(const struct dirent *entry) {
#ifdef _DIRENT_HAVE_D_NAMLEN
	return wuptr_mem(entry->d_name, (size_t)entry->d_namlen);
#else
	return wuptr_str(entry->d_name);
#endif
}

static bool is_regular_file(DIR *dp, const struct dirent *entry) {
#ifdef _DIRENT_HAVE_D_TYPE
	switch (entry->d_type) {
	case DT_UNKNOWN:
		break;
	case DT_LNK:
	case DT_REG:
		return true;
	default:
		return false;
	}
#endif
	struct stat sb;
	if (fstatat(dirfd(dp), entry->d_name, &sb, 0) == 0) {
		return S_ISREG(sb.st_mode);
	}
	return false;
}

static struct lenstr * pool_getptr(struct keypool *pool, const size_t reserve) {
	struct wugrow *pg = &pool->grow;
	if (reserve > POOL_SIZE - pool->used) {
		if (!wugrow_recheck(&pool->buf, pg)) {
			return NULL;
		}
		pool->buf[pg->pos] = malloc(POOL_SIZE);
		if (!pool->buf[pg->pos]) {
			return NULL;
		}
		++pg->pos;
		pool->used = 0;
	}
	return (struct lenstr *)(pool->buf[pg->pos - 1] + pool->used);
}

static struct lenstr * keygen(struct keypool *pool, const struct wuptr *file,
struct collator *icu) {
	struct lenstr *ptr;
	const size_t sptr = sizeof(*ptr);
	const size_t count = file->len * EXPAND_FACTOR + sptr;
	ptr = pool_getptr(pool, count);
	if (!ptr) {
		return NULL;
	}

	uiter_setUTF8(&icu->iter, (const char *)file->ptr, (int32_t)file->len);
	uint32_t state[2] = {0};
	const int32_t written = ucol_nextSortKeyPart(icu->coll, &icu->iter,
		state, ptr->str, (int32_t)count, &icu->err);
	ptr->len = (uint16_t)written;

	const size_t align = ((size_t)written + sptr - 1) / sptr * sptr;
	pool->used += sptr + align;
	return ptr;
}

static bool pathcat(struct fs_dir *dir, const struct wuptr *file,
struct collator *icu) {
	if (!wugrow_recheck(&dir->entries, &dir->entries_grow)) {
		return false;
	}

	struct lenstr *keyptr = keygen(&dir->pool, file, icu);
	if (!keyptr) {
		return false;
	}

	// parent->str is either empty or has a trailing slash
	struct wustr *parent = &dir->path.parent;
	char *name = malloc(parent->len + file->len + 1);
	if (name) {
		memcpy(name, parent->str, parent->len);
		memcpy(name + parent->len, file->ptr, file->len + 1);
		dir->entries[dir->entries_grow.pos] = (struct fs_entry) {
			.key = keyptr,
			.name = name,
		};
		++dir->entries_grow.pos;
	}
	return (bool)name;
}

static bool filter_dir(DIR *dp, struct fs_dir *dir, struct fs_entry *init_key,
struct collator *icu) {
	dir->entries_grow = wugrow_init(sizeof(*dir->entries));
	dir->pool.grow = wugrow_init(sizeof(*dir->pool.buf));
	if (dir->path.file.ptr) {
		if (!pathcat(dir, &dir->path.file, icu)) {
			return false;
		}
		if (init_key) {
			*init_key = dir->entries[0];
		}
	} else if (init_key) {
		*init_key = (struct fs_entry){0};
	}

	const struct dirent *dirent;
	while ( (dirent = readdir(dp)) ) {
		if (!is_regular_file(dp, dirent)) {
			continue;
		}
		const struct wuptr name = dirent_wuptr(dirent);
		if (wuptr_eq(dir->path.file, name)) {
			dir->path.file.len = 0;
			continue;
		}
		if (!fmtmap_known_extension(name)) {
			continue;
		}
		if (!pathcat(dir, &name, icu)) {
			return false;
		}
	}
	return dir->entries_grow.pos > 0;
}

static int open_dirfd(const char *str) {
	return open(str, O_RDONLY | O_DIRECTORY);
}

int fs_get_parent_dir(struct fs_path *path, const char *str, bool must_exist) {
	int dfd = -1;
	const struct wuptr name = wuptr_str(str);
	if (name.len) {
		errno = 0;
		dfd = open_dirfd(str);
		if (dfd != -1) {
			fs_path_set_dir(path, name);
		} else if (errno == ENOTDIR || !must_exist) {
			errno = 0;
			if (fs_path_set_path(path, name)) {
				dfd = open_dirfd(path->parent.str[0]
					? (char *)path->parent.str : ".");
			}
		}
	} else {
		dfd = open_dirfd(".");
		fs_path_set_empty_dir(path);
	}
	return dfd;
}

static bool get_files(struct fs_dir *dir, const char *name,
struct fs_entry *init_key) {
	const int dfd = fs_get_parent_dir(&dir->path, name, true);
	bool status = false;
	if (dfd != -1) {
		DIR *dp = fdopendir(dfd);
		if (dp) {
			struct collator icu = {
				.coll = ucol_open(NULL, &icu.err),
			};
			if (U_SUCCESS(icu.err)) {
				ucol_setAttribute(icu.coll,
					UCOL_NUMERIC_COLLATION, UCOL_ON, &icu.err);
				status = filter_dir(dp, dir, init_key, &icu);
				ucol_close(icu.coll);
			}
			closedir(dp);
		} else {
			close(dfd);
		}
	}
	fs_path_free(&dir->path);
	return status;
}

static int mem_strcoll(const void *restrict v1, const void *restrict v2) {
	const struct fs_entry *f1 = v1;
	const struct fs_entry *f2 = v2;
	const struct lenstr *l1 = f1->key;
	const struct lenstr *l2 = f2->key;
	return memcmp(l1->str, l2->str, zumin((size_t)l1->len, (size_t)l2->len));
}

static size_t sort_dir_entries(struct fs_dir *dir, const struct fs_entry *init_key) {
	qsort(dir->entries, dir->entries_grow.pos, sizeof(*dir->entries),
		mem_strcoll);
	if (init_key->name) {
		const struct fs_entry *loc = bsearch(init_key, dir->entries,
			dir->entries_grow.pos, sizeof(*dir->entries), mem_strcoll);
		return (size_t)(loc - dir->entries);
	}
	return 0;
}

char ** fs_filter_sort(const char *name, size_t *nr, size_t *start_idx) {
	struct fs_dir dir = {
		.pool.used = POOL_SIZE,
	};
	struct fs_entry init_key;

	if (get_files(&dir, name, &init_key)) {
		*start_idx = sort_dir_entries(&dir, &init_key);
		*nr = dir.entries_grow.pos;
		return fs_dir_names(&dir);
	}
	fs_dir_free(&dir);
	return NULL;
}
