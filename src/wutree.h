#ifndef WU_TREE
#define WU_TREE

#include <stdbool.h>
#include <time.h>

enum wu_leaf_type {
	not_a_leaf = 0,
	wu_leaf_array,
	wu_leaf_string,
	wu_leaf_unsigned,
	wu_leaf_signed,
	wu_leaf_double,
	wu_leaf_time,
};

union wu_leaf_val {
	struct wu_tree *branch;
	char array[sizeof(char *)];
	char *string;
	unsigned long u;
	long d;
	double g;
	time_t time;
};

struct wu_leaf {
	union wu_leaf_val val;
	enum wu_leaf_type type;
};

struct wu_tree_sap {
	const char *name;
	enum wu_leaf_type type;
	union wu_leaf_val value;
};

struct wu_tree {
	size_t name_len;
	union {
		char array[sizeof(char *)];
		char *string;
	} name;
	size_t alloc;
	size_t len;
	union wu_leaf_val pick;
	enum wu_leaf_type is_leaf;
};

void tree_unroot(struct wu_tree *root);

void tree_print(const struct wu_tree *node, size_t max_x, size_t max_y);

bool tree_graft_measured_leaf(struct wu_tree *par, const char *name,
char *value, size_t len);

bool tree_graft_unsafe_leaf(struct wu_tree *par, const char *name,
void *data, size_t len);

bool tree_graft_leaf(struct wu_tree *par, const char *name, char *value);

bool tree_sprout_unsafe_leaf(struct wu_tree *par, const char *name,
const void *data, size_t len);

bool tree_sprout_measured_leaf(struct wu_tree *par, const char *name,
const char *value, size_t len);

bool tree_sprout_leaf(struct wu_tree *par, const char *name,
const char *value);

bool tree_bud_leaf(struct wu_tree *par, const char *name, struct wu_leaf leaf);

bool tree_bud_leaves(struct wu_tree *par, const struct wu_tree_sap *sap,
size_t len);

struct wu_tree * tree_sprout_branch(struct wu_tree *par, const char *name);

struct wu_tree * tree_findadd_branch(struct wu_tree *par, const char *name);

bool tree_sow(struct wu_tree *node, const char *name);

#endif /* WU_TREE */
