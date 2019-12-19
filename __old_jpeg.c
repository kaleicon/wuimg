#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <jpeglib.h>

#include "wudefs.h"
#include "common.h"

static const char *mname[] = {
	// C0
	"SOF0 (Start Of Frame, Baseline DCT)",
	NULL,
	"SOF2 (Start Of Frame, Progressive DCT)",
	NULL,
	"DHT (Define Huffman Table)",
	NULL, NULL, NULL,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	// D0
	"RST%hhu (Restart)", NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	"SOI (Start Of Image)",
	"EOI (End Of Image)",
	"SOS (Start of Scan)",
	"DQT (Define Quantization Table)",
	NULL,
	"DRI (Define Restart Interval)",
	NULL, NULL,
	// E0
	"APP%hhu (Application-specific)", NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	// F0
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL, NULL, NULL,
	"COM (Comment)",
	NULL
};

struct marker_list find_markers(const unsigned char *restrict buf, size_t size) {
	size_t alloc = 32;
	struct marker_list soi = {
		.loc = malloc(alloc * sizeof(struct marker_loc)),
	};
	size_t i = 0;
	size_t pos = 0;
	while (pos < size) {
		size_t delta = 0;
		size_t has_payload = 1;
		const unsigned char *marker = buf + pos;
		const unsigned char type = marker[1];
		if (*marker == 0xFF && type != 0x00 && type != 0xFF) {
			int offset = type - 0xC0;
			if (type >= 0xD0 && type <= 0xD7) {
				printf(mname[0x10], type - 0xD0);
				printf(" (0xff%hhx) at %#lx\n", type, pos);
				has_payload = 0;
			} else if (type >= 0xE0 && type <= 0xEF) {
				printf(mname[0x20], type - 0xE0);
				printf(" (0xff%hhx) at %#lx\n", type, pos);
			} else {
				if (mname[offset]) {
					printf("%s (0xff%hhx) at %#lx\n",
						mname[offset], type, pos);
				} else {
					printf("Unknown marker 0xff%hhx at %#lx\n",
						type, pos);
				}
				switch (type) {
				case 0xD8: // Start Of Image
					if (i == alloc) {
						alloc += alloc / 4;
						soi.loc = realloc(soi.loc, alloc);
					}
					soi.loc[i].ptr = marker;
					++i;
					break;
				case 0xD9: // End Of Image
					soi.loc[i-1].size = marker - soi.loc[i].ptr;
					break;
				}
			}

			if (type != 0xD8 && type != 0xD9) {
				delta = (size_t) marker[2] << 8;
				delta += (size_t) marker[3] + 2;
			} else {
				delta = 2;
			}
			pos += delta;
		} else {
			++pos;
		}
	}
	printf("End of file. %#lx\n", size);
	soi.nr = nr;
	return soi;
}
