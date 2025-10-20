# SPDX-License-Identifier: 0BSD
# SPDX-FileCopyrightText: 2020 kaleido
python = find_program('python3')
comptime = files('comptime.py')
fmt_names = run_command(python, comptime, 'names', check : true).stdout().split()

external_map = {
	# For empty arrays, use cc.find_library(key) instead of dependency()
	'avif': [],
	'flif': [],
	'gif': [],
	'heif': [],
	'jbig': [],
	'jpeg': [],
	'png': [],
	'raw': [],
	'tiff': [],

	'jbig2': ['jbig2dec'],
	'jpeg2000': ['libopenjp2'],
	'jpegls': ['charls'],
	'jpegxl': ['libjxl', 'libjxl_threads'],
	'lerc' : ['Lerc'],
	'svg': ['librsvg-2.0'],
	'webp': ['libwebpdecoder', 'libwebpdemux']
}

pkg_map = external_map + {
	'bmz': ['zlib'],
	'hg3': ['zlib'],
	'xyz': ['zlib'],
}

attr_map = {
	'encoder': ['jpegxl'],
	'freerider': ['bmz'],
}

# Core setup
src_list = []
dec_enable = configuration_data()
dep_list = [
	cc.find_library('m', required : false),
	dependency('exiv2'),
	dependency('lcms2'),
	dependency('icu-i18n'),
	dependency('icu-uc'),
	dependency('uchardet')
]

foreach f : fmt_names
	is_external = f in external_map
	state = get_option(f).disable_auto_if(
		is_external and get_option('disable_external')
	)

	deps = []
	if f in pkg_map
		if pkg_map[f].length() == 0
			deps += cc.find_library(f, required : state)
		else
			foreach lib : pkg_map[f]
				deps += dependency(lib, required : state)
			endforeach
		endif
	endif

	enable = true
	foreach d : deps
		enable = enable and d.found()
	endforeach

	if state.require(enable).allowed()
		dec_enable.set('WU_ENABLE_' + f.to_upper(), true)
		dep_list += deps
		fmtfile = f.to_lower() + '.c'
		if f not in attr_map['freerider']
			src_list += 'dec' / fmtfile
			if not is_external
				src_list += 'lib' / fmtfile
			endif
		endif
		if f in attr_map['encoder']
			src_list += 'enc' / fmtfile
		endif

	endif
endforeach

dec_enable = configure_file(output : 'dec_enable.def', configuration : dec_enable)

src_list += [custom_target('dec_maps',
		input : dec_enable,
		output : 'dec_maps.c',
		capture : true,
		feed : true,
		command : [python, comptime, 'maps']),
	custom_target('dec_fn',
		input : dec_enable,
		output : 'dec_fn.h',
		capture : true,
		feed : true,
		command : [python, comptime, 'header']),

	'auto.c',
	'conf.c',
	'dec.c',
	'filesystem.c',
	'rast_utils.c',
	'write.c',
	'wudefs.c',

	'enc/pam.c',

	'misc/bit.c',
	'misc/common.c',
	'misc/decomp.c',
	'misc/endian.c',
	'misc/file.c',
	'misc/iff.c',
	'misc/mat.c',
	'misc/math.c',
	'misc/mem.c',
	'misc/metadata.cpp',
	'misc/mparser.c',
	'misc/term.c',
	'misc/time.c',
	'misc/utf8.c',
	'misc/wustr.c',
	'misc/wutree.c',

	'raster/bitfield.c',
	'raster/cicp.c',
	'raster/color.c',
	'raster/compost.c',
	'raster/err.c',
	'raster/fmt.c',
	'raster/graphics_adapters.c',
	'raster/icc.c',
	'raster/pal.c',
	'raster/pix.c',
	'raster/strip.c',
	'raster/unpack.c',
	'raster/wuimg.c']

# FIXME: seriously?
libuwu = static_library('uwu', src_list, dependencies : dep_list)

# Viewer setup
window_map = {
	'window_wayland': ['wayland-client', 'wayland-egl', 'xkbcommon'],
	'window_drm': ['gbm', 'libdrm'],
	'window_glfw': ['glfw3'],
}
window_dep_list = [
	dependency('epoxy', required : false),
	dependency('libarchive', required : false),
]
window_src_list = [
	'main.c',

	'display.c',
	'events.c',
	'extract.c',
	'opengl.c',

	'window.c',
	'window/base.c',
	'window/egl.c',
]
window_enable = configuration_data()

foreach backend, libs : window_map
	state = get_option(backend)
	deps = []
	enable = true
	foreach lib : libs
		d = dependency(lib, required : state)
		deps += d
		enable = enable and d.found()
	endforeach

	if state.require(enable).allowed()
		window_enable.set('WU_ENABLE_' + backend.to_upper(), true)
		window_dep_list += deps
		window_src_list += backend.replace('_', '/') + '.c'
	endif
endforeach

if window_enable.has('WU_ENABLE_WINDOW_WAYLAND')
	wayland_scanner = find_program('wayland-scanner')
	wayland_proto = dependency('wayland-protocols')
	base_path = wayland_proto.get_variable('pkgdatadir')
	protocols = [
		['stable', 'xdg-shell', ''],
		['staging', 'color-management', '-v1'],
		['staging', 'content-type', '-v1'],
		['staging', 'cursor-shape', '-v1'],
		['unstable', 'xdg-decoration', '-unstable-v1'],
		['stable', 'tablet', '-v2'], # needed by cursor-shape
	]
	type = [
		['private-code', '.c'],
		['client-header', '.h'],
	]
	foreach proto : protocols
		status = proto[0]
		basename = proto[1]
		path = base_path / status / basename / basename + proto[2] + '.xml'
		foreach t : type
			name = '-'.join([basename, t[0]])
			window_src_list += custom_target(name,
				feed : true,
				capture : true,
				input : path,
				output : name + t[1],
				command : [wayland_scanner, t[0]])
		endforeach
	endforeach
endif

window_enable = configure_file(output : 'window_enable.def', configuration : window_enable)

executable('wu', window_src_list,
	dependencies : dep_list + window_dep_list,
	link_with : libuwu,
	install : true)
executable('wuconv', ['main_wuconv.c', 'imgconv.c'],
	dependencies : dep_list,
	link_with : libuwu,
	build_by_default : false)
executable('wufuzz', ['main_fuzz.c'],
	dependencies : dep_list,
	link_with : libuwu,
	build_by_default : false)
custom_target('desktop',
	feed : true,
	input : dec_enable,
	output : ['wu.desktop', 'wu-archive.desktop'],
	command : [python, comptime, 'desktop', '@OUTPUT@'],
	install : true,
	install_dir: get_option('datadir') / 'applications')
custom_target('formats',
	capture : true,
	output : 'FORMATS.txt',
	command : [python, comptime, '-all', 'show'],
	build_by_default : false)
incdir = include_directories('.')
