#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "common.h"
#include "term.h"
#include "wutree.h"
#include "wustr.h"

static const char * get_name(const struct wu_tree *node) {
	if (node->name_len > sizeof(node->name.array)) {
		return node->name.string;
	}
	return node->name.array;
}

static bool copy_name(struct wu_tree *node, const char *name) {
	node->name_len = strlen(name);
	if (node->name_len > sizeof(node->name.array)) {
		node->name.string = memdup(name, node->name_len);
		return (bool)node->name.string;
	}
	memcpy(node->name.array, name, node->name_len);
	return true;
}

static struct wu_tree * irrigate(struct wu_tree *branch, const size_t reserve) {
	if (branch->leaf.type != not_a_leaf) {
		puts("BUG! Tried to grow a leaf node.");
		return NULL;
	}

	if (branch->len + reserve >= branch->alloc) {
		const size_t new_alloc = branch->alloc + branch->alloc / 4
			+ reserve;
		void *hold = realloc(branch->leaf.val.branch,
			new_alloc * sizeof(*branch->leaf.val.branch));
		if (!hold) {
			return NULL;
		}
		branch->alloc = new_alloc;
		branch->leaf.val.branch = hold;
	}
	return branch->leaf.val.branch + branch->len;
}


void tree_unroot(struct wu_tree *root) {
	if (root->name_len > sizeof(root->name.array)) {
		free(root->name.string);
	}

	switch (root->leaf.type) {
	case not_a_leaf:
		for (size_t i = 0; i < root->len; ++i) {
			tree_unroot(root->leaf.val.branch + i);
		}
		free(root->leaf.val.branch);
		break;
	case wu_leaf_string:
		free(root->leaf.val.string);
		break;
	default:
		break;
	}
}

static void indent_print(const struct wu_tree *node, const size_t max_x,
const size_t max_y, const size_t indent, FILE *out) {
	for (size_t n = 0; n < indent; ++n) {
		fputc(' ', out);
	}

	fwrite(get_name(node), 1, node->name_len, out);
	switch (node->leaf.type) {
	case not_a_leaf:
		if (node->len > max_y || !max_x) {
			fprintf(out, ": <%zu items hidden>", node->len);
		} else {
			fputs(":\n", out);
			for (size_t i = 0; i < node->len; ++i) {
				indent_print(node->leaf.val.branch + i, max_x - 1,
					max_y - max_y/4, indent + 1, out);
			}
			return;
		}
		break;
	case wu_leaf_string:
		if (node->len > max_x) {
			fputs(": <omitted long string>", out);
		} else {
			fputs(": ", out);
			fwrite(node->leaf.val.string, 1, node->len, out);
		}
		break;
	case wu_leaf_array:
		fputs(": ", out);
		fwrite(node->leaf.val.array, 1, node->len, out);
		break;
	case wu_leaf_unsigned:
		fprintf(out, ": %lu", node->leaf.val.u);
		break;
	case wu_leaf_signed:
		fprintf(out, ": %ld", node->leaf.val.d);
		break;
	case wu_leaf_double:
		fprintf(out, ": %g", node->leaf.val.g);
		break;
	case wu_leaf_time:
		fputs(": ", out);
		rfc3339_format(node->leaf.val.time, out);
		break;
	case wu_leaf_bool:
		fputs((node->leaf.val.b) ? ": true" : ": false", out);
	}
	fputc('\n', out);
}

void tree_print(const struct wu_tree *node, const size_t max_x,
const size_t max_y) {
	indent_print(node, max_x, max_y, 0, stdout);
}

bool tree_graft_measured_leaf(struct wu_tree *par, const char *restrict name,
char *restrict value, const size_t len) {
	struct wu_tree *leaf = irrigate(par, 1);
	if (leaf && copy_name(leaf, name)) {
		const size_t end = term_printable_len(value, len);
		leaf->len = end;

		if (end > sizeof(leaf->leaf.val.array)) {
			leaf->leaf.type = wu_leaf_string;
			if (end < len) {
				char *hold = realloc(value, end);
				if (hold) {
					value = hold;
				}
			}
			leaf->leaf.val.string = value;
		} else {
			leaf->leaf.type = wu_leaf_array;
			memcpy(leaf->leaf.val.array, value, end);
			free(value);
		}
		++par->len;
		return true;
	}
	free(value);
	return false;
}

bool tree_graft_unsafe_leaf(struct wu_tree *par, const char *name,
void *restrict data, size_t len) {
	char *val = term_format_unsafe_or_same(data, len, &len);
	if (val) {
		return tree_graft_measured_leaf(par, name, val, len);
	}
	free(data);
	return false;
}

bool tree_graft_leaf(struct wu_tree *par, const char *restrict name,
char *restrict value) {
	return tree_graft_measured_leaf(par, name, value, strlen(value));
}

bool tree_sprout_unsafe_leaf(struct wu_tree *par, const char *name,
const void *restrict data, size_t len) {
	char *val = term_format_unsafe(data, len, &len);
	if (val) {
		return tree_graft_measured_leaf(par, name, val, len);
	}
	return false;
}

bool tree_sprout_measured_leaf(struct wu_tree *par, const char *restrict name,
const char *restrict value, size_t len) {
	len = term_printable_len(value, len);
	if (len > sizeof(par->leaf.val.array)) {
		char *copy = memdup(value, len);
		if (copy) {
			return tree_graft_measured_leaf(par, name, copy, len);
		}
	} else {
		struct wu_tree *leaf = irrigate(par, 1);
		if (leaf && copy_name(leaf, name)) {
			leaf->leaf.type = wu_leaf_array;
			leaf->len = len;
			memcpy(leaf->leaf.val.array, value, len);

			++par->len;
			return true;
		}
	}
	return false;
}

bool tree_sprout_leaf(struct wu_tree *par, const char *restrict name,
const char *restrict value) {
	return tree_sprout_measured_leaf(par, name, value, strlen(value));
}

static bool sap_bud(struct wu_tree *bud, const char *name,
const struct wu_leaf leaf, size_t *len) {
	switch (leaf.type) {
	case wu_leaf_unsigned:
	case wu_leaf_signed:
	case wu_leaf_double:
	case wu_leaf_time:
	case wu_leaf_bool:
		if (copy_name(bud, name)) {
			bud->leaf = leaf;
			*len += 1;
			return true;
		}
	case not_a_leaf:
	case wu_leaf_array:
	case wu_leaf_string:
		break;
	}
	return false;
}

bool tree_bud_leaf(struct wu_tree *par, const char *name,
const struct wu_leaf leaf) {
	struct wu_tree *bud = irrigate(par, 1);
	return bud ? sap_bud(bud, name, leaf, &par->len) : false;
}

bool tree_bud_leaves(struct wu_tree *par, const struct wu_tree_sap *sap,
const size_t len) {
	struct wu_tree *buds = irrigate(par, len);
	if (buds) {
		for (size_t i = 0; i < len; ++i) {
			if (!sap_bud(buds + i, sap[i].name, sap[i].leaf, &par->len)) {
				return false;
			}
		}
	}
	return buds;
}

struct wu_tree * tree_sprout_branch(struct wu_tree *par, const char *name) {
	struct wu_tree *branch = irrigate(par, 1);
	if (branch && tree_sow(branch, name)) {
		++par->len;
		return branch;
	}
	return NULL;
}

struct wu_tree * tree_find_branch(struct wu_tree *par, const char *name) {
	const struct wuptr n = wuptr_str(name);
	for (size_t i = 0; i < par->len; ++i) {
		struct wu_tree *b = par->leaf.val.branch + i;
		if (wuptr_eq(n, wuptr_mem(get_name(b), b->name_len))) {
			return b;
		}
	}
	return NULL;
}

struct wu_tree * tree_findadd_branch(struct wu_tree *par, const char *name) {
	struct wu_tree *branch = tree_find_branch(par, name);
	if (branch) {
		return branch;
	}
	return tree_sprout_branch(par, name);
}

struct wu_tree * tree_find_path(struct wu_tree *par, const char *path[],
const size_t len) {
	for (size_t i = 0; i < len; ++i) {
		if (!par || par->leaf.type != not_a_leaf) {
			return NULL;
		}
		par = tree_find_branch(par, path[i]);
	}
	return par;
}

bool tree_sow(struct wu_tree *root, const char *name) {
	if (copy_name(root, name)) {
		root->leaf.type = not_a_leaf;
		root->alloc = 8;
		root->len = 0;
		root->leaf.val.branch = malloc(sizeof(*root->leaf.val.branch)
			* root->alloc);
		return (bool)root->leaf.val.branch;
	}
	return false;
}
