#!/usr/bin/python3
import glob
import os
import sys
from itertools import chain, product, repeat, starmap
from functools import partial, reduce
import operator
from subprocess import run, Popen, DEVNULL, PIPE

FFMPEG_BASE = ["ffmpeg", "-v", "quiet"]
OUT_DIR = "gen/"
CLEAR_LINE = "\x1b[K"

def starforeach(fun, iter):
	for i in iter:
		fun(*i)

def foreach(fun, iter):
	for i in iter:
		fun(i)

def outfile_base(name):
	return OUT_DIR + name.split(".")[0]

def get_pix_fmts(codec):
	info = run(FFMPEG_BASE + ["-h", "encoder=" + codec],
		stdin=DEVNULL, stderr=DEVNULL, stdout=PIPE, text=True).stdout
	if info:
		key = "Supported pixel formats:"
		start = info.find(key)
		if start != -1:
			start += len(key)
			end = info.find("\n", start)
			return info[start:end].split()
	return tuple()

def arg_choices(key, val_seq):
	return zip(repeat(key), map(str, val_seq))

def arg_product(codec_args):
	if codec_args:
		perms = starmap(arg_choices, codec_args.items())
		return product(*perms)
	return ((),)

def write_ok(cmd, name):
	try:
		with open(name, "xb") as out:
			return run(cmd, stdin=DEVNULL, stderr=DEVNULL, stdout=out).returncode == 0
	except FileExistsError:
		return "File exists"
	except:
		return sys.exc_info()[1]

def enc(cmd, out, arg_pairs, cmd_tail, out_tail):
	cmd += chain.from_iterable(arg_pairs)
	out += map("=".join, arg_pairs)
	cmd.append(cmd_tail)
	out.append(out_tail)

	name = ".".join(out)
	return name, write_ok(cmd, name)

def enc_im(infile, outfile, codec, arg_pairs):
	cmd = ["convert", infile]
	outname = [outfile, "im"]
	return enc(cmd, outname, arg_pairs, codec + ":-", codec)

def enc_ff(infile, outfile, codec, pix_fmt, arg_pairs):
	cmd = FFMPEG_BASE + ["-i", infile, "-f", "image2", "-c:v", codec,
		"-pix_fmt", pix_fmt]
	outname = [outfile, "ff", pix_fmt]
	out_tail = "tga" if codec == "targa" else codec
	return enc(cmd, outname, arg_pairs, "-", out_tail)

def encode_imagemagick(infile, codec, flags):
	fun = partial(enc_im, infile, outfile_base(infile), codec)
	return map(fun, arg_product(flags))

def encode_ffmpeg(infile, codec, flags):
	fun = partial(enc_ff, infile, outfile_base(infile), codec)
	args = product(get_pix_fmts(codec), arg_product(flags))
	return starmap(fun, args)

def printer(name, status):
	end = "\n"
	if status == True:
		status = "OK"
		end = "\r"
	elif status == False:
		status = "Encoding error"
	print(CLEAR_LINE, name, ":", status, end=end, flush=True)

def encode_all(infile, items):
	prog, fun, codec_list = items
	fun = partial(fun, infile)

	print(prog, "images")
	starforeach(printer, chain.from_iterable(starmap(fun, codec_list)))
	print(CLEAR_LINE)

ENCODERS = (
	("ffmpeg", encode_ffmpeg, (
		("bmp", None),
		("dpx", None),
		("gif", None),
		("jpeg2000", None),
		#("jpegls", None),
		("mjpeg", None),
		("pam", None),
		("pbm", None),
		("pgm", None),
		("ppm", None),
		("pcx", None),
		("png", None),
		("sgi", {"-rle": range(0, 2)}),
		("sunrast", {"-rle": range(0, 2)}),
		("targa", {"-rle": range(0, 2)}),
		("tiff", None),
		("webp", {"-lossless": range(0, 2)}),
		("xbm", None),
	)),

	("imagemagick", encode_imagemagick, (
		("avs", None),
		("dib", None), ("bmp", None), ("bmp3", None), ("bmp2", None), ("ico", None),
		("dpx", {"-depth": (1, 8, 16)}),
		#("dpx", {"-depth": (32,), "-define": ("quantum:format=floating-point",)}),
		#("flif", None),
		("gif", None), ("gif87", None),
		("heic", None), #("avif", None),
		("jbig", None),
		("jp2", None), ("j2k", None),
		("pcx", None), ("dcx", None),
		("pam", None),
		("pbm", {"-compress": ("undefined", "none")}),
		("pgm", {"-compress": ("undefined", "none")}),
		("ppm", {"-compress": ("undefined", "none")}),
		("pfm", {"-type": ("Grayscale", "TrueColor")}),
		("mtv", None),
		("png", None),
		("sgi", {"-compress": ("none", "rle")}),
		("sixel", None),
		("sun", None),
		#("tga", {"-compress": ("none", "rle")}),
		("tif", {"-depth": range(32, 0, -1)}),
		("tif", {"-depth": range(8, 0, -1), "-type": ("Palette",)}),
		("tif", {"-depth": (8, 16, 32), "-define": ("tiff:tile-geometry=64x64",)}),
		("tif", {"-depth": (32, 64), "-define": ("quantum:format=floating-point",)}),
		("tif", {"-depth": (1,), "-type": ("Bilevel",),
			"-define": ("quantum:polarity=min-is-white", "quantum:polarity=min-is-black")}),
		("wbmp", None),
		("xbm", None),
	)),
)


def run_compare(infile):
	wu = Popen(("wu", "write", "-s", infile),
		stdin=DEVNULL, stderr=DEVNULL, stdout=PIPE)
	com = Popen(("compare", "-metric", "PAE", infile, "-", "null:"),
		stdin=wu.stdout, stderr=PIPE, stdout=DEVNULL)
	wu.stdout.close()
	stderr = com.communicate()[1]
	if wu.wait() != 0:
		return "Decoding error"
	elif com.returncode == 2:
		return "Comparison error"
	return float( str(stderr.partition(b"(")[2].partition(b")")[0], encoding="ascii") )

def compare(dir, file):
	infile = dir + file
	score = run_compare(infile)
	print(CLEAR_LINE, score, end="\r", flush=True)
	return infile, score

def compare_dir(root, _dirs, files):
	return map(partial(compare, root + "/"), files)

def tup_key(tup, val=2):
	if type(tup[1]) == str:
		return val
	return tup[1]

def tup_sum(x, ty):
	print(*ty, sep=" : ")
	return x + tup_key(ty, 1)

def compare_count(args):
	tree = chain.from_iterable(map(os.walk, args))
	results = list(chain.from_iterable(starmap(compare_dir, tree)))

	total = len(results)
	bad = sorted(filter(tup_key, results), key=tup_key)

	diff = reduce(tup_sum, bad, 0)
	score = total - diff
	print(CLEAR_LINE)
	print("Scored", score, "out of", total, ":", score/total)
	print(len(bad), "files are not equal")


if len(sys.argv) <= 2 or sys.argv[2] == "-h":
	print("Usage:", sys.argv[0], "gen INPUT_FILE.pam [...]")
	print("Usage:", sys.argv[0], "compare DIR [...]")
else:
	args = sys.argv[2:]
	if sys.argv[1] == "gen":
		os.makedirs(OUT_DIR, exist_ok=True)
		starforeach(encode_all, product(args, ENCODERS))
	elif sys.argv[1] == "compare":
		compare_count(args)
