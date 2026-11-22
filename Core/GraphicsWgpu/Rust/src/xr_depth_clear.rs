// Adreno 540 driver feb9270: layered depth loadOp=clear reads back as zero during depth testing.
// Keep the API clear and additionally draw its depth value. A bundle leaves no pipeline bound.
pub(super) fn bundle(
    device: &wgpu::Device,
    color: wgpu::TextureFormat,
    depth: wgpu::TextureFormat,
    stencil_read_only: bool,
    multiview_mask: Option<std::num::NonZeroU32>,
) -> wgpu::RenderBundle {
    let shader = device.create_shader_module(wgpu::ShaderModuleDescriptor {
        label: Some("XR layered depth clear"),
        source: wgpu::ShaderSource::Wgsl(
            r#"
            @vertex fn vs(@builtin(vertex_index) i:u32) -> @builtin(position) vec4f {
                return vec4f(array(vec2f(-1.,-1.),vec2f(3.,-1.),vec2f(-1.,3.))[i],1.,1.);
            }
            @fragment fn fs() -> @location(0) vec4f { return vec4f(0.); }
        "#
            .into(),
        ),
    });
    let pipeline = device.create_render_pipeline(&wgpu::RenderPipelineDescriptor {
        label: Some("XR layered depth clear"),
        layout: None,
        vertex: wgpu::VertexState {
            module: &shader,
            entry_point: Some("vs"),
            compilation_options: Default::default(),
            buffers: &[],
        },
        fragment: Some(wgpu::FragmentState {
            module: &shader,
            entry_point: Some("fs"),
            compilation_options: Default::default(),
            targets: &[Some(wgpu::ColorTargetState {
                format: color,
                blend: None,
                write_mask: wgpu::ColorWrites::empty(),
            })],
        }),
        depth_stencil: Some(wgpu::DepthStencilState {
            format: depth,
            depth_write_enabled: Some(true),
            depth_compare: Some(wgpu::CompareFunction::Always),
            stencil: Default::default(),
            bias: Default::default(),
        }),
        primitive: Default::default(),
        multisample: Default::default(),
        multiview_mask,
        cache: None,
    });
    let mut encoder = device.create_render_bundle_encoder(&wgpu::RenderBundleEncoderDescriptor {
        label: Some("XR layered depth clear"),
        color_formats: &[Some(color)],
        sample_count: 1,
        depth_stencil: Some(wgpu::RenderBundleDepthStencil {
            format: depth,
            depth_read_only: false,
            stencil_read_only,
        }),
        multiview: multiview_mask,
    });
    encoder.set_pipeline(&pipeline);
    encoder.draw(0..3, 0..1);
    encoder.finish(&Default::default())
}
