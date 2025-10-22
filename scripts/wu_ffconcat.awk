#!/bin/awk -f
# SPDX-License-Identifier: 0BSD
# SPDX-FileCopyrightText: 2025 kaleido
BEGIN {
	if (ARGV[1] ~ "-h|--help") {
		print "Using the output of wuconv, create an ffmpeg concat file for each animation:"
		print "\twuconv -e pam image.png | wu_ffconcat.awk"
		print
		print "Then pass each .txt file to assemble all PAM frames into video:"
		print "\tffmpeg -i image.png_00001.txt ... # your options here"
		print
		print "All in one loop:"
		print "\twuconv -e pam image.png | wu_ffconcat.awk | while read -r cc; do"
		print "\t\tffmpeg -i \"$cc\" -c:v apng \"${cc%.txt}.apng\""
		print "\tdone"
		exit
	}
	FS="[._]"
}
/_[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+\.[0-9A-z]+$/ {
	concat = substr($0, match($0, ".*_[0-9]+"), RLENGTH) ".txt"
	frame = $(NF-3)
	num = $(NF-2)
	den = $(NF-1)
	gsub("'", "\\\&", $0)
	if (frame == 0) {
		if (prev_file) {
			print prev_file
		}
		prev_file = concat
		print "ffconcat version 1.0" > concat
	}
	print "file '" $0 "'" >> concat
	print "duration", num/den >> concat
}
END {
	if (prev_file) {
		print prev_file
	}
}
