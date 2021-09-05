#ifndef WU_SORT
#define WU_SORT

struct natfrm_item {
	int zeroes;
	int len;
	void *str;
};

struct natfrm {
	void *buf;
	int len;
	bool str_first;
	struct natfrm_item nat[];
};

struct natfrm_data {
	size_t alloc;
	size_t pool_limit;
	size_t pool_size;
	size_t pool_pos;
	unsigned char *pool;
};

void natfrm_free(struct natfrm *frm);

int natcmp(const struct natfrm *restrict xx, const struct natfrm *restrict yy);

struct natfrm * natfrm_str(char *str, struct natfrm_data *data);

void natfrm_data_init(struct natfrm_data *data, size_t max_str_len);

#endif /* WU_SORT */
