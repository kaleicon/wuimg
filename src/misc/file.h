// SPDX-License-Identifier: 0BSD
#ifndef WU_FILE
#define WU_FILE

#include <stdio.h>

#include "wustr.h"

size_t file_tail(void *buf, size_t size, size_t nmemb, FILE *ifp);

size_t file_remaining(FILE *ifp);

FILE * file_from_stdin(void);


int file_unmap(struct wuptr *mm);

bool file_map_fd(struct wuptr *mm, int fd);

#endif /* WU_FILE */
