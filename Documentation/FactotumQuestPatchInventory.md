# Factotum Quest patch inventory

Routing ledger, 2026-11-21. An open PR is not a released dependency. The performance
comparison uses Quest Browser 112 and the Vulkan fork, not Babylon's bgfx renderer.
Keep runtime implementation, portable conformance, app policy and replay tooling in
separate commits. Do not export the dirty fork as one patch.

## November 23: compositor and Android application

These drafts are in Rebecker repositories. The fork's runtime can be built
without a sibling checkout. Keep OpenXR/Vulkan implementation details out of
the portable WebXR API proposals.

| Scope | Review and revision | Verification |
| --- | --- | --- |
| Portable WebXR | [Rigid transform dictionaries](https://github.com/rebeckerspecialties/BabylonNative/pull/30) `f7017ba1`; [moving reference spaces](https://github.com/rebeckerspecialties/BabylonNative/pull/31) `96f0cf07`; [animation timestamps and end events](https://github.com/rebeckerspecialties/BabylonNative/pull/32) `755c3f06`; [required feature negotiation](https://github.com/rebeckerspecialties/BabylonNative/pull/33) `3400b309` | Separate candidates for BabylonNative, independent of the wgpu renderer. |
| Android video | [Native fork #34](https://github.com/rebeckerspecialties/BabylonNative/pull/34) | Earlier callback-only comparisons missed green decoded pixels. The [current device handoff](QuestRuntimeHandoff.md) records the corrected decoder, FFmpeg comparison, and separate eye-routing checks. Direct media uses compositor surfaces instead. |
| Projection foveation and space warp | [Native fork #35](https://github.com/rebeckerspecialties/BabylonNative/pull/35) `70c3b1e3`, `496e6514`, `fc3903db`; [wgpu fork #5](https://github.com/rebeckerspecialties/wgpu/pull/5) `b1e25a650`, `c5e0db1da`; downstream carry `e1ccd8a8` | Quest checkerboard at the recommended eye extent: 240 center and peripheral edges per eye when off, 240 center / 0 peripheral at maximum, 240 / 240 restored. Combined warp and media: 145/141/144 application frames over four seconds for quad/cylinder/equirect. No optical synthesized-frame count. |
| Reproducible samples | [Samples #1](https://github.com/rebeckerspecialties/babylonnative-webxr-samples/pull/1) `960645e`, pinned to `fc3903db` | [Clean arm64 APK build](https://github.com/rebeckerspecialties/babylonnative-webxr-samples/actions/runs/45270798538) includes the assets and runs the host tests. See the sample repository's QUEST-VALIDATION.md for fixture IDs and physical measurements. |
| Application and CI | [Webapp shaders #1342](https://github.com/rebeckerspecialties/webapp/pull/1342) `34249fbc`; [native education application #1341](https://github.com/rebeckerspecialties/webapp/pull/1341) | Build the real application as a side-by-side ARM64 APK and run it on the Quest. The hosted simulator screenshot is deferred and opt-in. Verify both eyes, the welcome title, Start Story and education playback; a process launch alone is insufficient. |

## Babylon.js

| Patch | Destination/status | Acceptance boundary |
| --- | --- | --- |
| Forward the requested projection layer scale in the WebGPU branch of WebXRLayers | Upstream draft [19214](https://github.com/BabylonJS/Babylon.js/pull/19214). Forwards only scaleFactor, not WebGL format enums. | Default, 0.75 and 1.5 tested at attachment; 450 XR tests passed. |
| Replace the WebGPU XR shell's depth attachment using the existing target setter | Independent upstream draft [19215](https://github.com/BabylonJS/Babylon.js/pull/19215). Releases the superseded shell attachment and updates the stencil-aspect flag. | Two regressions reproduced before the fix; depth-only and no-replacement controls; 451 XR tests passed. Separate from compositor texture teardown and application discard policy. |
| Render pass batching and atomic replay | Already upstream: [19075](https://github.com/BabylonJS/Babylon.js/pull/19075), [19084](https://github.com/BabylonJS/Babylon.js/pull/19084). | Keep the host provider in BabylonNative. Do not re-submit. Check protocol/fallback on package updates. |
| Fast snapshot rendering-group clears | [19196](https://github.com/BabylonJS/Babylon.js/pull/19196) open. | Retain patch 0008 until present in the installed release; do not enable fast snapshot mode on a changing visualization. |
| Uniform comparison length, bundle accessor pass/compression, command-word reservation, shared versioned material UBO, matrix policy hoist, restricted getter expressions | Fork PRs [11](https://github.com/rebeckerspecialties/Babylon.js/pull/11), [12](https://github.com/rebeckerspecialties/Babylon.js/pull/12), [13](https://github.com/rebeckerspecialties/Babylon.js/pull/13), [14](https://github.com/rebeckerspecialties/Babylon.js/pull/14), [15](https://github.com/rebeckerspecialties/Babylon.js/pull/15), [16](https://github.com/rebeckerspecialties/Babylon.js/pull/16), respectively. Patch 0009 (invalidate only existing submeshes) is already upstream as open [19189](https://github.com/BabylonJS/Babylon.js/pull/19189). | Do not infer a V8 win from JSC. Same-frame material mutations, views, subclasses and browser fallback must remain correct. |
| Font metrics, KTX sRGB/mipmaps, SSAO2 world normals, WebXR capability order, GUI3D/particles WGSL | Already upstream: [18463](https://github.com/BabylonJS/Babylon.js/pull/18463), [18538](https://github.com/BabylonJS/Babylon.js/pull/18538), [18539](https://github.com/BabylonJS/Babylon.js/pull/18539), [18711](https://github.com/BabylonJS/Babylon.js/pull/18711), [18460](https://github.com/BabylonJS/Babylon.js/pull/18460). | No local replacement. Fix invalid application WGSL in the app. |
| Session/input source disposal | Upstream [19201](https://github.com/BabylonJS/Babylon.js/pull/19201) and [19202](https://github.com/BabylonJS/Babylon.js/pull/19202) open. | Repeated browser XR entry/exit, distinct from GPU resource retirement in the native backend. |

## BabylonNative upstream and monorepo

| Patch | Destination/status | Acceptance boundary |
| --- | --- | --- |
| Node-API conformance harness | Maintainer draft [2097](https://github.com/BabylonJS/BabylonNative/pull/2097). | Preserve maintainer changes; no parallel harness. |
| Node-API v7, detachable ArrayBuffers and adapter differences | [2100](https://github.com/BabylonJS/BabylonNative/pull/2100), monorepo Dependencies/JsRuntimeHost. | Detach/double detach, mapped-buffer transfer and GC; never pin a JSC backing store just to test its address. |
| Dedicated workers, clone/transfer and shutdown | [2101](https://github.com/BabylonJS/BabylonNative/pull/2101), not Streams or Fetch. | Worker-owned callbacks; exception, termination and in-flight transfer tests on each runtime. |
| Compression/DecompressionStream and stream plumbing | [2103](https://github.com/BabylonJS/BabylonNative/pull/2103), independent of Worker. | Test supported codecs on both realms; malformed/truncated input, cancellation, rejection. Keep actual archive bytes gzip. |
| HTTP status/headers/redirect, transport errors and opt-in HTTP/3 | [2078](https://github.com/BabylonJS/BabylonNative/pull/2078) ports UrlLib work; Android binary response fix [2035](https://github.com/BabylonJS/BabylonNative/pull/2035) already merged. | Loopback server; distinguish HTTP failure responses from transport rejection. Do not replay archived UrlLib wholesale. |
| Headers/Response constructor and Body consumption/clone; arbitrary abort reasons | Separate the remaining semantics in the fork Fetch changes from 2078. Test against the **combined** monorepo heads before publishing a residual patch. | Body lock/disturb/clone, double consumption, exact abort reason and gzip/SPZ stream. No implicit Worker dependency. |
| JNI string access | Compare Dependencies/AndroidExtensions/Source/JavaWrappers.cpp against master: GetStringUTFRegion vs GetStringUTFChars. Archived [AndroidExtensions 15](https://github.com/BabylonJS/AndroidExtensions/pull/15) merged; do not re-port unrelated lifetime changes. | Modified UTF-8, NUL, surrogate pairs, empty strings, pending JNI exceptions. Submit separately only with a demonstrated improvement. |
| Reference space rejection, Android font fallback, Intl-enabled V8 | Existing [2105](https://github.com/BabylonJS/BabylonNative/pull/2105), [2106](https://github.com/BabylonJS/BabylonNative/pull/2106), [2107](https://github.com/BabylonJS/BabylonNative/pull/2107), respectively. | No Vulkan dependency. Paused date must match supported locale behavior; do not globally override Date. |
| Optional JS XRFrame wrapper and correct frame/session ownership; projection extent negotiation | Split general NativeXr lifetime/selection from wgpu image import/resolve. Local bare-WebXR tests exercise both eyes and several scale factors. | No BABYLON global required. Test frame lifetime and repeated sessions; never advertise unsupported reference spaces or frame APIs. |

## Webapp

| Patch | Destination/status | Acceptance boundary |
| --- | --- | --- |
| Common bootstrap and asset readiness before native auto-entry | Webapp main candidate from the native branch. DOM capability detection, packaged education and worker URLs; keep gesture entry in browsers. | Cold start to real two-eye content, not just Android Activity startup. No automatic story click in a production bundle. |
| Valid WGSL emoji texture array sampling | Webapp [1340](https://github.com/rebeckerspecialties/webapp/pull/1340), based on main. | Renderer Jest tests and Chromium WebGPU pixels: red layer 0, green layer 1, blue underlying material with the plugin disabled. Panel layout and native text metrics are separate. |
| Renderer caching, real variant prewarm, archive hydration off-thread, retained buffers | Extend [1322](https://github.com/rebeckerspecialties/webapp/pull/1322). Diff native experiments against its head first. | Independently qualify label geometry reuse, bounded visible-text updates and GPU mip generation. Queue work must not show stale labels. |
| Shared worker transport | [1323](https://github.com/rebeckerspecialties/webapp/pull/1323); preserve transferable/ordinary buffer fallback. | No orphan responses, extra archive copies or main-thread JSON.parse. |
| Quantizer aggregation, playback resilience and tour-end navigation | Existing [1291](https://github.com/rebeckerspecialties/webapp/pull/1291), [1292](https://github.com/rebeckerspecialties/webapp/pull/1292), [1303](https://github.com/rebeckerspecialties/webapp/pull/1303). Renderer culling/depth-order [1293](https://github.com/rebeckerspecialties/webapp/pull/1293) already merged. | Replay through an actual release and calendar date. Do not bury renderer LOD in a playback correctness PR. |
| Navigation benchmark, repro dates, screenshots/optical QA | Preview/integration [1324](https://github.com/rebeckerspecialties/webapp/pull/1324), not production main. | Keep autorun and instrumentation test-only; compare matching dates, eye density, display rate and pose. |
| Geometry cost of release rings, avatar rings and commit cubes | New renderer experiment, not a runtime driver patch. | Measure per-mesh index/instance costs; preserve silhouette, stereoscopy, pick IDs, color and selection. Benchmark at the same date before shipping. |

The tested native webapp still resizes its thin-instance arrays and looks up
child meshes for per-instance updates. The head of webapp #1322 already uses
grow-only capacity, retained mesh references and partial dirty ranges. Replay
that implementation against the same release dates before designing another
native-only upload optimization. It targets moving releases, not the January 10
plateau where no new buffers were created.

## Renderer fork and dependencies

wgpu's TextureBlitter minification sampler fix was already merged in
[10657](https://github.com/gfx-rs/wgpu/pull/10657). Do not resubmit it.
The Quest pass timestamp sampler is optional renderer instrumentation, not an
upstream requirement or a reason to change default WebGPU store operations.

| Patch | Destination/status | Acceptance boundary |
| --- | --- | --- |
| Async WebGPU render/compute compilation | BabylonNative wgpu fork: NativeWebGPU, GraphicsWgpu and compiler tests. One worker per physical renderer; owned inputs, JS-thread publication, isolated failures. | Valid render pixels/compute values, cross-device rejection, late device destroy, no scope/event leaks, caller progress. Babylon opt-in async PSOs is a distinct app/engine choice. |
| Canvas snapshot on shared device/queue, mip reuse | CanvasWgpu/GraphicsWgpu fork; isolate femtovg shared-pipeline patch as a dependency candidate. | Pixel test with nonuniform color/alpha; no CPU canvas readbacks; label updates must survive resource retirement. |
| WebGPU completion, map/detach, timestamp queries | Fork bridge and CTS, apart from monorepo Node-API detach support. | Test errors, destroyed devices and genuine GPU timestamps; leave Stats capture off when closed. |
| OpenXR Vulkan backend, negotiated eye size, color-space resolve and ownership | Fork only for renderer-specific code. Preserve preferred newer OpenXR/Vulkan path and runtime-supported fallback. | Validate each eye and opt-in linear/sRGB paths separately; no inference of physical lens output from the submission count. |
| wgpu-native C parity/surface ownership and C tests | Upstream [630](https://github.com/gfx-rs/wgpu-native/pull/630) and dependent [631](https://github.com/gfx-rs/wgpu-native/pull/631) open. | Rust pin/workspace glue stays in the fork. Direct C tests independently of JS CTS. |
| Adreno Vulkan and SPIR-V code generation | Review overlap with [10979](https://github.com/gfx-rs/wgpu/pull/10979), [10990](https://github.com/gfx-rs/wgpu/pull/10990), [10991](https://github.com/gfx-rs/wgpu/pull/10991). Split empty descriptor layout from instance-flag preservation and build-only workspace paths. | Minimal shader/dispatch fixtures on Adreno plus non-Adreno controls; no driver workarounds in app JS. |
| Apple-only improvements | Metal snorm16 shader [fork 4](https://github.com/rebeckerspecialties/wgpu/pull/4); Metal multi-draw [9640](https://github.com/gfx-rs/wgpu/pull/9640) merged; count variant [9679](https://github.com/gfx-rs/wgpu/pull/9679) open. | Neither a Metal ICB nor a Metal register-spill improvement explains Adreno performance. |
| Android V8 distribution | NativeScript [16](https://github.com/NativeScript/v8-buildscripts/pull/16) open; BabylonNative consumer support is 2107. | Keep optional CPU tuning and ICU data packaging separate from general N-API behavior. |
