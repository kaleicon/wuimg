#include "wudefs.h"
#include "rast_utils.h"
#include "lib/wbmp.h"

enum wu_error wbmp_dec(struct image_file *infile,
const struct wu_conf *wuconf) {
	return rast_fread_dec(infile, wuconf, wbmp_open_file);
}
