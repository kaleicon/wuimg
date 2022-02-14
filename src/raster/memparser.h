#ifndef COMMON_MEMPARSER
#define COMMON_MEMPARSER

#include <inttypes.h>

#include "../wustr.h"

#define mem_scan_xint_MACRO(mp, ptr) mem_scan_xint( mp , sizeof(*( ptr )) , ptr )
#define mem_scan_uint_MACRO(mp, ptr) mem_scan_uint( mp , sizeof(*( ptr )) , ptr )

typedef int_fast64_t mem_fast_t;

struct mem_parser {
	size_t pos;
	size_t len;
	const unsigned char *mem;
};

unsigned char mem_next_char_unsafe(struct mem_parser *mp);

size_t mem_get_uint_unsafe(struct mem_parser *mp, size_t digits,
mem_fast_t *val);

void mem_skip_blank(struct mem_parser *mp);

void mem_skip_space(struct mem_parser *mp);

void mem_skip_nonspace(struct mem_parser *mp);

void mem_skip_line(struct mem_parser *mp);

int mem_next_char(struct mem_parser *mp);

int mem_next_nonblank(struct mem_parser *mp);

int mem_next_nonspace(struct mem_parser *mp);

struct wuptr mem_next_remaining(struct mem_parser *mp, size_t len);

const uint8_t * mem_next_slice(struct mem_parser *mp, size_t len);

struct wuptr mem_get_word(struct mem_parser *mp);

size_t mem_get_uint(struct mem_parser *mp, size_t digits, mem_fast_t *val);

bool mem_scan_xint(struct mem_parser *mp, size_t size, void *restrict val);

bool mem_scan_uint(struct mem_parser *mp, size_t size, void *restrict val);

struct mem_parser mem_parser_mem(size_t size, const void *restrict data);

#endif /* COMMON_MEMPARSER */
