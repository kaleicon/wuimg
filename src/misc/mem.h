// SPDX-License-Identifier: 0BSD
#ifndef RASTER_MEM
#define RASTER_MEM

#include <stddef.h>
#include <stdint.h>

uint8_t memcycle(uint8_t *dst, size_t pos);

void memtessel(void *restrict dst, const void *restrict src, size_t size,
size_t bytes);

void memwordset(void *restrict dst, const void *restrict src, size_t size,
size_t nmemb);

void memrepeat(void *dst, size_t pos, size_t offset, size_t count);

void memrepeat_or_zero(void *dst, size_t pos, size_t offset, size_t count);

const void * memchk(const void *s, unsigned char c, size_t n);

void * memdup(const void *s, size_t n);

size_t memccpy_cur(unsigned char *restrict dst,
const unsigned char *restrict src, const unsigned char c, size_t dst_len,
size_t src_len);

#ifndef _GNU_SOURCE
void * memrchr(const void *s, int c, size_t n);
#endif

#endif /* RASTER_MEM */
