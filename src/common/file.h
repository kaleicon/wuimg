#ifndef WU_FILE
#define WU_FILE

#include <stdio.h>
#include <stdint.h>

#include "wustr.h"

uint8_t * fileccpy(struct wugrow *grow, uint8_t ch, size_t max, FILE *ifp);

size_t file_tail(void *buf, size_t size, size_t nmemb, FILE *ifp);

size_t file_remaining(FILE *ifp);


struct map_info {
	size_t len;
	const unsigned char *data;
};

int file_unmap(struct map_info *mm);

bool file_map(struct map_info *mm, FILE *ifp);

bool file_map_fd(struct map_info *mm, int fd);

#endif /* WU_FILE */
