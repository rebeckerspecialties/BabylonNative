struct Vertex {
    @builtin(position) pos: vec4f,
    @location(0) uv: vec2f,
    @location(1) @interpolate(flat) flags: u32,
}

@group(0) @binding(0) var canvas: texture_2d<f32>;

@vertex fn vs(@builtin(vertex_index) i: u32, @builtin(instance_index) flags: u32) -> Vertex {
    let xy = vec2f(f32((i << 1u) & 2u), f32(i & 2u));
    return Vertex(vec4f(xy * 2.0 - 1.0, 0.0, 1.0),
        vec2f(xy.x, select(1.0 - xy.y, xy.y, (flags & 1u) != 0u)), flags);
}

@fragment fn fs(input: Vertex) -> @location(0) vec4f {
    let c = textureLoad(canvas, vec2i(input.uv * vec2f(textureDimensions(canvas))), 0);
    var rgb = c.rgb;
    if (input.flags & 2u) != 0u {
        rgb = select(vec3f(0), min(rgb / max(c.a, 1.0 / 255.0), vec3f(1)), c.a > 0.0);
    }
    // Canvas bytes are already sRGB encoded; compensate for attachment encoding.
    if (input.flags & 4u) != 0u {
        rgb = select(pow((rgb + 0.055) / 1.055, vec3f(2.4)), rgb / 12.92, rgb <= vec3f(0.04045));
    }
    return vec4f(rgb, c.a);
}
