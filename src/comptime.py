# SPDX-License-Identifier: 0BSD
# SPDX-FileCopyrightText: 2024 kaleido
import sys
import collections
import typing
from itertools import batched, chain, starmap
from functools import partial
from collections.abc import Callable, Iterable, Sequence

EXT_LIMIT = 6
MAGIC_LIMIT = 12
NAME_LIMIT = 8

# Various RAW camera formats are actually TIFF with extra data, and can only be
# distinguished by their extension. Still, they may contain a thumbnail that
# libtiff can handle, so we define these for both formats.
RAW_TIFF_EXTS = (
	# Sony
	"arw", "sr2", "srf",
	# Canon
	"cr2",
	# Adobe
	"dng",
	# EPSON
	"erf",
	# Hasselblad
	"3fr", "fff",
	# Kodak
	"dcr", "k25", "kdc",
	# Leaf
	"mos",
	# Mamiya
	"mef",
	# Nikon
	"nef", "nrw",
	# Pentax
	"pef",
	# Phase One
	"iiq",
	# Sinar
	"sti",
)
RAW_TIFF_MIMES = (
	"x-dcraw", # generic
	"x-adobe-dng",
	"x-canon-cr2",
	"x-kodak-dcr", "x-kodak-k25", "x-kodak-kdc",
	"x-nikon-nef", "x-nikon-nrw",
	"x-pentax-pef",
	"x-sony-arw", "x-sony-sr2", "x-sony-srf",
)
TIFF_MAGICS = (
	b"II\x2a\x00",
	b"MM\x00\x2a",

	# BigTIFF
	b"II\x2b\x00\x08\x00\0\0",
	b"MM\x00\x2b\x00\x08\0\0",
)

type StrSeq = str | tuple[str, ...]
class FmtInfo(typing.NamedTuple):
	desc: str
	'''Description'''

	ext: StrSeq = tuple()
	'''File extensions that are just informative (e.g. filtering file lists)'''

	match: StrSeq = tuple()
	'''Extensions that aid or are needed for identification'''

	magic: bytes | tuple[bytes, ...] = tuple()
	'''Byte sequence(s) that must match exactly'''

	mask: tuple[bytes, ...] = tuple()
	'''An AND mask plus magic sequence. Only set bits need to match.
	Even terms are masks, odd terms are signatures.'''

	size: int | tuple[int, ...] = tuple()
	'''File size, for formats with constant size'''

	mime: StrSeq = tuple()
	'''MIME types, with "image/" prefix omitted'''

type DecFmt = dict[str, FmtInfo]
type DecMap = dict[str, DecFmt]
DEC_MAP: DecMap = {
	# Simple raw formats, implemented in auto.c
	"auto": {
		"aipd": FmtInfo("National Instruments AIPD (uncertain color interpretation)",
			ext="apd",
			magic=b"AIPD"
		),
		"amibios": FmtInfo("AMI BIOS Logo",
			ext="grf",
			magic=b"GRFX"
		),
		"avs": FmtInfo("Stardent AVS X",
			match=(
				"avs",
				"mbfavs",
				"x",
			)
		),
		"bob": FmtInfo("Bob raytracer raster",
			match="bob"
		),
		"bru": FmtInfo("Degas Brush",
			match="bru",
			size=64
		),
		"chky": FmtInfo("IFF Chunky",
			ext="ciff",
			mask=(
				b"\xff\xff\xff\xff" b"\0\0\0\0" b"\xff\xff\xff\xff",
				b"FORM" b"\0\0\0\0" b"CHKY",
			)
		),
		"ckiss": FmtInfo("Cherry KiSS CEL",
			ext="cel",
			magic=b"KiSS\x20\x20"
		),
		"farbfeld": FmtInfo("farbfeld",
			ext="ff",
			magic=b"farbfeld"
		),
		"gemview": FmtInfo("GEM View-Dither",
			ext="dit",
			magic=b"B&W256"
		),
		"hir": FmtInfo("Print-Technik Raw",
			ext="hir",
			mask=(
				b"\xff\xff\xff\xff" b"\0\0\0\0" b"\xff\xff",
				b"\x0f\x0f\x00\x01" b"\0\0\0\0" b"\x00\x01",
			)
		),
		"hpicon": FmtInfo("HP Palmtop Icon",
			ext="icn",
			magic=
				# 0x002c and 0x0020 are the image dimensions, but
				# they're always the same so match against them too
				b"\x01\x00\x01\x00\x2c\x00\x20\x00"
		),
		"iim": FmtInfo("InShape IIM",
			ext="iim",
			magic=b"IS_IMAGE\0"
		),
		"kips": FmtInfo("IBM KIPS (no palette)",
			ext="kps",
			magic=b"DFIMAG00"
		),
		"kro": FmtInfo("Kolor Raw",
			ext="kro",
			magic=b"KRO\x01"
		),
		"megapat": FmtInfo("MegaPaint Pattern",
			ext="pat",
			mask=(
				b"\xff\xff\xff\xff" b"\xff\0\xff\0",
				b"\x07PAT" b" \0.\0"
			),
			size=0x112c
		),
		"olpc565": FmtInfo("OLPC 565 boot graphic",
			ext="565",
			magic=b"C565"
		),
		"ota": FmtInfo("Over The Air bitmap (uncompliant)",
			match="otb",
			mask=(
				# u8 dims
				b"\xff\0\0\xff",
				b"\x00\0\0\x01",
				# u16 dims
				b"\xff\0\0\0\0\xff",
				b"\x10\0\0\0\0\x01",
			)
		),
		"piccel": FmtInfo("Autodesk Animator PIC/CEL",
			ext=("pic", "cel"),
			magic=b"\x19\x91"
		),
		"pictris": FmtInfo("Pictris",
			ext="pic",
			magic=b"$PICTURE FOR PICTRIS (c) by Kai Lemke",
			size=0xfd25
		),
		"pgf": FmtInfo("Portfolio Graphics uncompressed",
			ext="pgf",
			size=0x780
		),
		"pxy": FmtInfo("Eclipse Proxy",
			ext="pxy",
			magic=b"\xaf\xcb"
		),
		"pzl": FmtInfo("X11 Puzzle",
			match=("cm", "pzl"),
		),
		"qdv": FmtInfo("Giffer QDV",
			match="qdv",
		),
		"seuck": FmtInfo("Seuck Font",
			ext="g",
			magic=b"\x42\0",
			size=514,
		),

		# Atari Falcon True Color family
		"coke": FmtInfo("COKE (Atari Falcon)",
			ext="tg1",
			magic=b"COKE format."
		),
		"eggpaint": FmtInfo("EggPaint",
			ext="trp",
			magic=b"TRUP"
		),
		"ftc": FmtInfo("Falcon True Color",
			match="ftc",
			size=0x2d000
		),
		"god": FmtInfo("GodPaint",
			match="god"
		),
		"indy": FmtInfo("IndyPaint",
			ext=("hgr", "tru"),
			magic=b"Indy"
		),
		"tcp": FmtInfo("Atari Rembrandt",
			ext="tcp",
			magic=b"TRUECOLR"
		),
		"trp": FmtInfo("Spooky Sprites uncompressed",
			ext=("trp", "tru"),
			magic=b"tru?"
		),

		# Atari ST
		"da4": FmtInfo("PaintShop (Atari ST)",
			match="da4",
			size=0xfa00
		),
		"doo": FmtInfo("Atari Doodle",
			match="doo",
			size=0x7d00
		),
		"imgscan": FmtInfo("IMG Scan",
			match=(
				"rwl",
				"rwh"
				# "raw" conflicts with raw camera formats, and
				# those are probably more common
			),
			size=(64000, 256000, 128000)
		),

		# TRS-80
		"trs80clp": FmtInfo("TRS-80 Clip Art",
			ext="clp",
			magic=
				b"\x00\x00\x00\x03" b"\x01\x5e\x00\x00"
				b"\x20\x00\x20\x01" b"\x01\x2c\x00\x0a"
				b"\x00\x38\x00\x20" b"\x00\x38\x00\x20\x05",
			size=0x132
		),
		"trs80hr": FmtInfo("TRS-80 High Resolution",
			match="hr",
			size=(0x4b00, 0x4b80, 0x4c00)
		),
		"trs80max": FmtInfo("TRS-80 MAX",
			ext=("grf", "max", "p41", "pix"),
			mask=(
				b"\xff\xff\xfe\xff\xff",
				b"\x00\x18\x00\x0e\x00",
			),
			size=(0x1c00, 0x1880, 0x180b, 0x180a)
		),
	},

	# Formats implemented in lib/
	"aliaspix": {
		"aliaspix": FmtInfo("AliasPIX and Vivid",
			match=("als", "img", "lux", "pix"),
			mask=(
				# match 8- and 24-bits
				b"\0\0\0\0\0\0\0\0" b"\xff\xef",
				b"\0\0\0\0\0\0\0\0" b"\x00\x08",
			)
		),
	},

	"ant": {
		"ant": FmtInfo("Studio e.Go ANT Image",
			ext="ant",
			magic=b"ANTI\x10\0\0\0",
		),
	},

	"atari": {
		"crg": FmtInfo("Calamus Raster Graphic",
			ext=("crg", "img"),
			magic=b"CALAMUSCRG",
		),

		"dali": FmtInfo("Dali uncompressed",
			match=("sd0", "sd1", "sd2"),
			magic=b"\0\0\0\0",
			size=0x7d80
		),

		"degas": FmtInfo("DEGAS, DEGAS Elite, PaintPro",
			ext="pic", # PaintPro
			match=(
				"pi1", "pi2", "pi3",
				"pc1", "pc2", "pc3",
				# Uncompressed high-resolution GFA raytrace
				"suh",
			),
			mask=(
				b"\x7f\xfe", b"\0\0", # Low and Medium res
				b"\x7f\xff", b"\0\x02", # High res
			),
			size=(0x7d22, 0x7d42)
		),

		"ez": FmtInfo("EZ-Art Professional",
			ext="eza",
			magic=b"EZ\0\xc8"
		),

		"gfa": FmtInfo("GFA Raytrace",
			ext=("sah", "sal", "sch", "scl", "sul"),
			mask=(
				# "sah", "sal"
				b"\xff\xff\xfb\xff\xff" b"\xff\xff\xff\xf0" b"\xff\xff\xff\xf0",
				b"sah\r\n" b"\0\0\0\0" b"\0\0\0\0",
				# "sch", "scl"
				b"\xff\xff\xfb\xff\xff" b"\xf0\xff\xff",
				b"sch\r\n" b"0\r\n",
				# "sul"
				b"\xff\xff\xff\xff\xff" b"\xf0\xff\xff",
				b"sul\r\n" b"0\r\n",
			)
		),

		"bld": FmtInfo("MegaPaint",
			match="bld"
		),

		"spu": FmtInfo("Spectrum 512 Uncompressed (3/4/5-bits)",
			match="spu",
			magic=b"5BIT",
			size=0xc7a0
		),

		"stad": FmtInfo("STAD PAC, Arabesque",
			ext=(
				"pac",        # STAD PAC
				"abm", "puf"  # Arabesque
			),
			magic=(
				b"pM85", b"pM86", # STAD PAC
				b"ESO88b",        # Arabesque
			),
			mask=(
				# match Arabesque "ESO88a" and "ESO89a"
				b"\xff\xff\xff\xff\xfe\xff", b"ESO88a",
			)
		),

		"tiny": FmtInfo("Tiny Stuff",
			match=(
				"tny",
				"tn1", "tn2", "tn3",
				"tn4", "tn5", "tn6",
			),
			mask=(
				b"\xfc", b"\x00", # Resolution 0-3
				b"\xfe", b"\x04", # Resolution 4-5
			)
		),
	},

	"bethesda": {
		"bsi": FmtInfo("Bethesda BSI texture (IFHD, BSIF)",
			ext="bsi",
			magic=(b"IFHD\0\0\0\x2c", b"BSIF\0\0\0\0BHDR")
		),
		"fnhd": FmtInfo("Bethesda font (FNHD)",
			ext="fnt",
			magic=b"FNHD\0\0\0\x38",
		),
		"gxa": FmtInfo("Bethesda GXA image (BMHD)",
			ext=("bmp", "gxa"),
			magic=b"BMHD\0\0\0\x22"
		),
	},

	"bmz": {
		"bmz": FmtInfo("GSD engine compressed BMP. Requires DIB",
			ext="bmz",
			magic=b"ZLC3"
		),
	},

	"c": {
		"c": FmtInfo("C code formats: DEGAS Elite Icon, XBM X10 and X11",
			ext=("icn", "icon"),
			match="xbm",
			magic=(
				b"/*",
				b"#define ",
			),
			mime=("xbm", "x-xbitmap")
		),
	},

	"c64": {
		"c64": FmtInfo("Hires and Multicolor formats"
				": AFLI editor"
				", Art Studio (OCP), Artist64, Blazing Paddles"
				", CDU-Paint, Cheese, Create With Garfield"
				", Doodle (raw & compressed), Faces Painter"
				", FLI Designer"
				", Hi-Eddi, HiPic Creator, HiRes Editor"
				", Hires FLI (HFC)"
				", Image System, Interpaint"
				", KoalaPainter (raw & compressed)"
				", Paint Magic"
				", Picasso 64, Runpaint, Saracen Paint"
				", Vidcom 64, probably others by accident",
			ext=(
				# Extensions too generic to be of any use
				# Art Studio
				"art",
				# Blazing Paddles
				"pi",
			),
			match=(
				# AFLI editor
				"afl",
				# Art Studio
				"aas", "hcp", "hpi", "ocp", "shp",
				# Wigmore Artist64
				"a64", "wig",
				# Blazing Paddles
				"bp", "bpl",
				# CDU-Paint
				"cdu",
				# Cheese
				"che",
				# Create With Garfield
				"cwg",
				# Doodle
				"dd", "ddl", "jj",
				# Faces Painter
				"fcp", "fcs", "fpt",
				# FLI Designer
				"fd2", "fli",
				# Hi-Eddi
				"hed",
				# HiPic Creator
				"hpc",
				# HiRes Editor
				"het",
				# Hires FLI
				"hfc",
				# Image System
				"ims", "ish", "ism",
				# Interpaint
				"ip64h", "iph", "ipt",
				# KoalaPainter
				"gg", "gig", "kla", "koa", "koala",
				# Paint Magic
				"pmg",
				# Picasso 64
				"p64",
				# Runpaint
				"rpm",
				# Saracen Paint
				"sar",
				# Vidcom 64
				"vid",
				# Generic C64, according to FileFormat Wiki
				"vic",
			),
			magic=(
				# Picasso 64
				b"\x00\x18",
				# Art Studio, HiEddi
				b"\x00\x20",
				# Image System, FLI Designer
				b"\x00\x3c",
				# Paint Magic
				b"\x8e\x3f",
				# Interpaint, Image System, Wigmore Artist64,
				# FacesPainter, AFLI editor, Hires FLI
				b"\x00\x40",
				# Vidcom 64
				b"\x00\x58",
				# HiPic, KoalaPainter, Runpaint
				b"\x00\x60",
				# Saracen Paint
				b"\x00\x78",
				# Blazing Paddles
				b"\x00\xa0",
				# CDU-Paint
				b"\xef\x7e",
				# Cheese
				b"\x00\x80",
			),
			mask=(
				# Doodle (0x1c00 and 0x5c00)
				b"\xff\xbf", b"\x00\x1c",
			),
			size=(
				9002, 9003, 9009, # Art Studio
				9026, 9217, 9346, # Doodle
				9194, # Hi-Eddi
				9218, # Doodle, Hi-Eddi
				9332, # Paint Magic
				10001, 10003, 10004, 10006, 10007, # KoalaPainter
				10018, # Advanced Art Studio
				10050, # Picasso 64, Vidcom 64
				10218, # Image System
				10219, # Saracen Paint
				10242, # Artist64, Blazing Paddles,
				10277, # CDU-Paint
				16385, # AFLI editor
				16386, # Hires FLI
				17218, 17409, # FLI Designer
				20482, # Cheese
			)
		),
	},

	"cbg": {
		"cbg": FmtInfo("BGI/Ethornell CompressedBG (v1)",
			magic=b"CompressedBG___\0",
		),
	},

	"chunsoft": {
		"at6p": FmtInfo("999 AT6P",
			ext="dat",
			magic=b"AT6P"
		),

		"sir0": FmtInfo("999 SIR0 sprites",
			ext="dat",
			magic=b"SIR0"
		),
	},

	"cisrle": {
		"cisrle": FmtInfo("CompuServe RLE (CompuServe Information Service)",
			ext="rle",
			magic=(b"\x1bGH", b"\x1bGM")
		),
	},

	"croteam": {
		"tbn": FmtInfo("Croteam Texture",
			ext=("tbn", "tex"),
			magic=b"TVER\x04\0\0\0TDAT"
		),
	},

	"dib": {
		"bmp": FmtInfo("Microsoft Bitmap, Jigsaw Puzzle image",
			ext=("dt", "bmp", "bmp24", "jig"),
			magic=(b"BM", b"JG"),
			mime=("bmp", "x-bmp")
		),

		"dib": FmtInfo("Microsoft DIB",
			match="dib",
			mime="x-ms-bmp"
		),

		"ico": FmtInfo("Microsoft Icon",
			match=("cur", "ico"),
			magic=(
				b"\0\0\x01\0", # Icon
				b"\0\0\x02\0", # Cursor
			),
			mime=("vnd.microsoft.icon", "x-icon")
		),
	},

	"dpx": {
		"dpx": FmtInfo("Digital Picture Exchange",
			ext="dpx",
			mask=(
				b"\xff\xff\xff\xff" b"\0\0\0\0" b"\xff\x00\xff\xff",
					b"XPDS\0\0\0\0V\0.0",
				b"\xff\xff\xff\xff" b"\0\0\0\0" b"\xff\x00\xff\xff",
					b"SDPX\0\0\0\0V\0.0",
			),
			mime="dpx"
		),
	},

	"dvm": {
		"dvm": FmtInfo("Magic Software DVM",
			ext="dvm",
			magic=b"DVM",
		),
	},

	"ea": {
		"eafnt": FmtInfo("Electronic Arts Fonts (FNTF, FNTS, FNTI)",
			ext=("ffn", "sfn"),
			mask=(
				# Middle letters can be upper or lowercase
				b"\xff\xdf\xdf\xff",
				b"FNTF",
				b"\xff\xdf\xdf\xff",
				b"FNTI",
				b"\xff\xdf\xdf\xff",
				b"FNTS",
			),
		),
	},

	"eclipse": {
		"eclipse": FmtInfo("Eclipse TILE",
			ext=("tile", "tmsk"),
			mask=(b"\xff\xff\xff\xfe", b"\x07\x28\x00\x00")
		),
	},

	"elecbyte": {
		"eb_fnt": FmtInfo("Elecbyte M.U.G.E.N. Font. Requires PCX",
			ext="fnt",
			magic=b"ElecbyteFnt\0"
		),

		"eb_sff": FmtInfo("Elecbyte M.U.G.E.N. Sprite. PCX and PNG recommended",
			ext="sff",
			magic=b"ElecbyteSpr\0"
		),
	},

	"fax": {
		"apf": FmtInfo("Async Professional Fax",
			ext="apf",
			magic=b"APF10\x1a",
		),
		"faxx": FmtInfo("IFF-FAXX, GPFax (FAX3)",
			ext=("fax", "faxx"),
			mask=(
				b"\xff\xff\xff\xff" b"\0\0\0\0" b"\xff\xff\xff\xff",
				b"FORM" b"\0\0\0\0" b"FAXX",

				b"\xff\xff\xff\xff" b"\0\0\0\0" b"\xff\xff\xff\xff",
				b"FORM" b"\0\0\0\0" b"FAX3",
			),
		),
		"g3": FmtInfo("Raw Group3/T.4 One-dimensional MSB stream",
			match=("fax", "g3"),
		),
		"qfx": FmtInfo("Quick Link II fax",
			ext=("cph", "cpn", "fax", "qfx"),
			magic=b"QLIIFAX ",
		),
		"zyxel": FmtInfo("ZyXEL fax",
			ext="fax",
			magic=b"ZyXEL\0\x02\0",
		),
	},

	"g00": {
		"g00": FmtInfo("RealLive engine G00",
			match="g00"
		),
	},

	"gp4": {
		"gp4": FmtInfo("elf AI5 engine GP4",
			match="gp4"
		),
	},

	"hel": {
		"hel": FmtInfo("Herahera Animation (へらへらアニメ, HEL)",
			ext="hel",
			magic=b"he1\0" b"\x01\0\0\0"
		),
	},

	"hg3": {
		"hg3": FmtInfo("CatSystem engine image",
			ext="hg3",
			magic=b"HG-3"
		),
	},

	"ilbm": {
		"ilbm": FmtInfo("Interleaved Bitmap & Co.: "
			"ILBM, PBM, ACBM, RGB8, RGBN, Mean Streets MLDF, "
			"Command Simulations little-endian ILBM",
			ext=(
				"iff", "ilbm", "lbm",
				"bl1", "bl2", "bl3",
				"acbm",
				"mld", "bru" # MLDF
			),
			mask=(
				b"\xff\xff\xff\xff" b"\0\0\0\0" b"\xff\xff\xff\xff",
					b"FORM\0\0\0\0ACBM",
				b"\xff\xff\xff\xff" b"\0\0\0\0" b"\xff\xff\xff\xff",
					b"FORM\0\0\0\0ILBM",
				b"\xff\xff\xff\xff" b"\0\0\0\0" b"\xff\xff\xff\xff",
					b"FORM\0\0\0\0MLDF",
				b"\xff\xff\xff\xff" b"\0\0\0\0" b"\xff\xff\xff\xff",
					b"FORM\0\0\0\0PBM ",
				b"\xff\xff\xff\xff" b"\0\0\0\0" b"\xff\xff\xff\xff",
					b"FORM\0\0\0\0RGB8",
				b"\xff\xff\xff\xff" b"\0\0\0\0" b"\xff\xff\xff\xff",
					b"FORM\0\0\0\0RGBN",
				# Command Simulations
				b"\xff\xff\xff\xff" b"\0\0\0\0" b"\xff\xff\xff\xff",
					b"MROF\0\0\0\0MBLI",
			),
			mime="x-ilbm"
		),
	},

	"jam": {
		"jam": FmtInfo("Aladdin JAM format",
			ext="jam",
			magic=b"XCOM"
		),
	},

	"kyg": {
		"kyg": FmtInfo("Kyss graphics format (KYG)",
			ext="kyg",
			magic=b"KYGformat ver.0.10\x0d\x0a"
		),
	},

	"lwi": {
		"lwi": FmtInfo("LightWork Image",
			ext="lwi",
			magic=b"\x18\x31Copyright"
		),
	},

	"mac": {
		"mac": FmtInfo("MacPaint",
			match=("mac", "pntg"),
			mime="x-macpaint"
		),
	},

	"mag": {
		"mag": FmtInfo("MAG (MAKI02)",
			ext=("mag", "max"),
			magic=b"MAKI02  "
		),
	},

	"maki": {
		"maki": FmtInfo("MAKIchan (MAKI01)",
			ext="mki",
			magic=(b"MAKI01A ", b"MAKI01B "),
		),
	},

	"mgx": {
		"mgxicn": FmtInfo("Micrografx ICN",
			ext="icn",
			magic=b"ZZZZ",
		),
	},

	"msx": {
		"msx": FmtInfo("MSX-BASIC dump, Graph Saurus",
			# You can sort of tell whether a file is an MSX-BASIC format,
			# but you can rarely tell the screen mode it uses without the
			# extension.
			match=(
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
			magic=(
				b"\xfe\0\0\x00\x6a\0\0", # Graph saurus SR5
			),
			mask=(
				# MSX uncompressed
				b"\xff\xff\xff\0\0\xff\xff", b"\xfe\0\0\0\0\0\0",
				# MSX compressed
				b"\xff\xff\xff\0\0\xff\xff", b"\xfd\0\0\0\0\0\0",
			)
		),
	},

	"nokia": {
		"nlm": FmtInfo("Nokia Logo Manager",
			ext="nlm",
			mask=(
				# Sixth byte is version, 0 to 3
				b"\xff\xff\xff\xff\xff\xfc", b"NLM \x01\x00",
			)
		),
		"nol": FmtInfo("Nokia Operator Logo and Nokia Group Graphics (NOL/NGG)",
			ext=("ngg", "no", "nol"),
			magic=(
				b"NGG\0\x01\x00",
				b"NOL\0\x01\x00",
			)
		),
		"npm": FmtInfo("Nokia Picture Message",
			ext="npm",
			magic=b"NPM\0"
		),
		"nsl": FmtInfo("Nokia Startup Logo",
			ext="nsl",
			mask=(
				b"\xff\xff\xff\xff" b"\0\0" b"\xff\xff\xff\xff",
				b"FORM" b"\0\0" b"VERS",
			)
		),
	},

	"pc98": {
		"prs": FmtInfo("Kirara/IDES PRS"
			", Micro Cabin PRS (Kimagure Orange Road)",
			match="prs",
		),
		"gpc": FmtInfo("IDES GPC (Fairytale, Cocktail)",
			ext="gpc",
			magic=b"PC98)GPCFILE   \0"
		),
		"clm": FmtInfo("IDES thumbnail? (no palette support)",
			match="clm"
		),
	},

	"pcf": {
		"pcf": FmtInfo("PCF bitmap font",
			ext="pcf",
			magic=b"\1fcp"
		),
	},

	"pcx": {
		"dcx": FmtInfo("Multi-image PCX",
			ext="dcx",
			magic=b"\xb1\x68\xde\x3a",
			mime="x-dcx"
		),

		"pcx": FmtInfo("PC Paintbrush PCX (all versions, plus CGA mode)"
			", Word for DOS screen capture",
			ext=(
				"pcc", "pcx",
				"mwg", "scr", # Word for DOS
			),
			mask=(
				# Second byte is version. Valid values are
				# 0,2,3,4,5. (If 1 were a valid version, we
				# could get away with just two masks. This
				# damned format truly screws with us at every
				# possible turn.)
				# Third byte is compression. Almost always 1,
				# rarely 0.
				b"\xff\xff\xfe", b"\x0a\x00\x00", # 0
				b"\xff\xfe\xfe", b"\x0a\x02\x00", # 2,3
				b"\xff\xfe\xfe", b"\x0a\x04\x00", # 4,5
			),
			magic=(
				# Word for DOS variant. Version and compression
				# are assumed to be constant
				b"\xcd\x05\x01",
			),
			mime=("vnd.zbrush.pcx", "x-pcx")
		),
	},

	"pdt": {
		"pdt": FmtInfo("RealLive engine PDT10 and PDT11",
			ext="pdt",
			mask=(
				# Match "PDT10" and "PDT11"
				b"\xff\xff\xff\xff\xfe\xff\xff\xff", b"PDT10\x00\x00\x00",
			)
		),
	},

	"pgx": {
		"pgx": FmtInfo("Glib2 engine image",
			ext="pgx",
			magic=b"PGX\0"
		),
	},

	"pi": {
		"pi": FmtInfo("Yanagisawa's Pi, Excellents truncated Pi (.g, .lsp)",
			ext="pi",
			match=("g", "lsp"),
			magic=b"Pi"
		),
		"dpc": FmtInfo("Excellents Yuuguri DPC image and palette",
			match="dpc",
		),
	},

	"pic": {
		"pic": FmtInfo("Yanagisawa's PIC",
			ext="pic",
			magic=b"PIC"
		),
	},

	"pic2": {
		"pic2": FmtInfo("Yanagisawa's PIC2",
			ext="p2",
			magic=b"P2DT"
		),
	},

	"pictor": {
		"pictor": FmtInfo("PCPaint PICtor",
			ext="pic",
			magic=b"\x34\x12"
		),
	},

	"pmg": {
		"pmg": FmtInfo("Print Magic Graphic (PMGRAF)",
			ext="pmg",
			magic=b"PMGRAF",
		),
	},

	"pnm": {
		"pnm": FmtInfo("PNM extended family (PNM, PAM, PFM, PHM, Xv Thumbnail, MTV, JPEG2000-PGX)",
			ext=(
				"pbm", "pgm", "ppm", "pam", "pnm",
				"pfm", "phm",
				"p7",
				"pgx"
			),
			match="mtv",
			mask=(
				# Second byte is ASCII version.
				b"\xff\xff", b"P1",
				b"\xff\xfe", b"P2", # '2', '3'
				b"\xff\xfe", b"P4", # '4', '5'
				b"\xff\xff", b"P6",

				b"\xff\xdf", b"PF", # 'F', 'f' (color/gray PFM)
				b"\xff\xdf", b"PH", # 'H', 'h' (color/gray PHM)
			),
			magic=(
				b"P7\n", # PAM
				b"P7 332\n", # Xv thumbnail

				# JPEG2000 PGX
				b"PG ML ",
				b"PG LM ",
			),
			mime=(
				"x-portable-bitmap",       # PBM
				"x-portable-graymap",      # Text PGM
				"x-portable-greymap",      # Raw PGM. blame `file` for the spellings
				"x-portable-pixmap",       # PPM
				"x-portable-arbitrarymap", # PAM
				"x-portable-anymap",       # PNM
				"x-xv-thumbnail",          # Xv
			)
		),
	},

	"prt": {
		"prt": FmtInfo("Kid engine image",
			ext=("cps", "prt"),
			magic=b"PRT\0"
		),
	},

	"px": {
		"px": FmtInfo("Leaf engine image",
			match="px"
		),
	},

	"q4": {
		"q4": FmtInfo("MAJYO's Q4 (XLD4)",
			# This format has a magic sequence, but until we bump
			# the signature limit to 16, it's not very useful, so
			# match on extension
			match="q4",
			mask=(
				b"\xff\0\0\0" b"\0\0\0\0" b"\0\0\0" b"\xff\xff\xff\xff\xff",
				b"\x1a\0\0\0" b"\0\0\0\0" b"\0\0\0" b"MAJYO",
			)
		),
	},

	"qoi": {
		"qoi": FmtInfo("QuiteOK image",
			ext="qoi",
			magic=b"qoif",
			mime="qoi"
		),
	},

	"quake": {
		"idsp": FmtInfo("id Software Sprite"
			" (Quake, Half-Life, and 32-bit variants,"
			" only first image of each animation group)",
			ext=("spr", "spr32"),
			magic=b"IDSP"
		),
		"lmp": FmtInfo("Quake LMP",
			match="lmp"
		),
	},

	"sgf": {
		"sgf": FmtInfo("Somera Graphic Format",
			ext="sgf",
			mask=(
				b"\0" + b"\xff"*0x35,

				b"\0SoMERA GRaPHIc "
				b"FORMAT r10 - by "
				b"T.Pomar a.k.a. S"
				b"obakus",
			)
		),
	},

	"sgi": {
		"sgi": FmtInfo("Silicon Graphics Image",
			ext=("bw", "rgb", "rgba", "sgi"),
			magic=b"\x01\xda",
			mime="x-sgi"
		),
	},

	"siff": {
		"pim": FmtInfo("SIFF PIM sprite and animation (first frame only)",
			ext=("pan", "pim"),
			mask=(
				b"\xff\xff\xff\xff" b"\0\0\0\0" b"\xff\xff\xff\xff",
				b"SIFF\0\0\0\0PXAN",
			)
		),
	},

	"signum": {
		"imc": FmtInfo("Signum! IMC",
			ext="imc",
			magic=b"bimc0002"
		),
	},

	"sixel": {
		"sixel": FmtInfo("SIXEL terminal graphics",
			match=("six", "sixel"),
			magic=(
				# SIXEL files may contain arbitrary terminal
				# sequences, so detection is not foolproof.
				b"\x90",  # 8-bit espace sequence
				b"\x1bP", # 7-bit escape sequence
			)
		),
	},

	"skyroads": {
		"skyroads": FmtInfo("SkyRoads LZS graphics",
			ext="lzs",
			magic=b"CMAP"
		),
	},

	"spooky": {
		"tre": FmtInfo("Spooky Sprites Run-Length Encoded",
			ext=("dta", "tre"),
			magic=b"tre1"
		),

		"trs": FmtInfo("Spooky Sprites sprite",
			ext="trs",
			magic=b"TCSF"
		),
	},

	"sun": {
		"sun": FmtInfo("Sun Raster",
			ext=(
				"im1", "im4", "im8", "im24", "im32",
				"ras", "sun"
			),
			magic=b"\x59\xa6\x6a\x95",
			mime="x-sun-raster"
		),
	},

	"tga": {
		"tga": FmtInfo("Truevision TGA (TARGA)",
			match="tga",
			# Depending on the version, TGA has a signature... at the end
			mask=(
				b"\x00\xfe\xf6",
				b"\x00\x00\x00",
			),
			mime="x-tga"
		),
	},

	"tim": {
		"tim": FmtInfo("PlayStation image, multiple palettes",
			match="tim",
			mask=(
				b"\xff\xff\xff\xff" b"\xf0\xff\xff\xff",
				b"\x10\x00\x00\x00" b"\x00\x00\x00\x00",
			),
			mime="x-sony-tim"
		),
	},
	"tim2": {
		"tim2": FmtInfo("PlayStation 2 TIM2",
			ext=("tim2", "tm2", "clt2"),
			magic=(b"TIM2", b"CLT2"),
		),
	},
	"tlg": {
		"tlg": FmtInfo("KiriKiri engine image (v5)",
			ext="tlg",
			magic=(
				b"TLG5.0\x00raw\x1a",
				# Not supported
				#b"TLG6.0\x00raw\x1a",
				#b"TLG0.0\x00sds\x1a",
			)
		),
	},

	"txf": {
		"txf": FmtInfo("TexFont Texture Mapped Font",
			ext="txf",
			magic=b"\xfftxf"
		),
	},

	"utahrle": {
		"utahrle": FmtInfo("Utah RLE",
			ext="rle",
			magic=b"\x52\xcc"
		),
	},

	"wbmp": {
		"wbmp": FmtInfo("Wireless Bitmap",
			magic=b"\0\0",
			match="wbmp",
			mime="vnd.wap.wbmp"
		),
	},

	"wgtspr": {
		"wgtspr": FmtInfo("WGT Sprite",
			ext="spr",
			mask=(
				# First byte is version.
				b"\xfc\xff\xff\xff" b"\xff\xff\xff\xff" b"\xff\xff\xff\xff" b"\xff\xff\xff",
					b"\x00\0 Sprite File ", # 0..3
				b"\xff\xff\xff\xff" b"\xff\xff\xff\xff" b"\xff\xff\xff\xff" b"\xff\xff\xff",
					b"\x04\0 Sprite File ", # 4
			)
		),
	},

	"wpx": {
		"wbm": FmtInfo("Wild-Bug engine image",
			ext="wbm",
			magic=b"WPX\x1aBMP\0"
		),

		"wia": FmtInfo("Wild-Bug engine image sequence",
			ext="wia",
			magic=b"WPX\x1aIA2\0"
		),
	},

	"xcursor": {
		"xcursor": FmtInfo("X11 cursor",
			magic=b"Xcur",
			mime="x-xcursor"
		),
	},

	"xwd": {
		"xwd": FmtInfo("X11 Window Dump",
			match=("dmp", "xwd"),
			mask=(
				# Match X10 and X11 (0x06 and 0x07)
				b"\0\0\0\0" b"\xff\xff\xff\xfe",
				b"\0\0\0\0" b"\x00\x00\x00\x06",
			),
			mime="x-xwindowdump"
		),
	},

	"xyz": {
		"xyz": FmtInfo("RPG Maker image",
			ext="xyz",
			magic=b"XYZ1"
		),
	},

	# Formats requiring external libraries
	"avif|heif": {
		"avif": FmtInfo("AV1 Image File Format",
			ext=("avif", "avifs"),
			mask=(
				# See heif definition for notes
				# avic|avis
				b"\0\0\0\x03" b"\xff\xff\xff\xff" b"\xff\xff\xff\xff",
					b"\0\0\0\0" b"ftypavif",
				b"\0\0\0\x03" b"\xff\xff\xff\xff" b"\xff\xff\xff\xef",
					b"\0\0\0\0" b"ftypavic",
				#b"\0\0\0\x03" b"\xff\xff\xff\xff" b"\xff\xff\xff\xff",
				#	b"\0\0\0\0" b"ftypavis",
			),
			mime="avif"
		),
	},

	"flif": {
		"flif": FmtInfo("Free Lossless Image Format",
			ext="flif",
			magic=b"FLIF"
		),
	},

	"gif": {
		"gif": FmtInfo("Graphics Interchange Format",
			ext=("gif", "gif87", "gif89"),
			magic=(
				b"GIF87a",
				b"GIF89a",
			),
			mime="gif"
		),
	},

	"heif": {
		"heif": FmtInfo("High Efficiency Image File Format",
			ext=(
				"heic", "heics",
				"heif", "heifs",
				"hif",
			),
			mask=(
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
			mime=("heic", "heif", "heic-sequence", "heif-sequence")
		),
	},

	"jbig": {
		"jbig": FmtInfo("JBIG",
			match=("bie", "jbg", "jbig"),
			mime="jbig"
		),
	},

	"jbig2": {
		"jbig2": FmtInfo("JBIG2",
			ext="jb2",
			magic=b"\x97JB2\x0d\x0a\x1a\x0a"
		),
	},

	"jpeg": {
		"jpeg": FmtInfo("JPEG, MPO, Esm Software PIX, Ricoh J6I",
			ext=(
				"dt2", # Microsoft Messenger
				"jfi", "jfif", "jif",
				"jpe", "jpeg", "jpg",
				"jps",
				"mpo",
				"stj", # Stereoscopic JPEG
				"thm",
				# Not sure where I got this from. Conflicts with Tiny Stuff
				"tn3",
				"pix", # ESM
				"j6i", # Ricoh J6I
			),
			magic=(
				# In a well written JPEG the third byte would be 0xff.
				# Not all JPEG files are well written.
				b"\xff\xd8",
				# ESM
				b"Esm Software PIX file\xff\xd8",
				# Ricoh
				b"\x80\x3eDSCIM\0",
			),
			mime="jpeg"
		),
	},

	"jpeg2000": {
		"j2k": FmtInfo("JPEG 2000 codestream",
			ext=("j2c", "j2k"),
			magic=b"\xff\x4f\xff\x51",
			mime="x-jp2-codestream"
		),

		"jp2": FmtInfo("JPEG 2000",
			ext=(
				"jp2", "jpc",
				"jhc", "jph", # High troughput
			),
			magic=b"\r\n\x87\n",
			mask=(
				b"\xff\xff\xff\x00" b"\xff\xff\xff\xff" b"\xff\xff\xff\xff",
					b"\0\0\0\0" b"jP\x20\x20" b"\r\n\x87\n",
			),
			mime="jp2"
		),
	},

	"jpegls": {
		"jpegls": FmtInfo("JPEG LS",
			# JPEG-LS and JPEG share the same structure, but
			# JPEG-LS uses the F7 marker. Hopefully that's always
			# at offset 2.
			ext="jls",
			magic=b"\xff\xd8\xff\xf7",
			mime="jls"
		),
	},

	"jpegxl": {
		"jpegxl": FmtInfo("JPEG XL",
			ext="jxl",
			magic=b"\xff\x0a",
			mask=(
				b"\xff\xff\xff\x00" b"\xff\xff\xff\xff" b"\xff\xff\xff\xff",
					b"\0\0\0\0" b"JXL\x20" b"\r\n\x87\n",
			),
			mime="jxl"
		),
	},

	"lerc": {
		"lerc": FmtInfo("Limited Error Raster Compression",
			ext=("lrc", "lerc", "lerc1", "lerc2"),
			magic=(
				b"CntZImage ",
				b"Lerc2 ",
			)
		),
	},

	"png": {
		"png": FmtInfo("Portable Network Graphics, Malie engine MGF",
			ext=("png", "mgf"),
			magic=(
				b"\x89PNG\r\n\x1a\n",
				b"MalieGF\0",
			),
			mime="png"
		),
	},

	"raw": {
		"raw": FmtInfo("Raw camera formats"
				": Adobe DNG"
				", Apple QuickTake"
				", Canon CR2/CR3/CRW"
				", EPSON ERF"
				", Fujifilm RAF"
				", Kodak DCR/K25/KDC"
				", Minolta MRW"
				", Nikon NEF/NRW"
				", Olympus ORF"
				", Panasonic RW2/RWL"
				", Pentax PEF"
				", Sigma X3F"
				", Sony ARW/SR2/SRF",
			ext=(
				"cr3", "crw", # Canon
				"orf", "ori", # Olympus
				"qtk", # Apple QuickTake
				"raf", # Fuji
				"rw2", "rwl", # Panasonic (it's RWL, not RW1!)
			),
			match=RAW_TIFF_EXTS + (
				"raw", # generic, GITUP
				"mrw", # Minolta MRW
			),
			magic=TIFF_MAGICS + (
				# Apple QuickTake
				b"qktk", b"qktn",
				# Canon CRW
				b"II\x1a\0",
				# Fujifilm RAF
				b"FUJIFILMCCD-RAW ",
				# Olympus ORF
				b"IIRS",
				b"IIRO",
				b"MMOR",
				# Panasonic RW/RW2
				b"IIU\0",
				# Sigma X3F
				b"FOVb",
			),
			mime=RAW_TIFF_MIMES + (
				"x-canon-cr3", "x-canon-crw",
				"x-fuji-raf",
				"x-minolta-mrw",
				"x-olympus-orf",
				"x-panasonic-rw", "x-panasonic-rw2",
				"x-sigma-x3f",
			)
		),
	},

	"svg": {
		"svg": FmtInfo("Scalable Vector Graphics",
			match=("svg", "svgz"),
			magic=(
				b"<svg ",
				b"<?xml ",
			),
			mime=("svg+xml", "svg+xml-compressed")
		),
	},

	"tiff": {
		"tiff": FmtInfo("Tag Image File Format, BigTIFF",
			ext=("g3", "g3n", "tif", "tiff") + RAW_TIFF_EXTS,
			magic=TIFF_MAGICS,
			mime=("tiff", "x-tiff-multipage") + RAW_TIFF_MIMES,
		),
	},

	"webp": {
		"webp": FmtInfo("WebP",
			ext="webp",
			mask=(
				b"\xff\xff\xff\xff" b"\0\0\0\0" b"\xff\xff\xff\xff",
					b"RIFF\0\0\0\0WEBP",
			),
			mime="webp"
		),
	},
}

# application/ mimetypes supported by libarchive
MIME_APPLICATION_MAP = (
	"vnd.rar",
	"x-7z-compressed",
	"x-archive", # .ar
	"x-cpio",
	"x-iso9660-image",
	"x-lzh-compressed",
	"x-rar-compressed", # deprecated
	"x-tar",
	"x-xar",
	"zip",

	# OpenRaster (.ora)
	"openraster",
	# Krita KRA/KRZ
	"x-krita",

	# Comic book archives
	"vnd.comicbook-rar",
	"vnd.comicbook+zip",
	# Deprecated comic book types
	"x-cbr",
	"x-cbz",
)

def foreach[T](fn: Callable[[T], typing.Any], it: Iterable[T]) -> None:
	collections.deque(map(fn, it), maxlen=0)

def eprint[T](*p: Iterable[T]) -> None:
	print(*p, file=sys.stderr)

def maskbits(b: bytes) -> int:
	return int.from_bytes(b).bit_count()

def graph_or_hex(i: int, readable: bool) -> str:
	if readable and i >= 0x20 and i < 0x80:
		return "'{}'".format(chr(i))
	return '0x{:02x}'.format(i)

def u8_array(b: str | bytes, limit: int = 0, readable: bool = False) -> str:
	if isinstance(b, str):
		b = b.encode()
	if limit and len(b) > limit:
		eprint('array exceeds length limit. will truncate:', b)
		b = b[:limit]
	return ','.join(map(lambda i: graph_or_hex(i, readable), b))

class FmtMIME(typing.NamedTuple):
	mime: str
	'''MIME type sans "image/" prefix'''

	id: int
	'''Decoder id'''

class FmtSize(typing.NamedTuple):
	size: int
	'''Expected file size'''

	id: int
	'''Decoder id'''

	@staticmethod
	def struct(bitlimit: int) -> str:
		return '''\
		struct fmt_size {{
			const uint{}_t size;
			const int id;
		}};'''.format(bitlimit)

	def declare(self) -> str:
		return '\t{{ .size={}, .id={} }},'.format(
			self.size, self.id)

class FmtMagic(typing.NamedTuple):
	mask: bytes
	'''AND mask to be applied to the file before comparison'''

	bytes: bytes
	'''Magic sequence'''

	id: int
	'''Decoder id'''

	@staticmethod
	def struct(limit: int) -> str:
		return '''\
		struct fmt_magic {{
			const unsigned char and_mask[{0}];
			const unsigned char bytes[{0}];
			const short id;
		}};'''.format(limit)

	def declare(self, limit: int) -> str:
		return '\t{{ .and_mask={{ {} }}, .bytes={{ {} }}, .id={} }},'.format(
			u8_array(self.mask, limit, False),
			u8_array(self.bytes, limit, True),
			self.id)

	def __lt__(self, other: typing.Any) -> bool:
		# Compare number of mask bits, then magic bytes
		d = maskbits(self.bytes) - maskbits(other.bytes)
		if d == 0:
			d = len(self.bytes) - len(other.bytes)
			if d == 0:
				return bool(self.bytes < other.bytes)
		return d < 0

class FmtExt(typing.NamedTuple):
	ext: str
	'''This extension'''

	id: int
	'''Decoder id, or -1 if this extension is not needed for identification'''

	@staticmethod
	def struct(limit: int) -> str:
		return '''\
		struct fmt_ext {{
			const char ext[{0}];
			const short id;
		}};'''.format(limit)

	def declare(self, ext_limit: int) -> str:
		if len(self.ext) > ext_limit:
			raise BaseException('extension exceeds length limit: ' + self.ext)
		return '\t{{ .ext={{ {} }}, .id={} }},'.format(
			u8_array(self.ext, ext_limit, True),
			self.id)

	def __lt__(self, other: typing.Any) -> bool:
		if self[0] == other[0]:
			# Reverse id order so that -1 values are last
			return bool(other[1] < self[1])
		return bool(self[0] < other[0])

def full_mask(mag: bytes, id: int) -> FmtMagic:
	if isinstance(mag, bytes):
		return FmtMagic(b'\xff' * len(mag), mag, id)
	raise BaseException('magic must be bytes sequence: ' + mag)

def mask_from_magic(info: FmtInfo, id: int) -> Iterable[FmtMagic]:
	magic = info.magic
	if isinstance(magic, bytes):
		yield full_mask(magic, id)
	elif magic:
		yield from map(lambda m: full_mask(m, id), magic)

def mask_extract(name: str, info: FmtInfo, id: int) -> Iterable[FmtMagic]:
	masks = info.mask
	if len(masks) % 2 != 0:
		raise BaseException('invalid mask sequence length in ' + name)
	for mask, mag in batched(masks, 2):
		if not isinstance(mask, bytes) or not isinstance(mag, bytes):
			raise BaseException(f'mask must be bytes sequence: {mag}')
		elif len(mask) != len(mag):
			raise BaseException(f'AND mask and magic have different lengths: {mag!r}')
		yield FmtMagic(mask, mag, id)

def ext_iter(exts: StrSeq, id: int = -1) -> Iterable[FmtExt]:
	if isinstance(exts, str):
		yield FmtExt(exts, id)
	elif exts:
		yield from map(lambda e: FmtExt(e, id), exts)

# fmt_desc is defined in dec.h
class FmtDesc(typing.NamedTuple):
	dec: str
	'''Decoder family'''

	name: str
	'''Format name'''

	info: FmtInfo

	@staticmethod
	def struct(limit: int) -> str:
		return '''\
		struct fmt_desc {{
			char name[{0}];
			const char *description;
			bool is_auto;
			union {{
				const struct image_fn *fn;
				const struct wuptr *desc;
			}} dec;
		}};'''.format(limit)

	def extern(self) -> str:
		dec, name, info = self
		is_auto = dec == 'auto'
		suffix = 'desc' if is_auto else 'fn'
		type = 'wuptr' if is_auto else 'image_fn'
		return f'extern const struct {type} {name}_{suffix};'

	def declare(self, name_limit: int) -> str:
		dec, name, info = self
		if len(name) > name_limit:
			raise BaseException('format name exceeds length limit: ' + name)
		is_auto = dec == 'auto'
		is_auto_str = 'true' if is_auto else 'false'
		suffix = 'desc' if is_auto else 'fn'
		return '''\
		{{
			.name = {{ {0} }},
			.description = "{1}",
			.is_auto = {2},
			.dec.{3} = &{4}_{3},
		}},'''.format(u8_array(name, name_limit, True),
			info.desc, is_auto_str, suffix, name)

	def get_exts(self, id: int) -> Iterable[FmtExt]:
		yield from ext_iter(self.info.ext)
		yield from ext_iter(self.info.match, id)

	def get_magics(self, id: int) -> Iterable[FmtMagic]:
		yield from mask_extract(self.name, self.info, id)
		yield from mask_from_magic(self.info, id)

	def get_sizes(self, id: int) -> Iterable[FmtSize]:
		sizes = self.info.size
		if isinstance(sizes, int):
			yield FmtSize(sizes, id)
		elif sizes:
			yield from map(lambda s: FmtSize(s, id), sizes)

	def get_mimes(self, id: int) -> Iterable[FmtMIME]:
		mime = self.info.mime
		if isinstance(mime, str):
			yield FmtMIME(mime, id)
		elif mime:
			yield from map(lambda m: FmtMIME(m, id), mime)

	def __lt__(self, other: typing.Any) -> bool:
		return bool(self.name < other.name)

def begin_map_def(name: str) -> None:
	print('static const struct fmt_{0} {0}_map[] = {{'.format(name))

def end_def() -> None:
	print('};')

def struct_and_define(type: typing.Any, limit: int) -> None:
	print(type.struct(limit))
	begin_map_def(type.__name__.replace('Fmt', '').lower())

def fmt_map_iter[T](getter: Callable[[FmtDesc, int], Iterable[T]], fmt_map: Iterable[FmtDesc]) -> Iterable[T]:
	return chain.from_iterable(starmap(lambda id, fmt: getter(fmt, id), enumerate(fmt_map)))

def minmax(min_len: int, max_len: int, n: int) -> tuple[int, int]:
	return min(min_len, n), max(max_len, n)

def print_fmt_magic(fmt_map: Iterable[FmtDesc], limit: int) -> tuple[int, int]:
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

def ext_filter(ext_map: Sequence[FmtExt], n: int, cur: FmtExt) -> bool:
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

def print_fmt_ext(fmt_map: Iterable[FmtDesc], limit: int) -> tuple[int, int]:
	ext_map = sorted(fmt_map_iter(FmtDesc.get_exts, fmt_map))

	min_len = limit
	max_len = 0
	struct_and_define(FmtExt, limit)
	for _, ext in filter(lambda t: ext_filter(ext_map, *t), enumerate(ext_map)):
		print(ext.declare(limit))
		min_len, max_len = minmax(min_len, max_len, len(ext.ext))
	end_def()
	return min_len, max_len

def print_fmt_size(fmt_map: Iterable[FmtDesc]) -> None:
	sizes = sorted(fmt_map_iter(FmtDesc.get_sizes, fmt_map), reverse=True)
	struct_and_define(FmtSize, 32)
	foreach(lambda size: print(size.declare()), sizes)
	end_def()

def print_fmt_desc(fmt_map: Iterable[FmtDesc], limit: int) -> None:
	begin_map_def('desc')
	foreach(lambda fmt: print(fmt.declare(limit)), fmt_map)
	end_def()

def print_fmt_enum(fmt_map: Iterable[FmtDesc]) -> None:
	print('enum fmt_id {')
	print('\tfmt_unknown = -1,')
	foreach(lambda fmt: print('\tfmt_', fmt.name, ',', sep=''), fmt_map)
	end_def()

def print_include(name: str) -> None:
	print('#include "', name, '"', sep='');

def gen_maps(fmt_map: Iterable[FmtDesc]) -> None:
	# Include the output of dec_header()
	print_include('dec_fn.h')
	print_include('dec_fmt_desc.h')

	# Generate `fmt_XXX` enums
	print_fmt_enum(fmt_map)
	# Generate four arrays out of the format map:
	# metadata and decoder pointers, magic sequences, extensions, and file
	# sizes.
	# These are sorted so that the program won't need initialization routines
	print_fmt_desc(fmt_map, NAME_LIMIT)
	min_mag_len, max_mag_len = print_fmt_magic(fmt_map, MAGIC_LIMIT)
	min_ext_len, max_ext_len = print_fmt_ext(fmt_map, EXT_LIMIT)
	print_fmt_size(fmt_map)

	# Print length bounds
	print(f'''
		static const size_t MIN_MAG_LEN = {min_mag_len};
		static const size_t MAX_MAG_LEN = {max_mag_len};
		static const size_t MIN_EXT_LEN = {min_ext_len};
		static const size_t MAX_EXT_LEN = {max_ext_len};'''.replace('\t', ''))

	# Include the rest of the file
	print_include('fmtmap.c')

def fmt_desc_header() -> int:
	print(FmtDesc.struct(NAME_LIMIT))
	return 0

def dec_header(fmt_map: Iterable[FmtDesc]) -> None:
	print_include('wudefs.h');
	foreach(lambda fmt: print(fmt.extern()), fmt_map)

def show_supported(fmt_map: Sequence[FmtDesc]) -> None:
	width = 1 + max(map(lambda fmt: max(len(fmt.dec), len(fmt.name)), fmt_map))
	tpl = '{:{width}}{:{width}}{}'
	print_tab = lambda t: print(tpl.format(*t, width=width))

	# Format families table
	print(len(DEC_MAP), 'families,', len(fmt_map), 'formats supported\n')

	header = tpl.format('Family', 'Format', 'Description', width=width)
	print(header)
	print('-' * (len(header) + 1))
	foreach(print_tab, sorted(
		map(lambda fmt: (fmt.dec, fmt.name, fmt.info.desc), fmt_map)
	))
	print()

	# Extensions
	exts = sorted(set(
		map(lambda f: f.ext, fmt_map_iter(FmtDesc.get_exts, fmt_map))
	))
	print('Known extensions:', len(exts))
	print(', '.join(exts), end='\n\n')

	# Magic sequences
	tpl = '{:{width}}{}'
	magics = sorted(map(lambda m: (fmt_map[m.id].name, m.bytes),
		fmt_map_iter(FmtDesc.get_magics, fmt_map)
	))
	print('Known magic sequences:', len(magics))
	foreach(print_tab, magics)
	print()

	# File sizes
	sizes = sorted(map(lambda s: (fmt_map[s.id].name, s.size),
		fmt_map_iter(FmtDesc.get_sizes, fmt_map)
	))
	print('Fixed file sizes:', len(sizes))
	foreach(print_tab, sizes)
	print()

	# MIME types
	mimes = sorted(map(lambda m: (fmt_map[m.id].name, m.mime),
		fmt_map_iter(FmtDesc.get_mimes, fmt_map)
	))
	print('MIME types:', len(mimes))
	foreach(print_tab, mimes)

def mime_fmt(type: str, it: Iterable[str]) -> str:
	return ''.join(map(lambda s: f'{type}/{s};', sorted(set(it))))

def write_desktop(outname: str, entries: Iterable[tuple[str, str]]) -> None:
	with open(outname, "w") as fp:
		print('[Desktop Entry]', file=fp)
		foreach(lambda t: print(*t, sep='=', file=fp), entries)

def gen_desktop_file(fmt_map: Iterable[FmtDesc], desktop_file: str, archive_file: str) -> None:
	image_mime = mime_fmt('image',
		map(lambda m: m.mime, fmt_map_iter(FmtDesc.get_mimes, fmt_map))
	)
	common = (
		('Categories', 'Graphics;Viewer;2DGraphics;'),
		('Icon', 'applications-graphics'),
		('Terminal', 'true'),
		('TryExec', 'wu'),
		('Type', 'Application'),
	)

	desktop = (
		('Name', 'wu'),
		('GenericName', 'Image viewer'),
		('Exec', 'wu %F'),
		('MimeType', image_mime + 'inode/directory;'),
	)
	archive = (
		('Name', 'wu archive'),
		('GenericName', 'Comic book/archive image viewer'),
		('Exec', 'wu archive %f'),
		('MimeType', mime_fmt('application', MIME_APPLICATION_MAP)),
	)

	write_desktop(desktop_file, desktop + common)
	write_desktop(archive_file, archive + common)


def print_names(map_keys: Iterable[str]) -> int:
	foreach(print, sorted(set(chain.from_iterable(
		map(lambda s: s.split('|'), filter(lambda s: s != 'auto', map_keys))
	))))
	return 0

def extract_fmt(dec: str, fmts: DecFmt) -> Iterable[FmtDesc]:
	return map(lambda t: FmtDesc(dec, *t), fmts.items())

def contains_any(contains: Callable[[str], bool], t: tuple[str, DecFmt]) -> bool:
	return any(map(contains, t[0].split('|')))

def fmts_from_decs(dec: DecMap, enabled: set[str] | None = None) -> Iterable[FmtDesc]:
	fn = partial(contains_any, enabled.__contains__ if enabled else bool)
	return chain.from_iterable(starmap(extract_fmt, filter(fn, dec.items())))

def enabled_formats() -> set[str]:
	prefix = '#define WU_ENABLE_'
	return set(
		map(lambda s: s[len(prefix):-1].lower(),
			filter(lambda s: s.startswith(prefix), sys.stdin)
		)
	)

if __name__ == '__main__':
	enabled = None
	i = 1
	if sys.argv[i] == 'names':
		sys.exit(print_names(DEC_MAP.keys()))
	elif sys.argv[i] == 'fmt_desc':
		sys.exit(fmt_desc_header())
	if sys.argv[i] == '-all':
		i += 1
	else:
		enabled = enabled_formats() | set(("auto",))

	# Filter and flatten the decoder map to get a format list.
	# After sorting, the position within the list will be the format id.
	fmt_map = sorted(fmts_from_decs(DEC_MAP, enabled))

	fn: dict[str, Callable[..., None]] = {
		'maps': gen_maps,
		'header': dec_header,
		'desktop': gen_desktop_file,
		'show': show_supported,
	}
	fn[sys.argv[i]](fmt_map, *sys.argv[i+1:])
