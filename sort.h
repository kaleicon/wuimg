#ifndef WU_SORT
#define WU_SORT

struct natfrm {
	union {
		char *str;
		double num;
	} v;
	union {
		unsigned int str_len;
		int zeroes;
	} n;
	bool is_str;
	bool more;
};

struct natfrm_data {
	size_t max_alloc_seen;
	size_t buf_len;
	char *buf;
};

void natfrm_free(struct natfrm *frm);

int natcmp(const struct natfrm *restrict xx, const struct natfrm *restrict yy);

struct natfrm * natfrm_str(char *str, struct natfrm_data *data);

void natfrm_data_free(struct natfrm_data *data);

bool natfrm_data_init(struct natfrm_data *data, size_t max_str_len);

#endif /* WU_SORT */
