//! Describe the native compiler invocation, not just the JS request that scheduled it.
use std::sync::{
    OnceLock,
    atomic::{AtomicU64, Ordering},
};

#[cfg(target_os = "android")]
mod platform {
    use std::ffi::{CString, c_char};
    #[link(name = "log")]
    unsafe extern "C" {
        fn __android_log_is_loggable(priority: i32, tag: *const c_char, fallback: i32) -> i32;
        fn __android_log_write(priority: i32, tag: *const c_char, message: *const c_char) -> i32;
    }
    const DEBUG: i32 = 3;
    const INFO: i32 = 4;
    pub(super) fn enabled() -> bool {
        unsafe { __android_log_is_loggable(DEBUG, c"BabylonPipeline".as_ptr(), INFO) != 0 }
    }
    pub(super) fn emit(line: &str) {
        if let Ok(line) = CString::new(line) {
            unsafe {
                __android_log_write(DEBUG, c"BabylonPipeline".as_ptr(), line.as_ptr());
            }
        }
    }
}

#[cfg(not(target_os = "android"))]
mod platform {
    pub(super) fn enabled() -> bool {
        std::env::var_os("BABYLON_NATIVE_PIPELINE_BREADCRUMBS").is_some_and(|v| v == "1")
    }
    pub(super) fn emit(line: &str) {
        eprintln!("BabylonPipeline: {line}");
    }
}

pub(super) fn enabled() -> bool {
    static ENABLED: OnceLock<bool> = OnceLock::new();
    *ENABLED.get_or_init(platform::enabled)
}

pub(super) fn shader(id: u64, source: &str) {
    if enabled() {
        // FNV-1a is an identification hint for comparing known WGSL variants, not an integrity check.
        let fingerprint = source.bytes().fold(0xcbf29ce484222325u64, |h, b| {
            (h ^ u64::from(b)).wrapping_mul(0x100000001b3)
        });
        platform::emit(&format!(
            "shader={id} bytes={} fnv1a64={fingerprint:016x}",
            source.len()
        ));
    }
}

pub(super) fn compile<T>(descriptor: Option<&str>, create: impl FnOnce() -> T) -> T {
    let Some(descriptor) = descriptor else {
        return create();
    };
    static NEXT: AtomicU64 = AtomicU64::new(1);
    trace(
        NEXT.fetch_add(1, Ordering::Relaxed),
        descriptor,
        platform::emit,
        create,
    )
}

fn trace<T>(id: u64, mut descriptor: &str, emit: impl Fn(&str), create: impl FnOnce() -> T) -> T {
    let bytes = descriptor.len();
    let mut offset = 0;
    // Stay below logcat's per-entry byte limit; never split a UTF-8 label.
    while !descriptor.is_empty() {
        let mut n = descriptor.len().min(3000);
        while !descriptor.is_char_boundary(n) {
            n -= 1;
        }
        emit(&format!(
            "pipeline={id} offset={offset} {}",
            &descriptor[..n]
        ));
        descriptor = &descriptor[n..];
        offset += n;
    }
    emit(&format!("pipeline={id} begin bytes={bytes}"));
    let result = create();
    emit(&format!("pipeline={id} returned"));
    result
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::cell::RefCell;

    #[test]
    fn compile_is_bracketed_on_the_calling_thread_and_descriptor_is_lossless() {
        let descriptor = format!("{}{}{}", "a".repeat(2999), '\u{1f642}', "b".repeat(3088));
        let lines = RefCell::new(Vec::new());
        let result = trace(
            7,
            &descriptor,
            |s| lines.borrow_mut().push(s.to_owned()),
            || {
                assert!(lines.borrow().last().unwrap().contains(" begin "));
                42
            },
        );
        assert_eq!(result, 42);
        let lines = lines.into_inner();
        let mut reconstructed = String::new();
        for chunk in &lines[..lines.len() - 2] {
            assert!(chunk.len() < 4000);
            reconstructed.push_str(
                chunk
                    .strip_prefix(&format!("pipeline=7 offset={} ", reconstructed.len()))
                    .unwrap(),
            );
        }
        assert_eq!(descriptor, reconstructed);
        assert_eq!(lines.last().unwrap(), "pipeline=7 returned");
    }

    #[test]
    fn unwinding_does_not_report_a_return() {
        let lines = RefCell::new(Vec::new());
        let result = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
            trace(
                9,
                "{}",
                |s| lines.borrow_mut().push(s.to_owned()),
                || panic!("driver boundary"),
            );
        }));
        assert!(result.is_err());
        assert_eq!(
            lines.into_inner().last().unwrap(),
            "pipeline=9 begin bytes=2"
        );
        assert_eq!(compile(None, || 18), 18);
    }
}
