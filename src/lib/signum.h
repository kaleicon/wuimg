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

size_t imc_decode(const struct imc_desc *desc, struct wuimg *img);

enum wu_error imc_parse(struct imc_desc *desc, struct wuimg *img);

enum wu_error imc_identify(struct imc_desc *desc, struct wuptr mem);

#endif /* LIB_IMC */
