// Distances and planes are expressed in the source mesh's local coordinates.
export function eyeDemand(m, x, y, z, r, width, height) {
    const cx = m[0]*x + m[4]*y + m[8]*z + m[12];
    const cy = m[1]*x + m[5]*y + m[9]*z + m[13];
    const cz = m[2]*x + m[6]*y + m[10]*z + m[14];
    const w = m[3]*x + m[7]*y + m[11]*z + m[15];
    for (let axis = 0; axis < 3; ++axis) {
        const c = axis === 0 ? cx : axis === 1 ? cy : cz;
        for (let sign = -1; sign <= 1; sign += 2) {
            // A symmetric clip depth accepts both the WebGL and WebGPU near plane conservatively.
            const outside = -(w + sign * c);
            if (outside <= 0) continue;
            const a = m[3]+sign*m[axis], b=m[7]+sign*m[4+axis], d=m[11]+sign*m[8+axis];
            if (outside*outside > r*r*(a*a+b*b+d*d)) return -1;
        }
    }
    const dw = r * Math.hypot(m[3], m[7], m[11]);
    if (!(w > dw)) return Infinity;
    const rx = r * Math.hypot(m[0], m[4], m[8]) + Math.abs(cx/w)*dw;
    const ry = r * Math.hypot(m[1], m[5], m[9]) + Math.abs(cy/w)*dw;
    return Math.max(rx*width, ry*height) / (2*(w-dw));
}

export function ringSegments(radiusPixels, maximum, previous) {
    if (radiusPixels < 0) return 0;
    if (!Number.isFinite(radiusPixels)) return maximum;
    // Bound the projected sagitta of a chord. A dead band avoids resubmission on small pose changes.
    for (let n = 8; n < maximum; n *= 2) {
        const error = radiusPixels * (1-Math.cos(Math.PI/n));
        if (error <= (previous && previous <= n ? 0.85 : 0.55)) return n;
    }
    return maximum;
}

export function ringIndices(maximum, segments) {
    if (!Number.isInteger(maximum/segments) || segments < 3) throw Error("Incompatible torus grid");
    const result = new Uint16Array(segments*segments*6);
    const stride=maximum+1, step=maximum/segments;
    for (let i=0, k=0; i<maximum; i+=step) {
        for (let j=0; j<maximum; j+=step) {
            const a=i*stride+j, b=a+step, c=a+step*stride, d=c+step;
            result[k++]=a; result[k++]=b; result[k++]=c;
            result[k++]=b; result[k++]=d; result[k++]=c;
        }
    }
    return result;
}
