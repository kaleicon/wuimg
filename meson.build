project('wu', 'c')

internal_fmts = ['avs', 'bmp', 'pcx', 'pgx', 'pi', 'pnm', 'sgi', 'sun', 'tga',
	'wbmp', 'xbm']
external_fmts = ['flif', 'gif', 'heif', 'jbig', 'jpeg', 'jpeg2000', 'png',
	'raw', 'svg', 'tiff', 'webp']


cc = meson.get_compiler('c')
fmt_deps = []
pkg_deps = {'jpeg2000': 'libopenjp2', 'svg': 'librsvg-2.0', 'webp': 'libwebpdemux'}

external_decs = []
foreach f : external_fmts
	if f in pkg_deps
		dep = dependency(pkg_deps[f])
	else
		dep = cc.find_library(f)
	endif

	external_decs += ''.join(['dec_', f, '.c'])
	fmt_deps += dep
endforeach


internal_libs = []
internal_decs = []
foreach f : internal_fmts
	internal_libs += ''.join(['lib_', f, '.c'])
	internal_decs += ''.join(['dec_', f, '.c'])
endforeach

decs = ['dec.c', 'common_composite.c', 'common_unpack.c', 'common_lib.c'] \
	+ internal_libs + internal_decs + external_decs
common = ['common.c', 'wudefs.c']
util = ['colorimetry.c', 'extract.c', 'write_pam.c', 'conf.c']
gui = ['display.c', 'window.c', 'opengl.c', 'events.c']

base_deps = fmt_deps + [cc.find_library('m', required : false),
	dependency('uchardet')]
all_deps = base_deps \
	+ [dependency('epoxy'), dependency('glfw3'), dependency('libarchive')]

wuview = ['main.c'] + common + decs + util + gui
wuvib = ['wuvib.c', 'colorimetry.c'] + decs + common
executable('wu', wuview, dependencies : all_deps)
executable('wuvib', wuvib, dependencies : base_deps)
