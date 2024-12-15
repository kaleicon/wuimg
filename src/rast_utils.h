// SPDX-License-Identifier: 0BSD
#ifndef RAST_WUTILS
#define RAST_WUTILS

#include "wudefs.h"
#include "misc/file.h"

typedef size_t (*rast_vdec_t)(const void *restrict desc, struct wuimg *img);
typedef void (*rast_vmeta_t)(const void *restrict desc, struct wu_tree *metadata);
typedef enum wu_error (*rast_vparse_t)(void *restrict desc, struct wuimg *img);
typedef enum wu_error (*rast_vopen_t)(void *restrict desc, struct image_file *infile);

typedef enum wu_error (*rast_open_t)(struct wuimg *img, FILE *ifp);


enum wu_error rast_trivial_dec(struct image_file *infile,
const struct wu_conf *wuconf, void *desc, rast_vopen_t open,
rast_vparse_t parse, rast_vmeta_t meta, rast_vdec_t dec);

enum wu_error rast_trivial_fread(struct image_file *infile,
const struct wu_conf *wuconf, rast_open_t open_fn);

#endif /* RAST_WUTILS */
