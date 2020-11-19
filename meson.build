project('wu', 'c', 'cpp', default_options : ['optimization=3',
	'warning_level=3', 'werror=true', 'b_lto=true', 'b_pie=true',
	'c_std=gnu99', 'c_args=-ffast-math -fallow-store-data-races'])
add_project_arguments('-Wextra', '-Wpedantic', '-Wconversion', '-Winline',
	'-Wwrite-strings', '-Wformat-signedness',
	'-Wunsafe-loop-optimizations', language : 'c')

internal_fmts = ['avs', 'bmp', 'mac', 'pcx', 'pgx', 'pi', 'pnm', 'sgi',
	'sixel', 'sun', 'tga', 'wbmp', 'xbm']
external_fmts = ['flif', 'gif', 'heif', 'jbig', 'jpeg', 'jpeg2000', 'png',
	'raw', 'svg', 'tiff', 'webp']


cc = meson.get_compiler('c')
fmt_deps = []
pkg_deps = {'jpeg2000': 'libopenjp2', 'svg': 'librsvg-2.0', 'webp': 'libwebpdemux'}
conf_data = configuration_data()

external_decs = []
foreach f : external_fmts
	state = get_option(f)
	if f in pkg_deps
		dep = dependency(pkg_deps[f], required : state)
	else
		dep = cc.find_library(f, required : state)
	endif

	if dep.found()
		fmt_deps += dep
		external_decs += ''.join(['dec/', f, '.c'])
		conf_data.set('WU_ENABLE_' + f.to_upper(), true)
	endif
endforeach

internal_libs = []
internal_decs = []
foreach f : internal_fmts
	if not get_option(f).disabled()
		internal_libs += ''.join(['dec/lib/', f, '.c'])
		internal_decs += ''.join(['dec/', f, '.c'])
		conf_data.set('WU_ENABLE_' + f.to_upper(), true)
	endif
endforeach

configure_file(output : 'dec_conf.h', configuration : conf_data)

decs = ['dec.c', 'dec/lib/common/composite.c', 'dec/lib/common/unpack.c',
	'dec/lib/common/text.c', 'dec/lib/common/lib.c', 'metadata.cpp'] \
	+ internal_libs + internal_decs + external_decs
common = ['common.c', 'wudefs.c', 'wutree.c']
util = ['colorimetry.c', 'extract.c', 'write_pam.c', 'conf.c']
gui = ['display.c', 'window.c', 'opengl.c', 'events.c']

base_deps = fmt_deps + [cc.find_library('m', required : false),
	dependency('uchardet')]
all_deps = base_deps \
	+ [dependency('epoxy'), dependency('glfw3'), dependency('libarchive'),
		dependency('exiv2')]

wuview = ['main.c'] + decs + util + gui + common
#wuvib = ['wuvib.c', 'colorimetry.c'] + decs + common
#wuglbench = ['gl_upload_bench.c'] + decs + gui + common
executable('wu', wuview, dependencies : all_deps, install : true)
#executable('wuvib', wuvib, dependencies : base_deps)
#executable('wuglbench', wuglbench, dependencies : all_deps)
