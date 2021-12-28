#include "../wudefs.h"
#include "../rast_utils.h"
#include "../lib/avs.h"

enum wu_error avs_dec(struct image_file *infile, const struct wu_conf *wuconf) {
	return rast_trivial_dec(infile, wuconf, avs_open_file);
}
