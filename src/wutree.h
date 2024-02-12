// SPDX-License-Identifier: 0BSD
#ifndef WU_TREE
#define WU_TREE

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
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
	struct wu_tree *b;
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
	struct wu_emb_str s;
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

struct wu_tree_sap {
	const char *name;
	struct wu_leaf leaf;
};

struct wu_tree {
	struct wu_emb_str name;
	struct wu_leaf leaf;
};

void tree_unroot(struct wu_tree *root);

void tree_print(const struct wu_tree *node, size_t max_x, size_t max_y,
size_t indent, FILE *out);


bool tree_add_leaf_utf8_len(struct wu_tree *par, const char *name,
struct wuptr value);

bool tree_add_leaf_utf8_limit(struct wu_tree *par, const char *name,
struct wuptr value);

bool tree_add_leaf_utf8(struct wu_tree *par, const char *name,
const char *value);


bool tree_add_leaf_len(struct wu_tree *par, const char *name,
struct wuptr value, const char *encoding);

bool tree_add_leaf_limit(struct wu_tree *par, const char *name,
struct wuptr value, const char *encoding);

bool tree_add_leaf(struct wu_tree *par, const char *name,
const char *value, const char *encoding);


bool tree_bud_leaf_u(struct wu_tree *par, const char *name, unsigned long val);

bool tree_bud_leaf_d(struct wu_tree *par, const char *name, long val);

bool tree_bud_leaf_f(struct wu_tree *par, const char *name, double val);

bool tree_bud_leaf_time(struct wu_tree *par, const char *name, time_t val);

bool tree_bud_leaf_bool(struct wu_tree *par, const char *name, bool val);

bool tree_bud_leaves(struct wu_tree *par, const struct wu_tree_sap *sap,
size_t len);


struct wu_tree * tree_add_branch(struct wu_tree *par, const char *name);

struct wu_tree * tree_findadd_branch(struct wu_tree *par, const char *name);

struct wu_tree * tree_find_path(struct wu_tree *par, const char *path[],
size_t len);

bool tree_sow(struct wu_tree *root, const char *name);

struct wu_tree * tree_plant(const char *name);

#endif /* WU_TREE */
