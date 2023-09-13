#!/usr/bin/python3
# SPDX-License-Identifier: 0BSD
import re
import os
import sys
import argparse
from itertools import chain, product, repeat, starmap
from functools import partial, reduce
from subprocess import run, Popen, DEVNULL, PIPE

FFMPEG_BASE = ["ffmpeg", "-v", "quiet", "-hide_banner"]
CLEAR_LINE = "\x1b[K"

def starforeach(fun, iter):
	for i in iter:
		fun(*i)

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

def write_ok(cmd, filename):
	try:
		with open(filename, "xb") as out:
			return run(cmd, stdin=DEVNULL, stderr=DEVNULL, stdout=out).returncode == 0
	except:
		return sys.exc_info()[1]

def enc(cmd, outname, arg_pairs, cmd_tail, outname_tail):
	cmd += chain.from_iterable(arg_pairs)
	cmd.append(cmd_tail)
	outname += map("=".join, arg_pairs)
	outname.append(outname_tail)

	filename = ".".join(outname)
	return filename, write_ok(cmd, filename)

def enc_im(infile, outpath, codec, arg_pairs):
	cmd = ["convert", infile]
	outname = [outpath, "im"]
	return enc(cmd, outname, arg_pairs, codec + ":-", codec)

def enc_ff(infile, outpath, codec, pix_fmt, arg_pairs):
	cmd = FFMPEG_BASE + ["-i", infile, "-f", "image2", "-c:v", codec,
		"-pix_fmt", pix_fmt]
	outname = [outpath, "ff", pix_fmt]
	out_tail = "tga" if codec == "targa" else codec
	return enc(cmd, outname, arg_pairs, "-", out_tail)

def encode_imagemagick(infile, outpath, codec, flags):
	fun = partial(enc_im, infile, outpath, codec)
	return map(fun, arg_product(flags))

def encode_ffmpeg(infile, outpath, codec, flags):
	fun = partial(enc_ff, infile, outpath, codec)
	args = product(get_pix_fmts(codec), arg_product(flags))
	return starmap(fun, args)

def printer(name, status):
	end = "\n"
	if status == True:
		msg = "OK"
		end = "\r"
	elif status == False:
		msg = "Encoding error"
	else:
		msg = status
	print(CLEAR_LINE, name, ":", msg, end=end, flush=True)

def encode_all(infile, outpath, items):
	prog, fun, codec_list = items
	pfun = partial(fun, infile, outpath)

	print(prog, "images")
	starforeach(printer, chain.from_iterable(starmap(pfun, codec_list)))
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
	)),
)

def generate_images(args):
	os.makedirs(args.outdir, exist_ok=True)
	outfile_base = "/".join((args.outdir, args.image.split(".")[0]))
	starforeach(encode_all, zip(repeat(args.image), repeat(outfile_base), ENCODERS))


def run_compare_ffmpeg(infile, wu):
	filter = ",".join((
		"[0:v]format=pix_fmts=gbrap16le[pipe]",
		"[1:v]format=pix_fmts=gbrap16le[infile]",
		"[pipe][infile]msad",
		"metadata=mode=print:file=-"
	))
	cmd = FFMPEG_BASE + ["-f", "pam_pipe", "-i", "-", "-i", infile,
		"-filter_complex", filter, "-f", "null", "-"]
	ff = Popen(cmd, stdin=wu.stdout, stdout=PIPE, stderr=DEVNULL)
	wu.stdout.close()
	stdout = ff.communicate()[0]
	if wu.wait() != 0:
		return "Decoding error"
	elif ff.returncode != 0:
		return "Comparison error"
	pat = b"lavfi\.msad\.msad_avg=([0-9.]+)"
	m = re.search(pat, stdout)
	if m:
		return float(m.group(1))
	return "Unexpected result: " + str(stdout)

def run_compare_imagemagick(infile, wu):
	im = Popen(("compare", "-metric", "MAE", infile, "-", "null:"),
		stdin=wu.stdout, stdout=DEVNULL, stderr=PIPE)
	wu.stdout.close()
	stderr = im.communicate()[1]
	if wu.wait() != 0:
		return "Decoding error"
	elif im.returncode == 2:
		return "Comparison error"
	return float( stderr.partition(b"(")[2].partition(b")")[0] )

def run_compare(fn, infile):
	wu_proc = Popen(("wu", "write", "-s", infile),
		stdin=DEVNULL, stderr=DEVNULL, stdout=PIPE)
	return fn(infile, wu_proc)

def compare_file(fn, basepath, file):
	infile = basepath + file
	score = run_compare(fn, infile)
	print(CLEAR_LINE, score, end="\r", flush=True)
	return infile, score

def compare_dir(fn, root, _dirs, files):
	basepath = root + "/"
	return map(partial(compare_file, fn, basepath), files)

def tup_key(tup, val=None):
	if type(tup[1]) == str:
		return val
	return tup[1]

def compare_images(args):
	fn = run_compare_ffmpeg if args.use_ffmpeg else run_compare_imagemagick
	cmp_dir = partial(compare_dir, fn)
	results = tuple(chain.from_iterable(starmap(cmp_dir, os.walk(args.dir))))

	compared = tuple(filter(lambda r: r != None, map(tup_key, results)))
	diff = sum(compared)
	total = len(compared)
	score = total - diff

	starforeach(partial(print, sep=" : "), sorted(results, key=partial(tup_key, val=2)))
	print(CLEAR_LINE)
	print("Scored", score, "out of", total, ":", score/total)
	print(len(results), "files found")
	print(total, "were compared")

if __name__ == "__main__":
	parser = argparse.ArgumentParser(description="Image decoding comparator")
	subp = parser.add_subparsers(required=True)

	gen = subp.add_parser("generate")
	gen.add_argument("image", help="Source image")
	gen.add_argument("outdir", help="Where to save the converted files")
	gen.set_defaults(func=generate_images)

	comp = subp.add_parser("compare")
	comp.add_argument("--use_ffmpeg", help="Use ffmpeg for comparisons", action="store_true")
	comp.add_argument("dir", help="Directory containing the images to compare")
	comp.set_defaults(func=compare_images)

	args = parser.parse_args()
	args.func(args)
