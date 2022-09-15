#include "rast_utils.h"
#include "lib/farbfeld.h"

enum wu_error farbfeld_dec(struct image_file *infile,
const struct wu_conf *wuconf) {
	return rast_fread_dec(infile, wuconf, farbfeld_open_file);
}
