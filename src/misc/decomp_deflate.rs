// SPDX-License-Identifier: 0BSD
// SPDX-FileCopyrightText: 2026 kaleido

/* Deflate using fdeflate. Performance is supposedly close to zlib-rs, but the
 * real reason we prefer it is because png uses it, so we might as well. */

use fdeflate;

#[no_mangle]
pub extern "C" fn decomp_deflate(dst_ptr: *mut u8, dst_len: usize,
src_ptr: *const u8, src_len: usize) -> usize {
	let (dst, src) = unsafe {
		(std::slice::from_raw_parts_mut(dst_ptr, dst_len),
			std::slice::from_raw_parts(src_ptr, src_len))
	};
	let mut dcmp = fdeflate::Decompressor::new();
	if let Ok((_read, written)) = dcmp.read(src, dst, 0, false) {
		return written;
	}
	0
}
