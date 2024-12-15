// SPDX-License-Identifier: 0BSD
#include <unistd.h>
#include <sys/mman.h>

#include "misc/file.h"
#include "misc/math.h"

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

FILE * file_from_stdin(void) {
	FILE *tmp = tmpfile();
	if (tmp) {
		unsigned char buf[BUFSIZ];
		size_t read;
		while ( (read = fread(buf, 1, sizeof(buf), stdin)) ) {
			fwrite(buf, 1, read, tmp);
		}
		rewind(tmp);
	}
	return tmp;
}


int file_unmap(struct wuptr *mm) {
	return munmap((void *)mm->ptr, mm->len);
}

bool file_map_fd(struct wuptr *mm, const int fd) {
	const off_t end = lseek(fd, 0, SEEK_END);
	if (end >= 0) {
		const size_t len = (size_t)end;
		void *ptr = mmap(NULL, len, PROT_READ, MAP_SHARED, fd, 0);
		*mm = (struct wuptr) {
			.len = len,
			.ptr = ptr,
		};
		return ptr != MAP_FAILED;
	}
	return false;
}
