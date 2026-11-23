#version 450
layout(set=0, binding=0) uniform sampler2D eye;
layout(location=0) out vec4 color;
void main() {
    color = texture(eye, gl_FragCoord.xy / vec2(1216.0, 1344.0));
}
