// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2022 kaleido
#include "misc/endian.h"

const char * endian_str(const enum endianness e) {
	switch (e) {
	case big_endian: return "Big endian";
	case little_endian: return "Little endian";
	}
	return "???";
}

void endian_loop16(uint16_t *data, const enum endianness e, const size_t n) {
	if (e != which_end()) {
		for (size_t i = 0; i < n; ++i) {
			data[i] = swap16(data[i]);
		}
	}
}
void endian_loop24(uint8_t *data, const enum endianness e, const size_t n) {
	if (e != which_end()) {
		for (size_t i = 0; i < n; ++i) {
			const uint8_t tmp = data[i*3];
			data[i*3] = data[i*3+2];
			data[i*3+2] = tmp;
		}
	}
}
void endian_loop32(uint32_t *data, const enum endianness e, const size_t n) {
	if (e != which_end()) {
		for (size_t i = 0; i < n; ++i) {
			data[i] = swap32(data[i]);
		}
	}
}
void endian_loop64(uint64_t *data, const enum endianness e, const size_t n) {
	if (e != which_end()) {
		for (size_t i = 0; i < n; ++i) {
			data[i] = swap64(data[i]);
		}
	}
}
