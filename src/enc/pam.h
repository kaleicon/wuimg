// SPDX-License-Identifier: 0BSD
#ifndef ENC_PAM
#define ENC_PAM

#include "raster/wuimg.h"

size_t pam_write_row(uint8_t *restrict row, const struct wuimg *dst, FILE *ofp);

void pam_write_header(const struct wuimg *dst, FILE *ofp);

void pam_best_fit(struct wuimg *dst, const struct wuimg *src);

bool pam_can_cpy(struct wuimg *dst, const struct wuimg *src);

#endif // ENC_PAM
