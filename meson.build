# SPDX-License-Identifier: 0BSD
# SPDX-FileCopyrightText: 2020 kaleido
project('wu', 'c', 'cpp',
	default_options : ['optimization=3', 'warning_level=3',
		'b_lto=true', 'b_pie=true', 'c_std=c99'],
	license : '0BSD')

cc = meson.get_compiler('c')

add_project_arguments(
	'-D_DEFAULT_SOURCE',
	'-Wpedantic',
	'-Wconversion',
	'-Wshadow',
	'-Wwrite-strings',

	# Needed by the ultra-convenient null_function
	'-Wno-strict-prototypes',
	'-Wno-missing-field-initializers',

	'-fno-math-errno',
	'-fno-trapping-math',
	'-ffp-contract=fast',
	language : 'c')
if cc.get_id() == 'gcc'
	add_project_arguments(
		'-Wformat-signedness',
		'-Wstringop-overflow=4',
		'-Wunsafe-loop-optimizations',

		'-fallow-store-data-races',
		'-fno-signaling-nans',
		language : 'c')
endif

subdir('src')
