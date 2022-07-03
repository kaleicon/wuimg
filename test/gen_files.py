#!/usr/bin/python3
import os
import sys
from itertools import product, repeat, starmap
from subprocess import run, DEVNULL, PIPE

FFMPEG_BASE = ["ffmpeg", "-v", "quiet"]
OUT_DIR = "gen/"

def foreach(fun, iter):
	for i in iter:
		fun(i)

def outfile(name):
	return OUT_DIR + name.rpartition(".")[0]

def arg_append(cmd, out, arg):
	cmd.extend(arg)
	out.append("=".join(arg))

def get_pix_fmts(fmt):
	info = run(FFMPEG_BASE + ["-h", "encoder=" + fmt],
		stdin=DEVNULL, stderr=DEVNULL, stdout=PIPE, text=True).stdout
	if info:
		key = "Supported pixel formats:"
		start = info.find(key)
		if start != -1:
			start += len(key)
			end = info.find("\n", start)
			return info[start:end].split()
	return tuple()

def write_ok(cmd, name):
	with open(name, "ab") as out:
		if os.stat(out.fileno()).st_size == 0:
			return run(cmd, stdin=DEVNULL, stderr=DEVNULL,
				stdout=out).returncode == 0
	return None

def enc(cmd, name):
	name = ".".join(name)
	print(name, write_ok(cmd, name), sep=" : ")

def enc_ff(infile, fmt, pix_fmt, args):
	cmd = FFMPEG_BASE + ["-i", infile, "-f", "image2", "-c:v", fmt,
		"-pix_fmt", pix_fmt]
	out = [outfile(infile), "ff", pix_fmt]
	foreach(lambda a: arg_append(cmd, out, a), args)
	cmd.append("-")
	out.append("tga" if fmt == "targa" else fmt)
	enc(cmd, out)

def enc_im(file, fmt, args):
	cmd = ["convert", file]
	out = [outfile(file), "im"]
	foreach(lambda a: arg_append(cmd, out, a), args)
	cmd.append(fmt + ":-")
	out.append(fmt)
	enc(cmd, out)

def arg_iter(dc):
	if dc:
		return product(*starmap(
			lambda k, v: zip(repeat(k), map(str, v)), dc.items()))
	return ((),)

def enc_ffmpeg(tup):
	file, tup = tup
	fmt, args = tup
	foreach(lambda t: enc_ff(file, fmt, *t),
		product(get_pix_fmts(fmt), arg_iter(args)))

def enc_imagemagick(tup):
	infile, tup = tup
	fmt, args = tup
	foreach(lambda a: enc_im(infile, fmt, a), arg_iter(args))

if len(sys.argv) > 1:
	files = sys.argv[1:]
	os.makedirs(OUT_DIR, exist_ok=True)

	print("ffmpeg images")
	foreach(enc_ffmpeg, product(files, (
		("bmp", None),
		("gif", None),
		("jpeg2000", {"-format": range(0, 2)}),
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
	)))

	print("\nimagemagick images")
	foreach(enc_imagemagick, product(files, (
		("avs", None),
		("dib", None), ("bmp", None), ("bmp3", None), ("bmp2", None),
		#("flif", None),
		("gif", None), ("gif87", None),
		("heic", None), ("avif", None),
		("jbig", None),
		("jp2", None), ("j2k", None),
		("pcx", None),
		("dcx", None),
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
		("tga", {"-compress": ("none", "rle")}),
		("tif", {"-depth": range(32, 0, -1)}),
		("tif", {"-depth": range(8, 0, -1), "-type": ("Palette",)}),
		("tif", {"-depth": (8, 16, 32), "-define": ("tiff:tile-geometry=64x64",)}),
		("tif", {"-depth": (32, 64), "-define": ("quantum:format=floating-point",)}),
		("tif", {"-depth": (1,), "-type": ("Bilevel",),
			"-define": ("quantum:polarity=min-is-white", "quantum:polarity=min-is-black")}),
		("wbmp", None),
		("xbm", None),
	)))
else:
	print("Usage:", sys.argv[0], "INPUT_FILE [...]")
