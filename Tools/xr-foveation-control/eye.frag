#version 450
#ifndef STEPS
#define STEPS 20
#endif
layout(location=0) out vec4 color;
void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    float x = dot(gl_FragCoord.xy, vec2(0.018, 0.031));
    for (int i = 0; i < STEPS; ++i) x = fract(sin(x + float(i)) * 155.17);
    color = vec4(float((p.x ^ p.y) & 1), x, float(p.x & 1), 1);
}
