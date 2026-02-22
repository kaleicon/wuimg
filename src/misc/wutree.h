// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2025 kaleido
#ifndef WU_TREE
#define WU_TREE

#include <stdint.h>
#include <time.h>

#include "misc/wustr.h"

enum wu_leaf_type {
	not_a_leaf = 0,
	wu_leaf_string,
	wu_leaf_bin,
	wu_leaf_unsigned,
	wu_leaf_signed,
	wu_leaf_float,
	wu_leaf_time,
	wu_leaf_bool,
};

struct wu_branch {
	size_t len;
	size_t alloc;
	struct wutree *b;
};

struct wu_emb_str {
	size_t len;
	union {
		uint8_t arr[sizeof(uint8_t *)];
		uint8_t *str;
	} s;
};

union wu_leaf_val {
	struct wu_branch branch;
	struct wu_emb_str emb;
	uint64_t u;
	int64_t d;
	double f;
	time_t time;
	bool b;
};

struct wu_leaf {
	enum wu_leaf_type type;
	union wu_leaf_val val;
};

struct wutree_sap {
	const char *name;
	struct wu_leaf leaf;
};

struct wutree {
	struct wu_emb_str name;
	struct wu_leaf leaf;
};

void tree_unroot(struct wutree *root);

void tree_print(const struct wutree *node, size_t max_x, size_t max_y,
size_t indent, FILE *out);


bool tree_add_leaf_utf8_len(struct wutree *par, const char *name,
struct wuptr value);

bool tree_add_leaf_utf8_limit(struct wutree *par, const char *name,
struct wuptr value);

bool tree_add_leaf_utf8(struct wutree *par, const char *name,
const char *value);


bool tree_add_leaf_len(struct wutree *par, const char *name,
struct wuptr value, const char *encoding);

bool tree_add_leaf_limit(struct wutree *par, const char *name,
struct wuptr value, const char *encoding);

bool tree_add_leaf(struct wutree *par, const char *name,
const char *value, const char *encoding);


bool tree_bud_leaf_u(struct wutree *par, const char *name, uint64_t val);

bool tree_bud_leaf_d(struct wutree *par, const char *name, int64_t val);

bool tree_bud_leaf_f(struct wutree *par, const char *name, double val);

bool tree_bud_leaf_time(struct wutree *par, const char *name, time_t val);

bool tree_bud_leaf_bool(struct wutree *par, const char *name, bool val);

bool tree_bud_leaves(struct wutree *par, const struct wutree_sap *sap,
size_t len);


struct wutree * tree_graft_branch(struct wutree *par, struct wutree *graft,
const char *new_name);

struct wutree * tree_add_branch(struct wutree *par, const char *name);

struct wutree * tree_findadd_branch(struct wutree *par, const char *name);

struct wutree * tree_find_path(struct wutree *par, const char *path[],
size_t len);

bool tree_sow(struct wutree *root, const char *name);

struct wutree * tree_plant(const char *name);

#endif /* WU_TREE */
