#ifndef WU_UTF8
#define WU_UTF8

#include "common/common.h"
#include "common/wustr.h"

enum trit utf8_convert(const char *restrict data, size_t len,
struct wustr *out, const char *restrict enc);

#endif /* WU_UTF8 */
