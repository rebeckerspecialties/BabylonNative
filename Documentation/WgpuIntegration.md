# wgpu Monorepo Integration

Integration checkpoint: November 11, 2026. The canonical branch is `wgpu`.
The pre-integration tip is preserved as `backup/wgpu-before-reconcile-20261111`.

## Dependency Baseline

| Component | Pin |
| --- | --- |
| BabylonNative upstream master | `2a8cad62` |
| Babylon.js packages | `9.35.0`, exact versions in Apps/package.json |
| wgpu/core/hal/types/Naga | `30.0.1`, `40f4a34ebaf56f9a046231f54125ad046239d3f3` |
| wgpu-native parity runtime | `8322fe1a94ad0193c3393d03fd2a2f3c565e320b` |
| wgpu-native upstream included by parity runtime | `589e6caa4e9fa5596f601e82ca5d3937a1afc829` |

Upstream wgpu-native still uses wgpu 29.0.1. The published parity runtime supplies
the 30.x support and proposed runtime fixes; the local compatibility patch aligns
it with released 30.0.1. Cargo.lock resolves one copy of each wgpu crate.
The large runtime projection patch and released Babylon.js patches are retired.

Use the ES2015+ UMD packages directly. The upstream switch to ES5 bundles is
intentionally not used by the fork: see AGENTS.md for the texture-layout defect.

## Recovered Work

- `wgpu-monorepo-merge`, `wgpu-integration-20261110` and the recorder-provider
  work were already ancestors of the starting tip.
- `native-xr-ownership-sync` and `apple-sdk-build-consistency-sync` are merged.
- `urllib-http3-opt-in` is merged, retaining streaming Fetch from the fork and
  the branch's HTTP, redirect, header, XHR event and loopback-test improvements.
- PR branches for deferred V8 finalizers, Apple async WebAssembly progress,
  NativeEngine texture deletion, sRGB readback and framebuffer scissor state
  are merged. Their upstream review branches remain available independently.
- The CanvasWgpu gradient retention fix is recovered from `f81140d8`.
- Runtime scheduler lifetime safety is ported from JsRuntimeHost `290bfa5` and
  `0a5d570`, with WebGPU completion routing from `d5ec0bf1`.
- The Node-API v7 implementation and conformance suite were already replayed
  into the monorepo in `dcb2e355`; they remain enabled, not a parallel dependency.
- Browser Streams/Compression startup and its integration test were already
  present in the starting branch, despite different commit ancestry.

The old per-callback JSC CFRunLoop pump must not coexist with the Apple dispatch
loop's CFRunLoop source. That combination reentered the dispatcher and crashed
on moved callbacks. The outer loop now owns the pump, and termination wakes it.
Regression tests cover queued callbacks, async WebAssembly, idle shutdown and
scheduler calls during/after runtime teardown.

Standalone JsRuntimeHost has uncommitted Android JSC loading experiments. Those
are preserved in that checkout; they are not required for Apple deployment.
NativeDawn development remains an independent renderer contribution branch.
Historical backup and review branches are not extra sources for normal builds.
The clean `bn-base`, `bn-1891` and `bn-urllib` comparison worktrees were removed
after verifying their commits were integrated or superseded. Their branches
remain. The dirty `bn-test` worktree is retained: its benchmark app, network
metrics logging and experimental NativeEngine deferred texture retirement are
not production fixes and have not been folded into wgpu.

Native presentation now treats each canvas backing size independently of the
host drawable. The old coupling could encode a 2560-pixel copy into a 1280-pixel
drawable and invalidate the frame. Equal-size compatible targets retain the
copy path; other targets use wgpu's texture blitter, including format conversion.
Host resizes preserve canvas resources, and screenshot dimensions still match
the native readback callback contract.

## Validation

Verified against the integration on M4 Metal:

- Nine Node harness/dependency-patch tests pass.
- 48 runtime tests pass, including 420 JavaScript/polyfill assertions and the
  recovered scheduler tests.
- The BabylonNative test executable passes 12 selected tests, including 49
  JavaScript WebGPU cases and seven image-tracking ownership tests.
- 18 native WebGPU async/C API tests pass, including a GPU buffer round trip.
- Two explicit Rust GPU regression tests pass for independent canvas sizes,
  host resizes, full-image scaling and RGBA/BGRA presentation. They require a
  native adapter and are opt-in for CPU-only test environments.
- N-API conformance: 16 pass, one existing version-gated reference test skipped.
- GUI3D SpherePanel, GUI Slate, Simple refraction, PBR refraction, and texture
  repetition Standard/PBR screenshot checks pass at their existing thresholds.

The PBR repetition reference is the browser-validated image from `8aafdc33`,
retained across an upstream add/add conflict. It is not a new native-generated
reference. Its unchanged 1% threshold passes at 0.317% difference on M4.

The iOS Release app builds and is signed by Rebecker Specialties Inc., using
Apple Development: Matthew Donovan Hargett (79G4CD2XGC), team 4GWK4YU2PW and
bundle identifier com.rebeckerspecialties.BabylonNative.Playground.

Physical AR acceptance remains separate: camera authorization and a user-held
phone aimed into a scene are required before plane placement, compositor
correctness and sustained post-placement 50+ fps can be accepted. A camera
permission dialog was observed in a CoreDevice display screenshot during this
checkpoint; pre-placement/framebuffer data does not establish that acceptance.

The non-XR full Hill Valley stress run on M4 rendered its materials without
presentation errors and reported about 25-27 fps with SSAO (roughly 227 draws
and 10 passes per frame). Its material audit reported no missing backdrop,
emissive, reflective-car or lamp overrides. This is a stress-camera result,
not physical AR acceptance or proof of a restored 50+ fps target on XS.

A build-local SnapshotRenderingHelper experiment reported 60 fps but replayed
an SSAO geometry-buffer pipeline into an RGBA8 pass, producing validation
errors. Supplying an explicit geometry-buffer render list did not fix it.
That experiment is not shipped and its frame rate is not a valid improvement.
Snapshot/pass ordering with AO needs a focused reproducer before adoption.

The production portal now defers SSAO allocation and passes until placement;
post-placement AO quality settings remain unchanged. Zero hit-test results
while the camera authorization dialog is present are not a plane-detection
regression. No sustained post-placement XS frame-rate claim is made here.

## Repeatable Local Commands

Use the configured build caches, retaining their platform/toolchain settings:

```sh
export PATH=/Users/matt/.rustup/toolchains/1.94.1-aarch64-apple-darwin/bin:$PATH
cmake --build build_int_macos --target Playground UnitTests JsRuntimeHostUnitTests NativeWebGPUAsyncTests --parallel 8
cmake --build build_int_ios --target Playground --config Release --parallel 8 -- 'CODE_SIGN_IDENTITY=Apple Development' PRODUCT_BUNDLE_IDENTIFIER=com.rebeckerspecialties.BabylonNative.Playground
node --test Patches/testApplyPatch.mjs Apps/scripts/testReadinessFrameBoundaries.mjs Apps/scripts/testValidationSceneLifetime.mjs
node Apps/scripts/applyBabylonJsPatchStack.js --check
```

Use BABYLON_NATIVE_BUILD_WEBGPU_TESTS=ON for the macOS GPU integration suite;
it needs the readback hooks. Run JsRuntimeHostUnitTests from its build output
directory so its relative Worker fixtures resolve. Its filter is GTEST_FILTER.
Select screenshot cases by repeated --test flags. --once stops after the first
matching case, rather than completing the selected set.

```sh
xcrun devicectl device install app --device 00008020-001C292A2190003A build_int_ios/Apps/Playground/Release-iphoneos/Playground.app
xcrun devicectl device process launch --device 00008020-001C292A2190003A --terminate-existing --console --environment-variables '{"NSUnbufferedIO":"YES"}' com.rebeckerspecialties.BabylonNative.Playground app:///Scripts/native_xr_portal_manual_validation.js app:///Scripts/native_xr_babylon_portal_demo.js
xcrun devicectl device capture screenshot --device 00008020-001C292A2190003A --destination /private/tmp/portal-display.png
```

The last command captures the device display, including compositor content.
Do not add a short global devicectl timeout to a console-attached manual run:
it detaches the console when that timeout expires.

Run the Rust GPU regressions explicitly (from the repository root):

```sh
BABYLON_WGPU_NATIVE_FFI_DIR="$PWD/build_int_macos/_deps/wgpu_native-src/ffi" \
CARGO_TARGET_DIR=build_int_macos/cargo \
cargo test --locked --lib -p babylon_graphics_backend --features wgpu-native-rlib \
  --config "patch.\"https://github.com/rebeckerspecialties/wgpu-native.git\".wgpu-native.path=\"$PWD/build_int_macos/_deps/wgpu_native-rlib-src\"" \
  canvas_ -- --ignored --test-threads=1
```
