// SPDX-License-Identifier: 0BSD
#include <string.h>
#include <stdlib.h>

#include "misc/common.h"
#include "misc/mem.h"
#include "misc/term.h"
#include "misc/time.h"
#include "misc/utf8.h"
#include "misc/wutree.h"

static bool is_emb_ptr(const struct wu_emb_str *emb) {
	return emb->len > sizeof(emb->s.arr);
}

static void free_emb_str(const struct wu_emb_str *emb) {
	if (is_emb_ptr(emb)) {
		free(emb->s.str);
	}
}

static const uint8_t * get_emb_str(const struct wu_emb_str *emb) {
	return is_emb_ptr(emb) ? emb->s.str : emb->s.arr;
}

static void graft_emb_mem(struct wu_emb_str *emb, struct wustr src) {
	emb->len = src.len;
	if (is_emb_ptr(emb)) {
		emb->s.str = src.str;
	} else {
		memcpy(emb->s.arr, src.str, src.len);
		wustr_free(&src);
	}
}

static bool copy_emb_mem(struct wu_emb_str *emb, const struct wuptr src) {
	emb->len = src.len;
	if (is_emb_ptr(emb)) {
		emb->s.str = memdup(src.ptr, emb->len);
		return (bool)emb->s.str;
	}
	memcpy(emb->s.arr, src.ptr, emb->len);
	return true;
}

static bool copy_name(struct wutree *tree, const char *name) {
	return copy_emb_mem(&tree->name, wuptr_str(name));
}

static struct wu_branch * get_branch(struct wutree *tree) {
	if (tree->leaf.type == not_a_leaf) {
		return &tree->leaf.val.branch;
	}
	return NULL;
}

static struct wutree * irrigate(struct wutree *par, const size_t reserve) {
	struct wu_branch *branch = get_branch(par);
	if (!branch) {
		fatal_bug("wutree_irrigate()", "Tried to grow a leaf node.");
		return NULL;
	}

	if (branch->len + reserve >= branch->alloc) {
		const size_t new_alloc = branch->alloc + branch->alloc / 4
			+ reserve;
		void *hold = small_realloc(branch->b, new_alloc, sizeof(*branch->b));
		if (!hold) {
			return NULL;
		}
		branch->alloc = new_alloc;
		branch->b = hold;
	}
	return branch->b + branch->len;
}


void tree_unroot(struct wutree *root) {
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
	case wu_leaf_bin:
		free_emb_str(&root->leaf.val.emb);
		break;
	default:
		break;
	}
}

void tree_print(const struct wutree *node, const size_t max_x,
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
				tree_print(branch->b + i, max_x - 1,
					max_y - max_y/4, indent + 1, out);
			}
			return;
		}
		break;
	case wu_leaf_string:
	case wu_leaf_bin:
		;const struct wu_emb_str *emb = &node->leaf.val.emb;
		if (emb->len > max_x) {
			fputs(": <omitted long string>", out);
		} else {
			fputs(": ", out);
			term_print_escaped(get_emb_str(emb), emb->len,
				node->leaf.type == wu_leaf_string, out);
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

static bool add_leaf_copy(struct wutree *par, const char *restrict name,
const struct wuptr value, const bool is_utf8) {
	struct wutree *branch = irrigate(par, 1);
	if (branch && copy_name(branch, name)) {
		if (copy_emb_mem(&branch->leaf.val.emb, value)) {
			branch->leaf.type = is_utf8 ? wu_leaf_string : wu_leaf_bin;
			++par->leaf.val.branch.len;
			return true;
		}
	}
	return false;
}

static bool add_leaf_owned(struct wutree *par, const char *restrict name,
struct wustr value, const bool is_utf8) {
	struct wutree *branch = irrigate(par, 1);
	if (branch && copy_name(branch, name)) {
		struct wu_leaf *leaf = &branch->leaf;
		graft_emb_mem(&leaf->val.emb, value);
		leaf->type = is_utf8 ? wu_leaf_string : wu_leaf_bin;
		++par->leaf.val.branch.len;
		return true;
	}
	wustr_free(&value);
	return false;
}

bool tree_add_leaf_utf8_len(struct wutree *par, const char *name,
const struct wuptr value) {
	return add_leaf_copy(par, name, value, true);
}

bool tree_add_leaf_utf8_limit(struct wutree *par, const char *restrict name,
struct wuptr value) {
	value.len = strnlen((const char *)value.ptr, value.len);
	return tree_add_leaf_utf8_len(par, name, value);
}

bool tree_add_leaf_utf8(struct wutree *par, const char *restrict name,
const char *restrict value) {
	return tree_add_leaf_utf8_len(par, name, wuptr_str(value));
}

bool tree_add_leaf_len(struct wutree *par, const char *restrict name,
const struct wuptr value, const char *restrict encoding) {
	struct wustr utf;
	switch (utf8_convert((const char *)value.ptr, value.len, &utf, encoding)) {
	case trit_true:
		return add_leaf_owned(par, name, utf, true);
	case trit_false:
		return tree_add_leaf_utf8_len(par, name, value);
	case trit_what:
		break;
	}
	return false;
}

bool tree_add_leaf_limit(struct wutree *par, const char *name,
struct wuptr value, const char *encoding) {
	value.len = strnlen((const char *)value.ptr, value.len);
	return tree_add_leaf_len(par, name, value, encoding);
}

bool tree_add_leaf(struct wutree *par, const char *restrict name,
const char *restrict value, const char *restrict encoding) {
	return tree_add_leaf_len(par, name, wuptr_str(value), encoding);
}


static bool sap_bud(struct wutree *bud, const char *name,
const struct wu_leaf leaf, struct wutree *parent) {
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
	case wu_leaf_bin:
		break;
	}
	return false;
}

static bool bud_leaf(struct wutree *par, const char *name,
const struct wu_leaf leaf) {
	struct wutree *bud = irrigate(par, 1);
	return bud ? sap_bud(bud, name, leaf, par) : false;
}

bool tree_bud_leaf_u(struct wutree *par, const char *name, const uint64_t val) {
	return bud_leaf(par, name,
		(struct wu_leaf){.val.u = val, .type = wu_leaf_unsigned});
}

bool tree_bud_leaf_d(struct wutree *par, const char *name, const int64_t val) {
	return bud_leaf(par, name,
		(struct wu_leaf){.val.d = val, .type = wu_leaf_signed});
}

bool tree_bud_leaf_f(struct wutree *par, const char *name, const double val) {
	return bud_leaf(par, name,
		(struct wu_leaf){.val.f = val, .type = wu_leaf_float});
}

bool tree_bud_leaf_time(struct wutree *par, const char *name, const time_t val) {
	return bud_leaf(par, name,
		(struct wu_leaf){.val.time = val, .type = wu_leaf_time});
}

bool tree_bud_leaf_bool(struct wutree *par, const char *name, const bool val) {
	return bud_leaf(par, name,
		(struct wu_leaf){.val.b = val, .type = wu_leaf_bool});
}

bool tree_bud_leaves(struct wutree *par, const struct wutree_sap *sap,
const size_t len) {
	struct wutree *buds = irrigate(par, len);
	if (buds) {
		for (size_t i = 0; i < len; ++i) {
			if (!sap_bud(buds + i, sap[i].name, sap[i].leaf, par)) {
				return false;
			}
		}
	}
	return buds;
}

struct wutree * tree_add_branch(struct wutree *par, const char *name) {
	struct wutree *branch = irrigate(par, 1);
	if (branch && tree_sow(branch, name)) {
		++par->leaf.val.branch.len;
		return branch;
	}
	return NULL;
}

static struct wutree * tree_find_branch(struct wutree *par, const char *name) {
	struct wu_branch *branch = get_branch(par);
	if (branch) {
		const struct wuptr n = wuptr_str(name);
		for (size_t i = 0; i < branch->len; ++i) {
			struct wutree *b = branch->b + i;
			if (wuptr_eq(n, wuptr_mem(get_emb_str(&b->name), b->name.len))) {
				return b;
			}
		}
	}
	return NULL;
}

struct wutree * tree_findadd_branch(struct wutree *par, const char *name) {
	struct wutree *branch = tree_find_branch(par, name);
	if (branch) {
		return branch;
	}
	return tree_add_branch(par, name);
}

struct wutree * tree_find_path(struct wutree *par, const char *path[],
const size_t len) {
	for (size_t i = 0; i < len && par; ++i) {
		if (par->leaf.type != not_a_leaf) {
			return NULL;
		}
		par = tree_find_branch(par, path[i]);
	}
	return par;
}

bool tree_sow(struct wutree *root, const char *name) {
	if (copy_name(root, name)) {
		root->leaf.type = not_a_leaf;
		struct wu_branch *branch = &root->leaf.val.branch;
		branch->alloc = 8;
		branch->len = 0;
		branch->b = small_malloc(branch->alloc, sizeof(*branch->b));
		return (bool)branch->b;
	}
	return false;
}

struct wutree * tree_plant(const char *name) {
	struct wutree *root = calloc(1, sizeof(*root));
	if (root) {
		if (tree_sow(root, name)) {
			return root;
		}
		free(root);
	}
	return NULL;
}
