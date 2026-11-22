// Compute dispatch path for the branch-local WebGPU shim.
//
// Keep this small: it exists to preserve the native WebGPU test hook while the
// renderer is being collapsed toward the upstream wgpu-native C API.

#[cfg(feature = "wgpu-native-rlib")]
unsafe extern "C" {
    fn wgpuGetVersion() -> u32;
}

#[derive(Default)]
pub(crate) struct ComputeCache {
    cached_shader_source: String,
    cached_entry_point: String,
    cached_pipeline: Option<wgpu::ComputePipeline>,
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
) -> Result<(), String> {
    let renderer = crate::active_renderer()?;
    let (device, queue) = &renderer.device_queue;
    let mut runtime = renderer
        .compute_cache
        .lock()
        .unwrap_or_else(|e| e.into_inner());
    let entry = if entry_point.is_empty() {
        "main"
    } else {
        entry_point
    };

    let pipeline_needs_rebuild = runtime.cached_pipeline.is_none()
        || runtime.cached_shader_source != shader_source
        || runtime.cached_entry_point != entry;

    if pipeline_needs_rebuild {
        let shader_module = device.create_shader_module(wgpu::ShaderModuleDescriptor {
            label: Some("babylon-native-webgpu.compute-shader"),
            source: wgpu::ShaderSource::Wgsl(shader_source.into()),
        });

        let pipeline = device.create_compute_pipeline(&wgpu::ComputePipelineDescriptor {
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
