# WGPU Migration Plan (BGFX Replacement)

## Scope
- Replace `bgfx` with a Rust `wgpu` backend across Babylon Native.
- Keep current feature parity for NativeEngine + NativeXR.
- Enable BabylonJS WebGPU execution path on native targets.
- Preserve platform coverage: Android 10+, iOS, macOS, and Windows 10.
- Rebase long-term backend implementation onto upstream `wgpu-native` to avoid
  maintaining a local fork-like Rust implementation.

## Module Ownership Map (for reviewers)
- `Core/GraphicsWgpu`:
  owns backend lifetime (instance/surface/device/queue/present path) and exports
  the shared Rust C ABI (`babylon_graphics_backend`).
- `Plugins/NativeWebGPU`:
  owns JS-facing `navigator.gpu` module surface and forwards WebGPU/interop calls
  to the shared GraphicsWgpu ABI.
- `Polyfills/CanvasWgpu`:
  owns JS-facing Canvas subset (`Canvas`, `Context`, `Path2D`, etc.) and forwards
  Canvas draw operations to Rust/femtovg; `getCanvasTexture()` returns a native
  texture handle consumed by `GPUQueue.copyExternalImageToTexture(...)`.
- Relationship:
  `NativeWebGPU` and `CanvasWgpu` are peer JS modules; both use
  `Core/GraphicsWgpu` as the single backend target so the process links one
  wgpu runtime graph.

## Progress Update (current branch)
- Added a root Rust workspace (`/Cargo.toml`) centered on
  `Core/GraphicsWgpu/Rust`, with CanvasWgpu Rust source consumed as an in-crate
  module from GraphicsWgpu so there is a single dependency graph and lockfile
  (`/Cargo.lock`) for the linked backend.
- Unified Rust build artifacts under top-level build output (`${CMAKE_BINARY_DIR}/cargo`)
  and removed inline Rust-target lockfiles from source subdirectories.
- Removed separate CanvasWgpu Rust crate build wiring; CanvasWgpu Rust exports
  are compiled into `babylon_graphics_backend` so only one Rust static backend
  target is linked into native binaries.
- Implemented `CanvasWgpu` filter blur execution (blur-only CSS filter path)
  and exposed native canvas interop handle export through `getCanvasTexture()`.
- Wired Playground WebGPU smoke script to render Canvas gradient+blur+text and
  push the native canvas handle into the WebGPU canvas-texture path.
- Added native bridge function
  `babylon_wgpu_import_canvas_texture_from_native(...)`
  (with `babylon_wgpu_set_debug_texture_from_native(...)` alias retained) to
  import CanvasWgpu output into GraphicsWgpu cube sampling (used by
  `GPUQueue.copyExternalImageToTexture(...)` internals).
- Added optional upstream `wgpu-native` source integration (`FetchContent`) behind
  `BABYLON_NATIVE_WGPU_USE_UPSTREAM_NATIVE`, with build-local manifest patching
  so `wgpu-native` is consumed as an `rlib` in the same Cargo graph as the
  GraphicsWgpu backend instead of as a second Rust staticlib.
- Added explicit `webgpu-headers` FetchContent wiring (pinned to the commit used
  by upstream `wgpu-native`) and switched shim bindgen include resolution to use
  that source-of-truth header path instead of relying on nested vendored copies.
- Added a shared internal C ABI declaration header
  (`Core/GraphicsWgpu/InternalInclude/Babylon/Graphics/WgpuInterop.h`) so
  `Core/GraphicsWgpu` and `Plugins/NativeWebGPU` consume one declaration source
  for Rust-exported `babylon_wgpu_*` entry points instead of duplicating local
  extern declarations.
- Added canvas-prefixed texture-import/stats C ABI entry points
  (`babylon_wgpu_import_canvas_texture_from_native` and
  `babylon_wgpu_get_canvas_texture_*`) and switched NativeWebGPU to those names
  while retaining debug-prefixed aliases for compatibility.
- Replaced NativeWebGPU `GPUQueue.copyExternalImageToTexture` no-op with a
  standards-aligned bridge that accepts Canvas-like external image sources and
  routes them through the shared canvas texture import path.
- Gated non-standard `navigator.gpu` hooks behind Chromium/WebKit-style
  developer flags:
  `BABYLON_NATIVE_ENABLE_WEBGPU_DEVELOPER_FEATURES` and
  `BABYLON_NATIVE_ENABLE_UNSAFE_WEBGPU`.
- Replaced the direct upstream `wgpu-native` staticlib link with a build-local
  `rlib` manifest patch under `${CMAKE_BINARY_DIR}/_deps`. This keeps the repo
  source clean while avoiding duplicate Rust runtime and Objective-C class
  symbols from independent `wgpu-hal` / `raw-window-metal` compilations.
- Converted the `upstream_wgpu_native` feature seam from no-op to active probe:
  backend init now records upstream `wgpu-native` version via `wgpuGetVersion()`
  and includes that metadata in adapter diagnostics.
- Expanded the upstream probe to use `wgpu-native` C ABI request flows
  (`wgpuInstanceRequestAdapter` + `wgpuAdapterRequestDevice` + `wgpuInstanceProcessEvents`)
  so adapter/device bootstrap viability is validated through upstream primitives
  before local fallback path execution.
- Updated async wiring assumptions for current upstream behavior: target builds
  treat `WGPUFuture` IDs from adapter/device/map/error-scope/pipeline async calls
  as optional metadata (`NULL_FUTURE` is expected today), and rely on callback
  completion + `wgpuInstanceProcessEvents` instead of `wgpuInstanceWaitAny`.
- Removed production `std::async` usage from NativeWebGPU Promise APIs (kept only
  under test hooks) and switched to JS-runtime deferred dispatch to cut thread
  churn and hot-path heap pressure.
- Optimized debug canvas texture import path to reuse GPU texture resources when
  dimensions are stable, avoiding per-frame texture/view/bind-group rebuilds.
- Added native readback staging-buffer reuse and CPU upload-buffer recycling in
  the debug canvas-texture import path, eliminating per-frame GPU/heap buffer
  allocations while preserving texture import behavior.
- Eliminated a CanvasWgpu hot-loop allocation leak by avoiding render-target
  recreation when width/height/DPI are unchanged across `nvgBeginFrame`.
- Added native `destroy` aliases for CanvasWgpu `Canvas` and `Context` objects,
  and wired disposal to release retained JS context references.
- Reduced NativeWebGPU per-frame JS wrapper churn by caching shared no-op and
  draw-marker callbacks instead of creating new function objects repeatedly.
- Reworked local fallback compute dispatch to reuse a persistent device/queue
  and cached compute pipeline, removing per-dispatch adapter/device setup.
- Reworked upstream `wgpu-native` compute dispatch path to reuse a persistent
  runtime (instance/adapter/device/queue) plus cached pipeline, removing
  per-dispatch bootstrap and reducing hot-path allocation churn.
- Inlined upstream `wgpu-native` C ABI bindings/dispatch logic into the
  primary GraphicsWgpu crate (`Core/GraphicsWgpu/Rust/src/lib.rs`) and removed
  the separate `upstream-shim` crate boundary from the workspace/dependency
  graph to avoid split ownership and duplicate crate wiring.
- Moved local `wgpu` runtime bootstrap ownership (instance + adapter/device
  selection/retry) into the same shim crate and switched `create_context` to
  consume shim-managed bootstrap results, reducing duplicate runtime ownership
  logic in `Core/GraphicsWgpu/Rust/src/lib.rs`.
- Moved depth/offscreen target creation and dimension clamping into shim-owned
  `LocalRuntimeState` helpers so local backend context code no longer duplicates
  those lifecycle/guard utilities.
- Moved local surface creation/configuration helpers into the shim crate
  (`create_local_surface` + `configure_local_surface`) so `create_context`
  remains focused on Babylon-facing context assembly while preserving behavior.
- Moved `create_context` bootstrap wiring (`instance/surface/probe/runtime/
  surface-config/format resolution`) into shim-managed
  `bootstrap_local_context`, leaving local `lib.rs` focused on Babylon pipeline
  assembly and per-frame behavior.
- Moved adapter identity resolution (backend/vendor/device/name fallback) into
  shim bootstrap output, so `create_context` no longer duplicates local-vs-
  upstream adapter mapping logic.
- Moved surface-frame acquisition and queue submit/present handoff into the
  shim (`acquire_surface_frame_view` + `submit_and_present`) so the local
  render loop uses shim-managed present semantics with less duplicated
  surface-error handling.
- Moved runtime+renderer ownership into a shim-managed
  `InteropBackendContext` (bootstrap/resize/render/install-debug-texture),
  leaving `Core/GraphicsWgpu/Rust/src/lib.rs` focused on Babylon FFI, error
  propagation, and telemetry state.
- Moved surface reconfigure path into shim (`reconfigure_local_surface`) so
  surface lifecycle operations (configure/reconfigure/acquire/present) are now
  consistently shim-owned.
- Introduced shim-owned `LocalRuntimeState` and migrated local backend context
  ownership to that runtime struct (device/queue/surface/surface-config/
  adapter metadata), reducing duplicated bootstrap/lifecycle fields in
  `Core/GraphicsWgpu/Rust/src/lib.rs`.
- Added persistent upstream bootstrap runtime initialization (`instance` +
  `adapter` + `device` + `queue`) in the shim and switched feature-enabled
  `create_context` probe path to consume that runtime.
- Extended upstream shim coverage with real surface-backed adapter probing
  (`wgpuInstanceCreateSurface` for Metal/Android/Win32 + adapter request with
  `compatibleSurface`) and switched `create_context` probe path to use it when
  a platform surface handle is available.
- Added upstream surface lifecycle probe (`wgpuSurfaceGetCapabilities` +
  `wgpuSurfaceConfigure` + `wgpuSurfaceGetCurrentTexture` + `wgpuSurfacePresent`
  + `wgpuSurfaceUnconfigure`) to validate queue/present-path viability through
  upstream C ABI before local fallback rendering path.
- Removed the remaining inline fallback shim definitions from
  `Core/GraphicsWgpu/Rust/src/lib.rs`; shim dependency is now always present,
  with upstream behavior selected via crate features.
- Switched root default to `BABYLON_NATIVE_WGPU_USE_UPSTREAM_NATIVE=ON` for this
  branch and removed the large non-upstream compute fallback in the shim,
  reducing local duplicate runtime logic while keeping explicit compile-time
  disabled stubs for non-upstream builds.
- Removed shim-only upstream surface probe/present validation helpers and
  switched bootstrap probing to `ensure_bootstrap_runtime(...)` to keep the
  interop layer smaller and avoid duplicate surface lifecycle validation paths
  that are already exercised by live render execution.
- Added serialized backend-call gating around Rust FFI `render`/`resize`/`destroy`
  to prevent resize-vs-present races observed on Android API 31 emulator
  (`Surface is not configured for presentation` panic loop).
- Hardened Playground startup against script ordering races by waiting for
  `createScene` initialization in the runner instead of exiting early, which
  removes intermittent gray-screen launches on Android API 31 simulator.
- Removed non-standard `__nativeWebGpuReady` and `__nativeCanvasReady` globals.
  The AppRuntime FIFO WorkQueue guarantees `navigator.gpu` and `_native.Canvas`
  are synchronously available before any script runs, matching the W3C WebGPU
  spec where `navigator.gpu` is a synchronous `[SameObject]` attribute.
- Hardened platform launcher reset scripts (macOS/iOS/Android Playground) to
  clear stale WebGPU smoke globals (`__babylonPlaygroundWebGpuSmokeReady`,
  `__webgpuSmokeDispose`) alongside `createScene` factory state, reducing
  intermittent gray starts caused by cross-run script-state carryover.
- Added explicit WebGPU-smoke readiness signaling
  (`__babylonPlaygroundWebGpuSmokeReady`) so the runner can await real
  async scene/canvas-texture readiness instead of relying on retry-only startup
  heuristics; this removed intermittent Android/iOS/macOS gray-start races
  without blocking the JS runtime thread.
- Removed native fail-open auto-enable draw fallback in
  `babylon_wgpu_render` (240-frame timeout path). Presentation is now driven by
  explicit JS/native draw signals (`_markWebGpuDrawRequested` and successful
  canvas-texture upload) instead of implicit frame-count heuristics.
- Fixed Playground scene bootstrap to bind Babylon scenes to the passed engine
  instance (`new BABYLON.Scene(engineArg)`), removing stale-global runtime
  coupling that could leave Android launches in a gray-screen state.
- Added render-loop exception guarding in the Playground runner so transient
  JS-side render errors are surfaced via status callbacks and trigger runtime
  recycle after repeated failures instead of leaving a silent gray frame.
- Removed extra panic/unwind wrapping from `babylon_wgpu_render` and
  `babylon_wgpu_resize` hot paths and switched to direct context casts by C ABI
  contract, reducing per-frame overhead on native render loops.
- Added a lock-free pending-upload gate for canvas texture handoff so the
  render loop avoids mutex acquisition when no Canvas texture upload is queued.
- Aligned local `wgpu` crate usage to the upstream `wgpu-native` major line
  (`wgpu` 27.x) and updated local API callsites (`FilterMode` sampler mipmap
  setting, `PipelineLayoutDescriptor.push_constant_ranges`, and
  `RenderPipelineDescriptor.multiview`) to keep compatibility while reducing
  drift during migration.
- Updated the local Rust graph to `wgpu = 29.0.3`; after upstream femtovg PR
  278 was published as `femtovg = 0.25.0`, the temporary fork branch was
  removed and `femtovg` now resolves `wgpu`, `wgpu-core`, `wgpu-hal`,
  `wgpu-types`, and `naga` to one 29.0.3 lineage from crates.io.
- Updated `wgpu-native` FetchContent to `v29.0.0.0` and switched local C/C++
  header resolution to the `webgpu-headers` copy bundled with that exact
  upstream tag. `wgpu-native` is now linked through the shared Cargo graph by
  adding `rlib` to a build-local copy of the fetched manifest, avoiding the
  duplicate Metal/Objective-C symbols caused by linking a separate Rust
  staticlib beside the local Rust `wgpu` API path.
- Replaced the temporary C-ABI compute-test shim with a small cached compute
  path on the existing local `wgpu` 29.0.3 runtime. The backend version probe
  calls the linked `wgpu-native` C ABI (`wgpuGetVersion`) from the same Rust
  staticlib, proving symbol resolution without a second compiled `wgpu` stack.
- Guarded `glslang` and `SPIRV-Cross` dependency fetch/build behind
  `BABYLON_NATIVE_PLUGIN_SHADERCOMPILER`; default wgpu-branch builds no longer
  fetch them because the bgfx shader compiler/tool/cache path is disabled.
- Hardened Android Playground launch sequencing by explicitly clearing
  stale `createScene` / scene-factory globals before script load and adding
  bounded async startup retries in `playground_runner.js`, reducing intermittent
  gray-start races without blocking the runtime thread.
- Removed probe-only upstream bootstrap ownership from the GraphicsWgpu render
  bootstrap path (`ensure_bootstrap_runtime` usage in local context creation),
  keeping adapter identity sourced from the active local runtime and reducing
  duplicate C-ABI validation paths in `Core/GraphicsWgpu/Rust/src/lib.rs`.
- Simplified local adapter/device bootstrap error/retry flow in
  `Core/GraphicsWgpu/Rust/src/lib.rs` by collapsing duplicated retry branches
  into a single helper-driven selection sequence and one Android-specific
  low-power fallback retry path.
- Further reduced local bootstrap duplication by replacing platform-split
  adapter selection branches with one ordered attempt plan per platform
  (`ADAPTER_ATTEMPTS`) in `bootstrap_local_wgpu_runtime`, keeping behavior
  while shrinking local orchestration code.
- Aligned iOS/macOS Playground script bootstrap with Android by clearing stale
  global scene factory symbols before loading smoke/runner scripts, reducing
  intermittent gray-start races after repeated app relaunches.
- Added explicit post-submit device polling in CanvasWgpu and tightened iOS
  simulator poll mode in GraphicsWgpu submit path to improve command retirement
  and reduce sustained simulator memory growth during long-running smoke loops.
- Fixed Apple WGPU surface bootstrap after the upstream sync by passing the
  already-typed `CA::MetalLayer*` surface pointer directly into GraphicsWgpu
  instead of dereferencing a non-existent `.layer` member on `WindowT`.
- Wrapped macOS/iOS/visionOS Playground render and resize callbacks in local
  autorelease pools so Objective-C/Metal autoreleased temporaries are drained
  per frame on Apple host loops.
- Switched the workspace `wgpu` dependency to `default-features = false` and
  selected backend features per target: Metal/WGSL/std on Apple targets and
  Vulkan/WGSL/std on Android, Linux, and Windows. The build-local
  `wgpu-native` rlib manifest patch also removes upstream's direct `hal`
  DX12/RenderDoc feature forcing so our platform feature selection controls
  the linked backend set.
- Moved CanvasWgpu image decoding on macOS/iOS/visionOS from Rust `image` crate
  codecs to a small Swift 6 `ImageIO`/`CoreGraphics` decoder behind the existing
  `babylon_canvas_decode_image_rgba` C ABI. The decoder intentionally loads
  only the first frame and keeps `HTMLImageElement` animation behavior out of
  scope for this JS game-engine runtime.
- Moved CanvasWgpu image decoding on Android 10+ from Rust `image` crate codecs
  to platform decoders: Android 11/API 30+ dynamically loads the NDK
  `AImageDecoder` entrypoints from `libjnigraphics` and decodes directly into
  native RGBA memory; Android 10/API 29 keeps a `BitmapFactory` +
  `AndroidBitmap_lockPixels` fallback.
- Added UnitTests coverage for CanvasWgpu `Image` PNG data URL decode success
  and invalid data URL rejection so the platform decoder path is exercised
  through the same JS-visible Canvas image API.
- Added Playground smoke coverage that decodes a PNG data URL, draws it into the
  CanvasWgpu texture, and checks invalid data URL rejection through the same
  `Image` event path used by application JS.

## Latest Validation Snapshot (2026-05-14)
- Dependency/feature state:
  - Apple target Cargo feature audit resolves `wgpu` and `wgpu-native` with
    `metal` only; the Rust `image` crate is not in the Apple target graph.
  - Android target Cargo feature audit resolves `wgpu` and `wgpu-native` with
    `vulkan`; the Rust `image` crate is not in the Android target graph.
  - Linux/Windows target Cargo feature audits resolve `wgpu` and `wgpu-native`
    with `vulkan`; those targets still retain the Rust `image` decoder while
    the Linux/SteamOS platform decoder strategy is finalized.
  - Upstream `wgpu` 29.0.3 still hardwires `wgpu-core`'s `renderdoc` feature
    for non-wasm targets through its target dependency. The local
    `wgpu-native` patch no longer adds DX12 or RenderDoc on top of that.
  - Steam Runtime 3.0 `sniper` latest public stable/beta manifests include
    `libgdk-pixbuf-2.0-0`, `libjpeg62-turbo`, `libpng16-16`, `libtiff5`, and
    `libwebp6`, so a dynamically loaded Linux system/runtime decoder path looks
    plausible. Do not remove the Linux Rust decoder until that path is
    implemented and validated inside the Steam Runtime container.
- Release LTO-bitcode build:
  - Reconfigured `build_wgpu_29_release_lto_bitcode` with Rust `1.94.1`
    (`rustc 1.94.1`, LLVM `21.1.8`) and
    `BABYLON_NATIVE_LTO_BITCODE_STATIC_LIBS=ON`.
  - Built `Playground`, `UnitTests`, and `NativeWebGPUAsyncTests`.
  - Final stripped Playground size changed from `5,417,720` bytes to
    `4,880,648` bytes, a reduction of `537,072` bytes (`9.91%`).
  - Raw Playground size changed from `6,616,872` bytes to `6,001,752` bytes,
    a reduction of `615,120` bytes (`9.30%`).
  - Final link-input inspection checked 25 object/archive inputs and 132
    archive members; all had LLVM bitcode markers.
  - `otool -L` now shows dynamic links to system `CoreGraphics` and `ImageIO`;
    `nm` confirms `babylon_canvas_decode_image_rgba` resolves from
    `CanvasAppleImageDecoder` and the Rust image codec symbols are absent from
    the macOS Playground binary.
- Tests and smoke validation:
  - `ctest --test-dir build_wgpu_29_release_lto_bitcode --output-on-failure`
    passed, including the new Canvas `Image` data URL decode/reject tests.
  - `NativeWebGPUAsyncTests` passed all 8 tests.
  - Refreshed macOS ASan/UBSan and ThreadSanitizer builds passed `UnitTests`,
    `NativeWebGPUAsyncTests`, and Playground log smoke. Both sanitizer
    Playgrounds decoded the PNG data URL, rejected the invalid image data URL,
    drew the decoded image into the CanvasWgpu texture, and reported the
    BabylonJS WebGPU path without sanitizer diagnostics.
  - Android API 31 ASan Playground build/install/run passed the image decoder
    smoke through the `AImageDecoder` path: `image-loaded:2x1` and
    `image-invalid-rejected:1`, with no ASan report. Native heap stayed flat at
    `22,364 KB` through 15 one-second `dumpsys meminfo` samples after warmup.
    This emulator still reports a broken Vulkan/wgpu device limit
    (`max buffer size (0)`), so the Android WebGPU visual path remains blocked
    on this virtual device even though the decoder smoke is now green.
  - macOS LTO Playground launched visibly by running the executable directly
    from the bundle (the Ninja bundle Info.plist still contains
    `$(EXECUTABLE_NAME)`, so `open` cannot launch it). Screenshot:
    `/tmp/babylonnative-validation/playground-lto-after-appledecode-macos.png`.
  - The visible smoke rendered the BabylonJS WebGPU cube with CanvasWgpu
    gradient/text texture. A short RSS sample still showed page-sized upward
    drift; the saved pre-change LTO executable showed the same trend in the
    same scene, so this was not introduced by the Apple decoder change.

## Previous Validation Snapshot (2026-05-13)
- Branch state:
  - `wgpu` is based on current `origin/master`
    (`git merge-base --is-ancestor origin/master HEAD` passed).
  - HEAD remained `af1853d1 Keep wgpu branch defaults production-safe` before
    this validation pass.
- Dependency state:
  - Local Rust uses `rustup` toolchain `1.94.1` (`rustc 1.94.1`, LLVM `21.1.8`).
  - Local workspace resolves `wgpu = 29.0.3` and upstream crates.io
    `femtovg = 0.25.0`. The temporary
    `https://github.com/rebeckerspecialties/femtovg.git`
    branch is no longer used after upstream femtovg PR 278 was merged and
    published.
  - The active compiled graph contains a single 29.0.3 lineage for `wgpu`,
    `wgpu-core`, `wgpu-hal`, `wgpu-types`, and `naga`; the remaining
    `cargo tree --duplicates` output is limited to non-wgpu transitive crates
    (`hashbrown` in this snapshot).
  - `wgpu-native` FetchContent is updated to `v29.0.0.0` for upstream source
    and bundled-header alignment. The build creates a patched copy under the
    build tree with `crate-type = ["rlib"]` and Cargo
    patches the git dependency to that copy, so `wgpu-native`, local `wgpu`,
    and femtovg share one compiled `wgpu` 29.0.3 graph.
  - Default wgpu builds no longer need `glslang` or `SPIRV-Cross`; they are
    only fetched when `BABYLON_NATIVE_PLUGIN_SHADERCOMPILER` is enabled.
- femtovg validation:
  - BabylonNative now consumes published `femtovg 0.25.0` from crates.io with
    `default-features = false` and `features = ["wgpu", "textlayout"]`.
  - `cargo tree --duplicates` still shows one coherent `wgpu`/`wgpu-core`/
    `wgpu-hal`/`naga` 29.0.3 lineage across BabylonNative, femtovg, and
    `wgpu-native`; the only duplicate shown is non-wgpu `hashbrown`.
- macOS build:
  - After the `wgpu-native` rlib fix, `build_wgpu_29_debug` rebuilt
    `Playground`, `UnitTests`, and `NativeWebGPUAsyncTests` successfully.
    Archive inspection showed one `raw_window_metal` object, one `wgpu-hal`
    object hash, one `wgpu-core` object hash, and exported `wgpu-native` C ABI
    symbols (`wgpuGetVersion`, `wgpuCreateInstance`) in
    `libbabylon_graphics_backend.a`.
  - `build_wgpu_29_debug` configured and built `Playground`, `UnitTests`, and
    `NativeWebGPUAsyncTests` successfully after the wgpu 29.0.3/femtovg update.
  - `build_wgpu_29_asan_ubsan` configured with `ENABLE_SANITIZERS=ON` and
    built `UnitTests` + `NativeWebGPUAsyncTests` successfully.
  - `build_wgpu_29_tsan` configured with AppleClang
    `-fsanitize=thread -fno-omit-frame-pointer` C/C++ flags and built
    `UnitTests` + `NativeWebGPUAsyncTests` successfully. Rust code in this
    build is not TSan-instrumented; this validates the C/C++/ObjC++ side and
    link/runtime compatibility with the Rust staticlib.
  - Old intermediate build directories were removed. Current optimized
    comparison builds are:
    - `build_wgpu_29_release_baseline`: Release, non-LTO.
    - `build_wgpu_29_release_lto_bitcode`: Release with
      `BABYLON_NATIVE_LTO_BITCODE_STATIC_LIBS=ON`.
  - LTO-bitcode build uses Xcode 26.5 / AppleClang 21.0.0 and Rust
    `1.94.1` (`LLVM 21.1.8`). Native C/C++/ObjC/ObjC++ compile and final link
    use `-flto=full`. Rust target crates use target-scoped
    `CARGO_TARGET_AARCH64_APPLE_DARWIN_RUSTFLAGS=-C linker-plugin-lto -C embed-bitcode=yes -C codegen-units=1`.
    The build also uses Rust `build-std` with `panic_abort` so the Rust
    standard-library and compiler-builtins members are rebuilt by the same
    Rust 1.94.1 toolchain instead of pulling prebuilt native std objects into
    `libbabylon_graphics_backend.a`.
  - Final-link archive inspection for
    `build_wgpu_29_release_lto_bitcode/Apps/Playground/Playground.app/Contents/MacOS/Playground`
    showed every direct object and every member of every linked `.a` contains
    LTO bitcode (`other=0`), including
    `cargo/aarch64-apple-darwin/release/libbabylon_graphics_backend.a`
    (`total=103`, `lto_bitcode=103`, `other=0`).
  - Stripped optimized Playground size changed from `12,222,464` bytes
    (`build_wgpu_29_release_baseline`) to `5,417,720` bytes
    (`build_wgpu_29_release_lto_bitcode`), a reduction of `6,804,744` bytes.
  - `build_wgpu_29_release_lto_bitcode` passed `UnitTests` and all 8
    `NativeWebGPUAsyncTests`.
- macOS visible smoke:
  - After the `wgpu-native` rlib fix, Debug Playground rendered a non-blank
    BabylonJS WebGPU spinning cube with visible CanvasWgpu gradient/text.
    Screenshot captured at
    `/tmp/babylonnative-validation/playground-wgpu-native-rlib-macos.png`.
    Logs included `webgpu-smoke:compute-dispatched`,
    `webgpu-smoke:babylon-webgpu-path:1`, and runtime counters
    `frames=643:submit=486:draw=968:canvasSkip=0:canvasW=512:canvasH=512`.
  - Debug Playground rendered a non-blank BabylonJS WebGPU spinning cube with
    visible CanvasWgpu gradient/text on cube faces after the wgpu 29.0.3 update.
  - Screenshot captured at
    `/tmp/babylonnative-validation/playground-wgpu29-macos.png`.
  - Logs included `Babylon.js v9.6.2 - WebGPU1 engine`,
    `runner:using-webgpu-engine`, `webgpu-smoke:canvas-texture-uploaded:1`,
    `webgpu-smoke:compute-dispatched`, and
    `webgpu-smoke:runtime-counters:frames=655:submit=498:draw=992:canvasSkip=0:canvasHash=5627888531996255000:canvasW=512:canvasH=512:gpuBytes=13049576`.
  - Release+IPO Playground rendered a non-blank BabylonJS WebGPU spinning cube
    with visible CanvasWgpu gradient/text on cube faces.
  - Clean screenshot captured at
    `/var/folders/_c/krb2zmbx2ss6b1xhpydpxcr80000gn/T/codex-shot-2026-05-13_00-00-53.png`.
  - Logs included `Babylon.js v9.6.2 - WebGPU1 engine`,
    `runner:using-webgpu-engine`, `webgpu-smoke:queue-copy-ready:1`,
    `webgpu-smoke:canvas-texture-uploaded:1`, `webgpu-smoke:compute-dispatched`,
    `runner:renderloop-frame:first`, and
    `webgpu-smoke:babylon-webgpu-path:1:pipeline=3:submit=52:draw=100:drawPath=true`.
  - Runtime counter at ~10s: `frames=618`, `submit=495`, `draw=986`,
    `canvasSkip=0`, `canvasW=512`, `canvasH=512`,
    `gpuBytes=13049576`.
  - Release LTO-bitcode Playground rendered a non-blank BabylonJS WebGPU
    spinning cube with visible CanvasWgpu gradient/text on cube faces after
    switching from the temporary femtovg fork to published `femtovg 0.25.0`.
    Screenshot captured at
    `/tmp/babylonnative-validation/playground-lto-bitcode-macos.png`.
  - Logs included `Babylon.js v9.6.2 - WebGPU1 engine`,
    `runner:using-webgpu-engine`, `webgpu-smoke:canvas-upload-mode:queue-copyExternalImageToTexture`,
    `webgpu-smoke:compute-dispatched`,
    `webgpu-smoke:babylon-webgpu-path:1:pipeline=3:submit=52:draw=100:drawPath=true:backendMode=interop-shim-babylonjs-webgpu`,
    and
    `webgpu-smoke:runtime-counters:frames=627:submit=498:draw=992:canvasSkip=0:canvasHash=4467692183165683000:canvasW=512:canvasH=512:gpuBytes=13049576`.
- macOS memory/CPU:
  - For the wgpu 29.0.3 Debug smoke, the initial RSS sample rose from
    `259,632 KB` to `260,320 KB` over 15s and then continued to
    `261,952 KB` over a longer warmup, so it was investigated instead of being
    accepted immediately.
  - A later `vmmap -summary` pair over 30s was flat (`Physical footprint`
    `546.1 MB` -> `546.0 MB`, `IOAccelerator (graphics)` `5984 KB` stable,
    `IOSurface` `11.9 MB` stable, `owned unmapped (graphics)` `397.9 MB`
    stable). A final 15s RSS sample plateaued around `262,992-263,024 KB`,
    with CPU generally `13-21%` in Debug.
  - Current conclusion for the wgpu 29.0.3 Debug run: warmup/residency growth
    followed by a stable plateau, not confirmed unbounded growth.
  - Initial post-warmup RSS rose from `249,968 KB` at `00:16` elapsed to
    `251,680 KB` at `00:56`, so the run was investigated further.
  - `vmmap -summary` did not show a growing heap/footprint trend across later
    samples: physical footprint moved `578.2 MB` -> `573.7 MB`, WebKit malloc
    allocated bytes moved `64.1 MB` -> `58.3 MB`, and Default malloc allocated
    bytes moved `16.7 MB` -> `16.0 MB`.
  - A longer sample then plateaued around `253,312-253,424 KB` RSS from
    roughly `02:06` through `04:05` elapsed, with CPU generally `7-12%`.
  - Current conclusion: the macOS smoke shows warmup/residency growth followed
    by a stable plateau, not confirmed unbounded CPU/GPU memory growth.
  - The release LTO-bitcode smoke showed increasing `ps` RSS during the first
    samples, but a `vmmap -summary` pair taken 20s apart did not show allocator
    or GPU-surface growth: dirty/resident-like `TOTAL` accounting moved
    `582.5 MB` -> `577.1 MB`, malloc allocated moved `82.2 MB` -> `74.8 MB`,
    and `IOSurface` stayed flat at `11.9 MB`. Current conclusion: the observed
    RSS rise is resident page-in/accounting noise, not confirmed heap or GPU
    memory growth.
- macOS sanitizer tests:
  - ASan+UBSan: `ctest --test-dir build_wgpu_29_asan_ubsan -R UnitTests`
    passed. `NativeWebGPUAsyncTests` passed with
    `ASAN_OPTIONS=detect_leaks=0:abort_on_error=1` and
    `UBSAN_OPTIONS=halt_on_error=1`; Apple ASan reported that
    `detect_leaks=1` is unsupported on this platform.
  - ThreadSanitizer: `ctest --test-dir build_wgpu_29_tsan -R UnitTests`
    passed and `NativeWebGPUAsyncTests` passed with
    `TSAN_OPTIONS=halt_on_error=1`.
- Device validation status:
  - `devicectl list devices` currently reports the requested iPhone XS entry as
    `unavailable` (`C7A1D5AD-C89D-5E03-99B9-5E65EEC30486`). The visible iPhone XS
    smoke run has not been completed in this pass.
  - `adb devices` reports no running Android devices. The API 31 Vulkan AVD
    exists as `BN_API_31_GA_VK`, but the visible Android smoke run has not been
    completed in this pass.
  - The current upstream screenshot comparison harness
    (`Apps/Playground/Scripts/validation_native.js`) still targets
    `BABYLON.NativeEngine()`, not the WebGPU engine path, so the screenshot
    comparison suite is not yet a valid WebGPU-path acceptance signal.

## Current Spike Reality (as of this branch)
- `Core/GraphicsWgpu/Rust/src/lib.rs` is now the single GraphicsWgpu Rust
  runtime source (~2.9k LOC) and contains Babylon-facing FFI glue plus the
  temporary local `wgpu` runtime management that must be collapsed toward
  upstream `wgpu-native` C ABI calls. The compute dispatch path lives in
  `src/compute.rs` as a submodule and currently dispatches on the same local
  `wgpu` runtime while the version probe resolves through `wgpu-native` C ABI
  symbols from the shared Rust staticlib.
- `Polyfills/CanvasWgpu/Rust/src/lib.rs` remains the CanvasWgpu Rust runtime
  source (~1.2k LOC) and is included into the GraphicsWgpu crate via
  `#[path = ...]` so only one Rust staticlib is produced.
  - TODO: Convert CanvasWgpu to a proper workspace member crate once the
    code stabilizes, to improve IDE tooling and eliminate the fragile
    cross-directory `#[path]` include.
- There is no longer a separate `Core/GraphicsWgpu/Rust/upstream-shim` crate
  in-tree.
- This is not a patched dependency; `wgpu` resolves from crates.io.
- MSRV is currently recorded as `1.76` in `Cargo.toml`, but the active local
  toolchain for this branch is Rust `1.94.1`; re-check the declared MSRV before
  treating it as a supported floor for `wgpu` 29.0.3.
- Android emulator validation indicates the practical floor is currently API 31
  for stable Vulkan behavior in this environment; API 29/30 emulator images
  expose adapter/device-loss issues that are likely emulator-stack specific.
- `Plugins/NativeWebGPU` still contains temporary stubbed draw-path bridging:
  WebGPU JS draw activity currently marks draw intent/counters, while actual
  frame rendering is still executed by `DebugCubeRenderer` in
  `Core/GraphicsWgpu/Rust/src/lib.rs`. Full BabylonJS command-stream execution
  over upstream `wgpu-native` C ABI remains a Phase 2 migration item.
- All FFI entry points now use `catch_unwind` to prevent panics from crossing
  the `extern "C"` boundary, including the render and resize hot paths.

## Current Coupling Snapshot
- `bgfx` API usage in tracked source: ~644 references (`Core/` + `Plugins/`).
- Direct `#include <bgfx/...>` usage: 28 files.
- Coupling is not only in rendering core:
  - `Core/Graphics`: device lifecycle, frame scheduling, view IDs, texture/framebuffer lifetime.
  - `Plugins/NativeEngine`: shader binaries, pipeline state mapping, texture formats, draw submission.
  - `Plugins/NativeXr`: swapchain texture wrapping via `bgfx::overrideInternal`.
  - `Plugins/ExternalTexture` and `Plugins/NativeCamera`: native texture interop currently assumes bgfx handles.

## Phase 2 Blockers (must resolve before PR merge)
- [ ] Replace `DebugCubeRenderer` with real BabylonJS WebGPU command-stream
  execution through upstream `wgpu-native` C ABI.
- [ ] Port the active local `wgpu` Rust API render/compute path to upstream
  `wgpu-native` C ABI calls now that the duplicate-staticlib/ObjC class symbol
  issue is fixed by consuming `wgpu-native` as an `rlib` in the shared Cargo
  graph.
- [ ] Implement `getImageData` GPU texture readback (currently returns zeros).
- [ ] Windows validation snapshot — no Win32 D3D12 data in current results.
- [ ] Vulkan-backend CI coverage on Windows (currently D3D12 only).
- [ ] Android runtime test automation (currently commented out in CI).

## Known API / Fidelity Gaps
| Area | Gap | Severity |
|------|-----|----------|
| Canvas `getImageData` | Returns zeroed pixel data (no GPU readback) | High |
| Canvas `putImageData` | Throws `not implemented` | High |
| Canvas `setLineDash` | Throws `not implemented` (femtovg lacks stroke dashing) | Medium |
| Canvas shadow properties | All throw `not implemented` | Medium |
| Canvas `drawImage` 9-arg | Source rect parameters ignored | Medium |
| Canvas RTL text | Byte-reversed (incorrect for multi-byte UTF-8) | Medium |
| Canvas `roundRect` elliptic | Averages x/y radius (approximation) | Low |
| NativeWebGPU error scopes | `pushErrorScope`/`popErrorScope` are no-ops | Medium |
| NativeWebGPU `device.lost` | Never-resolving promise | Low |
| NativeWebGPU adapter limits | Hardcoded, not queried from GPU | Medium |
| NativeWebGPU cached encoder | Object-identity diverges from W3C spec | Low |
| NativeWebGPU `GPUCanvasContext` | Non-standard `_createCanvasContext` only | Medium |
| NativeWebGPU features | `adapter.features` and `device.features` are empty Sets | Medium |
| Smoke test pixel validation | Counter-based only; no pixel comparison | Medium |
| Compute shader validation | No-op shader only; no output verification | Low |

## bgfx Removal Tracking
The following areas still contain bgfx coupling that must be addressed during
Phases 3-6. Search for `TODO(bgfx-removal)` comments in the codebase.
- `Core/Graphics/` — device lifecycle, frame scheduling, view IDs, texture/framebuffer lifetime (~644 bgfx references across `Core/` + `Plugins/`).
- `Plugins/NativeEngine/` — shader binaries, pipeline state mapping, texture formats, draw submission.
- `Plugins/NativeXr/` — swapchain texture wrapping via `bgfx::overrideInternal`.
- `Plugins/ExternalTexture/` and `Plugins/NativeCamera/` — native texture interop assumes bgfx handles.
- `CMakeLists.txt` root — `option()` toggles for NativeWebGPU/Canvas are hardwired ON with `FORCE` overrides.
- `WgpuInterop.h` — legacy `debug_texture` aliases should be removed.
- `validation_native.js` — still uses `BABYLON.NativeEngine()`, not WebGPU engine.
- CI templates — Linux still references `OpenGL_GL_PREFERENCE=GLVND` (bgfx-era flag).

## Migration Strategy

### Phase 0: Freeze behavior + baseline
- Lock current behavior with golden tests and capture:
  - `Apps/UnitTests` render tests.
  - XR startup/render/session teardown flow.
  - External texture and camera integration smoke tests.
- Add a feature coverage checklist (textures, MRT, readback, stencil, shader variants, XR multiview).

### Phase 1: Introduce backend boundary (no behavior change)
- Add backend-agnostic interfaces in `Core/Graphics`:
  - `IRenderDevice`, `IRenderQueue`, `ITexture`, `IFrameBuffer`, `IPipeline`, `IShaderModule`.
- Convert `DeviceContext`, `Texture`, and `FrameBuffer` wrappers to depend on these interfaces, not raw `bgfx` handles.
- Keep `bgfx` implementation behind the new boundary as the initial backend.
- Do not change JS-facing API yet.

### Phase 2: Rebase core on `wgpu-native`
- Adopt upstream `wgpu-native` as the implementation base for WebGPU-native ABI.
- Keep Babylon-specific code as a thin shim layer only:
  - platform surface wiring and host window handles,
  - async bridge glue (Rust task -> C++ future -> JS Promise),
  - diagnostics and feature gating.
  - preserve actionable JS callsite stack fidelity for rejected async APIs so
    crash/telemetry systems (e.g., Sentry) keep useful JavaScript frames.
- Replace large local Rust render/device logic with calls into `wgpu-native` C ABI.
- Preserve existing C++ boundary while transitioning:
  - keep `WgpuNative` host object,
  - progressively route internals through upstream ABI.
- Version alignment requirement:
  - keep Babylon crate `wgpu*` versions aligned with `wgpu-native` major/minor
    during migration to avoid API drift and duplicate backend logic.
- Integration note:
  - upstream `wgpu-native` currently declares `cdylib/staticlib` crate types
    but not `rlib`; Babylon patches the fetched manifest in the build tree to
    add `rlib` so the C ABI can be linked through one Cargo graph. Avoid
    reintroducing a separately linked `libwgpu_native.a` while the local
    `wgpu` Rust API path still exists.
- Remaining work for Phase 2:
  - replace local `Core/GraphicsWgpu/Rust/src/lib.rs` device/pipeline logic
    with upstream `wgpu-native` ABI-backed calls while preserving current C++ APIs,
  - move CanvasWgpu interop path from local raw-handle bridge to upstream-safe
    interop abstractions where available,
  - eliminate temporary dual-runtime duplication (`wgpu` local runtime +
    `wgpu-native` staticlib runtime) once bootstrap/present paths are fully
    switched to upstream ABI.

### Phase 3: NativeEngine port
- Replace direct `bgfx` calls in `Plugins/NativeEngine` with backend interface calls.
- Rework shader path:
  - move from bgfx binary shader expectations to WGSL/SPIR-V path consumed by `wgpu`.
  - preserve existing BabylonJS shader defines/variants contract.
- Port render state translation (blend/depth/stencil/cull/sampler) to explicit `wgpu` pipeline descriptors.

### Phase 4: XR swapchain interop (critical)
- Replace `bgfx::overrideInternal` flow in `Plugins/NativeXr/Source/NativeXrImpl.cpp`.
- Import OpenXR swapchain images into the `wgpu` backend through a controlled unsafe interop path:
  - per-API import adapter (Vulkan/Metal/D3D12) in Rust side,
  - explicit ownership/lifetime rules (no implicit handle reuse),
  - one render target abstraction for mono/stereo array layers.
- Keep the existing JS render target creation callbacks unchanged.

### Phase 5: ExternalTexture + NativeCamera
- Port native texture wrapping to the new backend.
- Preserve existing behavior contracts for:
  - render-thread scheduling (`BeforeRenderScheduler`, `AfterRenderScheduler`),
  - async texture updates and teardown safety.

### Phase 6: Remove bgfx
- Delete `bgfx` dependencies from `Core`, `Plugins`, and `Dependencies` wiring.
- Remove bgfx-specific shader compiler code paths and constants.
- Keep a temporary compile-time rollback flag only during stabilization; remove after rollout.

## Windows 10 DX12 Plan (special handling)

### Why this needs special care
- Windows 10 has more driver/compiler fragmentation than Metal/Vulkan targets.
- Shader compiler choice and present model have material stability/perf impact.

### Required handling
- Use `wgpu` DX12 backend options explicitly:
  - shader compiler selection (`Fxc`, `DynamicDxc`, `StaticDxc`),
  - swapchain present model (`Discard`, `Sequential`, `FlipSequential`),
  - frame latency controls (`present_waitable`, `max_frame_latency`).
- Build/runtime policy:
  - Default to `DynamicDxc` when available.
  - Fall back to `Fxc` on systems missing compatible DXC.
  - Provide environment/config override hooks for support triage.
- Validation matrix on Windows 10:
  - base 19041 and later,
  - Intel + AMD + NVIDIA representative GPUs,
  - D3D12 fallback behavior when DXIL path is unavailable.

## BabylonJS WebGPU Enablement
- Keep the JS `NativeEngine` contract stable while adding a backend capability flag for WebGPU mode.
- Ensure BabylonJS can select its WebGPU path when native backend reports required capabilities.
- Validate with representative scenes:
  - PBR + postprocess,
  - MRT/depth/stencil heavy scenes,
  - compute + readback where supported.

## `wgpu-native` Rebase Checklist
- [x] Add a compile-time backend selector (`local` vs `wgpu-native`) and switch branch default to upstream while keeping rollback path.
- [x] Wire upstream `wgpu-native` C ABI symbols into Babylon without linking a
  second Rust staticlib by patching the fetched build-local manifest to expose
  `rlib` and Cargo-patching the git dependency to that source.
- [ ] Reintroduce the thin upstream C ABI shim only as callsites are actually
  ported to `wgpu-native`; avoid probe-only duplicate runtime ownership.
- [ ] Port adapter/device/surface bootstrap to upstream ABI.
  - Current state: default builds use the local `wgpu` 29.0.3 runtime for
    render/compute dispatch. Upstream `wgpu-native v29.0.0.0` source/headers
    and C ABI symbols are linked through the shared Rust staticlib for the next
    C-ABI migration slice.
- [ ] Migrate remaining local `create_context` render/surface pipeline setup
  into shim-managed upstream handles in reversible slices.
- [ ] Replace temporary DebugCubeRenderer submit path with real
  NativeWebGPU -> `wgpu-native` command ownership.
- [x] Continue collapsing local render-resource ownership into shim-managed
  runtime primitives (depth/offscreen target lifecycle + size/format guards).
- [ ] Port queue submit + present path to upstream ABI.
- [ ] Port async callback and error propagation tests.
- [ ] Remove local duplicate render/pipeline management code once parity is achieved.

## Risk Register
- XR external image import lifetime mismatches.
- Shader translation drift (bgfx shader model vs `wgpu` pipeline model).
- D3D12 compiler/runtime differences on older Windows 10 installs.
- Performance regressions from excessive command encoder churn.
- Cross-device texture copy overhead (Canvas renders on isolated wgpu device).
- `bindgen` build dependency requires `libclang` on all build hosts — not yet
  documented in contributor setup guides.
- Canvas blur approximation (up to 289 draw calls per blurred operation) may be
  prohibitive for complex scenes with multiple blurred elements.
- CanvasWgpu font data may be double-stored (once in `font_blobs`, once in femtovg).
- Stale pointer risk in `import_native_texture_rgba_inner` if Canvas Rust objects
  are dropped while C++ still holds the `CanvasNativeTextureHandle`.

## Suggested Deliverables
1. PR A: backend interfaces + bgfx adapter (no behavior change).
2. PR B: Rust `wgpu` bootstrap + device/surface init + clear-screen sample.
3. PR C: NativeEngine core draw path on `wgpu` (desktop first).
4. PR D: NativeXR swapchain import + stereo render targets.
5. PR E: ExternalTexture/NativeCamera port.
6. PR F: bgfx removal + cleanup.

## Definition of Done
- Unit tests and representative app scenarios pass on Android, iOS, macOS, Win10.
- BabylonJS WebGPU path runs in Playground scenario without feature regression.
- XR session lifecycle works in Android XR simulator + physical device with stable frame pacing.
- No remaining `bgfx` link or include dependency in Babylon Native core/plugins.
- No large local Rust backend implementation duplicates `wgpu-native` internals.
