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
		conf=$(dirname $(realpath "$0"))/rustfmt.toml
		cd "$1"
		amalgam=$(mktemp amalgamXXXXXX.h)
		gen_include > "$amalgam"
		bindgen \
			--with-derive-default \
			--no-prepend-enum-name \
			--rustfmt-configuration-file "$conf" \
			--raw-line \
			'#![allow(non_camel_case_types, non_upper_case_globals, non_snake_case, unnecessary_transmutes)]' \
			"$amalgam" -- -I .
		rm "$amalgam"
		;;
esac
