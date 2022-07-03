#ifndef RASTER_ALPHA
#define RASTER_ALPHA

enum alpha_op {
	// Most formats store unassociated alpha, so the default is to multiply
	alpha_multiply = 0,
	alpha_no_multiply = 1,
	alpha_one = 1 << 1,
};

enum alpha_interpretation {
	alpha_unassociated = alpha_multiply,
	alpha_associated =   alpha_no_multiply,
	alpha_key =          alpha_multiply    | alpha_one, // 'key' as in CMYK
	alpha_ignore =       alpha_no_multiply | alpha_one,
};

#endif /* RASTER_ALPHA */
