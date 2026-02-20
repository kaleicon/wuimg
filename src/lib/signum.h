// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2024 kaleido
#ifndef LIB_IMC
#define LIB_IMC

#include "misc/mparser.h"
#include "raster/wuimg.h"

/* Signum IMC */
struct imc_desc {
	struct mparser mp;
	uint16_t htiles, vtiles;
	uint32_t bitlen;
	uint8_t xor[2];
};

struct wu_st imc_decode(const struct imc_desc *desc, struct wuimg *img);

struct wu_st imc_parse(struct imc_desc *desc, struct wuimg *img,
struct wuptr mem);

#endif /* LIB_IMC */
