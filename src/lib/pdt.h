#ifndef LIB_PDT
#define LIB_PDT

#include "wudefs.h"
#include "raster/memparser.h"

enum pdt_version {
	pdt10 = '0',
	pdt11 = '1',
};

struct pdt_desc {
	struct mp_parser mp;
	enum pdt_version version;
	uint32_t mask_offset;
	const uint8_t *pal;
};

size_t pdt_decode(const struct pdt_desc *desc, struct raw_img *img);

enum wu_error pdt_parse_header(struct pdt_desc *desc, struct raw_img *img);

enum wu_error pdt_open_mem(struct pdt_desc *desc, const struct map_info *mm);

#endif /* LIB_PDT */
