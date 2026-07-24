// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2022 kaleido
#include "cicp.h"

static const char BT709[] = "BT.709-6";
static const char UNSPEC[] = "Unspecified";
static const char SYSM[] = "BT.470-6 System M";
static const char SYSBG[] = "BT.470-6 System B, G";
static const char BT601[] = "BT.601-7";
static const char ST240[] = "SMPTE ST 240";
static const char BT2020[] = "BT.2020-2";
static const char ST428[] = "SMPTE ST 428-1";
static const char DEFAULT[] = "Reserved (assuming sRGB)";

const char * cicp_primaries_str(const enum cicp_primaries primaries) {
	switch (primaries) {
	case cicp_primaries_bt709_6: return BT709;
	case cicp_primaries_unspecified: return UNSPEC;
	case cicp_primaries_bt470_6_system_m: return SYSM;
	case cicp_primaries_bt470_6_system_b_g: return SYSBG;
	case cicp_primaries_bt601_7: return BT601;
	case cicp_primaries_smpte_st_240: return ST240;
	case cicp_primaries_generic_film: return "Generic film";
	case cicp_primaries_bt2020_2: return BT2020;
	case cicp_primaries_smpte_st_428_1: return ST428;
	case cicp_primaries_smpte_rp_431_2: return "SMPTE RP 431-2";
	case cicp_primaries_smpte_eg_432_1: return "SMPTE EG 432-1";
	case cicp_primaries_the_unidentified: return "The \"Unidentified\"";
	}
	return DEFAULT;
}

const char * cicp_transfer_str(const enum cicp_transfer transfer,
const enum cicp_matrix matrix) {
	switch (transfer) {
	case cicp_transfer_bt709_6: return BT709;
	case cicp_transfer_unspecified: return UNSPEC;
	case cicp_transfer_bt470_6_system_m: return SYSM;
	case cicp_transfer_bt470_6_system_b_g: return SYSBG;
	case cicp_transfer_bt601_7: return BT601;
	case cicp_transfer_smpte_st_240: return ST240;
	case cicp_transfer_linear: return "Linear";
	case cicp_transfer_log: return "Logarithmic";
	case cicp_transfer_log_sqrt: return "Logarithmic square";
	case cicp_transfer_iec_61966_2_4: return "IEC 61996-2-4";
	case cicp_transfer_bt1361_0: return "BT.1361-0";
	case cicp_transfer_iec_61966_2_1:
		switch (matrix) {
		case cicp_matrix_rgb: return "IEC 61966-2-1 sRGB";
		case cicp_matrix_bt470_6_system_b_g: return "IEC 61966-2-1 sYCC";
		default: break;
		}
		break;
	case cicp_transfer_bt2020_2_10bit: return "BT.2020-2 (10-bit system)";
	case cicp_transfer_bt2020_2_12bit: return "BT.2020-2 (12-bit system)";
	case cicp_transfer_smpte_st_2084: return "BT.2100-2 Perceptual Quantization system";
	case cicp_transfer_smpte_st_428_1: return ST428;
	case cicp_transfer_arib_std_b67: return "BT.2100-2 Hybrid Log-Gamma system";
	}
	return DEFAULT;
}

const char * cicp_matrix_str(const enum cicp_matrix matrix) {
	switch (matrix) {
	case cicp_matrix_rgb: return "Identity";
	case cicp_matrix_bt709_6: return BT709;
	case cicp_matrix_unspecified: return UNSPEC;
	case cicp_matrix_fcc_title_47: return "FCC Title 47";
	case cicp_matrix_bt470_6_system_b_g: return SYSBG;
	case cicp_matrix_bt601_7: return BT601;
	case cicp_matrix_smpte_st_240: return ST240;
	case cicp_matrix_ycgco: return "YCgCo";
	case cicp_matrix_bt2020_2_nonconstant:
		return "BT.2020-2 (Nonconstant luminance)";
	case cicp_matrix_bt2020_2_constant:
		return "BT.2020-2 (Constant luminance)";
	case cicp_matrix_smpte_st_2085:
		return "SMPTE ST 2085";
	case cicp_matrix_chroma_derived_nonconstant:
		return "Chroma derived nonconstant luminance";
	case cicp_matrix_chroma_derived_constant:
		return "Chroma derived constant luminance";
	case cicp_matrix_bt2100_2_ictcp:
		return "BT.2100-2 ICtCp";
	}
	return DEFAULT;
}
