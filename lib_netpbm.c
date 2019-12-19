#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <limits.h>
#include <inttypes.h>
#include <stdbool.h>

#include "lib_netpbm.h"

static unsigned char * raw_ppm_decode(const struct pnm_desc *desc) {
	const size_t dims = desc->h * desc->stride;
	unsigned char *output = malloc(dims);
	size_t cnt = 0;
	while (cnt < dims) {
		const size_t read = fread(desc->buf, 1, desc->stride, desc->ifp);
		if (read < desc->stride) {
			free(output);
			return NULL;
		}

		size_t i = 0;
		while (i < desc->stride) {
			uint_least32_t val;
			if (desc->depth == 2) {
				val = (uint_least32_t)
					(desc->buf[i] << 8) | desc->buf[i + 1];
			} else {
				val = desc->buf[i];
			}

			if (val > desc->maxval) {
				free(output);
				return NULL;
			}

			val *= desc->scale;
			if (desc->depth == 2) {
				output[cnt] = (unsigned char)(val >> 24);
				output[cnt + 1] = (unsigned char)(val >> 16);
			} else {
				output[cnt] = (unsigned char)(val >> 16);
			}

			i += desc->depth;
			cnt += desc->depth;
		}
	}
	return output;
}

static unsigned char * plain_ppm_decode(const struct pnm_desc *desc) {
	const size_t dims = desc->w * desc->h * desc->ch * desc->depth;
	unsigned char *output = malloc(dims);
	for (size_t cnt = 0; cnt < dims; cnt += desc->depth) {
		uint_least32_t val;
		const int read = fscanf(desc->ifp, " %" PRIuLEAST32, &val);
		if (read < 1 || val > desc->maxval) {
			free(output);
			return NULL;
		}

		val *= desc->scale;
		if (desc->depth == 2) {
			output[cnt] = (unsigned char)(val >> 24);
			output[cnt + 1] = (unsigned char)(val >> 16);
		} else {
			output[cnt] = (unsigned char)(val >> 16);
		}
	}
	return output;
}

static void expand_mono(unsigned char byte, unsigned char *out, const int nr) {
	for (int i = 0; i < nr; ++i) {
		out[i] = byte & (0x80 >> i) ? 0x00 : 0xff;
	}
}

static unsigned char * raw_pbm_decode(struct pnm_desc *desc) {
	const size_t dims = desc->w * desc->h;
	const size_t remainer = desc->w % 8;
	unsigned char *output = malloc(dims);
	size_t cnt = 0;
	while (cnt < dims) {
		const size_t read = fread(desc->buf, 1, desc->stride, desc->ifp);
		if (read < desc->stride) {
			free(output);
			return NULL;
		}

		size_t i = 0;
		while (i < desc->w / 8) {
			expand_mono(desc->buf[i], output + cnt, 8);
			cnt += 8;
			++i;
		}
		expand_mono(desc->buf[i], output + cnt, (int)remainer);
		cnt += remainer;
	}
	return output;
}

static unsigned char * plain_pbm_decode(struct pnm_desc *desc) {
	const size_t dims = desc->w * desc->h;
	unsigned char *output = malloc(dims);
	size_t cnt = 0;
	while (cnt < dims) {
		const size_t read = fread(desc->buf, 1, desc->stride, desc->ifp);
		if (read == 0) {
			free(output);
			return NULL;
		}

		for (size_t i = 0; i < read && cnt < dims; ++i) {
			switch (desc->buf[i]) {
			case '\t': case '\n': case '\v': case '\f': case '\r':
			case ' ':
				continue;
			case '0':
				output[cnt] = 0xff;
				++cnt;
				continue;
			case '1':
				output[cnt] = 0x00;
				++cnt;
				continue;
			default:
				free(output);
				return NULL;
			}
		}
	}
	return output;
}

void close_pnm_file(const struct pnm_desc *desc) {
	free(desc->buf);
	fclose(desc->ifp);
}

unsigned char * decode_pnm_next(struct pnm_desc *desc) {
	switch (desc->type) {
	case plain_pbm:
		return plain_pbm_decode(desc);
	case raw_pbm:
		return raw_pbm_decode(desc);
	case plain_pgm:
	case plain_ppm:
		return plain_ppm_decode(desc);
	case raw_pgm:
	case raw_ppm:
		return raw_ppm_decode(desc);
	default:
		return NULL;
	}
}

unsigned char ** decode_pnm_file(struct pnm_desc *desc, size_t *nr) {
	const size_t size = sizeof(unsigned char *);
	unsigned char **hold;

	if (desc->type >= 4) {
		size_t alloc = 4;
		hold = malloc(alloc * size);
		*nr = 0;
		while ((hold[*nr] = decode_pnm_next(desc)) != NULL) {
			++*nr;
			if (*nr == alloc) {
				alloc += alloc / 2;
				unsigned char **tmp = realloc(hold,
					alloc * size);
				if (tmp) {
					hold = tmp;
				} else {
					free(hold);
					return NULL;
				}
			}
		}
	} else {
		hold = malloc(size);
		hold[0] = decode_pnm_next(desc);
		*nr = 1;
	}
	return hold;
}

static bool setup_pnm_desc(struct pnm_desc *desc) {
	if (desc->type != plain_pbm && desc->type != raw_pbm) {
		if (desc->maxval < 1 || desc->maxval > 65535) {
			return false;
		}

		uint_least32_t range;
		if (desc->maxval > UCHAR_MAX) {
			range = USHRT_MAX;
			desc->depth = 2;
		} else {
			range = UCHAR_MAX;
			desc->depth = 1;
		}
		desc->scale = (range << 16) / desc->maxval + 1;
		if (desc->type == raw_pgm || desc->type == raw_ppm) {
			desc->stride = desc->w * desc->ch * desc->depth;
			desc->buf = malloc(desc->stride);
		}
	} else {
		desc->depth = 1;
		if (desc->type == plain_pbm) {
			desc->stride = BUFSIZ;
			desc->buf = malloc(BUFSIZ);
		} else {
			const size_t remainer = desc->w % 8;
			desc->stride = desc->w / 8 + (remainer != 0);
			desc->buf = malloc(desc->stride);
		}
	}

	if (!desc->buf) {
		return false;
	}
	return true;
}

bool parse_pnm_header(struct pnm_desc *desc) {
	int c;
	int seen = 0;
	bool comment = 0;
	while ((c = getc(desc->ifp)) != EOF) {
		if (c == '#') {
			comment = true;
		} else if (comment) {
			if (c == '\n' || c == '\r') {
				comment = false;
			}
		} else if (isdigit(c)) {
			ungetc(c, desc->ifp);
			int result;
			if (seen == 0) {
				result = fscanf(desc->ifp, "%zu", &desc->w);
			} else if (seen == 1) {
				result = fscanf(desc->ifp, "%zu", &desc->h);
			} else {
				result = fscanf(desc->ifp, "%u", &desc->maxval);
			}

			if (result < 1) {
				fclose(desc->ifp);
				return false;
			}

			if (seen == 1) {
				if (desc->type == plain_pbm
				|| desc->type == raw_pbm) {
					break;
				}
			} else if (seen == 2) {
				break;
			}
			++seen;
		} else if (isspace(c)) {
			continue;
		} else {
			fclose(desc->ifp);
			return false;
		}
	}

	while ((c = getc(desc->ifp)) != EOF) {
		if (c == '\n') {
			return setup_pnm_desc(desc);
		}
	}
	fclose(desc->ifp);
	return false;
}

enum pnm_format open_pnm_file(const char *filename, struct pnm_desc *desc) {
	desc->ifp = fopen(filename, "rb");
	if (!desc->ifp) {
		return 0;
	}

	char magic[3] = {0, 0, 0};
	if (fscanf(desc->ifp, "%2s", magic) > 0 && magic[0] == 'P') {
		desc->type = magic[1] - 48;
		switch (desc->type) {
		case plain_pbm:
		case plain_pgm:
		case raw_pbm:
		case raw_pgm:
			desc->ch = 1;
			return desc->type;
		case plain_ppm:
		case raw_ppm:
			desc->ch = 3;
			return desc->type;
		case pam:
			return 0;
		}
	}
	fclose(desc->ifp);
	return 0;
}
