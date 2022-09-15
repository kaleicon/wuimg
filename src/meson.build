internal_fmts = [
	'avs',
	'dib',
	'dpx',
	'farbfeld',
	'g00',
	'hg3',
	'mac',
	'mag',
	'maki',
	'msx',
	'pcx',
	'pdt',
	'pgx',
	'pi',
	'pictor',
	'pnm',
	'prt',
	'qoi',
	'sgi',
	'sixel',
	'sun',
	'tga',
	'tim',
	'tlg',
	'wbmp',
	'wpx',
	'xbm',
	'xcursor',
	'xyz']

external_fmts = [
	'avif',
	'flif',
	'gif',
	'heif',
	'jbig',
	'jpeg',
	'jpeg2000',
	'jpegls',
	'jpegxl',
	'Lerc',
	'png',
	'raw',
	'svg',
	'tiff',
	'webp']

dep_list = []
src_list = []
conf_data = configuration_data()

pkg_map = {'jpeg2000': 'libopenjp2', 'jpegls': 'charls',
	'jpegxl': 'libjxl', 'svg': 'librsvg-2.0', 'webp': 'libwebpdemux'}
foreach f : external_fmts
	state = get_option(f)
	if f in pkg_map
		dep = dependency(pkg_map[f], required : state)
	else
		dep = cc.find_library(f, required : state)
	endif

	if dep.found()
		dep_list += dep
		src_list += ''.join(['dec/', f.to_lower(), '.c'])
		conf_data.set('WU_ENABLE_' + f.to_upper(), true)
	endif
endforeach

extra_deps = {'hg3': 'zlib', 'xyz': 'zlib'}
foreach f : internal_fmts
	if not get_option(f).disabled()
		src_list += ''.join(['lib/', f, '.c'])
		src_list += ''.join(['dec/', f, '.c'])
		conf_data.set('WU_ENABLE_' + f.to_upper(), true)
		if f in extra_deps
			dep_list += dependency(extra_deps[f])
		endif
	endif
endforeach

dec_enable = configure_file(output : 'dec_enable.def', configuration : conf_data)
dec_comptime = executable('dec_comptime', 'dec_comptime.c')

src_list += [custom_target('dec_maps',
		output : ['dec_maps.c'],
		capture : true,
		depend_files : [dec_enable, 'dec.c'],
		command : [dec_comptime, '-m']),
	custom_target('dec_fn',
		output : ['dec_fn.h'],
		capture : true,
		depend_files : dec_enable,
		command : [dec_comptime, '-h']),

	'conf.c',
	'metadata.cpp',
	'rast_utils.c',
	'term.c',
	'utf8.c',
	'wudefs.c',
	'wutree.c',

	'common/bit.c',
	'common/common.c',
	'common/endian.c',
	'common/file.c',
	'common/math.c',
	'common/mem.c',
	'common/wustr.c',

	'raster/cicp.c',
	'raster/color.c',
	'raster/compost.c',
	'raster/fmt.c',
	'raster/graphics_adapters.c',
	'raster/icc.c',
	'raster/mat.c',
	'raster/memparser.c',
	'raster/pal.c',
	'raster/pix.c',
	'raster/strip.c',
	'raster/unpack.c']

dep_list += [cc.find_library('m', required : false),
	dependency('exiv2'),
	dependency('lcms2'),
	dependency('icu-i18n'),
	dependency('uchardet')]

wayland_scanner = find_program('wayland-scanner')
xdg_shell_xml = '/usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml'
view_src_list = [
	custom_target('xdg-shell-protocol',
		output : ['xdg-shell-protocol.c'],
		command : [wayland_scanner, 'private-code', xdg_shell_xml, '@OUTPUT@']),
	custom_target('xdg-shell-client-protocol',
		output : ['xdg-shell-client-protocol.h'],
		command : [wayland_scanner, 'client-header', xdg_shell_xml, '@OUTPUT@']),

	'display.c',
	'events.c',
	'extract.c',
	'filesystem.c',
	'opengl.c',
	'write_pam.c',

	'window.c',
	'window/base.c',
	'window/egl.c',
	'window/glfw.c',
	'window/drm.c',
	'window/wayland.c']

view_dep_list = [
	dependency('libarchive'),

	dependency('epoxy'),
	dependency('glfw3'),
	dependency('gbm'),
	dependency('libdrm'),

	cc.find_library('rt'),
	dependency('wayland-client'),
	dependency('wayland-egl'),
	dependency('xkbcommon')]

wufuzz = executable('wu-fuzz', ['main_fuzz.c'] + src_list,
	dependencies : dep_list,
	install : true)
wuview = executable('wu', ['main.c'] + src_list + view_src_list,
	dependencies : dep_list + view_dep_list,
	install : true)
