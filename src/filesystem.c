#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <limits.h>

#include <unicode/ucol.h>
#include <unicode/uiter.h>

#include "common.h"
#include "dec_fmtmap.h"
#include "wustr.h"
#include "filesystem.h"

struct collator {
	UCollator *coll;
	UCharIterator iter;
	UErrorCode err;
};

struct path {
	struct wustr_mut parent;
	struct wustr file;
};

struct lenstr {
	uint16_t len;
	uint8_t str[];
};

struct key_pool {
	size_t alloc;
	size_t nr;
	size_t bufpos;
	uint8_t **bufs;
};

struct fs_dir {
	size_t len;
	struct key_pool pool;
	struct fs_entry {
		struct lenstr *key;
		char *name;
	} *entries;
};


static const size_t EXPAND_FACTOR = 4; /* The ICU collation user guide
	recommends using 4 times the space of the longest string for sort keys.
	Meanwhile, the ICU vs GLIBC benchmarks indicate that the
	bytes/character ratio for the worst case (Korean names) is over 4.3,
	while the best case is 1.4. Since non-ASCII text (like Korean) will
	already come with a ratio >1, a relative increase of 4 should be safe.
	https://web.archive.org/web/http://site.icu-project.org/charts/collation-icu4c48-glibc
	*/
static const size_t LONGEST_STRING = EXPAND_FACTOR * (NAME_MAX + sizeof(struct lenstr));
	/* sizeof() bytes are for the string length, which we put in the first
	word instead of the containing struct. This reduces the amount of data
	swapped by qsort while letting us use memcmp() instead of strcmp() */
static const size_t EXPECTED_FILES = 64; /* Starting number */
static const size_t POOL_SIZE = EXPECTED_FILES * LONGEST_STRING;


static void key_pool_free(struct key_pool *pool) {
	for (size_t i = 0; i < pool->nr; ++i) {
		free(pool->bufs[i]);
	}
	free(pool->bufs);
}

static char ** fs_dir_names(struct fs_dir *list) {
	key_pool_free(&list->pool);

	char **names = (char **)list->entries;
	for (size_t i = 0; i < list->len; ++i) {
		names[i] = list->entries[i].name;
	}
	char **hold = realloc(names, list->len * sizeof(*names));
	return hold ? hold : names;
}

static void fs_dir_free(struct fs_dir *dir) {
	key_pool_free(&dir->pool);

	for (size_t i = 0; i < dir->len; ++i) {
		free(dir->entries[i].name);
	}
	free(dir->entries);
}

#ifndef _DIRENT_HAVE_D_TYPE
static bool is_regular_file(const int dfd, const char *name) {
	struct stat sb;
	if (fstatat(dfd, name, &sb, 0) == 0) {
		return S_ISREG(sb.st_mode);
	}
	return false;
}
#endif

static struct lenstr * keygen(const struct wustr *file, struct key_pool *pool,
struct collator *icu) {
	struct lenstr *ptr;
	const size_t count = file->len * EXPAND_FACTOR;
	if (pool->bufpos + count + sizeof(*ptr) >= POOL_SIZE) {
		if (!grow_buffer(&pool->bufs, &pool->alloc, pool->nr, sizeof(*pool->bufs))) {
			return NULL;
		}
		pool->bufs[pool->nr] = malloc(POOL_SIZE);
		if (!pool->bufs[pool->nr]) {
			return NULL;
		}
		++pool->nr;
		pool->bufpos = 0;
	}

	ptr = (struct lenstr *)(pool->bufs[pool->nr - 1] + pool->bufpos);

	uiter_setUTF8(&icu->iter, (const char *)file->str, (int32_t)file->len);
	uint32_t state[2] = {0};
	const int32_t written = ucol_nextSortKeyPart(icu->coll, &icu->iter,
		state, ptr->str, (int32_t)count, &icu->err);
	ptr->len = (uint16_t)written;

	const size_t align = sizeof(*ptr) - 1;
	pool->bufpos += sizeof(*ptr) + ( ((size_t)written + align) & (~align) ); // align to word
	return ptr;
}

static bool pathcat(struct fs_dir *list, const struct wustr_mut *parent,
const struct wustr *file, struct collator *icu) {
	struct lenstr *key = keygen(file, &list->pool, icu);
	if (!key) {
		return false;
	}

	// parent->str is either empty or has a trailing slash
	char *name = malloc(parent->len + file->len + 1);
	if (name) {
		memcpy(name, parent->str, parent->len);
		memcpy(name + parent->len, file->str, file->len + 1);
		list->entries[list->len] = (struct fs_entry) {
			.key = key,
			.name = name,
		};
		++list->len;
	}
	return name;
}

static bool filter_dir(DIR *dp, struct path *path, struct fs_dir *list,
struct fs_entry *init_key, struct collator *icu) {
	size_t alloc = EXPECTED_FILES;
	*list = (struct fs_dir) {
		.len = 0,
		.pool = {
			.alloc = 8,
			.nr = 0,
			.bufpos = POOL_SIZE,
		},
	};

	list->entries = malloc(alloc * sizeof(*list->entries)),
	list->pool.bufs = malloc(list->pool.alloc * sizeof(*list->pool.bufs));
	if (!list->entries || !list->pool.bufs) {
		return false;
	}

	if (path->file.str) {
		if (!pathcat(list, &path->parent, &path->file, icu)) {
			return false;
		}
		if (init_key) {
			*init_key = list->entries[0];
		}
	} else if (init_key) {
		*init_key = (struct fs_entry){0};
	}

	const struct dirent *dirent;
	while ( (dirent = readdir(dp)) ) {
#ifdef _DIRENT_HAVE_D_TYPE
		switch (dirent->d_type) {
		case DT_REG:
		case DT_LNK:
		case DT_UNKNOWN:
			break;
		default:
			continue;
		}
#else
		if (!is_regular_file(dirfd(dp), dirent->d_name)) {
			continue;
		}
#endif

#ifdef _DIRENT_HAVE_D_NAMLEN
		const struct wustr name = {.len = (size_t)dirent->d_namlen,
			.str = dirent->d_name};
#else
		const struct wustr name = wustr_str(dirent->d_name);
#endif

		if (wustr_eq(path->file, name)) {
			path->file.len = 0;
			continue;
		}

		if (!known_extension(name)) {
			continue;
		}
		bool ok = grow_buffer(&list->entries, &alloc, list->len, sizeof(*list->entries))
			&& pathcat(list, &path->parent, &name, icu);
		if (!ok) {
			return false;
		}
	}
	return list->len > 0;
}

static DIR * get_dir(const char *name, struct path *path) {
	*path = (struct path){0};
	DIR *dp = NULL;
	const size_t namelen = strlen(name);
	if (namelen > 0) {
		errno = 0;
		dp = opendir(name);
		if (dp) {
			const size_t dirlen = namelen + (name[dirlen-1] != '/');
			if (wustr_malloc(&path->parent, dirlen)) {
				memcpy(path->parent.str, name, namelen);
				path->parent.str[dirlen-1] = '/';
				path->parent.str[dirlen] = 0;
			}
		} else if (errno == ENOTDIR) {
			errno = 0;
			const char *slash = strrchr(name, '/');
			if (slash) {
				++slash;
				const size_t dirlen = (size_t)(slash - name);
				path->file = (struct wustr){
					.len = namelen - dirlen,
					.str = (unsigned char *)slash,
				};
				wustr_memdup(&path->parent, name, dirlen);
			} else {
				path->file = (struct wustr){
					.len = namelen,
					.str = (unsigned char *)name,
				};
				wustr_strdup(&path->parent, "");
			}
			if (path->parent.str) {
				dp = opendir(path->parent.str[0]
					? (char *)path->parent.str : ".");
			}
		}
	} else {
		dp = opendir(".");
		wustr_strdup(&path->parent, "");
	}
	return dp;
}

static bool get_files(const char *name, struct fs_dir *list,
struct fs_entry *init_key) {
	struct path path;
	DIR *dp = get_dir(name, &path);
	bool ok = false;
	if (dp) {
		if (path.parent.str) {
			struct collator icu = {
				.coll = ucol_open(NULL, &icu.err),
			};
			if (U_SUCCESS(icu.err)) {
				ucol_setAttribute(icu.coll,
					UCOL_NUMERIC_COLLATION, UCOL_ON, &icu.err);
				ok = filter_dir(dp, &path, list, init_key, &icu);
				ucol_close(icu.coll);
			}
		}
		closedir(dp);
	}
	// A string may be allocated even if the directory couldn't be opened.
	wustr_free(&path.parent);
	return ok;
}

static int icu_strcoll(const void *restrict v1, const void *restrict v2) {
	const struct fs_entry *f1 = v1;
	const struct fs_entry *f2 = v2;
	const struct lenstr *l1 = f1->key;
	const struct lenstr *l2 = f2->key;
	return memcmp(l1->str, l2->str, zumin(l1->len, l2->len));
}

static size_t sort_dir_entries(struct fs_dir *dir, const struct fs_entry *init_key) {
	qsort(dir->entries, dir->len, sizeof(*dir->entries), icu_strcoll);
	if (init_key->name) {
		const struct fs_entry *loc = bsearch(init_key, dir->entries,
			dir->len, sizeof(*dir->entries), icu_strcoll);
		return (size_t)(loc - dir->entries);
	}
	return 0;
}

char ** fs_filter_sort(const char *name, size_t *nr, size_t *start_idx) {
	struct fs_dir list = {0};
	struct fs_entry init_key = {0};

	if (get_files(name, &list, &init_key)) {
		*start_idx = sort_dir_entries(&list, &init_key);
		*nr = list.len;
		return fs_dir_names(&list);
	}
	fs_dir_free(&list);
	return NULL;
}
