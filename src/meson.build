internal_fmts = ['avs', 'dib', 'mac', 'pcx', 'pgx', 'pi', 'pictor', 'pnm',
	'sgi', 'sixel', 'sun', 'tga', 'tim', 'wbmp', 'xbm']
external_fmts = ['flif', 'gif', 'heif', 'jbig', 'jpeg', 'jpeg2000', 'png',
	'raw', 'svg', 'tiff', 'webp']

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
		internal_libs += ''.join(['lib/', f, '.c'])
		internal_decs += ''.join(['dec/', f, '.c'])
		conf_data.set('WU_ENABLE_' + f.to_upper(), true)
	endif
endforeach

dec_enable = configure_file(output : 'dec_enable.def', configuration : conf_data)
decs = [custom_target('dec_fmtmap',
		output : ['dec_fmtmap.c'],
		capture : true, depend_files : dec_enable,
		command : [executable('fmtmap_sorted', 'dec_fmtmap_sort_quine.c')]),
	'raster/composite.c',
	'raster/unpack.c',
	'raster/raster.c',
	'raster/text.c',
	'raster/graphics_adapters.c',
	'raster/pal.c',
	'raster/pix.c',
	'raster/lib.c',
	'dec.c',
	'rast_utils.c',
	'metadata.cpp'] \
	+ internal_libs + internal_decs + external_decs
base = ['common.c', 'conf.c', 'term.c', 'wudefs.c', 'wustr.c', 'wutree.c']
util = ['colorimetry.c', 'extract.c', 'filesystem.c', 'write_pam.c']
gui = ['display.c', 'drm.c', 'events.c', 'opengl.c', 'window.c']

base_deps = fmt_deps + [
	cc.find_library('m', required : false),
	dependency('exiv2'),
	dependency('icu-i18n'),
	dependency('uchardet'),
]
all_deps = base_deps + [
	dependency('epoxy'),
	dependency('gbm'),
	dependency('glfw3'),
	dependency('libarchive'),
	dependency('libdrm')
]

wuview = ['main.c'] + decs + util + gui + base
#timetest = ['time.c', 'common.c']
#wuvib = ['wuvib.c', 'colorimetry.c'] + decs + base
#wuglbench = ['gl_upload_bench.c'] + decs + gui + base
executable('wu', wuview, dependencies : all_deps, install : true)
#executable('wutime', timetest)
#executable('wuvib', wuvib, dependencies : base_deps)
#executable('wuglbench', wuglbench, dependencies : all_deps)
