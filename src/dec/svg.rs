// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2026 kaleido
use crate::wu;
use crate::wurs;
use resvg;
use resvg::{usvg,tiny_skia};

fn scale_dim(dim: f32, scale: f32) -> u32 {
	(dim*scale).ceil().max(1.0) as u32
}

fn fit_within_frame(mut fw: f32, mut fh: f32, svg_dims: usvg::Size) -> f32 {
	let tiny_skia_limit = (i32::MAX/4) as f32;
	fw = fw.min(tiny_skia_limit);
	fh = fh.min(tiny_skia_limit);
	let w = svg_dims.width();
	let h = svg_dims.height();
	(fw.min(w) / w).min(fh.min(h) / h)
}

fn get_dec_state(infile: &mut wu::image_file) -> *mut usvg::Tree {
	return infile.dec_state as *mut usvg::Tree;
}

extern "C" fn end_svg(infile_ptr: *mut wu::image_file) {
	let infile = unsafe {&mut *infile_ptr};
	let _ = unsafe { std::boxed::Box::from_raw(get_dec_state(infile)) };
	infile.dec_state = std::ptr::null_mut();
}

extern "C" fn event_svg(infile_ptr: *mut wu::image_file,
state_ptr: *mut wu::wu_state, ev: wu::image_event) -> wu::wu_st {
	let infile = unsafe {&mut *infile_ptr};
	let state = unsafe {&mut *state_ptr};
	let conf = unsafe {&*infile.conf};
	let img = unsafe {&mut *infile.sub_img};

	let tree = unsafe {&mut *get_dec_state(infile)};
	let svg_dims = tree.size();
	let scale = if conf.svg_window_adapt {
		fit_within_frame(state.fb.w as f32, state.fb.h as f32, svg_dims)
	} else {
		fit_within_frame(conf.max_img_size as f32, conf.max_img_size as f32,
			svg_dims)
	};
	match ev {
		wu::ev_metadata => {
			if !conf.svg_window_adapt {
				let out_dims = tiny_skia::IntSize::from_wh(
					scale_dim(svg_dims.width(), scale),
					scale_dim(svg_dims.height(), scale));
				let out_dims = match out_dims {
					Some(d) => d,
					None => return wurs::wuerr!(wu::wu_invalid_params,
						"(svg.rs) failed to create IntSize"),
				};
				img.w = out_dims.width() as usize;
				img.h = out_dims.height() as usize;
			}
			img.channels = 4;
			img.bitdepth = 8;
			img.set_alpha(wu::alpha_associated);
			img.set_scalable(conf.svg_window_adapt);
			wu::wu_st::ok()
		},
		wu::ev_subcycle | wu::ev_transform => {
			let mut ts = resvg::tiny_skia::Transform::from_scale(scale, scale);
			if conf.svg_window_adapt {
				let hw = svg_dims.width() * -0.5;
				let hh = svg_dims.height() * -0.5;
				ts = ts.pre_translate(hw, hh);
				let mut x = state.fb.w as f32 * 0.5;
				let mut y = state.fb.h as f32 * 0.5;
				if ev == wu::ev_transform {
					let s = state.zoom;
					ts = ts.post_scale(s, if state.mirror {-s} else {s})
						.post_rotate(state.rotate as f32 * 90.0);
					x = state.x_offset.mul_add(s, x);
					y = state.y_offset.mul_add(s, y);
				}
				ts = ts.post_translate(x, y);

				if img.w == state.fb.w as usize
				|| img.h == state.fb.h as usize {
					unsafe {
						wu::memset(img.data as *mut _, 0,
							wu::wuimg_size(img) as _);
					}
				} else {
					unsafe {
						wu::wuimg_free(img);
					}
					img.data = std::ptr::null_mut();
					img.w = state.fb.w as usize;
					img.h = state.fb.h as usize;
				}
			}
			if img.data.is_null() {
				let st = unsafe {wu::wuimg_alloc(img)};
				if st != wu::wu_ok {
					return wurs::wuerr_here!(st);
				}
					
			}

			let data = img.get_data();
			let pm = resvg::tiny_skia::PixmapMut::from_bytes(data,
				img.w as u32, img.h as u32);
			let mut pm = match pm {
				Some(p) => p,
				None => return wurs::wuerr!(wu::wu_invalid_params,
					"(svg.rs) failed to create pixmap"),
			};
			resvg::render(tree, ts, &mut pm);
			wu::wu_st::ok()
		},
		_ => wu::wu_st::no_change()
	}
}

extern "C" fn init_svg(infile_ptr: *mut wu::image_file) -> wu::wu_st {
	let infile = unsafe {&mut *infile_ptr};
	let map = infile.get_map();
	let usvg_opts = usvg::Options {
		..Default::default()
	};
	let tree = match usvg::Tree::from_data(map, &usvg_opts) {
		Ok(t) => std::boxed::Box::new(t),
		Err(_) => return wurs::wuerr!(wu::wu_decoding_error,
			"(svg.rs) failed to parse xml document"),
	};
	infile.dec_state = std::boxed::Box::into_raw(tree) as *mut _;
	wu::wu_st::ok()
}

#[no_mangle]
pub static svg_fn: wu::image_fn = wu::image_fn {
	mmap: true,
	alloc_single: true,
	init: Some(init_svg),
	event: Some(event_svg),
	end: Some(end_svg),

	alloc_on_subcycle: false,
	state_size: 0,
};
