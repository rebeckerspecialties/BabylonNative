# BabylonNative wgpu Branch Notes

This branch is a durable WebGPU/wgpu-native fork, not an upstream bgfx replacement. Keep changes mergeable with upstream BabylonNative and Babylon.js 9.x, and avoid adding JS-visible nonstandard hooks unless they are hidden behind browser-shaped APIs or validation-only internals.

## Local Workspace

- Canonical BabylonNative checkout: `/Users/matt/src/BabylonNative` on branch `wgpu`.
- Babylon.js fork checkout used for staged upstream fixes: `/Users/matt/src/Babylon.js` on branch `codex/webgpu-wgsl-gui3d-particles`.
- Prefer the canonical checkouts above. Codex worktrees under `/Users/matt/.codex/worktrees/...` may be detached and stale.
- Use Rust through rustup, especially `/Users/matt/.rustup/toolchains/1.94.1-aarch64-apple-darwin/bin`, so Rust LLVM stays aligned with the Xcode 26.5/LTO work.
- Do not commit build outputs or generated dependency trees. Build artifacts should stay under top-level build directories.

## Babylon.js Bundle Flow

When Babylon.js source changes are needed for WebGPU correctness:

1. Patch `/Users/matt/src/Babylon.js`.
2. Run the relevant Babylon.js builds, usually:
   - `npx nx build babylonjs --outputStyle=static`
   - `npx nx build babylonjs-gui --outputStyle=static` when GUI package code changed
3. Copy rebuilt UMD bundles into BabylonNative before local validation:
   - `packages/public/umd/babylonjs/babylon.max.js` to `Apps/node_modules/babylonjs/babylon.max.js`
   - `packages/public/umd/babylonjs/babylon.js` to `Apps/node_modules/babylonjs/babylon.js`
   - `packages/public/umd/babylonjs-gui/babylon.gui.js` to `Apps/node_modules/babylonjs-gui/babylon.gui.js` when GUI changed

Those bundle files are local validation inputs and may be ignored by git; the durable fixes should live in the Babylon.js branch/PR.

## Current Screenshot Test State

- GUI3D now compiles through real Babylon.js WGSL paths, not the old validation shim:
  - `26` GUI3D SpherePanel
  - `177` GUI Near Menu
- `176` GUI Slate is still not visually accepted. The native screenshot diff is
  concentrated in the slate header/top-right controls: `377` pixels
  (`0.157%`) differ at threshold `25`, and `renderCount=120/240` produced the
  same result. Keep `errorRatio: 0.1` so this remains an automated failure
  instead of a manual-inspection miss.
- Browser visual tests now imported into the native catalog:
  - `Texture Repetition - Standard Material` currently passes natively with
    `2` differing pixels.
  - `Texture Repetition - PBR Material` currently has a real native mismatch
    (`7,808` pixels, about `3.25%`), so its native `errorRatio` is tightened to
    `1.0` to keep it failing until root-caused.
- `Lighting Volume` was force-run with NativeWebGPU and only differed by
  `22` pixels; do not tighten it without new visual evidence of missing
  semantic content.
- Refraction and material-plugin coverage now has useful NativeWebGPU signal:
  - `131` Simple refraction
  - `249` PBR refraction
  - `314` Refraction local cube map STD
  - `315` Refraction local cube map PBR
  - `376` MeshDebugPluginMaterial
- GLSL-only samples should stay explicitly excluded from NativeWebGPU unless they gain WGSL implementations:
  - `337` Material Plugin uses GLSL-only custom MaterialPlugin code.
  - `656` Particles - Effects uses custom GLSL `createEffectForParticles`.
- Particle ramp-gradient tests are passing:
  - `649` Particles - Ramp Gradient
  - `650` Particles - Ramp Gradient Remap
  - `651` Particles - Ramp Gradient Remap Alpha
- GUI gradient tests are not resolved. Forced NativeWebGPU runs still fail with large diffs:
  - `354` GUI Gradient Linear: about 22k differing pixels
  - `355` GUI Gradient Radial: about 60k differing pixels
  - `356` GUI Gradient Linear with transparency: about 50k differing pixels

The GUI gradient failures look like CanvasWgpu/browser-Canvas semantics work, not a screenshot threshold problem: colors/styles are visibly wrong in the saved result images.

## Validation Commands

Build Playground and UnitTests with the pinned Rust toolchain first:

```sh
PATH=/Users/matt/.rustup/toolchains/1.94.1-aarch64-apple-darwin/bin:$PATH cmake --build build_wgpu_29_asan_ubsan --target Playground UnitTests --parallel 10
```

Focused screenshot smoke after Babylon.js/WebGPU material changes:

```sh
./build_wgpu_29_asan_ubsan/Apps/Playground/Playground.app/Contents/MacOS/Playground --test-index 26,176,177,131,249,314,315,337,376,656 --save-results true
```

Failure-driving checks for the current visual TODOs:

```sh
./build_wgpu_29_asan_ubsan/Apps/Playground/Playground.app/Contents/MacOS/Playground --test-index 176 --once --save-results true
./build_wgpu_29_asan_ubsan/Apps/Playground/Playground.app/Contents/MacOS/Playground --test "Texture Repetition - PBR Material" --once --save-results true
```

JavaScript unit sweep:

```sh
./build_wgpu_29_asan_ubsan/Apps/UnitTests/UnitTests '--gtest_filter=JavaScript.*'
PATH=/Users/matt/.rustup/toolchains/1.94.1-aarch64-apple-darwin/bin:$PATH cmake --build build_wgpu_29_tsan --target UnitTests --parallel 10
PATH=/Users/matt/.rustup/toolchains/1.94.1-aarch64-apple-darwin/bin:$PATH ./build_wgpu_29_tsan/Apps/UnitTests/UnitTests '--gtest_filter=JavaScript.*'
```

Before starting a new visible Playground run, make sure no previous Playground instance is still running.

## Error Handling Expectations

- NativeWebGPU does not ship glslang/twgsl. If Babylon.js code asks WebGPU to compile GLSL, surface an actionable JS error that names the shader/effect and says the path needs WGSL or an explicit NativeWebGPU exclusion.
- Async shader preparation rejection must propagate into Babylon.js effect compilation errors with a useful stack. Do not let readiness pumps turn those into anonymous timeouts.
- Keep screenshot validation failures diagnostic-first: capture the scene readiness state, native backend stats, and saved result/error images instead of adding sleeps or broad thresholds.
