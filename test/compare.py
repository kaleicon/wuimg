#!/usr/bin/python3
# SPDX-License-Identifier: 0BSD
# SPDX-FileCopyrightText: 2022 kaleido
import re
import os
import sys
import argparse
from functools import partial
from subprocess import Popen, DEVNULL, PIPE

FFMPEG_BASE = ["ffmpeg", "-v", "quiet"]
CLEAR_LINE = "\x1b[K"

def starforeach(fun, iter):
	for i in iter:
		fun(*i)

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
		return "Comparison error (not supported?)"
	pat = b"lavfi.msad.msad_avg=([0-9.]+)"
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
		return "Comparison error (not supported?)"
	return float( stderr.partition(b"(")[2].partition(b")")[0] )

def run_compare(wupath, fn, infile):
	wu_proc = Popen((wupath, "write", "-s", infile),
		stdin=DEVNULL, stderr=DEVNULL, stdout=PIPE)
	return fn(infile, wu_proc)

def compare_file(wupath, show_status, fn, infile):
	score = run_compare(wupath, fn, infile)
	if show_status:
		print(CLEAR_LINE, score, end="\r", flush=True)
	return infile, score

def tup_key(tup, val=None):
	if type(tup[1]) == str:
		return val
	return tup[1]

def compare_images(args):
	is_tty = sys.stdout.isatty()
	fn = run_compare_ffmpeg if args.use_ffmpeg else run_compare_imagemagick
	cmp_file = partial(compare_file, args.exe, is_tty, fn)
	results = tuple(map(cmp_file, args.images))

	compared = tuple(filter(lambda r: r != None, map(tup_key, results)))
	diff = sum(compared)
	total = len(compared)
	score = total - diff

	similarity = score/total
	if is_tty:
		print(CLEAR_LINE)
	starforeach(partial(print, sep=" : "), sorted(results, key=partial(tup_key, val=2)))
	print("Scored", score, "out of", total, ":", similarity)
	print("Expected:", args.expect)
	print(total, "files successfully compared out of", len(results))
	sys.exit(score < args.expect)

if __name__ == "__main__":
	comp = argparse.ArgumentParser(description="wu decoder comparator")
	comp.add_argument("--expect", help="Minimum expected value", type=float, default=0)
	comp.add_argument("--use_ffmpeg", help="Use ffmpeg for comparisons", action="store_true")
	comp.add_argument("exe", help="Path to the 'wu' binary")
	comp.add_argument("images", help="Images to compare", nargs="*")
	compare_images(comp.parse_args())
