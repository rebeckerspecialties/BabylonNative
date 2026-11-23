import assert from "node:assert/strict";
import test from "node:test";
import { ringReplay } from "./stereoRingReplay.mjs";

test("paused rings retain attributes across culling, including shrink and re-entry", () => {
    const identity = [1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1];
    const view = identity.slice();
    const world = {
        clone: () => ({m: identity.slice()}),
        multiplyToRef: (other, target) => {target.m = other;},
    };
    const target = {getRenderWidth: () => 2048, getRenderHeight: () => 2263};
    const eye = {outputRenderTarget: target, getTransformationMatrix: () => view};
    const writes = [];
    let tick;
    const scene = {
        activeCamera: {rigCameras: [eye, eye]},
        onBeforeRenderObservable: {add: f => {tick = f;}, remove: () => {}},
        getMeshByName(name) {
            const max = name.startsWith("skill") ? 32 : 64;
            const matrixData = new Float32Array([...identity, ...identity, ...identity]);
            matrixData[12] = -1; matrixData[28] = 0; matrixData[44] = 1;
            return {
                thinInstanceCount: 3, layerMask: 7, getTotalVertices: () => (max+1)**2,
                getWorldMatrix: () => world, computeWorldMatrix: () => world,
                _thinInstanceDataStorage: {matrixData},
                _userThinInstanceBuffersStorage: {data: {value: Float32Array.of(11,22,33)}, strides: {value:1}},
                clone: () => ({
                    setEnabled() {}, makeGeometryUnique() {}, setIndices() {}, thinInstanceSetBuffer() {}, dispose() {},
                    thinInstancePartialBufferUpdate(key, data, offset) {
                        if (key === "value") writes.push({data:[...data], offset});
                    },
                }),
            };
        },
    };
    const replay=ringReplay(scene);
    replay.setMode("cull");
    tick(); tick();
    assert.deepEqual(writes.splice(0), Array(2).fill({data:[11,22,33],offset:0}));
    view[12]=1; // Last instance leaves. Only draw count changes.
    tick(); assert.equal(writes.length,0);
    view[12]=-1; // First instance leaves. Preserve both remaining attributes in their new slots.
    tick();
    assert.deepEqual(writes.splice(0), Array(2).fill({data:[22,33],offset:0}));
    view[12]=0;
    tick(); tick();
    assert.deepEqual(writes, Array(2).fill({data:[11,22],offset:0}));
    replay.dispose();
});
