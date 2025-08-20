// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2024 kaleido
#include <string.h>

#include "lib/cbg.h"
#include "misc/bit.h"
#include "misc/common.h"
#include "misc/math.h"
#include "raster/fmt.h"

/* If true, abort decoding if the weights checksum is not valid.
 * There's no point to enabling this. The image either decodes or it doesn't,
 * and fuzzing is useless unless disabled. This is just to document what
 * supposed to happen. */
static const bool CHECK_CHECKSUM = false;

#define V1_NODES 0x100
#define HUFFMAN_NR_CHILDS 2
// Table size. Best value on an old processor
#define TABLE_BITS 12

struct huffman_table {
	uint16_t bits, idx;
};

struct huffman_node {
//	uint16_t child[HUFFMAN_NR_CHILDS];
	uint32_t child;
};

struct cbg_tree {
	union {
		uint32_t weights[V1_NODES * 2];
		struct huffman_table table[1 << TABLE_BITS];
	} u;
	struct bitstrm bs;
	struct huffman_node *nodes;
	struct huffman_node offset_nodes[V1_NODES];
};

struct leb128_state {
	uint32_t val;
	uint32_t off;
};

struct decryptor {
	uint32_t key;
	uint8_t sum, xor;
};

static void decorrelate(struct wuimg *img) {
	const size_t stride = img->w * img->channels;
	uint8_t *data = img->data;
	for (size_t x = 1; x < img->w; ++x) {
		for (size_t z = 0; z < img->channels; ++z) {
			size_t off = x*img->channels + z;
			data[off] += data[off - img->channels];
		}
	}
	for (size_t y = 1; y < img->h; ++y) {
		size_t line = stride*y;
		for (size_t z = 0; z < img->channels; ++z) {
			data[line + z] += data[line + z - stride];
		}
		for (size_t x = 1; x < img->w; ++x) {
			for (size_t z = 0; z < img->channels; ++z) {
				size_t off = line + x*img->channels + z;
				unsigned avg = data[off - img->channels]
					+ data[off - stride];
				data[off] += (uint8_t)(avg >> 1);
			}
		}
	}
}

static bool decryptor_check(const struct decryptor *dec,
const struct cbg_desc *desc) {
	return !CHECK_CHECKSUM || (dec->sum == desc->sum && dec->xor == desc->xor);
}

static uint8_t decryptor_feed(struct decryptor *dec, const uint8_t byte) {
	const uint32_t mask = 0xffff;
	uint32_t a = dec->key >> 16;
	uint32_t b = 20021 * (dec->key & mask);
	a = a * 20021 + dec->key * 346;
	a = (a + (b >> 16)) & mask;

	dec->key = (a << 16) + (b & mask) + 1;
	const uint8_t v = (uint8_t)(byte - a);
	if (CHECK_CHECKSUM) {
		dec->sum += v;
		dec->xor ^= v;
	}
	return v;
}

static enum trit leb128_feed(struct leb128_state *leb, const uint8_t byte) {
	if (leb->off != 7*4) {
		leb->val |= (byte & 0x7fu) << leb->off;
		leb->off += 7;
		return !(byte & 0x80) ? trit_true : trit_false;
	}
	return trit_what;
}

static uint16_t node_get_child(const struct huffman_node *node, bool bit) {
	//return node->child[bit];
	return (uint16_t)(node->child >> (bit*16));
}
static void node_set_child(struct huffman_node *node, bool bit, unsigned idx) {
	//node->child[bit] = (uint16_t)idx;
	node->child |= idx << (16*bit);
}

static uint8_t huffman_next(struct cbg_tree *tree) {
	const unsigned mask = (1 << TABLE_BITS) - 1;
	uint32_t w = bitstrm_msb_peek_high25(&tree->bs);
	uint32_t ww = (w >> (32 - TABLE_BITS)) & mask;

	const struct huffman_table t = tree->u.table[ww];
	uint32_t bits = t.bits;
	unsigned idx = t.idx;
	while (idx >= V1_NODES) {
		const bool bit = (w >> (31 - bits)) & 1;
		idx = node_get_child(tree->nodes + idx, bit);
		++bits;
	}
	bitstrm_seek(&tree->bs, bits);
	return (uint8_t)idx;
}

static struct wu_st unpack_rle(struct wuimg *img, struct cbg_tree *tree) {
	const size_t dst_len = wuimg_size(img);
	uint8_t *dst = img->data;
	size_t d = 0;
	for (bool zeroset = false; !tree->bs.eof; zeroset = !zeroset) {
		struct leb128_state leb = {0};
		enum trit t;
		do {
			t = leb128_feed(&leb, huffman_next(tree));
		} while (t == trit_false);
		if (t != trit_true) {
			break;
		}

		const size_t count = leb.val;
		if (dst_len - d < count) {
			break;
		}

		if (zeroset) {
			memset(dst + d, 0, count);
		} else {
			for (size_t i = 0; i < count; ++i) {
				dst[d+i] = huffman_next(tree);
			}
		}
		d += count;
	}
	return wuerr_partial(d, dst_len);
}

static uint32_t get_weights(uint32_t *dst, const size_t dst_len,
const uint8_t *restrict src, const struct cbg_desc *desc) {
	uint32_t total_weight = 0;
	struct decryptor dec = {.key = desc->key};
	size_t s = 0;
	for (size_t d = 0; d < dst_len; ++d) {
		struct leb128_state leb = {0};
		enum trit t;
		do {
			if (s >= desc->weights_len) {
				return 0;
			}
			t = leb128_feed(&leb, decryptor_feed(&dec, src[s]));
			++s;
		} while (t == trit_false);
		if (t != trit_true) {
			return 0;
		}
		if (UINT32_MAX - total_weight < leb.val) {
			return 0;
		}
		dst[d] = leb.val;
		total_weight += dst[d];
	}
	return decryptor_check(&dec, desc) ? total_weight : 0;
}

static void tabulate_tree(struct cbg_tree *tree, size_t root) {
	uint8_t stack[TABLE_BITS];
	uint16_t path[TABLE_BITS];
	unsigned i = 0;
	stack[i] = 0;
	path[i] = (uint16_t)root;
	uint32_t m = 0;
	do {
		if (stack[i] < HUFFMAN_NR_CHILDS) {
			uint16_t val = node_get_child(tree->nodes + path[i],
				stack[i]);
			++stack[i];
			if (val < V1_NODES || i + 1 == TABLE_BITS) {
				unsigned nr = i + 1;
				uint32_t range = 1u << (TABLE_BITS - nr);
				for (uint32_t x = 0; x < range; ++x) {
					tree->u.table[m+x] = (struct huffman_table) {
						.bits = (uint8_t)nr,
						.idx = val,
					};
				}
				m += range;
			} else {
				++i;
				stack[i] = 0;
				path[i] = val;
			}
		} else {
			--i;
		}
	} while (m != ARRAY_LEN(tree->u.table));
}

static bool make_tree(struct cbg_tree *tree, const uint32_t total_weight,
struct mparser *mp) {
	if (total_weight) {
		struct huffman_node *base = tree->nodes;
		uint32_t *weight = tree->u.weights;
		for (size_t n = V1_NODES; n < V1_NODES*2; ++n) {
			weight[n] = 0;
			struct huffman_node *dst = base + n;
			memset(dst, 0, sizeof(*dst));
			for (size_t c = 0; c < HUFFMAN_NR_CHILDS; ++c) {
				uint32_t min_seen = UINT32_MAX;
				uint16_t val = 0;
				for (size_t i = 0; i < n; ++i) {
					if (weight[i] && weight[i] < min_seen) {
						min_seen = weight[i];
						val = (uint16_t)i;
					}
				}
				node_set_child(dst, c, val);
				weight[n] += weight[val];
				weight[val] = 0;
			}
			if (weight[n] == total_weight) {
				bitstrm_from_wuptr(&tree->bs,
					mp_remaining(mp));
				tabulate_tree(tree, n);
				return true;
			}
		}
	}
	return false;
}

struct wu_st cbg_decode(const struct cbg_desc *desc, struct wuimg *img) {
	struct mparser mp = desc->mp;
	const uint8_t *enc_weights = mp_slice(&mp, desc->weights_len);
	if (!enc_weights) {
		return WUERR_HERE(wu_unexpected_eof);
	}

	struct cbg_tree *tree = malloc(sizeof(*tree));
	if (!tree) {
		return WUERR_HERE(wu_alloc_error);
	}
	tree->nodes = tree->offset_nodes - V1_NODES;

	const uint32_t total_weight = get_weights(tree->u.weights, V1_NODES,
		enc_weights, desc);
	struct wu_st st = wuerr(wu_decoding_error, "huffman tree weight == 0");
	if (make_tree(tree, total_weight, &mp)) {
		st = unpack_rle(img, tree);
		decorrelate(img);
	}
	free(tree);
	return st;
}

struct wu_st cbg_parse(struct cbg_desc *desc, struct wuimg *img,
const struct wuptr mem) {
	/* CBG structure:
		Offset  Type    Name
		0       u16     Magic[16]
		16      u16     Width
		18      u16     Height
		20      u32     Bitdepth
		24      u8      ???[8]
		32      u32     HuffmanUnpackLen
		36      u32     DecryptKey
		40      u32     WeightsLen
		44      u8      Sum
		45      u8      Xor
		46      u16     Version
		48
	*/

	*desc = (struct cbg_desc) {
		.mp = mp_wuptr(mem),
	};
	const uint8_t magic[16] = "CompressedBG___"; // Ending nul is important
	const uint8_t *header = mp_slice(&desc->mp, 48);
	if (!header) {
		return WUERR_HERE(wu_unexpected_eof);
	} else if (memcmp(header, magic, sizeof(magic))) {
		return WUERR_HERE(wu_invalid_signature);
	}

	const uint16_t version = buf_endian16l(header + 46);
	switch (version) {
	case cbg_v1:
		;const uint32_t depth = buf_endian32l(header + 20);
		switch (depth) {
		case 8: case 24: case 32:
			img->w = buf_endian16l(header+16);
			img->h = buf_endian16l(header+18);
			img->bitdepth = 8;
			img->channels = (uint8_t)(depth/8);
			img->layout = depth > 8 ? pix_bgra : pix_gray;

			desc->key = buf_endian32l(header + 36);
			desc->weights_len = buf_endian32l(header + 40);
			desc->sum = header[44];
			desc->xor = header[45];
			desc->version = (enum cbg_version)version;
			if (desc->weights_len < 0x400) {
				return WU_OK;
			}
		}
		break;
	case cbg_v2:
		return WUERR_HERE(wu_unsupported_feature);
	}
	return WUERR_HERE(wu_invalid_header);
}
