#ifndef RAST_WUTILS
#define RAST_WUTILS

#include "wudefs.h"
#include "common.h"
#include "raster/memparser.h"

typedef void (*rast_vfree_t)(void *desc);
typedef size_t (*rast_vdec_t)(const void *restrict desc, struct raw_img *img);
typedef void (*rast_vmeta_t)(const void *restrict desc, struct wu_tree *metadata);
typedef enum wu_error (*rast_vparse_t)(void *restrict desc, struct raw_img *img);
typedef enum wu_error (*rast_vmopen_t)(void *restrict desc, struct mp_parser mp);
typedef enum wu_error (*rast_vopen_t)(void *restrict desc, FILE *ifp);


typedef enum wu_error (*rast_map_t)(struct image_file *infile,
const struct wu_conf *wuconf, const struct map_info *mm);

typedef enum wu_error (*rast_open_t)(struct raw_img *img, FILE *ifp);


enum wu_error rast_trivial_dec(struct image_file *infile,
const struct wu_conf *wuconf, void *desc, rast_vopen_t open,
rast_vparse_t parse, rast_vmeta_t meta, rast_vdec_t dec, rast_vfree_t cleanup);

enum wu_error rast_trivial_map(struct image_file *infile,
const struct wu_conf *wuconf, void *desc, rast_vmopen_t mopen,
rast_vparse_t parse, rast_vmeta_t meta, rast_vdec_t dec, rast_vfree_t cleanup);


enum wu_error rast_map_wrap(struct image_file *infile,
const struct wu_conf *wuconf, rast_map_t wrap_fn);

enum wu_error rast_fread_dec(struct image_file *infile,
const struct wu_conf *wuconf, rast_open_t open_fn);

#endif /* RAST_WUTILS */
