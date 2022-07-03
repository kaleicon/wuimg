#include "common.h"
#include "raster/file.h"

size_t fread_tail(void *buf, const size_t size, const size_t nmemb,
FILE *ifp) {
	const size_t total = size*nmemb;
	return fseek(ifp, -(long)total, SEEK_END) == 0
		? fread(buf, size, nmemb, ifp) : 0;
}

long file_size(FILE *ifp) {
	fseek(ifp, 0, SEEK_END);
	return ftell(ifp);
}

size_t file_size_from(FILE *ifp, const long start) {
	return (size_t)lmax(0, file_size(ifp) - start);
}

long file_remaining(FILE *ifp) {
	const long cur = ftell(ifp);
	const long end = file_size(ifp);
	fseek(ifp, cur, SEEK_SET);
	return lmax(0, end - cur);
}
