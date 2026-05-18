// Threading contract: The `BackendContext` returned by `babylon_wgpu_create` is
// exclusively owned by a single C++ thread (the render/JS thread). The C++ side
// must never call `babylon_wgpu_render`, `babylon_wgpu_resize`, or
// `babylon_wgpu_destroy` concurrently. Global state (atomics and mutexes) is
// safe for concurrent access from any thread.

#[cfg(feature = "wgpu-native-rlib")]
#[allow(unused_extern_crates)]
extern crate wgpu_native;

use std::any::Any;
#[cfg(target_os = "android")]
use std::ffi::CString;
use std::ffi::{c_char, c_void, CStr};
use std::ptr;
use std::sync::atomic::{AtomicBool, AtomicPtr, AtomicU32, AtomicU64, Ordering};
use std::sync::{Mutex, OnceLock};

static WEBGPU_DRAW_ENABLED: AtomicBool = AtomicBool::new(false);
static RENDER_FRAME_COUNTER: AtomicU64 = AtomicU64::new(0);
static UPSTREAM_WGPU_NATIVE_VERSION: AtomicU32 = AtomicU32::new(0);
static ESTIMATED_GPU_MEMORY_BYTES: AtomicU64 = AtomicU64::new(0);
static EXTERNAL_IMAGE_UPLOAD_BORROWED_COUNT: AtomicU64 = AtomicU64::new(0);
static EXTERNAL_IMAGE_UPLOAD_BORROWED_BYTES: AtomicU64 = AtomicU64::new(0);
static EXTERNAL_IMAGE_UPLOAD_OWNED_COUNT: AtomicU64 = AtomicU64::new(0);
static EXTERNAL_IMAGE_UPLOAD_OWNED_BYTES: AtomicU64 = AtomicU64::new(0);
static ACTIVE_CONTEXT: AtomicPtr<BackendContext> = AtomicPtr::new(ptr::null_mut());
static LAST_ERROR: OnceLock<Mutex<String>> = OnceLock::new();

#[repr(C)]
#[derive(Clone, Copy)]
pub struct BabylonWgpuConfig {
    pub width: u32,
    pub height: u32,
    pub surface_layer: *mut c_void,
    pub prefer_low_power: u8,
    pub enable_validation: u8,
    pub _reserved0: u8,
    pub _reserved1: u8,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct BabylonWgpuInfo {
    pub backend: u32,
    pub vendor_id: u32,
    pub device_id: u32,
    pub adapter_name: [c_char; 128],
}

struct BackendContext {
    backend: upstream_wgpu_native::InteropBackendContext,
    info: BabylonWgpuInfo,
    screenshot_requested: bool,
    screenshot_rgba: Vec<u8>,
    screenshot_width: u32,
    screenshot_height: u32,
}

impl BackendContext {
    fn publish_estimated_gpu_memory_bytes(&self) {
        ESTIMATED_GPU_MEMORY_BYTES
            .store(self.backend.estimated_gpu_memory_bytes(), Ordering::Relaxed);
    }

    fn install_debug_texture(
        &mut self,
        upload: &upstream_wgpu_native::DebugTextureUploadData,
    ) -> bool {
        self.backend
            .install_debug_texture(upload.width, upload.height, &upload.rgba)
    }

    fn apply_pending_debug_texture(&mut self) {
        if let Some(upload) = upstream_wgpu_native::take_pending_debug_texture_upload() {
            let applied = self.install_debug_texture(&upload);
            if !applied {
                set_last_error("Failed to install native debug texture upload.");
            } else {
                // A successful texture import confirms JS -> native interop traffic.
                // Keep presentation enabled even when JS-side draw markers are delayed.
                WEBGPU_DRAW_ENABLED.store(true, Ordering::Release);
            }
            self.publish_estimated_gpu_memory_bytes();
            upstream_wgpu_native::recycle_debug_texture_upload(upload);
        }
    }

    fn resize(&mut self, width: u32, height: u32) {
        self.backend.resize(width, height);
        self.publish_estimated_gpu_memory_bytes();
    }

    fn render(&mut self) {
        RENDER_FRAME_COUNTER.fetch_add(1, Ordering::Relaxed);
        self.apply_pending_debug_texture();
        let draw_enabled = WEBGPU_DRAW_ENABLED.load(Ordering::Acquire);
        let screenshot_requested = self.screenshot_requested;
        self.screenshot_requested = false;
        if screenshot_requested {
            self.screenshot_rgba.clear();
            self.screenshot_width = 0;
            self.screenshot_height = 0;
        }

        match self.backend.render(draw_enabled, screenshot_requested) {
            Ok(Some(screenshot)) => {
                self.screenshot_width = screenshot.width;
                self.screenshot_height = screenshot.height;
                self.screenshot_rgba = screenshot.rgba;
            }
            Ok(None) => {}
            Err(error) => {
                log_backend_error(&format!("Render submission failed: {error}"));
            }
        }
    }
}

#[no_mangle]
pub extern "C" fn babylon_wgpu_mark_webgpu_draw_requested() {
    WEBGPU_DRAW_ENABLED.store(true, Ordering::Release);
}

#[no_mangle]
pub extern "C" fn babylon_wgpu_is_webgpu_draw_enabled() -> bool {
    WEBGPU_DRAW_ENABLED.load(Ordering::Acquire)
}

#[no_mangle]
pub extern "C" fn babylon_wgpu_get_render_frame_count() -> u64 {
    RENDER_FRAME_COUNTER.load(Ordering::Relaxed)
}

#[no_mangle]
pub extern "C" fn babylon_wgpu_get_debug_texture_hash() -> u64 {
    upstream_wgpu_native::debug_texture_import_stats().hash
}

#[no_mangle]
pub extern "C" fn babylon_wgpu_get_canvas_texture_hash() -> u64 {
    babylon_wgpu_get_debug_texture_hash()
}

#[no_mangle]
pub extern "C" fn babylon_wgpu_get_debug_texture_width() -> u32 {
    upstream_wgpu_native::debug_texture_import_stats().width
}

#[no_mangle]
pub extern "C" fn babylon_wgpu_get_canvas_texture_width() -> u32 {
    babylon_wgpu_get_debug_texture_width()
}

#[no_mangle]
pub extern "C" fn babylon_wgpu_get_debug_texture_height() -> u32 {
    upstream_wgpu_native::debug_texture_import_stats().height
}

#[no_mangle]
pub extern "C" fn babylon_wgpu_get_canvas_texture_height() -> u32 {
    babylon_wgpu_get_debug_texture_height()
}

#[no_mangle]
pub extern "C" fn babylon_wgpu_get_estimated_gpu_memory_bytes() -> u64 {
    ESTIMATED_GPU_MEMORY_BYTES.load(Ordering::Relaxed)
}

#[no_mangle]
pub extern "C" fn babylon_wgpu_get_debug_texture_import_skip_count() -> u64 {
    upstream_wgpu_native::debug_texture_import_stats().import_skip_count
}

#[no_mangle]
pub extern "C" fn babylon_wgpu_get_canvas_texture_import_skip_count() -> u64 {
    babylon_wgpu_get_debug_texture_import_skip_count()
}

#[no_mangle]
pub extern "C" fn babylon_wgpu_reset_webgpu_draw_requested() {
    WEBGPU_DRAW_ENABLED.store(false, Ordering::Release);
}

fn read_config_or_default(config: *const BabylonWgpuConfig) -> BabylonWgpuConfig {
    let config_ptr = config.cast::<c_void>();
    opaque_ptr_as_ref::<BabylonWgpuConfig>(config_ptr)
        .copied()
        .unwrap_or_else(default_config)
}

fn opaque_ptr_as_ref<'a, T>(opaque_ptr: *const c_void) -> Option<&'a T> {
    if opaque_ptr.is_null() {
        return None;
    }

    debug_assert!(
        opaque_ptr.align_offset(std::mem::align_of::<T>()) == 0,
        "opaque_ptr_as_ref: pointer is not properly aligned for {}",
        std::any::type_name::<T>()
    );

    // SAFETY: The caller guarantees `opaque_ptr` points to a valid `T` for the
    // duration of the borrow.
    unsafe { (opaque_ptr as *const T).as_ref() }
}

fn clear_debug_texture_uploads() {
    upstream_wgpu_native::clear_debug_texture_import_state();
}

fn import_canvas_texture_from_native(
    native_texture: *const c_void,
    width: u32,
    height: u32,
) -> bool {
    if native_texture.is_null() {
        return false;
    }

    let result = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        upstream_wgpu_native::set_debug_texture_from_native(native_texture, width, height)
    }));
    match result {
        Ok(Ok(_stats)) => {
            // Treat native texture import as an active WebGPU draw-path signal.
            WEBGPU_DRAW_ENABLED.store(true, Ordering::Release);
            true
        }
        Ok(Err(error)) => {
            set_last_error(&format!("Failed to import native debug texture: {error}"));
            false
        }
        Err(_) => {
            set_last_error("Native debug texture import panicked.");
            false
        }
    }
}

#[no_mangle]
pub extern "C" fn babylon_wgpu_import_canvas_texture_from_native(
    native_texture: *const c_void,
    width: u32,
    height: u32,
) -> bool {
    import_canvas_texture_from_native(native_texture, width, height)
}

#[no_mangle]
pub extern "C" fn babylon_wgpu_set_debug_texture_from_native(
    native_texture: *const c_void,
    width: u32,
    height: u32,
) -> bool {
    // Back-compat alias retained for migration scripts and tests.
    import_canvas_texture_from_native(native_texture, width, height)
}

fn fill_adapter_name(name: &str) -> [c_char; 128] {
    let mut output = [0 as c_char; 128];
    let bytes = name.as_bytes();
    let max_count = output.len().saturating_sub(1);
    let copy_count = bytes.len().min(max_count);

    for (dst, src) in output.iter_mut().zip(bytes.iter()).take(copy_count) {
        *dst = *src as c_char;
    }

    output
}

fn format_upstream_wgpu_native_version(version: u32) -> Option<String> {
    if version == 0 {
        return None;
    }

    let major = (version >> 24) & 0xFF;
    let minor = (version >> 16) & 0xFF;
    let patch = (version >> 8) & 0xFF;
    let build = version & 0xFF;

    let formatted = if build != 0 {
        format!("{major}.{minor}.{patch}.{build}")
    } else if patch != 0 {
        format!("{major}.{minor}.{patch}")
    } else {
        format!("{major}.{minor}")
    };

    Some(formatted)
}

fn decorated_adapter_name(name: &str) -> String {
    let upstream_version = UPSTREAM_WGPU_NATIVE_VERSION.load(Ordering::Relaxed);
    if let Some(version_text) = format_upstream_wgpu_native_version(upstream_version) {
        return format!("{name} (wgpu-native {version_text})");
    }

    name.to_owned()
}

#[cfg(target_os = "android")]
fn log_backend_error(message: &str) {
    unsafe extern "C" {
        fn __android_log_write(prio: i32, tag: *const c_char, text: *const c_char) -> i32;
    }

    const ANDROID_LOG_ERROR: i32 = 6;
    if let (Ok(tag), Ok(text)) = (CString::new("BabylonNative"), CString::new(message)) {
        // SAFETY: Strings are NUL-terminated and valid for the call duration.
        unsafe {
            let _ = __android_log_write(ANDROID_LOG_ERROR, tag.as_ptr(), text.as_ptr());
        }
    }
}

#[cfg(not(target_os = "android"))]
fn log_backend_error(_message: &str) {}

fn set_last_error(message: &str) {
    let storage = LAST_ERROR.get_or_init(|| Mutex::new(String::new()));
    let mut value = match storage.lock() {
        Ok(lock) => lock,
        Err(poisoned) => poisoned.into_inner(),
    };
    value.clear();
    value.push_str(message);

    log_backend_error(message);
}

fn clear_last_error() {
    let storage = LAST_ERROR.get_or_init(|| Mutex::new(String::new()));
    let mut value = match storage.lock() {
        Ok(lock) => lock,
        Err(poisoned) => poisoned.into_inner(),
    };
    value.clear();
}

fn panic_payload_to_string(payload: &(dyn Any + Send)) -> String {
    if let Some(message) = payload.downcast_ref::<&str>() {
        return (*message).to_owned();
    }
    if let Some(message) = payload.downcast_ref::<String>() {
        return message.clone();
    }

    "non-string panic payload".to_owned()
}

fn copy_last_error(output: *mut c_char, output_len: usize) -> bool {
    if output.is_null() || output_len == 0 {
        return false;
    }

    let storage = LAST_ERROR.get_or_init(|| Mutex::new(String::new()));
    let message = match storage.lock() {
        Ok(value) if !value.is_empty() => value,
        Err(poisoned) => {
            let value = poisoned.into_inner();
            if value.is_empty() {
                return false;
            }
            value
        }
        _ => return false,
    };

    let bytes = message.as_bytes();
    let max_copy = output_len.saturating_sub(1);
    let copy_len = bytes.len().min(max_copy);

    // SAFETY: Caller provides a valid writable output buffer with `output_len`
    // bytes by C ABI contract.
    let output_slice = unsafe { std::slice::from_raw_parts_mut(output.cast::<u8>(), output_len) };
    output_slice[..copy_len].copy_from_slice(&bytes[..copy_len]);
    output_slice[copy_len] = 0;

    true
}

fn create_context(config: BabylonWgpuConfig) -> Result<Box<BackendContext>, String> {
    let backend = upstream_wgpu_native::InteropBackendContext::create(
        upstream_wgpu_native::LocalBootstrapConfig {
            width: config.width.max(1),
            height: config.height.max(1),
            surface_layer: config.surface_layer,
            prefer_low_power: config.prefer_low_power != 0,
        },
    )?;

    if backend.used_fallback_adapter() {
        log_backend_error(
            "No hardware Vulkan adapter found; continuing with fallback Vulkan adapter.",
        );
    }
    let adapter_info = backend.resolved_adapter_info();

    let context_info = BabylonWgpuInfo {
        backend: adapter_info.backend,
        vendor_id: adapter_info.vendor_id,
        device_id: adapter_info.device_id,
        adapter_name: fill_adapter_name(
            decorated_adapter_name(adapter_info.adapter_name.as_str()).as_str(),
        ),
    };

    let context = BackendContext {
        backend,
        info: context_info,
        screenshot_requested: false,
        screenshot_rgba: Vec::new(),
        screenshot_width: 0,
        screenshot_height: 0,
    };

    context.publish_estimated_gpu_memory_bytes();
    Ok(Box::new(context))
}

fn dispatch_compute_global(shader_source: &str, entry_point: &str, x: u32, y: u32, z: u32) -> bool {
    if let Err(error) =
        upstream_wgpu_native::dispatch_compute_global(shader_source, entry_point, x, y, z, false)
    {
        log_backend_error(&format!(
            "upstream wgpu-native compute dispatch failed: {error}"
        ));
        return false;
    }

    true
}

fn read_c_string<'a>(value: *const c_char, label: &str) -> Result<&'a str, String> {
    if value.is_null() {
        return Err(format!("{label} pointer was null"));
    }

    unsafe { CStr::from_ptr(value) }
        .to_str()
        .map_err(|_| format!("{label} was not valid UTF-8"))
}

fn read_optional_c_string<'a>(value: *const c_char) -> Result<&'a str, String> {
    if value.is_null() {
        return Ok("");
    }

    unsafe { CStr::from_ptr(value) }
        .to_str()
        .map_err(|_| "optional string argument was not valid UTF-8".to_string())
}

fn run_with_active_backend<T, F>(operation: &str, fallback: T, f: F) -> T
where
    F: FnOnce(&mut upstream_wgpu_native::InteropBackendContext) -> Result<T, String>,
{
    let result = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        let context = ACTIVE_CONTEXT.load(Ordering::Acquire);
        if context.is_null() {
            return Err(format!(
                "{operation} called before WGPU backend initialization"
            ));
        }
        let context_ref = unsafe { &mut *context };
        f(&mut context_ref.backend)
    }));

    match result {
        Ok(Ok(value)) => value,
        Ok(Err(error)) => {
            set_last_error(&error);
            fallback
        }
        Err(payload) => {
            set_last_error(
                format!(
                    "{operation} panicked: {}",
                    panic_payload_to_string(payload.as_ref())
                )
                .as_str(),
            );
            fallback
        }
    }
}

#[no_mangle]
pub extern "C" fn babylon_wgpu_native_create_buffer(
    size: u64,
    usage: u32,
    mapped_at_creation: bool,
) -> u64 {
    run_with_active_backend("GPUDevice.createBuffer", 0, |backend| {
        backend.create_buffer(size, usage, mapped_at_creation)
    })
}

#[no_mangle]
pub extern "C" fn babylon_wgpu_native_write_buffer(
    buffer_id: u64,
    offset: u64,
    data: *const u8,
    data_len: usize,
) -> bool {
    run_with_active_backend("GPUQueue.writeBuffer", false, |backend| {
        let bytes = if data_len == 0 {
            &[]
        } else {
            if data.is_null() {
                return Err("GPUQueue.writeBuffer data pointer was null".to_string());
            }
            unsafe { std::slice::from_raw_parts(data, data_len) }
        };
        backend.write_buffer(buffer_id, offset, bytes)?;
        Ok(true)
    })
}

#[no_mangle]
pub extern "C" fn babylon_wgpu_native_create_texture(descriptor_json: *const c_char) -> u64 {
    run_with_active_backend("GPUDevice.createTexture", 0, |backend| {
        backend.create_texture(read_c_string(descriptor_json, "GPUTextureDescriptor")?)
    })
}

#[no_mangle]
pub extern "C" fn babylon_wgpu_native_create_texture_view(
    texture_id: u64,
    descriptor_json: *const c_char,
) -> u64 {
    run_with_active_backend("GPUTexture.createView", 0, |backend| {
        backend.create_texture_view(texture_id, read_optional_c_string(descriptor_json)?)
    })
}

#[no_mangle]
pub extern "C" fn babylon_wgpu_native_create_sampler(descriptor_json: *const c_char) -> u64 {
    run_with_active_backend("GPUDevice.createSampler", 0, |backend| {
        backend.create_sampler(read_optional_c_string(descriptor_json)?.if_empty_object())
    })
}

trait EmptyObjectJson {
    fn if_empty_object(&self) -> &str;
}

impl EmptyObjectJson for str {
    fn if_empty_object(&self) -> &str {
        if self.is_empty() {
            "{}"
        } else {
            self
        }
    }
}

#[no_mangle]
pub extern "C" fn babylon_wgpu_native_create_shader_module(code: *const c_char) -> u64 {
    run_with_active_backend("GPUDevice.createShaderModule", 0, |backend| {
        backend.create_shader_module(read_c_string(code, "GPUShaderModule code")?)
    })
}

#[no_mangle]
pub extern "C" fn babylon_wgpu_native_create_bind_group_layout(
    descriptor_json: *const c_char,
) -> u64 {
    run_with_active_backend("GPUDevice.createBindGroupLayout", 0, |backend| {
        backend.create_bind_group_layout(read_c_string(
            descriptor_json,
            "GPUBindGroupLayoutDescriptor",
        )?)
    })
}

#[no_mangle]
pub extern "C" fn babylon_wgpu_native_create_pipeline_layout(
    descriptor_json: *const c_char,
) -> u64 {
    run_with_active_backend("GPUDevice.createPipelineLayout", 0, |backend| {
        backend.create_pipeline_layout(read_c_string(
            descriptor_json,
            "GPUPipelineLayoutDescriptor",
        )?)
    })
}

#[no_mangle]
pub extern "C" fn babylon_wgpu_native_create_bind_group(descriptor_json: *const c_char) -> u64 {
    run_with_active_backend("GPUDevice.createBindGroup", 0, |backend| {
        backend.create_bind_group(read_c_string(descriptor_json, "GPUBindGroupDescriptor")?)
    })
}

#[no_mangle]
pub extern "C" fn babylon_wgpu_native_create_render_pipeline(
    descriptor_json: *const c_char,
) -> u64 {
    run_with_active_backend("GPUDevice.createRenderPipeline", 0, |backend| {
        backend.create_render_pipeline(read_c_string(
            descriptor_json,
            "GPURenderPipelineDescriptor",
        )?)
    })
}

#[no_mangle]
pub extern "C" fn babylon_wgpu_native_render_pipeline_get_bind_group_layout(
    pipeline_id: u64,
    index: u32,
) -> u64 {
    run_with_active_backend("GPURenderPipeline.getBindGroupLayout", 0, |backend| {
        backend.render_pipeline_get_bind_group_layout(pipeline_id, index)
    })
}

#[no_mangle]
pub extern "C" fn babylon_wgpu_native_create_compute_pipeline(
    descriptor_json: *const c_char,
) -> u64 {
    run_with_active_backend("GPUDevice.createComputePipeline", 0, |backend| {
        backend.create_compute_pipeline(read_c_string(
            descriptor_json,
            "GPUComputePipelineDescriptor",
        )?)
    })
}

#[no_mangle]
pub extern "C" fn babylon_wgpu_native_compute_pipeline_get_bind_group_layout(
    pipeline_id: u64,
    index: u32,
) -> u64 {
    run_with_active_backend("GPUComputePipeline.getBindGroupLayout", 0, |backend| {
        backend.compute_pipeline_get_bind_group_layout(pipeline_id, index)
    })
}

#[no_mangle]
pub extern "C" fn babylon_wgpu_native_create_command_encoder() -> u64 {
    run_with_active_backend("GPUDevice.createCommandEncoder", 0, |backend| {
        backend.create_command_encoder()
    })
}

#[no_mangle]
pub extern "C" fn babylon_wgpu_native_command_encoder_begin_render_pass(
    encoder_id: u64,
    descriptor_json: *const c_char,
) -> u64 {
    run_with_active_backend("GPUCommandEncoder.beginRenderPass", 0, |backend| {
        backend.command_encoder_begin_render_pass(
            encoder_id,
            read_c_string(descriptor_json, "GPURenderPassDescriptor")?,
        )
    })
}

#[no_mangle]
pub extern "C" fn babylon_wgpu_native_command_encoder_begin_compute_pass(
    encoder_id: u64,
    descriptor_json: *const c_char,
) -> u64 {
    run_with_active_backend("GPUCommandEncoder.beginComputePass", 0, |backend| {
        backend.command_encoder_begin_compute_pass(
            encoder_id,
            read_c_string(descriptor_json, "GPUComputePassDescriptor")?,
        )
    })
}

#[no_mangle]
pub extern "C" fn babylon_wgpu_native_command_encoder_copy_buffer_to_buffer(
    encoder_id: u64,
    source_id: u64,
    source_offset: u64,
    destination_id: u64,
    destination_offset: u64,
    size: u64,
) -> bool {
    run_with_active_backend("GPUCommandEncoder.copyBufferToBuffer", false, |backend| {
        backend.command_encoder_copy_buffer_to_buffer(
            encoder_id,
            source_id,
            source_offset,
            destination_id,
            destination_offset,
            size,
        )?;
        Ok(true)
    })
}

#[no_mangle]
pub extern "C" fn babylon_wgpu_native_command_encoder_copy_buffer_to_texture(
    encoder_id: u64,
    source_json: *const c_char,
    destination_json: *const c_char,
    size_json: *const c_char,
) -> bool {
    run_with_active_backend("GPUCommandEncoder.copyBufferToTexture", false, |backend| {
        backend.command_encoder_copy_buffer_to_texture(
            encoder_id,
            read_c_string(source_json, "GPUImageCopyBuffer")?,
            read_c_string(destination_json, "GPUImageCopyTexture")?,
            read_c_string(size_json, "GPUExtent3D")?,
        )?;
        Ok(true)
    })
}

#[no_mangle]
pub extern "C" fn babylon_wgpu_native_command_encoder_copy_texture_to_buffer(
    encoder_id: u64,
    source_json: *const c_char,
    destination_json: *const c_char,
    size_json: *const c_char,
) -> bool {
    run_with_active_backend("GPUCommandEncoder.copyTextureToBuffer", false, |backend| {
        backend.command_encoder_copy_texture_to_buffer(
            encoder_id,
            read_c_string(source_json, "GPUImageCopyTexture")?,
            read_c_string(destination_json, "GPUImageCopyBuffer")?,
            read_c_string(size_json, "GPUExtent3D")?,
        )?;
        Ok(true)
    })
}

#[no_mangle]
pub extern "C" fn babylon_wgpu_native_command_encoder_copy_texture_to_texture(
    encoder_id: u64,
    source_json: *const c_char,
    destination_json: *const c_char,
    size_json: *const c_char,
) -> bool {
    run_with_active_backend("GPUCommandEncoder.copyTextureToTexture", false, |backend| {
        backend.command_encoder_copy_texture_to_texture(
            encoder_id,
            read_c_string(source_json, "GPUImageCopyTexture source")?,
            read_c_string(destination_json, "GPUImageCopyTexture destination")?,
            read_c_string(size_json, "GPUExtent3D")?,
        )?;
        Ok(true)
    })
}

#[no_mangle]
pub extern "C" fn babylon_wgpu_native_command_encoder_clear_buffer(
    encoder_id: u64,
    buffer_id: u64,
    offset: u64,
    size: u64,
) -> bool {
    run_with_active_backend("GPUCommandEncoder.clearBuffer", false, |backend| {
        backend.command_encoder_clear_buffer(encoder_id, buffer_id, offset, size)?;
        Ok(true)
    })
}

#[no_mangle]
pub extern "C" fn babylon_wgpu_native_command_encoder_finish(encoder_id: u64) -> u64 {
    run_with_active_backend("GPUCommandEncoder.finish", 0, |backend| {
        backend.command_encoder_finish(encoder_id)
    })
}

#[no_mangle]
pub extern "C" fn babylon_wgpu_native_render_pass_set_pipeline(
    pass_id: u64,
    pipeline_id: u64,
) -> bool {
    run_with_active_backend("GPURenderPassEncoder.setPipeline", false, |backend| {
        backend.render_pass_set_pipeline(pass_id, pipeline_id)?;
        Ok(true)
    })
}

#[no_mangle]
pub extern "C" fn babylon_wgpu_native_render_pass_set_bind_group(
    pass_id: u64,
    index: u32,
    bind_group_id: u64,
    dynamic_offsets: *const u32,
    dynamic_offset_count: usize,
) -> bool {
    run_with_active_backend("GPURenderPassEncoder.setBindGroup", false, |backend| {
        let offsets = if dynamic_offset_count == 0 {
            &[]
        } else {
            if dynamic_offsets.is_null() {
                return Err("dynamic offsets pointer was null".to_string());
            }
            unsafe { std::slice::from_raw_parts(dynamic_offsets, dynamic_offset_count) }
        };
        backend.render_pass_set_bind_group(pass_id, index, bind_group_id, offsets)?;
        Ok(true)
    })
}

#[no_mangle]
pub extern "C" fn babylon_wgpu_native_render_pass_set_vertex_buffer(
    pass_id: u64,
    slot: u32,
    buffer_id: u64,
    offset: u64,
    size: u64,
) -> bool {
    run_with_active_backend("GPURenderPassEncoder.setVertexBuffer", false, |backend| {
        backend.render_pass_set_vertex_buffer(pass_id, slot, buffer_id, offset, size)?;
        Ok(true)
    })
}

#[no_mangle]
pub extern "C" fn babylon_wgpu_native_render_pass_set_index_buffer(
    pass_id: u64,
    buffer_id: u64,
    format: *const c_char,
    offset: u64,
    size: u64,
) -> bool {
    run_with_active_backend("GPURenderPassEncoder.setIndexBuffer", false, |backend| {
        backend.render_pass_set_index_buffer(
            pass_id,
            buffer_id,
            read_c_string(format, "GPUIndexFormat")?,
            offset,
            size,
        )?;
        Ok(true)
    })
}

#[no_mangle]
pub extern "C" fn babylon_wgpu_native_render_pass_set_viewport(
    pass_id: u64,
    x: f32,
    y: f32,
    width: f32,
    height: f32,
    min_depth: f32,
    max_depth: f32,
) -> bool {
    run_with_active_backend("GPURenderPassEncoder.setViewport", false, |backend| {
        backend.render_pass_push_command(
            pass_id,
            upstream_wgpu_native::RenderPassCommand::SetViewport {
                x,
                y,
                width,
                height,
                min_depth,
                max_depth,
            },
        )?;
        Ok(true)
    })
}

#[no_mangle]
pub extern "C" fn babylon_wgpu_native_render_pass_set_scissor_rect(
    pass_id: u64,
    x: u32,
    y: u32,
    width: u32,
    height: u32,
) -> bool {
    run_with_active_backend("GPURenderPassEncoder.setScissorRect", false, |backend| {
        backend.render_pass_push_command(
            pass_id,
            upstream_wgpu_native::RenderPassCommand::SetScissorRect {
                x,
                y,
                width,
                height,
            },
        )?;
        Ok(true)
    })
}

#[no_mangle]
pub extern "C" fn babylon_wgpu_native_render_pass_set_blend_constant(
    pass_id: u64,
    r: f64,
    g: f64,
    b: f64,
    a: f64,
) -> bool {
    run_with_active_backend("GPURenderPassEncoder.setBlendConstant", false, |backend| {
        backend.render_pass_push_command(
            pass_id,
            upstream_wgpu_native::RenderPassCommand::SetBlendConstant(wgpu::Color { r, g, b, a }),
        )?;
        Ok(true)
    })
}

#[no_mangle]
pub extern "C" fn babylon_wgpu_native_render_pass_set_stencil_reference(
    pass_id: u64,
    reference: u32,
) -> bool {
    run_with_active_backend(
        "GPURenderPassEncoder.setStencilReference",
        false,
        |backend| {
            backend.render_pass_push_command(
                pass_id,
                upstream_wgpu_native::RenderPassCommand::SetStencilReference(reference),
            )?;
            Ok(true)
        },
    )
}

#[no_mangle]
pub extern "C" fn babylon_wgpu_native_render_pass_draw(
    pass_id: u64,
    vertex_count: u32,
    instance_count: u32,
    first_vertex: u32,
    first_instance: u32,
) -> bool {
    run_with_active_backend("GPURenderPassEncoder.draw", false, |backend| {
        backend.render_pass_push_command(
            pass_id,
            upstream_wgpu_native::RenderPassCommand::Draw {
                vertices: first_vertex..first_vertex.saturating_add(vertex_count),
                instances: first_instance..first_instance.saturating_add(instance_count),
            },
        )?;
        Ok(true)
    })
}

#[no_mangle]
pub extern "C" fn babylon_wgpu_native_render_pass_draw_indexed(
    pass_id: u64,
    index_count: u32,
    instance_count: u32,
    first_index: u32,
    base_vertex: i32,
    first_instance: u32,
) -> bool {
    run_with_active_backend("GPURenderPassEncoder.drawIndexed", false, |backend| {
        backend.render_pass_push_command(
            pass_id,
            upstream_wgpu_native::RenderPassCommand::DrawIndexed {
                indices: first_index..first_index.saturating_add(index_count),
                base_vertex,
                instances: first_instance..first_instance.saturating_add(instance_count),
            },
        )?;
        Ok(true)
    })
}

#[no_mangle]
pub extern "C" fn babylon_wgpu_native_render_pass_draw_indirect(
    pass_id: u64,
    buffer_id: u64,
    offset: u64,
) -> bool {
    run_with_active_backend("GPURenderPassEncoder.drawIndirect", false, |backend| {
        backend.render_pass_draw_indirect(pass_id, buffer_id, offset)?;
        Ok(true)
    })
}

#[no_mangle]
pub extern "C" fn babylon_wgpu_native_render_pass_draw_indexed_indirect(
    pass_id: u64,
    buffer_id: u64,
    offset: u64,
) -> bool {
    run_with_active_backend(
        "GPURenderPassEncoder.drawIndexedIndirect",
        false,
        |backend| {
            backend.render_pass_draw_indexed_indirect(pass_id, buffer_id, offset)?;
            Ok(true)
        },
    )
}

#[no_mangle]
pub extern "C" fn babylon_wgpu_native_render_pass_end(pass_id: u64) -> bool {
    run_with_active_backend("GPURenderPassEncoder.end", false, |backend| {
        backend.render_pass_end(pass_id)?;
        Ok(true)
    })
}

#[no_mangle]
pub extern "C" fn babylon_wgpu_native_compute_pass_set_pipeline(
    pass_id: u64,
    pipeline_id: u64,
) -> bool {
    run_with_active_backend("GPUComputePassEncoder.setPipeline", false, |backend| {
        backend.compute_pass_set_pipeline(pass_id, pipeline_id)?;
        Ok(true)
    })
}

#[no_mangle]
pub extern "C" fn babylon_wgpu_native_compute_pass_set_bind_group(
    pass_id: u64,
    index: u32,
    bind_group_id: u64,
    dynamic_offsets: *const u32,
    dynamic_offset_count: usize,
) -> bool {
    run_with_active_backend("GPUComputePassEncoder.setBindGroup", false, |backend| {
        let offsets = if dynamic_offset_count == 0 {
            &[]
        } else {
            if dynamic_offsets.is_null() {
                return Err("compute dynamic offsets pointer was null".to_string());
            }
            unsafe { std::slice::from_raw_parts(dynamic_offsets, dynamic_offset_count) }
        };
        backend.compute_pass_set_bind_group(pass_id, index, bind_group_id, offsets)?;
        Ok(true)
    })
}

#[no_mangle]
pub extern "C" fn babylon_wgpu_native_compute_pass_dispatch_workgroups(
    pass_id: u64,
    x: u32,
    y: u32,
    z: u32,
) -> bool {
    run_with_active_backend(
        "GPUComputePassEncoder.dispatchWorkgroups",
        false,
        |backend| {
            backend.compute_pass_dispatch_workgroups(pass_id, x, y, z)?;
            Ok(true)
        },
    )
}

#[no_mangle]
pub extern "C" fn babylon_wgpu_native_compute_pass_dispatch_workgroups_indirect(
    pass_id: u64,
    buffer_id: u64,
    offset: u64,
) -> bool {
    run_with_active_backend(
        "GPUComputePassEncoder.dispatchWorkgroupsIndirect",
        false,
        |backend| {
            backend.compute_pass_dispatch_workgroups_indirect(pass_id, buffer_id, offset)?;
            Ok(true)
        },
    )
}

#[no_mangle]
pub extern "C" fn babylon_wgpu_native_compute_pass_end(pass_id: u64) -> bool {
    run_with_active_backend("GPUComputePassEncoder.end", false, |backend| {
        backend.compute_pass_end(pass_id)?;
        Ok(true)
    })
}

#[no_mangle]
pub extern "C" fn babylon_wgpu_native_queue_submit(
    command_buffer_ids: *const u64,
    command_buffer_count: usize,
) -> bool {
    run_with_active_backend("GPUQueue.submit", false, |backend| {
        let ids = if command_buffer_count == 0 {
            &[]
        } else {
            if command_buffer_ids.is_null() {
                return Err("GPUQueue.submit command buffer pointer was null".to_string());
            }
            unsafe { std::slice::from_raw_parts(command_buffer_ids, command_buffer_count) }
        };
        backend.queue_submit(ids)?;
        Ok(true)
    })
}

#[no_mangle]
pub extern "C" fn babylon_wgpu_native_queue_wait_submitted_work() -> bool {
    run_with_active_backend("GPUQueue.onSubmittedWorkDone", false, |backend| {
        backend.queue_wait_submitted_work()?;
        Ok(true)
    })
}

#[no_mangle]
pub extern "C" fn babylon_wgpu_native_queue_write_texture(
    destination_json: *const c_char,
    data: *const u8,
    data_len: usize,
    layout_json: *const c_char,
    size_json: *const c_char,
) -> bool {
    run_with_active_backend("GPUQueue.writeTexture", false, |backend| {
        let bytes = if data_len == 0 {
            &[]
        } else {
            if data.is_null() {
                return Err("GPUQueue.writeTexture data pointer was null".to_string());
            }
            unsafe { std::slice::from_raw_parts(data, data_len) }
        };
        backend.queue_write_texture(
            read_c_string(destination_json, "GPUImageCopyTexture")?,
            bytes,
            read_c_string(layout_json, "GPUImageDataLayout")?,
            read_c_string(size_json, "GPUExtent3D")?,
        )?;
        Ok(true)
    })
}

#[no_mangle]
pub extern "C" fn babylon_wgpu_native_queue_copy_external_image_to_texture(
    native_texture: *const c_void,
    source_width: u32,
    source_height: u32,
    destination_json: *const c_char,
    size_json: *const c_char,
) -> bool {
    run_with_active_backend("GPUQueue.copyExternalImageToTexture", false, |backend| {
        backend.queue_copy_external_image_to_texture(
            native_texture,
            source_width,
            source_height,
            read_c_string(destination_json, "GPUImageCopyTexture")?,
            read_c_string(size_json, "GPUExtent3D")?,
        )?;
        Ok(true)
    })
}

#[no_mangle]
pub extern "C" fn babylon_wgpu_native_queue_copy_external_image_rgba_to_texture(
    rgba: *const u8,
    rgba_len: usize,
    source_width: u32,
    source_height: u32,
    source_origin_x: u32,
    source_origin_y: u32,
    flip_y: u32,
    destination_json: *const c_char,
    size_json: *const c_char,
) -> bool {
    run_with_active_backend("GPUQueue.copyExternalImageToTexture", false, |backend| {
        let bytes = if rgba_len == 0 {
            &[]
        } else {
            if rgba.is_null() {
                return Err("copyExternalImageToTexture RGBA data pointer was null".to_string());
            }
            unsafe { std::slice::from_raw_parts(rgba, rgba_len) }
        };
        backend.queue_copy_external_image_rgba_to_texture(
            bytes,
            source_width,
            source_height,
            source_origin_x,
            source_origin_y,
            flip_y != 0,
            read_c_string(destination_json, "GPUImageCopyTexture")?,
            read_c_string(size_json, "GPUExtent3D")?,
        )?;
        Ok(true)
    })
}

#[no_mangle]
pub extern "C" fn babylon_wgpu_native_canvas_get_current_texture(
    canvas_id: u64,
    width: u32,
    height: u32,
    format: *const c_char,
    usage: u32,
) -> u64 {
    run_with_active_backend("GPUCanvasContext.getCurrentTexture", 0, |backend| {
        backend.canvas_get_current_texture(
            canvas_id,
            width,
            height,
            read_c_string(format, "GPUTextureFormat")?,
            usage,
        )
    })
}

#[no_mangle]
pub extern "C" fn babylon_wgpu_native_canvas_destroy(canvas_id: u64) -> bool {
    run_with_active_backend("GPUCanvasContext.unconfigure", false, |backend| {
        backend.canvas_destroy(canvas_id);
        Ok(true)
    })
}

#[no_mangle]
pub extern "C" fn babylon_wgpu_native_test_read_texture_pixel(
    texture_id: u64,
    x: u32,
    y: u32,
    out_rgba: *mut u8,
    out_rgba_len: usize,
) -> bool {
    if out_rgba.is_null() || out_rgba_len < 4 {
        set_last_error("NativeWebGPU test texture readback output buffer was too small.");
        return false;
    }

    run_with_active_backend("NativeWebGPU._testReadTexturePixel", false, |backend| {
        let pixel = backend.read_texture_pixel_rgba(texture_id, x, y)?;
        // SAFETY: The pointer was checked for null above and the caller provides
        // at least four writable bytes by C ABI contract.
        let out = unsafe { std::slice::from_raw_parts_mut(out_rgba, out_rgba_len) };
        out[..4].copy_from_slice(&pixel);
        Ok(true)
    })
}

#[no_mangle]
pub extern "C" fn babylon_wgpu_native_get_external_image_upload_borrowed_count() -> u64 {
    EXTERNAL_IMAGE_UPLOAD_BORROWED_COUNT.load(Ordering::Relaxed)
}

#[no_mangle]
pub extern "C" fn babylon_wgpu_native_get_external_image_upload_borrowed_bytes() -> u64 {
    EXTERNAL_IMAGE_UPLOAD_BORROWED_BYTES.load(Ordering::Relaxed)
}

#[no_mangle]
pub extern "C" fn babylon_wgpu_native_get_external_image_upload_owned_count() -> u64 {
    EXTERNAL_IMAGE_UPLOAD_OWNED_COUNT.load(Ordering::Relaxed)
}

#[no_mangle]
pub extern "C" fn babylon_wgpu_native_get_external_image_upload_owned_bytes() -> u64 {
    EXTERNAL_IMAGE_UPLOAD_OWNED_BYTES.load(Ordering::Relaxed)
}

#[no_mangle]
pub extern "C" fn babylon_wgpu_native_reset_external_image_upload_stats() {
    EXTERNAL_IMAGE_UPLOAD_BORROWED_COUNT.store(0, Ordering::Relaxed);
    EXTERNAL_IMAGE_UPLOAD_BORROWED_BYTES.store(0, Ordering::Relaxed);
    EXTERNAL_IMAGE_UPLOAD_OWNED_COUNT.store(0, Ordering::Relaxed);
    EXTERNAL_IMAGE_UPLOAD_OWNED_BYTES.store(0, Ordering::Relaxed);
}

#[no_mangle]
pub extern "C" fn babylon_wgpu_native_destroy_resource(kind: u32, resource_id: u64) -> bool {
    run_with_active_backend("GPU resource destroy", false, |backend| {
        Ok(backend.destroy_resource(kind, resource_id))
    })
}

fn default_config() -> BabylonWgpuConfig {
    BabylonWgpuConfig {
        width: 1,
        height: 1,
        surface_layer: ptr::null_mut(),
        prefer_low_power: 0,
        enable_validation: 0,
        _reserved0: 0,
        _reserved1: 0,
    }
}

#[no_mangle]
pub extern "C" fn babylon_wgpu_create(config: *const BabylonWgpuConfig) -> *mut c_void {
    clear_last_error();
    clear_debug_texture_uploads();

    let upstream_version = upstream_wgpu_native::version();
    UPSTREAM_WGPU_NATIVE_VERSION.store(upstream_version, Ordering::Relaxed);
    if let Some(version_text) = format_upstream_wgpu_native_version(upstream_version) {
        log_backend_error(&format!(
            "GraphicsWgpu upstream probe active: wgpu-native {version_text}"
        ));
    }

    let config_value = read_config_or_default(config);

    let result = std::panic::catch_unwind(|| create_context(config_value));
    match result {
        Ok(Ok(context)) => {
            let raw = Box::into_raw(context);
            ACTIVE_CONTEXT.store(raw, Ordering::Release);
            raw as *mut c_void
        }
        Ok(Err(error)) => {
            set_last_error(&error);
            ptr::null_mut()
        }
        Err(payload) => {
            set_last_error(
                format!(
                    "WGPU backend initialization panicked: {}",
                    panic_payload_to_string(payload.as_ref())
                )
                .as_str(),
            );
            ptr::null_mut()
        }
    }
}

#[no_mangle]
pub extern "C" fn babylon_wgpu_destroy(context: *mut c_void) {
    if context.is_null() {
        return;
    }

    let active = ACTIVE_CONTEXT.load(Ordering::Acquire);
    if active == context as *mut BackendContext {
        ACTIVE_CONTEXT.store(ptr::null_mut(), Ordering::Release);
    }

    // SAFETY: The pointer was allocated by babylon_wgpu_create and is owned by the caller.
    unsafe {
        drop(Box::from_raw(context as *mut BackendContext));
    }
    clear_debug_texture_uploads();
    ESTIMATED_GPU_MEMORY_BYTES.store(0, Ordering::Relaxed);
}

#[no_mangle]
pub extern "C" fn babylon_wgpu_resize(context: *mut c_void, width: u32, height: u32) -> bool {
    if context.is_null() {
        set_last_error("WGPU resize received null backend context pointer.");
        return false;
    }

    let result = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        // SAFETY: `context` comes from `babylon_wgpu_create` and remains exclusively
        // owned by the caller for the duration of this call.
        let context_ref = unsafe { &mut *(context as *mut BackendContext) };
        context_ref.resize(width, height);
    }));

    match result {
        Ok(()) => true,
        Err(payload) => {
            set_last_error(
                format!(
                    "WGPU resize panicked: {}",
                    panic_payload_to_string(payload.as_ref())
                )
                .as_str(),
            );
            false
        }
    }
}

#[no_mangle]
pub extern "C" fn babylon_wgpu_render(context: *mut c_void) -> bool {
    if context.is_null() {
        set_last_error("WGPU render received null backend context pointer.");
        return false;
    }

    let result = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        // SAFETY: `context` comes from `babylon_wgpu_create` and remains valid for
        // this call by C ABI contract.
        let context_ref = unsafe { &mut *(context as *mut BackendContext) };
        context_ref.render();
    }));

    match result {
        Ok(()) => true,
        Err(payload) => {
            set_last_error(
                format!(
                    "WGPU render panicked: {}",
                    panic_payload_to_string(payload.as_ref())
                )
                .as_str(),
            );
            false
        }
    }
}

#[no_mangle]
pub extern "C" fn babylon_wgpu_request_screenshot(context: *mut c_void) -> bool {
    if context.is_null() {
        set_last_error("WGPU screenshot request received null backend context pointer.");
        return false;
    }

    let result = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        // SAFETY: `context` comes from `babylon_wgpu_create` and remains valid for
        // this call by C ABI contract.
        let context_ref = unsafe { &mut *(context as *mut BackendContext) };
        context_ref.screenshot_requested = true;
    }));

    match result {
        Ok(()) => true,
        Err(payload) => {
            set_last_error(
                format!(
                    "WGPU screenshot request panicked: {}",
                    panic_payload_to_string(payload.as_ref())
                )
                .as_str(),
            );
            false
        }
    }
}

#[no_mangle]
pub extern "C" fn babylon_wgpu_get_screenshot_info(
    context: *const c_void,
    width: *mut u32,
    height: *mut u32,
    byte_len: *mut usize,
) -> bool {
    if context.is_null() || width.is_null() || height.is_null() || byte_len.is_null() {
        set_last_error("WGPU screenshot info received null pointer.");
        return false;
    }

    let result = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        // SAFETY: pointers were checked for null and are valid by C ABI contract.
        let context_ref = unsafe { &*(context as *const BackendContext) };
        unsafe {
            *width = context_ref.screenshot_width;
            *height = context_ref.screenshot_height;
            *byte_len = context_ref.screenshot_rgba.len();
        }
    }));

    match result {
        Ok(()) => true,
        Err(payload) => {
            set_last_error(
                format!(
                    "WGPU screenshot info panicked: {}",
                    panic_payload_to_string(payload.as_ref())
                )
                .as_str(),
            );
            false
        }
    }
}

#[no_mangle]
pub extern "C" fn babylon_wgpu_copy_screenshot(
    context: *const c_void,
    output: *mut u8,
    output_len: usize,
) -> bool {
    if context.is_null() || output.is_null() {
        set_last_error("WGPU screenshot copy received null pointer.");
        return false;
    }

    let result = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        // SAFETY: pointers were checked for null and are valid by C ABI contract.
        let context_ref = unsafe { &*(context as *const BackendContext) };
        if output_len < context_ref.screenshot_rgba.len() {
            return false;
        }
        unsafe {
            std::ptr::copy_nonoverlapping(
                context_ref.screenshot_rgba.as_ptr(),
                output,
                context_ref.screenshot_rgba.len(),
            );
        }
        true
    }));

    match result {
        Ok(value) => value,
        Err(payload) => {
            set_last_error(
                format!(
                    "WGPU screenshot copy panicked: {}",
                    panic_payload_to_string(payload.as_ref())
                )
                .as_str(),
            );
            false
        }
    }
}

#[no_mangle]
pub extern "C" fn babylon_wgpu_get_info(
    context: *const c_void,
    output_info: *mut BabylonWgpuInfo,
) -> bool {
    if output_info.is_null() {
        return false;
    }

    if context.is_null() {
        set_last_error("WGPU info requested with null backend context pointer.");
        return false;
    }

    // This interop layer is trusted app code (not untrusted web content), so
    // we keep ABI checks minimal and rely on upstream/runtime validation for
    // deeper invariants that are covered by WebGPU conformance paths.
    // SAFETY: pointers come from the C++ layer and are expected to remain valid.
    let context_ref = unsafe { &*(context as *const BackendContext) };
    // SAFETY: caller provides a valid writable output pointer.
    let output_info_ref = unsafe { &mut *output_info };
    *output_info_ref = context_ref.info;
    true
}

#[no_mangle]
pub extern "C" fn babylon_wgpu_get_last_error(output: *mut c_char, output_len: usize) -> bool {
    copy_last_error(output, output_len)
}

#[no_mangle]
pub extern "C" fn babylon_wgpu_dispatch_compute_global(
    shader_source: *const c_char,
    entry_point: *const c_char,
    x: u32,
    y: u32,
    z: u32,
) -> bool {
    if shader_source.is_null() {
        set_last_error("Compute dispatch shader source pointer was null.");
        return false;
    }
    if entry_point.is_null() {
        set_last_error("Compute dispatch entry point pointer was null.");
        return false;
    }

    // Keep conversion strict for deterministic diagnostics while avoiding
    // extra wrapper layers that duplicate downstream validation.
    // SAFETY: pointers are expected to reference NUL-terminated strings.
    let shader = match unsafe { CStr::from_ptr(shader_source) }.to_str() {
        Ok(value) => value,
        Err(_) => {
            set_last_error("Compute dispatch shader source was not valid UTF-8.");
            return false;
        }
    };
    // SAFETY: pointers are expected to reference NUL-terminated strings.
    let entry = match unsafe { CStr::from_ptr(entry_point) }.to_str() {
        Ok(value) => value,
        Err(_) => {
            set_last_error("Compute dispatch entry point was not valid UTF-8.");
            return false;
        }
    };

    let result = std::panic::catch_unwind(|| dispatch_compute_global(shader, entry, x, y, z));
    match result {
        Ok(value) => value,
        Err(payload) => {
            set_last_error(
                format!(
                    "Compute dispatch panicked: {}",
                    panic_payload_to_string(payload.as_ref())
                )
                .as_str(),
            );
            false
        }
    }
}

mod upstream_wgpu_native {
    use super::{
        opaque_ptr_as_ref, EXTERNAL_IMAGE_UPLOAD_BORROWED_BYTES,
        EXTERNAL_IMAGE_UPLOAD_BORROWED_COUNT, EXTERNAL_IMAGE_UPLOAD_OWNED_BYTES,
        EXTERNAL_IMAGE_UPLOAD_OWNED_COUNT,
    };
    use bytemuck::{Pod, Zeroable};
    use serde_json::Value;
    use std::borrow::Cow;
    use std::collections::HashMap;
    use std::ffi::c_void;
    use std::sync::atomic::{AtomicBool, Ordering};
    use std::sync::{Mutex, OnceLock};
    use wgpu::util::DeviceExt;

    #[derive(Clone, Debug)]
    pub struct AdapterProbeInfo {
        pub backend: u32,
        pub vendor_id: u32,
        pub device_id: u32,
        pub adapter_name: String,
    }

    pub struct LocalBootstrapRuntime {
        pub adapter: wgpu::Adapter,
        pub adapter_info: wgpu::AdapterInfo,
        pub limits: wgpu::Limits,
        pub device: wgpu::Device,
        pub queue: wgpu::Queue,
        pub used_fallback_adapter: bool,
    }

    pub struct LocalBootstrapConfig {
        pub width: u32,
        pub height: u32,
        pub surface_layer: *mut c_void,
        pub prefer_low_power: bool,
    }

    pub struct LocalRuntimeState {
        pub device: wgpu::Device,
        pub queue: wgpu::Queue,
        pub surface: Option<wgpu::Surface<'static>>,
        pub surface_config: Option<wgpu::SurfaceConfiguration>,
        pub resolved_adapter_info: AdapterProbeInfo,
        pub max_texture_dimension_2d: u32,
        pub width: u32,
        pub height: u32,
        pub render_target_format: wgpu::TextureFormat,
        pub used_fallback_adapter: bool,
        pub surface_acquire_failures: u32,
    }

    #[repr(C)]
    #[derive(Clone, Copy)]
    pub struct CanvasNativeTextureHandle {
        pub texture: *const c_void,
        pub device: *const c_void,
        pub queue: *const c_void,
        pub width: u32,
        pub height: u32,
        pub generation: u64,
    }

    pub struct DebugCubeRenderer {
        offscreen_texture: Option<wgpu::Texture>,
        offscreen_view: Option<wgpu::TextureView>,
        depth_texture: wgpu::Texture,
        depth_view: wgpu::TextureView,
        render_pipeline: wgpu::RenderPipeline,
        uniform_bind_group_layout: wgpu::BindGroupLayout,
        uniform_buffer: wgpu::Buffer,
        uniform_buffer_size: u64,
        uniform_bind_group: wgpu::BindGroup,
        canvas_sampler: wgpu::Sampler,
        canvas_texture: wgpu::Texture,
        canvas_texture_view: wgpu::TextureView,
        canvas_texture_width: u32,
        canvas_texture_height: u32,
        vertex_buffer: wgpu::Buffer,
        vertex_buffer_size: u64,
        index_buffer: wgpu::Buffer,
        index_buffer_size: u64,
        index_count: u32,
        width: u32,
        height: u32,
        frame_index: u64,
    }

    pub struct InteropBackendContext {
        runtime: LocalRuntimeState,
        resources: WebGpuResourceTable,
        offscreen_texture: Option<wgpu::Texture>,
        offscreen_view: Option<wgpu::TextureView>,
        offscreen_width: u32,
        offscreen_height: u32,
        offscreen_format: wgpu::TextureFormat,
        canvas_targets: HashMap<u64, CanvasTarget>,
        current_surface_frame: Option<wgpu::SurfaceTexture>,
        current_surface_frame_submitted: bool,
        current_canvas_texture: Option<wgpu::Texture>,
        current_canvas_texture_id: Option<u64>,
        current_canvas_id: Option<u64>,
    }

    pub struct ScreenshotData {
        pub width: u32,
        pub height: u32,
        pub rgba: Vec<u8>,
    }

    struct BufferResource {
        buffer: wgpu::Buffer,
        size: u64,
        mapped: bool,
    }

    struct TextureResource {
        texture: wgpu::Texture,
        width: u32,
        height: u32,
        depth_or_array_layers: u32,
        format: wgpu::TextureFormat,
    }

    struct CanvasTarget {
        texture: wgpu::Texture,
        view: wgpu::TextureView,
        texture_id: u64,
        width: u32,
        height: u32,
        format: wgpu::TextureFormat,
        usage: u32,
    }

    struct TextureViewResource {
        view: wgpu::TextureView,
        width: u32,
        height: u32,
    }

    struct SamplerResource {
        sampler: wgpu::Sampler,
    }

    struct ShaderModuleResource {
        module: wgpu::ShaderModule,
    }

    struct BindGroupLayoutResource {
        layout: wgpu::BindGroupLayout,
    }

    struct PipelineLayoutResource {
        layout: wgpu::PipelineLayout,
    }

    struct BindGroupResource {
        bind_group: wgpu::BindGroup,
    }

    struct RenderPipelineResource {
        pipeline: wgpu::RenderPipeline,
        vertex_buffer_slot_map: Option<Vec<(u32, u32)>>,
    }

    struct ComputePipelineResource {
        pipeline: wgpu::ComputePipeline,
    }

    #[derive(Clone)]
    struct ColorAttachmentCommand {
        view: wgpu::TextureView,
        resolve_target: Option<wgpu::TextureView>,
        load: wgpu::LoadOp<wgpu::Color>,
        store: wgpu::StoreOp,
        width: u32,
        height: u32,
    }

    #[derive(Clone)]
    struct DepthStencilAttachmentCommand {
        view: wgpu::TextureView,
        depth_ops: Option<wgpu::Operations<f32>>,
        stencil_ops: Option<wgpu::Operations<u32>>,
        width: u32,
        height: u32,
    }

    #[derive(Clone)]
    struct RenderPassDescriptorCommand {
        label: Option<String>,
        color_attachments: Vec<Option<ColorAttachmentCommand>>,
        depth_stencil_attachment: Option<DepthStencilAttachmentCommand>,
    }

    #[derive(Clone)]
    pub(super) enum RenderPassCommand {
        SetPipeline {
            id: u64,
            pipeline: wgpu::RenderPipeline,
            vertex_buffer_slot_map: Option<Vec<(u32, u32)>>,
        },
        SetBindGroup {
            index: u32,
            id: u64,
            bind_group: Option<wgpu::BindGroup>,
            dynamic_offsets: Vec<u32>,
        },
        SetVertexBuffer {
            slot: u32,
            id: u64,
            buffer: wgpu::Buffer,
            offset: u64,
            size: Option<u64>,
        },
        SetIndexBuffer {
            buffer: wgpu::Buffer,
            format: wgpu::IndexFormat,
            offset: u64,
            size: Option<u64>,
        },
        SetViewport {
            x: f32,
            y: f32,
            width: f32,
            height: f32,
            min_depth: f32,
            max_depth: f32,
        },
        SetScissorRect {
            x: u32,
            y: u32,
            width: u32,
            height: u32,
        },
        SetBlendConstant(wgpu::Color),
        SetStencilReference(u32),
        Draw {
            vertices: std::ops::Range<u32>,
            instances: std::ops::Range<u32>,
        },
        DrawIndexed {
            indices: std::ops::Range<u32>,
            base_vertex: i32,
            instances: std::ops::Range<u32>,
        },
        DrawIndirect {
            buffer: wgpu::Buffer,
            offset: u64,
        },
        DrawIndexedIndirect {
            buffer: wgpu::Buffer,
            offset: u64,
        },
    }

    struct RenderPassResource {
        encoder_id: u64,
        descriptor: RenderPassDescriptorCommand,
        commands: Vec<RenderPassCommand>,
        ended: bool,
    }

    #[derive(Clone)]
    enum ComputePassCommand {
        SetPipeline(wgpu::ComputePipeline),
        SetBindGroup {
            index: u32,
            bind_group: Option<wgpu::BindGroup>,
            dynamic_offsets: Vec<u32>,
        },
        DispatchWorkgroups {
            x: u32,
            y: u32,
            z: u32,
        },
        DispatchWorkgroupsIndirect {
            buffer: wgpu::Buffer,
            offset: u64,
        },
    }

    struct ComputePassResource {
        encoder_id: u64,
        label: Option<String>,
        commands: Vec<ComputePassCommand>,
    }

    #[derive(Clone)]
    enum EncoderCommand {
        RenderPass {
            descriptor: RenderPassDescriptorCommand,
            commands: Vec<RenderPassCommand>,
        },
        ComputePass {
            label: Option<String>,
            commands: Vec<ComputePassCommand>,
        },
        CopyBufferToBuffer {
            source: wgpu::Buffer,
            source_offset: u64,
            destination: wgpu::Buffer,
            destination_offset: u64,
            size: u64,
        },
        CopyBufferToTexture {
            source: wgpu::Buffer,
            source_layout: wgpu::TexelCopyBufferLayout,
            destination: wgpu::Texture,
            destination_mip_level: u32,
            destination_origin: wgpu::Origin3d,
            destination_aspect: wgpu::TextureAspect,
            size: wgpu::Extent3d,
        },
        CopyTextureToBuffer {
            source: wgpu::Texture,
            source_mip_level: u32,
            source_origin: wgpu::Origin3d,
            source_aspect: wgpu::TextureAspect,
            destination: wgpu::Buffer,
            destination_layout: wgpu::TexelCopyBufferLayout,
            size: wgpu::Extent3d,
        },
        CopyTextureToTexture {
            source: wgpu::Texture,
            source_mip_level: u32,
            source_origin: wgpu::Origin3d,
            source_aspect: wgpu::TextureAspect,
            destination: wgpu::Texture,
            destination_mip_level: u32,
            destination_origin: wgpu::Origin3d,
            destination_aspect: wgpu::TextureAspect,
            size: wgpu::Extent3d,
        },
        ClearBuffer {
            buffer: wgpu::Buffer,
            offset: u64,
            size: Option<u64>,
        },
    }

    struct CommandEncoderResource {
        commands: Vec<EncoderCommand>,
    }

    struct CommandBufferResource {
        commands: Vec<EncoderCommand>,
    }

    #[derive(Default)]
    struct WebGpuResourceTable {
        next_id: u64,
        buffers: HashMap<u64, BufferResource>,
        textures: HashMap<u64, TextureResource>,
        texture_views: HashMap<u64, TextureViewResource>,
        samplers: HashMap<u64, SamplerResource>,
        shader_modules: HashMap<u64, ShaderModuleResource>,
        bind_group_layouts: HashMap<u64, BindGroupLayoutResource>,
        pipeline_layouts: HashMap<u64, PipelineLayoutResource>,
        bind_groups: HashMap<u64, BindGroupResource>,
        render_pipelines: HashMap<u64, RenderPipelineResource>,
        compute_pipelines: HashMap<u64, ComputePipelineResource>,
        command_encoders: HashMap<u64, CommandEncoderResource>,
        render_passes: HashMap<u64, RenderPassResource>,
        compute_passes: HashMap<u64, ComputePassResource>,
        command_buffers: HashMap<u64, CommandBufferResource>,
    }

    impl WebGpuResourceTable {
        fn next(&mut self) -> u64 {
            self.next_id = self.next_id.saturating_add(1).max(1);
            self.next_id
        }
    }

    fn json_str<'a>(value: &'a Value, key: &str) -> Option<&'a str> {
        value.get(key).and_then(Value::as_str)
    }

    fn json_bool(value: &Value, key: &str, fallback: bool) -> bool {
        value.get(key).and_then(Value::as_bool).unwrap_or(fallback)
    }

    fn value_u32(value: &Value, fallback: u32) -> u32 {
        if let Some(raw) = value.as_u64() {
            return raw.min(u64::from(u32::MAX)) as u32;
        }

        if let Some(raw) = value.as_f64() {
            if raw.is_finite() && raw >= 0.0 {
                return raw.floor().min(f64::from(u32::MAX)) as u32;
            }
        }

        fallback
    }

    fn json_u32(value: &Value, key: &str, fallback: u32) -> u32 {
        value
            .get(key)
            .map(|raw| value_u32(raw, fallback))
            .unwrap_or(fallback)
    }

    fn json_u64(value: &Value, key: &str, fallback: u64) -> u64 {
        value.get(key).and_then(Value::as_u64).unwrap_or(fallback)
    }

    fn json_f64(value: &Value, key: &str, fallback: f64) -> f64 {
        value.get(key).and_then(Value::as_f64).unwrap_or(fallback)
    }

    fn native_id(value: &Value) -> Option<u64> {
        value
            .get("$nativeId")
            .or_else(|| value.get("__nativeId"))
            .and_then(Value::as_u64)
    }

    fn parse_json(json: &str) -> Result<Value, String> {
        serde_json::from_str(json)
            .map_err(|error| format!("invalid WebGPU descriptor JSON: {error}"))
    }

    fn map_buffer_usage(bits: u32) -> wgpu::BufferUsages {
        let mut usage = wgpu::BufferUsages::empty();
        if bits & 0x0001 != 0 {
            usage |= wgpu::BufferUsages::MAP_READ;
        }
        if bits & 0x0002 != 0 {
            usage |= wgpu::BufferUsages::MAP_WRITE;
        }
        if bits & 0x0004 != 0 {
            usage |= wgpu::BufferUsages::COPY_SRC;
        }
        if bits & 0x0008 != 0 {
            usage |= wgpu::BufferUsages::COPY_DST;
        }
        if bits & 0x0010 != 0 {
            usage |= wgpu::BufferUsages::INDEX;
        }
        if bits & 0x0020 != 0 {
            usage |= wgpu::BufferUsages::VERTEX;
        }
        if bits & 0x0040 != 0 {
            usage |= wgpu::BufferUsages::UNIFORM;
        }
        if bits & 0x0080 != 0 {
            usage |= wgpu::BufferUsages::STORAGE;
        }
        if bits & 0x0100 != 0 {
            usage |= wgpu::BufferUsages::INDIRECT;
        }
        if bits & 0x0200 != 0 {
            usage |= wgpu::BufferUsages::QUERY_RESOLVE;
        }
        usage
    }

    fn map_texture_usage(bits: u32) -> wgpu::TextureUsages {
        let mut usage = wgpu::TextureUsages::empty();
        if bits & 0x01 != 0 {
            usage |= wgpu::TextureUsages::COPY_SRC;
        }
        if bits & 0x02 != 0 {
            usage |= wgpu::TextureUsages::COPY_DST;
        }
        if bits & 0x04 != 0 {
            usage |= wgpu::TextureUsages::TEXTURE_BINDING;
        }
        if bits & 0x08 != 0 {
            usage |= wgpu::TextureUsages::STORAGE_BINDING;
        }
        if bits & 0x10 != 0 {
            usage |= wgpu::TextureUsages::RENDER_ATTACHMENT;
        }
        usage
    }

    fn map_shader_stage(bits: u32) -> wgpu::ShaderStages {
        let mut stages = wgpu::ShaderStages::empty();
        if bits & 0x1 != 0 {
            stages |= wgpu::ShaderStages::VERTEX;
        }
        if bits & 0x2 != 0 {
            stages |= wgpu::ShaderStages::FRAGMENT;
        }
        if bits & 0x4 != 0 {
            stages |= wgpu::ShaderStages::COMPUTE;
        }
        stages
    }

    fn map_texture_format(format: &str) -> Option<wgpu::TextureFormat> {
        Some(match format {
            "r8unorm" => wgpu::TextureFormat::R8Unorm,
            "r8snorm" => wgpu::TextureFormat::R8Snorm,
            "r8uint" => wgpu::TextureFormat::R8Uint,
            "r8sint" => wgpu::TextureFormat::R8Sint,
            "r16uint" => wgpu::TextureFormat::R16Uint,
            "r16sint" => wgpu::TextureFormat::R16Sint,
            "r16float" => wgpu::TextureFormat::R16Float,
            "rg8unorm" => wgpu::TextureFormat::Rg8Unorm,
            "rg8snorm" => wgpu::TextureFormat::Rg8Snorm,
            "rg8uint" => wgpu::TextureFormat::Rg8Uint,
            "rg8sint" => wgpu::TextureFormat::Rg8Sint,
            "r32uint" => wgpu::TextureFormat::R32Uint,
            "r32sint" => wgpu::TextureFormat::R32Sint,
            "r32float" => wgpu::TextureFormat::R32Float,
            "rg16uint" => wgpu::TextureFormat::Rg16Uint,
            "rg16sint" => wgpu::TextureFormat::Rg16Sint,
            "rg16float" => wgpu::TextureFormat::Rg16Float,
            "rgba8unorm" => wgpu::TextureFormat::Rgba8Unorm,
            "rgba8unorm-srgb" => wgpu::TextureFormat::Rgba8UnormSrgb,
            "rgba8snorm" => wgpu::TextureFormat::Rgba8Snorm,
            "rgba8uint" => wgpu::TextureFormat::Rgba8Uint,
            "rgba8sint" => wgpu::TextureFormat::Rgba8Sint,
            "bgra8unorm" => wgpu::TextureFormat::Bgra8Unorm,
            "bgra8unorm-srgb" => wgpu::TextureFormat::Bgra8UnormSrgb,
            "rgb10a2uint" => wgpu::TextureFormat::Rgb10a2Uint,
            "rgb10a2unorm" => wgpu::TextureFormat::Rgb10a2Unorm,
            "rg11b10ufloat" => wgpu::TextureFormat::Rg11b10Ufloat,
            "rgb9e5ufloat" => wgpu::TextureFormat::Rgb9e5Ufloat,
            "rg32uint" => wgpu::TextureFormat::Rg32Uint,
            "rg32sint" => wgpu::TextureFormat::Rg32Sint,
            "rg32float" => wgpu::TextureFormat::Rg32Float,
            "rgba16uint" => wgpu::TextureFormat::Rgba16Uint,
            "rgba16sint" => wgpu::TextureFormat::Rgba16Sint,
            "rgba16float" => wgpu::TextureFormat::Rgba16Float,
            "rgba32uint" => wgpu::TextureFormat::Rgba32Uint,
            "rgba32sint" => wgpu::TextureFormat::Rgba32Sint,
            "rgba32float" => wgpu::TextureFormat::Rgba32Float,
            "stencil8" => wgpu::TextureFormat::Stencil8,
            "depth16unorm" => wgpu::TextureFormat::Depth16Unorm,
            "depth24plus" => wgpu::TextureFormat::Depth24Plus,
            "depth24plus-stencil8" => wgpu::TextureFormat::Depth24PlusStencil8,
            "depth32float" => wgpu::TextureFormat::Depth32Float,
            "depth32float-stencil8" => wgpu::TextureFormat::Depth32FloatStencil8,
            _ => return None,
        })
    }

    fn texture_format_bytes_per_pixel(format: wgpu::TextureFormat) -> Option<u32> {
        Some(match format {
            wgpu::TextureFormat::R8Unorm
            | wgpu::TextureFormat::R8Snorm
            | wgpu::TextureFormat::R8Uint
            | wgpu::TextureFormat::R8Sint => 1,
            wgpu::TextureFormat::R16Uint
            | wgpu::TextureFormat::R16Sint
            | wgpu::TextureFormat::R16Float
            | wgpu::TextureFormat::Rg8Unorm
            | wgpu::TextureFormat::Rg8Snorm
            | wgpu::TextureFormat::Rg8Uint
            | wgpu::TextureFormat::Rg8Sint => 2,
            wgpu::TextureFormat::R32Uint
            | wgpu::TextureFormat::R32Sint
            | wgpu::TextureFormat::R32Float
            | wgpu::TextureFormat::Rg16Uint
            | wgpu::TextureFormat::Rg16Sint
            | wgpu::TextureFormat::Rg16Float
            | wgpu::TextureFormat::Rgba8Unorm
            | wgpu::TextureFormat::Rgba8UnormSrgb
            | wgpu::TextureFormat::Rgba8Snorm
            | wgpu::TextureFormat::Rgba8Uint
            | wgpu::TextureFormat::Rgba8Sint
            | wgpu::TextureFormat::Bgra8Unorm
            | wgpu::TextureFormat::Bgra8UnormSrgb => 4,
            wgpu::TextureFormat::Rg32Uint
            | wgpu::TextureFormat::Rg32Sint
            | wgpu::TextureFormat::Rg32Float
            | wgpu::TextureFormat::Rgba16Uint
            | wgpu::TextureFormat::Rgba16Sint
            | wgpu::TextureFormat::Rgba16Float => 8,
            wgpu::TextureFormat::Rgba32Uint
            | wgpu::TextureFormat::Rgba32Sint
            | wgpu::TextureFormat::Rgba32Float => 16,
            _ => return None,
        })
    }

    fn f32_to_f16_bits(value: f32) -> u16 {
        let bits = value.clamp(0.0, 1.0).to_bits();
        let sign = ((bits >> 16) & 0x8000) as u16;
        let exponent = ((bits >> 23) & 0xff) as i32 - 127 + 15;
        let mantissa = bits & 0x7fffff;

        if exponent <= 0 {
            if exponent < -10 {
                return sign;
            }
            let mantissa = mantissa | 0x800000;
            let shift = (14 - exponent) as u32;
            return sign | ((mantissa >> shift) as u16);
        }

        if exponent >= 31 {
            return sign | 0x7c00;
        }

        sign | ((exponent as u16) << 10) | (((mantissa + 0x1000) >> 13) as u16)
    }

    fn premultiply_u8(value: u8, alpha: u8) -> u8 {
        ((u32::from(value) * u32::from(alpha) + 127) / 255) as u8
    }

    fn unpremultiply_u8(value: u8, alpha: u8) -> u8 {
        if alpha == 0 {
            return 0;
        }

        let unpremultiplied = (u32::from(value) * 255 + u32::from(alpha) / 2) / u32::from(alpha);
        unpremultiplied.min(255) as u8
    }

    fn convert_alpha_mode(
        r: u8,
        g: u8,
        b: u8,
        a: u8,
        source_premultiplied_alpha: bool,
        destination_premultiplied_alpha: bool,
    ) -> [u8; 4] {
        if source_premultiplied_alpha == destination_premultiplied_alpha {
            return [r, g, b, a];
        }

        if destination_premultiplied_alpha {
            [
                premultiply_u8(r, a),
                premultiply_u8(g, a),
                premultiply_u8(b, a),
                a,
            ]
        } else {
            [
                unpremultiply_u8(r, a),
                unpremultiply_u8(g, a),
                unpremultiply_u8(b, a),
                a,
            ]
        }
    }

    fn encode_external_image_upload(
        rgba: &[u8],
        source_width: u32,
        source_origin_x: u32,
        source_origin_y: u32,
        copy_width: u32,
        copy_height: u32,
        flip_y: bool,
        source_premultiplied_alpha: bool,
        destination_premultiplied_alpha: bool,
        format: wgpu::TextureFormat,
    ) -> Result<(Cow<'_, [u8]>, u32), String> {
        let source_stride = source_width as usize * 4;
        let pixel_count = copy_width as usize * copy_height as usize;
        let bytes_per_pixel = texture_format_bytes_per_pixel(format).ok_or_else(|| {
            format!("copyExternalImageToTexture unsupported destination format {format:?}")
        })?;
        let upload_stride = copy_width as usize * bytes_per_pixel as usize;
        if !flip_y
            && source_origin_x == 0
            && source_origin_y == 0
            && copy_width == source_width
            && source_premultiplied_alpha == destination_premultiplied_alpha
            && matches!(
                format,
                wgpu::TextureFormat::Rgba8Unorm | wgpu::TextureFormat::Rgba8UnormSrgb
            )
        {
            let upload_len = upload_stride * copy_height as usize;
            if rgba.len() < upload_len {
                return Err("external image RGBA data contained too few bytes".to_string());
            }
            EXTERNAL_IMAGE_UPLOAD_BORROWED_COUNT.fetch_add(1, Ordering::Relaxed);
            EXTERNAL_IMAGE_UPLOAD_BORROWED_BYTES.fetch_add(upload_len as u64, Ordering::Relaxed);
            return Ok((Cow::Borrowed(&rgba[..upload_len]), upload_stride as u32));
        }

        let mut upload = vec![0; upload_stride * copy_height as usize];

        for row in 0..copy_height as usize {
            let source_row = if flip_y {
                source_origin_y as usize + copy_height as usize - 1 - row
            } else {
                source_origin_y as usize + row
            };
            let source_start = source_row * source_stride;
            let upload_start = row * upload_stride;
            for column in 0..copy_width as usize {
                let src = source_start + (source_origin_x as usize + column) * 4;
                let dst = upload_start + column * bytes_per_pixel as usize;
                let r = rgba[src];
                let g = rgba[src + 1];
                let b = rgba[src + 2];
                let a = rgba[src + 3];
                let [r, g, b, a] = convert_alpha_mode(
                    r,
                    g,
                    b,
                    a,
                    source_premultiplied_alpha,
                    destination_premultiplied_alpha,
                );
                match format {
                    wgpu::TextureFormat::Rgba8Unorm | wgpu::TextureFormat::Rgba8UnormSrgb => {
                        upload[dst..dst + 4].copy_from_slice(&[r, g, b, a]);
                    }
                    wgpu::TextureFormat::Bgra8Unorm | wgpu::TextureFormat::Bgra8UnormSrgb => {
                        upload[dst..dst + 4].copy_from_slice(&[b, g, r, a]);
                    }
                    wgpu::TextureFormat::Rgba16Float => {
                        let values = [
                            f32_to_f16_bits(r as f32 / 255.0),
                            f32_to_f16_bits(g as f32 / 255.0),
                            f32_to_f16_bits(b as f32 / 255.0),
                            f32_to_f16_bits(a as f32 / 255.0),
                        ];
                        for (index, value) in values.iter().enumerate() {
                            upload[dst + index * 2..dst + index * 2 + 2]
                                .copy_from_slice(&value.to_le_bytes());
                        }
                    }
                    wgpu::TextureFormat::Rgba32Float => {
                        let values = [
                            r as f32 / 255.0,
                            g as f32 / 255.0,
                            b as f32 / 255.0,
                            a as f32 / 255.0,
                        ];
                        for (index, value) in values.iter().enumerate() {
                            upload[dst + index * 4..dst + index * 4 + 4]
                                .copy_from_slice(&value.to_le_bytes());
                        }
                    }
                    _ => {
                        return Err(format!(
                            "copyExternalImageToTexture unsupported destination format {format:?}"
                        ));
                    }
                }
            }
        }

        debug_assert_eq!(upload.len(), pixel_count * bytes_per_pixel as usize);
        EXTERNAL_IMAGE_UPLOAD_OWNED_COUNT.fetch_add(1, Ordering::Relaxed);
        EXTERNAL_IMAGE_UPLOAD_OWNED_BYTES.fetch_add(upload.len() as u64, Ordering::Relaxed);
        Ok((Cow::Owned(upload), upload_stride as u32))
    }

    fn compatible_view_formats(format: wgpu::TextureFormat) -> Vec<wgpu::TextureFormat> {
        match format {
            wgpu::TextureFormat::Bgra8Unorm => vec![wgpu::TextureFormat::Bgra8UnormSrgb],
            wgpu::TextureFormat::Bgra8UnormSrgb => vec![wgpu::TextureFormat::Bgra8Unorm],
            wgpu::TextureFormat::Rgba8Unorm => vec![wgpu::TextureFormat::Rgba8UnormSrgb],
            wgpu::TextureFormat::Rgba8UnormSrgb => vec![wgpu::TextureFormat::Rgba8Unorm],
            _ => Vec::new(),
        }
    }

    fn map_texture_dimension(dimension: Option<&str>) -> wgpu::TextureDimension {
        match dimension.unwrap_or("2d") {
            "1d" => wgpu::TextureDimension::D1,
            "3d" => wgpu::TextureDimension::D3,
            _ => wgpu::TextureDimension::D2,
        }
    }

    fn map_texture_view_dimension(dimension: Option<&str>) -> Option<wgpu::TextureViewDimension> {
        Some(match dimension.unwrap_or("2d") {
            "1d" => wgpu::TextureViewDimension::D1,
            "2d" => wgpu::TextureViewDimension::D2,
            "2d-array" => wgpu::TextureViewDimension::D2Array,
            "cube" => wgpu::TextureViewDimension::Cube,
            "cube-array" => wgpu::TextureViewDimension::CubeArray,
            "3d" => wgpu::TextureViewDimension::D3,
            _ => return None,
        })
    }

    fn map_texture_aspect(aspect: Option<&str>) -> wgpu::TextureAspect {
        match aspect.unwrap_or("all") {
            "depth-only" => wgpu::TextureAspect::DepthOnly,
            "stencil-only" => wgpu::TextureAspect::StencilOnly,
            _ => wgpu::TextureAspect::All,
        }
    }

    fn map_vertex_format(format: &str) -> Option<wgpu::VertexFormat> {
        Some(match format {
            "uint8x2" => wgpu::VertexFormat::Uint8x2,
            "uint8x4" => wgpu::VertexFormat::Uint8x4,
            "sint8x2" => wgpu::VertexFormat::Sint8x2,
            "sint8x4" => wgpu::VertexFormat::Sint8x4,
            "unorm8x2" => wgpu::VertexFormat::Unorm8x2,
            "unorm8x4" => wgpu::VertexFormat::Unorm8x4,
            "snorm8x2" => wgpu::VertexFormat::Snorm8x2,
            "snorm8x4" => wgpu::VertexFormat::Snorm8x4,
            "uint16x2" => wgpu::VertexFormat::Uint16x2,
            "uint16x4" => wgpu::VertexFormat::Uint16x4,
            "sint16x2" => wgpu::VertexFormat::Sint16x2,
            "sint16x4" => wgpu::VertexFormat::Sint16x4,
            "unorm16x2" => wgpu::VertexFormat::Unorm16x2,
            "unorm16x4" => wgpu::VertexFormat::Unorm16x4,
            "snorm16x2" => wgpu::VertexFormat::Snorm16x2,
            "snorm16x4" => wgpu::VertexFormat::Snorm16x4,
            "float16x2" => wgpu::VertexFormat::Float16x2,
            "float16x4" => wgpu::VertexFormat::Float16x4,
            "float32" => wgpu::VertexFormat::Float32,
            "float32x2" => wgpu::VertexFormat::Float32x2,
            "float32x3" => wgpu::VertexFormat::Float32x3,
            "float32x4" => wgpu::VertexFormat::Float32x4,
            "uint32" => wgpu::VertexFormat::Uint32,
            "uint32x2" => wgpu::VertexFormat::Uint32x2,
            "uint32x3" => wgpu::VertexFormat::Uint32x3,
            "uint32x4" => wgpu::VertexFormat::Uint32x4,
            "sint32" => wgpu::VertexFormat::Sint32,
            "sint32x2" => wgpu::VertexFormat::Sint32x2,
            "sint32x3" => wgpu::VertexFormat::Sint32x3,
            "sint32x4" => wgpu::VertexFormat::Sint32x4,
            _ => return None,
        })
    }

    fn map_index_format(format: &str) -> Option<wgpu::IndexFormat> {
        Some(match format {
            "uint16" => wgpu::IndexFormat::Uint16,
            "uint32" => wgpu::IndexFormat::Uint32,
            _ => return None,
        })
    }

    fn map_step_mode(step_mode: Option<&str>) -> wgpu::VertexStepMode {
        match step_mode.unwrap_or("vertex") {
            "instance" => wgpu::VertexStepMode::Instance,
            _ => wgpu::VertexStepMode::Vertex,
        }
    }

    fn map_primitive_topology(topology: Option<&str>) -> wgpu::PrimitiveTopology {
        match topology.unwrap_or("triangle-list") {
            "point-list" => wgpu::PrimitiveTopology::PointList,
            "line-list" => wgpu::PrimitiveTopology::LineList,
            "line-strip" => wgpu::PrimitiveTopology::LineStrip,
            "triangle-strip" => wgpu::PrimitiveTopology::TriangleStrip,
            _ => wgpu::PrimitiveTopology::TriangleList,
        }
    }

    fn map_front_face(front_face: Option<&str>) -> wgpu::FrontFace {
        match front_face.unwrap_or("ccw") {
            "cw" => wgpu::FrontFace::Cw,
            _ => wgpu::FrontFace::Ccw,
        }
    }

    fn map_cull_mode(cull_mode: Option<&str>) -> Option<wgpu::Face> {
        match cull_mode.unwrap_or("none") {
            "front" => Some(wgpu::Face::Front),
            "back" => Some(wgpu::Face::Back),
            _ => None,
        }
    }

    fn map_compare(compare: Option<&str>) -> Option<wgpu::CompareFunction> {
        Some(match compare? {
            "never" => wgpu::CompareFunction::Never,
            "less" => wgpu::CompareFunction::Less,
            "equal" => wgpu::CompareFunction::Equal,
            "less-equal" => wgpu::CompareFunction::LessEqual,
            "greater" => wgpu::CompareFunction::Greater,
            "not-equal" => wgpu::CompareFunction::NotEqual,
            "greater-equal" => wgpu::CompareFunction::GreaterEqual,
            "always" => wgpu::CompareFunction::Always,
            _ => return None,
        })
    }

    fn map_address_mode(mode: Option<&str>) -> wgpu::AddressMode {
        match mode.unwrap_or("clamp-to-edge") {
            "repeat" => wgpu::AddressMode::Repeat,
            "mirror-repeat" => wgpu::AddressMode::MirrorRepeat,
            _ => wgpu::AddressMode::ClampToEdge,
        }
    }

    fn map_filter_mode(mode: Option<&str>) -> wgpu::FilterMode {
        match mode.unwrap_or("nearest") {
            "linear" => wgpu::FilterMode::Linear,
            _ => wgpu::FilterMode::Nearest,
        }
    }

    fn map_mipmap_filter_mode(mode: Option<&str>) -> wgpu::MipmapFilterMode {
        match mode.unwrap_or("nearest") {
            "linear" => wgpu::MipmapFilterMode::Linear,
            _ => wgpu::MipmapFilterMode::Nearest,
        }
    }

    fn map_blend_factor(factor: Option<&str>) -> wgpu::BlendFactor {
        match factor.unwrap_or("one") {
            "zero" => wgpu::BlendFactor::Zero,
            "one" => wgpu::BlendFactor::One,
            "src" => wgpu::BlendFactor::Src,
            "one-minus-src" => wgpu::BlendFactor::OneMinusSrc,
            "src-alpha" => wgpu::BlendFactor::SrcAlpha,
            "one-minus-src-alpha" => wgpu::BlendFactor::OneMinusSrcAlpha,
            "dst" => wgpu::BlendFactor::Dst,
            "one-minus-dst" => wgpu::BlendFactor::OneMinusDst,
            "dst-alpha" => wgpu::BlendFactor::DstAlpha,
            "one-minus-dst-alpha" => wgpu::BlendFactor::OneMinusDstAlpha,
            "src-alpha-saturated" => wgpu::BlendFactor::SrcAlphaSaturated,
            "constant" => wgpu::BlendFactor::Constant,
            "one-minus-constant" => wgpu::BlendFactor::OneMinusConstant,
            "src1" => wgpu::BlendFactor::Src1,
            "one-minus-src1" => wgpu::BlendFactor::OneMinusSrc1,
            "src1-alpha" => wgpu::BlendFactor::Src1Alpha,
            "one-minus-src1-alpha" => wgpu::BlendFactor::OneMinusSrc1Alpha,
            _ => wgpu::BlendFactor::One,
        }
    }

    fn map_blend_operation(operation: Option<&str>) -> wgpu::BlendOperation {
        match operation.unwrap_or("add") {
            "subtract" => wgpu::BlendOperation::Subtract,
            "reverse-subtract" => wgpu::BlendOperation::ReverseSubtract,
            "min" => wgpu::BlendOperation::Min,
            "max" => wgpu::BlendOperation::Max,
            _ => wgpu::BlendOperation::Add,
        }
    }

    fn parse_blend_component(value: Option<&Value>) -> wgpu::BlendComponent {
        let Some(value) = value else {
            return wgpu::BlendComponent::REPLACE;
        };
        wgpu::BlendComponent {
            src_factor: map_blend_factor(json_str(value, "srcFactor")),
            dst_factor: map_blend_factor(json_str(value, "dstFactor")),
            operation: map_blend_operation(json_str(value, "operation")),
        }
    }

    fn parse_blend_state(value: Option<&Value>) -> Option<wgpu::BlendState> {
        let value = value?;
        Some(wgpu::BlendState {
            color: parse_blend_component(value.get("color")),
            alpha: parse_blend_component(value.get("alpha")),
        })
    }

    fn parse_color(value: Option<&Value>) -> wgpu::Color {
        let Some(value) = value else {
            return wgpu::Color::BLACK;
        };
        if let Some(array) = value.as_array() {
            return wgpu::Color {
                r: array.get(0).and_then(Value::as_f64).unwrap_or(0.0),
                g: array.get(1).and_then(Value::as_f64).unwrap_or(0.0),
                b: array.get(2).and_then(Value::as_f64).unwrap_or(0.0),
                a: array.get(3).and_then(Value::as_f64).unwrap_or(1.0),
            };
        }
        wgpu::Color {
            r: json_f64(value, "r", 0.0),
            g: json_f64(value, "g", 0.0),
            b: json_f64(value, "b", 0.0),
            a: json_f64(value, "a", 1.0),
        }
    }

    fn parse_extent3d(value: &Value) -> wgpu::Extent3d {
        if let Some(array) = value.as_array() {
            return wgpu::Extent3d {
                width: array
                    .get(0)
                    .map(|raw| value_u32(raw, 1))
                    .unwrap_or(1)
                    .max(1),
                height: array
                    .get(1)
                    .map(|raw| value_u32(raw, 1))
                    .unwrap_or(1)
                    .max(1),
                depth_or_array_layers: array
                    .get(2)
                    .map(|raw| value_u32(raw, 1))
                    .unwrap_or(1)
                    .max(1),
            };
        }
        wgpu::Extent3d {
            width: json_u32(value, "width", 1).max(1),
            height: json_u32(value, "height", 1).max(1),
            depth_or_array_layers: json_u32(value, "depthOrArrayLayers", 1).max(1),
        }
    }

    fn parse_origin3d(value: Option<&Value>) -> wgpu::Origin3d {
        let Some(value) = value else {
            return wgpu::Origin3d::ZERO;
        };
        if let Some(array) = value.as_array() {
            return wgpu::Origin3d {
                x: array.get(0).and_then(Value::as_u64).unwrap_or(0) as u32,
                y: array.get(1).and_then(Value::as_u64).unwrap_or(0) as u32,
                z: array.get(2).and_then(Value::as_u64).unwrap_or(0) as u32,
            };
        }
        wgpu::Origin3d {
            x: json_u32(value, "x", 0),
            y: json_u32(value, "y", 0),
            z: json_u32(value, "z", 0),
        }
    }

    fn parse_buffer_layout(value: &Value) -> wgpu::TexelCopyBufferLayout {
        wgpu::TexelCopyBufferLayout {
            offset: json_u64(value, "offset", 0),
            bytes_per_row: value
                .get("bytesPerRow")
                .and_then(Value::as_u64)
                .map(|raw| raw.min(u64::from(u32::MAX)) as u32),
            rows_per_image: value
                .get("rowsPerImage")
                .and_then(Value::as_u64)
                .map(|raw| raw.min(u64::from(u32::MAX)) as u32),
        }
    }

    impl InteropBackendContext {
        pub fn create(config: LocalBootstrapConfig) -> Result<Self, String> {
            let runtime = LocalRuntimeState::bootstrap(config)?;
            let render_target_format = runtime.render_target_format;
            Ok(Self {
                runtime,
                resources: WebGpuResourceTable::default(),
                offscreen_texture: None,
                offscreen_view: None,
                offscreen_width: 0,
                offscreen_height: 0,
                offscreen_format: render_target_format,
                canvas_targets: HashMap::new(),
                current_surface_frame: None,
                current_surface_frame_submitted: false,
                current_canvas_texture: None,
                current_canvas_texture_id: None,
                current_canvas_id: None,
            })
        }

        pub fn resolved_adapter_info(&self) -> &AdapterProbeInfo {
            &self.runtime.resolved_adapter_info
        }

        pub fn used_fallback_adapter(&self) -> bool {
            self.runtime.used_fallback_adapter
        }

        pub fn estimated_gpu_memory_bytes(&self) -> u64 {
            let buffer_bytes = self
                .resources
                .buffers
                .values()
                .fold(0u64, |acc, buffer| acc.saturating_add(buffer.size));
            let texture_bytes = self.resources.textures.values().fold(0u64, |acc, texture| {
                acc.saturating_add(estimated_texture_bytes(
                    texture.width,
                    texture
                        .height
                        .saturating_mul(texture.depth_or_array_layers.max(1)),
                    texture.format,
                ))
            });
            buffer_bytes.saturating_add(texture_bytes)
        }

        pub fn install_debug_texture(&mut self, width: u32, height: u32, rgba: &[u8]) -> bool {
            let Some(texture) = self.current_canvas_texture.as_ref() else {
                return false;
            };
            let width = width.max(1);
            let height = height.max(1);
            let expected_len = width as usize * height as usize * 4;
            if rgba.len() < expected_len {
                return false;
            }
            self.runtime.queue.write_texture(
                wgpu::TexelCopyTextureInfo {
                    texture,
                    mip_level: 0,
                    origin: wgpu::Origin3d::ZERO,
                    aspect: wgpu::TextureAspect::All,
                },
                &rgba[..expected_len],
                wgpu::TexelCopyBufferLayout {
                    offset: 0,
                    bytes_per_row: Some(width.saturating_mul(4)),
                    rows_per_image: Some(height),
                },
                wgpu::Extent3d {
                    width,
                    height,
                    depth_or_array_layers: 1,
                },
            );
            true
        }

        pub fn resize(&mut self, width: u32, height: u32) {
            let (width, height) = self.runtime.clamped_extent(width, height);
            self.runtime.reconfigure_surface(width, height);
            self.offscreen_texture = None;
            self.offscreen_view = None;
            self.current_surface_frame = None;
            self.current_surface_frame_submitted = false;
            if let Some(id) = self.current_canvas_texture_id.take() {
                self.resources.textures.remove(&id);
            }
            for target in self.canvas_targets.drain().map(|(_, target)| target) {
                self.resources.textures.remove(&target.texture_id);
            }
            self.current_canvas_texture = None;
            self.current_canvas_id = None;
        }

        pub fn render(
            &mut self,
            _draw_enabled: bool,
            screenshot_requested: bool,
        ) -> Result<Option<ScreenshotData>, String> {
            let screenshot = if screenshot_requested {
                let source = self
                    .current_canvas_texture
                    .as_ref()
                    .or_else(|| self.offscreen_texture.as_ref());
                if std::env::var_os("BABYLON_NATIVE_WEBGPU_TRACE").is_some() {
                    eprintln!(
                        "NativeWebGPU trace screenshotSource: currentCanvasId={:?} hasCurrent={} hasOffscreen={} size={}x{}",
                        self.current_canvas_texture_id,
                        self.current_canvas_texture.is_some(),
                        self.offscreen_texture.is_some(),
                        self.runtime.width,
                        self.runtime.height
                    );
                }
                if let Some(source_texture) = source {
                    let mut encoder = self.runtime.device.create_command_encoder(
                        &wgpu::CommandEncoderDescriptor {
                            label: Some("babylon-native-webgpu.screenshot-encoder"),
                        },
                    );
                    let (staging_buffer, padded_bytes_per_row, unpadded_bytes_per_row) =
                        copy_texture_to_readback_buffer(
                            &self.runtime.device,
                            &mut encoder,
                            source_texture,
                            self.runtime.width,
                            self.runtime.height,
                        )?;
                    self.runtime.queue.submit(Some(encoder.finish()));
                    Some(map_readback_buffer_to_rgba(
                        &self.runtime.device,
                        &staging_buffer,
                        padded_bytes_per_row,
                        unpadded_bytes_per_row,
                        self.runtime.width,
                        self.runtime.height,
                        self.runtime.render_target_format,
                    )?)
                } else {
                    None
                }
            } else {
                None
            };

            if self.runtime.surface.is_some() && self.offscreen_texture.is_some() {
                if self
                    .runtime
                    .surface_config
                    .as_ref()
                    .map(|config| {
                        config.width != self.runtime.width || config.height != self.runtime.height
                    })
                    .unwrap_or(false)
                {
                    self.runtime
                        .reconfigure_surface(self.runtime.width, self.runtime.height);
                }

                if let (Some(surface), Some(source_texture)) = (
                    self.runtime.surface.as_ref(),
                    self.offscreen_texture.as_ref(),
                ) {
                    match surface.get_current_texture() {
                        wgpu::CurrentSurfaceTexture::Success(frame)
                        | wgpu::CurrentSurfaceTexture::Suboptimal(frame) => {
                            let mut encoder = self.runtime.device.create_command_encoder(
                                &wgpu::CommandEncoderDescriptor {
                                    label: Some("babylon-native-webgpu.present-offscreen"),
                                },
                            );
                            encoder.copy_texture_to_texture(
                                wgpu::TexelCopyTextureInfo {
                                    texture: source_texture,
                                    mip_level: 0,
                                    origin: wgpu::Origin3d::ZERO,
                                    aspect: wgpu::TextureAspect::All,
                                },
                                wgpu::TexelCopyTextureInfo {
                                    texture: &frame.texture,
                                    mip_level: 0,
                                    origin: wgpu::Origin3d::ZERO,
                                    aspect: wgpu::TextureAspect::All,
                                },
                                wgpu::Extent3d {
                                    width: self.runtime.width,
                                    height: self.runtime.height,
                                    depth_or_array_layers: 1,
                                },
                            );
                            self.runtime.queue.submit(Some(encoder.finish()));
                            frame.present();
                        }
                        wgpu::CurrentSurfaceTexture::Lost
                        | wgpu::CurrentSurfaceTexture::Outdated
                        | wgpu::CurrentSurfaceTexture::Validation => {
                            self.runtime
                                .reconfigure_surface(self.runtime.width, self.runtime.height);
                        }
                        wgpu::CurrentSurfaceTexture::Timeout
                        | wgpu::CurrentSurfaceTexture::Occluded => {}
                    }
                }
            }
            self.current_surface_frame = None;
            self.current_surface_frame_submitted = false;

            Ok(screenshot)
        }

        pub fn create_buffer(
            &mut self,
            size: u64,
            usage: u32,
            mapped_at_creation: bool,
        ) -> Result<u64, String> {
            let id = self.resources.next();
            let buffer = self.runtime.device.create_buffer(&wgpu::BufferDescriptor {
                label: Some("babylon-native-webgpu.web-buffer"),
                size: size.max(4),
                usage: map_buffer_usage(usage),
                mapped_at_creation,
            });
            self.resources.buffers.insert(
                id,
                BufferResource {
                    buffer,
                    size: size.max(4),
                    mapped: mapped_at_creation,
                },
            );
            Ok(id)
        }

        pub fn write_buffer(
            &mut self,
            buffer_id: u64,
            offset: u64,
            data: &[u8],
        ) -> Result<(), String> {
            let buffer = self
                .resources
                .buffers
                .get_mut(&buffer_id)
                .ok_or_else(|| format!("GPUBuffer {buffer_id} was not found"))?;
            if buffer.mapped {
                let start = offset.min(buffer.size);
                let available = buffer.size.saturating_sub(start);
                let copy_len = (data.len() as u64).min(available) as usize;
                if copy_len > 0 {
                    let end = start + copy_len as u64;
                    let mut mapped = buffer.buffer.slice(start..end).get_mapped_range_mut();
                    mapped.copy_from_slice(&data[..copy_len]);
                    drop(mapped);
                }
                buffer.buffer.unmap();
                buffer.mapped = false;
                return Ok(());
            }

            if data.is_empty() {
                return Ok(());
            }
            self.runtime
                .queue
                .write_buffer(&buffer.buffer, offset, data);
            Ok(())
        }

        pub fn create_texture(&mut self, descriptor_json: &str) -> Result<u64, String> {
            let descriptor = parse_json(descriptor_json)?;
            let size = descriptor
                .get("size")
                .map(parse_extent3d)
                .unwrap_or(wgpu::Extent3d {
                    width: 1,
                    height: 1,
                    depth_or_array_layers: 1,
                });
            let format_text = json_str(&descriptor, "format").unwrap_or("rgba8unorm");
            let format = map_texture_format(format_text)
                .ok_or_else(|| format!("unsupported GPUTexture format '{format_text}'"))?;
            let usage = map_texture_usage(json_u32(&descriptor, "usage", 0x10));
            let view_formats = compatible_view_formats(format);
            let texture = self
                .runtime
                .device
                .create_texture(&wgpu::TextureDescriptor {
                    label: json_str(&descriptor, "label"),
                    size,
                    mip_level_count: json_u32(&descriptor, "mipLevelCount", 1).max(1),
                    sample_count: json_u32(&descriptor, "sampleCount", 1).max(1),
                    dimension: map_texture_dimension(json_str(&descriptor, "dimension")),
                    format,
                    usage,
                    view_formats: &view_formats,
                });
            let id = self.resources.next();
            self.resources.textures.insert(
                id,
                TextureResource {
                    texture,
                    width: size.width,
                    height: size.height,
                    depth_or_array_layers: size.depth_or_array_layers,
                    format,
                },
            );
            Ok(id)
        }

        pub fn create_texture_view(
            &mut self,
            texture_id: u64,
            descriptor_json: &str,
        ) -> Result<u64, String> {
            let descriptor = if descriptor_json.is_empty() {
                Value::Object(Default::default())
            } else {
                parse_json(descriptor_json)?
            };
            let texture = self
                .resources
                .textures
                .get(&texture_id)
                .ok_or_else(|| format!("GPUTexture {texture_id} was not found"))?;
            let format = json_str(&descriptor, "format")
                .map(|format| {
                    map_texture_format(format)
                        .ok_or_else(|| format!("unsupported GPUTextureView format '{format}'"))
                })
                .transpose()?;
            let dimension = map_texture_view_dimension(json_str(&descriptor, "dimension"));
            let base_mip_level = json_u32(&descriptor, "baseMipLevel", 0);
            let width = texture
                .width
                .checked_shr(base_mip_level)
                .unwrap_or(0)
                .max(1);
            let height = texture
                .height
                .checked_shr(base_mip_level)
                .unwrap_or(0)
                .max(1);
            let view = texture.texture.create_view(&wgpu::TextureViewDescriptor {
                label: json_str(&descriptor, "label"),
                format,
                dimension,
                usage: None,
                aspect: map_texture_aspect(json_str(&descriptor, "aspect")),
                base_mip_level,
                mip_level_count: descriptor
                    .get("mipLevelCount")
                    .map(|raw| value_u32(raw, u32::MAX)),
                base_array_layer: json_u32(&descriptor, "baseArrayLayer", 0),
                array_layer_count: descriptor
                    .get("arrayLayerCount")
                    .and_then(Value::as_u64)
                    .map(|raw| raw.min(u64::from(u32::MAX)) as u32),
            });
            let id = self.resources.next();
            self.resources.texture_views.insert(
                id,
                TextureViewResource {
                    view,
                    width,
                    height,
                },
            );
            Ok(id)
        }

        pub fn create_sampler(&mut self, descriptor_json: &str) -> Result<u64, String> {
            let descriptor = parse_json(descriptor_json)?;
            let sampler = self
                .runtime
                .device
                .create_sampler(&wgpu::SamplerDescriptor {
                    label: json_str(&descriptor, "label"),
                    address_mode_u: map_address_mode(json_str(&descriptor, "addressModeU")),
                    address_mode_v: map_address_mode(json_str(&descriptor, "addressModeV")),
                    address_mode_w: map_address_mode(json_str(&descriptor, "addressModeW")),
                    mag_filter: map_filter_mode(json_str(&descriptor, "magFilter")),
                    min_filter: map_filter_mode(json_str(&descriptor, "minFilter")),
                    mipmap_filter: map_mipmap_filter_mode(json_str(&descriptor, "mipmapFilter")),
                    lod_min_clamp: json_f64(&descriptor, "lodMinClamp", 0.0) as f32,
                    lod_max_clamp: json_f64(&descriptor, "lodMaxClamp", 32.0) as f32,
                    compare: map_compare(json_str(&descriptor, "compare")),
                    anisotropy_clamp: json_u32(&descriptor, "maxAnisotropy", 1).min(16) as u16,
                    border_color: None,
                });
            let id = self.resources.next();
            self.resources
                .samplers
                .insert(id, SamplerResource { sampler });
            Ok(id)
        }

        pub fn create_shader_module(&mut self, code: &str) -> Result<u64, String> {
            let module = self
                .runtime
                .device
                .create_shader_module(wgpu::ShaderModuleDescriptor {
                    label: Some("babylon-native-webgpu.web-shader"),
                    source: wgpu::ShaderSource::Wgsl(code.into()),
                });
            let id = self.resources.next();
            self.resources
                .shader_modules
                .insert(id, ShaderModuleResource { module });
            Ok(id)
        }

        pub fn create_bind_group_layout(&mut self, descriptor_json: &str) -> Result<u64, String> {
            let descriptor = parse_json(descriptor_json)?;
            let mut entries = Vec::new();
            if let Some(array) = descriptor.get("entries").and_then(Value::as_array) {
                for entry in array {
                    let binding = json_u32(entry, "binding", 0);
                    let visibility = map_shader_stage(json_u32(entry, "visibility", 0));
                    let ty = if let Some(buffer) = entry.get("buffer") {
                        let buffer_type = match json_str(buffer, "type").unwrap_or("uniform") {
                            "storage" => wgpu::BufferBindingType::Storage { read_only: false },
                            "read-only-storage" => {
                                wgpu::BufferBindingType::Storage { read_only: true }
                            }
                            _ => wgpu::BufferBindingType::Uniform,
                        };
                        wgpu::BindingType::Buffer {
                            ty: buffer_type,
                            has_dynamic_offset: json_bool(buffer, "hasDynamicOffset", false),
                            min_binding_size: std::num::NonZeroU64::new(json_u64(
                                buffer,
                                "minBindingSize",
                                0,
                            )),
                        }
                    } else if let Some(sampler) = entry.get("sampler") {
                        let sampler_type = match json_str(sampler, "type").unwrap_or("filtering") {
                            "non-filtering" => wgpu::SamplerBindingType::NonFiltering,
                            "comparison" => wgpu::SamplerBindingType::Comparison,
                            _ => wgpu::SamplerBindingType::Filtering,
                        };
                        wgpu::BindingType::Sampler(sampler_type)
                    } else if let Some(texture) = entry.get("texture") {
                        let sample_type = match json_str(texture, "sampleType").unwrap_or("float") {
                            "unfilterable-float" => {
                                wgpu::TextureSampleType::Float { filterable: false }
                            }
                            "depth" => wgpu::TextureSampleType::Depth,
                            "sint" => wgpu::TextureSampleType::Sint,
                            "uint" => wgpu::TextureSampleType::Uint,
                            _ => wgpu::TextureSampleType::Float { filterable: true },
                        };
                        wgpu::BindingType::Texture {
                            sample_type,
                            view_dimension: map_texture_view_dimension(json_str(
                                texture,
                                "viewDimension",
                            ))
                            .unwrap_or(wgpu::TextureViewDimension::D2),
                            multisampled: json_bool(texture, "multisampled", false),
                        }
                    } else if let Some(storage_texture) = entry.get("storageTexture") {
                        let format_text =
                            json_str(storage_texture, "format").unwrap_or("rgba8unorm");
                        let access =
                            match json_str(storage_texture, "access").unwrap_or("write-only") {
                                "read-only" => wgpu::StorageTextureAccess::ReadOnly,
                                "read-write" => wgpu::StorageTextureAccess::ReadWrite,
                                _ => wgpu::StorageTextureAccess::WriteOnly,
                            };
                        wgpu::BindingType::StorageTexture {
                            access,
                            format: map_texture_format(format_text).ok_or_else(|| {
                                format!("unsupported storage texture format '{format_text}'")
                            })?,
                            view_dimension: map_texture_view_dimension(json_str(
                                storage_texture,
                                "viewDimension",
                            ))
                            .unwrap_or(wgpu::TextureViewDimension::D2),
                        }
                    } else {
                        continue;
                    };
                    entries.push(wgpu::BindGroupLayoutEntry {
                        binding,
                        visibility,
                        ty,
                        count: None,
                    });
                }
            }
            let layout =
                self.runtime
                    .device
                    .create_bind_group_layout(&wgpu::BindGroupLayoutDescriptor {
                        label: json_str(&descriptor, "label"),
                        entries: &entries,
                    });
            let id = self.resources.next();
            self.resources
                .bind_group_layouts
                .insert(id, BindGroupLayoutResource { layout });
            Ok(id)
        }

        pub fn create_pipeline_layout(&mut self, descriptor_json: &str) -> Result<u64, String> {
            let descriptor = parse_json(descriptor_json)?;
            let mut layouts = Vec::new();
            if let Some(array) = descriptor.get("bindGroupLayouts").and_then(Value::as_array) {
                for value in array {
                    let Some(layout_id) = native_id(value) else {
                        continue;
                    };
                    let layout = self
                        .resources
                        .bind_group_layouts
                        .get(&layout_id)
                        .ok_or_else(|| format!("GPUBindGroupLayout {layout_id} was not found"))?;
                    layouts.push(Some(&layout.layout));
                }
            }
            let layout =
                self.runtime
                    .device
                    .create_pipeline_layout(&wgpu::PipelineLayoutDescriptor {
                        label: json_str(&descriptor, "label"),
                        bind_group_layouts: &layouts,
                        immediate_size: 0,
                    });
            let id = self.resources.next();
            self.resources
                .pipeline_layouts
                .insert(id, PipelineLayoutResource { layout });
            Ok(id)
        }

        pub fn create_bind_group(&mut self, descriptor_json: &str) -> Result<u64, String> {
            enum EntrySpec {
                Buffer(usize),
                Sampler(usize),
                TextureView(usize),
            }

            let descriptor = parse_json(descriptor_json)?;
            let layout_id = descriptor
                .get("layout")
                .and_then(native_id)
                .ok_or_else(|| "GPUBindGroup descriptor missing layout".to_string())?;
            let layout = self
                .resources
                .bind_group_layouts
                .get(&layout_id)
                .ok_or_else(|| format!("GPUBindGroupLayout {layout_id} was not found"))?;
            let mut binding_numbers = Vec::new();
            let mut specs = Vec::new();
            let mut buffers: Vec<(wgpu::Buffer, u64, Option<std::num::NonZeroU64>)> = Vec::new();
            let mut samplers = Vec::new();
            let mut texture_views = Vec::new();

            if let Some(array) = descriptor.get("entries").and_then(Value::as_array) {
                for entry in array {
                    let binding = json_u32(entry, "binding", 0);
                    let Some(resource) = entry.get("resource") else {
                        continue;
                    };

                    if let Some(buffer_value) = resource.get("buffer") {
                        let buffer_id = native_id(buffer_value)
                            .ok_or_else(|| "GPUBufferBinding missing buffer handle".to_string())?;
                        let buffer = self
                            .resources
                            .buffers
                            .get(&buffer_id)
                            .ok_or_else(|| format!("GPUBuffer {buffer_id} was not found"))?;
                        let offset = json_u64(resource, "offset", 0);
                        let size = resource
                            .get("size")
                            .and_then(Value::as_u64)
                            .and_then(std::num::NonZeroU64::new);
                        binding_numbers.push(binding);
                        specs.push(EntrySpec::Buffer(buffers.len()));
                        buffers.push((buffer.buffer.clone(), offset, size));
                    } else if let Some(resource_id) = native_id(resource) {
                        if let Some(sampler) = self.resources.samplers.get(&resource_id) {
                            binding_numbers.push(binding);
                            specs.push(EntrySpec::Sampler(samplers.len()));
                            samplers.push(sampler.sampler.clone());
                        } else if let Some(view) = self.resources.texture_views.get(&resource_id) {
                            binding_numbers.push(binding);
                            specs.push(EntrySpec::TextureView(texture_views.len()));
                            texture_views.push(view.view.clone());
                        }
                    }
                }
            }

            let buffer_bindings: Vec<_> = buffers
                .iter()
                .map(|(buffer, offset, size)| wgpu::BufferBinding {
                    buffer,
                    offset: *offset,
                    size: *size,
                })
                .collect();
            let mut entries = Vec::with_capacity(specs.len());
            for (i, spec) in specs.iter().enumerate() {
                let resource = match *spec {
                    EntrySpec::Buffer(index) => {
                        wgpu::BindingResource::Buffer(buffer_bindings[index].clone())
                    }
                    EntrySpec::Sampler(index) => wgpu::BindingResource::Sampler(&samplers[index]),
                    EntrySpec::TextureView(index) => {
                        wgpu::BindingResource::TextureView(&texture_views[index])
                    }
                };
                entries.push(wgpu::BindGroupEntry {
                    binding: binding_numbers[i],
                    resource,
                });
            }

            let bind_group = self
                .runtime
                .device
                .create_bind_group(&wgpu::BindGroupDescriptor {
                    label: json_str(&descriptor, "label"),
                    layout: &layout.layout,
                    entries: &entries,
                });
            let id = self.resources.next();
            self.resources
                .bind_groups
                .insert(id, BindGroupResource { bind_group });
            Ok(id)
        }

        pub fn create_render_pipeline(&mut self, descriptor_json: &str) -> Result<u64, String> {
            let descriptor = parse_json(descriptor_json)?;
            let layout = descriptor.get("layout").and_then(native_id).and_then(|id| {
                self.resources
                    .pipeline_layouts
                    .get(&id)
                    .map(|value| &value.layout)
            });

            let vertex = descriptor
                .get("vertex")
                .ok_or_else(|| "GPURenderPipeline descriptor missing vertex state".to_string())?;
            let vertex_module_id = vertex
                .get("module")
                .and_then(native_id)
                .ok_or_else(|| "GPUVertexState missing shader module".to_string())?;
            let vertex_module = self
                .resources
                .shader_modules
                .get(&vertex_module_id)
                .ok_or_else(|| format!("GPUShaderModule {vertex_module_id} was not found"))?;
            let vertex_entry = json_str(vertex, "entryPoint").unwrap_or("main");

            let mut attribute_storage: Vec<Vec<wgpu::VertexAttribute>> = Vec::new();
            let mut layout_specs: Vec<(u64, wgpu::VertexStepMode, usize)> = Vec::new();
            let mut vertex_buffer_slot_map = Vec::new();
            let mut has_null_vertex_slots = false;
            if let Some(buffers) = vertex.get("buffers").and_then(Value::as_array) {
                for (slot, buffer) in buffers.iter().enumerate() {
                    if buffer.is_null() {
                        has_null_vertex_slots = true;
                        continue;
                    }
                    let mut attributes = Vec::new();
                    if let Some(attribute_array) =
                        buffer.get("attributes").and_then(Value::as_array)
                    {
                        for attribute in attribute_array {
                            let Some(format_text) = json_str(attribute, "format") else {
                                continue;
                            };
                            let Some(format) = map_vertex_format(format_text) else {
                                return Err(format!(
                                    "unsupported GPUVertexAttribute format '{format_text}'"
                                ));
                            };
                            attributes.push(wgpu::VertexAttribute {
                                format,
                                offset: json_u64(attribute, "offset", 0),
                                shader_location: json_u32(attribute, "shaderLocation", 0),
                            });
                        }
                    }
                    let attribute_index = attribute_storage.len();
                    attribute_storage.push(attributes);
                    layout_specs.push((
                        json_u64(buffer, "arrayStride", 0),
                        map_step_mode(json_str(buffer, "stepMode")),
                        attribute_index,
                    ));
                    vertex_buffer_slot_map.push((slot as u32, (layout_specs.len() - 1) as u32));
                }
            }
            let vertex_buffers: Vec<_> = layout_specs
                .iter()
                .map(
                    |(array_stride, step_mode, attribute_index)| wgpu::VertexBufferLayout {
                        array_stride: *array_stride,
                        step_mode: *step_mode,
                        attributes: &attribute_storage[*attribute_index],
                    },
                )
                .collect();

            let fragment_state = if let Some(fragment) = descriptor
                .get("fragment")
                .filter(|fragment| !fragment.is_null())
            {
                let module_id = fragment
                    .get("module")
                    .and_then(native_id)
                    .ok_or_else(|| "GPUFragmentState missing shader module".to_string())?;
                let module = self
                    .resources
                    .shader_modules
                    .get(&module_id)
                    .ok_or_else(|| format!("GPUShaderModule {module_id} was not found"))?;
                let entry = json_str(fragment, "entryPoint").unwrap_or("main");
                let mut targets = Vec::new();
                if let Some(target_array) = fragment.get("targets").and_then(Value::as_array) {
                    for target in target_array {
                        if target.is_null() {
                            targets.push(None);
                            continue;
                        }
                        let format_text = json_str(target, "format").unwrap_or("bgra8unorm");
                        let format = map_texture_format(format_text).ok_or_else(|| {
                            format!("unsupported GPUColorTargetState format '{format_text}'")
                        })?;
                        targets.push(Some(wgpu::ColorTargetState {
                            format,
                            blend: parse_blend_state(target.get("blend")),
                            write_mask: wgpu::ColorWrites::from_bits_truncate(json_u32(
                                target,
                                "writeMask",
                                wgpu::ColorWrites::ALL.bits(),
                            )),
                        }));
                    }
                }
                if !targets.is_empty() && targets.iter().all(Option::is_none) {
                    targets.clear();
                }
                Some((module.module.clone(), entry.to_owned(), targets))
            } else {
                None
            };

            let primitive_value = descriptor.get("primitive").unwrap_or(&Value::Null);
            let primitive = wgpu::PrimitiveState {
                topology: map_primitive_topology(json_str(primitive_value, "topology")),
                strip_index_format: json_str(primitive_value, "stripIndexFormat")
                    .and_then(map_index_format),
                front_face: map_front_face(json_str(primitive_value, "frontFace")),
                cull_mode: map_cull_mode(json_str(primitive_value, "cullMode")),
                unclipped_depth: json_bool(primitive_value, "unclippedDepth", false),
                polygon_mode: wgpu::PolygonMode::Fill,
                conservative: false,
            };

            let depth_stencil = descriptor
                .get("depthStencil")
                .filter(|depth| !depth.is_null())
                .map(|depth| {
                    let format_text = json_str(depth, "format").unwrap_or("depth24plus");
                    let format =
                        map_texture_format(format_text).unwrap_or(wgpu::TextureFormat::Depth24Plus);
                    wgpu::DepthStencilState {
                        format,
                        depth_write_enabled: Some(json_bool(depth, "depthWriteEnabled", false)),
                        depth_compare: map_compare(json_str(depth, "depthCompare")),
                        stencil: wgpu::StencilState::default(),
                        bias: wgpu::DepthBiasState {
                            constant: json_u32(depth, "depthBias", 0) as i32,
                            slope_scale: json_f64(depth, "depthBiasSlopeScale", 0.0) as f32,
                            clamp: json_f64(depth, "depthBiasClamp", 0.0) as f32,
                        },
                    }
                });

            let multisample_value = descriptor.get("multisample").unwrap_or(&Value::Null);
            let multisample = wgpu::MultisampleState {
                count: json_u32(multisample_value, "count", 1).max(1),
                mask: json_u64(multisample_value, "mask", !0u64),
                alpha_to_coverage_enabled: json_bool(
                    multisample_value,
                    "alphaToCoverageEnabled",
                    false,
                ),
            };

            let fragment =
                fragment_state
                    .as_ref()
                    .map(|(module, entry, targets)| wgpu::FragmentState {
                        module,
                        entry_point: Some(entry.as_str()),
                        compilation_options: wgpu::PipelineCompilationOptions::default(),
                        targets,
                    });

            let pipeline =
                self.runtime
                    .device
                    .create_render_pipeline(&wgpu::RenderPipelineDescriptor {
                        label: json_str(&descriptor, "label"),
                        layout,
                        vertex: wgpu::VertexState {
                            module: &vertex_module.module,
                            entry_point: Some(vertex_entry),
                            compilation_options: wgpu::PipelineCompilationOptions::default(),
                            buffers: &vertex_buffers,
                        },
                        primitive,
                        depth_stencil,
                        multisample,
                        fragment,
                        multiview_mask: None,
                        cache: None,
                    });

            let id = self.resources.next();
            self.resources.render_pipelines.insert(
                id,
                RenderPipelineResource {
                    pipeline,
                    vertex_buffer_slot_map: if has_null_vertex_slots {
                        Some(vertex_buffer_slot_map)
                    } else {
                        None
                    },
                },
            );
            Ok(id)
        }

        pub fn render_pipeline_get_bind_group_layout(
            &mut self,
            pipeline_id: u64,
            index: u32,
        ) -> Result<u64, String> {
            let pipeline = self
                .resources
                .render_pipelines
                .get(&pipeline_id)
                .ok_or_else(|| format!("GPURenderPipeline {pipeline_id} was not found"))?;
            let layout = pipeline.pipeline.get_bind_group_layout(index);
            let id = self.resources.next();
            self.resources
                .bind_group_layouts
                .insert(id, BindGroupLayoutResource { layout });
            Ok(id)
        }

        pub fn create_compute_pipeline(&mut self, descriptor_json: &str) -> Result<u64, String> {
            let descriptor = parse_json(descriptor_json)?;
            let layout = descriptor.get("layout").and_then(native_id).and_then(|id| {
                self.resources
                    .pipeline_layouts
                    .get(&id)
                    .map(|value| &value.layout)
            });

            let compute = descriptor
                .get("compute")
                .ok_or_else(|| "GPUComputePipeline descriptor missing compute state".to_string())?;
            let module_id = compute
                .get("module")
                .and_then(native_id)
                .ok_or_else(|| "GPUComputeState missing shader module".to_string())?;
            let module = self
                .resources
                .shader_modules
                .get(&module_id)
                .ok_or_else(|| format!("GPUShaderModule {module_id} was not found"))?;
            let entry = json_str(compute, "entryPoint").unwrap_or("main");

            let pipeline =
                self.runtime
                    .device
                    .create_compute_pipeline(&wgpu::ComputePipelineDescriptor {
                        label: json_str(&descriptor, "label"),
                        layout,
                        module: &module.module,
                        entry_point: Some(entry),
                        compilation_options: wgpu::PipelineCompilationOptions::default(),
                        cache: None,
                    });

            let id = self.resources.next();
            self.resources
                .compute_pipelines
                .insert(id, ComputePipelineResource { pipeline });
            Ok(id)
        }

        pub fn compute_pipeline_get_bind_group_layout(
            &mut self,
            pipeline_id: u64,
            index: u32,
        ) -> Result<u64, String> {
            let pipeline = self
                .resources
                .compute_pipelines
                .get(&pipeline_id)
                .ok_or_else(|| format!("GPUComputePipeline {pipeline_id} was not found"))?;
            let layout = pipeline.pipeline.get_bind_group_layout(index);
            let id = self.resources.next();
            self.resources
                .bind_group_layouts
                .insert(id, BindGroupLayoutResource { layout });
            Ok(id)
        }

        fn parse_texture_copy_view(
            &self,
            value: &Value,
        ) -> Result<(wgpu::Texture, u32, wgpu::Origin3d, wgpu::TextureAspect), String> {
            let texture_id = value
                .get("texture")
                .and_then(native_id)
                .ok_or_else(|| "GPUImageCopyTexture missing texture handle".to_string())?;
            let texture = self
                .resources
                .textures
                .get(&texture_id)
                .ok_or_else(|| format!("GPUTexture {texture_id} was not found"))?;
            Ok((
                texture.texture.clone(),
                json_u32(value, "mipLevel", 0),
                parse_origin3d(value.get("origin")),
                map_texture_aspect(json_str(value, "aspect")),
            ))
        }

        fn parse_render_pass_descriptor(
            &self,
            descriptor_json: &str,
        ) -> Result<RenderPassDescriptorCommand, String> {
            let descriptor = parse_json(descriptor_json)?;
            let mut color_attachments = Vec::new();
            if let Some(array) = descriptor.get("colorAttachments").and_then(Value::as_array) {
                for attachment in array {
                    if attachment.is_null() {
                        color_attachments.push(None);
                        continue;
                    }
                    let view_id = attachment
                        .get("view")
                        .and_then(native_id)
                        .ok_or_else(|| "GPURenderPassColorAttachment missing view".to_string())?;
                    let view = self
                        .resources
                        .texture_views
                        .get(&view_id)
                        .ok_or_else(|| format!("GPUTextureView {view_id} was not found"))?;
                    let resolve_target = attachment
                        .get("resolveTarget")
                        .and_then(native_id)
                        .map(|id| {
                            self.resources
                                .texture_views
                                .get(&id)
                                .map(|view| view.view.clone())
                                .ok_or_else(|| format!("GPUTextureView {id} was not found"))
                        })
                        .transpose()?;
                    let load = match json_str(attachment, "loadOp").unwrap_or("load") {
                        "clear" => wgpu::LoadOp::Clear(parse_color(attachment.get("clearValue"))),
                        _ => wgpu::LoadOp::Load,
                    };
                    let store = match json_str(attachment, "storeOp").unwrap_or("store") {
                        "discard" => wgpu::StoreOp::Discard,
                        _ => wgpu::StoreOp::Store,
                    };
                    color_attachments.push(Some(ColorAttachmentCommand {
                        view: view.view.clone(),
                        resolve_target,
                        load,
                        store,
                        width: view.width,
                        height: view.height,
                    }));
                }
            }

            let depth_stencil_attachment = descriptor
                .get("depthStencilAttachment")
                .filter(|attachment| !attachment.is_null())
                .map(|attachment| {
                    let view_id = attachment.get("view").and_then(native_id).ok_or_else(|| {
                        "GPURenderPassDepthStencilAttachment missing view".to_string()
                    })?;
                    let view = self
                        .resources
                        .texture_views
                        .get(&view_id)
                        .ok_or_else(|| format!("GPUTextureView {view_id} was not found"))?;
                    let depth_ops =
                        attachment
                            .get("depthLoadOp")
                            .and_then(Value::as_str)
                            .map(|load_op| wgpu::Operations {
                                load: if load_op == "clear" {
                                    wgpu::LoadOp::Clear(
                                        json_f64(attachment, "depthClearValue", 1.0) as f32,
                                    )
                                } else {
                                    wgpu::LoadOp::Load
                                },
                                store: if json_str(attachment, "depthStoreOp").unwrap_or("store")
                                    == "discard"
                                {
                                    wgpu::StoreOp::Discard
                                } else {
                                    wgpu::StoreOp::Store
                                },
                            });
                    let stencil_ops =
                        attachment
                            .get("stencilLoadOp")
                            .and_then(Value::as_str)
                            .map(|load_op| wgpu::Operations {
                                load: if load_op == "clear" {
                                    wgpu::LoadOp::Clear(json_u32(
                                        attachment,
                                        "stencilClearValue",
                                        0,
                                    ))
                                } else {
                                    wgpu::LoadOp::Load
                                },
                                store: if json_str(attachment, "stencilStoreOp").unwrap_or("store")
                                    == "discard"
                                {
                                    wgpu::StoreOp::Discard
                                } else {
                                    wgpu::StoreOp::Store
                                },
                            });
                    Ok::<_, String>(DepthStencilAttachmentCommand {
                        view: view.view.clone(),
                        depth_ops,
                        stencil_ops,
                        width: view.width,
                        height: view.height,
                    })
                })
                .transpose()?;

            Ok(RenderPassDescriptorCommand {
                label: json_str(&descriptor, "label").map(ToOwned::to_owned),
                color_attachments,
                depth_stencil_attachment,
            })
        }

        pub fn create_command_encoder(&mut self) -> Result<u64, String> {
            let id = self.resources.next();
            self.resources.command_encoders.insert(
                id,
                CommandEncoderResource {
                    commands: Vec::new(),
                },
            );
            Ok(id)
        }

        pub fn command_encoder_begin_render_pass(
            &mut self,
            encoder_id: u64,
            descriptor_json: &str,
        ) -> Result<u64, String> {
            if !self.resources.command_encoders.contains_key(&encoder_id) {
                return Err(format!("GPUCommandEncoder {encoder_id} was not found"));
            }
            let descriptor = self.parse_render_pass_descriptor(descriptor_json)?;
            let id = self.resources.next();
            self.resources.render_passes.insert(
                id,
                RenderPassResource {
                    encoder_id,
                    descriptor,
                    commands: Vec::new(),
                    ended: false,
                },
            );
            Ok(id)
        }

        pub fn command_encoder_begin_compute_pass(
            &mut self,
            encoder_id: u64,
            descriptor_json: &str,
        ) -> Result<u64, String> {
            if !self.resources.command_encoders.contains_key(&encoder_id) {
                return Err(format!("GPUCommandEncoder {encoder_id} was not found"));
            }
            let descriptor = parse_json(descriptor_json)?;
            let id = self.resources.next();
            self.resources.compute_passes.insert(
                id,
                ComputePassResource {
                    encoder_id,
                    label: json_str(&descriptor, "label").map(ToOwned::to_owned),
                    commands: Vec::new(),
                },
            );
            Ok(id)
        }

        pub fn render_pass_set_pipeline(
            &mut self,
            pass_id: u64,
            pipeline_id: u64,
        ) -> Result<(), String> {
            let pipeline = self
                .resources
                .render_pipelines
                .get(&pipeline_id)
                .ok_or_else(|| format!("GPURenderPipeline {pipeline_id} was not found"))?;
            let pipeline_handle = pipeline.pipeline.clone();
            let vertex_buffer_slot_map = pipeline.vertex_buffer_slot_map.clone();
            let pass = self
                .resources
                .render_passes
                .get_mut(&pass_id)
                .ok_or_else(|| format!("GPURenderPassEncoder {pass_id} was not found"))?;
            pass.commands.push(RenderPassCommand::SetPipeline {
                id: pipeline_id,
                pipeline: pipeline_handle,
                vertex_buffer_slot_map,
            });
            Ok(())
        }

        pub fn render_pass_set_bind_group(
            &mut self,
            pass_id: u64,
            index: u32,
            bind_group_id: u64,
            dynamic_offsets: &[u32],
        ) -> Result<(), String> {
            let bind_group = if bind_group_id == 0 {
                None
            } else {
                Some(
                    self.resources
                        .bind_groups
                        .get(&bind_group_id)
                        .ok_or_else(|| format!("GPUBindGroup {bind_group_id} was not found"))?
                        .bind_group
                        .clone(),
                )
            };
            let pass = self
                .resources
                .render_passes
                .get_mut(&pass_id)
                .ok_or_else(|| format!("GPURenderPassEncoder {pass_id} was not found"))?;
            pass.commands.push(RenderPassCommand::SetBindGroup {
                index,
                id: bind_group_id,
                bind_group,
                dynamic_offsets: dynamic_offsets.to_vec(),
            });
            Ok(())
        }

        pub fn render_pass_set_vertex_buffer(
            &mut self,
            pass_id: u64,
            slot: u32,
            buffer_id: u64,
            offset: u64,
            size: u64,
        ) -> Result<(), String> {
            let buffer_resource = self
                .resources
                .buffers
                .get(&buffer_id)
                .ok_or_else(|| format!("GPUBuffer {buffer_id} was not found"))?;
            let size = if size == u64::MAX {
                buffer_resource.size.saturating_sub(offset)
            } else {
                size
            };
            let buffer = buffer_resource.buffer.clone();
            let pass = self
                .resources
                .render_passes
                .get_mut(&pass_id)
                .ok_or_else(|| format!("GPURenderPassEncoder {pass_id} was not found"))?;
            pass.commands.push(RenderPassCommand::SetVertexBuffer {
                slot,
                id: buffer_id,
                buffer,
                offset,
                size: Some(size),
            });
            Ok(())
        }

        pub fn render_pass_set_index_buffer(
            &mut self,
            pass_id: u64,
            buffer_id: u64,
            format: &str,
            offset: u64,
            size: u64,
        ) -> Result<(), String> {
            let format = map_index_format(format)
                .ok_or_else(|| format!("unsupported GPUIndexFormat '{format}'"))?;
            let buffer_resource = self
                .resources
                .buffers
                .get(&buffer_id)
                .ok_or_else(|| format!("GPUBuffer {buffer_id} was not found"))?;
            let size = if size == u64::MAX {
                buffer_resource.size.saturating_sub(offset)
            } else {
                size
            };
            let buffer = buffer_resource.buffer.clone();
            let pass = self
                .resources
                .render_passes
                .get_mut(&pass_id)
                .ok_or_else(|| format!("GPURenderPassEncoder {pass_id} was not found"))?;
            pass.commands.push(RenderPassCommand::SetIndexBuffer {
                buffer,
                format,
                offset,
                size: Some(size),
            });
            Ok(())
        }

        pub fn render_pass_push_command(
            &mut self,
            pass_id: u64,
            command: RenderPassCommand,
        ) -> Result<(), String> {
            let pass = self
                .resources
                .render_passes
                .get_mut(&pass_id)
                .ok_or_else(|| format!("GPURenderPassEncoder {pass_id} was not found"))?;
            pass.commands.push(command);
            Ok(())
        }

        pub fn render_pass_draw_indirect(
            &mut self,
            pass_id: u64,
            buffer_id: u64,
            offset: u64,
        ) -> Result<(), String> {
            let buffer = self
                .resources
                .buffers
                .get(&buffer_id)
                .ok_or_else(|| format!("GPUBuffer {buffer_id} was not found"))?
                .buffer
                .clone();
            self.render_pass_push_command(
                pass_id,
                RenderPassCommand::DrawIndirect { buffer, offset },
            )
        }

        pub fn render_pass_draw_indexed_indirect(
            &mut self,
            pass_id: u64,
            buffer_id: u64,
            offset: u64,
        ) -> Result<(), String> {
            let buffer = self
                .resources
                .buffers
                .get(&buffer_id)
                .ok_or_else(|| format!("GPUBuffer {buffer_id} was not found"))?
                .buffer
                .clone();
            self.render_pass_push_command(
                pass_id,
                RenderPassCommand::DrawIndexedIndirect { buffer, offset },
            )
        }

        pub fn render_pass_end(&mut self, pass_id: u64) -> Result<(), String> {
            let pass = self
                .resources
                .render_passes
                .remove(&pass_id)
                .ok_or_else(|| format!("GPURenderPassEncoder {pass_id} was not found"))?;
            let encoder = self
                .resources
                .command_encoders
                .get_mut(&pass.encoder_id)
                .ok_or_else(|| format!("GPUCommandEncoder {} was not found", pass.encoder_id))?;
            encoder.commands.push(EncoderCommand::RenderPass {
                descriptor: pass.descriptor,
                commands: pass.commands,
            });
            Ok(())
        }

        pub fn compute_pass_set_pipeline(
            &mut self,
            pass_id: u64,
            pipeline_id: u64,
        ) -> Result<(), String> {
            let pipeline = self
                .resources
                .compute_pipelines
                .get(&pipeline_id)
                .ok_or_else(|| format!("GPUComputePipeline {pipeline_id} was not found"))?
                .pipeline
                .clone();
            let pass = self
                .resources
                .compute_passes
                .get_mut(&pass_id)
                .ok_or_else(|| format!("GPUComputePassEncoder {pass_id} was not found"))?;
            pass.commands
                .push(ComputePassCommand::SetPipeline(pipeline));
            Ok(())
        }

        pub fn compute_pass_set_bind_group(
            &mut self,
            pass_id: u64,
            index: u32,
            bind_group_id: u64,
            dynamic_offsets: &[u32],
        ) -> Result<(), String> {
            let bind_group = if bind_group_id == 0 {
                None
            } else {
                Some(
                    self.resources
                        .bind_groups
                        .get(&bind_group_id)
                        .ok_or_else(|| format!("GPUBindGroup {bind_group_id} was not found"))?
                        .bind_group
                        .clone(),
                )
            };
            let pass = self
                .resources
                .compute_passes
                .get_mut(&pass_id)
                .ok_or_else(|| format!("GPUComputePassEncoder {pass_id} was not found"))?;
            pass.commands.push(ComputePassCommand::SetBindGroup {
                index,
                bind_group,
                dynamic_offsets: dynamic_offsets.to_vec(),
            });
            Ok(())
        }

        pub fn compute_pass_dispatch_workgroups(
            &mut self,
            pass_id: u64,
            x: u32,
            y: u32,
            z: u32,
        ) -> Result<(), String> {
            let pass = self
                .resources
                .compute_passes
                .get_mut(&pass_id)
                .ok_or_else(|| format!("GPUComputePassEncoder {pass_id} was not found"))?;
            pass.commands
                .push(ComputePassCommand::DispatchWorkgroups { x, y, z });
            Ok(())
        }

        pub fn compute_pass_dispatch_workgroups_indirect(
            &mut self,
            pass_id: u64,
            buffer_id: u64,
            offset: u64,
        ) -> Result<(), String> {
            let buffer = self
                .resources
                .buffers
                .get(&buffer_id)
                .ok_or_else(|| format!("GPUBuffer {buffer_id} was not found"))?
                .buffer
                .clone();
            let pass = self
                .resources
                .compute_passes
                .get_mut(&pass_id)
                .ok_or_else(|| format!("GPUComputePassEncoder {pass_id} was not found"))?;
            pass.commands
                .push(ComputePassCommand::DispatchWorkgroupsIndirect { buffer, offset });
            Ok(())
        }

        pub fn compute_pass_end(&mut self, pass_id: u64) -> Result<(), String> {
            let pass = self
                .resources
                .compute_passes
                .remove(&pass_id)
                .ok_or_else(|| format!("GPUComputePassEncoder {pass_id} was not found"))?;
            let encoder = self
                .resources
                .command_encoders
                .get_mut(&pass.encoder_id)
                .ok_or_else(|| format!("GPUCommandEncoder {} was not found", pass.encoder_id))?;
            encoder.commands.push(EncoderCommand::ComputePass {
                label: pass.label,
                commands: pass.commands,
            });
            Ok(())
        }

        pub fn command_encoder_copy_buffer_to_buffer(
            &mut self,
            encoder_id: u64,
            source_id: u64,
            source_offset: u64,
            destination_id: u64,
            destination_offset: u64,
            size: u64,
        ) -> Result<(), String> {
            let source = self
                .resources
                .buffers
                .get(&source_id)
                .ok_or_else(|| format!("GPUBuffer {source_id} was not found"))?
                .buffer
                .clone();
            let destination = self
                .resources
                .buffers
                .get(&destination_id)
                .ok_or_else(|| format!("GPUBuffer {destination_id} was not found"))?
                .buffer
                .clone();
            let encoder = self
                .resources
                .command_encoders
                .get_mut(&encoder_id)
                .ok_or_else(|| format!("GPUCommandEncoder {encoder_id} was not found"))?;
            encoder.commands.push(EncoderCommand::CopyBufferToBuffer {
                source,
                source_offset,
                destination,
                destination_offset,
                size,
            });
            Ok(())
        }

        pub fn command_encoder_copy_buffer_to_texture(
            &mut self,
            encoder_id: u64,
            source_json: &str,
            destination_json: &str,
            size_json: &str,
        ) -> Result<(), String> {
            let source = parse_json(source_json)?;
            let destination = parse_json(destination_json)?;
            let size = parse_extent3d(&parse_json(size_json)?);
            let buffer_id = source
                .get("buffer")
                .and_then(native_id)
                .ok_or_else(|| "GPUImageCopyBuffer missing buffer handle".to_string())?;
            let buffer = self
                .resources
                .buffers
                .get(&buffer_id)
                .ok_or_else(|| format!("GPUBuffer {buffer_id} was not found"))?
                .buffer
                .clone();
            let (texture, mip_level, origin, aspect) =
                self.parse_texture_copy_view(&destination)?;
            let encoder = self
                .resources
                .command_encoders
                .get_mut(&encoder_id)
                .ok_or_else(|| format!("GPUCommandEncoder {encoder_id} was not found"))?;
            encoder.commands.push(EncoderCommand::CopyBufferToTexture {
                source: buffer,
                source_layout: parse_buffer_layout(&source),
                destination: texture,
                destination_mip_level: mip_level,
                destination_origin: origin,
                destination_aspect: aspect,
                size,
            });
            Ok(())
        }

        pub fn command_encoder_copy_texture_to_buffer(
            &mut self,
            encoder_id: u64,
            source_json: &str,
            destination_json: &str,
            size_json: &str,
        ) -> Result<(), String> {
            let source = parse_json(source_json)?;
            let destination = parse_json(destination_json)?;
            let size = parse_extent3d(&parse_json(size_json)?);
            let (texture, mip_level, origin, aspect) = self.parse_texture_copy_view(&source)?;
            let buffer_id = destination
                .get("buffer")
                .and_then(native_id)
                .ok_or_else(|| "GPUImageCopyBuffer missing buffer handle".to_string())?;
            let buffer = self
                .resources
                .buffers
                .get(&buffer_id)
                .ok_or_else(|| format!("GPUBuffer {buffer_id} was not found"))?
                .buffer
                .clone();
            let encoder = self
                .resources
                .command_encoders
                .get_mut(&encoder_id)
                .ok_or_else(|| format!("GPUCommandEncoder {encoder_id} was not found"))?;
            encoder.commands.push(EncoderCommand::CopyTextureToBuffer {
                source: texture,
                source_mip_level: mip_level,
                source_origin: origin,
                source_aspect: aspect,
                destination: buffer,
                destination_layout: parse_buffer_layout(&destination),
                size,
            });
            Ok(())
        }

        pub fn command_encoder_copy_texture_to_texture(
            &mut self,
            encoder_id: u64,
            source_json: &str,
            destination_json: &str,
            size_json: &str,
        ) -> Result<(), String> {
            let source = parse_json(source_json)?;
            let destination = parse_json(destination_json)?;
            let size = parse_extent3d(&parse_json(size_json)?);
            let (source_texture, source_mip, source_origin, source_aspect) =
                self.parse_texture_copy_view(&source)?;
            let (destination_texture, destination_mip, destination_origin, destination_aspect) =
                self.parse_texture_copy_view(&destination)?;
            let encoder = self
                .resources
                .command_encoders
                .get_mut(&encoder_id)
                .ok_or_else(|| format!("GPUCommandEncoder {encoder_id} was not found"))?;
            encoder.commands.push(EncoderCommand::CopyTextureToTexture {
                source: source_texture,
                source_mip_level: source_mip,
                source_origin,
                source_aspect,
                destination: destination_texture,
                destination_mip_level: destination_mip,
                destination_origin,
                destination_aspect,
                size,
            });
            Ok(())
        }

        pub fn command_encoder_clear_buffer(
            &mut self,
            encoder_id: u64,
            buffer_id: u64,
            offset: u64,
            size: u64,
        ) -> Result<(), String> {
            let buffer_resource = self
                .resources
                .buffers
                .get(&buffer_id)
                .ok_or_else(|| format!("GPUBuffer {buffer_id} was not found"))?;
            let offset = offset.min(buffer_resource.size);
            let available = buffer_resource.size.saturating_sub(offset);
            let size = if size == u64::MAX {
                None
            } else {
                Some(size.min(available))
            };
            let buffer = buffer_resource.buffer.clone();
            let encoder = self
                .resources
                .command_encoders
                .get_mut(&encoder_id)
                .ok_or_else(|| format!("GPUCommandEncoder {encoder_id} was not found"))?;
            encoder.commands.push(EncoderCommand::ClearBuffer {
                buffer,
                offset,
                size,
            });
            Ok(())
        }

        pub fn command_encoder_finish(&mut self, encoder_id: u64) -> Result<u64, String> {
            let encoder = self
                .resources
                .command_encoders
                .remove(&encoder_id)
                .ok_or_else(|| format!("GPUCommandEncoder {encoder_id} was not found"))?;
            let id = self.resources.next();
            self.resources.command_buffers.insert(
                id,
                CommandBufferResource {
                    commands: encoder.commands,
                },
            );
            Ok(id)
        }

        fn execute_encoder_command(
            &mut self,
            encoder: &mut wgpu::CommandEncoder,
            command: &EncoderCommand,
        ) {
            match command {
                EncoderCommand::RenderPass {
                    descriptor,
                    commands,
                } => {
                    if std::env::var_os("BABYLON_NATIVE_WEBGPU_TRACE").is_some()
                        && descriptor
                            .label
                            .as_deref()
                            .map(|label| {
                                label.contains("MainRenderPass") || label.contains("shadowMap")
                            })
                            .unwrap_or(false)
                    {
                        let command_labels = commands
                            .iter()
                            .map(|command| match command {
                                RenderPassCommand::SetPipeline { id, .. } => {
                                    format!("setPipeline({id})")
                                }
                                RenderPassCommand::SetBindGroup { index, id, .. } => {
                                    if let RenderPassCommand::SetBindGroup {
                                        dynamic_offsets, ..
                                    } = command
                                    {
                                        if *id == 0 {
                                            format!(
                                                "setBindGroup({index},null,{dynamic_offsets:?})"
                                            )
                                        } else {
                                            format!(
                                                "setBindGroup({index},id={id},{dynamic_offsets:?})"
                                            )
                                        }
                                    } else {
                                        format!("setBindGroup({index})")
                                    }
                                }
                                RenderPassCommand::SetVertexBuffer { slot, id, .. } => {
                                    format!("setVertexBuffer({slot},id={id})")
                                }
                                RenderPassCommand::SetIndexBuffer { .. } => {
                                    "setIndexBuffer".to_string()
                                }
                                RenderPassCommand::SetViewport { .. } => "setViewport".to_string(),
                                RenderPassCommand::SetScissorRect { .. } => {
                                    "setScissorRect".to_string()
                                }
                                RenderPassCommand::SetBlendConstant(_) => {
                                    "setBlendConstant".to_string()
                                }
                                RenderPassCommand::SetStencilReference(_) => {
                                    "setStencilReference".to_string()
                                }
                                RenderPassCommand::Draw {
                                    vertices,
                                    instances,
                                } => {
                                    format!("draw({:?},{:?})", vertices, instances)
                                }
                                RenderPassCommand::DrawIndexed {
                                    indices,
                                    base_vertex,
                                    instances,
                                } => format!(
                                    "drawIndexed({:?},base={},inst={:?})",
                                    indices, base_vertex, instances
                                ),
                                RenderPassCommand::DrawIndirect { .. } => {
                                    "drawIndirect".to_string()
                                }
                                RenderPassCommand::DrawIndexedIndirect { .. } => {
                                    "drawIndexedIndirect".to_string()
                                }
                            })
                            .collect::<Vec<_>>()
                            .join(",");
                        eprintln!(
                            "NativeWebGPU trace executeRenderPass: {} [{}]",
                            descriptor.label.as_deref().unwrap_or("(unlabeled)"),
                            command_labels
                        );
                    }
                    let color_attachments: Vec<_> =
                        if descriptor.color_attachments.iter().all(Option::is_none) {
                            Vec::new()
                        } else {
                            descriptor
                                .color_attachments
                                .iter()
                                .map(|attachment| {
                                    attachment.as_ref().map(|attachment| {
                                        wgpu::RenderPassColorAttachment {
                                            view: &attachment.view,
                                            depth_slice: None,
                                            resolve_target: attachment.resolve_target.as_ref(),
                                            ops: wgpu::Operations {
                                                load: attachment.load,
                                                store: attachment.store,
                                            },
                                        }
                                    })
                                })
                                .collect()
                        };
                    let depth_stencil_attachment = descriptor
                        .depth_stencil_attachment
                        .as_ref()
                        .map(|attachment| wgpu::RenderPassDepthStencilAttachment {
                            view: &attachment.view,
                            depth_ops: attachment.depth_ops,
                            stencil_ops: attachment.stencil_ops,
                        });
                    let render_target_extent = descriptor
                        .color_attachments
                        .iter()
                        .find_map(|attachment| {
                            attachment
                                .as_ref()
                                .map(|attachment| (attachment.width, attachment.height))
                        })
                        .or_else(|| {
                            descriptor
                                .depth_stencil_attachment
                                .as_ref()
                                .map(|attachment| (attachment.width, attachment.height))
                        });
                    let mut pass = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
                        label: descriptor.label.as_deref(),
                        color_attachments: &color_attachments,
                        depth_stencil_attachment,
                        occlusion_query_set: None,
                        timestamp_writes: None,
                        multiview_mask: None,
                    });
                    let mut current_vertex_buffer_slot_map: Option<Vec<(u32, u32)>> = None;
                    for command in commands {
                        match command {
                            RenderPassCommand::SetPipeline {
                                pipeline,
                                vertex_buffer_slot_map,
                                ..
                            } => {
                                pass.set_pipeline(pipeline);
                                current_vertex_buffer_slot_map = vertex_buffer_slot_map.clone();
                            }
                            RenderPassCommand::SetBindGroup {
                                index,
                                bind_group,
                                dynamic_offsets,
                                ..
                            } => pass.set_bind_group(*index, bind_group.as_ref(), dynamic_offsets),
                            RenderPassCommand::SetVertexBuffer {
                                slot,
                                buffer,
                                offset,
                                size,
                                ..
                            } => {
                                let mapped_slot = match current_vertex_buffer_slot_map.as_ref() {
                                    Some(slot_map) => {
                                        slot_map.iter().find_map(|(source, target)| {
                                            (*source == *slot).then_some(*target)
                                        })
                                    }
                                    None => Some(*slot),
                                };
                                let Some(mapped_slot) = mapped_slot else {
                                    continue;
                                };
                                if let Some(size) = size {
                                    pass.set_vertex_buffer(
                                        mapped_slot,
                                        buffer.slice(*offset..offset.saturating_add(*size)),
                                    );
                                } else {
                                    pass.set_vertex_buffer(mapped_slot, buffer.slice(*offset..));
                                }
                            }
                            RenderPassCommand::SetIndexBuffer {
                                buffer,
                                format,
                                offset,
                                size,
                            } => {
                                if let Some(size) = size {
                                    pass.set_index_buffer(
                                        buffer.slice(*offset..offset.saturating_add(*size)),
                                        *format,
                                    );
                                } else {
                                    pass.set_index_buffer(buffer.slice(*offset..), *format);
                                }
                            }
                            RenderPassCommand::SetViewport {
                                x,
                                y,
                                width,
                                height,
                                min_depth,
                                max_depth,
                            } => pass.set_viewport(*x, *y, *width, *height, *min_depth, *max_depth),
                            RenderPassCommand::SetScissorRect {
                                x,
                                y,
                                width,
                                height,
                            } => {
                                if let Some((target_width, target_height)) = render_target_extent {
                                    let clamped_x = (*x).min(target_width);
                                    let clamped_y = (*y).min(target_height);
                                    pass.set_scissor_rect(
                                        clamped_x,
                                        clamped_y,
                                        (*width).min(target_width.saturating_sub(clamped_x)),
                                        (*height).min(target_height.saturating_sub(clamped_y)),
                                    );
                                } else {
                                    pass.set_scissor_rect(*x, *y, *width, *height);
                                }
                            }
                            RenderPassCommand::SetBlendConstant(color) => {
                                pass.set_blend_constant(*color)
                            }
                            RenderPassCommand::SetStencilReference(reference) => {
                                pass.set_stencil_reference(*reference)
                            }
                            RenderPassCommand::Draw {
                                vertices,
                                instances,
                            } => pass.draw(vertices.clone(), instances.clone()),
                            RenderPassCommand::DrawIndexed {
                                indices,
                                base_vertex,
                                instances,
                            } => {
                                pass.draw_indexed(indices.clone(), *base_vertex, instances.clone())
                            }
                            RenderPassCommand::DrawIndirect { buffer, offset } => {
                                pass.draw_indirect(buffer, *offset)
                            }
                            RenderPassCommand::DrawIndexedIndirect { buffer, offset } => {
                                pass.draw_indexed_indirect(buffer, *offset)
                            }
                        }
                    }
                }
                EncoderCommand::ComputePass { label, commands } => {
                    let mut pass = encoder.begin_compute_pass(&wgpu::ComputePassDescriptor {
                        label: label.as_deref(),
                        timestamp_writes: None,
                    });
                    for command in commands {
                        match command {
                            ComputePassCommand::SetPipeline(pipeline) => {
                                pass.set_pipeline(pipeline)
                            }
                            ComputePassCommand::SetBindGroup {
                                index,
                                bind_group,
                                dynamic_offsets,
                            } => pass.set_bind_group(*index, bind_group.as_ref(), dynamic_offsets),
                            ComputePassCommand::DispatchWorkgroups { x, y, z } => {
                                pass.dispatch_workgroups(*x, *y, *z)
                            }
                            ComputePassCommand::DispatchWorkgroupsIndirect { buffer, offset } => {
                                pass.dispatch_workgroups_indirect(buffer, *offset)
                            }
                        }
                    }
                }
                EncoderCommand::CopyBufferToBuffer {
                    source,
                    source_offset,
                    destination,
                    destination_offset,
                    size,
                } => encoder.copy_buffer_to_buffer(
                    source,
                    *source_offset,
                    destination,
                    *destination_offset,
                    *size,
                ),
                EncoderCommand::CopyBufferToTexture {
                    source,
                    source_layout,
                    destination,
                    destination_mip_level,
                    destination_origin,
                    destination_aspect,
                    size,
                } => encoder.copy_buffer_to_texture(
                    wgpu::TexelCopyBufferInfo {
                        buffer: source,
                        layout: *source_layout,
                    },
                    wgpu::TexelCopyTextureInfo {
                        texture: destination,
                        mip_level: *destination_mip_level,
                        origin: *destination_origin,
                        aspect: *destination_aspect,
                    },
                    *size,
                ),
                EncoderCommand::CopyTextureToBuffer {
                    source,
                    source_mip_level,
                    source_origin,
                    source_aspect,
                    destination,
                    destination_layout,
                    size,
                } => encoder.copy_texture_to_buffer(
                    wgpu::TexelCopyTextureInfo {
                        texture: source,
                        mip_level: *source_mip_level,
                        origin: *source_origin,
                        aspect: *source_aspect,
                    },
                    wgpu::TexelCopyBufferInfo {
                        buffer: destination,
                        layout: *destination_layout,
                    },
                    *size,
                ),
                EncoderCommand::CopyTextureToTexture {
                    source,
                    source_mip_level,
                    source_origin,
                    source_aspect,
                    destination,
                    destination_mip_level,
                    destination_origin,
                    destination_aspect,
                    size,
                } => encoder.copy_texture_to_texture(
                    wgpu::TexelCopyTextureInfo {
                        texture: source,
                        mip_level: *source_mip_level,
                        origin: *source_origin,
                        aspect: *source_aspect,
                    },
                    wgpu::TexelCopyTextureInfo {
                        texture: destination,
                        mip_level: *destination_mip_level,
                        origin: *destination_origin,
                        aspect: *destination_aspect,
                    },
                    *size,
                ),
                EncoderCommand::ClearBuffer {
                    buffer,
                    offset,
                    size,
                } => encoder.clear_buffer(buffer, *offset, *size),
            }
        }

        pub fn queue_submit(&mut self, command_buffer_ids: &[u64]) -> Result<(), String> {
            if command_buffer_ids.is_empty() {
                let _ = self.runtime.device.poll(wgpu::PollType::Poll);
                return Ok(());
            }

            let mut encoder =
                self.runtime
                    .device
                    .create_command_encoder(&wgpu::CommandEncoderDescriptor {
                        label: Some("babylon-native-webgpu.web-command-submit"),
                    });
            for command_buffer_id in command_buffer_ids {
                let command_buffer = self
                    .resources
                    .command_buffers
                    .remove(command_buffer_id)
                    .ok_or_else(|| format!("GPUCommandBuffer {command_buffer_id} was not found"))?;
                if std::env::var_os("BABYLON_NATIVE_WEBGPU_TRACE").is_some() {
                    let labels = command_buffer
                        .commands
                        .iter()
                        .map(|command| match command {
                            EncoderCommand::RenderPass {
                                descriptor,
                                commands,
                            } => {
                                format!(
                                    "render:{}:{}cmd",
                                    descriptor.label.as_deref().unwrap_or("(unlabeled)"),
                                    commands.len()
                                )
                            }
                            EncoderCommand::ComputePass { label, commands } => {
                                format!(
                                    "compute:{}:{}cmd",
                                    label.as_deref().unwrap_or("(unlabeled)"),
                                    commands.len()
                                )
                            }
                            EncoderCommand::CopyBufferToBuffer { .. } => {
                                "copyBufferToBuffer".to_string()
                            }
                            EncoderCommand::CopyBufferToTexture { .. } => {
                                "copyBufferToTexture".to_string()
                            }
                            EncoderCommand::CopyTextureToBuffer { .. } => {
                                "copyTextureToBuffer".to_string()
                            }
                            EncoderCommand::CopyTextureToTexture { .. } => {
                                "copyTextureToTexture".to_string()
                            }
                            EncoderCommand::ClearBuffer { .. } => "clearBuffer".to_string(),
                        })
                        .collect::<Vec<_>>()
                        .join(",");
                    eprintln!(
                        "NativeWebGPU trace executeCommandBuffer: id={} commands={} [{}]",
                        command_buffer_id,
                        command_buffer.commands.len(),
                        labels
                    );
                }
                for command in &command_buffer.commands {
                    self.execute_encoder_command(&mut encoder, command);
                }
            }
            self.runtime.queue.submit(Some(encoder.finish()));
            self.current_surface_frame_submitted = true;
            let _ = self.runtime.device.poll(wgpu::PollType::Poll);
            Ok(())
        }

        pub fn queue_wait_submitted_work(&mut self) -> Result<(), String> {
            self.runtime
                .device
                .poll(wgpu::PollType::wait_indefinitely())
                .map_err(|error| format!("failed waiting for submitted GPU work: {error}"))?;
            Ok(())
        }

        pub fn queue_write_texture(
            &mut self,
            destination_json: &str,
            data: &[u8],
            layout_json: &str,
            size_json: &str,
        ) -> Result<(), String> {
            let destination = parse_json(destination_json)?;
            let layout = parse_buffer_layout(&parse_json(layout_json)?);
            let size = parse_extent3d(&parse_json(size_json)?);
            let texture_id = destination
                .get("texture")
                .and_then(native_id)
                .ok_or_else(|| "GPUImageCopyTexture missing texture handle".to_string())?;
            let texture_format = self
                .resources
                .textures
                .get(&texture_id)
                .ok_or_else(|| format!("GPUTexture {texture_id} was not found"))?
                .format;
            if let (Some(bytes_per_row), Some(bytes_per_pixel)) = (
                layout.bytes_per_row,
                texture_format_bytes_per_pixel(texture_format),
            ) {
                let required = size.width.saturating_mul(bytes_per_pixel);
                if bytes_per_row < required {
                    return Err(format!(
                        "GPUQueue.writeTexture bytesPerRow {bytes_per_row} is less than required {required} for format {texture_format:?} and width {}",
                        size.width
                    ));
                }
            }
            let (texture, mip_level, origin, aspect) =
                self.parse_texture_copy_view(&destination)?;
            self.runtime.queue.write_texture(
                wgpu::TexelCopyTextureInfo {
                    texture: &texture,
                    mip_level,
                    origin,
                    aspect,
                },
                data,
                layout,
                size,
            );
            Ok(())
        }

        pub fn queue_copy_external_image_to_texture(
            &mut self,
            native_texture: *const c_void,
            source_width: u32,
            source_height: u32,
            destination_json: &str,
            size_json: &str,
        ) -> Result<(), String> {
            let size = parse_extent3d(&parse_json(size_json)?);
            let destination = parse_json(destination_json)?;
            let destination_premultiplied_alpha =
                json_bool(&destination, "premultipliedAlpha", false);
            let mut rgba = Vec::new();
            let (width, height) = import_native_texture_rgba_into(
                native_texture,
                source_width.max(size.width),
                source_height.max(size.height),
                &mut rgba,
            )?;
            let texture_id = destination
                .get("texture")
                .and_then(native_id)
                .ok_or_else(|| "GPUImageCopyTexture missing texture handle".to_string())?;
            let texture_format = self
                .resources
                .textures
                .get(&texture_id)
                .ok_or_else(|| format!("GPUTexture {texture_id} was not found"))?
                .format;
            let (texture, mip_level, origin, aspect) =
                self.parse_texture_copy_view(&destination)?;
            let copy_width = size.width.min(width).max(1);
            let copy_height = size.height.min(height).max(1);
            let source_stride = width as usize * 4;
            let expected_len = source_stride * copy_height as usize;
            if rgba.len() < expected_len {
                return Err("external image readback returned too few bytes".to_string());
            }
            let (upload, upload_stride) = encode_external_image_upload(
                &rgba,
                width,
                0,
                0,
                copy_width,
                copy_height,
                true,
                true,
                destination_premultiplied_alpha,
                texture_format,
            )?;
            self.runtime.queue.write_texture(
                wgpu::TexelCopyTextureInfo {
                    texture: &texture,
                    mip_level,
                    origin,
                    aspect,
                },
                upload.as_ref(),
                wgpu::TexelCopyBufferLayout {
                    offset: 0,
                    bytes_per_row: Some(upload_stride),
                    rows_per_image: Some(copy_height),
                },
                wgpu::Extent3d {
                    width: copy_width,
                    height: copy_height,
                    depth_or_array_layers: 1,
                },
            );
            Ok(())
        }

        pub fn queue_copy_external_image_rgba_to_texture(
            &mut self,
            rgba: &[u8],
            source_width: u32,
            source_height: u32,
            source_origin_x: u32,
            source_origin_y: u32,
            flip_y: bool,
            destination_json: &str,
            size_json: &str,
        ) -> Result<(), String> {
            if source_width == 0 || source_height == 0 {
                return Err(
                    "copyExternalImageToTexture source dimensions must be non-zero".to_string(),
                );
            }
            if source_origin_x >= source_width || source_origin_y >= source_height {
                return Err(
                    "copyExternalImageToTexture source origin was outside the image".to_string(),
                );
            }

            let size = parse_extent3d(&parse_json(size_json)?);
            let destination = parse_json(destination_json)?;
            let destination_premultiplied_alpha =
                json_bool(&destination, "premultipliedAlpha", false);
            let texture_id = destination
                .get("texture")
                .and_then(native_id)
                .ok_or_else(|| "GPUImageCopyTexture missing texture handle".to_string())?;
            let texture_format = self
                .resources
                .textures
                .get(&texture_id)
                .ok_or_else(|| format!("GPUTexture {texture_id} was not found"))?
                .format;
            let (texture, mip_level, origin, aspect) =
                self.parse_texture_copy_view(&destination)?;
            let copy_width = size.width.min(source_width - source_origin_x).max(1);
            let copy_height = size.height.min(source_height - source_origin_y).max(1);
            let source_stride = source_width as usize * 4;
            let expected_len =
                source_stride.saturating_mul((source_origin_y + copy_height) as usize);
            if rgba.len() < expected_len {
                return Err("external image RGBA data contained too few bytes".to_string());
            }

            let (upload, upload_stride) = encode_external_image_upload(
                rgba,
                source_width,
                source_origin_x,
                source_origin_y,
                copy_width,
                copy_height,
                flip_y,
                false,
                destination_premultiplied_alpha,
                texture_format,
            )?;
            self.runtime.queue.write_texture(
                wgpu::TexelCopyTextureInfo {
                    texture: &texture,
                    mip_level,
                    origin,
                    aspect,
                },
                upload.as_ref(),
                wgpu::TexelCopyBufferLayout {
                    offset: 0,
                    bytes_per_row: Some(upload_stride),
                    rows_per_image: Some(copy_height),
                },
                wgpu::Extent3d {
                    width: copy_width,
                    height: copy_height,
                    depth_or_array_layers: 1,
                },
            );
            Ok(())
        }

        pub fn canvas_get_current_texture(
            &mut self,
            canvas_id: u64,
            width: u32,
            height: u32,
            format: &str,
            usage: u32,
        ) -> Result<u64, String> {
            if canvas_id == 0 {
                return Err("GPUCanvasContext had an invalid native canvas id".to_string());
            }

            let (width, height) = self.runtime.clamped_extent(width, height);
            self.runtime.width = width;
            self.runtime.height = height;

            let requested_format =
                map_texture_format(format).unwrap_or(self.runtime.render_target_format);
            let format = match (requested_format, self.runtime.render_target_format) {
                (wgpu::TextureFormat::Bgra8Unorm, wgpu::TextureFormat::Bgra8UnormSrgb)
                | (wgpu::TextureFormat::Rgba8Unorm, wgpu::TextureFormat::Rgba8UnormSrgb) => {
                    self.runtime.render_target_format
                }
                _ => requested_format,
            };

            if let Some(target) = self.canvas_targets.get(&canvas_id) {
                if target.width == width
                    && target.height == height
                    && target.format == format
                    && target.usage == usage
                {
                    self.current_canvas_id = Some(canvas_id);
                    self.current_canvas_texture_id = Some(target.texture_id);
                    self.current_canvas_texture = Some(target.texture.clone());
                    self.offscreen_texture = Some(target.texture.clone());
                    self.offscreen_view = Some(target.view.clone());
                    self.offscreen_width = width;
                    self.offscreen_height = height;
                    self.offscreen_format = format;
                    return Ok(target.texture_id);
                }

                let old_id = target.texture_id;
                self.resources.textures.remove(&old_id);
                self.canvas_targets.remove(&canvas_id);
            }

            let view_formats = compatible_view_formats(format);
            let texture = self
                .runtime
                .device
                .create_texture(&wgpu::TextureDescriptor {
                    label: Some("babylon-native-webgpu.canvas-offscreen"),
                    size: wgpu::Extent3d {
                        width,
                        height,
                        depth_or_array_layers: 1,
                    },
                    mip_level_count: 1,
                    sample_count: 1,
                    dimension: wgpu::TextureDimension::D2,
                    format,
                    usage: map_texture_usage(usage)
                        | wgpu::TextureUsages::RENDER_ATTACHMENT
                        | wgpu::TextureUsages::COPY_SRC
                        | wgpu::TextureUsages::COPY_DST,
                    view_formats: &view_formats,
                });
            let view = texture.create_view(&wgpu::TextureViewDescriptor::default());

            self.current_canvas_texture = Some(texture.clone());
            let id = self.resources.next();
            self.resources.textures.insert(
                id,
                TextureResource {
                    texture: texture.clone(),
                    width,
                    height,
                    depth_or_array_layers: 1,
                    format,
                },
            );
            self.offscreen_texture = Some(texture.clone());
            self.offscreen_view = Some(view.clone());
            self.offscreen_width = width;
            self.offscreen_height = height;
            self.offscreen_format = format;
            self.current_canvas_texture_id = Some(id);
            self.current_canvas_id = Some(canvas_id);
            self.canvas_targets.insert(
                canvas_id,
                CanvasTarget {
                    texture,
                    view,
                    texture_id: id,
                    width,
                    height,
                    format,
                    usage,
                },
            );
            Ok(id)
        }

        pub fn canvas_destroy(&mut self, canvas_id: u64) {
            let Some(target) = self.canvas_targets.remove(&canvas_id) else {
                return;
            };

            self.resources.textures.remove(&target.texture_id);
            if self.current_canvas_id == Some(canvas_id) {
                self.current_canvas_id = None;
                self.current_canvas_texture_id = None;
                self.current_canvas_texture = None;
                self.offscreen_texture = None;
                self.offscreen_view = None;
            }
        }

        pub fn read_texture_pixel_rgba(
            &mut self,
            texture_id: u64,
            x: u32,
            y: u32,
        ) -> Result<[u8; 4], String> {
            let texture_resource = self
                .resources
                .textures
                .get(&texture_id)
                .ok_or_else(|| format!("GPUTexture {texture_id} was not found"))?;
            if x >= texture_resource.width || y >= texture_resource.height {
                return Err(format!(
                    "texture pixel coordinate {x},{y} outside {}x{} texture",
                    texture_resource.width, texture_resource.height
                ));
            }

            let mut encoder =
                self.runtime
                    .device
                    .create_command_encoder(&wgpu::CommandEncoderDescriptor {
                        label: Some("babylon-native-webgpu.test-read-texture-pixel"),
                    });
            let (staging_buffer, padded_bytes_per_row, unpadded_bytes_per_row) =
                copy_texture_to_readback_buffer(
                    &self.runtime.device,
                    &mut encoder,
                    &texture_resource.texture,
                    texture_resource.width,
                    texture_resource.height,
                )?;
            self.runtime.queue.submit(Some(encoder.finish()));
            let rgba = map_readback_buffer_to_rgba(
                &self.runtime.device,
                &staging_buffer,
                padded_bytes_per_row,
                unpadded_bytes_per_row,
                texture_resource.width,
                texture_resource.height,
                texture_resource.format,
            )?;
            let offset = ((y as usize * texture_resource.width as usize) + x as usize) * 4;
            Ok([
                rgba.rgba[offset],
                rgba.rgba[offset + 1],
                rgba.rgba[offset + 2],
                rgba.rgba[offset + 3],
            ])
        }

        pub fn destroy_resource(&mut self, kind: u32, resource_id: u64) -> bool {
            match kind {
                1 => self.resources.buffers.remove(&resource_id).is_some(),
                2 => {
                    if self.current_canvas_texture_id == Some(resource_id) {
                        self.current_canvas_texture_id = None;
                        self.current_canvas_texture = None;
                        self.current_canvas_id = None;
                        self.offscreen_texture = None;
                        self.offscreen_view = None;
                    }
                    self.canvas_targets
                        .retain(|_, target| target.texture_id != resource_id);
                    self.resources.textures.remove(&resource_id).is_some()
                }
                3 => self.resources.texture_views.remove(&resource_id).is_some(),
                4 => self.resources.samplers.remove(&resource_id).is_some(),
                5 => self.resources.shader_modules.remove(&resource_id).is_some(),
                6 => self
                    .resources
                    .bind_group_layouts
                    .remove(&resource_id)
                    .is_some(),
                7 => self
                    .resources
                    .pipeline_layouts
                    .remove(&resource_id)
                    .is_some(),
                8 => self.resources.bind_groups.remove(&resource_id).is_some(),
                9 => self
                    .resources
                    .render_pipelines
                    .remove(&resource_id)
                    .is_some(),
                10 => self
                    .resources
                    .command_encoders
                    .remove(&resource_id)
                    .is_some(),
                11 => self.resources.render_passes.remove(&resource_id).is_some(),
                12 => self
                    .resources
                    .command_buffers
                    .remove(&resource_id)
                    .is_some(),
                13 => self
                    .resources
                    .compute_pipelines
                    .remove(&resource_id)
                    .is_some(),
                14 => self.resources.compute_passes.remove(&resource_id).is_some(),
                _ => false,
            }
        }
    }

    #[derive(Default)]
    struct NativeReadbackCache {
        device_key: usize,
        size: u64,
        buffer: Option<wgpu::Buffer>,
    }

    static NATIVE_READBACK_CACHE: OnceLock<Mutex<NativeReadbackCache>> = OnceLock::new();
    static NATIVE_READBACK_IMPORT_GUARD: OnceLock<Mutex<()>> = OnceLock::new();
    static DEBUG_TEXTURE_IMPORT_STATE: OnceLock<Mutex<DebugTextureImportState>> = OnceLock::new();
    static DEBUG_TEXTURE_UPLOAD_PENDING: AtomicBool = AtomicBool::new(false);

    fn native_readback_cache() -> &'static Mutex<NativeReadbackCache> {
        NATIVE_READBACK_CACHE.get_or_init(|| Mutex::new(NativeReadbackCache::default()))
    }

    fn native_readback_import_guard() -> &'static Mutex<()> {
        NATIVE_READBACK_IMPORT_GUARD.get_or_init(|| Mutex::new(()))
    }

    #[derive(Clone, Copy, PartialEq, Eq)]
    struct DebugTextureSourceSignature {
        texture_ptr: usize,
        width: u32,
        height: u32,
        generation: u64,
    }

    #[derive(Default)]
    pub struct DebugTextureUploadData {
        pub width: u32,
        pub height: u32,
        pub rgba: Vec<u8>,
    }

    #[derive(Clone, Copy, Default)]
    pub struct DebugTextureImportStats {
        pub hash: u64,
        pub width: u32,
        pub height: u32,
        pub import_skip_count: u64,
    }

    #[derive(Default)]
    struct DebugTextureImportState {
        source_signature: Option<DebugTextureSourceSignature>,
        pending: Option<DebugTextureUploadData>,
        reusable: Option<DebugTextureUploadData>,
        stats: DebugTextureImportStats,
    }

    fn debug_texture_import_state() -> &'static Mutex<DebugTextureImportState> {
        DEBUG_TEXTURE_IMPORT_STATE.get_or_init(|| Mutex::new(DebugTextureImportState::default()))
    }

    fn hash_bytes(bytes: &[u8]) -> u64 {
        // 64-bit FNV-1a keeps this lightweight and deterministic across platforms.
        let mut hash = 0xcbf29ce484222325u64;
        for byte in bytes {
            hash ^= u64::from(*byte);
            hash = hash.wrapping_mul(0x100000001b3);
        }
        hash
    }

    fn make_debug_texture_source_signature(
        native_texture: *const c_void,
        width: u32,
        height: u32,
    ) -> DebugTextureSourceSignature {
        let mut texture_ptr = native_texture as usize;
        let mut generation = 0u64;

        if let Some(handle) = opaque_ptr_as_ref::<CanvasNativeTextureHandle>(native_texture) {
            if !handle.texture.is_null() {
                texture_ptr = handle.texture as usize;
            }

            generation = handle.generation;
        }

        DebugTextureSourceSignature {
            texture_ptr,
            width: width.max(1),
            height: height.max(1),
            generation,
        }
    }

    pub fn clear_debug_texture_import_state() {
        let mut state = match debug_texture_import_state().lock() {
            Ok(lock) => lock,
            Err(poisoned) => poisoned.into_inner(),
        };
        state.source_signature = None;
        state.pending = None;
        state.reusable = None;
        state.stats = DebugTextureImportStats::default();
        DEBUG_TEXTURE_UPLOAD_PENDING.store(false, Ordering::Release);
    }

    pub fn debug_texture_import_stats() -> DebugTextureImportStats {
        let state = match debug_texture_import_state().lock() {
            Ok(lock) => lock,
            Err(poisoned) => poisoned.into_inner(),
        };
        state.stats
    }

    pub fn take_pending_debug_texture_upload() -> Option<DebugTextureUploadData> {
        if !DEBUG_TEXTURE_UPLOAD_PENDING.load(Ordering::Acquire) {
            return None;
        }

        let mut state = match debug_texture_import_state().lock() {
            Ok(lock) => lock,
            Err(poisoned) => poisoned.into_inner(),
        };
        let pending = state.pending.take();
        if pending.is_none() {
            DEBUG_TEXTURE_UPLOAD_PENDING.store(false, Ordering::Release);
        }
        pending
    }

    pub fn recycle_debug_texture_upload(upload: DebugTextureUploadData) {
        let mut state = match debug_texture_import_state().lock() {
            Ok(lock) => lock,
            Err(poisoned) => poisoned.into_inner(),
        };
        state.reusable = Some(upload);
    }

    pub fn set_debug_texture_from_native(
        native_texture: *const c_void,
        width: u32,
        height: u32,
    ) -> Result<DebugTextureImportStats, String> {
        if native_texture.is_null() {
            return Err("native texture handle pointer was null".to_string());
        }

        let signature = make_debug_texture_source_signature(native_texture, width, height);
        let mut upload = {
            let mut state = debug_texture_import_state()
                .lock()
                .map_err(|_| "debug texture import state lock poisoned".to_string())?;

            if state
                .source_signature
                .as_ref()
                .is_some_and(|last| *last == signature)
            {
                state.stats.import_skip_count = state.stats.import_skip_count.saturating_add(1);
                return Ok(state.stats);
            }

            state
                .pending
                .take()
                .or_else(|| state.reusable.take())
                .unwrap_or_default()
        };

        let (imported_width, imported_height) = import_native_texture_rgba_into(
            native_texture,
            signature.width,
            signature.height,
            &mut upload.rgba,
        )?;

        upload.width = imported_width;
        upload.height = imported_height;

        let mut state = debug_texture_import_state()
            .lock()
            .map_err(|_| "debug texture import state lock poisoned".to_string())?;
        let hash = hash_bytes(&upload.rgba);
        state.stats.hash = hash;
        state.stats.width = upload.width;
        state.stats.height = upload.height;
        state.source_signature = Some(signature);
        if let Some(previous) = state.pending.replace(upload) {
            state.reusable = Some(previous);
        }
        DEBUG_TEXTURE_UPLOAD_PENDING.store(true, Ordering::Release);

        Ok(state.stats)
    }

    fn acquire_native_readback_buffer(
        source_device: &wgpu::Device,
        required_size: u64,
    ) -> Result<wgpu::Buffer, String> {
        let mut cache = native_readback_cache()
            .lock()
            .map_err(|_| "native readback cache lock poisoned".to_string())?;

        let device_key = (source_device as *const wgpu::Device) as usize;
        let needs_rebuild =
            cache.device_key != device_key || cache.size < required_size || cache.buffer.is_none();

        if needs_rebuild {
            cache.buffer = Some(source_device.create_buffer(&wgpu::BufferDescriptor {
                label: Some("babylon-native-webgpu.native-debug-readback"),
                size: required_size,
                usage: wgpu::BufferUsages::COPY_DST | wgpu::BufferUsages::MAP_READ,
                mapped_at_creation: false,
            }));
            cache.device_key = device_key;
            cache.size = required_size;
        }

        cache
            .buffer
            .as_ref()
            .cloned()
            .ok_or_else(|| "native readback buffer allocation failed".to_string())
    }

    fn align_to(value: u32, alignment: u32) -> u32 {
        if alignment <= 1 {
            return value;
        }

        let remainder = value % alignment;
        if remainder == 0 {
            value
        } else {
            value.saturating_add(alignment - remainder)
        }
    }

    fn copy_texture_to_readback_buffer(
        device: &wgpu::Device,
        encoder: &mut wgpu::CommandEncoder,
        source_texture: &wgpu::Texture,
        width: u32,
        height: u32,
    ) -> Result<(wgpu::Buffer, u32, u32), String> {
        let unpadded_bytes_per_row = width.saturating_mul(4);
        if unpadded_bytes_per_row == 0 || height == 0 {
            return Err("invalid screenshot extent".to_string());
        }

        let padded_bytes_per_row =
            align_to(unpadded_bytes_per_row, wgpu::COPY_BYTES_PER_ROW_ALIGNMENT);
        let buffer_size = (padded_bytes_per_row as u64).saturating_mul(height as u64);
        if buffer_size == 0 {
            return Err("invalid screenshot buffer size".to_string());
        }

        let staging_buffer = acquire_native_readback_buffer(device, buffer_size)?;
        encoder.copy_texture_to_buffer(
            wgpu::TexelCopyTextureInfo {
                texture: source_texture,
                mip_level: 0,
                origin: wgpu::Origin3d::ZERO,
                aspect: wgpu::TextureAspect::All,
            },
            wgpu::TexelCopyBufferInfo {
                buffer: &staging_buffer,
                layout: wgpu::TexelCopyBufferLayout {
                    offset: 0,
                    bytes_per_row: Some(padded_bytes_per_row),
                    rows_per_image: Some(height),
                },
            },
            wgpu::Extent3d {
                width,
                height,
                depth_or_array_layers: 1,
            },
        );

        Ok((staging_buffer, padded_bytes_per_row, unpadded_bytes_per_row))
    }

    fn map_readback_buffer_to_rgba(
        device: &wgpu::Device,
        staging_buffer: &wgpu::Buffer,
        padded_bytes_per_row: u32,
        unpadded_bytes_per_row: u32,
        width: u32,
        height: u32,
        format: wgpu::TextureFormat,
    ) -> Result<ScreenshotData, String> {
        let slice = staging_buffer.slice(..);
        let (tx, rx) = std::sync::mpsc::channel::<Result<(), String>>();
        slice.map_async(wgpu::MapMode::Read, move |result| {
            let _ = tx.send(result.map_err(|error| error.to_string()));
        });

        device
            .poll(wgpu::PollType::wait_indefinitely())
            .map_err(|error| format!("screenshot poll failed: {error}"))?;
        match rx.recv() {
            Ok(Ok(())) => {}
            Ok(Err(error)) => return Err(format!("screenshot map_async failed: {error}")),
            Err(error) => return Err(format!("screenshot map_async channel failed: {error}")),
        }

        let mapped = slice.get_mapped_range();
        let expected_len = (width as usize)
            .saturating_mul(height as usize)
            .saturating_mul(4);
        let mut rgba = vec![0; expected_len];
        for row in 0..(height as usize) {
            let src_start = row.saturating_mul(padded_bytes_per_row as usize);
            let src_end = src_start.saturating_add(unpadded_bytes_per_row as usize);
            let dst_start = row.saturating_mul(unpadded_bytes_per_row as usize);
            let dst_end = dst_start.saturating_add(unpadded_bytes_per_row as usize);
            rgba[dst_start..dst_end].copy_from_slice(&mapped[src_start..src_end]);
        }
        drop(mapped);
        staging_buffer.unmap();

        if matches!(
            format,
            wgpu::TextureFormat::Bgra8Unorm | wgpu::TextureFormat::Bgra8UnormSrgb
        ) {
            for pixel in rgba.chunks_exact_mut(4) {
                pixel.swap(0, 2);
            }
        }

        Ok(ScreenshotData {
            width,
            height,
            rgba,
        })
    }

    fn import_native_texture_rgba_inner(
        native_texture: *const c_void,
        requested_width: u32,
        requested_height: u32,
        rgba: &mut Vec<u8>,
    ) -> Result<(u32, u32), String> {
        let _import_guard = native_readback_import_guard()
            .lock()
            .map_err(|_| "native texture import lock poisoned".to_string())?;

        if native_texture.is_null() {
            return Err("native texture handle pointer was null".to_string());
        }

        let native_handle = opaque_ptr_as_ref::<CanvasNativeTextureHandle>(native_texture)
            .ok_or_else(|| "native texture handle pointer was invalid".to_string())?;
        let source_texture = opaque_ptr_as_ref::<wgpu::Texture>(native_handle.texture)
            .ok_or_else(|| "native texture pointer was invalid".to_string())?;
        let source_device = opaque_ptr_as_ref::<wgpu::Device>(native_handle.device)
            .ok_or_else(|| "native device pointer was invalid".to_string())?;
        let source_queue = opaque_ptr_as_ref::<wgpu::Queue>(native_handle.queue)
            .ok_or_else(|| "native queue pointer was invalid".to_string())?;

        let width = if requested_width == 0 {
            native_handle.width
        } else {
            requested_width
        }
        .max(1);
        let height = if requested_height == 0 {
            native_handle.height
        } else {
            requested_height
        }
        .max(1);

        let unpadded_bytes_per_row = width.saturating_mul(4);
        if unpadded_bytes_per_row == 0 {
            return Err("invalid native texture width".to_string());
        }
        let padded_bytes_per_row =
            align_to(unpadded_bytes_per_row, wgpu::COPY_BYTES_PER_ROW_ALIGNMENT);
        let buffer_size = (padded_bytes_per_row as u64).saturating_mul(height as u64);
        if buffer_size == 0 {
            return Err("invalid native texture size".to_string());
        }

        let staging_buffer = acquire_native_readback_buffer(source_device, buffer_size)?;

        let mut encoder = source_device.create_command_encoder(&wgpu::CommandEncoderDescriptor {
            label: Some("babylon-native-webgpu.native-debug-readback-encoder"),
        });
        encoder.copy_texture_to_buffer(
            wgpu::TexelCopyTextureInfo {
                texture: source_texture,
                mip_level: 0,
                origin: wgpu::Origin3d::ZERO,
                aspect: wgpu::TextureAspect::All,
            },
            wgpu::TexelCopyBufferInfo {
                buffer: &staging_buffer,
                layout: wgpu::TexelCopyBufferLayout {
                    offset: 0,
                    bytes_per_row: Some(padded_bytes_per_row),
                    rows_per_image: Some(height),
                },
            },
            wgpu::Extent3d {
                width,
                height,
                depth_or_array_layers: 1,
            },
        );
        source_queue.submit(Some(encoder.finish()));

        let slice = staging_buffer.slice(..);
        let (tx, rx) = std::sync::mpsc::channel::<Result<(), String>>();
        slice.map_async(wgpu::MapMode::Read, move |result| {
            let _ = tx.send(result.map_err(|error| error.to_string()));
        });

        source_device
            .poll(wgpu::PollType::wait_indefinitely())
            .map_err(|error| format!("native texture poll failed: {error}"))?;
        match rx.recv() {
            Ok(Ok(())) => {}
            Ok(Err(error)) => return Err(format!("native texture map_async failed: {error}")),
            Err(error) => return Err(format!("native texture map_async channel failed: {error}")),
        }

        let mapped = slice.get_mapped_range();
        let expected_len = (width as usize)
            .saturating_mul(height as usize)
            .saturating_mul(4);
        rgba.resize(expected_len, 0);
        for row in 0..(height as usize) {
            let src_start = row.saturating_mul(padded_bytes_per_row as usize);
            let src_end = src_start.saturating_add(unpadded_bytes_per_row as usize);
            let dst_start = row.saturating_mul(unpadded_bytes_per_row as usize);
            let dst_end = dst_start.saturating_add(unpadded_bytes_per_row as usize);
            rgba[dst_start..dst_end].copy_from_slice(&mapped[src_start..src_end]);
        }
        drop(mapped);
        staging_buffer.unmap();

        Ok((width, height))
    }

    pub fn import_native_texture_rgba_into(
        native_texture: *const c_void,
        requested_width: u32,
        requested_height: u32,
        rgba: &mut Vec<u8>,
    ) -> Result<(u32, u32), String> {
        import_native_texture_rgba_inner(native_texture, requested_width, requested_height, rgba)
    }

    #[repr(C)]
    #[derive(Clone, Copy, Pod, Zeroable)]
    struct DebugCubeVertex {
        position: [f32; 3],
        color: [f32; 3],
        uv: [f32; 2],
        face_id: u32,
    }

    const DEBUG_CUBE_SHADER_WGSL: &str = r#"
    struct Uniforms {
        mvp: mat4x4<f32>,
    };
    
    @group(0) @binding(0)
    var<uniform> uniforms: Uniforms;
    
    struct VertexIn {
        @location(0) position: vec3<f32>,
        @location(1) color: vec3<f32>,
        @location(2) uv: vec2<f32>,
        @location(3) face_id: u32,
    };
    
    struct VertexOut {
        @builtin(position) position: vec4<f32>,
        @location(0) color: vec3<f32>,
        @location(1) uv: vec2<f32>,
        @location(2) @interpolate(flat) face_id: u32,
    };
    
    @group(0) @binding(1)
    var canvas_sampler: sampler;
    
    @group(0) @binding(2)
    var canvas_texture: texture_2d<f32>;
    
    @vertex
    fn vs_main(input: VertexIn) -> VertexOut {
        var output: VertexOut;
        output.position = uniforms.mvp * vec4<f32>(input.position, 1.0);
        output.color = input.color;
        output.uv = input.uv;
        output.face_id = input.face_id;
        return output;
    }
    
    @fragment
    fn fs_main(input: VertexOut) -> @location(0) vec4<f32> {
        let sampled_uv = vec2<f32>(1.0 - input.uv.x, input.uv.y);
        let sampled = textureSample(canvas_texture, canvas_sampler, sampled_uv);
        if (input.face_id == 1u) {
            return sampled;
        }
        return mix(vec4<f32>(input.color, 1.0), sampled, 0.35);
    }
    "#;

    const DEBUG_CUBE_INDICES: [u16; 36] = [
        0, 1, 2, 2, 3, 0, 4, 5, 6, 6, 7, 4, 8, 9, 10, 10, 11, 8, 12, 13, 14, 14, 15, 12, 16, 17,
        18, 18, 19, 16, 20, 21, 22, 22, 23, 20,
    ];

    fn build_debug_cube_vertices() -> [DebugCubeVertex; 24] {
        const POSITIONS: [[f32; 3]; 8] = [
            [-1.0, -1.0, -1.0],
            [1.0, -1.0, -1.0],
            [1.0, 1.0, -1.0],
            [-1.0, 1.0, -1.0],
            [-1.0, -1.0, 1.0],
            [1.0, -1.0, 1.0],
            [1.0, 1.0, 1.0],
            [-1.0, 1.0, 1.0],
        ];
        const UVS: [[f32; 2]; 4] = [[0.0, 1.0], [1.0, 1.0], [1.0, 0.0], [0.0, 0.0]];
        const FACE_LAYOUT: [([usize; 4], [f32; 3], u32); 6] = [
            ([0, 1, 2, 3], [1.0, 0.2, 0.2], 0),    // back
            ([4, 5, 6, 7], [1.0, 0.2, 1.0], 1),    // front textured
            ([0, 3, 7, 4], [0.95, 0.25, 0.25], 2), // left
            ([1, 5, 6, 2], [0.25, 0.95, 0.25], 3), // right
            ([3, 2, 6, 7], [0.25, 0.25, 0.95], 4), // top
            ([0, 4, 5, 1], [0.95, 0.95, 0.25], 5), // bottom
        ];

        let mut vertices = [DebugCubeVertex::zeroed(); 24];
        for (face_index, (corners, color, face_id)) in FACE_LAYOUT.iter().enumerate() {
            let base = face_index * 4;
            for corner_index in 0..4 {
                vertices[base + corner_index] = DebugCubeVertex {
                    position: POSITIONS[corners[corner_index]],
                    color: *color,
                    uv: UVS[corner_index],
                    face_id: *face_id,
                };
            }
        }
        vertices
    }

    pub fn create_default_debug_cube_renderer(runtime: &LocalRuntimeState) -> DebugCubeRenderer {
        let vertices = build_debug_cube_vertices();
        DebugCubeRenderer::new(
            runtime,
            DEBUG_CUBE_SHADER_WGSL,
            std::mem::size_of::<[f32; 16]>() as u64,
            bytemuck::cast_slice(&vertices),
            std::mem::size_of::<DebugCubeVertex>() as u64,
            bytemuck::cast_slice(&DEBUG_CUBE_INDICES),
            DEBUG_CUBE_INDICES.len() as u32,
        )
    }

    fn create_canvas_texture_with_view(
        device: &wgpu::Device,
        width: u32,
        height: u32,
    ) -> (wgpu::Texture, wgpu::TextureView) {
        let texture = device.create_texture(&wgpu::TextureDescriptor {
            label: Some("babylon-native-webgpu.canvas-texture"),
            size: wgpu::Extent3d {
                width,
                height,
                depth_or_array_layers: 1,
            },
            mip_level_count: 1,
            sample_count: 1,
            dimension: wgpu::TextureDimension::D2,
            format: wgpu::TextureFormat::Rgba8Unorm,
            usage: wgpu::TextureUsages::TEXTURE_BINDING | wgpu::TextureUsages::COPY_DST,
            view_formats: &[],
        });
        let view = texture.create_view(&wgpu::TextureViewDescriptor {
            label: Some("babylon-native-webgpu.canvas-texture-view"),
            ..Default::default()
        });
        (texture, view)
    }

    pub fn create_debug_cube_bind_group(
        device: &wgpu::Device,
        layout: &wgpu::BindGroupLayout,
        uniform_buffer: &wgpu::Buffer,
        canvas_sampler: &wgpu::Sampler,
        canvas_texture_view: &wgpu::TextureView,
    ) -> wgpu::BindGroup {
        device.create_bind_group(&wgpu::BindGroupDescriptor {
            label: Some("babylon-native-webgpu.uniform-bind-group"),
            layout,
            entries: &[
                wgpu::BindGroupEntry {
                    binding: 0,
                    resource: uniform_buffer.as_entire_binding(),
                },
                wgpu::BindGroupEntry {
                    binding: 1,
                    resource: wgpu::BindingResource::Sampler(canvas_sampler),
                },
                wgpu::BindGroupEntry {
                    binding: 2,
                    resource: wgpu::BindingResource::TextureView(canvas_texture_view),
                },
            ],
        })
    }

    fn create_debug_cube_bind_group_layout(device: &wgpu::Device) -> wgpu::BindGroupLayout {
        device.create_bind_group_layout(&wgpu::BindGroupLayoutDescriptor {
            label: Some("babylon-native-webgpu.uniform-bind-group-layout"),
            entries: &[
                wgpu::BindGroupLayoutEntry {
                    binding: 0,
                    visibility: wgpu::ShaderStages::VERTEX,
                    ty: wgpu::BindingType::Buffer {
                        ty: wgpu::BufferBindingType::Uniform,
                        has_dynamic_offset: false,
                        min_binding_size: None,
                    },
                    count: None,
                },
                wgpu::BindGroupLayoutEntry {
                    binding: 1,
                    visibility: wgpu::ShaderStages::FRAGMENT,
                    ty: wgpu::BindingType::Sampler(wgpu::SamplerBindingType::Filtering),
                    count: None,
                },
                wgpu::BindGroupLayoutEntry {
                    binding: 2,
                    visibility: wgpu::ShaderStages::FRAGMENT,
                    ty: wgpu::BindingType::Texture {
                        sample_type: wgpu::TextureSampleType::Float { filterable: true },
                        view_dimension: wgpu::TextureViewDimension::D2,
                        multisampled: false,
                    },
                    count: None,
                },
            ],
        })
    }

    fn bytes_per_pixel(format: wgpu::TextureFormat) -> u64 {
        match format {
            wgpu::TextureFormat::Rgba8Unorm
            | wgpu::TextureFormat::Rgba8UnormSrgb
            | wgpu::TextureFormat::Bgra8Unorm
            | wgpu::TextureFormat::Bgra8UnormSrgb
            | wgpu::TextureFormat::Depth24Plus
            | wgpu::TextureFormat::Depth24PlusStencil8
            | wgpu::TextureFormat::Depth32Float => 4,
            wgpu::TextureFormat::Rgba16Float => 8,
            _ => 4,
        }
    }

    fn estimated_texture_bytes(width: u32, height: u32, format: wgpu::TextureFormat) -> u64 {
        u64::from(width.max(1))
            .saturating_mul(u64::from(height.max(1)))
            .saturating_mul(bytes_per_pixel(format))
    }

    fn mul_matrix(a: [f32; 16], b: [f32; 16]) -> [f32; 16] {
        let mut out = [0.0f32; 16];
        for col in 0..4 {
            for row in 0..4 {
                out[col * 4 + row] = a[row] * b[col * 4]
                    + a[4 + row] * b[col * 4 + 1]
                    + a[8 + row] * b[col * 4 + 2]
                    + a[12 + row] * b[col * 4 + 3];
            }
        }

        out
    }

    fn translation_matrix(x: f32, y: f32, z: f32) -> [f32; 16] {
        [
            1.0, 0.0, 0.0, 0.0, // col 0
            0.0, 1.0, 0.0, 0.0, // col 1
            0.0, 0.0, 1.0, 0.0, // col 2
            x, y, z, 1.0, // col 3
        ]
    }

    fn rotation_x_matrix(angle: f32) -> [f32; 16] {
        let c = angle.cos();
        let s = angle.sin();

        [
            1.0, 0.0, 0.0, 0.0, // col 0
            0.0, c, s, 0.0, // col 1
            0.0, -s, c, 0.0, // col 2
            0.0, 0.0, 0.0, 1.0, // col 3
        ]
    }

    fn rotation_y_matrix(angle: f32) -> [f32; 16] {
        let c = angle.cos();
        let s = angle.sin();

        [
            c, 0.0, -s, 0.0, // col 0
            0.0, 1.0, 0.0, 0.0, // col 1
            s, 0.0, c, 0.0, // col 2
            0.0, 0.0, 0.0, 1.0, // col 3
        ]
    }

    fn perspective_rh_zo(fovy: f32, aspect: f32, near: f32, far: f32) -> [f32; 16] {
        let f = 1.0 / (fovy * 0.5).tan();

        [
            f / aspect,
            0.0,
            0.0,
            0.0, // col 0
            0.0,
            f,
            0.0,
            0.0, // col 1
            0.0,
            0.0,
            far / (near - far),
            -1.0, // col 2
            0.0,
            0.0,
            (near * far) / (near - far),
            0.0, // col 3
        ]
    }

    impl DebugCubeRenderer {
        pub fn new(
            runtime: &LocalRuntimeState,
            shader_source: &str,
            uniform_buffer_size: u64,
            vertex_data: &[u8],
            vertex_stride: u64,
            index_data: &[u8],
            index_count: u32,
        ) -> Self {
            let (offscreen_texture, offscreen_view) = if runtime.surface.is_none() {
                let (texture, view) =
                    runtime.create_offscreen_target(runtime.width, runtime.height);
                (Some(texture), Some(view))
            } else {
                (None, None)
            };

            let (depth_texture, depth_view) =
                runtime.create_depth_target(runtime.width, runtime.height);

            let shader = runtime
                .device
                .create_shader_module(wgpu::ShaderModuleDescriptor {
                    label: Some("babylon-native-webgpu.cube-shader"),
                    source: wgpu::ShaderSource::Wgsl(shader_source.into()),
                });
            let uniform_buffer = runtime.device.create_buffer(&wgpu::BufferDescriptor {
                label: Some("babylon-native-webgpu.uniform-buffer"),
                size: uniform_buffer_size,
                usage: wgpu::BufferUsages::UNIFORM | wgpu::BufferUsages::COPY_DST,
                mapped_at_creation: false,
            });
            let canvas_sampler = runtime.device.create_sampler(&wgpu::SamplerDescriptor {
                label: Some("babylon-native-webgpu.canvas-sampler"),
                address_mode_u: wgpu::AddressMode::ClampToEdge,
                address_mode_v: wgpu::AddressMode::ClampToEdge,
                address_mode_w: wgpu::AddressMode::ClampToEdge,
                mag_filter: wgpu::FilterMode::Linear,
                min_filter: wgpu::FilterMode::Linear,
                mipmap_filter: wgpu::MipmapFilterMode::Linear,
                ..Default::default()
            });
            let (canvas_texture, canvas_texture_view) =
                create_canvas_texture_with_view(&runtime.device, 1, 1);
            runtime.queue.write_texture(
                wgpu::TexelCopyTextureInfo {
                    texture: &canvas_texture,
                    mip_level: 0,
                    origin: wgpu::Origin3d::ZERO,
                    aspect: wgpu::TextureAspect::All,
                },
                &[255u8, 255u8, 255u8, 255u8],
                wgpu::TexelCopyBufferLayout {
                    offset: 0,
                    bytes_per_row: Some(4),
                    rows_per_image: Some(1),
                },
                wgpu::Extent3d {
                    width: 1,
                    height: 1,
                    depth_or_array_layers: 1,
                },
            );
            let uniform_bind_group_layout = create_debug_cube_bind_group_layout(&runtime.device);
            let uniform_bind_group = create_debug_cube_bind_group(
                &runtime.device,
                &uniform_bind_group_layout,
                &uniform_buffer,
                &canvas_sampler,
                &canvas_texture_view,
            );
            let pipeline_layout =
                runtime
                    .device
                    .create_pipeline_layout(&wgpu::PipelineLayoutDescriptor {
                        label: Some("babylon-native-webgpu.pipeline-layout"),
                        bind_group_layouts: &[Some(&uniform_bind_group_layout)],
                        immediate_size: 0,
                    });
            let vertex_buffer =
                runtime
                    .device
                    .create_buffer_init(&wgpu::util::BufferInitDescriptor {
                        label: Some("babylon-native-webgpu.vertex-buffer"),
                        contents: vertex_data,
                        usage: wgpu::BufferUsages::VERTEX,
                    });
            let index_buffer =
                runtime
                    .device
                    .create_buffer_init(&wgpu::util::BufferInitDescriptor {
                        label: Some("babylon-native-webgpu.index-buffer"),
                        contents: index_data,
                        usage: wgpu::BufferUsages::INDEX,
                    });
            let vertex_attributes = [
                wgpu::VertexAttribute {
                    offset: 0,
                    shader_location: 0,
                    format: wgpu::VertexFormat::Float32x3,
                },
                wgpu::VertexAttribute {
                    offset: 12,
                    shader_location: 1,
                    format: wgpu::VertexFormat::Float32x3,
                },
                wgpu::VertexAttribute {
                    offset: 24,
                    shader_location: 2,
                    format: wgpu::VertexFormat::Float32x2,
                },
                wgpu::VertexAttribute {
                    offset: 32,
                    shader_location: 3,
                    format: wgpu::VertexFormat::Uint32,
                },
            ];
            let render_pipeline =
                runtime
                    .device
                    .create_render_pipeline(&wgpu::RenderPipelineDescriptor {
                        label: Some("babylon-native-webgpu.cube-pipeline"),
                        layout: Some(&pipeline_layout),
                        vertex: wgpu::VertexState {
                            module: &shader,
                            entry_point: Some("vs_main"),
                            compilation_options: wgpu::PipelineCompilationOptions::default(),
                            buffers: &[wgpu::VertexBufferLayout {
                                array_stride: vertex_stride,
                                step_mode: wgpu::VertexStepMode::Vertex,
                                attributes: &vertex_attributes,
                            }],
                        },
                        primitive: wgpu::PrimitiveState {
                            topology: wgpu::PrimitiveTopology::TriangleList,
                            strip_index_format: None,
                            front_face: wgpu::FrontFace::Ccw,
                            cull_mode: None,
                            unclipped_depth: false,
                            polygon_mode: wgpu::PolygonMode::Fill,
                            conservative: false,
                        },
                        depth_stencil: Some(wgpu::DepthStencilState {
                            format: wgpu::TextureFormat::Depth32Float,
                            depth_write_enabled: Some(true),
                            depth_compare: Some(wgpu::CompareFunction::Less),
                            stencil: wgpu::StencilState::default(),
                            bias: wgpu::DepthBiasState::default(),
                        }),
                        multisample: wgpu::MultisampleState::default(),
                        fragment: Some(wgpu::FragmentState {
                            module: &shader,
                            entry_point: Some("fs_main"),
                            compilation_options: wgpu::PipelineCompilationOptions::default(),
                            targets: &[Some(wgpu::ColorTargetState {
                                format: runtime.render_target_format,
                                blend: Some(wgpu::BlendState::REPLACE),
                                write_mask: wgpu::ColorWrites::ALL,
                            })],
                        }),
                        multiview_mask: None,
                        cache: None,
                    });

            Self {
                offscreen_texture,
                offscreen_view,
                depth_texture,
                depth_view,
                render_pipeline,
                uniform_bind_group_layout,
                uniform_buffer,
                uniform_buffer_size,
                uniform_bind_group,
                canvas_sampler,
                canvas_texture,
                canvas_texture_view,
                canvas_texture_width: 1,
                canvas_texture_height: 1,
                vertex_buffer,
                vertex_buffer_size: vertex_data.len() as u64,
                index_buffer,
                index_buffer_size: index_data.len() as u64,
                index_count,
                width: runtime.width,
                height: runtime.height,
                frame_index: 0,
            }
        }

        pub fn estimated_gpu_memory_bytes_base(&self, runtime: &LocalRuntimeState) -> u64 {
            let mut total = self
                .uniform_buffer_size
                .saturating_add(self.vertex_buffer_size)
                .saturating_add(self.index_buffer_size);

            total = total.saturating_add(estimated_texture_bytes(
                self.canvas_texture_width,
                self.canvas_texture_height,
                wgpu::TextureFormat::Rgba8Unorm,
            ));

            total = total.saturating_add(estimated_texture_bytes(
                self.width,
                self.height,
                wgpu::TextureFormat::Depth32Float,
            ));

            if runtime.surface.is_some() {
                if let Some(config) = runtime.surface_config.as_ref() {
                    // Swapchain depth is driver-managed; estimate double-buffered color allocations only.
                    total = total.saturating_add(
                        estimated_texture_bytes(config.width, config.height, config.format)
                            .saturating_mul(2),
                    );
                }
            } else {
                total = total.saturating_add(estimated_texture_bytes(
                    self.width,
                    self.height,
                    runtime.render_target_format,
                ));
            }

            total
        }

        fn update_uniforms(&mut self, runtime: &LocalRuntimeState) {
            let aspect = (self.width as f32 / self.height.max(1) as f32).max(0.0001);
            let t = self.frame_index as f32 * 0.016;

            let projection = perspective_rh_zo(60.0_f32.to_radians(), aspect, 0.1, 100.0);
            let view = translation_matrix(0.0, 0.0, -4.5);
            // Keep the textured face visible during validation while still animating.
            let model = mul_matrix(rotation_y_matrix(0.55 + t * 0.35), rotation_x_matrix(-0.20));
            let mvp = mul_matrix(mul_matrix(projection, view), model);
            runtime
                .queue
                .write_buffer(&self.uniform_buffer, 0, bytemuck::cast_slice(&mvp));
        }

        pub fn install_debug_texture(
            &mut self,
            runtime: &LocalRuntimeState,
            upload_width: u32,
            upload_height: u32,
            rgba: &[u8],
        ) -> bool {
            if upload_width == 0 || upload_height == 0 {
                return false;
            }

            let width = runtime.clamped_dimension(upload_width);
            let height = runtime.clamped_dimension(upload_height);
            let expected_len = (width as usize)
                .saturating_mul(height as usize)
                .saturating_mul(4);
            if rgba.len() < expected_len {
                return false;
            }

            if self.canvas_texture_width != width || self.canvas_texture_height != height {
                // Recreate only on dimension changes; steady-state updates reuse the same GPU objects.
                // Eagerly destroy replaced storage to avoid long-lived allocations when
                // workloads repeatedly resize upload textures.
                self.canvas_texture.destroy();
                let (texture, view) =
                    create_canvas_texture_with_view(&runtime.device, width, height);
                self.canvas_texture = texture;
                self.canvas_texture_view = view;
                self.canvas_texture_width = width;
                self.canvas_texture_height = height;
                self.uniform_bind_group = create_debug_cube_bind_group(
                    &runtime.device,
                    &self.uniform_bind_group_layout,
                    &self.uniform_buffer,
                    &self.canvas_sampler,
                    &self.canvas_texture_view,
                );
            }

            runtime.queue.write_texture(
                wgpu::TexelCopyTextureInfo {
                    texture: &self.canvas_texture,
                    mip_level: 0,
                    origin: wgpu::Origin3d::ZERO,
                    aspect: wgpu::TextureAspect::All,
                },
                &rgba[..expected_len],
                wgpu::TexelCopyBufferLayout {
                    offset: 0,
                    bytes_per_row: Some(width.saturating_mul(4)),
                    rows_per_image: Some(height),
                },
                wgpu::Extent3d {
                    width,
                    height,
                    depth_or_array_layers: 1,
                },
            );
            true
        }

        pub fn resize(&mut self, runtime: &mut LocalRuntimeState, width: u32, height: u32) {
            (self.width, self.height) = runtime.clamped_extent(width, height);

            if runtime.surface.is_some() {
                runtime.reconfigure_surface(self.width, self.height);
            } else {
                let (texture, view) = runtime.create_offscreen_target(self.width, self.height);
                self.offscreen_texture = Some(texture);
                self.offscreen_view = Some(view);
            }

            let (depth_texture, depth_view) = runtime.create_depth_target(self.width, self.height);
            self.depth_texture = depth_texture;
            self.depth_view = depth_view;
        }

        pub fn render(
            &mut self,
            runtime: &mut LocalRuntimeState,
            draw_enabled: bool,
            screenshot_requested: bool,
        ) -> Result<Option<ScreenshotData>, String> {
            // The interop cube path is a temporary presentation shim while
            // BabylonJS WebGPU command coverage is incrementally replaced with
            // upstream wgpu-native C-ABI ownership. Keep rendering gated by
            // observed WebGPU JS draw traffic so native output reflects the
            // JS->C++->Rust path instead of free-running independently.
            if !draw_enabled {
                return Ok(None);
            }

            self.update_uniforms(runtime);

            let (color_view, surface_frame) = match acquire_draw_target(
                runtime,
                self.width,
                self.height,
                &mut self.offscreen_texture,
                &mut self.offscreen_view,
            )? {
                DrawTargetAcquireResult::Ready {
                    color_view,
                    surface_frame,
                } => (color_view, surface_frame),
                DrawTargetAcquireResult::Reconfigure => {
                    runtime.reconfigure_surface(self.width, self.height);
                    return Ok(None);
                }
                DrawTargetAcquireResult::SkipFrame => {
                    return Ok(None);
                }
            };

            let mut encoder =
                runtime
                    .device
                    .create_command_encoder(&wgpu::CommandEncoderDescriptor {
                        label: Some("babylon-native-webgpu.encoder"),
                    });

            {
                let mut render_pass = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
                    label: Some("babylon-native-webgpu.cube-pass"),
                    color_attachments: &[Some(wgpu::RenderPassColorAttachment {
                        view: &color_view,
                        depth_slice: None,
                        resolve_target: None,
                        ops: wgpu::Operations {
                            load: wgpu::LoadOp::Clear(wgpu::Color {
                                r: 0.03,
                                g: 0.05,
                                b: 0.08,
                                a: 1.0,
                            }),
                            store: wgpu::StoreOp::Store,
                        },
                    })],
                    depth_stencil_attachment: Some(wgpu::RenderPassDepthStencilAttachment {
                        view: &self.depth_view,
                        depth_ops: Some(wgpu::Operations {
                            load: wgpu::LoadOp::Clear(1.0),
                            store: wgpu::StoreOp::Store,
                        }),
                        stencil_ops: None,
                    }),
                    occlusion_query_set: None,
                    timestamp_writes: None,
                    multiview_mask: None,
                });

                render_pass.set_pipeline(&self.render_pipeline);
                render_pass.set_bind_group(0, &self.uniform_bind_group, &[]);
                render_pass.set_vertex_buffer(0, self.vertex_buffer.slice(..));
                render_pass
                    .set_index_buffer(self.index_buffer.slice(..), wgpu::IndexFormat::Uint16);
                render_pass.draw_indexed(0..self.index_count, 0, 0..1);
            }

            let capture = if screenshot_requested {
                if surface_frame.is_some()
                    && !runtime
                        .surface_config
                        .as_ref()
                        .map(|config| config.usage.contains(wgpu::TextureUsages::COPY_SRC))
                        .unwrap_or(false)
                {
                    return Err(
                        "surface texture does not support screenshot COPY_SRC usage".to_string()
                    );
                }

                let source_texture = surface_frame
                    .as_ref()
                    .map(|frame| &frame.texture)
                    .or_else(|| self.offscreen_texture.as_ref())
                    .ok_or_else(|| "screenshot source texture was unavailable".to_string())?;

                Some(copy_texture_to_readback_buffer(
                    &runtime.device,
                    &mut encoder,
                    source_texture,
                    self.width,
                    self.height,
                )?)
            } else {
                None
            };

            let command_buffer = encoder.finish();
            if capture.is_none() {
                submit_and_present(
                    &runtime.device,
                    &runtime.queue,
                    command_buffer,
                    surface_frame,
                );
            } else {
                runtime.queue.submit(Some(command_buffer));
                if let Some(frame) = surface_frame {
                    frame.present();
                }
            }

            self.frame_index = self.frame_index.wrapping_add(1);
            if let Some((staging_buffer, padded_bytes_per_row, unpadded_bytes_per_row)) = capture {
                map_readback_buffer_to_rgba(
                    &runtime.device,
                    &staging_buffer,
                    padded_bytes_per_row,
                    unpadded_bytes_per_row,
                    self.width,
                    self.height,
                    runtime.render_target_format,
                )
                .map(Some)
            } else {
                Ok(None)
            }
        }
    }

    impl LocalRuntimeState {
        pub fn bootstrap(config: LocalBootstrapConfig) -> Result<Self, String> {
            let requested_width = config.width.max(1);
            let requested_height = config.height.max(1);

            let instance = create_local_instance();
            let surface = create_local_surface(&instance, config.surface_layer)?;

            let bootstrap =
                bootstrap_local_wgpu_runtime(&instance, surface.as_ref(), config.prefer_low_power)?;
            let max_texture_dimension_2d = bootstrap.limits.max_texture_dimension_2d.max(1);
            let (width, height) =
                clamped_extent(requested_width, requested_height, max_texture_dimension_2d);

            let mut surface_config = None;
            if let Some(surface_ref) = surface.as_ref() {
                let config = configure_local_surface(
                    surface_ref,
                    &bootstrap.adapter,
                    &bootstrap.device,
                    width,
                    height,
                )?;
                surface_config = Some(config);
            }

            let render_target_format = surface_config
                .as_ref()
                .map(|config| config.format)
                .unwrap_or(wgpu::TextureFormat::Rgba8Unorm);
            let resolved_adapter_info = AdapterProbeInfo {
                backend: map_local_backend_to_babylon_backend(bootstrap.adapter_info.backend),
                vendor_id: bootstrap.adapter_info.vendor,
                device_id: bootstrap.adapter_info.device,
                adapter_name: bootstrap.adapter_info.name.clone(),
            };

            Ok(Self {
                device: bootstrap.device,
                queue: bootstrap.queue,
                surface,
                surface_config,
                resolved_adapter_info,
                max_texture_dimension_2d,
                width,
                height,
                render_target_format,
                used_fallback_adapter: bootstrap.used_fallback_adapter,
                surface_acquire_failures: 0,
            })
        }

        pub fn reconfigure_surface(&mut self, width: u32, height: u32) {
            self.width = width.max(1);
            self.height = height.max(1);

            if let (Some(surface), Some(config)) =
                (self.surface.as_ref(), self.surface_config.as_mut())
            {
                reconfigure_local_surface(surface, &self.device, config, self.width, self.height);
            }
        }

        pub fn clamped_dimension(&self, value: u32) -> u32 {
            clamp_dimension(value, self.max_texture_dimension_2d)
        }

        pub fn clamped_extent(&self, width: u32, height: u32) -> (u32, u32) {
            clamped_extent(width, height, self.max_texture_dimension_2d)
        }

        pub fn create_offscreen_target(
            &self,
            width: u32,
            height: u32,
        ) -> (wgpu::Texture, wgpu::TextureView) {
            let (width, height) = self.clamped_extent(width, height);
            let texture = self.device.create_texture(&wgpu::TextureDescriptor {
                label: Some("babylon-native-webgpu.offscreen-color"),
                size: wgpu::Extent3d {
                    width,
                    height,
                    depth_or_array_layers: 1,
                },
                mip_level_count: 1,
                sample_count: 1,
                dimension: wgpu::TextureDimension::D2,
                format: self.render_target_format,
                usage: wgpu::TextureUsages::RENDER_ATTACHMENT | wgpu::TextureUsages::COPY_SRC,
                view_formats: &[],
            });

            let view = texture.create_view(&wgpu::TextureViewDescriptor {
                label: Some("babylon-native-webgpu.offscreen-color-view"),
                ..Default::default()
            });
            (texture, view)
        }

        pub fn create_depth_target(
            &self,
            width: u32,
            height: u32,
        ) -> (wgpu::Texture, wgpu::TextureView) {
            let (width, height) = self.clamped_extent(width, height);
            let texture = self.device.create_texture(&wgpu::TextureDescriptor {
                label: Some("babylon-native-webgpu.depth"),
                size: wgpu::Extent3d {
                    width,
                    height,
                    depth_or_array_layers: 1,
                },
                mip_level_count: 1,
                sample_count: 1,
                dimension: wgpu::TextureDimension::D2,
                format: wgpu::TextureFormat::Depth32Float,
                usage: wgpu::TextureUsages::RENDER_ATTACHMENT,
                view_formats: &[],
            });

            let view = texture.create_view(&wgpu::TextureViewDescriptor {
                label: Some("babylon-native-webgpu.depth-view"),
                ..Default::default()
            });
            (texture, view)
        }
    }

    fn clamp_dimension(value: u32, max_dimension: u32) -> u32 {
        value.max(1).min(max_dimension.max(1))
    }

    fn clamped_extent(width: u32, height: u32, max_dimension: u32) -> (u32, u32) {
        (
            clamp_dimension(width, max_dimension),
            clamp_dimension(height, max_dimension),
        )
    }

    pub fn create_local_surface(
        _instance: &wgpu::Instance,
        surface_layer: *mut c_void,
    ) -> Result<Option<wgpu::Surface<'static>>, String> {
        if surface_layer.is_null() {
            return Ok(None);
        }

        #[cfg(any(target_os = "macos", target_os = "ios"))]
        {
            // SAFETY: The caller passes a valid CoreAnimation layer pointer that stays alive
            // for the lifetime of the created surface.
            return unsafe {
                _instance.create_surface_unsafe(wgpu::SurfaceTargetUnsafe::CoreAnimationLayer(
                    surface_layer,
                ))
            }
            .map(Some)
            .map_err(|error| format!("Failed to create CoreAnimation surface: {error}"));
        }

        #[cfg(target_os = "android")]
        {
            use raw_window_handle::{
                AndroidDisplayHandle, AndroidNdkWindowHandle, RawDisplayHandle, RawWindowHandle,
            };
            use std::ptr::NonNull;

            let native_window = NonNull::new(surface_layer)
                .ok_or_else(|| "Android surface pointer was null.".to_string())?;
            let raw_display_handle = RawDisplayHandle::Android(AndroidDisplayHandle::new());
            let raw_window_handle =
                RawWindowHandle::AndroidNdk(AndroidNdkWindowHandle::new(native_window));

            // SAFETY: The caller passes an ANativeWindow* that remains valid while the
            // surface is alive.
            return unsafe {
                _instance.create_surface_unsafe(wgpu::SurfaceTargetUnsafe::RawHandle {
                    raw_display_handle: Some(raw_display_handle),
                    raw_window_handle,
                })
            }
            .map(Some)
            .map_err(|error| format!("Failed to create Android Vulkan surface: {error}"));
        }

        #[cfg(target_os = "windows")]
        {
            use raw_window_handle::{
                RawDisplayHandle, RawWindowHandle, Win32WindowHandle, WindowsDisplayHandle,
            };
            use std::num::NonZeroIsize;

            let hwnd = NonZeroIsize::new(surface_layer as isize)
                .ok_or_else(|| "Windows HWND pointer was null.".to_string())?;
            let raw_display_handle = RawDisplayHandle::Windows(WindowsDisplayHandle::new());
            let raw_window_handle = RawWindowHandle::Win32(Win32WindowHandle::new(hwnd));

            // SAFETY: The caller passes a valid HWND that remains alive while the
            // surface is alive.
            return unsafe {
                _instance.create_surface_unsafe(wgpu::SurfaceTargetUnsafe::RawHandle {
                    raw_display_handle: Some(raw_display_handle),
                    raw_window_handle,
                })
            }
            .map(Some)
            .map_err(|error| format!("Failed to create Win32 DX12 surface: {error}"));
        }

        #[allow(unreachable_code)]
        {
            Ok(None)
        }
    }

    pub fn configure_local_surface(
        surface: &wgpu::Surface<'static>,
        adapter: &wgpu::Adapter,
        device: &wgpu::Device,
        width: u32,
        height: u32,
    ) -> Result<wgpu::SurfaceConfiguration, String> {
        let mut config = surface
            .get_default_config(adapter, width.max(1), height.max(1))
            .ok_or_else(|| "Surface returned no default configuration.".to_string())?;

        let caps = surface.get_capabilities(adapter);
        if caps.formats.contains(&wgpu::TextureFormat::Bgra8UnormSrgb) {
            config.format = wgpu::TextureFormat::Bgra8UnormSrgb;
            config.view_formats = vec![wgpu::TextureFormat::Bgra8Unorm];
        } else if caps.formats.contains(&wgpu::TextureFormat::Bgra8Unorm) {
            config.format = wgpu::TextureFormat::Bgra8Unorm;
            config.view_formats = vec![wgpu::TextureFormat::Bgra8UnormSrgb];
        }
        if caps.alpha_modes.contains(&wgpu::CompositeAlphaMode::Opaque) {
            config.alpha_mode = wgpu::CompositeAlphaMode::Opaque;
        }
        if caps.usages.contains(wgpu::TextureUsages::COPY_SRC) {
            config.usage |= wgpu::TextureUsages::COPY_SRC;
        }
        if caps.usages.contains(wgpu::TextureUsages::COPY_DST) {
            config.usage |= wgpu::TextureUsages::COPY_DST;
        }

        surface.configure(device, &config);
        Ok(config)
    }

    pub fn reconfigure_local_surface(
        surface: &wgpu::Surface<'static>,
        device: &wgpu::Device,
        surface_config: &mut wgpu::SurfaceConfiguration,
        width: u32,
        height: u32,
    ) {
        surface_config.width = width.max(1);
        surface_config.height = height.max(1);
        surface.configure(device, surface_config);
    }

    pub enum DrawTargetAcquireResult {
        Ready {
            color_view: wgpu::TextureView,
            surface_frame: Option<wgpu::SurfaceTexture>,
        },
        Reconfigure,
        SkipFrame,
    }

    pub fn acquire_draw_target(
        runtime: &mut LocalRuntimeState,
        width: u32,
        height: u32,
        offscreen_texture: &mut Option<wgpu::Texture>,
        offscreen_view: &mut Option<wgpu::TextureView>,
    ) -> Result<DrawTargetAcquireResult, String> {
        if let Some(surface) = runtime.surface.as_ref() {
            let surface_texture_result =
                std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
                    surface.get_current_texture()
                }));
            return match surface_texture_result {
                Err(_) => Ok(DrawTargetAcquireResult::Reconfigure),
                Ok(surface_result) => match surface_result {
                    wgpu::CurrentSurfaceTexture::Success(surface_frame)
                    | wgpu::CurrentSurfaceTexture::Suboptimal(surface_frame) => {
                        runtime.surface_acquire_failures = 0;
                        let color_view =
                            surface_frame
                                .texture
                                .create_view(&wgpu::TextureViewDescriptor {
                                    label: Some("babylon-native-webgpu.surface-view"),
                                    ..Default::default()
                                });
                        Ok(DrawTargetAcquireResult::Ready {
                            color_view,
                            surface_frame: Some(surface_frame),
                        })
                    }
                    wgpu::CurrentSurfaceTexture::Lost | wgpu::CurrentSurfaceTexture::Outdated => {
                        runtime.surface_acquire_failures = 0;
                        Ok(DrawTargetAcquireResult::Reconfigure)
                    }
                    wgpu::CurrentSurfaceTexture::Timeout
                    | wgpu::CurrentSurfaceTexture::Occluded => {
                        // Recover from transient acquire failures by forcing a
                        // reconfigure after a few consecutive skips.
                        runtime.surface_acquire_failures =
                            runtime.surface_acquire_failures.saturating_add(1);
                        if runtime.surface_acquire_failures >= 4 {
                            runtime.surface_acquire_failures = 0;
                            Ok(DrawTargetAcquireResult::Reconfigure)
                        } else {
                            Ok(DrawTargetAcquireResult::SkipFrame)
                        }
                    }
                    wgpu::CurrentSurfaceTexture::Validation => {
                        Ok(DrawTargetAcquireResult::Reconfigure)
                    }
                },
            };
        }

        if offscreen_view.is_none() {
            let (texture, view) = runtime.create_offscreen_target(width, height);
            *offscreen_texture = Some(texture);
            *offscreen_view = Some(view);
        }

        let color_view = offscreen_view
            .as_ref()
            .ok_or_else(|| {
                "offscreen render target view was not available after creation".to_string()
            })?
            .clone();

        Ok(DrawTargetAcquireResult::Ready {
            color_view,
            surface_frame: None,
        })
    }

    pub fn submit_and_present(
        device: &wgpu::Device,
        queue: &wgpu::Queue,
        command_buffer: wgpu::CommandBuffer,
        surface_frame: Option<wgpu::SurfaceTexture>,
    ) {
        queue.submit(Some(command_buffer));
        if let Some(frame) = surface_frame {
            frame.present();
        }

        // Keep backend housekeeping progressing so completed submissions are
        // reclaimed without waiting for explicit map/poll paths.
        //
        // iOS simulator builds are especially sensitive to delayed submission
        // retirement and can exhibit sustained RSS growth unless we block for
        // completion. Use the stronger mode only on simulator targets.
        #[cfg(all(target_os = "ios", target_abi = "sim"))]
        {
            let _ = device.poll(wgpu::PollType::wait_indefinitely());
        }

        #[cfg(all(target_os = "ios", not(target_abi = "sim")))]
        {
            static IOS_SUBMIT_POLL_TICK: std::sync::atomic::AtomicU32 =
                std::sync::atomic::AtomicU32::new(0);
            let tick = IOS_SUBMIT_POLL_TICK.fetch_add(1, Ordering::Relaxed);
            let poll_mode = if tick % 8 == 0 {
                wgpu::PollType::wait_indefinitely()
            } else {
                wgpu::PollType::Poll
            };
            let _ = device.poll(poll_mode);
        }

        #[cfg(not(target_os = "ios"))]
        {
            static NON_IOS_SUBMIT_POLL_TICK: std::sync::atomic::AtomicU32 =
                std::sync::atomic::AtomicU32::new(0);
            let tick = NON_IOS_SUBMIT_POLL_TICK.fetch_add(1, Ordering::Relaxed);
            let poll_mode = if tick % 120 == 0 {
                // Bound queue residency drift in long-running sessions while
                // keeping steady-state frame pacing non-blocking.
                wgpu::PollType::wait_indefinitely()
            } else {
                wgpu::PollType::Poll
            };
            let _ = device.poll(poll_mode);
        }
    }

    fn map_local_backend_to_babylon_backend(backend: wgpu::Backend) -> u32 {
        match backend {
            wgpu::Backend::Vulkan => 1,
            wgpu::Backend::Metal => 2,
            wgpu::Backend::Dx12 => 3,
            wgpu::Backend::Gl => 4,
            _ => 0,
        }
    }

    pub fn preferred_wgpu_backends() -> wgpu::Backends {
        #[cfg(any(target_os = "macos", target_os = "ios"))]
        {
            return wgpu::Backends::METAL;
        }

        #[cfg(target_os = "windows")]
        {
            return wgpu::Backends::DX12;
        }

        #[cfg(not(any(target_os = "macos", target_os = "ios", target_os = "windows")))]
        {
            return wgpu::Backends::VULKAN;
        }
    }

    pub fn create_local_instance() -> wgpu::Instance {
        #[allow(unused_mut)]
        let mut descriptor = wgpu::InstanceDescriptor::new_without_display_handle();
        descriptor.backends = preferred_wgpu_backends();

        #[cfg(target_os = "android")]
        {
            descriptor.flags |= wgpu::InstanceFlags::ALLOW_UNDERLYING_NONCOMPLIANT_ADAPTER;
        }

        wgpu::Instance::new(descriptor)
    }

    pub fn bootstrap_local_wgpu_runtime(
        instance: &wgpu::Instance,
        compatible_surface: Option<&wgpu::Surface<'_>>,
        prefer_low_power: bool,
    ) -> Result<LocalBootstrapRuntime, String> {
        let power_preference = if prefer_low_power {
            wgpu::PowerPreference::LowPower
        } else {
            wgpu::PowerPreference::HighPerformance
        };

        fn try_adapter(
            adapter_errors: &mut Vec<String>,
            instance: &wgpu::Instance,
            power_preference: wgpu::PowerPreference,
            force_fallback_adapter: bool,
            surface: Option<&wgpu::Surface<'_>>,
            label: &'static str,
        ) -> Option<(wgpu::Adapter, bool)> {
            let options = wgpu::RequestAdapterOptions {
                power_preference,
                force_fallback_adapter,
                compatible_surface: surface,
            };
            match pollster::block_on(instance.request_adapter(&options)) {
                Ok(adapter) => Some((adapter, force_fallback_adapter)),
                Err(error) => {
                    adapter_errors.push(format!("{label}={error}"));
                    None
                }
            }
        }

        let mut adapter_errors: Vec<String> = Vec::new();
        #[cfg(target_os = "android")]
        const ADAPTER_ATTEMPTS: &[(bool, bool, &str)] = &[
            // Android emulator/device behavior can vary with surface-backed
            // selection, so prefer unsurfaced probing first.
            (false, false, "without_surface"),
            (true, false, "with_surface"),
            (false, true, "without_surface_fallback"),
            (true, true, "with_surface_fallback"),
        ];
        #[cfg(not(target_os = "android"))]
        const ADAPTER_ATTEMPTS: &[(bool, bool, &str)] = &[
            (true, false, "with_surface"),
            (false, false, "without_surface"),
            (true, true, "with_surface_fallback"),
            (false, true, "without_surface_fallback"),
        ];

        let adapter_result =
            ADAPTER_ATTEMPTS
                .iter()
                .find_map(|(use_surface, force_fallback_adapter, label)| {
                    let surface = if *use_surface {
                        compatible_surface
                    } else {
                        None
                    };
                    if *use_surface && surface.is_none() {
                        return None;
                    }

                    try_adapter(
                        &mut adapter_errors,
                        instance,
                        power_preference,
                        *force_fallback_adapter,
                        surface,
                        label,
                    )
                });

        #[allow(unused_mut)]
        let (mut adapter, mut used_fallback_adapter) = adapter_result.ok_or_else(|| {
            format!(
                "Failed to acquire GPU adapter. {}",
                adapter_errors.join("; ")
            )
        })?;

        #[allow(unused_mut)]
        let mut adapter_info = adapter.get_info();
        #[allow(unused_mut)]
        let mut adapter_limits = adapter.limits();
        let make_device = |selected_adapter: &wgpu::Adapter, limits: &wgpu::Limits| {
            let supported_features = selected_adapter.features();
            let mut required_features = wgpu::Features::empty();
            if supported_features.contains(wgpu::Features::EXTERNAL_TEXTURE) {
                required_features |= wgpu::Features::EXTERNAL_TEXTURE;
            }
            if supported_features.contains(wgpu::Features::BGRA8UNORM_STORAGE) {
                required_features |= wgpu::Features::BGRA8UNORM_STORAGE;
            }

            let descriptor = wgpu::DeviceDescriptor {
                label: Some("babylon-native-webgpu.device"),
                required_features,
                required_limits: limits.clone(),
                experimental_features: wgpu::ExperimentalFeatures::disabled(),
                memory_hints: wgpu::MemoryHints::default(),
                trace: wgpu::Trace::default(),
            };

            pollster::block_on(selected_adapter.request_device(&descriptor))
        };
        #[allow(unused_mut)]
        let mut device_result = make_device(&adapter, &adapter_limits);

        #[cfg(target_os = "android")]
        if device_result.is_err() {
            // Keep one strict recovery path for Android emulator variability.
            if let Some((retry_adapter, retry_used_fallback)) = try_adapter(
                &mut adapter_errors,
                instance,
                wgpu::PowerPreference::LowPower,
                true,
                None,
                "retry_low_power_fallback",
            ) {
                adapter = retry_adapter;
                used_fallback_adapter = retry_used_fallback;
                adapter_info = adapter.get_info();
                adapter_limits = adapter.limits();
                device_result = make_device(&adapter, &adapter_limits);
            }
        }

        let (device, queue) = device_result.map_err(|error| {
            format!(
                "Failed to create GPU device: {error} (adapter=\"{}\" backend={:?})",
                adapter_info.name, adapter_info.backend
            )
        })?;

        Ok(LocalBootstrapRuntime {
            adapter,
            adapter_info,
            limits: adapter_limits,
            device,
            queue,
            used_fallback_adapter,
        })
    }

    // The `enabled` submodule lives in compute.rs but is declared as a child of
    // `upstream_wgpu_native` so that `use super::*` inside compute.rs resolves to
    // the types and helpers defined in this module.
    #[path = "compute.rs"]
    mod enabled;

    pub use enabled::{dispatch_compute_global, version};
}

// Consolidate Rust backend code into a single staticlib so the native binary
// carries one wgpu code instance for both GraphicsWgpu and CanvasWgpu exports.
//
// TODO: Convert CanvasWgpu into a workspace member crate once the backend code
// stabilizes. The `#[path]` include avoids a second staticlib link target during
// the current rapid-iteration phase but should be replaced with a proper crate
// dependency (e.g. `canvas_wgpu_backend = { path = "..." }`) to get independent
// compilation units and cleaner module boundaries.
#[path = "../../../../Polyfills/CanvasWgpu/Rust/src/lib.rs"]
mod canvas_wgpu_backend_exports;
