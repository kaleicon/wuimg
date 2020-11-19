enum lib_fail pic_read_header(struct pic_desc *desc) {
	/* Pictor file header (after id):
		Offset  Size    Name
		0       WORD    Width;
		2       WORD    Height;
		4       WORD    XOffset;     // X of lower left corner of image
		6       WORD    YOffset;     // Y of lower left corner of image
		8       BYTE    PlaneInfo;   // BPP and number color planes
		9       BYTE    PaletteFlag; // Color palette/video flag
		10      BYTE    VideoMode;   // Video mode of image
		11      WORD    PaletteType; // Type of color palette
		13      WORD    PaletteSize; // Size of color palette
		15
	*/

	uint8_t buf[15];
	if (fread(buf, 1, sizeof(buf), desc->ifp) != sizeof(buf)) {
		return lib_unexpected_eof;
	}

	desc->w = buf_endian16(buf, little_endian);
	desc->h = buf_endian16(buf + 2, little_endian);
	desc->x = buf_endian16(buf + 4, little_endian);
	desc->y = buf_endian16(buf + 6, little_endian);

	if (buf[9] == 0xff) {
		desc->video_mode = buf[10];
		
	}
	return lib_ok;
}

enum lib_fail pic_open_file(FILE *ifp, struct pic_desc *desc) {
	uint16_t id;
	if (fread(&id, 1, sizeof(id), ifp) == sizeof(id)) {
		if (endian16(id, little_endian) == 0x1234) {
			desc->ifp = ifp;
			return lib_ok;
		}
		return lib_unknown_format;
	}
	return lib_unexpected_eof;
}
