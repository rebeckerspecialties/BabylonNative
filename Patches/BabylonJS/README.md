# Babylon.js Patch Stack

This directory contains the local Babylon.js source patches needed for
NativeWebGPU and NativeXR portal validation in this BabylonNative branch.

The patches are source-oriented on purpose. Avoid patching generated UMD files
such as `Apps/node_modules/babylonjs/babylon.max.js`; those diffs are large,
hard to review, and hide the real upstreamable changes.

## Included Patches

- `0001-add-dom-free-font-offset-fallback.patch`
  - Adds the DOM-free font offset fallback from
    `BabylonJS/Babylon.js#18463` so GUI text sizing does not collapse when
    DOM layout metrics are unavailable or return zero.
- `0002-preserve-ktx-compressed-srgb-metadata.patch`
  - Preserves compressed KTX sRGB/gamma metadata so native compressed texture
    uploads keep the intended color space.
- `0003-support-ssao2-world-space-normals.patch`
  - Lets SSAO2 consume world-space normals by transforming them into view space
    in the shader.
- `0004-wrap-native-xr-webgpu-render-targets.patch`
  - Wraps NativeXR WebGPU color/depth textures as Babylon.js render targets.
- `0005-batch-native-webgpu-render-pass-command-streams.patch`
  - Adds the guarded NativeWebGPU render-pass command stream used by the
    Babylon.js fork PR.

IBL-shadow experiments and custom animation-frame requester plumbing are
intentionally not part of this stack.

## Apply And Verify

From `Apps/`:

```sh
npm run patch:babylonjs:check -- --babylon-js-dir /Users/matt/src/Babylon.js
npm run patch:babylonjs:apply -- --babylon-js-dir /Users/matt/src/Babylon.js
```

The default Babylon.js path is the sibling checkout at `../Babylon.js`, so the
`--babylon-js-dir` argument is usually optional in the canonical local layout.

The check command creates a temporary clean worktree from the target checkout's
current `HEAD`, applies every patch in `series`, and fails if any patch no
longer aligns. The apply command performs the same temporary-worktree preflight
before mutating the target checkout, then applies the patches in order. It
refuses to apply over unstaged or staged local changes.

After applying, rebuild Babylon.js and copy the generated bundles into
BabylonNative using the flow documented in `AGENTS.md`.
