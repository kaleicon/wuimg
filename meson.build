project('wu', 'c', 'cpp',
	default_options : ['optimization=3', 'warning_level=3', 'werror=true',
		'b_lto=true', 'b_pie=true', 'c_std=c99',
		'c_args=-fallow-store-data-races -fno-sanitize-recover=all'])
add_project_arguments('-Wextra', '-Wconversion', '-Wpedantic', '-Wshadow',
	'-Wwrite-strings', '-Wformat-signedness',
	'-Wstringop-overflow=4', '-Wstringop-truncation',
	'-Wunsafe-loop-optimizations', '-D_DEFAULT_SOURCE', language : 'c')

cc = meson.get_compiler('c')

subdir('src')
