internal_fmts = ['avs', 'dib', 'g00', 'mac', 'pcx', 'pgx', 'pi', 'pictor',
	'pnm', 'sgi', 'sixel', 'sun', 'tga', 'tim', 'tlg', 'wbm', 'wbmp', 'xbm',
	'xcursor']
external_fmts = ['flif', 'gif', 'heif', 'jbig', 'jpeg', 'jpeg2000', 'png',
	'raw', 'svg', 'tiff', 'webp']

fmt_deps = []
dec_files = []
dec_headers = []
pkg_deps = {'jpeg2000': 'libopenjp2', 'svg': 'librsvg-2.0', 'webp': 'libwebpdemux'}
conf_data = configuration_data()

foreach f : external_fmts
	state = get_option(f)
	if f in pkg_deps
		dep = dependency(pkg_deps[f], required : state)
	else
		dep = cc.find_library(f, required : state)
	endif

	if dep.found()
		fmt_deps += dep
		dec_files += ''.join(['dec/', f, '.c'])
		dec_headers += ''.join(['dec/', f, '.h'])
		conf_data.set('WU_ENABLE_' + f.to_upper(), true)
	endif
endforeach

internal_libs = []
foreach f : internal_fmts
	if not get_option(f).disabled()
		internal_libs += ''.join(['lib/', f, '.c'])
		dec_files += ''.join(['dec/', f, '.c'])
		dec_headers += ''.join(['dec/', f, '.h'])
		conf_data.set('WU_ENABLE_' + f.to_upper(), true)
	endif
endforeach

dec_enable = configure_file(output : 'dec_enable.def', configuration : conf_data)
dec_comptime = executable('dec_comptime', 'dec_comptime.c')
dec_all = [custom_target('dec_fmtmap',
		output : ['dec_fmtmap_map.c'],
		capture : true,
		depend_files : [dec_enable, 'dec_fmtmap.c'],
		command : [dec_comptime, '-f']),
	custom_target('dec_include',
		output : ['dec_include.def'],
		capture : true,
		depend_files : dec_enable,
		command : [dec_comptime, '-h'] + dec_headers),
	'dec.c',
	'rast_utils.c',
	'metadata.cpp'] \
	+ internal_libs + dec_files
rast = ['raster/bit.c',
	'raster/color.c',
	'raster/compost.c',
	'raster/graphics_adapters.c',
	'raster/lib.c',
	'raster/mem.c',
	'raster/memparser.c',
	'raster/pal.c',
	'raster/pix.c',
	'raster/raster.c',
	'raster/other.c',
	'raster/unpack.c']

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

wuview = ['main.c'] + rast + dec_all + util + gui + base
#timetest = ['time.c', 'common.c']
#wuvib = ['wuvib.c', 'colorimetry.c'] + decs + base
#wuglbench = ['gl_upload_bench.c'] + rast + gui + base
executable('wu', wuview, dependencies : all_deps, install : true)
