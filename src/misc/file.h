// SPDX-License-Identifier: 0BSD
#ifndef WU_FILE
#define WU_FILE

#include <stdio.h>

#include "wustr.h"

bool file_read_pi_comm(struct wustr *comm, FILE *ifp);

size_t file_tail(void *buf, size_t size, size_t nmemb, FILE *ifp);

size_t file_remaining(FILE *ifp);

FILE * file_from_stdin(void);


struct map_info {
	size_t len;
	const unsigned char *data;
};

int file_unmap(struct map_info *mm);

bool file_map_fd(struct map_info *mm, int fd);

#endif /* WU_FILE */
