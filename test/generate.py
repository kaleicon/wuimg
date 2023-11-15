#!/usr/bin/python3
# SPDX-License-Identifier: 0BSD
import os
import sys
import argparse
from itertools import chain, product, repeat, starmap
from functools import partial, reduce
from subprocess import run, DEVNULL, PIPE
from collections import deque

FFMPEG_BASE = ["ffmpeg", "-v", "quiet"]
CLEAR_LINE = "\x1b[K" if sys.stdout.isatty() else ""

def get_pix_fmts(codec):
	info = run(FFMPEG_BASE + ["-h", "encoder=" + codec],
		stdin=DEVNULL, stderr=DEVNULL, stdout=PIPE).stdout
	if info:
		key = b"Supported pixel formats:"
		start = info.find(key)
		if start != -1:
			start += len(key)
			end = info.find(b"\n", start)
			return info[start:end].decode().split()
	return tuple()

def arg_choices(key, val_seq):
	return zip(repeat(key), map(str, val_seq))

def arg_product(codec_args):
	if codec_args:
		perms = starmap(arg_choices, codec_args.items())
		return product(*perms)
	return ((),)

def enc(cmd, outname, arg_pairs, ext):
	outname += map("=".join, arg_pairs)
	outname.append(ext)

	cmd += chain.from_iterable(arg_pairs)

	filename = ".".join(outname)
	cmd.append(filename)
	return cmd

def encode_imagemagick(infile, outpath, codec, flags):
	def enc_im(arg_pairs):
		cmd = ["convert", infile]
		outname = [outpath, "im"]
		return enc(cmd, outname, arg_pairs, codec)

	return map(enc_im, arg_product(flags))

def encode_ffmpeg(infile, outpath, codec, flags):
	def enc_ff(pix_fmt, arg_pairs):
		cmd = FFMPEG_BASE + ["-i", infile, "-f", "image2", "-c:v", codec,
			"-pix_fmt", pix_fmt]
		outname = [outpath, "ff", pix_fmt]
		return enc(cmd, outname, arg_pairs, codec)

	args = product(get_pix_fmts(codec), arg_product(flags))
	return starmap(enc_ff, args)

ENCODERS = (
	(encode_ffmpeg, (
		("bmp", None),
		("dpx", None),
		("gif", None),
		("jpeg2000", None),
		#("jpegls", None),
		("jpegxl", None),
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
		("xwd", None),
	)),

	(encode_imagemagick, (
		("avif", None),
		("avs", None),
		("dib", None), ("bmp", None), ("bmp3", None), ("bmp2", None), ("ico", None),
		("dpx", {"-depth": (1, 8, 16)}),
		#("dpx", {"-depth": (32,), "-define": ("quantum:format=floating-point",)}),
		#("flif", None),
		("gif", None), ("gif87", None),
		("heic", None),
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
		("xwd", None),
	)),
)

def print_file(cmd):
	filename = cmd[-1]
	print(os.path.basename(filename), end="\0")

def conv_file(cmd):
	end="\n"
	filename = cmd[-1]
	try:
		st = run(cmd, stdin=DEVNULL, stderr=DEVNULL, stdout=DEVNULL)
		if st.returncode == 0:
			msg = "OK"
			if CLEAR_LINE:
				end = "\r"
		else:
			msg = "Encoding error"
		print(CLEAR_LINE, filename, ":", msg, end=end, flush=True)
	except FileExistsError:
		pass

def generate_images(args):
	outpath = args.image.rpartition(".")[0]
	if args.outdir:
		outpath = os.path.join(args.outdir, os.path.basename(outpath))

	def get_cmds(items):
		fun, codecs = items
		cmd_gen = partial(fun, args.image, outpath)
		return chain.from_iterable(starmap(cmd_gen, codecs))

	consume = partial(deque, maxlen=0)

	cmds = chain.from_iterable(map(get_cmds, ENCODERS))
	consume(map(print_file if args.print else conv_file, cmds))

if __name__ == "__main__":
	gen = argparse.ArgumentParser(description="wu sample generator")
	gen.add_argument("--outdir", help="Where to save the generated files")
	gen.add_argument("--print", help="Only print file paths to use", action="store_true")
	gen.add_argument("image", help="Source image")
	generate_images(gen.parse_args())
