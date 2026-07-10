// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2026 kaleido
use crate::wu;
use crate::wurs;

use std::default::Default;

struct GifPrev {
	dispose: gif::DisposalMethod,
	frame: wu::compost,
}

impl Default for GifPrev {
	fn default() -> Self {
		GifPrev {
			dispose: gif::DisposalMethod::Any,
			frame: Default::default(),
		}
	}
}

struct GifState<'a> {
	decoder: gif::Decoder<std::io::Cursor<&'a [u8]>>,
	prev: GifPrev,
	buf: Vec<u8>,
	frame_area_size: usize,
	pal: [[u8; 4]; 256],
}

fn gif_frame_to_compost(fr: &gif::Frame) -> wu::compost {
	wu::compost {
		x: fr.left as usize,
		y: fr.top as usize,
		w: fr.width as usize,
		h: fr.height as usize,
	}
}

fn rgb_to_rgba(dst: &mut [[u8; 4]; 256], src: &[u8]) {
	dst.into_iter().zip(src.as_chunks::<3>().0.into_iter()).for_each(|(d, s)| {
		d[..s.len()].copy_from_slice(s);
		d[s.len()] = 0xff;
	});
}

fn compost_frame(img: &wu::wuimg, frames: &mut wu::image_frames,
ds: &mut GifState) -> wu::wu_st {
	let frame = match ds.decoder.next_frame_info() {
		Ok(Some(f)) => f,
		_ => return wurs::wuerr_here!(
			wu::wu_decoding_error,
			"unexpected frame info read failure"),
	};

	// Save frame info before it's invalidated
	let cur = gif_frame_to_compost(frame);
	let trns = frame.transparent;
	let dispose = frame.dispose;

	// Expand to RGBA for faster copies
	match ds.decoder.palette() {
		Ok(src_pal) => rgb_to_rgba(&mut ds.pal, src_pal),
		Err(_) => return wurs::wuerr_here!(
			wu::wu_decoding_error,
			"no palette for frame"),
	}

	// Decode into our preallocated buffer
	let (src, restore) = ds.buf.split_at_mut(ds.frame_area_size);
	match ds.decoder.read_into_buffer(src) {
		Ok(_) => {},
		Err(_) => return wurs::wuerr_here!(wu::wu_decoding_error,
			"failed to decode frame"),
	};

	let dst = img.get_data();
	let stride = unsafe { wu::wuimg_stride(img) };
	let ch = 4;

	/* Disposal explanation: https://usage.imagemagick.org/anim_basics/#dispose
	 * Any | Keep: Do nothing
	 * Background: Clear frame area to transparency after it's shown
	 * Previous: Restore frame area to what it was before. That implies the
	 *           empty canvas if the very first frame uses Previous. */
	let p = ds.prev.frame;
	match ds.prev.dispose {
		gif::DisposalMethod::Any | gif::DisposalMethod::Keep => {},
		gif::DisposalMethod::Background => {
			img.compost_clear(0, &p);
		},
		gif::DisposalMethod::Previous => {
			img.compost_overwrite(restore, &p);
		},
	};

	ds.prev.dispose = dispose;
	ds.prev.frame = cur;
	// Save canvas area for later restoral
	if dispose == gif::DisposalMethod::Previous {
		img.compost_extract(restore, &p);
	}

	for y in 0..cur.h {
		for x in 0..cur.w {
			let c = src[y*cur.w + x];
			if let Some(t) = trns {
				if c == t {
					continue;
				}
			}
			let pix = (y+cur.y)*stride + (x+cur.x)*ch;
			dst[pix..pix+ch].copy_from_slice(&ds.pal[c as usize]);
		}
	}
	frames.current += 1;
	wu::wu_st::ok()
}

fn build_gif_decoder(data: &[u8], skip_decode: bool)
-> Result<gif::Decoder<std::io::Cursor<&[u8]>>, wu::wu_st> {
	let mut opt = gif::DecodeOptions::new();
	opt.skip_frame_decoding(skip_decode);
	match opt.read_info(std::io::Cursor::new(data)) {
		Ok(d) => Ok(d),
		Err(_) => Err(wurs::wuerr_here!(wu::wu_invalid_header,
			"failed to read gif header")),
	}
}

fn checked_mul(w: u16, h: u16, ch: u8) -> (usize, bool) {
	let m1 = (w as usize).overflowing_mul(h as usize);
	let m2 = m1.0.overflowing_mul(ch as usize);
	(m2.0, m2.1 | m1.1)
}

fn gather_gif_info(img: &mut wu::wuimg, infile: &wu::image_file,
ds: &mut GifState) -> wu::wu_st {
	/* Get number of frames and calculate canvas dimensions.
	 * Frames may be bigger or be located outside the canvas area given in
	 * the header. If so, we'll just make a bigger canvas. */
	let mut w = ds.decoder.width();
	let mut h = ds.decoder.height();
	let mut nr_frames = 0;
	let mut msg = None;
	while let Ok(Some(fr)) = ds.decoder.next_frame_info() {
		if let (Some(mw), Some(mh)) = (fr.width.checked_add(fr.left), fr.height.checked_add(fr.top)) {
			w = std::cmp::max(w, mw);
			h = std::cmp::max(h, mh);
		} else {
			msg = Some(c"frame position causes u16 overflow, will truncate animation");
			break;
		}
		nr_frames += 1;
	}
	if nr_frames < 1 {
		return wurs::wuerr_here!(wu::wu_no_image_data, "no frames");
	}

	img.w = w as usize;
	img.h = h as usize;
	img.channels = 4;
	img.bitdepth = 8;
	if let Some(icc) = ds.decoder.icc_profile() {
		let ok = unsafe {
			wu::color_space_set_icc_copy(&mut img.cs,
				icc.as_ptr() as *const std::ffi::c_void,
				icc.len())
		};
		if !ok {
			msg = Some(c"failed to set icc profile, will ignore");
		}
	}
	match img.frames_init(nr_frames) {
		Some(_) => {},
		None => return wurs::wuerr_here!(wu::wu_alloc_error),
	};
	let frames = unsafe {
		let frames = &mut *img.frames;
		frames.f.as_mut_slice(frames.nr)
	};

	/* Do a second pass, this time getting timing info and calculating
	 * frame change regions (different from frame sizes due to disposal) */
	ds.decoder = match build_gif_decoder(infile.get_map(), true) {
		Ok(d) => d,
		Err(e) => return e,
	};
	let mut restore_w = 0;
	let mut restore_h = 0;
	let mut max_w = 0;
	let mut max_h = 0;
	let mut dispose: Option<wu::compost> = None;
	let mut i = 0;
	while let Ok(Some(fr)) = ds.decoder.next_frame_info() {
		let cur = gif_frame_to_compost(fr);
		let mut reg = if i == 0 {
			wu::compost {x: 0, y: 0, w: img.w, h: img.h}
		} else {
			cur
		};
		if let Some(dreg) = dispose {
			let x1 = std::cmp::max(reg.x + reg.w, dreg.x + dreg.w);
			let y1 = std::cmp::max(reg.y + reg.h, dreg.y + dreg.h);
			reg.x = std::cmp::min(reg.x, dreg.x);
			reg.y = std::cmp::min(reg.y, dreg.y);
			reg.w = x1 - reg.x;
			reg.h = y1 - reg.y;
		}
		frames[i] = wu::frame_info {
			reg: reg,
			sec: wu::frame_time {num: fr.delay as u32, den: 100},
			keyframe: i == 0,
		};

		max_w = std::cmp::max(max_w, fr.width);
		max_h = std::cmp::max(max_h, fr.height);
		let is_previous = fr.dispose == gif::DisposalMethod::Previous;
		if is_previous || fr.dispose == gif::DisposalMethod::Background {
			if is_previous {
				restore_w = std::cmp::max(restore_w, fr.width);
				restore_h = std::cmp::max(restore_h, fr.height);
			}
			dispose = Some(cur);
		} else {
			dispose = None;
		}
		i += 1;
	}

	// Alloc buffer for decoded frames and Previous disposal restoration
	let restore_size = checked_mul(restore_w, restore_h, img.channels);
	let max_frame_size = checked_mul(max_w, max_h, 1);
	let buf_size = restore_size.0.overflowing_add(max_frame_size.0);
	if restore_size.1 | max_frame_size.1 | buf_size.1 {
		return wurs::wuerr_here!(wu::wu_int_overflow);
	}
	match ds.buf.try_reserve_exact(buf_size.0) {
		Ok(_) => {
			ds.buf.extend(std::iter::repeat_n(0, buf_size.0));
			ds.frame_area_size = max_frame_size.0;
		},
		Err(_) => return wurs::wuerr_here!(wu::wu_alloc_error),
	};
	wu::wu_st {
		st: wu::wu_ok,
		msg: if let Some(m) = msg { m.as_ptr() } else { std::ptr::null() },
	}
}

fn get_dec_state<'a>(infile: &wu::image_file) -> *mut GifState<'a> {
	infile.dec_state as *mut _
}

extern "C" fn end_gif(infile_ptr: *mut wu::image_file) {
	let infile = unsafe {&mut *infile_ptr};
	let _ = unsafe { std::boxed::Box::from_raw(get_dec_state(infile)) };
	infile.dec_state = std::ptr::null_mut();
}

extern "C" fn event_gif(infile_ptr: *mut wu::image_file,
state: *mut wu::wu_state, ev: wu::image_event) -> wu::wu_st {
	let infile = unsafe {&mut *infile_ptr};
	let state = unsafe {&mut *state};
	let img = unsafe {&mut *(infile.sub_img)};
	let ds = unsafe {&mut *get_dec_state(infile)};

	match ev {
		wu::ev_metadata => {
			return gather_gif_info(img, infile, ds);
		},
		wu::ev_subcycle | wu::ev_frame => {
			let frames = unsafe {&mut *img.frames};
			if ev == wu::ev_subcycle || state.frame < frames.current {
				if state.frame < frames.current {
					img.get_data().fill(0);
				}
				ds.decoder = match build_gif_decoder(infile.get_map(), false) {
					Ok(d) => d,
					Err(e) => return e,
				};
				ds.prev = Default::default();
				frames.current = -1;
			}
			let mut st = wu::wu_st::ok();
			while frames.current < state.frame {
				st = compost_frame(img, frames, ds);
				if !st.isok() {
					return st;
				}
			}
			return st;
		},
		_ => {},
	};
	wu::wu_st {
		st: wu::wu_no_change,
		msg: std::ptr::null(),
	}
}

extern "C" fn init_gif(infile_ptr: *mut wu::image_file) -> wu::wu_st {
	let infile = unsafe {&mut *infile_ptr};
	let ds = std::boxed::Box::new(GifState {
		decoder: match build_gif_decoder(infile.get_map(), true) {
			Ok(d) => d,
			Err(e) => return e,
		},
		prev: Default::default(),
		buf: Default::default(),
		frame_area_size: Default::default(),
		pal: [[0; 4]; 256],
	});
	if let (Some(pal), Some(bg)) = (ds.decoder.global_palette(), ds.decoder.bg_color()) {
		if let Some(c) = pal.get(bg*3..bg*3+3) {
			infile.bg.r = c[0];
			infile.bg.g = c[1];
			infile.bg.b = c[2];
		}
	}
	if let Some(xmp) = ds.decoder.xmp_metadata() {
		infile.metadata.parse(wu::metadata_xmp, xmp);
	}
	infile.dec_state = std::boxed::Box::into_raw(ds) as *mut std::ffi::c_void;
	wu::wu_st::ok()
}

#[no_mangle]
pub static gif_fn: wu::image_fn = wu::image_fn {
	mmap: true,
	alloc_on_subcycle: true,
	alloc_single: true,
	init: Some(init_gif),
	event: Some(event_gif),
	end: Some(end_gif),

	state_size: 0,
};
