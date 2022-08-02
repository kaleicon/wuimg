#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "common.h"
#include "term.h"
#include "wutree.h"
#include "wustr.h"

static bool is_emb_str(const struct wu_emb_str *emb) {
	return emb->len > sizeof(emb->s.arr);
}

static void free_emb_str(const struct wu_emb_str *emb) {
	if (is_emb_str(emb)) {
		free(emb->s.str);
	}
}

static const char * get_emb_str(const struct wu_emb_str *emb) {
	return is_emb_str(emb) ? emb->s.str : emb->s.arr;
}

static bool copy_emb_mem(struct wu_emb_str *emb, const void *mem,
const size_t len) {
	emb->len = len;
	if (is_emb_str(emb)) {
		emb->s.str = memdup(mem, emb->len);
		return (bool)emb->s.str;
	}
	memcpy(emb->s.arr, mem, emb->len);
	return true;
}

static bool copy_name(struct wu_tree *tree, const char *name) {
	return copy_emb_mem(&tree->name, name, strlen(name));
}

static struct wu_branch * get_branch(struct wu_tree *tree) {
	if (tree->leaf.type == not_a_leaf) {
		return &tree->leaf.val.branch;
	}
	return NULL;
}

static struct wu_tree * irrigate(struct wu_tree *par, const size_t reserve) {
	struct wu_branch *branch = get_branch(par);
	if (!branch) {
		term_line_put("BUG! Tried to grow a leaf node.", stderr);
		return NULL;
	}

	if (branch->len + reserve >= branch->alloc) {
		const size_t new_alloc = branch->alloc + branch->alloc / 4
			+ reserve;
		void *hold = realloc(branch->b, new_alloc * sizeof(*branch->b));
		if (!hold) {
			return NULL;
		}
		branch->alloc = new_alloc;
		branch->b = hold;
	}
	return branch->b + branch->len;
}


void tree_unroot(struct wu_tree *root) {
	free_emb_str(&root->name);
	switch (root->leaf.type) {
	case not_a_leaf:
		;struct wu_branch *branch = &root->leaf.val.branch;
		for (size_t i = 0; i < branch->len; ++i) {
			tree_unroot(branch->b + i);
		}
		free(branch->b);
		break;
	case wu_leaf_string:
		free_emb_str(&root->leaf.val.s);
		break;
	default:
		break;
	}
}

static void indent_print(const struct wu_tree *node, const size_t max_x,
const size_t max_y, const size_t indent, FILE *out) {
	term_indent(indent, out);
	fwrite(get_emb_str(&node->name), 1, node->name.len, out);
	switch (node->leaf.type) {
	case not_a_leaf:
		;const struct wu_branch *branch = &node->leaf.val.branch;
		if (branch->len > max_y || !max_x) {
			fprintf(out, ": <%zu items hidden>", branch->len);
		} else {
			fputs(":\n", out);
			for (size_t i = 0; i < branch->len; ++i) {
				indent_print(branch->b + i, max_x - 1,
					max_y - max_y/4, indent + 1, out);
			}
			return;
		}
		break;
	case wu_leaf_string:
		;const struct wu_emb_str *emb = &node->leaf.val.s;
		if (emb->len > max_x) {
			fputs(": <omitted long string>", out);
		} else {
			fputs(": ", out);
			term_print_unsafe(get_emb_str(emb), emb->len, out);
		}
		break;
	case wu_leaf_unsigned:
		fprintf(out, ": %lu", node->leaf.val.u);
		break;
	case wu_leaf_signed:
		fprintf(out, ": %ld", node->leaf.val.d);
		break;
	case wu_leaf_float:
		fprintf(out, ": %g", node->leaf.val.f);
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
void *restrict value, const size_t len) {
	if (len) {
		struct wu_tree *leaf = irrigate(par, 1);
		if (leaf && copy_name(leaf, name)) {
			struct wu_emb_str *emb = &leaf->leaf.val.s;
			emb->len = len;
			if (is_emb_str(emb)) {
				emb->s.str = value;
			} else {
				memcpy(emb->s.arr, value, len);
				free(value);
			}
			leaf->leaf.type = wu_leaf_string;
			++par->leaf.val.branch.len;
			return true;
		}
	}
	free(value);
	return false;
}

bool tree_add_measured_leaf(struct wu_tree *par, const char *restrict name,
const void *restrict value, const size_t len) {
	if (len) {
		struct wu_tree *leaf = irrigate(par, 1);
		if (leaf && copy_name(leaf, name)) {
			if (copy_emb_mem(&leaf->leaf.val.s, value, len)) {
				leaf->leaf.type = wu_leaf_string;
				++par->leaf.val.branch.len;
				return true;
			}
		}
	}
	return false;
}

bool tree_add_limited_leaf(struct wu_tree *par, const char *restrict name,
const void *restrict value, const size_t len) {
	return tree_add_measured_leaf(par, name, value, strnlen(value, len));
}

bool tree_add_leaf(struct wu_tree *par, const char *restrict name,
const char *restrict value) {
	return tree_add_measured_leaf(par, name, value, strlen(value));
}

static bool sap_bud(struct wu_tree *bud, const char *name,
const struct wu_leaf leaf, struct wu_tree *parent) {
	switch (leaf.type) {
	case wu_leaf_unsigned:
	case wu_leaf_signed:
	case wu_leaf_float:
	case wu_leaf_time:
	case wu_leaf_bool:
		if (copy_name(bud, name)) {
			bud->leaf = leaf;
			parent->leaf.val.branch.len += 1;
			return true;
		}
	case not_a_leaf:
	case wu_leaf_string:
		break;
	}
	return false;
}

bool tree_bud_leaf(struct wu_tree *par, const char *name,
const struct wu_leaf leaf) {
	struct wu_tree *bud = irrigate(par, 1);
	return bud ? sap_bud(bud, name, leaf, par) : false;
}

bool tree_bud_leaves(struct wu_tree *par, const struct wu_tree_sap *sap,
const size_t len) {
	struct wu_tree *buds = irrigate(par, len);
	if (buds) {
		for (size_t i = 0; i < len; ++i) {
			if (!sap_bud(buds + i, sap[i].name, sap[i].leaf, par)) {
				return false;
			}
		}
	}
	return buds;
}

struct wu_tree * tree_add_branch(struct wu_tree *par, const char *name) {
	struct wu_tree *branch = irrigate(par, 1);
	if (branch && tree_sow(branch, name)) {
		++par->leaf.val.branch.len;
		return branch;
	}
	return NULL;
}

static struct wu_tree * tree_find_branch(struct wu_tree *par, const char *name) {
	struct wu_branch *branch = get_branch(par);
	if (branch) {
		const struct wuptr n = wuptr_str(name);
		for (size_t i = 0; i < branch->len; ++i) {
			struct wu_tree *b = branch->b + i;
			if (wuptr_eq(n, wuptr_mem(get_emb_str(&b->name), b->name.len))) {
				return b;
			}
		}
	}
	return NULL;
}

struct wu_tree * tree_findadd_branch(struct wu_tree *par, const char *name) {
	struct wu_tree *branch = tree_find_branch(par, name);
	if (branch) {
		return branch;
	}
	return tree_add_branch(par, name);
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
		struct wu_branch *branch = &root->leaf.val.branch;
		branch->alloc = 8;
		branch->len = 0;
		branch->b = malloc(sizeof(*branch->b) * branch->alloc);
		return (bool)branch->b;
	}
	return false;
}

struct wu_tree * tree_plant(const char *name) {
	struct wu_tree *root = malloc(sizeof(*root));
	if (root) {
		if (tree_sow(root, name)) {
			return root;
		}
		free(root);
	}
	return NULL;
}
