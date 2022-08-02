#!/bin/sh
# Use dimensions < 256 so that ICO encoding succeeds
WIDTH=253 # Odd width to test alignment handling
HEIGHT=252 # Multiple of 6 so that SIXEL images are not trimmed

# Use FFmpeg for rasterization, as ImageMagick has three different SVG renderers
ffmpeg -n -width "$WIDTH" -height "$HEIGHT" -keep_ar 0 -i "$1" \
	-f image2 -c:v pam ras.pam
./tester.py gen ras.pam && ./tester.py compare gen
