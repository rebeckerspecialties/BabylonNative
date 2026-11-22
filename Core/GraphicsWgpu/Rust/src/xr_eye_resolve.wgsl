struct Vertex { @builtin(position) pos: vec4f, @location(0) uv: vec2f };
@vertex fn vs(@builtin(vertex_index) i: u32) -> Vertex {
    let uv = vec2f(f32((i << 1u) & 2u), f32(i & 2u));
    return Vertex(vec4f(uv * 2. - 1., 0., 1.), vec2f(uv.x, 1. - uv.y));
}
@group(0) @binding(0) var src: texture_2d<f32>;
fn linear(s: vec4f) -> vec4f {
    return vec4f(select(pow((s.rgb + 0.055) / 1.055, vec3f(2.4)), s.rgb / 12.92, s.rgb <= vec3f(0.04045)), s.a);
}
@fragment fn fs(v: Vertex) -> @location(0) vec4f {
    let size = vec2i(textureDimensions(src));
    let pos = v.uv * vec2f(size) - 0.5;
    let p = vec2i(floor(pos));
    let w = fract(pos);
    let last = size - 1;
    let a = linear(textureLoad(src, clamp(p, vec2i(0), last), 0));
    let b = linear(textureLoad(src, clamp(p + vec2i(1,0), vec2i(0), last), 0));
    let c = linear(textureLoad(src, clamp(p + vec2i(0,1), vec2i(0), last), 0));
    let d = linear(textureLoad(src, clamp(p + vec2i(1,1), vec2i(0), last), 0));
    return mix(mix(a,b,w.x), mix(c,d,w.x), w.y);
}
