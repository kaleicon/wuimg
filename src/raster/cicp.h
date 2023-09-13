// SPDX-License-Identifier: 0BSD
#ifndef RASTER_CICP
#define RASTER_CICP

/* CICP enums
 * https://www.itu.int/rec/T-REC-H.273/en */
enum cicp_primaries {
	cicp_primaries_bt709_6 = 1,
	cicp_primaries_unspecified = 2,
	cicp_primaries_bt470_6_system_m = 4,
	cicp_primaries_bt470_6_system_b_g = 5,
	cicp_primaries_bt601_7 = 6,
	cicp_primaries_smpte_st_240 = 7,
	cicp_primaries_generic_film = 8,
	cicp_primaries_bt2020_2 = 9,
	cicp_primaries_smpte_st_428_1 = 10,
	cicp_primaries_smpte_rp_431_2 = 11,
	cicp_primaries_smpte_eg_432_1 = 12,
	cicp_primaries_the_unidentified = 22,
};

enum cicp_transfer {
	cicp_transfer_bt709_6 = 1,
	cicp_transfer_unspecified = 2,
	cicp_transfer_bt470_6_system_m = 4,
	cicp_transfer_bt470_6_system_b_g = 5,
	cicp_transfer_bt601_7 = 6,
	cicp_transfer_smpte_st_240 = 7,
	cicp_transfer_linear = 8,
	cicp_transfer_log = 9,
	cicp_transfer_log_sqrt = 10,
	cicp_transfer_iec_61966_2_4 = 11,
	cicp_transfer_bt1361_0 = 12,
	cicp_transfer_iec_61966_2_1 = 13,
	cicp_transfer_bt2020_2_10bit = 14,
	cicp_transfer_bt2020_2_12bit = 15,
	cicp_transfer_smpte_st_2084 = 16,
	cicp_transfer_smpte_st_428_1 = 17,
	cicp_transfer_arib_std_b67 = 18,
};

enum cicp_matrix {
	cicp_matrix_rgb = 0,
	cicp_matrix_bt709_6 = 1,
	cicp_matrix_unspecified = 2,
	cicp_matrix_fcc_title_47 = 4,
	cicp_matrix_bt470_6_system_b_g = 5,
	cicp_matrix_bt601_7 = 6,
	cicp_matrix_smpte_st_240 = 7,
	cicp_matrix_ycgco = 8,
	cicp_matrix_bt2020_2_nonconstant = 9,
	cicp_matrix_bt2020_2_constant = 10,
	cicp_matrix_smpte_st_2085 = 11,
	cicp_matrix_chroma_derived_nonconstant = 12,
	cicp_matrix_chroma_derived_constant = 13,
	cicp_matrix_bt2100_2_ictcp = 14,
};

const char * cicp_primaries_str(enum cicp_primaries primaries);

const char * cicp_transfer_str(enum cicp_transfer transfer,
enum cicp_matrix matrix);

const char * cicp_matrix_str(enum cicp_matrix matrix);

#endif /* RASTER_CICP */
