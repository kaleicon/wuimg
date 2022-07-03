#!/usr/bin/python3
import glob
import os
import sys
from itertools import chain
from subprocess import run, DEVNULL, PIPE

TEMP_DIR = "tmp/"

def foreach(fun, iter):
	for i in iter:
		fun(i)

def run_ok(cmd):
	return run(cmd, stdin=DEVNULL, stderr=DEVNULL, stdout=DEVNULL).returncode == 0

def run_compare(cmd):
	st = run(cmd, stdin=DEVNULL, stderr=PIPE, stdout=DEVNULL, text=True)
	if st.returncode == 2:
		return None
	return st.stderr

def compare(dir, file):
	infile = dir + file
	if run_ok(("wu", "write", "-f", "-o", TEMP_DIR, infile)):
		pamfile = TEMP_DIR + file + ".pam"
		out = run_compare(("compare", infile, pamfile, "-metric", "PAE", "null:"))
		if out:
			out = float(out.partition("(")[2].partition(")")[0])
			if out != 0:
				print(infile, out, sep=" : ")
		else:
			print(infile, "Failed", sep=" : ")
		foreach(os.remove, glob.iglob(TEMP_DIR + "*"))
	else:
		print("Failed to decode", infile)

def compare_recursive(tup):
	dir = tup[0] + "/"
	foreach(lambda s: compare(dir, s), tup[2])

if len(sys.argv) > 1:
	os.makedirs(TEMP_DIR, exist_ok=True)
	foreach(compare_recursive, chain.from_iterable(map(os.walk, sys.argv[1:])))
else:
	print("Usage:", sys.argv[0], "SRC_DIR [...]")
