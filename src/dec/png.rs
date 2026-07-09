// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2026 kaleido
use crate::wu;
use crate::wurs;

// Disclaimer: I'm a rust n00b

use std::io::{Seek, SeekFrom};

/* Reader that fakes the file signature, allowing MalieGF files to decode.
 * Essentially a seekable chain(Cursor, Cursor) */
struct FakeSig {
	sig: &'static [u8],
	map: &'static [u8],
	pos: usize,
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
				icc.as_ptr() as *const std::ffi::c_void,
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
				return wurs::wuerr_here!(wu::wu_alloc_error,
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
				return wurs::wuerr_here!(wu::wu_alloc_error,
					"failed to set chromacities");
			}
		}		
	}

	return wu::wu_st::ok();
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
	}

	return wu::wu_st::ok();
}

fn get_dec_state(infile: &mut wu::image_file) -> *mut png::Reader<FakeSig> {
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
	let reader = unsafe {&mut *get_dec_state(infile)};
	let info = reader.info();

	match ev {
		wu::ev_metadata => {
			let mut st = get_png_colorspace(img, info);
			if !st.isok() {
				return st;
			}
			if state.idx == 0 {
				img.w = info.width as usize;
				img.h = info.height as usize;
				img.channels = info.color_type.samples() as u8;
				img.bitdepth = info.bit_depth as u8;
				if info.color_type == png::ColorType::Indexed {
					st = get_png_palette(img, info);
					if !st.isok() {
						return st;
					}
				}
			} else {
				img.w = 16;
				img.h = 16;
				img.channels = 3 + info.trns.is_some() as u8;
				img.bitdepth = 8;
			}
			return st;
		},
		wu::ev_subcycle => {
			let data = img.get_data();
			if state.idx == 0 {
				if let Err(_) = reader.next_frame(data) {
					return wurs::wuerr_here!(wu::wu_decoding_error,
						"next_frame() failed");
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
				get_png_metadata(infile, img, reader.info());
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
				return wurs::wuerr_here!(wu::wu_decoding_error,
					"palette disappeared");
			}
			return wu::wu_st::ok();
		},
		_ => {},
	};
	wu::wu_st {
		st: wu::wu_no_change,
		msg: std::ptr::null(),
	}
}

extern "C" fn init_png(infile_ptr: *mut wu::image_file) -> wu::wu_st {
	let infile = unsafe {&mut *infile_ptr};
	let sig = b"\x89PNG\r\n\x1a\n";
	let map = infile.get_map();
	let decoder = png::Decoder::new(FakeSig::new(sig, map));
	let reader = match decoder.read_info() {
		Ok(r) => std::boxed::Box::new(r),
		Err(_) => {
			return wurs::wuerr_here!(wu::wu_invalid_header,
				"failed to read png info");
		}
	};

	// Non-indexed images may still include a palette. If so, show it.
	let info = reader.info();
	let opt_pal = info.color_type != png::ColorType::Indexed && info.palette.is_some();
	infile.nr = 1 + opt_pal as usize;

	infile.dec_state = std::boxed::Box::into_raw(reader) as *mut std::ffi::c_void;
	wu::wu_st::ok()
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
