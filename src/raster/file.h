#ifndef WU_FILE
#define WU_FILE

#include <stdio.h>
#include <stdint.h>

#include "wustr.h"

uint8_t * fileccpy(struct wugrow *grow, uint8_t ch, size_t max, FILE *ifp);

size_t fread_tail(void *buf, size_t size, size_t nmemb, FILE *ifp);

long file_size(FILE *ifp);

size_t file_size_from(FILE *ifp, long start);

long file_remaining(FILE *ifp);

#endif /* WU_FILE */
