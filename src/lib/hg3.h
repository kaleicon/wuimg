#ifndef LIB_HG3
#define LIB_HG3

#include "wudefs.h"
#include "raster/memparser.h"

struct hg3_desc {
	struct mp_parser mp;
	struct mp_parser image;
	int32_t x, y;
	uint32_t canvas_w, canvas_h;
};

size_t hg3_decode(const struct hg3_desc *desc, struct raw_img *img);

enum wu_error hg3_parse_image(struct hg3_desc *desc, struct raw_img *img);

enum wu_error hg3_next_image(struct hg3_desc *desc);

enum wu_error hg3_open(struct hg3_desc *desc, struct mp_parser mp);

#endif /* LIB_HG3 */
