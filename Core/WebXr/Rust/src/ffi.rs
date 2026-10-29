//! C ABI consumed by BabylonNative's NativeXr plugin (see Core/WebXr/Include/Babylon/WebXr/Core.h).
//! Strings cross as UTF-8 pointer + length; the plugin never owns memory allocated here.

use std::slice;

use crate::features::{Feature, SessionMode};

fn str_from_raw<'a>(ptr: *const u8, len: usize) -> Option<&'a str> {
    if ptr.is_null() {
        return None;
    }
    // SAFETY: the caller guarantees `ptr` points to `len` readable bytes for the duration of the call.
    let bytes = unsafe { slice::from_raw_parts(ptr, len) };
    std::str::from_utf8(bytes).ok()
}

/// Returns true when `name` is a WebXR session mode and stores its numeric value
/// (0 inline, 1 immersive-vr, 2 immersive-ar) in `out_mode`.
#[no_mangle]
pub extern "C" fn babylon_webxr_session_mode_parse(
    name: *const u8,
    len: usize,
    out_mode: *mut u32,
) -> bool {
    let Some(mode) = str_from_raw(name, len).and_then(SessionMode::parse) else {
        return false;
    };
    if !out_mode.is_null() {
        // SAFETY: the caller passes a valid, writable u32.
        unsafe { *out_mode = mode as u32 };
    }
    true
}

/// Returns true when `name` is a feature descriptor defined by the vendored WebXR specifications
/// and stores its index in [`Feature::ALL`] in `out_feature`.
#[no_mangle]
pub extern "C" fn babylon_webxr_feature_parse(
    name: *const u8,
    len: usize,
    out_feature: *mut u32,
) -> bool {
    let Some(feature) = str_from_raw(name, len).and_then(Feature::parse) else {
        return false;
    };
    if !out_feature.is_null() {
        let index = Feature::ALL.iter().position(|f| *f == feature).unwrap_or(0) as u32;
        // SAFETY: the caller passes a valid, writable u32.
        unsafe { *out_feature = index };
    }
    true
}

/// Number of feature descriptors; indices below it are valid for [`babylon_webxr_feature_name`].
#[no_mangle]
pub extern "C" fn babylon_webxr_feature_count() -> u32 {
    Feature::ALL.len() as u32
}

/// The descriptor string for a feature index, as a NUL-terminated static string, or null when out of range.
#[no_mangle]
pub extern "C" fn babylon_webxr_feature_name(index: u32) -> *const std::os::raw::c_char {
    const NAMES: [&[u8]; 17] = [
        b"viewer\0",
        b"local\0",
        b"local-floor\0",
        b"bounded-floor\0",
        b"unbounded\0",
        b"anchors\0",
        b"depth-sensing\0",
        b"dom-overlay\0",
        b"hand-tracking\0",
        b"hit-test\0",
        b"layers\0",
        b"light-estimation\0",
        b"mesh-detection\0",
        b"plane-detection\0",
        b"camera-access\0",
        b"secondary-views\0",
        b"webgpu\0",
    ];
    match NAMES.get(index as usize) {
        Some(name) => name.as_ptr() as *const std::os::raw::c_char,
        None => std::ptr::null(),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn names_match_feature_table() {
        for (index, feature) in Feature::ALL.iter().enumerate() {
            let ptr = babylon_webxr_feature_name(index as u32);
            let name = unsafe { std::ffi::CStr::from_ptr(ptr) }.to_str().unwrap();
            assert_eq!(name, feature.as_str());
            let mut out = u32::MAX;
            assert!(babylon_webxr_feature_parse(
                name.as_ptr(),
                name.len(),
                &mut out
            ));
            assert_eq!(out as usize, index);
        }
        assert!(babylon_webxr_feature_name(babylon_webxr_feature_count()).is_null());
        assert!(!babylon_webxr_feature_parse(
            std::ptr::null(),
            0,
            std::ptr::null_mut()
        ));
    }
}
