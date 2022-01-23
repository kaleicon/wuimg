#include <stdio.h>
#include <time.h>

#include "common.h"

int main() {
	const clock_t start = clock();
	FILE *devnull = fopen("/dev/null", "rb");
	if (devnull) {
		time_t t = 0;
		while (t < 25000000) {
			rfc3339_format(t, devnull);
			++t;
		}
		rfc3339_format(t, stdout);
		printf("\ntotal: %g\n", clock_ellapsed(start));
		return 0;
	}
	return 1;
}
