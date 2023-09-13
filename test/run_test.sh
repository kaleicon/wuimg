#!/bin/sh
# SPDX-License-Identifier: 0BSD

# Dimensions should always be < 256 so that ICO encoding succeeds
WIDTH=31 # Odd width to test alignment handling
HEIGHT=30 # Multiple of 6 so that SIXEL images are not trimmed
RASFILE=,ras.pam

ffmpeg -y -width "$WIDTH" -height "$HEIGHT" -keep_ar 0 -i vec.svgz \
	-c:v pam "$RASFILE"
./tester.py generate "$RASFILE" ,gen
