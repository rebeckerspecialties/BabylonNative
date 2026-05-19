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

## Native Performance Profiling Notes

For BabylonNative performance work, use the optimized Release/LTO builds, not
ASan/UBSan builds. The current local build directories used for this are:

```sh
PATH=/Users/matt/.rustup/toolchains/1.94.1-aarch64-apple-darwin/bin:$PATH cmake --build build_wgpu_29_release_lto --target Playground --parallel 10
PATH=/Users/matt/.rustup/toolchains/1.94.1-aarch64-apple-darwin/bin:$PATH cmake --build build_wgpu_29_ios_release_lto --target Playground --config Release --parallel 10
```

For iOS device profiling, keep using the existing Rebecker Specialties signing
setup. The observed Playground bundle id is
`com.rebeckerspecialties.BabylonNative.Playground`, signed with
`Apple Development: Matthew Donovan Hargett (79G4CD2XGC)` and the local
`iOS Team Provisioning Profile: *` profile.

The current macOS and iOS Release/LTO builds use the public operating-system
JavaScriptCore framework, not V8/JSI. Check
`NAPI_JAVASCRIPT_ENGINE:STRING=JavaScriptCore` in the active `CMakeCache.txt`
and `otool -L` on the built Playground binary if this needs to be revalidated.

The `Fluid rendering particle system` screenshot test is native catalog index
`348`. It is excluded from automatic runs, so force it explicitly:

```sh
xcrun devicectl device process launch --device <COREDEVICE_ID> --terminate-existing --console com.rebeckerspecialties.BabylonNative.Playground --test-index 348 --include-excluded --once --save-results true
```

Do not insert a standalone `--` before the Playground flags in that
`devicectl` command. The Playground parser treats `--` as end-of-options and
then interprets later arguments as script URLs, which fails with
`URL does not have a valid scheme`.

Useful local device ids from the profiling pass:

- iPhone 12 CoreDevice: `B5D4CA48-8949-525C-8E5D-4F661161BD9D`; xctrace UDID:
  `00008101-000A044A3C28801E`
- iPhone XS CoreDevice: `C7A1D5AD-C89D-5E03-99B9-5E65EEC30486`; xctrace UDID:
  `00008020-001C292A2190003A`

iPhone 12 can provide low-level CPU counter / PMU data through xctrace. iPhone
XS cannot provide those low-level PMU counters; use Time Profiler,
per-process CPU load, core behavior, run logs, and Metal traces instead. If
`devicectl` can launch the XS but `xcrun xctrace list devices` reports it
under `Devices Offline`, do not use the resulting failed trace as performance
evidence.

The first committed native-stack optimization sequence for this workload is:

1. Batch WebGPU queue submit command buffers.
2. Stage WebGPU buffer writes through a reusable staging belt.
3. Reuse staged WebGPU buffer write storage to avoid per-frame `Vec` allocation
   churn in the buffer write path.

Use `BABYLON_NATIVE_WEBGPU_PROFILE_TRACE=1` for targeted native upload/flush
timing. It logs WebGPU staged-buffer flushes plus `writeTexture` and
`copyExternalImageToTexture` CPU phase timings without changing default runs.
On the fluid test, that trace showed external-image CPU upload work was
sub-millisecond on both M4 Max and iPhone 12; the long startup gap correlated
with `wgpu`/Metal internal `PendingWrites` transfer lifetimes and synchronous
scene setup, not native row-copy CPU time.

Do not treat the validation log line `First pixel off...` as TTMFR. It is the
screenshot comparison point after the configured `renderCount` / readiness
pump, so it includes validation wait frames and readback/comparison work. In a
fresh iPhone 12 Metal trace, steady displayed frames were still about 59.8 fps,
while the visible long intervals were startup placeholder presentation, one
post-startup missed vsync during upload/mipmap work, and validation shutdown.
The corresponding Time Profiler samples were dominated by one JavaScriptCore
LLInt thread during scene setup; within the native stack, the actionable part
was the smaller upload/mipmap burst after setup, not the whole multi-second
startup hold.

Startup overlap experiment: moving WGPU bootstrap to a background future and
putting a ScriptLoader-ordered readiness barrier after the core Babylon.js
bundles was functionally safe, but on M4 Max and iPhone 12 the `WGPU
initialized` log still arrived about 0.13-0.32s before the Babylon.js engine
log. Direct fluid runs stayed essentially flat, so current TTMFR is not blocked
by serialized WGPU creation before Babylon.js parsing on these devices.

When `xctrace record` prints `Waiting for device to boot` for the iPhone 12
even though `devicectl` can launch it, a later `devicectl` launch may kick the
recording into `Starting recording...`. If that happens after app launch, use
the trace only for steady-state/render-path evidence, not TTMFR. CPU Counters
traces are only useful as PMU evidence when the exported trace contains the
dynamic `MetricAggregationForProcess` / `CounterMetric...` tables; a trace that
only exports generic `time-profile` / `time-sample` tables should not be
reported as low-level counter data. Use absolute `.trace` output paths for CPU
Counters; a relative output path failed to save with `trace_status=17`. In the
latest valid iPhone 12 CPU Counters capture, `MetricAggregationForProcess` and
`CoreTypeByThread` exported successfully, but xctrace still began recording
after WGPU/Babylon.js startup logs, so it is steady-render PMU evidence only.
Attempts to wait for `Starting recording...` before launching, or to use
`xctrace --launch` with the iOS bundle id, produced empty/error traces on this
device/toolchain combination.

On macOS, `xctrace record --launch` may fail against the local Playground bundle
because the CMake-generated `Info.plist` still contains unresolved
`$(EXECUTABLE_NAME)` placeholders. For local M4 traces, start an all-processes
recording first and then run
`build_wgpu_29_release_lto/Apps/Playground/Playground.app/Contents/MacOS/Playground`
directly with the test flags. Keep host Metal traces short and targeted:
an 18s all-process Metal trace generated a 5.3 GB in-progress package and spent
minutes in `DVTInstrumentsAnalysisCore` post-processing after the app had
finished. A target-scoped attach recorded the app but `xctrace` exited with
`Trace/BPT trap: 5` and exported a malformed trace, so preserve the app run log
and do not report the trace as valid unless schema export succeeds.

For 120 Hz / HDR checks on the local M4 Mac, do not treat the
`MTKView preferredFramesPerSecond=120` log as proof that the fluid simulation is
updating at 120 fps. It only proves the requested/native display cap. Use
`--profile-frames --preferred-fps 120 --hdr10` and compare three signals:

- macOS native frame logs from `drawInMTKView`, which should settle near 120 Hz
  after startup if the display path is actually ticking at that cadence.
- Validation JS frame logs, which report Babylon render-loop cadence and native
  WebGPU deltas.
- Particle profile fields: for the HDR fluid test, `particleUpdateCalls=30`
  per 30-frame window proves the GPU particle update path is running every
  validation frame; `particleUpdateInAnimate=true` confirms the fluid renderer
  has moved GPU particle updates into `animate()`.

The current M4 Max HDR fluid validation run requested/preferred/maxed at
`120` and measured native windows around `120 fps` after warmup, while JS render
windows ranged roughly `109-120 fps`. The validation harness intentionally sets
`useConstantAnimationDeltaTime`, so this run reported `animationRatio=0.9600`
and `particleTimeDelta=0.0192`; perceived motion speed in validation is
therefore not the same evidence as wall-clock display cadence. Screenshot
readback/comparison is also not live cadence evidence: the native frame window
dropped to about `73 fps` during the screenshot phase.

Focused local M4 HDR fluid cadence check:

```sh
pkill -x Playground || true
BABYLON_NATIVE_WEBGPU_MEMORY_TRACE=1 build_wgpu_29_release_lto/Apps/Playground/Playground.app/Contents/MacOS/Playground --test "Fluid rendering particle system HDR10" --include-excluded --once --save-results false --hdr10 --profile-frames --preferred-fps 120 --inspection-hold-ms 0
```

Performance lessons from rejected experiments:

- Staging texture writes in this layer regressed PMU/submit behavior. `wgpu`
  already stages and copies for `Queue::write_texture`, so the extra native
  staging added row repacking, owned data copies, and ordering flushes.
- Immediate staging-belt allocation in the buffer write call path regressed CPU
  cycles. Keeping writes cheap and encoding them later was better than moving
  mapped staging allocation into the hot call path.
- Reducing `device.poll(Poll)` improved some first-render timings but worsened
  PMU counters. Treat that poll as pacing/progress work, not pure overhead.
- Command-vector pooling should not scan pools in the hot path. A `.pop()` pool
  variant improved some iPhone 12 Metal metrics, but PMU was mixed and XS
  trace validation was incomplete, so do not commit that idea without a fresh
  per-device retest.

## Error Handling Expectations

- NativeWebGPU does not ship glslang/twgsl. If Babylon.js code asks WebGPU to compile GLSL, surface an actionable JS error that names the shader/effect and says the path needs WGSL or an explicit NativeWebGPU exclusion.
- Async shader preparation rejection must propagate into Babylon.js effect compilation errors with a useful stack. Do not let readiness pumps turn those into anonymous timeouts.
- Keep screenshot validation failures diagnostic-first: capture the scene readiness state, native backend stats, and saved result/error images instead of adding sleeps or broad thresholds.
