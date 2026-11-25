# Quest 1 Vulkan handoff

Start with this file, not the historical timing logs. The renderer is wgpu/Vulkan. Do not enable GLES, a
second graphics device, driver tracing, or camera diagnostics that restart the compositor.

## Known-good inputs

- For current APKs use the BabylonNative gitlink in [Samples draft #1](https://github.com/rebeckerspecialties/babylonnative-webxr-samples/pull/1); do not substitute the latest checkout. The [earlier clean ARM64 artifact](https://github.com/rebeckerspecialties/babylonnative-webxr-samples/actions/runs/45291729947) predates stereo cube arrays and lazy XR snapshots.
- Cube: mirror the *entire* source strip before slicing, not just the order or just the face pixels. The six decoded faces match the original transform exactly.
- Android video: the direct XR media surface played the reference VP8; the old texture path displayed green stripes while the video clock and RGB changed. Read CPU-accessible MediaCodec output images; use plane position/limit, strides, crop and color range. Actual texture samples at 12.5 and 13.5 s matched both source eyes (<0.30/255 mean error, correlation >0.999). Separate final-display markers verified eye routing off/max, including swapped/duplicate controls.
- Quest texture video: 1056 and 1062 XR callbacks per 15 seconds (default/max foveation); source clip is 30 fps. Warp validation is submission of paired motion/depth, not an optical synthetic-frame measurement. Do not transfer these rates to Factotum year two.
- Factotum: [ARM64 qualification change](https://github.com/rebeckerspecialties/webapp/commit/275b5522). Install separately from the user's production package. The normal app opens education through Start Story; automation/replay belongs in tests. Hosted simulator screenshot work is deferred; x86-64 artifacts do not run on Quest.
- Physical qualification sequence: Home + passthrough -> one APK/case -> normal exit -> Home + passthrough. Record APK hash, package, focused activity, first *visible* stereo, menu glyphs, controller and post-exit behavior separately. A callback, process launch or passing package test is not a visible-eye result.
- Clean Samples APK SHA-256: `9a032f2cdca7d95b5f178bdcc1f847afa4553602685a75b36f9ba92647b6e8a9`. Installed the downloaded artifact on Quest 1, ran case 6 alone, and inspected a system display capture: ceiling and hanging lamp above, workbench below in both eyes. After the test runner closed Samples, `com.oculus.shellenv/.ShellEnvActivity` was resumed. Home being resumed does not establish optical passthrough.

## Passthrough relapse

The [hosted Samples APK at 95ede2f](https://github.com/rebeckerspecialties/babylonnative-webxr-samples/actions/runs/45508469215)
passed case 119: 1,435 tracked frames, room visible behind the markers, Home
passthrough working after normal session end (both checked through the lenses).
This run used runtime `be4997de`. Switching directly between two active apps
is a different lifecycle case and has left the previous app on the display.

The owner confirmed a browser AR sample still displayed real passthrough on the same headset while Home passthrough stayed black. Treat Home/Guardian presentation as a separate path: this control is inconsistent with global camera unavailability. Do not reset tracking, clear Guardian or reboot before comparing those paths.

After reboot, the user confirmed Home and tracking recovered. After subsequent runs the user again saw broken passthrough. During this second failure Home was resumed, neither test app was running, and CAMERA app-ops showed no operations for Samples or either Factotum package. Stock Quest does not expose Android's media.camera service. Vendor tracking remained in 6DoF with changing exposure on all four cameras, zero reported camera issues per minute, and intermittent skipped map-tracking updates. No contemporaneous GPU fault was identified. This excludes a surviving test process as the camera owner at that point; it does not identify which system session or presentation transition failed.
- The ordinary system log at 15:45:42 shows Samples completing both `xrEndSession` and `xrDestroySession`; Guardian reported zero client references, disconnection and removal of the active client. At 15:45:44 it received Home's focus and primary-package events. Inspect events *after* that handoff as well as interrupted session exits. No system tracing or service restart is needed to collect these events.
- [ ] Establish Home-only control after reboot; compare one normal session exit with background/stop. Inspect vendor service ownership and session transition logs before resetting anything. Do not kill system services or clear Guardian data.
- [ ] Isolate plain projection, static layer, texture video and direct media in separate runs. Test physical passthrough after each, then warp/foveation only if baseline passes.
- [ ] Use screenshots for app content, not proof of real passthrough. Check system passthrough in the lenses.

## Factotum acceptance

**Education playback:** `native/ci/quest-education.py SERIAL PACKAGE OUT` passed on
the installed `com.rebecker.factotum.openingcheck` build: May 1 2024, 77 cubes,
five rings, tour7 narration and changing display pixels in both eyes (2.76%/2.74%).
The earlier [qualification artifact](https://github.com/rebeckerspecialties/webapp/actions/runs/45293795788)
failed with a black left eye. Qualify each new APK independently, including normal Back.

**Factotum lifecycle (failed).** A normal Android Back from the active Release session resumed Home but also produced SIGSEGV. The separate debug comparator showed the same path: `NativeXr::Impl::SessionState` released N-API references through `napi_delete_reference` / `napi_env__::DequeueFinalizer` during graphics device destruction. The embedding destroys AppRuntime before the device; a frame/task can still retain XR state. Check teardown of a live session without an explicit JS end, including a queued RAF, vs explicit `session.end()` before exit. Release JS-owned references on their owning runtime while still alive, and verify repeated Back/re-entry without a native crash. This failure does not establish the cause of Home-only black passthrough.

## Upstream runtime audit

- [N-API v7 #2100](https://github.com/BabylonJS/BabylonNative/pull/2100), audited `5f04b149`: JSC length-only queries avoid requesting the native byte pointer; detach verifies its postcondition and refreshes wrapper metadata. `DetachNeverReportsSuccessForPinnedBuffer`, `ArrayBufferWrapperRefreshesInfoAfterDetach`, fallback/empty buffer and V8 finalizer tests are present. NativeWebGPU's actual GPUBuffer range ownership/unmap/reattach tests remain renderer tests. Keep [Intl-enabled Android V8 #2107](https://github.com/BabylonJS/BabylonNative/pull/2107) separate.
- [Workers #2101](https://github.com/BabylonJS/BabylonNative/pull/2101), audited `2b0d9d51`: parent teardown stress, same-task close ordering, retained scheduler, structured-message transfer and failed-getter/no-detach tests exist. Android's WPT runner installs a custom `Options.ScriptResolver`. Its default `LoadSource(app:...)` uses a filesystem path; the tested fork instead reads APK assets in the worker ([cfd12e0e](https://github.com/rebeckerspecialties/BabylonNative/commit/cfd12e0e)). Do not replace the upstream resolver/close/error work wholesale. Also absent at this head: fork's global same-realm `structuredClone` and its cyclic/alias/invalid-date/getter/transfer tests ([ff30a3a7](https://github.com/rebeckerspecialties/BabylonNative/commit/ff30a3a7)); distinguish it from postMessage cloning. No direct structuredClone call was found in the checked Factotum visualization/libs sources.
- Retested the Node Lite `test/js-native-api/test_typedarray/test.js` addon on physical Quest V8 at #2100's head, including detachment of every typed-array kind; exit 0. Pass the absolute path to the JS entry in this Android harness. Test JSC pointer pinning on JSC, not with this V8 executable.
- Worker #2101 implements transfer semantics, not zero-copy transport. Its JS serializer snapshots with `buffer.slice(0)`; `Serialize` copies into a native vector and `Deserialize` copies into the receiving realm. Do not remove the JSC snapshot without preserving detachment and lifetime rules. Benchmark archive-size transfers and peak memory against [webapp #1323](https://github.com/rebeckerspecialties/webapp/pull/1323) before changing the portable fallback.
- [ ] Add a production-default packaged Worker entry plus nested relative import, missing asset and explicit-resolver override test. Upstream documents that an embedder can supply a resolver; either wire it in the production Android embedding or provide an Android-only default while preserving explicit resolver precedence. Distinguish real default APK behavior from extracted-test-directory or injected-resolver success. Keep [Streams #2103](https://github.com/BabylonJS/BabylonNative/pull/2103), [UrlLib #2078](https://github.com/BabylonJS/BabylonNative/pull/2078), [N-API harness #2097](https://github.com/BabylonJS/BabylonNative/pull/2097) and Vulkan out of the Worker diff.
- Fork Native #28/#29 and upstream #2100/#2101 use the same code branches. Do not commit fork-only handoff files to a shared upstream head. Link this maintained handoff in the fork PR description instead.

## Review index

Per-PR ownership, implementation reminders and physical acceptance criteria. Consult the linked PR for its latest status.

| Fork PR | Scope | Next implementation check | Next device check |
|---|---|---|---|
| [BabylonNative #28](https://github.com/rebeckerspecialties/BabylonNative/pull/28) | N-API v7: JSC detached-buffer metadata and pinned-pointer postcondition are present on upstream #2100. | Keep renderer-specific mapping ownership out of Node-API; reconcile V8 14 support with #2107. | Run detach, zero-length, finalizer and transfer failures on the exact adapter heads; run mapped write/unmap in NativeWebGPU separately. |
| [BabylonNative #29](https://github.com/rebeckerspecialties/BabylonNative/pull/29) | Dedicated Workers: distinguish production APK assets from the Android WPT custom ScriptResolver. | Supply and pin a default APK asset path for app:/// entry and relative importScripts; compare same-realm structuredClone with the fork. Preserve parent-realm FIFO shutdown/error delivery. | Without an injected resolver, load nested APK scripts, transfer real bytes, terminate during delivery, and replay education then react-native. |
| [BabylonNative #30](https://github.com/rebeckerspecialties/BabylonNative/pull/30) | XRRigidTransform: portable dictionary defaults; keep independent of the Vulkan renderer. | Keep defaults independent for omitted translation/orientation and cover invalid and partial dictionaries. | Verify identical left/right poses under translation-only and rotation-only transforms, followed by session re-entry. |
| [BabylonNative #31](https://github.com/rebeckerspecialties/BabylonNative/pull/31) | Reference spaces: test offsets using a moving viewer, not just stationary identity transforms. | Check nested offsets, ray direction and poses after recentering; avoid applying a view transform twice. | Test panning and vertical translation on a tracking headset; validate hit/anchor on a device that actually supports them. |
| [BabylonNative #32](https://github.com/rebeckerspecialties/BabylonNative/pull/32) | XR animation timing and end events must follow session lifecycle, not request-handle numbering. | Audit cancellation, repeated sessions and exactly-once end delivery with queued callbacks. | Run enter/end/re-enter, background/resume and normal Home handoff; check the clock stays monotonic. |
| [BabylonNative #33](https://github.com/rebeckerspecialties/BabylonNative/pull/33) | Session negotiation: reject unavailable required features, omit unavailable optional ones. | Keep enabledFeatures matched to granted runtime capabilities, including negative paths on other backends. | Test unknown required/optional features, unsupported immersive-ar on Quest, and retry a valid session. |
| [BabylonNative #34](https://github.com/rebeckerspecialties/BabylonNative/pull/34) | Keep direct media on the compositor surface; texture uploads need CPU-readable MediaCodec planes, not decoded vendor surface storage. | Cover looping, crop/stride, pause/resume and loss of the decoder; keep scope independent of XR eye geometry. | Read both packed texture halves against FFmpeg; run Samples 10/36 and tagged display 105/106; check Home passthrough after exit. |
| [BabylonNative #35](https://github.com/rebeckerspecialties/BabylonNative/pull/35) | Projection density, motion and depth are separate from media-layer eye routing. A successful endFrame alone is insufficient. | Audit end-session swapchain/surface retirement and interrupted media cleanup; keep unsupported extension behavior explicit. | Repeat off/max/off density and 102-106, including a cold Home/Guardian/passthrough check after each individual case. |
| [wgpu #5](https://github.com/rebeckerspecialties/wgpu/pull/5) | Vulkan fragment-density attachments: preserve ordinary render targets and target-handle portability. | Keep attachment lifetime, feature fallback and targets without atomic u64 covered; do not assume every adapter accepts density images. | Compare peripheral/center raster in both eyes at off/max/off and rerun a non-density Vulkan control. |
| [babylonnative-webxr-samples #1](https://github.com/rebeckerspecialties/babylonnative-webxr-samples/pull/1) | Samples 6/10/36 qualify atlas orientation and actual movie content; 105/106 distinguish eye identity after composition. | Keep hosted APK asset checks, independent FFmpeg oracle and swapped-eye controls; investigate the Home passthrough relapse independently of successful layer output. | Install the clean artifact; verify cube floor/ceiling, movie and eye markers, normal exit and Home passthrough before the next case. |
| [webapp #1340](https://github.com/rebeckerspecialties/webapp/pull/1340) | Emoji WebGPU texture arrays require the array-layer operand, independent of the renderer's normal maps. | Keep WGSL index distinct from mip level and avoid changing the WebGL path. | Verify layer 0/layer 1 and disabled-plugin pixels; inspect commit-cube tops in stereo during playback. |
| [webapp #1341](https://github.com/rebeckerspecialties/webapp/pull/1341) | Default qualification is a side-by-side ARM64 APK. Hosted simulator screenshots are deferred; never replace the user's main install. | Preserve compressed education data, all opening assets and the real Start Story action. Keep the emulator gate opt-in. | Cold-start the exact APK, verify menu in both eyes, start education, pause, navigate to react-native and exit to a functioning Home. |
| [webapp #1342](https://github.com/rebeckerspecialties/webapp/pull/1342) | Use valid WGSL in Factotum material/skill-ring plugins; keep web and native shader semantics aligned. | Keep per-variant syntax checks and distinguish shader correctness from geometry LOD or filtering. | Compare per-eye ring edges, alpha ordering, material colors and cube shimmer at the same playback date. |
| [webapp #1324](https://github.com/rebeckerspecialties/webapp/pull/1324) | Integration preview is for matched baseline playback, not a substitute for production bundle qualification. | Keep automatic input and diagnostics test-only; track source branches separately. | Compare Quest Browser 112 and native at the same education and year-two react-native checkpoints, then verify Home after exit. |
| [webapp #1323](https://github.com/rebeckerspecialties/webapp/pull/1323) | Shared worker transport must retain a working transferable fallback. | Verify ownership, error paths, teardown and absence of duplicate copies before enabling shared storage. | Replay large archives across repeated navigation; measure stalls and memory and check that no requests are orphaned. |
| [webapp #1322](https://github.com/rebeckerspecialties/webapp/pull/1322) | Retained render data/prewarm addresses navigation work, not every GPU-bound playback frame. | Check partial ranges, growth and stale-label invalidation against real archive transitions. | Compare cold menu, first release and year-two playback at fixed eye resolution; inspect label centering during navigation. |
| [webapp #1320](https://github.com/rebeckerspecialties/webapp/pull/1320) | Animated issue and PR cubes need a visible, stable reaction on release. | Check asset/shader variant selection and animated to idle transitions without changing scene-wide AA. | Inspect cube tops, dependency labels and release rings in both eyes before and after a release. |
| [Babylon.js #17](https://github.com/rebeckerspecialties/Babylon.js/pull/17) | Fast snapshot rendering group clears must preserve visible group ordering. | Verify replay and non-snapshot clear behavior against the same recorded clear flags. | Test dialog/cube transparency and spectator output with snapshot rendering on and off. |
| [Babylon.js #16](https://github.com/rebeckerspecialties/Babylon.js/pull/16) | Only inline accessor expressions when receiver identity and evaluation count are preserved. | Pin impure getters, overridden accessors and changed receivers; retain unchanged bundle fallback. | Run moving-camera release A/B on interpreter-bound hardware; check same-frame and multi-view material changes. |
| [Babylon.js #15](https://github.com/rebeckerspecialties/Babylon.js/pull/15) | Uniform matrix policy is per update, not a frame-wide constant. | Pin same-frame policy changes and preserve behavior when the receiver or matrix changes. | A/B uninstrumented moving-camera runs and verify both views see updated transforms. |
| [Babylon.js #14](https://github.com/rebeckerspecialties/Babylon.js/pull/14) | Versioned material UBOs must remain correct across views and same-frame changes. | Cover invalidation, effect layout differences and disposal; do not share per-view or per-mesh state as material state. | Compare moving car reflections, sky, under-car shading and stereo materials under same-frame mutation. |
| [Babylon.js #13](https://github.com/rebeckerspecialties/Babylon.js/pull/13) | Reserve command storage without changing opcode layout, order or fallback semantics. | Keep exact word counts, capacity growth and rejected pass behavior covered. | Compare recorded/direct pixel results and bridge traversals at identical draw counts. |
| [Babylon.js #12](https://github.com/rebeckerspecialties/Babylon.js/pull/12) | Bundle specialization is opt-in and cannot assume arbitrary Babylon config is invariant. | Pin fallback behavior and overrides; fail if required transforms silently do not run. | Compare stock/specialized bundles with matched thermals, moving camera and no per-call probes. |
| [Babylon.js #11](https://github.com/rebeckerspecialties/Babylon.js/pull/11) | Uniform comparison length caching must preserve zero-length updates and immediate same-frame writes. | Retain regression tests for dirty ranges and mutation; distinguish CPU-side shadow data from depth comparison sampling. | A/B with identical visuals and report sustained JS CPU, not just FPS. |
| [Babylon.js #2](https://github.com/rebeckerspecialties/Babylon.js/pull/2) | Render-command recorder support has moved upstream; use the published optional recorder contract. | Compare current upstream atomic replay and fallback before carrying remaining patches. | Exercise with provider absent, present and rejected; compare pixels, call counts and scene timing. |
| [Babylon.js #1](https://github.com/rebeckerspecialties/Babylon.js/pull/1) | Thin-instance buffers affect release animation cost, not static menu fill rate. | Compare against retained/partial writes in webapp #1322; preserve instance picking, bounds and update order. | Measure the first releases and the same year-two checkpoint in both eyes, including buffer churn. |
| [wgpu #4](https://github.com/rebeckerspecialties/wgpu/pull/4) | Apple Metal signed-normalized vertex conversion and register pressure are separate from Quest's Vulkan results. | Preserve format semantics and retest regenerated Metal output and controls before upstreaming. | Check matching pixels and actual register pressure at matched thermals on the XS and iPhone 12. |
| [femtovg #10](https://github.com/rebeckerspecialties/femtovg/pull/10) | Canvas rendering cost and clipping are distinct from XR eye resolution. | Preserve nested clip/layer coverage and compare recording allocations without a GPU readback. | Inspect full-resolution label and icon edges in both eyes and check memory through modal reopen. |
| [femtovg #11](https://github.com/rebeckerspecialties/femtovg/pull/11) | Released transient images must survive queued drawing and flush. | Pin delayed flush, reuse and shutdown ordering while keeping the image lifetime bounded. | Open/close text-heavy modals repeatedly and verify pixels after resource reuse. |
| [femtovg #12](https://github.com/rebeckerspecialties/femtovg/pull/12) | Correct contour holes before attributing missing glyph details to XR downsampling. | Verify nested/touching contour cases; pair accuracy and performance checks rather than tuning screenshot thresholds. | Compare text counters and glyph holes at full native label resolution before stereo downsample. |

### Historical runtime and transport branches

| Fork PR | Scope | Next implementation check | Next device check |
|---|---|---|---|
| [JsRuntimeHost #4](https://github.com/rebeckerspecialties/JsRuntimeHost/pull/4) | Historical v7 port. The maintained monorepo adapter is upstream BabylonNative #2100; do not reapply this tree wholesale. | Diff detach and pinned-pointer behavior against #2100 rather than rebasing an archived runtime. | Run the Quest Node Lite typed-array fixture and run the pinned-byte-pointer case separately on iOS system JSC. |
| [JsRuntimeHost #8](https://github.com/rebeckerspecialties/JsRuntimeHost/pull/8) | Historical Workers implementation; compare with BabylonNative #2101, not with the XR renderer. | Keep production Android APK resolution separate from host filesystem roots and from a test-only resolver. | Load an APK Worker entry and its relative import without injection; ensure pending messages drain on shutdown. |
| [JsRuntimeHost #9](https://github.com/rebeckerspecialties/JsRuntimeHost/pull/9) | The in-memory IndexedDB facility is not persistent browser storage. Factotum reports IndexedDB availability at startup. | Confirm which opening and navigation code paths require persistence; keep IndexedDB separate from Worker and N-API. | Cold start a fresh qualification package, navigate twice, then relaunch and compare application storage behavior with the browser. |
| [JsRuntimeHost #10](https://github.com/rebeckerspecialties/JsRuntimeHost/pull/10) | QuickJS exception state affects Worker failure reporting independently of message delivery. | Compare pending-exception status, error message and stack preservation with the monorepo adapter before porting. | Run the worker failed-getter and missing-script fixtures on QuickJS and verify the parent stays usable. |
| [JsRuntimeHost #11](https://github.com/rebeckerspecialties/JsRuntimeHost/pull/11) | Use the maintained BabylonNative conformance port (#2097) for new cases; this is a historical CI twin. | Keep relevant regressions and explicit per-engine skips; do not duplicate the maintained harness. | Run the detach and typed-array subsets on Quest V8, then run pointer-pinning on physical system JSC. |
| [JsRuntimeHost #12](https://github.com/rebeckerspecialties/JsRuntimeHost/pull/12) | Android addons and the embedding must see the same runtime and N-API shared library. | Check DT_NEEDED and packaged native library ownership in the final APK; do not link a second adapter. | Load a real Android addon on the Quest; test detachment and shutdown on the same adapter instance. |
| [UrlLib #2](https://github.com/rebeckerspecialties/UrlLib/pull/2) | Historical transport error and CI mirror. The monorepo transport review is BabylonNative #2078. | Keep HTTP status distinct from transport failure and avoid porting CI-only commits into production code. | Check packaged and HTTP archives independently, including truncated bodies and an offline fetch on the Quest. |
| [UrlLib #3](https://github.com/rebeckerspecialties/UrlLib/pull/3) | HTTP/2 and optional HTTP/3 are separate from Worker APK asset resolution. | Test protocol negotiation and fallback without turning HTTP/3 into a requirement for local app URLs. | Verify a real HTTPS response, an offline failure, and packaged education on an unmodified transport configuration. |

See the [patch inventory](FactotumQuestPatchInventory.md) for upstream equivalents, historical playback PRs, and dispositions. Do not update upstream PRs with fork-specific qualification notes.
## Projection and opening display regressions

Factotum's two-view startup log and a submitted OpenXR frame both occurred with the entire left eye black. Its sky disables color clears. On Babylon.js 9.10.1 the camera output-target path then omitted the requested depth/stencil clear for both eyes. Independent depth clears restored the textured room and UI to both halves of a Quest display capture, with the original color and swapchain settings. Use the physical display gate from [the application draft](https://github.com/rebeckerspecialties/webapp/pull/1341); a mock framebuffer bind or pooled menu OCR cannot detect this.

For a background layer, the samples submit the image or film *before* the transparent controller projection. Native OpenXR honors the projection's `blendTextureSourceAlpha`; it must not reorder an application's layers. The [sample draft](https://github.com/rebeckerspecialties/babylonnative-webxr-samples/pull/1) has physical display controls 107/108: red in the left eye, blue in the right, panorama visible with source alpha, black around the markers without it. Stop other test apps before capture: an older focused Factotum frame can otherwise appear even when Samples logs successful submissions.

## Android movie and JS hitches

For a separate timing diagnosis, set `adb shell setprop debug.babylon.xr_timing 1`
before creating the session, capture ordinary logcat, then set the property to
`0` and restart before qualification. `XR predicted gap` splits the previous
frame into eye preparation, JS queue delay, callback (including native calls),
time after JS, and frame submission; `wait` belongs to the *next* frame.
These are elapsed intervals, not CPU samples or GPU timestamps. On the Quad
image with controller/HUD, three of five missed refreshes included 16–22 ms
inside the JS task; the other two included 4.5–7.3 ms of JS queue delay.
To correlate GC, enable `debug.babylon.gc=1` for a separate run, retain
`BabylonNative` and `BabylonGC` logcat tags with threadtime timestamps, and run
`node Apps/scripts/summarizeQuestFrameGaps.mjs LOG APP_PID`. Disable both flags
and restart before timing. A 24/72 direct VP9 run had nine steady render-isolate
collections but none overlapping its four late callbacks or dispatch waits.
The decoder wrote to the compositor Surface; there were zero app movie uploads.
Do not launch Factotum over a still-active Samples
session for the education check: Android can resume Factotum and advance the
tour while the display continues showing Samples. Test normal app handoff
separately from each app's isolated performance.

On Quest 1 the V8 sampler attributed 19.6 MB/25 s to the stereo projection clear
callback and 9.7 MB to its caller; separate timing runs used no sampler. Render-pass
and command-encoder methods belong on the receiver's prototype, not each instance.
Keep native pose storage independent per frame; materialize JS matrices only on access.
For 45-second texture/direct comparisons run Samples 111-114; read steady missed
refreshes and longest clean interval, not just headline FPS. See the
[raw and interactive device results](https://github.com/rebeckerspecialties/babylonnative-webxr-samples/blob/b812f88/qualification/quest-1/pacing.md);
the interactive measurements include the controller projection and FPS HUD. See the
[decoder import probe](../Core/GraphicsWgpu/Tests/AndroidDecoderImport.md) before
changing the production upload path. For iOS, ARKit currently grants immersive AR,
not the stereo VR presentation required by the Quest applications.

Quest 1, 960x1080 VP8, same 72 Hz layer test alternating texture and direct media twice:
copying 4,147,200 decoded bytes into a new JS ArrayBuffer for every texture frame gave
61.5/57.4 app FPS; lending the decoder front buffer synchronously gave 68.9/68.8.
The V8 render-thread collections longer than 1 ms fell from 257/266 per test to 9/5.
Direct media remained 71.1-71.5 FPS. Prefer the Android compositor Surface for
presentation: VP8 and hardware VP9 Profile 0 need no app pixel upload. Quest 1
did not decode the 10-bit Profile 2 control. Its actual decoder buffers lacked
Vulkan's external-format sampled-image bit. The WebGPU texture fallback still
converts YUV and writes a texture; do not expose the native span past the call.
Check real left/right pixels against the timed VP8 reference after changes.

Factotum uses a different V8 build (Intl, linked into its N-API library).
One opening-menu major collection took 47 ms; the JS wait was 46 ms and the
next headset reporting windows contained 12 stale frames. Other education
stalls occurred without a contemporaneous JS collection. A common 4-second
GC timer has not been established. A separate 307 ms major GC in the settled
menu blocked the waiting XR task for 300 ms; the headset then reported 29/60
FPS with 7.9 ms app GPU time. Never replace its V8 adapter with the one
from Samples when profiling; verify DT_NEEDED for the actual installed library.

To identify JS allocation producers, set `debug.babylon.alloc=1` before a
diagnostic Samples launch. V8 samples allocations (including collected objects)
for 30 seconds starting 35 seconds after isolate startup. Read `BabylonAlloc`
in logcat for that PID/TID; check that the first menu was visible throughout.
Bytes are statistical estimates by JS call stack, including allocations made
inside native calls. They are neither retained size nor GPU memory. Reset the
property to 0 and relaunch before timing. No driver layer or debugger is needed.

Silent texture movies have a second requirement: MediaPlayer on Quest Android
does not advance a playback clock without an audio track or attached Surface.
Drive the independent texture decoder from monotonic playing time in that case
only. Preserve the MediaPlayer position for audio-bearing clips and direct
compositor surfaces. Samples cases 109/110 run a real, silent 24 fps stereo
remux at 72 Hz; verify actual left/right source content, not just currentTime.
