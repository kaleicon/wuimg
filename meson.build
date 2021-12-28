project('wu', 'c', 'cpp',
	default_options : ['optimization=3', 'warning_level=3', 'werror=true',
		'b_lto=true', 'b_pie=true', 'c_std=c11',
		'c_args=-fallow-store-data-races'])
add_project_arguments('-Wextra', '-Wconversion', '-Winline', '-Wno-pedantic',
	'-Wwrite-strings', '-Wformat-signedness',
	'-Wunsafe-loop-optimizations', '-D_DEFAULT_SOURCE', language : 'c')

cc = meson.get_compiler('c')

subdir('src')
