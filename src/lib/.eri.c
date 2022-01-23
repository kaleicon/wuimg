enum lib_fail eri_read_header(struct eri_desc *desc) {
}

enum lib_fail eri_open_file(struct eri_desc *desc, FILE *ifp) {
	const unsigned char magic[] =
		"Entis\x1a\00\00"
		"\x00\x01\x00\x03\x00\x00\x00\x00"
		"Entis Rasterized Image";
	const enum lib_fail st = lib_sigcmp(magic, sizeof(magic) - 1, ifp);
	if (st == lib_ok) {
		desc->ifp = ifp;
	}
	return st;
}
