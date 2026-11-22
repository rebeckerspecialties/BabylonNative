pub(super) struct Pipeline {
    pipeline: wgpu::RenderPipeline,
    layout: wgpu::BindGroupLayout,
}

pub(super) struct Target {
    pipeline: wgpu::RenderPipeline,
    layers: Vec<Layer>,
}

struct Layer {
    binding: wgpu::BindGroup,
    destination: wgpu::TextureView,
}

impl Pipeline {
    pub(super) fn new(device: &wgpu::Device, format: wgpu::TextureFormat) -> Self {
        let module = device.create_shader_module(wgpu::include_wgsl!("xr_eye_resolve.wgsl"));
        let pipeline = device.create_render_pipeline(&wgpu::RenderPipelineDescriptor {
            label: Some("NativeXR linear-light resolve"),
            layout: None,
            vertex: wgpu::VertexState {
                module: &module,
                entry_point: Some("vs"),
                buffers: &[],
                compilation_options: Default::default(),
            },
            fragment: Some(wgpu::FragmentState {
                module: &module,
                entry_point: Some("fs"),
                targets: &[Some(format.into())],
                compilation_options: Default::default(),
            }),
            primitive: Default::default(),
            multisample: Default::default(),
            depth_stencil: None,
            multiview_mask: None,
            cache: None,
        });
        Self {
            layout: pipeline.get_bind_group_layout(0),
            pipeline,
        }
    }

    pub(super) fn target(
        &self,
        device: &wgpu::Device,
        source: &wgpu::Texture,
        destination: &wgpu::Texture,
    ) -> Target {
        let layers = (0..source.depth_or_array_layers())
            .map(|index| {
                let desc = wgpu::TextureViewDescriptor {
                    dimension: Some(wgpu::TextureViewDimension::D2),
                    base_array_layer: index,
                    array_layer_count: Some(1),
                    ..Default::default()
                };
                let view = source.create_view(&desc);
                let binding = device.create_bind_group(&wgpu::BindGroupDescriptor {
                    label: Some("NativeXR eye image"),
                    layout: &self.layout,
                    entries: &[wgpu::BindGroupEntry {
                        binding: 0,
                        resource: wgpu::BindingResource::TextureView(&view),
                    }],
                });
                Layer {
                    binding,
                    destination: destination.create_view(&desc),
                }
            })
            .collect();
        Target {
            pipeline: self.pipeline.clone(),
            layers,
        }
    }
}

impl Target {
    pub(super) fn layers(&self) -> usize {
        self.layers.len()
    }
    pub(super) fn encode(
        &self,
        index: usize,
        encoder: &mut wgpu::CommandEncoder,
        timestamp_writes: Option<wgpu::RenderPassTimestampWrites<'_>>,
    ) {
        let mut pass = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
            label: Some("NativeXR resolve before release"),
            color_attachments: &[Some(wgpu::RenderPassColorAttachment {
                view: &self.layers[index].destination,
                depth_slice: None,
                resolve_target: None,
                // Every pixel is overwritten; do not load the preceding frame into tile memory.
                ops: wgpu::Operations {
                    load: wgpu::LoadOp::Clear(wgpu::Color::TRANSPARENT),
                    store: wgpu::StoreOp::Store,
                },
            })],
            timestamp_writes,
            ..Default::default()
        });
        pass.set_pipeline(&self.pipeline);
        pass.set_bind_group(0, &self.layers[index].binding, &[]);
        pass.draw(0..3, 0..1);
    }
}
