# Babylon.js Patch Stack

This directory contains the local Babylon.js source patches needed for
NativeWebGPU and NativeXR portal validation in this BabylonNative branch.
The stack is maintained against the Babylon.js `9.17.0` release used by this
branch.

The patches are source-oriented on purpose. Avoid patching generated UMD files
such as `Apps/node_modules/babylonjs/babylon.max.js`; those diffs are large,
hard to review, and hide the real upstreamable changes.

## Included Patches

- `0001-enable-webgpu-layers-for-native-xr.patch`
  - Lets a native runtime use the standard WebXR-WebGPU projection-layer path
    when it exposes `XRGPUBinding`, while retaining the legacy native WebGL
    render-target path. Browser WebGL and WebGPU behavior is unchanged.
- `0002-add-webgpu-render-command-batching.patch`
  - Adds a backend-neutral render command batcher and WebGPU render-pass
    lowering path, with fallback replay and focused unit tests for
    compatibility boundaries.

The KTX compressed-sRGB fix (`BabylonJS/Babylon.js#18538`), SSAO2 world-space
normal fix (`BabylonJS/Babylon.js#18539`), DOM-free font offset fallback
(`BabylonJS/Babylon.js#18463`), and standard WebGPU XR render-target provider
are already part of Babylon.js 9.17.0. IBL-shadow experiments and custom
animation-frame requester plumbing are intentionally not part of this stack.

## Apply And Verify

From the BabylonNative checkout root:

```sh
node Apps/scripts/applyBabylonJsPatchStack.js --check --babylon-js-dir /Users/matt/src/Babylon.js
node Apps/scripts/applyBabylonJsPatchStack.js --apply --babylon-js-dir /Users/matt/src/Babylon.js
```

The default Babylon.js path is the sibling checkout at `../Babylon.js`, so the
`--babylon-js-dir` argument is usually optional in the canonical local layout.

The check command creates a temporary clean worktree from the target checkout's
current `HEAD`, then requires every patch in `series` to either apply cleanly
or be detected as already present by a reverse-apply check. Anything else fails
hard. Run it against a checkout at the documented Babylon.js release. The apply
command performs the same temporary-worktree preflight before mutating the
target checkout, then applies or skips already-present patches in order. It
refuses to apply over unstaged or staged local changes.

After applying, rebuild Babylon.js and copy the generated bundles into
BabylonNative using the flow documented in `AGENTS.md`.
