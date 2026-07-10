// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2026 kaleido

// Convenience wrappers around wu.rs

use crate::wu;

impl wu::wu_st {
	pub fn ok() -> wu::wu_st {
		wu::wu_st {
			st: wu::wu_ok,
			msg: std::ptr::null(),
		}
	}

	pub fn isok(self) -> bool {
		self.st == wu::wu_ok
	}
}

impl wu::wuimg {
	pub fn get_data(&self) -> &mut [u8] {
		unsafe {
			std::slice::from_raw_parts_mut(self.data, wu::wuimg_size(self))
		}
	}

	pub fn frames_init<'a>(&'a mut self, nr: usize) -> Option<&'a mut wu::image_frames> {
		unsafe {
			wu::wuimg_frames_init(self, nr).as_mut()
		}
	}

	pub fn compost_clear(&self, fill: u8, reg: &wu::compost) {
		unsafe {
			wu::compost_clear(self.data as *mut _, self.w,
				self.channels, fill as std::ffi::c_int, reg);
		}
	}

	pub fn compost_overwrite(&self, src: &[u8], reg: &wu::compost) {
		unsafe {
			wu::compost_overwrite(self.data as *mut _, self.w,
				self.channels, src.as_ptr() as *const _, reg);
		}
	}

	pub fn compost_extract(&self, dst: &mut [u8], reg: &wu::compost) {
		unsafe {
			wu::compost_extract(dst.as_mut_ptr() as *mut _, reg,
				self.data as *mut _, self.w, self.channels);
		}
	}
}

impl wu::image_file {
	pub fn get_map<'a>(&self) -> &'a [u8] {
		unsafe {
			std::slice::from_raw_parts(self.map.ptr, self.map.len)
		}
	}
}

macro_rules! wuerr_here {
	($st:expr) => {
		wu::wu_st {
			st: $st,
			msg: concat!(file!(), ":", line!(), "\0").as_ptr() as *const std::ffi::c_char,
		}
	};

	($st:expr, $msg:literal) => {
		wu::wu_st {
			st: $st,
			msg: concat!(file!(), ":", line!(), ": ", $msg, "\0").as_ptr() as *const std::ffi::c_char,
		}
	};
}

pub(crate) use wuerr_here;

impl wu::wutree {
	pub fn parse(&mut self, which: wu::metadata_type, data: &[u8]) -> bool {
		unsafe {
			wu::metadata_parse(which,
				data.as_ptr() as *const std::ffi::c_void,
				data.len(), self)
		}
	}
}
