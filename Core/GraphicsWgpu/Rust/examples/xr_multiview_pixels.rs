//! A two-pass pixel oracle for the wgpu backend. Does not start OpenXR or a compositor.
use std::num::NonZeroU32;

const W: u32 = 64;
const H: u32 = 32;
const SHADER: &str = r#"
@vertex fn vs(@builtin(vertex_index) i:u32, EYE) -> @builtin(position) vec4f {
    let corner = array(vec2f(-0.20,-0.25),vec2f(0.20,-0.25),vec2f(0.,0.25))[i];
    return vec4f(corner + vec2f(select(-0.5,0.5,eye == 1u),0.),0.5,1.);
}
@fragment fn fs() -> @location(0) vec4f {return vec4f(1.,0.5,0.,1.);}
"#;

fn render(
    device: &wgpu::Device,
    queue: &wgpu::Queue,
    single: bool,
    independent: bool,
    depth_format: Option<wgpu::TextureFormat>,
) -> Vec<u8> {
    let sampled = std::env::args().any(|arg| arg == "--sampled-depth");
    let draw_clear = std::env::args().any(|arg| arg == "--draw-clear");
    let reverse = std::env::args().any(|arg| arg == "--reverse-depth");
    let size = wgpu::Extent3d {
        width: W,
        height: H,
        depth_or_array_layers: 2,
    };
    let color_format = wgpu::TextureFormat::Rgba8Unorm;
    let texture = |format, usage| {
        device.create_texture(&wgpu::TextureDescriptor {
            label: None,
            size: wgpu::Extent3d {
                depth_or_array_layers: if independent { 1 } else { 2 },
                ..size
            },
            mip_level_count: 1,
            sample_count: 1,
            dimension: wgpu::TextureDimension::D2,
            format,
            usage,
            view_formats: &[],
        })
    };
    let color: Vec<_> = (0..if independent { 2 } else { 1 })
        .map(|_| {
            texture(
                color_format,
                wgpu::TextureUsages::RENDER_ATTACHMENT | wgpu::TextureUsages::COPY_SRC,
            )
        })
        .collect();
    let depth_usage = wgpu::TextureUsages::RENDER_ATTACHMENT
        | if sampled {
            wgpu::TextureUsages::TEXTURE_BINDING
        } else {
            wgpu::TextureUsages::empty()
        };
    let depth: Vec<_> = (0..color.len())
        .map(|_| {
            texture(
                depth_format.unwrap_or(wgpu::TextureFormat::Depth24Plus),
                depth_usage,
            )
        })
        .collect();
    let mask = if single { NonZeroU32::new(3) } else { None };
    let vertex_eye = if single {
        "@builtin(view_index) eye:u32"
    } else {
        "@builtin(instance_index) eye:u32"
    };
    let module = device.create_shader_module(wgpu::ShaderModuleDescriptor {
        label: None,
        source: wgpu::ShaderSource::Wgsl(SHADER.replace("EYE", vertex_eye).into()),
    });
    let make_pipeline = |module: &wgpu::ShaderModule, compare, write_mask| {
        device.create_render_pipeline(&wgpu::RenderPipelineDescriptor {
            label: None,
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
                    format: color_format,
                    blend: None,
                    write_mask,
                })],
            }),
            depth_stencil: depth_format.map(|format| wgpu::DepthStencilState {
                format,
                depth_write_enabled: Some(true),
                depth_compare: Some(compare),
                stencil: Default::default(),
                bias: Default::default(),
            }),
            primitive: Default::default(),
            multisample: Default::default(),
            multiview_mask: mask,
            cache: None,
        })
    };
    let compare = if reverse { wgpu::CompareFunction::Greater } else { wgpu::CompareFunction::Less };
    let pipeline = make_pipeline(&module, compare, wgpu::ColorWrites::ALL);
    // A farther, green triangle drawn last must be rejected. This would fail if the
    // workaround simply disabled depth or if per-eye depth writes were not preserved.
    let occluded = depth_format.map(|_| {
        let shader = SHADER.replace("EYE", vertex_eye)
            .replace("0.),0.5,1.)", if reverse { "0.),0.2,1.)" } else { "0.),0.8,1.)" })
            .replace("1.,0.5,0.,1.", "0.,1.,0.,1.");
        let module = device.create_shader_module(wgpu::ShaderModuleDescriptor {
            label: Some("occluded geometry"), source: wgpu::ShaderSource::Wgsl(shader.into()),
        });
        make_pipeline(&module, compare, wgpu::ColorWrites::ALL)
    });
    let clear_value = if reverse { 0. } else { 1. };
    let clear_pipeline = draw_clear.then(|| {
        let shader = format!("@vertex fn vs(@builtin(vertex_index) i:u32)->@builtin(position) vec4f {{return vec4f(array(vec2f(-1.,-1.),vec2f(3.,-1.),vec2f(-1.,3.))[i],{clear_value:.1},1.);}} @fragment fn fs()->@location(0) vec4f {{return vec4f(0.);}}");
        let clear = device.create_shader_module(wgpu::ShaderModuleDescriptor{label:None,source:wgpu::ShaderSource::Wgsl(shader.into())});
        make_pipeline(&clear,wgpu::CompareFunction::Always,wgpu::ColorWrites::empty())
    });
    let read = device.create_buffer(&wgpu::BufferDescriptor {
        label: None,
        size: u64::from(W * H * 8),
        usage: wgpu::BufferUsages::COPY_DST | wgpu::BufferUsages::MAP_READ,
        mapped_at_creation: false,
    });
    let mut enc = device.create_command_encoder(&Default::default());
    for eye in 0..if single { 1 } else { 2 } {
        let desc = wgpu::TextureViewDescriptor {
            dimension: Some(if single {
                wgpu::TextureViewDimension::D2Array
            } else {
                wgpu::TextureViewDimension::D2
            }),
            base_array_layer: if independent { 0 } else { eye },
            array_layer_count: Some(if single { 2 } else { 1 }),
            ..Default::default()
        };
        let slot = if independent { eye as usize } else { 0 };
        let color_view = color[slot].create_view(&desc);
        let depth_view = depth[slot].create_view(&desc);
        let mut pass = enc.begin_render_pass(&wgpu::RenderPassDescriptor {
            color_attachments: &[Some(wgpu::RenderPassColorAttachment {
                view: &color_view,
                depth_slice: None,
                resolve_target: None,
                ops: wgpu::Operations {
                    load: wgpu::LoadOp::Clear(wgpu::Color::BLACK),
                    store: wgpu::StoreOp::Store,
                },
            })],
            depth_stencil_attachment: depth_format.map(|_| {
                wgpu::RenderPassDepthStencilAttachment {
                    view: &depth_view,
                    depth_ops: Some(wgpu::Operations {
                        load: wgpu::LoadOp::Clear(clear_value),
                        store: wgpu::StoreOp::Store,
                    }),
                    stencil_ops: None,
                }
            }),
            multiview_mask: mask,
            ..Default::default()
        });
        if let Some(clear) = &clear_pipeline {
            pass.set_pipeline(clear);
            pass.draw(0..3, 0..1);
        }
        pass.set_pipeline(&pipeline);
        pass.draw(0..3, eye..eye + 1);
        if let Some(occluded) = &occluded {
            pass.set_pipeline(occluded);
            pass.draw(0..3, eye..eye + 1);
        }
    }
    for eye in 0..2 {
        let slot = if independent { eye as usize } else { 0 };
        let mut src = color[slot].as_image_copy();
        src.origin.z = if independent { 0 } else { eye };
        enc.copy_texture_to_buffer(
            src,
            wgpu::TexelCopyBufferInfo {
                buffer: &read,
                layout: wgpu::TexelCopyBufferLayout {
                    offset: u64::from(eye * W * H * 4),
                    bytes_per_row: Some(W * 4),
                    rows_per_image: Some(H),
                },
            },
            wgpu::Extent3d {
                depth_or_array_layers: 1,
                ..size
            },
        );
    }
    queue.submit([enc.finish()]);
    let (tx, rx) = std::sync::mpsc::channel();
    read.slice(..)
        .map_async(wgpu::MapMode::Read, move |r| tx.send(r).unwrap());
    device.poll(wgpu::PollType::wait_indefinitely()).unwrap();
    rx.recv().unwrap().unwrap();
    let bytes = read.slice(..).get_mapped_range().unwrap().to_vec();
    read.unmap();
    for (eye, pixels) in bytes.chunks_exact((W * H * 4) as usize).enumerate() {
        let xs: Vec<_> = pixels
            .chunks_exact(4)
            .enumerate()
            .filter(|(_, p)| p[0] > 200 && p[2] == 0)
            .map(|(i, _)| i % W as usize)
            .collect();
        let center = xs.iter().sum::<usize>() as f64 / xs.len() as f64;
        println!("single={single} independent={independent} depth={depth_format:?} eye={eye} background={:?} markerPixels={} centerX={center}",&pixels[..4],xs.len());
        if independent {
            assert!(
                (center - [15.5, 47.5][eye]).abs() < 1.,
                "invalid independent reference"
            );
        }
    }
    bytes
}
fn main() {
    let instance = wgpu::Instance::new(wgpu::InstanceDescriptor {
        backends: if cfg!(target_os = "android") {
            wgpu::Backends::VULKAN
        } else {
            wgpu::Backends::PRIMARY
        },
        ..wgpu::InstanceDescriptor::new_without_display_handle()
    });
    let adapter = pollster::block_on(instance.request_adapter(&Default::default())).unwrap();
    let features = adapter.features() & wgpu::Features::MULTIVIEW;
    println!(
        "adapter={:?} features={features:?} maxViews={}",
        adapter.get_info(),
        adapter.limits().max_multiview_view_count
    );
    let (device, queue) = pollster::block_on(adapter.request_device(&wgpu::DeviceDescriptor {
        required_features: features,
        required_limits: adapter.limits(),
        ..Default::default()
    }))
    .unwrap();
    let mut errors = 0;
    for depth in [
        None,
        Some(wgpu::TextureFormat::Depth16Unorm),
        Some(wgpu::TextureFormat::Depth24Plus),
        Some(wgpu::TextureFormat::Depth24PlusStencil8),
        Some(wgpu::TextureFormat::Depth32Float),
    ] {
        let reference = render(&device, &queue, false, true, depth);
        for single in [false, true] {
            if single {
                assert!(
                    features.contains(wgpu::Features::MULTIVIEW),
                    "multiview is unavailable"
                );
            }
            let layered = render(&device, &queue, single, false, depth);
            let differences = reference
                .iter()
                .zip(layered)
                .filter(|(a, b)| **a != *b)
                .count();
            println!(
                "single={single} depth={depth:?} differentBytes={differences}/{}",
                reference.len()
            );
            errors += differences;
        }
    }
    assert_eq!(
        errors, 0,
        "layered outputs differ from independent two-pass reference"
    );
    println!("PASS");
}
