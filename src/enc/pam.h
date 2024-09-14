// SPDX-License-Identifier: 0BSD
#ifndef ENC_PAM
#define ENC_PAM

#include "raster/wuimg.h"

size_t pam_write_row(const struct wuimg *out, FILE *ofp);

void pam_write_header(const struct wuimg *out, FILE *ofp);

bool pam_can_cpy(struct wuimg *out, const struct wuimg *in);

#endif // ENC_PAM
