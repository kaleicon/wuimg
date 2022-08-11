#ifndef WU_TREE
#define WU_TREE

#include <stdbool.h>
#include <time.h>

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
	unsigned long u;
	long d;
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
const void *value, size_t len);

bool tree_add_leaf_utf8_limit(struct wu_tree *par, const char *name,
const char *value, size_t len);

bool tree_add_leaf_utf8(struct wu_tree *par, const char *name,
const char *value);

bool tree_add_leaf_len(struct wu_tree *par, const char *name,
const void *value, size_t len, const char *encoding);

bool tree_add_leaf_limit(struct wu_tree *par, const char *name,
const void *value, size_t len, const char *encoding);

bool tree_add_leaf(struct wu_tree *par, const char *name,
const char *value, const char *encoding);

bool tree_bud_leaf(struct wu_tree *par, const char *name, struct wu_leaf leaf);

bool tree_bud_leaves(struct wu_tree *par, const struct wu_tree_sap *sap,
size_t len);

struct wu_tree * tree_add_branch(struct wu_tree *par, const char *name);

struct wu_tree * tree_findadd_branch(struct wu_tree *par, const char *name);

struct wu_tree * tree_find_path(struct wu_tree *par, const char *path[], size_t len);

bool tree_sow(struct wu_tree *root, const char *name);

struct wu_tree * tree_plant(const char *name);

#endif /* WU_TREE */
