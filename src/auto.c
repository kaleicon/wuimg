// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2024 kaleido
#include <ctype.h>

#include "misc/common.h"
#include "misc/math.h"
#include "misc/mparser.h"
#include "misc/term.h"
#include "raster/fmt.h"
#include "auto.h"

#define DESC(arg) {.ptr = (const uint8_t *)(arg), .len = sizeof(arg) - 1}

// AIPD NI
const struct wuptr aipd_desc = DESC(
	"endian:big\n"
	"bitdepth:8\n"

	"match:[AIPD]\n"
	"skip:4\n"
	"match:<u16>(\n"
		"8 c:1 match:[\0\x01]\n"
		"32 c:4 alpha:ignore skip:2\n"
	")\n"
	"match:[\0\x02\x0\0]\n"
	"w:<u16>\n"
	"h:<u16>\n"
	"skip:0x1ec"
);

/* AVS
 * Defined in Appendix E-3 of the AVS user guide:
http://bitsavers.informatik.uni-stuttgart.de/pdf/stardent/002424-001_Rev_A_Application_Visualization_System_Users_Guide_1989.pdf
 * Data is in RGB format plus an auxiliary channel with no set interpretation.
*/
const struct wuptr avs_desc = DESC(
	"endian:big\n"
	"channels:4\n"
	"bitdepth:8\n"
	"layout:argb\n"
	"alpha:ignore\n"

	"w:<u32>\n"
	"h:<u32>"
);

// Bob Raytracer Raster
const struct wuptr bob_desc = DESC(
	"endian:little\n"
	"bitdepth:8\n"

	"w:<u16>\n"
	"h:<u16>\n"
	"pal:<u24>[]"
);

// BRU - Degas Elite Brush
const struct wuptr bru_desc = DESC(
	"w:8\n"
	"h:8\n"
	"channels:1\n"
	"bitdepth:8\n"
	"bitrange:1\n"
	"attr:inverted"
);

// FARBFELD
const struct wuptr farbfeld_desc = DESC(
	"endian:big\n"
	"channels:4\n"
	"bitdepth:16\n"

	"match:[farbfeld]\n"
	"w:<u32>\n"
	"h:<u32>"
);

// GEM View-Dither
const struct wuptr gemview_desc = DESC(
	"endian:big\n"
	"channels:1\n"
	"bitdepth:8\n"

	"match:[B&W256]\n"
	"w:<u16>\n"
	"h:<u16>"
);

/* HIR - Print-Technik Raw
https://www.atari-wiki.com/index.php?title=Print-Technik_Raw_Data_file_format
*/
const struct wuptr hir_desc = DESC(
	"endian:big\n"
	"channels:1\n"
	"bitdepth:8\n"
	"bitrange:7\n"

	"match:[\x0f\x0f\x00\x01]\n"
	"w:<u16>\n"
	"h:<u16>\n"
	"match:[\x00\x01]"
);

// HP Palmtop Icon
const struct wuptr hpicon_desc = DESC(
	"endian:little\n"
	"channels:1\n"
	"bitdepth:1\n"
	"attr:inverted\n"

	"match:[\x01\x00\x01\x00]\n"
	"w:<u16>\n"
	"h:<u16>"
);

/* InShape IIM
https://temlib.org/AtariForumWiki/index.php/InShape_file_format

 * According to their website, 32-bit files use the extra channel as alpha,
 * and in the only such sample I've tested with it's unassociated.
http://inshape.lutece.net/boutique.html
*/
const struct wuptr iim_desc = DESC(
	"endian:big\n"
	"channels:1\n"
	"bitdepth:8\n"

	"match:[IS_IMAGE]\n"
	"(match:<u16>"
		"0 attr:inverted bitdepth:1\n"
		"1 attr:inverted\n"
		"4 channels:3\n"
		"5 channels:4 layout:argb\n"
	")\n"
	"skip:2\n" // "bit planes", sometimes. not neccesary for decoding
	"w:<u16>\n"
	"h:<u16>"
);

// IBM KIPS
const struct wuptr kips_desc = DESC(
	"endian:little\n"
	"channels:1\n"
	"bitdepth:8\n"

	"match:[DFIMAG00]\n"
	"h:<u16>\n"
	"w:<u16>\n"
	"match:[\x01\0\0\0]\n"
	"skip:0x10"
);

// KRO - Kolor Raw
const struct wuptr kro_desc = DESC(
	"endian:big\n"

	"match:[KRO\x01]\n"
	"w:<u32>\n"
	"h:<u32>\n"
	"(bitdepth:<u32> 8 16 32)\n"
	"(channels:<u32> 3 4)"
);

// Nokia Logo Manager
// TODO: Report logo type, multiple images
const struct wuptr nlm_desc = DESC(
	"channels:1\n"
	"bitdepth:1\n"
	"attr:inverted\n"

	"match:[NLM \x01]\n"
	"skip:1\n" // 0: Operator, 1: Caller, 2: Startup, 3: Picture image
	"match:[\0]\n" // Number of images - 1
	"w:<u8>\n"
	"h:<u8>\n"
	"match:[\x01]" // ???
);

/* OTA - Over The Air bitmap
https://web.archive.org/web/20120624211208/http://www.csoft.co.uk/documents/sms3_0_0.pdf
 * Unlike every monochrome format ever, there are no padding bits between rows.
 * That means we only properly support widths that are a multiple of 8. OTOH,
 * ImageMagick includes padding bits anyway, and two wrongs make a right so
 * we're good. FIXME someday anyway. */
const struct wuptr ota_desc = DESC(
	"channels:1\n"
	"bitdepth:1\n"
	"attr:inverted\n"

	"(match:<u8>\n"
		"0 w:<u8> h:<u8>\n"
		"0x10 endian:big w:<u16> h:<u16>\n"
	")\n"
	"(bitdepth:<u8> 1)"
);

/* Eclipse Proxy (pxy)
 * Related to Eclipse TILE (see lib/eclipse.c), but raster is not tiled,
 * colorspace is always RGB, and there's no metadata, hence it being here. */
const struct wuptr pxy_desc = DESC(
	/*
		Offset  Type    Name
		0       u16     ID         // 0xaf 0xcb
		2       u32     RasterSize
		6       u32     Width
		10      u32     Height
		14      u32     OrigWidth  // of the original TILE file
		18      u32     OrigHeight
		22      u8      ???[16]    // matches mystery data in TILE file
		38
	 * Header is then filled with zeros up to 0x100.
	*/
	"endian:big\n"
	"channels:4\n"
	"bitdepth:8\n"
	"layout:abgr\n"
	"alpha:ignore\n"
	"mirror:1\n"

	"match:[\xaf\xcb]\n"
	"skip:4\n"
	"w:<u32>\n"
	"h:<u32>\n"
	"skip:0xf2" // to 0x100
);

/* Atari Falcon True Color family */
// COKE
const struct wuptr coke_desc = DESC(
	"endian:big\n"
	"bitdepth:16\n"
	"layout:bgra\n"
	"bitfield:0x565\n"

	"match:[COKE format.]\n"
	"w:<u16>\n"
	"h:<u16>\n"
	"match:[\x00\x12]"
);

// EggPaint
const struct wuptr eggpaint_desc = DESC(
	"endian:big\n"
	"bitdepth:16\n"
	"layout:bgra\n"
	"bitfield:0x565\n"

	"match:[TRUP]\n"
	"w:<u16>\n"
	"h:<u16>"
);

// FTC (Falcon True Color)
const struct wuptr ftc_desc = DESC(
	"w:384\n"
	"h:240\n"
	"bitdepth:16\n"
	"layout:bgra\n"
	"bitfield:0x565"
);

// GodPaint
const struct wuptr god_desc = DESC(
	"endian:big\n"
	"bitdepth:16\n"
	"layout:bgra\n"
	"bitfield:0x565\n"

	"skip:2\n" // Technically format ID, but files have inconsistent values
	"w:<u16>\n"
	"h:<u16>"
);

// IndyPaint
const struct wuptr indy_desc = DESC(
	"endian:big\n"
	"bitdepth:16\n"
	"layout:bgra\n"
	"bitfield:0x565\n"

	"match:[Indy]\n"
	"w:<u16>\n"
	"h:<u16>\n"
	"skip:248" // A run of zeros
);

// Rembrandt
/* Program with documentation (in French):
https://no-fragments.atari.org/no_fragments_04/archive/work/gfx/remb306b.zip
 * TODO: Support comments and multiple images, once we find files that use them
*/
const struct wuptr tcp_desc = DESC(
	/* Rembrandt header:
		Offset  Type    Name
		0       char    ID[8]
		8       u32     FileSize
		12      u16     HeaderSize
		14      u16     Version
		16      u16     NumPictures
		18
	 * Picture header:
		0       char    ID[4]
		4       u32     RasterSize
		8       u16     PictHeaderSize
		10      u16     Width
		12      u16     Height
		14      u16     TransparentColor
		16      u16     CrayonColor
		18      u8      Compression
		19      u8      HasPalette
		20      u8      Overscan
		21      u8      DoubleWidth
		22      u8      DoubleHeight
		23      char    Comment[175]
		198
	*/
	"endian:big\n"
	"bitdepth:16\n"
	"layout:bgra\n"
	"bitfield:0x565\n"

	"match:[TRUECOLR]\n"
	"skip:4\n"
	"match:[\x00\x12\x00\x01\x00\x01PICT]\n"
	"skip:4\n"
	"match:[\x00\xc6]\n"
	"w:<u16>\n"
	"h:<u16>\n"
	"skip:0xb8" // 0xc6 - 14
);

// Spooky Sprites TRP
const struct wuptr trp_desc = DESC(
	"endian:big\n"
	"bitdepth:16\n"
	"layout:bgra\n"
	"bitfield:0x565\n"

	"match:[tru?]\n"
	"w:<u16>\n"
	"h:<u16>"
);

/* Atari ST High Resolution */
// DA4 (PaintShop)
const struct wuptr da4_desc = DESC(
	"w:640\n"
	"h:800\n"
	"channels:1\n"
	"bitdepth:1\n"
	"attr:inverted"
);

// DOO (Atari Doodle)
const struct wuptr doo_desc = DESC(
	"w:640\n"
	"h:400\n"
	"channels:1\n"
	"bitdepth:1\n"
	"attr:inverted"
);

enum token_type {
	token_num,
	token_enum,
	token_load,
	token_load_array,
	token_str,
	token_colon,
	token_open_paren,
	token_close_paren,
	token_eof,
};

struct load {
	bool is_signed;
	uint8_t size;
	uint16_t array;
	uint32_t value;
};

struct token {
	enum token_type type;
	union {
		uintmax_t num;
		struct load load;
		struct wuptr str;
	} u;
};

static struct wu_st pbug(const char *msg) {
	return wuerr(wu_string_parse_error, msg);
}

static enum endianness get_endian(void *state) {
	return (enum endianness)(uintptr_t)state;
}

static int count_equals(struct mparser *mp, const uint8_t end) {
	int n = 0;
	int c;
	while ( (c = mp_next_char(mp)) == '=') {
		++n;
	}
	if (c == end) { // "[[", "[=["
		++n;
	} else { // "["
		if (n) { // "[="
			return -1;
		}
		mp->pos -= c != EOF;
	}
	return n;
}

static struct wu_st get_str(struct mparser *mp, struct token *tok) {
	const int depth = count_equals(mp, '[');
	if (depth < 0) {
		return pbug("Expected '[' delimiter");
	}
	size_t init = mp->pos;
	while (mp->pos < mp->len) {
		mp_skip_until(mp, ']');
		const size_t end = mp->pos - 1;
		const int e = count_equals(mp, ']');
		if (e == depth) {
			tok->type = token_str;
			tok->u.str = (struct wuptr) {
				.ptr = mp->mem + init,
				.len = end - init,
			};
			return wuok();
		}
	}
	return pbug("Unexpected end of string");
}

static struct wu_st get_load(struct mparser *mp, struct token *tok) {
	const int type = mp_next_char(mp);
	uintmax_t n;
	switch (type) {
	case 'i': case 'u':
		mp_scan_uint(mp, 2, &n);
		switch (n) {
		case 8: case 16: case 24: case 32:
			if (mp_next_char(mp) == '>') {
				int c = mp_cur_char(mp);
				tok->type = c == '[' ? token_load_array : token_load;
				tok->u.load = (struct load) {
					.is_signed = type == 'i',
					.size = (uint8_t)(n/8),
				};
				if (tok->type == token_load_array) {
					++mp->pos;
					// Array is either empty ("[]") or > 0
					if (!mp_scan_xint(mp, 4, &n) || n) {
						if (mp_next_char(mp) == ']') {
							tok->u.load.array =
								(uint16_t)n;
							return wuok();
						}
					}
				} else {
					return wuok();
				}
			}
		}
	}
	return pbug("Bad type spec");
}

static struct wu_st get_word(struct mparser *mp, struct token *tok) {
	const size_t init = mp->pos;
	for (;;) {
		const int c = mp_cur_char(mp);
		if (!isalnum(c)) {
			break;
		}
		++mp->pos;
	}
	tok->type = token_enum;
	tok->u.str = (struct wuptr){.ptr = mp->mem + init, .len = mp->pos - init};
	return wuok();
}

static struct wu_st read_token(struct mparser *mp, struct token *tok) {
	const int type = mp_next_nonspace(mp);
	switch (type) {
	case EOF: tok->type = token_eof; break;
	case ':': tok->type = token_colon; break;
	case '(': tok->type = token_open_paren; break;
	case ')': tok->type = token_close_paren; break;
	case '[': return get_str(mp, tok);
	case '<': return get_load(mp, tok);
	case '0': case '1': case '2': case '3': case '4':
	case '5': case '6': case '7': case '8': case '9':
		--mp->pos;
		mp_scan_xint(mp, 6, &tok->u.num);
		if (tok->u.num > 0xffff) {
			return pbug("Number literals greater than 65535 (0xffff)"
				" not supported");
		}
		tok->type = token_num;
		break;
	default:
		if (!isalpha(type)) {
			return pbug("Unexpected symbol in word");
		}
		--mp->pos;
		return get_word(mp, tok);
	}
	return wuok();
}

static uint8_t tohex(const uint8_t c) {
	switch (c) {
	case '0': case '1': case '2': case '3': case '4':
	case '5': case '6': case '7': case '8': case '9':
		return c - '0';
	case 'A': case 'a': return 0xa;
	case 'B': case 'b': return 0xb;
	case 'C': case 'c': return 0xc;
	case 'D': case 'd': return 0xd;
	case 'E': case 'e': return 0xe;
	case 'F': case 'f': return 0xf;
	}
	return 0x10;
}

static struct wu_st str_file_cmp(const struct wuptr arg, FILE *ifp) {
	size_t i = 0;
	const struct wu_st eof = wuerr(wu_unexpected_eof, NULL);
	while (i < arg.len) {
		int c = arg.ptr[i];
		++i;
		if (c == '\\') {
			if (i >= arg.len) {
				return eof;
			}
			uint8_t d = arg.ptr[i];
			++i;
			switch (d) {
			case '\\': break;
			case 't': c = '\t'; break;
			case 'r': c = '\r'; break;
			case 'n': c = '\n'; break;
			case '0': c = 0; break;
			case 'x':
				if (arg.len - i < 2) {
					return eof;
				}
				uint8_t x[2] = {
					tohex(arg.ptr[i]),
					tohex(arg.ptr[i+1]),
				};
				if (x[0] >= 0x10 || x[1] >= 0x10) {
					return pbug("Invalid hex literal");
				}
				c = x[0] << 4 | x[1];
				i += 2;
				break;
			default: return pbug("Unrecognized escape char");
			}
		}
		int f = getc(ifp);
		if (c != f) {
			return wuerr(wu_invalid_header, "Matching failure");
		}
	}
	return wuok();
}

static struct wu_st load_pal(struct wuimg *img, FILE *ifp,
const struct load l) {
	const size_t elems = l.array ? l.array : (1 << img->bitdepth);
	if (elems <= 256) {
		switch (l.size) {
		case 3: case 4:
			;struct palette *pal = wuimg_palette_init(img);
			if (pal) {
				const enum wu_error st = fmt_load_pal(ifp, pal,
					l.size, elems);
				return wuerr(st, "Palette load failure");
			}
			return wuerr(wu_alloc_error, "Palette alloc error");
		default: return pbug("Palette must be <u24> or <u32>");
		}
	}
	return pbug("Palette entries must be <= 256");
}

static struct wu_st load_val(struct image_file *infile, const struct token *tok,
uint32_t *scalar) {
	const uint8_t size = tok->u.load.size;
	void *ptr = scalar;
	if (!fread(ptr, size, 1, infile->ifp)) {
		return wuerr(wu_unexpected_eof, NULL);
	}
	const enum endianness e = get_endian(infile->dec_state);
	switch (size) {
	case 1: *scalar = *((uint8_t *)ptr); break;
	case 2: *scalar = endian16(*((uint16_t *)ptr), e); break;
	case 4: *scalar = endian32(*scalar, e); break;
	default: return pbug("Bad word size");
	}
	if (tok->u.load.is_signed && (*scalar & (1u << (size - 1)))) {
		return wuerr(wu_invalid_header, "Got negative value from file");
	}
	return wuok();
}

static bool shorthand(const struct wuptr op, const char *str) {
	if (op.len == 1) {
		return op.ptr[0] == str[0];
	}
	return wuptr_eq_str(op, str);
}

static struct wu_st set_num(struct wuimg *img, const struct wuptr op,
const uintmax_t num) {
	if (shorthand(op, "width")) {
		img->w = (size_t)num;
	} else if (shorthand(op, "height")) {
		img->h = (size_t)num;
	} else if (shorthand(op, "channels")) {
		img->channels = (uint8_t)num;
	} else if (shorthand(op, "bitdepth")) {
		img->bitdepth = (uint8_t)num;
	} else {
		return pbug("Unknown variable");
	}
	return wuok();
}

static struct wu_st exec_stmt(struct image_file *infile, const struct wuptr op,
const struct token *tok, uint32_t *scalar) {
	struct wuimg *img = infile->sub_img;
	switch (tok->type) {
	case token_str:
		if (wuptr_eq_str(op, "match")) {
			return str_file_cmp(tok->u.str, infile->ifp);
		}
		return pbug("Unknown variable-str pair");
	case token_enum:
		;const struct wuptr arg = tok->u.str;
		if (wuptr_eq_str(op, "endian")) {
			if (wuptr_eq_str(arg, "little")) {
				infile->dec_state = (void *)little_endian;
			} else if (wuptr_eq_str(arg, "big")) {
				infile->dec_state = (void *)big_endian;
			} else {
				return pbug("Bad endian value");
			}
		} else if (wuptr_eq_str(op, "layout")) {
			uint8_t buf[4] = {0, 0, 0, 1};
			if (arg.len != sizeof(buf)) {
				return pbug("Bad layout value");
			}
			if (!wuptr_eq_str(arg, "gray")) {
				for (uint8_t i = 0; i < sizeof(buf); ++i) {
					uint8_t c;
					switch (tolower(arg.ptr[i])) {
					case 'r': c = 0; break;
					case 'g': c = 1; break;
					case 'b': c = 2; break;
					case 'a': c = 3; break;
					default: return pbug("Bad layout value");
					}
					buf[c] = i;
				}
			}
			img->layout = pix_layout_pack(buf[0], buf[1], buf[2], buf[3]);
		} else if (wuptr_eq_str(op, "attr")) {
			if (wuptr_eq_str(arg, "normal")) {
				img->attr = pix_normal;
			} else if (wuptr_eq_str(arg, "inverted")) {
				img->attr = pix_inverted;
			} else if (wuptr_eq_str(arg, "signed")) {
				img->attr = pix_signed;
			} else if (wuptr_eq_str(arg, "float")) {
				img->attr = pix_float;
			} else {
				return pbug("Bad attr value");
			}
		} else if (wuptr_eq_str(op, "alpha")) {
			if (wuptr_eq_str(arg, "unassociated")) {
				img->alpha = alpha_unassociated;
			} else if (wuptr_eq_str(arg, "associated")) {
				img->alpha = alpha_associated;
			} else if (wuptr_eq_str(arg, "key")) {
				img->alpha = alpha_key;
			} else if (wuptr_eq_str(arg, "ignore")) {
				img->alpha = alpha_ignore;
			} else {
				return pbug("Bad alpha value");
			}
		} else {
			return pbug("Unknown variable-enum pair");
		}
		return wuok();
	case token_num:
		;const uintmax_t num = tok->u.num;
		if (wuptr_eq_str(op, "skip")) {
			fseek(infile->ifp, (long)num, SEEK_CUR);
		} else if (wuptr_eq_str(op, "bitrange")) {
			img->bitrange = (uint8_t)num;
		} else if (wuptr_eq_str(op, "bitfield")) {
			if (!wuimg_bitfield_from_id(img, (uint16_t)num)) {
				return wuerr(wu_alloc_error,
					"Bitfield alloc error");
			}
		} else if (wuptr_eq_str(op, "mirror")) {
			img->mirror = num;
		} else {
			return set_num(img, op, num);
		}
		return wuok();
	case token_load:
		;const struct wu_st err = load_val(infile, tok, scalar);
		if (!err.st) {
			if (wuptr_eq_str(op, "match")) {
				// Do nothing, just leave `scalar` set
			} else {
				return set_num(img, op, *scalar);
			}
		}
		return err;
	case token_load_array:
		if (wuptr_eq_str(op, "pal")) {
			return load_pal(img, infile->ifp, tok->u.load);
		}
		return pbug("Unknown variable-array pair");
	default:
		break;
	}
	return pbug("Syntax error");
}

struct parse_state {
	bool exec:1;
	bool got_match:1;
};

static struct wu_st parse(struct mparser *mp, struct image_file *infile) {
	uint32_t scalar = 0;

	struct parse_state state[4];
	uint8_t d = 0;
	state[d] = (struct parse_state) {.exec = true};
	for (;;) {
		struct token l;
		struct wu_st err = read_token(mp, &l);
		if (err.st != wu_ok) {
			return err;
		}

		switch (l.type) {
		case token_eof: return d ? pbug("Unclosed contexts") : wuok();
		case token_enum:
			;struct token r;
			err = read_token(mp, &r);
			if (err.st != wu_ok) {
				return err;
			} else if (r.type != token_colon) {
				return pbug("Syntax error");
			}
			err = read_token(mp, &r);
			if (err.st != wu_ok) {
				return err;
			} else if (state[d].exec) {
				scalar = 0;
				err = exec_stmt(infile, l.u.str, &r, &scalar);
				if (err.st != wu_ok) {
					return err;
				}
			}
			break;
		case token_num:
			if (state[d].got_match) {
				state[d].exec = false;
			} else {
				state[d].exec = l.u.num == scalar;
				state[d].got_match = state[d].exec;
			}
			break;
		case token_open_paren:
			++d;
			if (d >= ARRAY_LEN(state)) {
				return pbug("Max depth reached");
			}
			state[d] = (struct parse_state) {
				.exec = state[d-1].exec,
				.got_match = !state[d-1].exec & state[d-1].got_match,
			};
			break;
		case token_close_paren:
			if (!state[d].exec && !state[d].got_match) {
				return wuerr(wu_invalid_header, "No matches found");
			} else if (!d) {
				return pbug("Excess closing parens");
			}
			--d;
			break;
		default: return pbug("Syntax error");
		}
	}
	return wuok();
}

struct wu_st auto_load(struct image_file *infile) {
	struct wuimg *img = infile->sub_img;
	enum wu_error st = wuimg_alloc(img);
	if (st == wu_ok) {
		const enum endianness e = get_endian(infile->dec_state);
		return wuerr_partial(fmt_load_raster_swap(img, infile->ifp, e),
			wuimg_size(img));
	}
	return wuerr(st, NULL);
}

struct wu_st auto_init(struct image_file *infile, const struct wu_conf *conf,
const struct wuptr desc) {
	struct mparser mp = mp_wuptr(desc);
	const struct wu_st st = parse(&mp, infile);
	if (!wu_isok(st)) {
		if (st.msg && getenv("WU_DEBUG")) {
			term_line_put("Processed:", stderr);
			fwrite(mp.mem, 1, mp.pos, stderr);
			fputc('\n', stderr);
		}
		return st;
	}
	return wuerr(wuimg_exceeds_limit(infile->sub_img, conf)
		? wu_exceeds_size_limit : wu_ok, NULL);
}
