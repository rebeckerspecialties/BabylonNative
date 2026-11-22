use std::collections::HashMap;

#[derive(Default)]
pub(super) struct CanvasSnapshots {
    pipelines: HashMap<wgpu::TextureFormat, wgpu::RenderPipeline>,
    source_region: Option<wgpu::Texture>,
}

impl CanvasSnapshots {
    pub fn copy(
        &mut self,
        device: &wgpu::Device,
        queue: &wgpu::Queue,
        src: &wgpu::Texture,
        src_xy: [u32; 2],
        flip: bool,
        premultiplied: bool,
        dst: wgpu::TexelCopyTextureInfo<'_>,
        size: wgpu::Extent3d,
    ) -> Result<(), String> {
        let format = dst.texture.format();
        use wgpu::TextureFormat as F;
        if !matches!(
            format,
            F::R8Unorm
                | F::R16Float
                | F::R32Float
                | F::Rg8Unorm
                | F::Rg16Float
                | F::Rg32Float
                | F::Rgba8Unorm
                | F::Rgba8UnormSrgb
                | F::Bgra8Unorm
                | F::Bgra8UnormSrgb
                | F::Rgb10a2Unorm
                | F::Rgba16Float
                | F::Rgba32Float
        ) {
            return Err(format!(
                "Unsupported Canvas snapshot destination format {format:?}"
            ));
        }
        if dst.mip_level >= dst.texture.mip_level_count() {
            return Err("Invalid Canvas snapshot destination mip".into());
        }
        let bounds = dst
            .texture
            .size()
            .mip_level_size(dst.mip_level, dst.texture.dimension());
        if dst.texture.dimension() != wgpu::TextureDimension::D2
            || dst.texture.sample_count() != 1
            || dst.aspect != wgpu::TextureAspect::All
            || size.depth_or_array_layers != 1
            || dst.origin.z >= bounds.depth_or_array_layers
            || dst
                .origin
                .x
                .checked_add(size.width)
                .is_none_or(|end| end > bounds.width)
            || dst
                .origin
                .y
                .checked_add(size.height)
                .is_none_or(|end| end > bounds.height)
            || !dst
                .texture
                .usage()
                .contains(wgpu::TextureUsages::COPY_DST | wgpu::TextureUsages::RENDER_ATTACHMENT)
        {
            return Err(
                "Invalid Canvas snapshot destination extent, mip, layer, aspect, or usage".into(),
            );
        }
        if size.width == 0 || size.height == 0 {
            return Ok(());
        }
        let mut encoder = device.create_command_encoder(&wgpu::CommandEncoderDescriptor {
            label: Some("Canvas2D.snapshot"),
        });
        if !flip
            && premultiplied
            && src.format().remove_srgb_suffix() == format.remove_srgb_suffix()
        {
            encoder.copy_texture_to_texture(
                wgpu::TexelCopyTextureInfo {
                    origin: wgpu::Origin3d {
                        x: src_xy[0],
                        y: src_xy[1],
                        z: 0,
                    },
                    ..src.as_image_copy()
                },
                dst,
                size,
            );
        } else {
            let cropped = size.width != src.width() || size.height != src.height();
            let sample_source = if cropped {
                if self
                    .source_region
                    .as_ref()
                    .is_none_or(|tex| tex.size() != size || tex.format() != src.format())
                {
                    self.source_region = Some(device.create_texture(&wgpu::TextureDescriptor {
                        label: Some("Canvas2D.snapshot.sourceRegion"),
                        size,
                        format: src.format(),
                        mip_level_count: 1,
                        sample_count: 1,
                        dimension: wgpu::TextureDimension::D2,
                        usage: wgpu::TextureUsages::COPY_DST | wgpu::TextureUsages::TEXTURE_BINDING,
                        view_formats: &[],
                    }));
                }
                let tex = self.source_region.as_ref().unwrap();
                encoder.copy_texture_to_texture(
                    wgpu::TexelCopyTextureInfo {
                        origin: wgpu::Origin3d {
                            x: src_xy[0],
                            y: src_xy[1],
                            z: 0,
                        },
                        ..src.as_image_copy()
                    },
                    tex.as_image_copy(),
                    size,
                );
                tex
            } else {
                src
            };
            let pipeline = self.pipelines.entry(format).or_insert_with(|| {
                let module = device.create_shader_module(wgpu::ShaderModuleDescriptor {
                    label: Some("Canvas2D.snapshot"),
                    source: wgpu::ShaderSource::Wgsl(include_str!("canvas_snapshot.wgsl").into()),
                });
                device.create_render_pipeline(&wgpu::RenderPipelineDescriptor {
                    label: Some("Canvas2D.snapshot"),
                    layout: None,
                    vertex: wgpu::VertexState {
                        module: &module,
                        entry_point: Some("vs"),
                        compilation_options: Default::default(),
                        buffers: &[],
                    },
                    fragment: Some(wgpu::FragmentState {
                        module: &module,
                        entry_point: Some("fs"),
                        compilation_options: Default::default(),
                        targets: &[Some(wgpu::ColorTargetState {
                            format,
                            blend: None,
                            write_mask: wgpu::ColorWrites::ALL,
                        })],
                    }),
                    primitive: Default::default(),
                    depth_stencil: None,
                    multisample: Default::default(),
                    multiview_mask: None,
                    cache: None,
                })
            });
            let origin = dst.origin;
            let source_view = sample_source.create_view(&Default::default());
            let bind = device.create_bind_group(&wgpu::BindGroupDescriptor {
                label: Some("Canvas2D.snapshot"),
                layout: &pipeline.get_bind_group_layout(0),
                entries: &[wgpu::BindGroupEntry {
                    binding: 0,
                    resource: wgpu::BindingResource::TextureView(&source_view),
                }],
            });
            let partial = size.width != bounds.width || size.height != bounds.height;
            let target = dst.texture.create_view(&wgpu::TextureViewDescriptor {
                dimension: Some(wgpu::TextureViewDimension::D2),
                base_mip_level: dst.mip_level,
                mip_level_count: Some(1),
                base_array_layer: origin.z,
                array_layer_count: Some(1),
                ..Default::default()
            });
            let mut pass = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
                label: Some("Canvas2D.snapshot"),
                color_attachments: &[Some(wgpu::RenderPassColorAttachment {
                    view: &target,
                    depth_slice: None,
                    resolve_target: None,
                    ops: wgpu::Operations {
                        load: if partial {
                            wgpu::LoadOp::Load
                        } else {
                            wgpu::LoadOp::Clear(wgpu::Color::TRANSPARENT)
                        },
                        store: wgpu::StoreOp::Store,
                    },
                })],
                ..Default::default()
            });
            pass.set_pipeline(pipeline);
            pass.set_bind_group(0, &bind, &[]);
            pass.set_scissor_rect(origin.x, origin.y, size.width, size.height);
            pass.set_viewport(
                origin.x as f32,
                origin.y as f32,
                size.width as f32,
                size.height as f32,
                0.0,
                1.0,
            );
            // One triangle, no parameter-buffer allocation or upload per snapshot.
            let flags = u32::from(flip)
                | (u32::from(!premultiplied) << 1)
                | (u32::from(format.is_srgb()) << 2);
            pass.draw(0..3, flags..flags + 1);
        }
        queue.submit([encoder.finish()]);
        Ok(())
    }
}
