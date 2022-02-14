#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "common.h"
#include "term.h"
#include "wutree.h"

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
	if (branch->is_leaf) {
		puts("FIXME! Tried to grow a leaf node.");
		return NULL;
	}

	if (branch->len + reserve >= branch->alloc) {
		const size_t new_alloc = branch->alloc + branch->alloc / 4
			+ reserve;
		void *hold = realloc(branch->pick.branch,
			new_alloc * sizeof(*branch->pick.branch));
		if (!hold) {
			return NULL;
		}
		branch->alloc = new_alloc;
		branch->pick.branch = hold;
	}
	return branch->pick.branch + branch->len;
}


void tree_unroot(struct wu_tree *root) {
	if (root->name_len > sizeof(root->name.array)) {
		free(root->name.string);
	}

	switch (root->is_leaf) {
	case not_a_leaf:
		for (size_t i = 0; i < root->len; ++i) {
			tree_unroot(root->pick.branch + i);
			break;
		}
		free(root->pick.branch);
		break;
	case wu_leaf_string:
		free(root->pick.string);
		break;
	default:
		break;
	}
}

static void ident_print(const struct wu_tree *node, const size_t max_x,
const size_t max_y, const size_t ident, FILE *out) {
	for (size_t n = 0; n < ident; ++n) {
		fputc(' ', out);
	}

	fwrite(get_name(node), 1, node->name_len, out);
	switch (node->is_leaf) {
	case not_a_leaf:
		if (node->len > max_y) {
			fprintf(out, ": <%zu items hidden>", node->len);
		} else {
			fputs(":\n", out);
			for (size_t i = 0; i < node->len; ++i) {
				ident_print(node->pick.branch + i, max_x - 1,
					max_y - max_y/4, ident + 1, out);
			}
			return;
		}
		break;
	case wu_leaf_string:
		if (node->len > max_x) {
			fputs(": <omitted long string>", out);
		} else {
			fputs(": ", out);
			fwrite(node->pick.string, 1, node->len, out);
		}
		break;
	case wu_leaf_array:
		fputs(": ", out);
		fwrite(node->pick.array, 1, node->len, out);
		break;
	case wu_leaf_unsigned:
		fprintf(out, ": %lu", node->pick.u);
		break;
	case wu_leaf_signed:
		fprintf(out, ": %ld", node->pick.d);
		break;
	case wu_leaf_double:
		fprintf(out, ": %g", node->pick.g);
		break;
	case wu_leaf_time:
		fputs(": ", out);
		rfc3339_format(node->pick.time, out);
		break;
	case wu_leaf_bool:
		fputs((node->pick.b) ? ": true" : ": false", out);
	}
	fputc('\n', out);
}

void tree_print(const struct wu_tree *node, const size_t max_x,
const size_t max_y) {
	ident_print(node, max_x, max_y, 0, stdout);
}

bool tree_graft_measured_leaf(struct wu_tree *par, const char *restrict name,
char *restrict value, const size_t len) {
	struct wu_tree *leaf = irrigate(par, 1);
	if (!leaf) {
		free(value);
		return false;
	}

	if (!copy_name(leaf, name)) {
		free(value);
		return false;
	}

	const size_t end = term_printable_len(value, len);
	leaf->len = end;

	if (end > sizeof(leaf->pick.array)) {
		leaf->is_leaf = wu_leaf_string;
		if (end < len) {
			char *hold = realloc(value, end);
			if (hold) {
				value = hold;
			}
		}
		leaf->pick.string = value;
	} else {
		leaf->is_leaf = wu_leaf_array;
		memcpy(leaf->pick.array, value, end);
		free(value);
	}

	++par->len;
	return true;
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
	if (len > sizeof(par->pick.array)) {
		char *copy = memdup(value, len);
		if (copy) {
			return tree_graft_measured_leaf(par, name, copy, len);
		}
	} else {
		struct wu_tree *leaf = irrigate(par, 1);
		if (!leaf) {
			return false;
		}

		if (!copy_name(leaf, name)) {
			return false;
		}
		leaf->is_leaf = wu_leaf_array;
		leaf->len = len;
		memcpy(leaf->pick.array, value, len);

		++par->len;
		return true;
	}
	return false;
}

bool tree_sprout_leaf(struct wu_tree *par, const char *restrict name,
const char *restrict value) {
	return tree_sprout_measured_leaf(par, name, value, strlen(value));
}

static bool sap_bud(struct wu_tree *bud, const char *name,
const enum wu_leaf_type type, const union wu_leaf_val val) {
	switch (type) {
	case wu_leaf_unsigned:
	case wu_leaf_signed:
	case wu_leaf_double:
	case wu_leaf_time:
	case wu_leaf_bool:
		if (copy_name(bud, name)) {
			bud->is_leaf = type;
			bud->pick = val;
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
	if (!bud) {
		return false;
	}
	const bool success = sap_bud(bud, name, leaf.type, leaf.val);
	if (success) {
		++par->len;
	}
	return success;
}

bool tree_bud_leaves(struct wu_tree *par, const struct wu_tree_sap *sap,
const size_t len) {
	struct wu_tree *buds = irrigate(par, len);
	if (!buds) {
		return false;
	}

	for (size_t i = 0; i < len; ++i) {
		const bool success = sap_bud(buds + i, sap[i].name, sap[i].type,
			sap[i].val);
		if (success) {
			++par->len;
		} else {
			return false;
		}
	}
	return true;
}

struct wu_tree * tree_sprout_branch(struct wu_tree *par, const char *name) {
	struct wu_tree *branch = irrigate(par, 1);
	if (!branch) {
		return NULL;
	}

	if (!tree_sow(branch, name)) {
		return NULL;
	}
	++par->len;
	return branch;
}

struct wu_tree * tree_findadd_branch(struct wu_tree *par, const char *name) {
	for (size_t i = par->len - 1; i < par->len; --i) {
		struct wu_tree *branch = par->pick.branch + i;
		const char *branch_name = get_name(branch);
		if (!strncmp(branch_name, name, branch->name_len)) {
			return branch;
		}
	}
	return tree_sprout_branch(par, name);
}

bool tree_sow(struct wu_tree *root, const char *name) {
	if (!copy_name(root, name)) {
		return false;
	}
	root->is_leaf = not_a_leaf;
	root->alloc = 8;
	root->len = 0;
	root->pick.branch = malloc(sizeof(*root->pick.branch) * root->alloc);
	if (!root->pick.branch) {
		return false;
	}
	return true;
}
