// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2026 kaleido
use crate::wu;
use crate::wurs;

// Disclaimer: I'm a rust n00b

/* Test images:
https://philip.html5.org/tests/apng/tests.html
 * Some would seem to be rendered wrong, but that's because we enforce a
 * minimum frame delay (we don't hide frames), and we ignore loop limits
 * (we're not a browser).
 * TODO:
 * - Test APNG with RGB and Grayscale
 * - Native 16-bit APNG (currently converted to 8-bit by image-png)
 * - Alpha blending in linear light? libwebp doesn't seem to bother and no
 *   one seems to complain, sooo...
*/

use std::default::Default;
use std::io::{Seek, SeekFrom};

/* Reader that fakes the file signature, allowing MalieGF files to decode.
 * Essentially a seekable chain(Cursor, Cursor) */
struct FakeSig {
	sig: &'static [u8],
	map: &'static [u8],
	pos: usize,
}

struct PngPrev {
	dispose: png::DisposeOp,
	frame: wu::compost,
}

struct PngIdx {
	apng: std::ffi::c_int,
	default: std::ffi::c_int,
	pal: std::ffi::c_int,
}

struct PngState {
	reader: png::Reader<FakeSig>,
	prev: PngPrev,
	buf: Vec<u8>,
	restore_size: usize,
	idx: PngIdx,
}

impl FakeSig {
	fn new(sig: &'static [u8], map: &'static [u8]) -> FakeSig {
		FakeSig{sig, map: &map[sig.len()..], pos: 0}
	}
}

impl std::io::Seek for FakeSig {
	fn seek(&mut self, pos: SeekFrom) -> Result<u64, std::io::Error> {
		let max = self.sig.len() + self.map.len();
		self.pos = match pos {
			SeekFrom::Start(i) => i as usize,
			SeekFrom::End(i) => ((max as i64) + i) as usize,
			SeekFrom::Current(i) => ((self.pos as i64) + i) as usize,
		};
		self.pos = std::cmp::min(self.pos, max);
		return Ok(self.pos as u64);
	}
}

fn slice_copy(dst: &mut [u8], src: &[u8]) -> usize {
	let len = std::cmp::min(dst.len(), src.len());
	dst[..len].copy_from_slice(&src[..len]);
	len
}

impl std::io::Read for FakeSig {
	fn read(&mut self, buf: &mut [u8]) -> Result<usize, std::io::Error> {
		let mut start = self.pos;
		let mut r = 0;
		if start < self.sig.len() {
			r += slice_copy(buf, &self.sig[start..]);
			start = 0;
		}
		r += slice_copy(&mut buf[r..], &self.map[start..]);
		self.pos += r;
		return Ok(r);
	}
}

impl std::io::BufRead for FakeSig {
	fn fill_buf(&mut self) -> Result<&[u8], std::io::Error> {
		let s = if self.pos < self.sig.len() {
			&self.sig[self.pos..]
		} else {
			&self.map[self.pos - self.sig.len()..]
		};
		Ok(s)
	}

	fn consume(&mut self, i: usize) {
		let _ = self.seek(SeekFrom::Current(i as i64));
	}
}


fn add_text(infile: &mut wu::image_file, key: &str, val: &str) {
	if let Ok(ckey) = std::ffi::CString::new(key) {
		let val_wuptr = wu::wuptr {
			ptr: val.as_ptr(),
			len: val.len(),
		};
		unsafe {
			wu::tree_add_leaf_utf8_len(&mut infile.metadata,
				ckey.as_ptr(), val_wuptr);
		};
	}
}

fn get_png_metadata(infile: &mut wu::image_file, img: &mut wu::wuimg,
info: &png::Info) {
	info.utf8_text.iter().for_each(
		|text| {
			if let Ok(val) = text.get_text() {
				add_text(infile, &text.keyword, &val);
			}
		}
	);
	info.uncompressed_latin1_text.iter().for_each(
		|text| add_text(infile, &text.keyword, &text.text)
	);
	info.compressed_latin1_text.iter().for_each(
		|text| {
			if let Ok(val) = text.get_text() {
				add_text(infile, &text.keyword, &val);
			}
		}
	);
	if let Some(exif) = &info.exif_metadata {
		infile.metadata.parse(wu::metadata_exif, exif);
	}
	if let Some(bkgd) = &info.bkgd {
		match bkgd.len() {
			1 => {
				if img.mode() == wu::image_mode_palette {
					infile.bg = unsafe {
						(*img.u.palette).color[bkgd[0] as usize]
					};
				}
			},
			2 => {
				infile.bg.r = bkgd[0];
				infile.bg.g = bkgd[0];
				infile.bg.b = bkgd[0];
			},
			6 => {
				infile.bg.r = bkgd[0];
				infile.bg.g = bkgd[2];
				infile.bg.b = bkgd[4];
			},
			_ => {}
		}
	}
}

fn get_png_colorspace(img: &mut wu::wuimg, info: &png::Info) -> wu::wu_st {
	if let Some(cicp) = info.coding_independent_code_points {
		img.cs.set_primaries(cicp.color_primaries.into());
		img.cs.set_transfer(cicp.transfer_function.into());
		img.cs.set_matrix(cicp.matrix_coefficients.into());
		img.cs.set_limited(!cicp.is_video_full_range_image);
		return wu::wu_st::ok();
	}

	if let Some(icc) = &info.icc_profile {
		unsafe {
			let ok = wu::color_space_set_icc_copy(&mut img.cs,
				icc.as_ptr() as *const _,
				icc.len());
			if ok {
				return wu::wu_st::ok();
			}
		};
	}

	if let Some(_) = info.srgb {
		return wu::wu_st::ok();
	}

	if let Some(gama) = info.gama_chunk {
		unsafe {
			let ok = wu::color_space_set_gamma(&mut img.cs,
				1.0 / (gama.into_value() as f64));
			if !ok {
				return wurs::wuerr!(wu::wu_alloc_error,
					"failed to set gamma");
			}
		}
	}

	if let Some(chrm) = info.chrm_chunk {
		unsafe {
			let ok = wu::color_space_set_primaries(&mut img.cs,
				chrm.white.0.into_value() as f64,
				chrm.white.1.into_value() as f64,
				chrm.red.0.into_value() as f64,
				chrm.red.1.into_value() as f64,
				chrm.green.0.into_value() as f64,
				chrm.green.1.into_value() as f64,
				chrm.blue.0.into_value() as f64,
				chrm.blue.1.into_value() as f64);
			if !ok {
				return wurs::wuerr!(wu::wu_alloc_error,
					"failed to set chromacities");
			}
		}		
	}
	wu::wu_st::ok()
}

fn get_png_palette(img: &mut wu::wuimg, info: &png::Info) -> wu::wu_st {
	if let Some(palette) = &info.palette {
		let st = unsafe {
			wu::wuimg_palette_from_buf(img, 3, palette.len()/3,
				palette.as_ptr())
		};
		if !st.isok() {
			return st;
		}
		if let Some(trns) = &info.trns {
			let pal = unsafe { &mut *img.u.palette };
			for i in 0..std::cmp::min(pal.color.len(), trns.len()) {
				pal.color[i].a = trns[i];
			}
		}
		return wu::wu_st::ok()
	}
	wurs::wuerr!(wu::wu_invalid_header, "indexed image but palette is missing")
}

fn png_frame_to_compost(fr: &png::FrameControl) -> wu::compost {
	wu::compost {
		x: fr.x_offset as usize, y: fr.y_offset as usize,
		w: fr.width as usize, h: fr.height as usize,
	}
}

fn checked_mul(w: usize, h: usize, ch: u8) -> (usize, bool) {
	let m1 = (w as usize).overflowing_mul(h as usize);
	let m2 = m1.0.overflowing_mul(ch as usize);
	(m2.0, m2.1 | m1.1)
}

fn get_png_frame_info(img: &mut wu::wuimg, i: usize,
fr: &png::FrameControl, restore_w: &mut u32, restore_h: &mut u32,
den: &mut u16, den_changes: &mut usize) -> wu::wu_st {
	let reg = png_frame_to_compost(fr);
	let ok = unsafe {
		wu::wuimg_anim_frame_set_checked(img, i, &reg,
			fr.blend_op == png::BlendOp::Source)
	};
	if !ok {
		return wurs::wuerr!(wu::wu_invalid_header,
			"frame out of bounds");
	}
	if fr.dispose_op == png::DisposeOp::Previous {
		*restore_w = std::cmp::max(*restore_w, fr.width);
		*restore_h = std::cmp::max(*restore_h, fr.height);
	}
	*den_changes += (*den != fr.delay_den) as usize;
	*den = fr.delay_den;
	wu::wu_st::ok()
}

fn get_png_anim_info(infile: &wu::image_file, img: &mut wu::wuimg,
ds: &mut PngState) -> wu::wu_st {
	ds.reader = match build_png_reader(infile, false) {
		Ok(r) => r,
		Err(e) => return e,
	};
	let anim = unsafe {&mut *img.anim};
	let mut restore_w = 0;
	let mut restore_h = 0;
	let mut den = 0;
	let mut den_changes = 0;
	let mut i = 0;
	if ds.idx.default == -1 {
		if let Some(fr) = ds.reader.info().frame_control {
			let st = get_png_frame_info(img, i, &fr,
				&mut restore_w, &mut restore_h,
				&mut den, &mut den_changes);
			if !st.isok() {
				return st;
			}
			i += 1;
		} else {
			return wurs::wuerr!(wu::wu_invalid_params,
				"failed to skip default apng image");
		}
				
	}
	while let Ok(fr) = ds.reader.next_frame_info() {
		if i >= anim.nr {
			break;
		}
		let st = get_png_frame_info(img, i, fr,
			&mut restore_w, &mut restore_h,
			&mut den, &mut den_changes);
		if !st.isok() {
			return st;
		}
		i += 1;
	}
	anim.nr = i;
	anim.varying_den = den_changes > 1;

	let restore_size = checked_mul(restore_w as usize, restore_h as usize,
		img.channels);
	let frame_size = checked_mul(img.w, img.h, img.channels);
	let buf_size = restore_size.0.overflowing_add(frame_size.0);
	if restore_size.1 | frame_size.1 | buf_size.1 {
		return wurs::wuerr_here!(wu::wu_int_overflow);
	}
	match ds.buf.try_reserve_exact(buf_size.0) {
		Ok(_) => {
			ds.buf.extend(std::iter::repeat_n(0, buf_size.0));
			ds.restore_size = restore_size.0;
		},
		Err(_) => return wurs::wuerr_here!(wu::wu_alloc_error),
	};
	wu::wu_st::ok()
}

fn build_png_reader(infile: &wu::image_file, apng: bool)
-> Result<png::Reader<FakeSig>, wu::wu_st> {
	let sig = b"\x89PNG\r\n\x1a\n";
	let map = infile.get_map();
	let mut decoder = png::Decoder::new(FakeSig::new(sig, map));
	decoder.set_transformations(if apng {
		png::Transformations::ALPHA | png::Transformations::STRIP_16
	} else {
		png::Transformations::IDENTITY
	});
	match decoder.read_info() {
		Ok(r) => Ok(r),
		Err(_) => Err(wurs::wuerr!(wu::wu_invalid_header,
			"failed to read png info")),
	}
}

fn render_png_frame(infile: &wu::image_file, img: &mut wu::wuimg,
state: &wu::wu_state, ds: &mut PngState, ev: wu::image_event) -> wu::wu_st {
	let anim = unsafe{&mut *img.anim};
	anim.dt = Default::default();
	let skip_first = ds.idx.default != -1;
	if ev == wu::ev_subcycle || img.anim_seek_nearest(state.frame) {
		ds.reader = match build_png_reader(infile, true) {
			Ok(r) => r,
			Err(e) => return e,
		};
		for _ in -1..(anim.cur - 1 + skip_first as std::ffi::c_int) {
			let _ = ds.reader.next_frame_info();
		}
		anim.dt.w = img.w;
		anim.dt.h = img.h;
	}
	let add_alpha = match ds.reader.output_color_type().0 {
		png::ColorType::Rgb | png::ColorType::Grayscale => true,
		_ => false,
	};
	while anim.cur < state.frame {
		anim.cur += 1;
		let frame = if anim.cur == 0 && !skip_first {
			&match ds.reader.info().frame_control {
				Some(f) => f,
				None => return wurs::wuerr!(wu::wu_decoding_error,
					"missing frame control chunk"),
			}
		} else {
			match ds.reader.next_frame_info() {
				Ok(f) => f,
				Err(_) => {
					return wurs::wuerr!(wu::wu_decoding_error,
						"unexpected next_frame_info() failure");
				}
			}
		};
		let cur = png_frame_to_compost(frame);
		let dispose = frame.dispose_op;
		let blend = frame.blend_op;
		anim.sec.num = frame.delay_num as u32;
		anim.sec.den = if frame.delay_den == 0 {
			100
		} else {
			frame.delay_den as u32
		};

		let (restore, buf) = ds.buf.split_at_mut(ds.restore_size);
		let direct_write = anim.is_keyframe() && !add_alpha;
		if !direct_write {
			if anim.cur == 0 {
				img.get_data().fill(0);
			} else {
				let p = ds.prev.frame;
				match ds.prev.dispose {
					png::DisposeOp::None => {},
					png::DisposeOp::Background => {
						p.clear(img);
						anim.dt.affect(&p);
					},
					png::DisposeOp::Previous => {
						p.overwrite(img, restore);
						anim.dt.affect(&p);
					},
				};
			}
		}
		anim.dt.affect(&cur);
		ds.prev.frame = cur;
		ds.prev.dispose = dispose;
		if dispose == png::DisposeOp::Previous {
			cur.extract(restore, img);
		}

		/* image-png wants the target buffer to always have the size of
		 * a full frame. Bummer. */
		let dst = if direct_write {
			img.get_data()
		} else {
			buf
		};
		if let Err(_) = ds.reader.next_frame(dst) {
			return wurs::wuerr!(wu::wu_decoding_error,
				"failed to decode frame");
		};
		if !direct_write {
			if add_alpha {
				cur.overwrite_add_alpha(img, dst);
			} else {
				match blend {
					png::BlendOp::Source => cur.overwrite(img, dst),
					png::BlendOp::Over => cur.alpha_blend(img, dst),
				};
			}
		}
	}
	wu::wu_st::ok()
}

fn get_dec_state(infile: &mut wu::image_file) -> *mut PngState {
	infile.dec_state as *mut _
}

extern "C" fn end_png(infile_ptr: *mut wu::image_file) {
	let infile = unsafe {&mut *infile_ptr};
	let _ = unsafe { std::boxed::Box::from_raw(get_dec_state(infile)) };
	infile.dec_state = std::ptr::null_mut();
}

extern "C" fn event_png(infile_ptr: *mut wu::image_file,
state: *mut wu::wu_state, ev: wu::image_event) -> wu::wu_st {
	let infile = unsafe {&mut *infile_ptr};
	let state = unsafe {&mut *state};
	let img = unsafe {&mut *(infile.sub_img.add(state.idx as usize))};
	let ds = unsafe {&mut *get_dec_state(infile)};
	let info = ds.reader.info();

	match ev {
		wu::ev_metadata => {
			let mut st = get_png_colorspace(img, info);
			if !st.isok() {
				return st;
			}
			if state.idx == ds.idx.pal {
				img.w = 16;
				img.h = 16;
				img.channels = 3 + info.trns.is_some() as u8;
				img.bitdepth = 8;
				return st;
			}
			img.w = info.width as usize;
			img.h = info.height as usize;
			img.channels = info.color_type.samples() as u8;
			img.bitdepth = info.bit_depth as u8;
			if state.idx == ds.idx.apng {
				if let Some(actl) = info.animation_control {
					img.channels = if img.channels > 2 {4} else {2};
					img.bitdepth = 8;
					if img.anim_init(actl.num_frames as usize, 0, 0).is_none() {
						return wurs::wuerr_here!(wu::wu_alloc_error);
					}
					st = get_png_anim_info(infile, img, ds);
					if !st.isok() {
						return st;
					}
				} else {
					return wurs::wuerr!(wu::wu_invalid_params,
						"image metadata changed");
				}
			} else {
				if info.color_type == png::ColorType::Indexed {
					st = get_png_palette(img, info);
					if !st.isok() {
						return st;
					}
				}
			}
			if state.idx == 0 {
				let _ = ds.reader.finish();
				get_png_metadata(infile, img, ds.reader.info());
			}
			return st;
		},
		wu::ev_subcycle | wu::ev_frame => {
			let data = img.get_data();
			if state.idx == ds.idx.apng {
				return render_png_frame(infile, img, state, ds, ev);
			} else if state.idx == ds.idx.default {
				ds.reader = match build_png_reader(infile, false) {
					Ok(r) => r,
					Err(e) => return e,
				};
				if let Err(_) = ds.reader.next_frame(data) {
					return wurs::wuerr!(wu::wu_decoding_error,
						"next_frame() failed for static image");
				}
				// SWAP_ENDIAN currently not implemented by the crate
				if img.bitdepth == 16 {
					unsafe {
						wu::endian_loop16(
							img.data as *mut u16,
							wu::big_endian,
							data.len()/2);
					}
				}
			} else if let Some(plte) = &info.palette {
				if let Some(trns) = &info.trns {
					data.as_chunks_mut::<4>().0.into_iter().zip(
						plte.as_ref().as_chunks::<3>().0.into_iter().zip(trns.as_ref())
					).for_each(|(dst, (rgb, a))| {
						dst[..rgb.len()].copy_from_slice(rgb);
						dst[rgb.len()] = *a;
					});
				} else {
					slice_copy(data, plte);
				}
			} else {
				return wurs::wuerr!(wu::wu_decoding_error,
					"palette disappeared");
			}
			return wu::wu_st::ok();
		},
		_ => {},
	};
	wu::wu_st::no_change()
}

extern "C" fn init_png(infile_ptr: *mut wu::image_file) -> wu::wu_st {
	let infile = unsafe {&mut *infile_ptr};
	let mut ds = std::boxed::Box::new(PngState {
		reader: match build_png_reader(infile, false) {
			Ok(r) => r,
			Err(e) => return e,
		},
		prev: PngPrev {
			dispose: png::DisposeOp::None,
			frame: Default::default(),
		},
		buf: Default::default(),
		restore_size: Default::default(),
		idx: PngIdx {
			apng: -1,
			default: -1,
			pal: -1,
		},
	});

	let info = ds.reader.info();
	let max = unsafe {&*infile.conf}.max_img_size as usize;
	if std::cmp::max(info.width, info.height) as usize > max {
		return wurs::wuerr_here!(wu::wu_exceeds_size_limit);
	}

	// Order sub-images according to role
	let mut st = wu::wu_st::ok();
	let mut idx = 0;
	if info.animation_control.is_some() {
		ds.idx.apng = idx;
		// Default image, meant for viewers that don't support APNG
		if info.frame_control.is_none() {
			idx += 1;
			ds.idx.default = idx;
		};
	} else {
		if info.animation_control.is_some() {
			st = wurs::wuerr!(wu::wu_ok,
				"apng with color-type unsupported by renderer, \
					will ignore");
		}
		ds.idx.default = idx;
	}
	// Suggested palette for non-indexed images
	let opt_pal = info.color_type != png::ColorType::Indexed
		&& info.palette.is_some();
	if opt_pal {
		idx += 1;
		ds.idx.pal = idx;
	}
	infile.nr = idx as usize + 1;

	infile.dec_state = std::boxed::Box::into_raw(ds) as *mut _;
	st
}

#[no_mangle]
pub static png_fn: wu::image_fn = wu::image_fn {
	mmap: true,
	alloc_on_subcycle: true,
	init: Some(init_png),
	event: Some(event_png),
	end: Some(end_png),

	alloc_single: false,
	state_size: 0,
};
