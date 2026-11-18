import assert from "node:assert/strict";
import { createRequire } from "node:module";
import { resolve } from "node:path";
import test from "node:test";

const require = createRequire(import.meta.url);
if (
  !process.env.BABYLON_CONTROL_BUNDLE ||
  !process.env.BABYLON_CANDIDATE_BUNDLE
)
  throw Error(
    "Set BABYLON_CONTROL_BUNDLE and BABYLON_CANDIDATE_BUNDLE to distinct, verified bundles.",
  );
const controlPath = resolve(process.env.BABYLON_CONTROL_BUNDLE);
const candidatePath = resolve(process.env.BABYLON_CANDIDATE_BUNDLE);
assert.notEqual(
  controlPath,
  candidatePath,
  "A differential test must not compare the bundle with itself.",
);
const control = require(controlPath);
const candidate = require(candidatePath);

function run(B, { dynamic, hardCheck, repeats, override = false }) {
  const buffers = [],
    uploads = [],
    draws = [];
  const engine = {
    supportsUniformBuffers: true,
    frameId: 1,
    _uniformBuffers: [],
    _features: {
      trackUbosInFrame: true,
      checkUbosContentBeforeUpload: true,
      uniformBufferHardCheckMatrix: hardCheck,
    },
    createUniformBuffer(data) {
      const buffer = { uniqueId: buffers.length, gpu: data.slice() };
      buffers.push(buffer);
      return buffer;
    },
    createDynamicUniformBuffer(data) {
      return this.createUniformBuffer(data);
    },
    updateUniformBuffer(buffer, data) {
      buffer.gpu = data.slice();
      uploads.push([buffer.uniqueId, ...data]);
    },
    _releaseBuffer() {
      return true;
    },
  };
  const material = new B.UniformBuffer(engine, undefined, dynamic);
  material.addUniform("color", 4);
  material.create();
  const view = new B.UniformBuffer(engine);
  view.addUniform("viewProjection", 16);
  view.create();
  const matrices = [
    B.Matrix.Translation(1, 0, 0),
    B.Matrix.Translation(2, 0, 0),
  ];
  let overrides = 0;
  if (override) {
    const update = material.updateUniform;
    material.updateUniform = function (...args) {
      overrides++;
      return Reflect.apply(update, this, args);
    };
  }
  for (let frame = 1; frame <= 3; frame++) {
    engine.frameId = frame;
    for (const [eye, color] of [
      [0, 1],
      [0, 1],
      [1, 2],
      [0, 3],
      [1, 1],
    ]) {
      engine._features.uniformBufferHardCheckMatrix =
        !engine._features.uniformBufferHardCheckMatrix;
      view.updateMatrix("viewProjection", matrices[eye]);
      view.update();
      material.updateFloat4("color", color, 0.5, 0.25, 1);
      material.update();
      for (let i = 0; i < repeats; i++) {
        material.updateFloat4("color", color, 0.5, 0.25, 1);
        material.update();
      }
      draws.push(
        [material.getBuffer(), view.getBuffer()].map((buffer) => [
          buffer.uniqueId,
          ...buffer.gpu,
        ]),
      );
    }
  }
  return {
    buffers: buffers.map((b) => [b.uniqueId, ...b.gpu]),
    uploads,
    overrides,
    draws,
  };
}

for (const dynamic of [false, true])
  for (const hardCheck of [false, true])
    for (const repeats of [0, 1, 40]) {
      test(`emitted bundle same-frame/multiple-view parity: dynamic=${dynamic} hardCheck=${hardCheck} repeats=${repeats}`, () => {
        const actual = run(candidate, { dynamic, hardCheck, repeats });
        assert.deepEqual(actual, run(control, { dynamic, hardCheck, repeats }));
        assert.deepEqual(
          actual.draws.map(([material, view]) => [material[1], view[13]]),
          Array.from({ length: 3 }, () => [
            [1, 1],
            [1, 1],
            [2, 2],
            [3, 1],
            [1, 2],
          ]).flat(),
        );
      });
    }
test("emitted bundle retains updateUniform overrides", () => {
  const settings = {
    dynamic: false,
    hardCheck: true,
    repeats: 2,
    override: true,
  };
  assert.deepEqual(run(candidate, settings), run(control, settings));
});

function materialFlags(B) {
  const engine = new B.NullEngine();
  const scene = new B.Scene(engine);
  const material = new B.PBRMaterial("state-check", scene);
  const reflection = new B.RenderTargetTexture("reflection", 16, scene);
  const cameras = [
    new B.FreeCamera("left", B.Vector3.Zero(), scene),
    new B.FreeCamera("right", B.Vector3.Zero(), scene),
  ];
  material.reflectionTexture = reflection;
  const old = B.MaterialFlags.ReflectionTextureEnabled;
  try {
    const results = [];
    for (const [eye, enabled] of [
      [0, true],
      [1, false],
      [0, false],
      [0, true],
      [1, true],
    ]) {
      scene.activeCamera = cameras[eye];
      B.MaterialFlags.ReflectionTextureEnabled = enabled;
      results.push(material.hasRenderTargetTextures);
    }
    return results;
  } finally {
    B.MaterialFlags.ReflectionTextureEnabled = old;
    scene.dispose();
    engine.dispose();
  }
}

test("emitted PBR material observes static flag changes between same-frame views", () => {
  assert.deepEqual(materialFlags(control), [true, false, false, true, true]);
  assert.deepEqual(materialFlags(candidate), [true, false, false, true, true]);
});
