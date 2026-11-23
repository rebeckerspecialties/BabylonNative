import test from "node:test";
import assert from "node:assert/strict";
import {eyeDemand, ringSegments, ringIndices} from "./stereoRingLod.mjs";

const identity = [1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1];
test("either eye can retain a ring, or demand its finer geometry", () => {
    const left=identity.slice(), right=identity.slice();
    left[12]=2.5;
    for(const [a,b] of [[left,right],[right,left]]) {
        const demand=Math.max(eyeDemand(a,0,0,0,0.5,2048,2000),eyeDemand(b,0,0,0,0.5,2048,2000));
        assert.equal(ringSegments(demand,64,0),64);
    }
    assert.equal(eyeDemand(left,0,0,0,0.5,2048,2000),-1);
    assert.equal(eyeDemand(identity,1.5,0,0,0.5,2048,2000),512);
    assert.equal(ringSegments(-1,32,0),0);
});
test("near intersection stays at full detail and a threshold has hysteresis", () => {
    const near=identity.slice(); near[11]=1; near[15]=0;
    assert.equal(eyeDemand(near,0,0,0.01,0.1,1024,1024),Infinity);
    assert.equal(ringSegments(Infinity,32,0),32);
    assert.equal(ringSegments(9,32,8),8);
    assert.equal(ringSegments(9,32,16),16);
});
test("an eight-pixel guard band delays exits, never entry or a change in geometric demand", () => {
    const demand=(x,wasVisible) => eyeDemand(identity,x,0,0,0.5,2000,2000,wasVisible);
    assert.equal(demand(1.504,false),-1);
    assert.equal(demand(1.504,true),500);
    assert.equal(demand(1.51,true),-1);
    assert.equal(demand(1.499,false),demand(1.499,true));
});
test("decimation preserves winding and the distinct seam vertices of the original UV grid", () => {
    for(const max of [32,64]) for(const n of [8,16,32]) {
        const indices=ringIndices(max,n), step=max/n, stride=max+1;
        assert.equal(indices.length,6*n*n);
        assert.deepEqual([...indices.slice(0,6)],[0,step,step*stride,step,step*(stride+1),step*stride]);
        assert.equal(Math.max(...indices),stride*stride-1);
    }
});
