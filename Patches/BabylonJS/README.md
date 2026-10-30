# Babylon.js Patch Stack

This directory contains the local Babylon.js source patches needed for
NativeWebGPU and NativeXR portal validation in this BabylonNative branch.
The stack is maintained against the Babylon.js `9.33.0` release used by this
branch.

The patches are source-oriented on purpose. Avoid patching generated UMD files
such as `Apps/node_modules/babylonjs/babylon.max.js`; those diffs are large,
hard to review, and hide the real upstreamable changes.

## Included Patches

- `0002-webgpu-render-command-stream-contract.patch`
  - BabylonJS/Babylon.js#19076 and #19084 on top of 9.33.0 (which already ships
    the render-pass command stream of #19075): direct draws stay on the default
    path when no recorder provider is installed, and the stream retains the
    objects referenced by pending command words, replays a rejected batch
    through direct encoder calls, reports the fallback through
    `provider.onDisabled`, and bounds batches to 1024 draws. Drop this patch
    once a Babylon.js release contains both PRs.

The native WebXR-WebGPU projection-layer enablement (formerly `0001-enable-webgpu-layers-for-native-xr.patch`) landed upstream in the `[WebGPU-XR]` series (`BabylonJS/Babylon.js#18711`, `#18773`, `#18779`) and is part of 9.22.1, so it is no longer carried here. The KTX compressed-sRGB fix (`BabylonJS/Babylon.js#18538`), SSAO2 world-space
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
