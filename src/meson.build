# SPDX-License-Identifier: 0BSD
fmt_map = {
	'internal': [
		'avs',
		'c64',
		'degas',
		'dib',
		'dpx',
		'farbfeld',
		'g00',
		'hg3',
		'ilbm',
		'mac',
		'mag',
		'maki',
		'msx',
		'pcx',
		'pdt',
		'pgx',
		'pi',
		'pic',
		'pictor',
		'pnm',
		'prt',
		'px',
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
		'xwd',
		'xyz'
	],

	'external': [
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
		'webp'
	],

	'freerider': [
		'bmz',
	],
}

pkg_map = {
	'bmz': ['zlib'],
	'hg3': ['zlib'],
	'xyz': ['zlib'],

	'jpeg2000': ['libopenjp2'],
	'jpegls': ['charls'],
	'jpegxl': ['libjxl', 'libjxl_threads'],
	'svg': ['librsvg-2.0'],
	'webp': ['libwebpdemux']
}
dep_list = []
src_list = []
dec_enable = configuration_data()

foreach cat, fmts : fmt_map
	foreach f : fmts
		state = get_option(f)
		deps = []
		if f in pkg_map
			foreach lib : pkg_map[f]
				deps += dependency(lib, required : state)
			endforeach
		elif cat == 'external'
			deps += cc.find_library(f, required : state)
		endif

		enable = true
		foreach d : deps
			if not d.found()
				enable = false
			endif
		endforeach

		if state.require(enable).allowed()
			dec_enable.set('WU_ENABLE_' + f.to_upper(), true)
			dep_list += deps
			if cat != 'freerider'
				src_list += ''.join(['dec/', f.to_lower(), '.c'])
				if cat == 'internal'
					src_list += ''.join(['lib/', f.to_lower(), '.c'])
				endif
			endif
		endif
	endforeach
endforeach

window_map = {
	'window_wayland': ['wayland-client', 'wayland-egl', 'xkbcommon'],
	'window_drm': ['gbm', 'libdrm'],
	'window_glfw': ['glfw3'],
}
window_dep_list = []
window_src_list = []
window_enable = configuration_data()

foreach backend, libs : window_map
	state = get_option(backend)
	deps = []
	enable = true
	foreach lib : libs
		d = dependency(lib, required : state)
		if d.found()
			deps += d
		else
			enable = false
		endif
	endforeach

	if state.require(enable).allowed()
		window_enable.set('WU_ENABLE_' + backend.to_upper(), true)
		window_dep_list += deps
		window_src_list += backend.replace('_', '/') + '.c'
	endif
endforeach

dep_list += [cc.find_library('m', required : false),
	dependency('exiv2'),
	dependency('lcms2'),
	dependency('icu-i18n'),
	dependency('uchardet')]
window_dep_list += [dependency('libarchive'),
	dependency('epoxy')]

if window_enable.has('WU_ENABLE_WINDOW_WAYLAND')
	wayland_scanner = find_program('wayland-scanner')
	protocol_names = ['xdg-shell']
	type = [['private-code', '.c'], ['client-header', '.h']]
	foreach p : protocol_names
		foreach t : type
			name = '-'.join([p, t[0]])
			path = '/usr/share/wayland-protocols/stable/%/%.xml'.replace('%', p)
			window_src_list += custom_target(name,
				output : [name + t[1]],
				command : [wayland_scanner, t[0], path, '@OUTPUT@'])
		endforeach
	endforeach
endif

dec_enable = configure_file(output : 'dec_enable.def', configuration : dec_enable)
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

	'misc/bit.c',
	'misc/common.c',
	'misc/decomp.c',
	'misc/endian.c',
	'misc/file.c',
	'misc/math.c',
	'misc/mem.c',
	'misc/mparser.c',
	'misc/wustr.c',

	'raster/cicp.c',
	'raster/color.c',
	'raster/compost.c',
	'raster/fmt.c',
	'raster/graphics_adapters.c',
	'raster/icc.c',
	'raster/mat.c',
	'raster/pal.c',
	'raster/pix.c',
	'raster/strip.c',
	'raster/unpack.c',
	'raster/wuimg.c']

window_enable = configure_file(output : 'window_enable.def', configuration : window_enable)
window_src_list += ['display.c',
	'events.c',
	'extract.c',
	'filesystem.c',
	'opengl.c',
	'write_pam.c',

	'window.c',
	'window/base.c',
	'window/egl.c']

wuview = executable('wu', ['main.c'] + src_list + window_src_list,
	dependencies : dep_list + window_dep_list,
	install : true)
if get_option('compile_fuzzer')
	wufuzz = executable('wu-fuzz', ['main_fuzz.c'] + src_list,
		dependencies : dep_list,
		install : true)
endif
