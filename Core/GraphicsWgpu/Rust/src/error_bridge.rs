//! Deliver real backend errors to the JS device that issued the operation.
//! The callback only wakes the host; never enter JS while wgpu holds its locks.
use std::cell::Cell;
use std::collections::VecDeque;
use std::ffi::{c_char, c_void};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex};

thread_local! {
    static DEVICE: Cell<(u64, bool)> = const { Cell::new((0, true)) };
    static SERIAL: Cell<u64> = const { Cell::new(0) };
}

struct Error {
    device: u64,
    kind: u32,
    message: String,
}

type Notify = extern "C" fn(*mut c_void);
static NOTIFY: Mutex<Option<(Notify, usize)>> = Mutex::new(None);
static ERRORS: Mutex<VecDeque<Error>> = Mutex::new(VecDeque::new());
static HAS_ERRORS: AtomicBool = AtomicBool::new(false);

pub fn active() -> bool {
    DEVICE.get().1
}

pub fn serial() -> u64 {
    SERIAL.get()
}

pub fn record(kind: u32, message: String) {
    SERIAL.set(SERIAL.get().wrapping_add(1));
    {
        let mut errors = ERRORS.lock().unwrap();
        errors.push_back(Error {
            device: DEVICE.get().0,
            kind,
            message,
        });
        HAS_ERRORS.store(true, Ordering::Release);
    }
    // Holding the registration lock makes unregister a barrier for the userdata's lifetime.
    if let Some((notify, data)) = *NOTIFY.lock().unwrap() {
        notify(data as *mut c_void);
    }
}

pub fn record_wgpu(error: wgpu::Error) {
    let kind = match &error {
        wgpu::Error::Validation { .. } => 1,
        wgpu::Error::OutOfMemory { .. } => 2,
        wgpu::Error::Internal { .. } => 3,
    };
    record(kind, error.to_string());
}

pub fn install(device: &wgpu::Device) {
    device.on_uncaptured_error(Arc::new(record_wgpu));
}

#[no_mangle]
pub extern "C" fn babylon_wgpu_native_set_error_device(device: u64, active: bool) {
    DEVICE.set((device, active));
}

#[no_mangle]
pub extern "C" fn babylon_wgpu_native_set_error_notify(notify: Option<Notify>, data: *mut c_void) {
    let mut registration = NOTIFY.lock().unwrap();
    if notify.is_some() || registration.is_some_and(|(_, registered)| registered == data as usize) {
        *registration = notify.map(|notify| (notify, data as usize));
    }
}

/// Return required message capacity, leaving the error queued if the buffer is too small.
#[no_mangle]
pub unsafe extern "C" fn babylon_wgpu_native_take_error(
    device: *mut u64,
    kind: *mut u32,
    output: *mut c_char,
    capacity: usize,
) -> usize {
    if !HAS_ERRORS.load(Ordering::Acquire) {
        return 0;
    }
    let mut errors = ERRORS.lock().unwrap();
    let Some(error) = errors.front() else {
        return 0;
    };
    let required = error.message.len() + 1;
    if capacity < required || output.is_null() || device.is_null() || kind.is_null() {
        return required;
    }
    // SAFETY: The host supplies writable output pointers and the advertised capacity.
    unsafe {
        *device = error.device;
        *kind = error.kind;
        std::ptr::copy_nonoverlapping(error.message.as_ptr(), output.cast(), required - 1);
        *output.add(required - 1) = 0;
    }
    errors.pop_front();
    HAS_ERRORS.store(!errors.is_empty(), Ordering::Release);
    required
}
