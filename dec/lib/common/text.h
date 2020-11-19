#ifndef COMMON_TEXT
#define COMMON_TEXT

struct text_block {
	size_t tail;
	char buf[BUFSIZ];
};

size_t read_delim_text(struct text_block *text, const char delim, FILE *ifp);

size_t read_spaced_text(struct text_block *text, FILE *ifp);

struct text_block * new_text_block(void);

int tonum(const int digit);

#endif /* COMMON_TEXT */
