#include <stdlib.h>

#include <unistd.h>
#include <sys/mman.h>

#include "common/file.h"
#include "common/math.h"

uint8_t * fileccpy(struct wugrow *grow, const uint8_t ch, const size_t max,
FILE *ifp) {
	uint8_t *buf = NULL;
	*grow = wugrow_init(sizeof(*buf));
	while (grow->pos < max) {
		const int c = getc(ifp);
		if (c == EOF) {
			break;
		} else if (c == ch) {
			return buf;
		}
		if (!wugrow_recheck(&buf, grow)) {
			break;
		}
		buf[grow->pos] = (uint8_t)c;
		++grow->pos;
	}
	free(buf);
	return NULL;
}

size_t file_tail(void *buf, const size_t size, const size_t nmemb,
FILE *ifp) {
	const size_t total = size*nmemb;
	return fseek(ifp, -(long)total, SEEK_END) == 0
		? fread(buf, size, nmemb, ifp) : 0;
}

size_t file_remaining(FILE *ifp) {
	const long cur = ftell(ifp);
	fseek(ifp, 0, SEEK_END);
	const long end = ftell(ifp);
	fseek(ifp, cur, SEEK_SET);
	return (size_t)lmax(0, end - cur);
}


int file_unmap(struct map_info *mm) {
	return munmap((void *)mm->data, mm->len);
}

static bool map_common(struct map_info *mm, const int fd, const off_t end) {
	const size_t len = (size_t)end;
	void *data = mmap(NULL, len, PROT_READ, MAP_PRIVATE, fd, 0);
	*mm = (struct map_info) {
		.len = len,
		.data = data,
	};
	return data != MAP_FAILED;
}

bool file_map(struct map_info *mm, FILE *ifp) {
	fseek(ifp, 0, SEEK_END);
	return map_common(mm, fileno(ifp), ftello(ifp));
}

bool file_map_fd(struct map_info *mm, const int fd) {
	return map_common(mm, fd, lseek(fd, 0, SEEK_END));
}
