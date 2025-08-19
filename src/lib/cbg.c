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
 * and fuzzing is useless unless disabled. This is only for documentation. */
static const bool CHECK_CHECKSUM = false;

static const size_t V1_NODES = 0x100;

struct huffman_node {
	short int child[2];
};

struct huffman_tree {
	int root;
	struct huffman_node *nodes;
	struct bitstrm bs;
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

static int huffman_next(struct huffman_tree *tree, uint8_t *c) {
	int idx = tree->root;
	while (tree->bs.pos < tree->bs.len) {
		const bool bit = bit_get(tree->bs.buf, tree->bs.pos);
		++tree->bs.pos;
		idx = tree->nodes[idx].child[bit];
		if (idx < (int)V1_NODES) {
			*c = (uint8_t)idx;
			return true;
		}
	}
	return false;
}

static size_t unpack_rle(struct wuimg *img, struct huffman_tree *tree) {
	const size_t dst_len = wuimg_size(img);
	uint8_t *dst = img->data;
	size_t d = 0;
	for (bool zeroset = false;; zeroset = !zeroset) {
		struct leb128_state leb = {0};
		enum trit t;
		do {
			uint8_t c;
			if (!huffman_next(tree, &c)) {
				return d;
			}
			t = leb128_feed(&leb, c);
		} while (t == trit_false);
		if (t != trit_true) {
			break;
		}

		const size_t count = leb.val;
		if (count > dst_len || dst_len - count < d) {
			break;
		}

		if (zeroset) {
			memset(dst + d, 0, count);
			d += count;
		} else {
			for (size_t i = 0; i < count; ++i) {
				if (!huffman_next(tree, dst + d)) {
					return d;
				}
				++d;
			}
		}
	}
	return d;
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

static bool make_tree(struct huffman_tree *tree, uint32_t *weight,
const size_t nodes, const uint32_t total_weight, struct mparser *mp) {
	if (total_weight) {
		// Create parent nodes
		struct huffman_node *node = tree->nodes;
		for (size_t n = nodes; n < nodes*2; ++n) {
			weight[n] = 0;
			struct huffman_node *dst = node + n;
			for (size_t c = 0; c < ARRAY_LEN(dst->child); ++c) {
				uint32_t min_seen = UINT32_MAX;
				const int16_t unset = -1;
				dst->child[c] = unset;
				for (size_t i = 0; i < n; ++i) {
					if (weight[i] && weight[i] < min_seen) {
						min_seen = weight[i];
						dst->child[c] = (short int)i;
					}
				}
				if (dst->child[c] != unset) {
					weight[n] += weight[dst->child[c]];
					weight[dst->child[c]] = 0;
				}
			}
			if (weight[n] == total_weight) {
				tree->root = (int)n;
				tree->bs = bitstrm_from_wuptr(mp_remaining(mp));
				return true;
			}
		}
	}
	return false;
}

size_t cbg_decode(const struct cbg_desc *desc, struct wuimg *img) {
	if (!wuimg_alloc_noverify(img)) {
		return 0;
	}

	struct mparser mp = desc->mp;
	const uint8_t *enc_weights = mp_slice(&mp, desc->weights_len);
	if (!enc_weights) {
		return 0;
	}

	uint32_t *weights;
	struct huffman_tree tree;
	const size_t node_size = V1_NODES * sizeof(*tree.nodes);
	const size_t weight_size = V1_NODES*2 * sizeof(*weights);
	uint8_t *buf = malloc(weight_size + node_size);
	if (!buf) {
		return 0;
	}

	weights = (uint32_t *)buf;
	tree.nodes = (struct huffman_node *)(buf + weight_size) - V1_NODES;
	const uint32_t total_weight = get_weights(weights, V1_NODES,
		enc_weights, desc);
	size_t written = 0;
	if (make_tree(&tree, weights, V1_NODES, total_weight, &mp)) {
		written = unpack_rle(img, &tree);
		decorrelate(img);
	}
	free(buf);
	return written;
}

enum wu_error cbg_parse(struct cbg_desc *desc, struct wuimg *img) {
	/* CBG structure (after signature):
		Offset  Type    Name
		0       u16     Width
		2       u16     Height
		4       u32     Bitdepth
		8       u8      ???[8]
		16      u32     HuffmanUnpackLen
		20      u32     DecryptKey
		24      u32     WeightsLen
		28      u8      Sum
		29      u8      Xor
		30      u16     Version
		32
	*/

	const uint8_t *header = mp_slice(&desc->mp, 32);
	if (!header) {
		return wu_unexpected_eof;
	}

	const uint16_t version = buf_endian16(header + 30, little_endian);
	switch (version) {
	case cbg_v1:
		;const uint32_t depth = buf_endian32(header + 4, little_endian);
		switch (depth) {
		case 8: case 24: case 32:
			img->w = buf_endian16(header, little_endian);
			img->h = buf_endian16(header+2, little_endian);
			img->bitdepth = 8;
			img->channels = (uint8_t)(depth/8);
			img->layout = depth > 8 ? pix_bgra : pix_gray;

			desc->weights_len = buf_endian32(header + 24, little_endian);
			desc->key = buf_endian32(header + 20, little_endian);
			desc->sum = header[28];
			desc->xor = header[29];
			desc->version = (enum cbg_version)version;
			if (desc->weights_len < 0x400) {
				return wuimg_verify(img);
			}
		}
		break;
	case cbg_v2:
		return wu_unsupported_feature;
	}
	return wu_invalid_header;
}

enum wu_error cbg_init(struct cbg_desc *desc, const struct wuptr mem) {
	*desc = (struct cbg_desc) {
		.mp = mp_wuptr(mem),
	};
	const uint8_t magic[16] = "CompressedBG___"; // Ending nul is important
	return fmt_sigcmp_mem(magic, sizeof(magic), &desc->mp);
}
