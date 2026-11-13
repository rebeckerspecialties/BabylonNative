import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
import { resolve } from 'node:path';
import test from 'node:test';

const require = createRequire(import.meta.url);
const bundlePath = process.env.BABYLONJS_BUNDLE
    ? resolve(process.env.BABYLONJS_BUNDLE)
    : require.resolve('babylonjs/babylon.max.js');
const BABYLON = require(bundlePath);
console.log(`Checking published Babylon.js ${BABYLON.Engine.Version}: ${bundlePath}`);

// Reach the compiled stream through the public provider setter, not a source-tree substitute.
function setup(t) {
    t.mock.method(BABYLON.Logger, 'Error', () => {});
    const draws = [];
    const notices = [];
    let hostError = 'original host error';
    const provider = {
        protocolVersion: 1, recordsAtomically: true,
        isEnabled: () => true,
        getResourceId: resource => resource?.id ?? 0,
        getLastError: () => hostError,
        onDisabled(info) {
            assert.equal(this, provider);
            notices.push(info);
        },
    };
    const engine = Object.create(BABYLON.WebGPUEngine.prototype);
    engine._renderPassCommandBatching = null;
    engine.renderPassCommandRecorderProvider = provider;
    const batcher = engine._renderPassCommandBatching.batcher;
    const stream = batcher._lowering;
    const pass = {
        _recordCommands: () => true,
        setPipeline() {}, setIndexBuffer() {}, setVertexBuffer() {}, setBindGroup() {},
        draw(...args) { hostError = 'overwritten by direct draw'; draws.push(args); },
    };
    const command = {
        renderPass: pass, drawKind: 1, pipeline: { id: 1 }, bindGroups: [], vertexBuffers: [],
        indexBuffer: null, indexFormat: null, count: 3, instancesCount: 1, start: 7,
        indirectDrawBuffer: null, indirectDrawByteOffset: 0,
    };
    return { batcher, stream, pass, command, draws, notices, provider };
}

test('legacy providers draw directly without entering an unsafe replay path', t => {
    const { batcher, pass, command, draws, notices, provider } = setup(t);
    delete provider.recordsAtomically;
    pass._recordCommands = () => assert.fail('legacy recorder must not run');
    assert.equal(batcher.submit(command), false);
    assert.deepEqual(draws, [[3, 1, 7, 0]]);
    assert.equal(notices[0].reason, 'atomic-recording-unsupported');
});

test('pending backend objects survive until consumption and are then released', t => {
    const { batcher, stream, pass, command } = setup(t);
    const resources = [command.pipeline, { id: 2 }, { id: 3 }, { id: 4 }, { id: 5 }];
    Object.assign(command, {
        drawKind: 0, indexBuffer: { underlyingResource: resources[1] }, indexFormat: 'uint32',
        vertexBuffers: [{ effectiveBuffer: { underlyingResource: resources[2] }, _validOffsetRange: true }],
        bindGroups: [resources[3]], indirectDrawBuffer: resources[4],
    });
    assert.equal(batcher.submit(command), true);
    assert.deepEqual(stream._pendingResources.slice(0, stream._pendingResourceCount), resources);
    pass._recordCommands = () => {
        assert.deepEqual(stream._pendingResources.slice(0, stream._pendingResourceCount), resources);
        return true;
    };
    assert.equal(batcher.flush(), true);
    assert.equal(stream._pendingResourceCount, 0);
    assert(stream._pendingResources.every(r => r === null));
});

for (const mode of ['rejected', 'throwing', 'falsy-throw', 'missing']) {
    test(`${mode} recorder replays once and reports the failure`, t => {
        const { batcher, stream, pass, command, draws, notices } = setup(t);
        assert.equal(batcher.submit(command), true);
        if (mode === 'missing') delete pass._recordCommands;
        else pass._recordCommands = () => {
            if (mode === 'throwing') throw new Error('decoder failed');
            if (mode === 'falsy-throw') throw false;
            return false;
        };
        assert.equal(batcher.flush(), false);
        assert.deepEqual(draws, [[3, 1, 7, 0]]);
        assert.equal(notices.length, 1);
        assert.equal(notices[0].reason, mode === 'missing' ? 'recorder-missing'
            : mode === 'rejected' ? 'recording-rejected' : 'recording-threw');
        assert.equal(notices[0].replayedDrawCount, 1);
        if (mode !== 'missing') assert.equal(notices[0].hostError, 'original host error');
        assert.equal(stream._pendingResourceCount, 0);
        assert.equal(batcher.submit(command), false);
        batcher.flush();
        assert.equal(draws.length, 2);
        assert.equal(notices.length, 1);
    });
}

test('batches are bounded to 1024 draws', t => {
    const { batcher, pass, command } = setup(t);
    const batches = [];
    pass._recordCommands = (_words, count) => { batches.push(count); return true; };
    for (let i = 0; i < 1025; i++) assert.equal(batcher.submit(command), true);
    batcher.flush();
    assert.deepEqual(batches, [1024, 1]);
});

for (const mode of ['accepted', 'rejected', 'throwing']) {
    test(`snapshot replay follows pending ${mode} draws with unchanged dynamic state`, t => {
        const { batcher, pass, command, draws } = setup(t);
        pass._recordCommands = () => {
            if (mode === 'throwing') throw new Error('decoder failed');
            if (mode === 'rejected') return false;
            draws.push([command.count, command.instancesCount, command.start, 0]);
            return true;
        };
        const proto = BABYLON.WebGPUEngine.prototype;
        const draw = {
            kept: true, generation: 0, renderTarget: null, sampleCount: 1,
            colorFormats: [], depthFormat: undefined, pipeline: {}, bindGroups: [],
            indexBuffer: null, vertexBuffers: [], vertexOffsets: [], indirectBuffer: null,
            indexed: false, count: 3, instances: 1, start: 100,
        };
        let dirty = false;
        let slotUsed = 0;
        const wrapper = {
            drawContext: { _recordedDraw: draw, isDirty: () => dirty },
            materialContext: { updateId: 0, isDirty: false },
            effect: { _pipelineContext: { uniformBuffer: { _markOwnerSlotUsed() { slotUsed++; } } } },
        };
        const engine = {
            _snapshotRendering: { record: true }, snapshotRenderingReuseDraws: true,
            _recordedDrawsGeneration: 0, _currentRenderTarget: null, currentSampleCount: 1,
            _cacheRenderPipeline: { colorFormats: [] },
            _bundleList: { numDrawCalls: 0, getBundleEncoder: () => pass },
            _getCurrentRenderPass() {}, applyStates() {}, _applyRenderPassChanges() {}, _reportDrawCall() {},
            _flushRenderPassCommands: () => batcher.flush(),
            _canEncodeRecordedDraw: w => proto._canEncodeRecordedDraw.call(engine, w),
        };
        assert.equal(batcher.submit(command), true);
        assert.equal(proto._replayRecordedDraw.call(engine, wrapper), true);
        batcher.flush();
        assert.deepEqual(draws, [[3, 1, 7, 0], [3, 1, 100, 0]]);
        assert.equal(slotUsed, 1);
        dirty = true;
        assert.equal(proto._replayRecordedDraw.call(engine, wrapper), false);
        assert.equal(draws.length, 2);
        const recordsDraws = Object.getOwnPropertyDescriptor(proto, '_recordsDraws').get;
        assert.equal(recordsDraws.call(engine), true);
        engine.snapshotRenderingReuseDraws = false;
        assert.equal(recordsDraws.call(engine), false);
    });
}
