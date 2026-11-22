//! Opt-in Android pass timestamps. Read back query words, never eye pixels.
use std::{
    ffi::{c_char, CString},
    sync::{
        atomic::{AtomicBool, Ordering},
        Arc,
    },
};

unsafe extern "C" {
    fn __system_property_get(name: *const c_char, value: *mut c_char) -> i32;
    fn __android_log_write(priority: i32, tag: *const c_char, message: *const c_char) -> i32;
}

fn report(message: &str) {
    let message = CString::new(message).unwrap();
    unsafe {
        __android_log_write(4, c"BabylonNative".as_ptr(), message.as_ptr());
    }
}

const PAIRS: u32 = 16;
struct Sample {
    set: wgpu::QuerySet,
    passes: Vec<(crate::xr_eye_sample::Target, bool)>,
    invalid: bool,
}

pub(super) struct Timer {
    enabled: bool,
    frame: u64,
    sample: Option<Sample>,
    busy: Arc<AtomicBool>,
}

impl Default for Timer {
    fn default() -> Self {
        // Android system property values occupy at most PROP_VALUE_MAX (92) bytes.
        let mut value = [0; 92];
        let len = unsafe {
            __system_property_get(c"debug.babylon.eye_timing".as_ptr(), value.as_mut_ptr())
        };
        Self {
            enabled: len == 1 && value[0] == b'1' as c_char,
            frame: 0,
            sample: None,
            busy: Default::default(),
        }
    }
}

impl Timer {
    pub(super) fn active(&self) -> bool {
        self.sample.is_some()
    }

    pub(super) fn pass(
        &mut self,
        id: crate::xr_eye_sample::Target,
        downsample: bool,
        occupied: bool,
    ) -> Option<wgpu::RenderPassTimestampWrites<'_>> {
        let s = self.sample.as_mut()?;
        if occupied || s.passes.len() >= PAIRS as usize {
            s.invalid = true;
            return None;
        }
        let index = s.passes.len() as u32 * 2;
        s.passes.push((id, downsample));
        Some(wgpu::RenderPassTimestampWrites {
            query_set: &s.set,
            beginning_of_pass_write_index: Some(index),
            end_of_pass_write_index: Some(index + 1),
        })
    }

    pub(super) fn finish(
        &mut self,
        device: &wgpu::Device,
        queue: &wgpu::Queue,
        encoder: &mut wgpu::CommandEncoder,
    ) {
        if !self.enabled {
            return;
        }
        self.frame += 1;
        if let Some(s) = self.sample.take() {
            if !s.invalid && crate::xr_eye_sample::complete(&s.passes) {
                let size = s.passes.len() as u64 * 16;
                let buffer = |usage| {
                    device.create_buffer(&wgpu::BufferDescriptor {
                        label: Some("XR pass timestamps"),
                        size,
                        usage,
                        mapped_at_creation: false,
                    })
                };
                let staging = buffer(wgpu::BufferUsages::COPY_DST | wgpu::BufferUsages::MAP_READ);
                let results =
                    buffer(wgpu::BufferUsages::QUERY_RESOLVE | wgpu::BufferUsages::COPY_SRC);
                encoder.resolve_query_set(&s.set, 0..(2 * s.passes.len()) as u32, &results, 0);
                encoder.copy_buffer_to_buffer(&results, 0, &staging, 0, size);
                let read = staging.clone();
                let period_ms = queue.get_timestamp_period() as f64 * 1e-6;
                let frame = self.frame;
                let busy = self.busy.clone();
                busy.store(true, Ordering::Release);
                encoder.map_buffer_on_submit(&staging, wgpu::MapMode::Read, .., move |status| {
                    if status.is_ok() {
                        let Ok(data) = read.slice(..).get_mapped_range() else {
                            read.unmap();
                            busy.store(false, Ordering::Release);
                            return;
                        };
                        if let Some(sums) =
                            crate::xr_eye_sample::milliseconds(&s.passes, &data, period_ms)
                        {
                            report(&format!(
                                "XR GPU frame={frame} eyeMs={:.3} resolveMs={:.3} passes={}",
                                sums[0],
                                sums[1],
                                s.passes.len()
                            ));
                        }
                        drop(data);
                        read.unmap();
                    }
                    busy.store(false, Ordering::Release);
                });
            }
        }
        if self.frame % 24 == 1 && !self.busy.load(Ordering::Acquire) {
            if !device.features().contains(wgpu::Features::TIMESTAMP_QUERY) {
                self.enabled = false;
                report("XR GPU timestamps unavailable");
                return;
            }
            self.sample = Some(Sample {
                set: device.create_query_set(&wgpu::QuerySetDescriptor {
                    label: Some("XR stereo samples"),
                    ty: wgpu::QueryType::Timestamp,
                    count: PAIRS * 2,
                }),
                passes: Vec::new(),
                invalid: false,
            });
        }
    }
}
