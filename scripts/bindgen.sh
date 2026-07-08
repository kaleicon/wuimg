#!/bin/sh
# SPDX-License-Identifier: 0BSD
# SPDX-FileCopyrightText: 2026 kaleido

gen_include() {
	for i in wudefs.h misc/metadata.h; do
		echo '#include "'"$i"'"'
	done
}

case "$1" in
	''|'-h'|'-help'|'--help')
		echo 'Usage:' "$0" 'src_root'
		;;
	*)
		cd "$1"
		amalgam=$(mktemp amalgamXXXXXX.h)
		gen_include > "$amalgam"
		bindgen \
			--no-prepend-enum-name \
			--raw-line \
			'#![allow(non_camel_case_types, non_upper_case_globals, non_snake_case, unnecessary_transmutes)]' \
			"$amalgam" -- -I .
		rm "$amalgam"
		;;
esac
