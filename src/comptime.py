# SPDX-License-Identifier: 0BSD
import sys
import collections
from itertools import batched, chain, starmap

EXT_LIMIT = 6
MAGIC_LIMIT = 12
NAME_LIMIT = 8

# These RAW formats are actually TIFF with extra data, and can only be
# distinguished by their extension. Still, they may contain a thumbnail that
# libtiff can handle, so we define these for both formats.
RAW_TIFF_EXTS = ("arw", "cr2", "dcr", "dng", "erf", "k25", "kdc", "nef", "nrw", "pef")

# "desc" = Format description
# "ext" = File extensions that are just informative (i.e. for filtering file lists)
#     Either a single string or a sequence of such
# "match" = Extensions that override magic detection, if any
#     Same format as "ext"
# "magic" = A signature that must match completely
#     A single byte string or a sequence of such
# "mask" = An AND mask plus magic signature. Only set bits need to match
#     A sequence of byte strings, where even terms are masks, and odd terms signatures
# "mime" = Format mime types, with "image/" prefix omitted. Unused at the moment
#     Same format as "ext"
DEC_MAP = {
	# Homemade decoders first
	"auto": {
		"avs": {
			"desc": "Stardent AVS X",
			"match": (
				"avs",
				"mbfavs",
				"x",
			),
		},
		"bob": {"desc": "Bob raytracer raster", "match": "bob"},
		"bru": {"desc": "Degas Brush", "match": "bru"},
		"farbfeld": {
			"desc": "farbfeld",
			"ext": "ff",
			"magic": b"farbfeld",
		},
		"gemview": {
			"desc": "GEM View-Dither",
			"ext": "dit",
			"magic": b"B&W256",
		},
		"hpicon": {
			"desc": "HP Palmtop Icon",
			"ext": "icn",
			"magic":
				# 0x002c and 0x0020 are the image dimensions, but
				# they're always the same so match against them too
				b"\x01\x00\x01\x00\x2c\x00\x20\x00",
		},
		"iim": {
			"desc": "InShape IIM",
			"ext": "iim",
			"magic": b"IS_IMAGE\0",
		},
		"nlm": {
			"desc": "Nokia Logo Manager",
			"ext": "nlm",
			"mask": (
				# Sixth byte is version, 0 to 3
				b"\xff\xff\xff\xff\xff\xfc", b"NLM \x01\x00",
			),
		},

		# Atari Falcon True Color family
		"coke": {
			"desc": "COKE (Atari Falcon)",
			"ext": "tg1",
			"magic": b"COKE format.",
		},
		"eggpaint": {
			"desc": "EggPaint",
			"ext": "trp",
			"magic": b"TRUP",
		},
		"ftc": {"desc": "Falcon True Color", "match": "ftc"},
		"god": {"desc": "GodPaint", "match": "god"},
		"indy": {
			"desc": "IndyPaint",
			"ext": ("hgr", "tru"),
			"magic": b"Indy",
		},
		"tcp": {
			"desc": "Atari Rembrandt",
			"ext": "tcp",
			"magic": b"TRUECOLR",
		},
		"trp": {
			"desc": "Spooky Sprites uncompressed",
			"ext": ("trp", "tru"),
			"magic": b"tru?",
		},

		# Atari ST High Resolution
		"da4": {"desc": "PaintShop (Atari ST)", "match": "da4"},
		"doo": {"desc": "Atari Doodle", "match": "doo"},
	},

	"atari": {
		"dali": {
			"desc": "Dali uncompressed",
			"match": ("sd0", "sd1", "sd2")
		},

		"degas": {
			"desc": "DEGAS and DEGAS Elite",
			"match": (
				"pi1", "pi2", "pi3",
				"pc1", "pc2", "pc3",
			)
		},

		"bld": {
			"desc": "MegaPaint",
			"match": "bld"
		},

		"tiny": {
			"desc": "Tiny Stuff",
			"match": (
				"tny",
				"tn1", "tn2", "tn3",
				"tn4", "tn5", "tn6",
			)
		},
	},

	"c64": {
		"c64": {
			"desc": "Art Studio (OCP), Artist64, Blazing Paddles, Create With Garfield"
				", Doodle (raw & compressed), Hi-Eddi, Image System, Interpaint"
				", KoalaPainter (raw & compressed), Runpaint, Saracen Paint"
				", Vidcom 64",
			"match": (
				# Omit conflicting extensions until we improve detection capabilities
				# Art Studio
				"aas", "hpi", "ocp", #"art,"
				# Wigmore Artist64
				"a64", "wig",
				# Blazing Paddles
				"bp", "bpl", #"pi",
				# Create With Garfield
				"cwg",
				# Doodle
				"dd", "ddl", "jj",
				# Hi-Eddi
				"hed",
				# Image System
				"ims", "ish", "ism",
				# Interpaint
				"ip64h", "iph", "ipt",
				# KoalaPainter
				"gg", "gig", "kla", "koa", "koala",
				# Runpaint
				"rpm",
				# Saracen Paint
				"sar",
				# Vidcom 64
				"vid",
			),
			#"mask": (b"\xff\x03", b"\x00\x00"),
		},
	},

	"cbg": {
		"cbg": {
			"desc": "BGI/Ethornell CompressedBG (v1)",
			"magic": b"CompressedBG___\0",
		}
	},

	"dib": {
		"bmp": {
			"desc": "Microsoft Bitmap",
			"ext": ("bmp", "bmp24"),
			"magic": b"BM",
			"mime": ("bmp", "x-bmp"),
		},

		"dib": {
			"desc": "Microsoft DIB",
			"match": "dib",
			"mime": "x-ms-bmp",
		},

		"ico": {
			"desc": "Microsoft Icon",
			"match": ("cur", "ico"),
			"mime": ("vnd.microsoft.icon", "x-icon"),
		},

		"bmz": {
			"desc": "GSD engine BMZ",
			"ext": "bmz",
			"magic": b"ZLC3",
		},
	},

	"dpx": {
		"dpx": {
			"desc": "Digital Picture Exchange",
			"ext": "dpx",
			"mask": (
				b"\xff\xff\xff\xff" b"\0\0\0\0" b"\xff\x00\xff\xff",
					b"XPDS\0\0\0\0V\0.0",
				b"\xff\xff\xff\xff" b"\0\0\0\0" b"\xff\x00\xff\xff",
					b"SDPX\0\0\0\0V\0.0",
			),
			"mime": "dpx",
		},
	},

	"elecbyte": {
		"eb_fnt": {
			"desc": "Elecbyte M.U.G.E.N. Font v1. Requires PCX",
			"ext": "fnt",
			"magic": b"ElecbyteFnt\0",
		},

		"eb_sff": {
			"desc": "Elecbyte M.U.G.E.N. Sprite v1. Requires PCX",
			"ext": "sff",
			"magic": b"ElecbyteSpr\0",
		},
	},

	"g00": {"g00": {"desc": "RealLive engine G00", "match": "g00"}},

	"gp4": {"gp4": {"desc": "elf AI5 engine GP4", "match": "gp4"}},

	"gpc": {
		"gpc": {
			"desc": "IDES/Fairytale PC-98 visual novel image",
			"ext": "gpc",
			"magic": b"PC98)GPCFILE   \0",
		},

		"clm": {
			"desc": "IDES/Fairytale PC-98 thumbnail? (no palette support)",
			"match": "clm",
		},
	},

	"hel": {
		"hel": {
			"desc": "Herahera Animation (へらへらアニメ, HEL)",
			"ext": "hel",
			"magic": b"he1\0" b"\x01\0\0\0",
		},
	},

	"hg3": {
		"hg3": {
			"desc": "CatSystem engine image",
			"ext": "hg3",
			"magic": b"HG-3",
		},
	},

	"ilbm": {
		"ilbm": {
			"desc": "Interleaved Bitmap (ILBM and PBM) (Extra Half-Brite, HAM, color cycling)",
			"ext": ("bl1", "iff", "ilbm", "lbm"),
			"mask": (
				b"\xff\xff\xff\xff" b"\0\0\0\0" b"\xff\xff\xff\xff",
					b"FORM\0\0\0\0ILBM",
				b"\xff\xff\xff\xff" b"\0\0\0\0" b"\xff\xff\xff\xff",
					b"FORM\0\0\0\0PBM ",
			),
		},
	},

	"kyg": {
		"kyg": {
			"desc": "Kyss graphics format (KYG)",
			"ext": "kyg",
			"magic": b"KYGformat ver.0.10\x0d\x0a",
		},
	},

	"mac": {"mac": {"desc": "MacPaint", "match": ("mac", "pntg")}},

	"mag": {
		"mag": {
			"desc": "MAG (MAKI02)",
			"ext": ("mag", "max"),
			"magic": b"MAKI02  ",
		},
	},

	"maki": {
		"maki": {
			"desc": "MAKIchan (MAKI01)",
			"ext": "mki",
			"magic": (b"MAKI01A ", b"MAKI01B "),
		}
	},

	"msx": {
		"msx": {
			"desc": "MSX-BASIC dump, Graph Saurus",
			# You can sort of tell whether a file is an MSX-BASIC format,
			# but you can rarely tell the screen mode it uses without the
			# extension.
			"match": (
				# sc? = MSX-BASIC file
				# s1? = Alternate field of a sc? file
				# sr? = Graph Saurus file
				"sc2", "grp",
				"sc3",
				"sc4",
				"sc5", "sr5", "ge5",
				"sc6", "s16", "sr6",
				"sc7", "s17", "sr7", "ge7",
				"sc8", "sr8", "ge8",
				"sca", "s1a",
				"scc", "s1c", "srs", "yjk"
			),
			"magic":
				# Graph saurus SR5
				b"\xfe\x00\x00\x00\x6a\x00\x00",
		},
	},

	"pcf": {
		"pcf": {
			"desc": "PCF bitmap font",
			"ext": "pcf",
			"magic": b"\1fcp",
		},
	},

	"pcx": {
		"dcx": {
			"desc": "Multi-image PCX",
			"ext": "dcx",
			"magic": b"\xb1\x68\xde\x3a",
			"mime": "x-dcx",
		},

		"pcx": {
			"desc": "PC Paintbrush PCX (all versions, plus CGA mode)",
			"ext": ("pcc", "pcx"),
			"mask": (
				# Second byte is version. Valid values are 0,2,3,4,5
				# (If 1 were a valid version, we could get away with
				# just two sequences. This damned format truly screws
				# with us at every possible turn.)
				b"\xff\xff\xff", b"\x0a\x00\x01", # 0
				b"\xff\xfe\xff", b"\x0a\x02\x01", # 2,3
				b"\xff\xfe\xff", b"\x0a\x04\x01", # 4,5
			),
			"mime": "x-pcx",
		},
	},

	"pdt": {
		"pdt": {
			"desc": "RealLive engine PDT10 and PDT11",
			"ext": "pdt",
			"mask": (
				# Match "PDT10" and "PDT11"
				b"\xff\xff\xff\xff\xfe\xff\xff\xff", b"PDT10\x00\x00\x00",
			)
		},
	},

	"pgx": {
		"pgx": {
			"desc": "Glib2 engine image",
			"ext": "pgx",
			"magic": b"PGX\0",
		},
	},

	"pi": {
		"pi": {
			"desc": "Yanagisawa's Pi",
			"ext": "pi",
			"magic": b"Pi",
		},
	},

	"pic": {
		"pic": {
			"desc": "Yanagisawa's PIC",
			"ext": "pic",
			"magic": b"PIC",
		},
	},

	"pic2": {
		"pic2": {
			"desc": "Yanagisawa's PIC2",
			"ext": "p2",
			"magic": b"P2DT",
		},
	},

	"pictor": {
		"pictor": {
			"desc": "PCPaint PICtor",
			"ext": "pic",
			"magic": b"\x34\x12",
		},
	},

	"pnm": {
		"pnm": {
			"desc": "PNM extended family (PNM, PAM, PFM, PHM, Xv Thumbnail, MTV, JPEG2000-PGX)",
			"ext": (
				"pbm", "pgm", "ppm", "pam", "pnm",
				"pfm", "phm",
				"p7",
				"pgx"
			),
			"match": "mtv",
			"mask": (
				# Second byte is ASCII version.
				b"\xff\xff", b"P1",
				b"\xff\xfe", b"P2", # '2', '3'
				b"\xff\xfe", b"P4", # '4', '5'
				b"\xff\xff", b"P6",

				b"\xff\xdf", b"PF", # 'F', 'f' (color/gray PFM)
				b"\xff\xdf", b"PH", # 'H', 'h' (color/gray PHM)
			),
			"magic": (
				b"P7\n", # PAM
				b"P7 332\n", # Xv thumbnail

				# JPEG2000 PGX
				b"PG ML ",
				b"PG LM ",
			),
			"mime": (
				"x-portable-bitmap",       # PBM
				"x-portable-graymap",      # Text PGM
				"x-portable-greymap",      # Raw PGM. blame `file` for the spellings
				"x-portable-pixmap",       # PPM
				"x-portable-arbitrarymap", # PAM
				"x-xv-thumbnail",          # Xv
			),
		},
	},

	"prt": {
		"prt": {
			"desc": "Kid engine image",
			"ext": ("cps", "prt"),
			"magic": b"PRT\0",
		},
	},

	"px": {"px": {"desc": "Leaf engine image", "match": "px"}},

	"q4": {
		"q4": {
			"desc": "MAJYO's Q4 (XLD4)",
			# This format has a magic sequence, but until we bump
			# the signature limit to 16, it's not very useful, so
			# match on extension
			"match": "q4",
			"#mask": (
				b"\xff\0\0\0" b"\0\0\0\0" b"\0\0\0" b"\xff\xff\xff\xff\xff",
				b"\x1a\0\0\0" b"\0\0\0\0" b"\0\0\0" b"MAJYO",
			),
		},
	},

	"qoi": {
		"qoi": {
			"desc": "QuiteOK image",
			"ext": "qoi",
			"magic": b"qoif",
		},
	},

	"quake": {
		"idsp": {
			"desc": "id Software Sprite (Quake, Half-Life, and 32-bit variants)",
			"ext": ("spr", "spr32"),
			"magic": b"IDSP",
		},
		"lmp": {
			"desc": "Quake LMP",
			"match": "lmp",
		},
	},

	"sgi": {
		"sgi": {
			"desc": "Silicon Graphics Image",
			"ext": ("bw", "rgb", "rgba", "sgi"),
			"magic": b"\x01\xda",
		},
	},

	"signum": {
		"imc": {
			"desc": "Signum! IMC",
			"ext": "imc",
			"magic": b"bimc0002",
		},
	},

	"sixel": {
		"sixel": {
			"desc": "SIXEL terminal graphics",
			"match": ("six", "sixel"),
			"magic": (
				# SIXEL files may contain arbitrary terminal
				# sequences, so detection is not foolproof.
				b"\x90",  # 8-bit espace sequence
				b"\x1bP", # 7-bit escape sequence
			),
		},
	},

	"spooky": {
		"tre": {
			"desc": "Spooky Sprites Run-Length Encoded",
			"ext": ("dta", "tre"),
			"magic": b"tre1",
		},

		"trs": {
			"desc": "Spooky Sprites sprite",
			"ext": "trs",
			"magic": b"TCSF",
		},
	},

	"sun": {
		"sun": {
			"desc": "Sun Raster",
			"ext": (
				"im1", "im4", "im8", "im24", "im32",
				"ras", "sun"
			),
			"magic": b"\x59\xa6\x6a\x95",
		},
	},

	"tga": {
		"tga": {
			"desc": "Truevision TGA (TARGA)",
			"match": "tga",
			# Depending on the version, TGA has a signature... at the end
			"mime": "x-tga",
		},
	},

	"tim": {
		"tim": {
			"desc": "PlayStation image",
			"ext": "tim",
			"magic": b"\x10\x00\x00\x00",
			"mime": "x-sony-tim",
		},
	},

	"tlg": {
		"tlg": {
			"desc": "KiriKiri engine image (v5)",
			"ext": "tlg",
			"magic": (
				b"TLG5.0\x00raw\x1a",
				# Not supported
				#b"TLG6.0\x00raw\x1a",
				#b"TLG0.0\x00sds\x1a",
			),
		},
	},

	"txf": {
		"txf": {
			"desc": "TexFont Texture Mapped Font",
			"ext": "txf",
			"magic": b"\xfftxf",
		},
	},

	"wbmp": {
		"wbmp": {
			"desc": "Wireless Bitmap",
			"match": "wbmp",
			"mime": "vnd.wap.wbmp",
		},
	},

	"wgtspr": {
		"wgtspr": {
			"desc": "WGT Sprite",
			"ext": "spr",
			"mask": (
				# First byte is version.
				b"\xfc\xff\xff\xff" b"\xff\xff\xff\xff" b"\xff\xff\xff\xff" b"\xff\xff\xff",
					b"\x00\0 Sprite File ", # 0..3
				b"\xff\xff\xff\xff" b"\xff\xff\xff\xff" b"\xff\xff\xff\xff" b"\xff\xff\xff",
					b"\x04\0 Sprite File ", # 4
			),
		},
	},

	"wpx": {
		"wbm": {
			"desc": "Wild-Bug engine image",
			"ext": "wbm",
			"magic": b"WPX\x1aBMP\0",
		},

		"wia": {
			"desc": "Wild-Bug engine image sequence",
			"ext": "wia",
			"magic": b"WPX\x1aIA2\0",
		},
	},

	"xbm": {
		"xbm": {
			"desc": "X10 and X11 Bitmap",
			"match": "xbm",
			"magic": (
				b"/*",
				b"//",
				b"#define ",
			),
			"mime": "xbm",
		},
	},

	"xcursor": {"xcursor": {"desc": "X11 cursor", "magic": b"Xcur"}},

	"xwd": {"xwd": {"desc": "X11 Window Dump", "match": ("dmp", "xwd")}},

	"xyz": {
		"xyz": {
			"desc": "RPG Maker image",
			"ext": "xyz",
			"magic": b"XYZ1",
		},
	},

	# External decoders
	"avif|heif": {
		"avif": {
			"desc": "AV1 Image File Format",
			"ext": ("avif", "avifs"),
			"mask": (
				# See heif definition for notes
				# avic|avis
				b"\0\0\0\x03" b"\xff\xff\xff\xff" b"\xff\xff\xff\xff",
					b"\0\0\0\0" b"ftypavif",
				b"\0\0\0\x03" b"\xff\xff\xff\xff" b"\xff\xff\xff\xef",
					b"\0\0\0\0" b"ftypavic",
				#b"\0\0\0\x03" b"\xff\xff\xff\xff" b"\xff\xff\xff\xff",
				#	b"\0\0\0\0" b"ftypavis",
			),
			"mime": "avif",
		},
	},

	"flif": {
		"flif": {
			"desc": "Free Lossless Image Format",
			"ext": "flif",
			"magic": b"FLIF",
		},
	},

	"gif": {
		"gif": {
			"desc": "Graphics Interchange Format",
			"ext": ("gif", "gif87", "gif89"),
			"magic": (
				b"GIF87a",
				b"GIF89a",
			),
			"mime": "gif",
		},
	},

	"heif": {
		"heif": {
			"desc": "High Efficiency Image File Format",
			"ext": (
				"heic", "heics",
				"heif", "heifs",
				"hif",
			),
			"mask": (
				# HEIF follows ISOBMFF, so we can't stop at 'ftyp'
				# or we could match a few hundred other formats.
				# https://github.com/file/file/blob/master/magic/Magdir/animation

				# The first 32-bit word (little-endian) is an offset
				# to something not relevant to us, but it must be a
				# multiple of 4.

				# heic|heix|heim|heis
				# Take advantage that c = 0110_0011 and s = 0111_0011
				b"\0\0\0\x03" b"\xff\xff\xff\xff" b"\xff\xff\xff\xef",
					b"\0\0\0\0" b"ftypheic",
				b"\0\0\0\x03" b"\xff\xff\xff\xff" b"\xff\xff\xff\xff",
					b"\0\0\0\0" b"ftypheix",
				b"\0\0\0\x03" b"\xff\xff\xff\xff" b"\xff\xff\xff\xff",
					b"\0\0\0\0" b"ftypheim",
				#b"\0\0\0\x03" b"\xff\xff\xff\xff" b"\xff\xff\xff\xff",
				#	b"\0\0\0\0" b"ftypheis",

				# hevc|hevx|hevm|hevs
				b"\0\0\0\x03" b"\xff\xff\xff\xff" b"\xff\xff\xff\xef",
					b"\0\0\0\0" b"ftyphevc",
				b"\0\0\0\x03" b"\xff\xff\xff\xff" b"\xff\xff\xff\xff",
					b"\0\0\0\0" b"ftyphevx",
				b"\0\0\0\x03" b"\xff\xff\xff\xff" b"\xff\xff\xff\xff",
					b"\0\0\0\0" b"ftyphevm",
				#b"\0\0\0\x03" b"\xff\xff\xff\xff" b"\xff\xff\xff\xff",
				#	b"\0\0\0\0" b"ftyphevs",

				# mif1|msf1
				b"\0\0\0\x03" b"\xff\xff\xff\xff" b"\xff\xff\xff\xff",
					b"\0\0\0\0" b"ftypmif1",
				b"\0\0\0\x03" b"\xff\xff\xff\xff" b"\xff\xff\xff\xff",
					b"\0\0\0\0" b"ftypmsf1",
			),
			"mime": ("heic", "heif", "heic-sequence", "heif-sequence"),
		},
	},

	"jbig": {
		"jbig": {
			"desc": "JBIG",
			"match": ("bie", "jbg", "jbig"),
			"mime": "jbig",
		},
	},

	"jbig2": {
		"jbig2": {
			"desc": "JBIG2",
			"ext": "jb2",
			"magic": b"\x97JB2\x0d\x0a\x1a\x0a",
		},
	},

	"jpeg": {
		"jpeg": {
			"desc": "JPEG, MPO",
			"ext": (
				"dt2", # Microsoft Messenger
				"jfi", "jfif", "jif",
				"jpe", "jpeg", "jpg",
				"jps",
				"mpo",
				"thm",
				# Not sure where I got this from. Conflicts with Tiny Stuff
				"tn3",
			),
			# In a well written JPEG the third byte would be 0xff.
			# Not all JPEG files are well written.
			"magic": b"\xff\xd8",
			"mime": "jpeg",
		},
	},

	"jpeg2000": {
		"j2k": {
			"desc": "JPEG 2000 codestream",
			"ext": ("j2c", "j2k"),
			"magic": b"\xff\x4f\xff\x51",
		},

		"jp2": {
			"desc": "JPEG 2000",
			"ext": (
				"jp2", "jpc",
				"jhc", "jph", # High troughput
			),
			"magic": b"\r\n\x87\n",
			"mask": (
				b"\xff\xff\xff\x00" b"\xff\xff\xff\xff" b"\xff\xff\xff\xff",
					b"\0\0\0\0" b"jP\x20\x20" b"\r\n\x87\n",
			),
			"mime": "jp2",
		},
	},

	"jpegls": {
		"jpegls": {
			# JPEG-LS and JPEG share the same structure, so it's not
			# possible to tell them apart without parsing
			"desc": "JPEG LS",
			"match": "jls",
			"mime": "jls",
		},
	},

	"jpegxl": {
		"jpegxl": {
			"desc": "JPEG XL",
			"ext": "jxl",
			"magic": b"\xff\x0a",
			"mask": (
				b"\xff\xff\xff\x00" b"\xff\xff\xff\xff" b"\xff\xff\xff\xff",
					b"\0\0\0\0" b"JXL\x20" b"\r\n\x87\n",
			),
			"mime": "jxl",
		},
	},

	"lerc": {
		"lerc": {
			"desc": "Limited Error Raster Compression",
			"ext": ("lrc", "lerc", "lerc1", "lerc2"),
			"magic": (
				b"CntZImage ",
				b"Lerc2 ",
			),
		},
	},

	"png": {
		"png": {
			"desc": "Portable Network Graphics",
			"ext": "png",
			"magic": b"\x89PNG\r\n\x1a\n",
			"mime": "png",
		},
	},

	"raw": {
		"raw": {
			"desc": "Raw camera formats",
			"ext": (
				"orf", "raf", "raw", "rw2", "rwl", # it's RWL, not RW1!
			),
			"match": RAW_TIFF_EXTS,
			"magic": (
				# Olympus ORF
				b"IIRS",
				b"IIRO",
				b"MMOR",

				# Panasonic RAW/RW2
				b"IIU\0",

				# Fujifilm Raw
				b"FUJIFILMCCD-RAW ",
			),
			"mime": ("x-canon-cr2", "x-canon-crw", "x-fuji-raf", "x-olympus-orf"),
		},
	},

	"svg": {
		"svg": {
			"desc": "Scalable Vector Graphics",
			"match": ("svg", "svgz"),
			"mime": "svg+xml",
		},
	},

	"tiff": {
		"tiff": {
			"desc": "Tag Image File Format, BigTIFF",
			"ext": ("tif", "tiff") + RAW_TIFF_EXTS,
			"magic": (
				b"II\x2a\x00",
				b"MM\x00\x2a",

				# BigTIFF
				b"II\x2b\x00\x08\x00\0\0",
				b"MM\x00\x2b\x00\x08\0\0",
			),
			"mime": "tiff",
		},
	},

	"webp": {
		"webp": {
			"desc": "WebP",
			"ext": "webp",
			"mask": (
				b"\xff\xff\xff\xff" b"\0\0\0\0" b"\xff\xff\xff\xff",
					b"RIFF\0\0\0\0WEBP",
			),
			"mime": "webp",
		},
	},
}

# application/ mimetypes supported by libarchive
MIME_APPLICATION_MAP = (
	"gzip",
	"x-7z-compressed",
	"x-cpio",
	"x-lzh-compressed",
	"x-rar",
	"x-tar",
	"zip",
)

def foreach(fn, it):
	collections.deque(map(fn, it), maxlen=0)

def eprint(*p):
	print(*p, file=sys.stderr)

def maskcmp(b):
	return int.from_bytes(b).bit_count()

def graph_or_hex(i, readable):
	if i >= 0x20 and i < 0x80:
		return "'{}'".format(chr(i))
	return '0x{:02x}'.format(i)

def u8_array(b, limit=None, readable=False):
	if not limit:
		limit = len(b)
	elif len(b) > limit:
		eprint('array exceeds length limit. will truncate:', b)
	return ','.join(map(lambda i: graph_or_hex(i, readable), b[0:limit]))

class FmtMagic(collections.namedtuple('magic', ('mask', 'bytes', 'id'))):
	__slots__ = ()
	@staticmethod
	def struct(limit):
		return '''\
		struct fmt_magic {{
			const unsigned char and_mask[{0}];
			const unsigned char bytes[{0}];
			const short id;
		}};'''.format(limit)

	def declare(self, limit):
		return '\t{{ .and_mask={{ {} }}, .bytes={{ {} }}, .id={} }},'.format(
			u8_array(self.mask, limit, False),
			u8_array(self.bytes, limit, True),
			self.id)

	def __lt__(self, other):
		# Compare number of mask bits, then magic bytes
		m1 = maskcmp(self.mask)
		m2 = maskcmp(other.mask)
		if m1 == m2:
			return self.bytes < other.bytes
		return m1 < m2

class FmtExt(collections.namedtuple('ext', ('ext', 'id'))):
	__slots__ = ()
	@staticmethod
	def struct(limit):
		return '''\
		struct fmt_ext {{
			const char ext[{0}];
			const short id;
		}};'''.format(limit)

	def declare(self, ext_limit):
		if len(self.ext) > ext_limit:
			raise BaseException('extension exceeds length limit: ' + self.ext)
		return '\t{{ .ext="{}", .id={} }},'.format(*self)

	def __lt__(self, other):
		if self[0] == other[0]:
			# Reverse id order so that -1 values are last
			return other[1] < self[1]
		return self[0] < other[0]

def full_mask(mag, id):
	if isinstance(mag, bytes):
		return FmtMagic(b'\xff' * len(mag), mag, id)
	raise BaseException('magic must be bytes sequence: ' + mag)

def mask_from_magic(info, id):
	magic = info.get('magic')
	if isinstance(magic, (bytes, str)):
		yield full_mask(magic, id)
	elif magic:
		yield from map(lambda m: full_mask(m, id), magic)

def mask_extract(name, info, id):
	masks = info.get('mask')
	if masks:
		if len(masks) % 2 != 0:
			raise BaseException('invalid mask sequence length in ' + name)
		for mask, mag in batched(masks, 2):
			if not isinstance(mask, bytes) or not isinstance(mag, bytes):
				raise BaseException(f'mask must be bytes sequence: {mag}')
			elif len(mask) != len(mag):
				raise BaseException(f'AND mask and magic have different lengths: {mag}')
			yield FmtMagic(mask, mag, id)

def ext_iter(exts, id=-1):
	if isinstance(exts, str):
		yield FmtExt(exts, id)
	elif exts:
		yield from map(lambda e: FmtExt(e, id), exts)

# fmt_desc is defined in wudefs.h
class FmtDesc(collections.namedtuple('desc', ('dec', 'name', 'info'))):
	__slots__ = ()
	def extern(self):
		dec, name, info = self
		is_auto = dec == 'auto'
		suffix = 'desc' if is_auto else 'fn'
		type = 'auto_desc' if is_auto else 'image_fn'
		return f'extern const struct {type} {name}_{suffix};'

	def declare(self, name_limit):
		dec, name, info = self
		if len(name) > name_limit:
			raise BaseException('format name exceeds length limit: ' + name)
		is_auto = dec == 'auto'
		is_auto_str = 'true' if is_auto else 'false'
		suffix = 'desc' if is_auto else 'fn'
		return '''\
		{{
			.name = "{0}",
			.description = "{1}",
			.is_auto = {2},
			.dec.{3} = &{0}_{3},
		}},'''.format(name, info['desc'], is_auto_str, suffix)

	def get_exts(self, id):
		yield from ext_iter(self.info.get('ext'))
		yield from ext_iter(self.info.get('match'), id)

	def get_magics(self, id):
		yield from mask_extract(self.name, self.info, id)
		yield from mask_from_magic(self.info, id)

	def get_mimes(self, _id):
		mime = self.info.get('mime')
		if isinstance(mime, str):
			yield mime
		elif mime:
			yield from mime

	def __lt__(self, other):
		return self.name < other.name

def begin_map_def(name):
	print('static const struct fmt_{0} {0}_map[] = {{'.format(name))

def end_def():
	print('};')

def struct_and_define(type, limit):
	print(type.struct(limit))
	begin_map_def(type.__name__.replace('Fmt', '').lower())

def fmt_map_iter(getter, fmt_map):
	return chain.from_iterable(starmap(lambda id, fmt: getter(fmt, id), enumerate(fmt_map)))

def minmax(min_len, max_len, n):
	return min(min_len, n), max(max_len, n)

def print_fmt_magic(fmt_map, limit):
	# Reverse so that masks with more bits come first
	magic_map = sorted(fmt_map_iter(FmtDesc.get_magics, fmt_map), reverse=True)

	min_len = limit
	max_len = 0
	struct_and_define(FmtMagic, limit)
	for magic in magic_map:
		print(magic.declare(limit))
		min_len, max_len = minmax(min_len, max_len, len(magic.mask))
	end_def()
	return min_len, min(max_len, limit)

def ext_filter(ext_map, n, cur):
	if n:
		prev = ext_map[n-1]
		if cur.ext == prev.ext:
			if cur.id == prev.id:
				eprint(f'found repeated "{cur.ext}" informative extensions. disregarding.')
			else:
				if cur.id != -1:
					raise BaseException('conflicting "match" extensions: ' + cur.ext)
				eprint(f'found repeated "info" and "match" type extensions for "{cur.ext}". will disregard "info"')
			return False
	return True

def print_fmt_ext(fmt_map, limit):
	ext_map = sorted(fmt_map_iter(FmtDesc.get_exts, fmt_map))

	min_len = limit
	max_len = 0
	struct_and_define(FmtExt, limit)
	for _, ext in filter(lambda t: ext_filter(ext_map, *t), enumerate(ext_map)):
		print(ext.declare(limit))
		min_len, max_len = minmax(min_len, max_len, len(ext.ext))
	end_def()
	return min_len, max_len

def print_fmt_desc(fmt_map, limit):
	begin_map_def('desc')
	foreach(lambda fmt: print(fmt.declare(limit)), fmt_map)
	end_def()

def print_include(name):
	print('#include "', name, '"', sep='');

def gen_maps(fmt_map):
	# Include the output of dec_header()
	print_include('dec_fn.h')

	# Generate three arrays out of the format map:
	# metadata and decoder pointers, magic sequences, and extensions
	# These are sorted so that the program won't need initialization routines
	print_fmt_desc(fmt_map, NAME_LIMIT)
	min_mag_len, max_mag_len = print_fmt_magic(fmt_map, MAGIC_LIMIT)
	min_ext_len, max_ext_len = print_fmt_ext(fmt_map, EXT_LIMIT)

	# Print length bounds
	print(f'''
		static const size_t MIN_MAG_LEN = {min_mag_len};
		static const size_t MAX_MAG_LEN = {max_mag_len};
		static const size_t MIN_EXT_LEN = {min_ext_len};
		static const size_t MAX_EXT_LEN = {max_ext_len};'''.replace('\t', ''))

	# Include the rest of the file
	print_include('fmtmap.c')

def dec_header(fmt_map):
	print_include('wudefs.h');
	foreach(lambda fmt: print(fmt.extern()), fmt_map)

def get_mimes(fmt_map):
	image = map('image/'.__add__, fmt_map_iter(FmtDesc.get_mimes, fmt_map))
	application = map('application/'.__add__, MIME_APPLICATION_MAP)
	foreach(print, chain(image, application, ('inode/directory',)))

def show_supported(fmt_map):
	width = 1 + max(map(lambda fmt: max(len(fmt.dec), len(fmt.name)), fmt_map))
	tpl = '{:{width}}{:{width}}{}'
	print_tab = lambda t: print(tpl.format(*t, width=width))

	# Decoder and format table
	print(len(DEC_MAP), 'decoders,', len(fmt_map), 'formats supported\n')

	header = tpl.format('Decoder', 'Format', 'Description', width=width)
	print(header)
	print('-' * (len(header) + 1))
	foreach(print_tab, sorted(
		map(lambda fmt: (fmt.dec, fmt.name, fmt.info['desc']), fmt_map)
	))
	print()

	# Extensions
	exts = sorted(set(
		map(lambda f: f.ext, fmt_map_iter(FmtDesc.get_exts, fmt_map))
	))
	print('Known extensions:', len(exts))
	print(', '.join(exts), end='\n\n')

	# Nagic sequences
	tpl = '{:{width}}{}'
	magics = sorted(map(lambda m: (fmt_map[m.id].name, m.bytes),
		fmt_map_iter(FmtDesc.get_magics, fmt_map)
	))
	print('Known magic sequences:', len(magics))
	foreach(print_tab, magics)

def extract_fmt(dec, fmts):
	return map(lambda t: FmtDesc(dec, *t), fmts.items())

def fmts_from_decs(dec, enabled=None):
	it = dec.items()
	if enabled:
		it = filter(lambda t: any(map(enabled.__contains__, t[0].split('|'))), it)
	return chain.from_iterable(starmap(extract_fmt, it))

def enabled_formats(file, include=tuple()):
	prefix = '#define WU_ENABLE_'
	return frozenset(chain(include,
		map(lambda s: s[len(prefix):-1].lower(),
			filter(lambda s: s.startswith(prefix), file)
		)
	))

if __name__ == '__main__':
	enabled = None
	i = 1
	if sys.argv[i] == '-all':
		i += 1
	else:
		enabled = enabled_formats(sys.stdin, ('auto',))

	# Filter and flatten the decoder map to get a format list.
	# After sorting, the position within the list will be the format id.
	fmt_map = sorted(fmts_from_decs(DEC_MAP, enabled))

	{
		'maps': gen_maps,
		'header': dec_header,
		'mime': get_mimes,
		'show': show_supported,
	}[sys.argv[i]](fmt_map)
