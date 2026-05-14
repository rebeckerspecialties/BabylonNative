// Compute dispatch path for the branch-local WebGPU shim.
//
// Keep this small: it exists to preserve the native WebGPU test hook while the
// renderer is being collapsed toward the upstream wgpu-native C API.

use std::sync::{Mutex, OnceLock};

use super::{bootstrap_local_wgpu_runtime, create_local_instance, LocalBootstrapRuntime};

#[cfg(feature = "wgpu-native-rlib")]
unsafe extern "C" {
    fn wgpuGetVersion() -> u32;
}

struct ComputeRuntime {
    _instance: wgpu::Instance,
    _bootstrap: LocalBootstrapRuntime,
    cached_shader_source: String,
    cached_entry_point: String,
    cached_pipeline: Option<wgpu::ComputePipeline>,
}

static COMPUTE_RUNTIME: OnceLock<Mutex<Option<ComputeRuntime>>> = OnceLock::new();

fn compute_runtime_cell() -> &'static Mutex<Option<ComputeRuntime>> {
    COMPUTE_RUNTIME.get_or_init(|| Mutex::new(None))
}

fn initialize_compute_runtime(prefer_low_power: bool) -> Result<ComputeRuntime, String> {
    let instance = create_local_instance();
    let bootstrap = bootstrap_local_wgpu_runtime(&instance, None, prefer_low_power)?;

    Ok(ComputeRuntime {
        _instance: instance,
        _bootstrap: bootstrap,
        cached_shader_source: String::new(),
        cached_entry_point: String::new(),
        cached_pipeline: None,
    })
}

fn ensure_runtime_locked(
    runtime_slot: &mut Option<ComputeRuntime>,
    prefer_low_power: bool,
) -> Result<&mut ComputeRuntime, String> {
    if runtime_slot.is_none() {
        *runtime_slot = Some(initialize_compute_runtime(prefer_low_power)?);
    }

    Ok(runtime_slot
        .as_mut()
        .expect("runtime initialized before returning mutable reference"))
}

pub fn version() -> u32 {
    #[cfg(feature = "wgpu-native-rlib")]
    {
        // SAFETY: The symbol is provided by the build-local rlib-enabled
        // wgpu-native dependency in the same Cargo graph as the local backend.
        return unsafe { wgpuGetVersion() };
    }

    #[cfg(not(feature = "wgpu-native-rlib"))]
    {
        0
    }
}

pub fn dispatch_compute_global(
    shader_source: &str,
    entry_point: &str,
    x: u32,
    y: u32,
    z: u32,
    prefer_low_power: bool,
) -> Result<(), String> {
    let mut runtime_guard = match compute_runtime_cell().lock() {
        Ok(lock) => lock,
        Err(poisoned) => poisoned.into_inner(),
    };

    let runtime = ensure_runtime_locked(&mut runtime_guard, prefer_low_power)?;
    let entry = if entry_point.is_empty() {
        "main"
    } else {
        entry_point
    };

    let pipeline_needs_rebuild = runtime.cached_pipeline.is_none()
        || runtime.cached_shader_source != shader_source
        || runtime.cached_entry_point != entry;

    if pipeline_needs_rebuild {
        let shader_module =
            runtime
                ._bootstrap
                .device
                .create_shader_module(wgpu::ShaderModuleDescriptor {
                    label: Some("babylon-native-webgpu.compute-shader"),
                    source: wgpu::ShaderSource::Wgsl(shader_source.into()),
                });

        let pipeline =
            runtime
                ._bootstrap
                .device
                .create_compute_pipeline(&wgpu::ComputePipelineDescriptor {
                    label: Some("babylon-native-webgpu.compute-pipeline"),
                    layout: None,
                    module: &shader_module,
                    entry_point: Some(entry),
                    compilation_options: wgpu::PipelineCompilationOptions::default(),
                    cache: None,
                });

        runtime.cached_shader_source.clear();
        runtime.cached_shader_source.push_str(shader_source);
        runtime.cached_entry_point.clear();
        runtime.cached_entry_point.push_str(entry);
        runtime.cached_pipeline = Some(pipeline);
    }

    let pipeline = runtime
        .cached_pipeline
        .as_ref()
        .ok_or_else(|| "compute pipeline was not available after creation".to_string())?;
    let device = &runtime._bootstrap.device;
    let queue = &runtime._bootstrap.queue;

    let mut encoder = device.create_command_encoder(&wgpu::CommandEncoderDescriptor {
        label: Some("babylon-native-webgpu.compute-encoder"),
    });
    {
        let mut pass = encoder.begin_compute_pass(&wgpu::ComputePassDescriptor {
            label: Some("babylon-native-webgpu.compute-pass"),
            timestamp_writes: None,
        });
        pass.set_pipeline(pipeline);
        pass.dispatch_workgroups(x.max(1), y.max(1), z.max(1));
    }

    queue.submit(std::iter::once(encoder.finish()));
    let _ = device.poll(wgpu::PollType::Poll);
    Ok(())
}
