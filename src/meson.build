internal_fmts = ['avs', 'dib', 'mac', 'pcx', 'pgx', 'pi', 'pictor', 'pnm',
	'sgi', 'sixel', 'sun', 'tga', 'tim', 'wbmp', 'xbm', 'xcursor']
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
	'dec.c',
	'rast_utils.c',
	'metadata.cpp'] \
	+ internal_libs + internal_decs + external_decs
rast = ['raster/composite.c',
	'raster/unpack.c',
	'raster/raster.c',
	'raster/text.c',
	'raster/graphics_adapters.c',
	'raster/pal.c',
	'raster/pix.c',
	'raster/lib.c']
base = ['common.c', 'conf.c', 'term.c', 'wudefs.c', 'wustr.c', 'wutree.c']
util = ['colorimetry.c', 'extract.c', 'filesystem.c', 'write_pam.c']

wayland_scanner = find_program('wayland-scanner')
xdg_shell_xml = '/usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml'
gui = [
	custom_target('xdg-shell-protocol',
		output : ['xdg-shell-protocol.c'],
		command : [wayland_scanner, 'private-code', xdg_shell_xml, '@OUTPUT@']),
	custom_target('xdg-shell-client-protocol',
		output : ['xdg-shell-client-protocol.h'],
		command : [wayland_scanner, 'client-header', xdg_shell_xml, '@OUTPUT@']),
	'display.c',
	'events.c',
	'opengl.c',
	'window.c',
	'window/base.c',
	'window/egl.c',
	'window/glfw.c',
	'window/drm.c',
	'window/wayland.c']

base_deps = fmt_deps + [
	cc.find_library('m', required : false),
	dependency('exiv2'),
	dependency('icu-i18n'),
	dependency('uchardet'),
]
all_deps = base_deps + [
	dependency('libarchive'),
	dependency('epoxy'),
	dependency('glfw3'),
	dependency('gbm'),
	dependency('libdrm'),
	dependency('wayland-client'),
	dependency('wayland-egl'),
	dependency('xkbcommon'),
	cc.find_library('rt')
]

wuview = ['main.c'] + rast + decs + util + gui + base
#timetest = ['time.c', 'common.c']
#wuvib = ['wuvib.c', 'colorimetry.c'] + decs + base
#wuglbench = ['gl_upload_bench.c'] + rast + gui + base
executable('wu', wuview, dependencies : all_deps, install : true)
#executable('wutime', timetest)
#executable('wuvib', wuvib, dependencies : base_deps)
#executable('wuglbench', wuglbench, dependencies : all_deps)
