#ifndef LIB_TLG
#define LIB_TLG

#include "../common.h"
#include "../raster/lib.h"
#include "../raster/memparser.h"

enum tlg_version {
	tlg_v5 = '5',
	tlg_v6 = '6',
};

struct tlg_desc {
	struct mem_parser mp;
	struct raster_desc r;
	bool tagged_data;
	enum tlg_version version:8;
	uint32_t block_height;
};

const char * tlg_version_str(enum tlg_version ver);

size_t tlg_decode(struct tlg_desc *desc, void *restrict dst);

enum lib_fail tlg_read_header(struct tlg_desc *desc);

enum lib_fail tlg_open_mem(struct tlg_desc *desc, const struct map_info *map);

#endif /* LIB_TLG */
