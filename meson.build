# SPDX-License-Identifier: 0BSD
# SPDX-FileCopyrightText: 2020 kaleido
project('wu', 'c', 'cpp',
	default_options : [
		'buildtype=release',
		'optimization=3',
		'warning_level=3',
		'b_lto=true',
		'b_pie=true',
		'c_std=c99',
		'rust_std=2021'
	],
	license : '0BSD',
	# 0.59.0: {'feature_option.allowed()', 'feature_option.disable_auto_if()', 'feature_option.require()', 'feed arg in custom_target'}
	meson_version : '>= 0.59'
)

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

cc = meson.get_compiler('c')
cc_id = cc.get_id()
if cc_id == 'gcc'
	add_project_arguments(
		'-Wformat-signedness',
		'-Wstringop-overflow=4',
		'-Wunsafe-loop-optimizations',
		'-Wno-unterminated-string-initialization',

		'-fallow-store-data-races',
		'-fno-signaling-nans',
		language : 'c')
elif cc_id == 'clang'
	add_project_arguments(
		'-Wno-bitwise-instead-of-logical',
		'-Wno-enum-enum-conversion',
		language : 'c')
endif

subdir('src')
subdir('test')
