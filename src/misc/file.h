// SPDX-License-Identifier: 0BSD
#ifndef WU_FILE
#define WU_FILE

#include <stdio.h>
#include <stdint.h>

#include "misc/endian.h"
#include "wustr.h"

size_t file_endian_read(void *restrict dst, size_t size, FILE *ifp,
size_t word_depth, enum endianness e);

bool file_read_pi_comm(struct wustr *comm, FILE *ifp);

size_t file_tail(void *buf, size_t size, size_t nmemb, FILE *ifp);

size_t file_remaining(FILE *ifp);


struct map_info {
	size_t len;
	const unsigned char *data;
};

int file_unmap(struct map_info *mm);

bool file_map_fd(struct map_info *mm, int fd);

#endif /* WU_FILE */
