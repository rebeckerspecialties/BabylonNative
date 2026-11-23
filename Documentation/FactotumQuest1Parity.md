# Factotum education on Quest 1

Reference state: the public education visualization in Quest Browser 112, after Enter Factotum.
Native target: the same education assets, V8, WebGPU on wgpu Vulkan, OpenXR stereo.
Measurements below were made on Quest 1 on 2026-11-21. Normal application and compositor
logs suffice. Do not enable the detailed Adreno profiler or restart the browser for comparison.

## Immersive desk replay, 9.37.1

Quest 1, release Vulkan build, automatic Start Story, followed by selecting
facebook/react-native and confirming Skip Tutorial. The table uses the 20 one-second
VrApi windows between the warm-menu marker and Start Story, filtered by the app PID.
Each row is one run; minimum includes the initial ramp. Both swapchains are single-sampled.
The browser menu had a separate 60 Hz receipt; its 2048 x 2263 eye dimensions were
measured in a different 72 Hz session. Do not combine those into one measurement.

| Native configuration | Eye pixels | Warm app FPS, median (range) | Evidence under build_quest_factotum_validation |
| --- | --- | --- | --- |
| 60 Hz, forced Babylon GPU timer | 1216 x 1344 | 43.5 (26-49) | quest1121-937-rate-contract.log |
| Extra Android render tick, otherwise identical | 1216 x 1344 | 44.5 (26-48) | quest1121-937-paced-control.log |
| Local graph clone, original cadence, forced timer | 1216 x 1344 | 45.5 (26-50) | quest1121-937-local-graph.log |
| Same, asynchronous GPU map and queue completion, forced timer | 1216 x 1344 | 56 (29-60) | quest1121-937-async-mapping.log |
| Same clone, ordinary Stats behavior (closed) | 1216 x 1344 | 59 (27-60) | quest1121-937-ordinary-stats.log |
| Same, browser-size validation swapchains | 2048 x 2263 | 19 (15-20) | quest1121-937-browser-density.log |
| Asset readiness before XR entry, ordinary Stats | 1216 x 1344 | 58.5 (36-61) | quest1121-937-prewarm.log |
| 4-sample MSAA resolve to each single-sampled OpenXR image | 1216 x 1344 | 30 (18-32) | quest1121-937-xr-msaa4.log |
| 72 Hz, ordinary Stats | 1216 x 1344 | 42 (29-49) | quest1121-937-ordinary-stats-72.log |
| 72 Hz, browser-size sRGB images | 2048 x 2263 | 9.5 (6-10) | quest1121-937-browser-density-72.log |
| Same, scene draws disabled during the menu only | 2048 x 2263 | 15.5 (6-17) | quest1121-937-browser-density-clear-72.log |
| One shared two-slice sRGB image, per-eye draws | 2048 x 2263 | 10 (6-11) | quest1121-937-dense-array-72.log |
| Immutable sRGB destination, GPU copy of encoded bytes | 2048 x 2263 | 10 (8-10) | quest1121-937-dense-copy-72.log |
| Immutable UNORM, no color conversion (incorrect color) | 2048 x 2263 | 37 (27-43) | quest1121-937-dense-formats-unorm.log |

At the same 72 Hz target and eye dimensions the recorded browser application delivered
34-37 FPS, not 72. Its WebXR viewports and layer indices were observed separately for
each eye. In the native array test frames 8 and 24 had 16/16 opaque samples on both
slices, with respectively 16 and 15 corresponding pixels different between eyes.
The Quest did not advertise `XR_KHR_vulkan_swapchain_format_list`. Repeating the original
format choice twice gave 10 FPS each time. The Android compositor capture also showed a
partial room overlay during the desk comparison. Do not equate this with a worn run.

A display patch with input `(0.18, 0.5, 0.8, 1)` differentiated speed from color accuracy.
In an unobscured region of the left lens the sRGB reference and immutable sRGB copy
both produced `(45,132,202)`; untreated UNORM produced `(121,190,228)`. Removing the
mutable flag alone did not restore performance. Raw UNORM is not a replacement.

On V8 14, a GPU pass that sampled the encoded rendering through an sRGB view and
wrote linear values to a 2048 x 2263 compositor image restored the display patch:
linear UNORM8 gave (46,132,202) at 24 FPS (23-27); RGBA16Float gave (45,132,202)
at 19 FPS (18-19). UNORM8 also reduces precision near black. Neither was enabled.
Evidence: `quest1121-v814-linear-{8,16}-{performance,color}.log`.

The browser's WebXR eye texture extent is known; the headset compositor's underlying
browser swapchain extent is not. A second experiment kept both app color **and depth**
at 2048 x 2263 but resolved each eye into the runtime's recommended
1216 x 1344 sRGB swapchain. Both app textures and both output extents were logged
in the same run. The display patch matched exactly, and the 20 warm-menu windows
were 28 FPS (26-30). This is supersampling, not a 2048 x 2263 final presentation.
The capture still includes a partial room overlay; sharpness and physical right-eye
continuity need a matched view. Evidence: `quest1121-v814-scale-resolve.log`
and its `-begins.png` / `-check.png` compositor captures.

The pinned wgpu `TextureBlitterBuilder::sample_type(Linear)` sets only the
magnification filter. These images are minified: the original control took
nearest samples. Do not use its solid color patch as an edge-sharpness test.
The following 72 Hz Quest 1 controls all rendered 2048 x 2263 per eye and
presented to separate 1216 x 1344 images. Deliberately changing a surface
descriptor in these APKs was for qualification, not a portable configuration.

| FP16 presentation control | Warm menu FPS, median (range) | Receipt suffix |
| --- | ---: | --- |
| Original application texture formats, nearest | 30 (23-32) | scale-fp16 |
| Respect application-declared view formats, nearest | 34 (28-36) | scale-declared |
| Clear instead of loading the overwritten destination, nearest | 35 (24-36) | scale-clear |
| Same, hardware linear minification of mutable sRGB source | 26 (25-30) | scale-filter |
| Immutable UNORM source, decode each of four texels before bilinear interpolation | 36 (35-44) | scale-manual |
| Same last configuration with ordinary education, no color diagnostic | 36 (35-43) | full-filtered |

The last shader preserved alpha and clamped each edge tap; the display patch
was (46,132,201), within one code of the sRGB reference on each channel. A
direct full-size sRGB swapchain with the declared-view fix still achieved only
11 FPS (9-11). The browser ran at 35.97 FPS in its centered menu. The native
desk captures include Guardian passthrough and the menu was off-axis: these runs
do not establish matched optical sharpness or continuity through a worn lens.
Evidence prefix: `quest1121-v814-`.

### Projection scale and cold entry

The Android runtime now sizes the app's color and depth images from the active
projection layer's requested scale, independently of the compositor image size.
The same aspect-preserving clamp applies to both eyes, using the runtime's
reported limits and the negotiated graphics device limit. Linear FP16 compositor
images are opt-in; the default remains sRGB where supported.

On the physical Quest, `webxr_projection_scale_checks.js` ran **without a Babylon
bundle**. Previously session creation failed with `Invalid argument` because the
plugin assumed Babylon had registered its optional JS frame wrapper. Both the
FP16 and default sRGB configurations passed 20 frames, at scales 1, 0.5, 1.25,
and 1 again, checking frame.session, color, depth, viewport and layer size for
both views. `webgpu_xr_eye_resolve_checks.js` ran the runtime's actual WGSL
against a CPU reference on Adreno. All channels in five FP16 images matched,
including alpha, sRGB threshold values, clamped edges, thin images and upscaling.
Receipts: `layer-{scale,srgb}-sequence.log` and `layer-scale-resolve.log`.

Two full education replays at 72 Hz requested 2048 x 2263 color per eye and
presented at 1216 x 1344. Warm menu medians were 36 and 37 FPS. Four standard
compositor captures, started 1, 4, 8 and 13 seconds after focus, showed both
eyes clearing and then drawing the floor and sky. Guardian passthrough covered
part of each eye; inspect the unoccluded areas, not the brightness of the room.
The menu was not centered and the samples do not exclude intermittent dropouts.
Receipts: `layer-scale-playback.log`, `wrapped-startup.log` and
`wrapped-startup-{1,4,8,13}s.png`.

The 9.37.1 Babylon WebGPU Layers path constructs a default projection dictionary
and drops the application's `projectionLayerInit.scaleFactor`. The prepared
comparison forwards **only** that shared member to XRGPUBinding; it does not
pass WebGL numeric color/depth enums to WebGPU. The app selected the density
using its existing framebuffer-scale setting. No headset-specific size belongs
in NativeXR. At that density the app spent 12.41 s preparing the opening scene
before requesting immersion. Focus to settled menu was 1.58 s instead of
10.41 s for immediate entry. Warm median remained 36 FPS. This moves work out
of head-tracked presentation; it does not remove 12 seconds of startup work.
The first and eighth real dependency labels took 2.37 and 2.78 s after selection;
the longest navigation frame was 488 ms. Receipt: `layer-scale-prepared.log`.

In a separate first/repeat selection test the worst frame for react-native,
react and react-native again was 503, 597 and 469 ms. The first and repeat
react-native frames each created four render pipelines, 177 buffers and one GPU
Canvas snapshot; neither read back Canvas pixels. These are allocations within
the interval, not individual compilation timings. Receipt: `layer-scale-nav-revisits.log`.

The navigation inventory found two scene graphs (the main renderer and its
comparison clone), followed by 39 new text planes and 40 label materials.
The latter were an attractive but insufficient explanation for the long frame:

| Same source-built opening scene, 72 Hz | Warm median FPS | Worst navigation frame | First / eighth label |
| --- | ---: | ---: | ---: |
| Control | 36 | 486 ms | 2241 / 2665 ms |
| Defer label material and geometry to the first paint | 37 | 483 ms | 2260 / 2676 ms |
| Enable Babylon async pipeline creation at selection | 36 | 351 ms | 2213 / 2764 ms |

The async run's four-pipeline frame took 344 ms, followed by a 177 ms frame.
NativeWebGPU dispatches the creation callback on the JS runtime thread. Moving
compilation off-thread requires taking owned copies of the shader and layout
handles before dispatch: a compiler worker must not access the renderer's
mutable resource table. Also preserve logical device lifetime and reject pipeline
errors through the promise, not through a different device's error scope. The
inspection build does not enable draw omission or deferred label materials.
Receipts: `layer-scale-inventory.log` and `layer-scale-source-{prepared,late-labels,async}.log`.

**Inspection release:** `quest1121-v814-immersive-inspection.apk`, SHA-256
`c2abd7c313c0400036d288e5bdb424bdf4fd5d470d1f649d98fccbe6b7b747a5`
on both host and headset. It includes the rebuilt Factotum opening component,
uses the app's framebuffer-scale query, starts education automatically and
does not drive the repository selector. With normal logging and Android GPU
debug layers off, focus to welcome was 2.00 s, followed by `tour0.aac` at
1.91 s after automatic Start Story. `quest1121-v814-immersive-inspection.log`
contains the release run. Keep its 30-window menu measurement separate from
the fixed 20-window A/B above.

* The session exposed 60 and 72 Hz, rejected an unsupported request, and reported
  nominal 60 after the application's normal request. OpenXR and VrApi agreed.
* The extra Android tick was removed. The full-size test did not change the default
  projection allocation. Median reported GPU utilization rose from 72% to 89% and
  compositor timewarp from 3.28 to 9.82 ms at the larger size. FPS at the smaller
  size does not qualify the larger size.
* GPU timer readback and Canvas uploads are different paths. At navigation start the
  ordinary run counted 28 GPU Canvas snapshots, zero CPU snapshots, and 2,919
  command-stream batches. No indirect multi-draw was issued. A separate Quest
  pixel test counted 164 GPU snapshots and zero CPU snapshots; a reused label
  matched freshly rendered glyphs after replacing long text with short text.
* In the local-graph clone pair, worker hydration fell from 2.63 to 1.54 s and
  main-realm hydration from 6.82 to 5.33 s. The two start concurrently, with
  distinct in-memory databases. The first eight react-native dependency labels
  were submitted 2.83 s after selection (1.76 s after confirmation). With Stats
  closed these were 2.72 s after selection, not 24 s. A 1.97 s frame gap remained.
  A submitted dependency label is not a physical inspection or a release animation.
* In the cold-eye attachment check the fixed build produced 16/16 opaque samples
  in each eye on frame one; the earlier right eye was 0/16. Confirm physical
  continuity after startup and navigation separately from attachment initialization.
* The timer path previously blocked the JavaScript thread while polling the physical
  GPU queue for readback. With asynchronous completion, the timer advanced across
  1,599 samples. The Quest also passed all 33 cases (328 subcases) in the unmodified
  CTS mapping selection, pin `32cc9f03ad14135b5f6f833dba83e7e80edc256f`
  (`quest1121-937-cts-mapping.log`).
* Preloading before XR entry moved 14.99 seconds of loading outside the immersive
  session but left a 2.03-second navigation gap. A separate tighter label-texture
  crop also left a 2.00-second gap. Neither experiment qualifies as a stall fix.

### Navigation controls

Normal logging, same immersive replay on the desk. Gaps below are application
frame intervals; the two camera callbacks are sequential, not GPU timings.
First-label time starts at navigation selection, including the confirmation.
The counters are deltas between phase reports, not per-draw probes.

| Control | Worst reported frame | First submitted dependency label | Bind groups in worst interval |
| --- | ---: | ---: | ---: |
| Mipmapped labels, V8 graph-clone control | 2072 ms | 2563 ms | 569 |
| Bilinear labels, no mipmaps | 1080 ms | 2190 ms | 133 |
| One full-quality bulk label update per scene frame | 659 ms | 4784 ms | 48 |
| Multi-level compute mipmaps, original synchronous label scheduling | 1394 ms | 2712 ms | 329 |

Matched V8 14 release desk replays, recommended eye size, automatic education,
same archive, full label dimensions and mip chain:

| Variant | Worst application frame | First / eighth submitted dependency label |
| --- | ---: | ---: |
| Original scheduling | 1759 ms | 2409 / 2409 ms |
| Lazy label textures, shared quad, compute mips, visible-label priority | 689 ms | 2320 / 2731 ms |
| Same, main-realm archive decoded in a worker with bounded messages | 485 ms | 2415 / 2831 ms |
| Same, honor declared GPUTexture view formats | 484 ms | 2264 / 2667 ms |
| Full filtered 72 Hz replay at browser application density | 505 ms | 2249 / 2657 ms |

In the 689 ms interval the render thread spent 371 ms in archive JSON parsing.
The worker variant decoded all 221 records; main cache hydration took 5.01 s
instead of 3.34 s, overlapping rendering. Its worst frame included 369 ms between
the first eye's camera callbacks, four new pipelines and 177 new buffers. This
isolates the archive stall from the remaining rendering stall; even 485 ms is too
long for an immersive transition. No label quality reduction or CPU Canvas copy.
See `quest1121-v814-{nav-phases,lazy-priority,worker-archive}.log`.

Disabling mips is a cost control, not a visual fix. The progressive experiment
pushed the dependency labels behind the ring labels. The compute experiment
kept the non-power-of-two 2560 x 256 textures, all 12 levels and trilinear
sampling. On M4 its output differed by at most one 8-bit channel value across
every texel of eight test sizes, including odd and one-dimensional mip tails.
The physical Adreno 540 passed the same eight pixel tests, including all 12 levels
of a 2560 x 256 label (`quest1121-mip-pixels.log`).
The first camera callback in its worst Quest frame still took 461 ms. Do not
infer that this was all driver compilation, or enable any of these controls
by default from a single replay. Local receipts: `quest1121-937-{native-clone-focused,label-no-mips,nav-progressive,compute-label-mips}.log`.

Direct typed WebGPU bind-group conversion vs. the JSON bridge, in an on-device
ABBA using the same two descriptor shapes: 12.71 to 7.18 ms per 192 buffer groups,
14.06 to 7.87 ms per 192 texture/sampler groups. The medians pool the eight warm
batches of each shape in each run; each run ended with a clean validation scope.
Cold `requestAdapter()` also needed to wait for real GPU initialization: the same
immediate-on-launch bind-group fixture failed before that change and passed after it.
Neither the microbenchmark nor the adapter fix eliminated the full navigation stall.

The Quest ran the two unmodified core-format CTS cases (1,764 texture-creation
and 3,612 view-creation subcases). The old native code allowed an undeclared
sRGB alternate view; explicit alternates in either direction still work. Canvas
and OpenXR imports use their own native formats. At recommended eye resolution,
20 warm windows with declared formats had median VrApi app time 6.59 ms vs.
11.79 ms previously. The ordinary Stats menu, opened after navigation and then
closed, displayed 9.0, 8.7, 13.1, 11.7, 9.7 and 13.7 ms GPU. These are the
actual UI text values, not compositor timing or a permanently enabled timer.
See `quest1121-v814-{cts-views-core,stats-gui}.log`.

On the `react-native -> react -> react-native` navigation sequence each current
repository label texture matched a forced full GUI repaint in all 300 x 50
RGBA pixels. Nontransparent pixel counts were 997, 703 and 997. There was one
label mesh; its hand-menu parent was disabled on the desk. This isolates stale
texture pixels from overlapping scene elements when the hand menu is visible.
See `quest1121-v814-label-history-check.log`.

### Android V8

[NativeScript/v8-buildscripts#16](https://github.com/NativeScript/v8-buildscripts/pull/16)
provides opt-in arm64 and CRC/crypto Cortex-A73 builds. Both packaged Intl/Wasm
executables at `7d810e8` passed unchanged on this Quest (Android API 29). The
full release APK linked against its A73 V8 14.9.207.39 monolith in `libnapi.so`;
the embedding imports V8 from that DSO, and the APK has no `libv8android.so`.
Education started `tour0.aac`; the eight react-native dependency labels were
submitted 2.71 s after navigation (`quest1121-v814-app.log`).

## Worn validation and navigation regression

The wearer confirmed education playback, the corrected pause date (no time or GMT suffix),
and that the first several react-native releases now animate. They also reported a
24+ second navigation freeze, labels that initially appear too wide, and cubes cutting
holes in a dialog. Do not use a worn headset to repeat a known multi-second motion stall.
The Android V8 date fix is [BabylonNative #2107](https://github.com/BabylonJS/BabylonNative/pull/2107).

* In the focused welcome-menu comparison the browser reported 59-61 app FPS (median
  60, 25 samples); native reported 1-3 (median 2, 14 samples). Both native swapchains
  exist from startup. An opt-in check sampled each attachment before OpenXR release:
  on frame 1, all 16 right-eye samples were transparent black while the left samples
  were opaque; both sides contained scene colors by frame 8. The readback changes GPU
  timing; it does not account for the longer physical right-eye outages.
* The automatic immersive navigation control recorded a 20,036 ms frame gap with
  40 new labels, including 18,827 ms in text draw/update. Main-realm archive hydration
  finished in 2,851 ms; the gzip was already packaged with the app. See
  `quest-nav-frame.log` in the local validation output directory.
* On Quest Vulkan, a fresh canvas's first text flush took 725-731 ms, then 2-4 ms.
  Sharing compiled pipelines and their layouts across live same-device renderers
  cut the second canvas's first flush to 3 ms. Mutable canvas targets, geometry,
  stencil, uniforms and bind groups are not shared. The baseline 2560 x 256 copy
  took 157-179 ms; bulk copying the mapping before pixel conversion took 33-49 ms.
  The maintained cache uses the pinned femtovg patch; the mapped-memory optimization
  only applies on Android Adreno 5xx, below the existing bounded upload pool limit.
* The final 100-second offscreen replay used the same archive, 40 labels, retained
  materials and the fixed spectator camera. Text draw/update cost 2,126 ms; that
  frame still took **4,352 ms**. Issuing archive puts in bounded groups allowed
  frames between writes rather than one 5.7 s gap in the earlier flat run. The
  main-realm transaction took 15.7 s wall time and overlapped rendering. The 4,595
  acquired flat images are not immersive FPS. See `quest1121-nav-bounded-flat.log`.
* Fully transparent fragments with depth writes enabled prevented a later, more
  distant blended quad from rendering on Quest Vulkan and M4 Metal. Disabling depth
  writes, discarding the transparent fragment, or moving the quad in front restored
  it on both. Factotum explicitly enables cube depth writes. Compare the exact
  pixel's material state and geometry in Browser and native before attributing the
  dialog artifact to WebXR. The initially stretched release labels remain unverified.
* CanvasWgpu separately rejected CSS fractional alpha: `rgba(255,0,0,0.5)`
  yielded transparent black. With the upstream Canvas color parser shared by
  both backends, the device returned RGBA `255,0,0,128` for that snapshot and
  `0,0,255,255` for a second HSL canvas. Mutating the source did not change
  the first snapshot. See `webgpu_canvas_snapshot_checks.js` and
  `quest1121-maintained-colors.log`.

| Gate | Device evidence | Conclusion / next discriminator |
| --- | --- | --- |
| Browser optical quality and cadence | Two 2048 x 2263 eye viewports, array layers 0 and 1; application 34-37 fps, compositor target 72 Hz. Sampled eye draws had no multisample buffers. The separate flat browser panel had 4x MSAA. | Match eye viewports and application state before measuring speed. Do not apply flat-panel MSAA to the eyes based on this trace. |
| Native eye surfaces | Both swapchains reported 1216 x 1344, single-sampled, Vulkan format 43, recommended 1216 x 1344. Browser draws 2.84x as many pixels per eye. | OpenXR currently allocates the recommended size at session creation. The JS projection layer retains its requested scale but does not propagate it to allocation; the app also selects a different scale by user agent. A sizing fix must retire old wrappers before swapchain replacement, clamp to actual device limits and report actual viewports. |
| Education crash | Before fix: SIGSEGV 8.155 s after automatic Start Story in immersive Vulkan. Optimized flat rendering reproduced it. Worker returned real dates and records. Hiding only the visible skill ring prevented it; preserving the geometry but switching away from indexed drawing did not. | A legal empty descriptor set before an occupied one triggers the Adreno 5xx driver problem. Keep physical layout padding in wgpu-hal on that GPU family, not in shaders or JS. Explicit empty groups and implicit pipeline-layout gaps both need coverage. The allocator reserves the private slot; shader-visible bindings stay unchanged. |
| Fixed playback stability | Real Quest, same original visible geometry: application-side layout-padding control completed 100 s / 4,327 surface frames. Native-only fix completed 100 s / 4,335 frames; second release run completed 600 s / 25,466 frames without geometry filters. | Surface frames count acquired flat test images, **not** immersive app FPS. Ten minutes covers the original crash many times. Worker progress, correct scene contents, real audio and headset output are separate gates. |
| WebGPU binding indices | The bridge discarded null elements in a pipeline layout. The M4 native test creates shader pipelines for later bindings with either a null or an explicitly empty preceding slot; shaders cannot see a binding at the empty slot. | Preserve positions instead of compacting the descriptor. 21/21 graphics backend tests passed, including distinct-eye pixel and canvas upload tests. |
| Audio | Android decoded the original unmodified AAC assets: 412,039 ms and 1,173 ms. The real education app sent track 7 to the Android audio mixer; the completion callback arrived after 412 seconds. Separate device tests cover pause, resume, stale focus, missing assets and end events. | Native explicitly disables Web Audio; Factotum previously still selected legacy Babylon Sound. Use opt-in packaged Android media for narration in that configuration. Keep normal browser audio and spatial audio separate. |
| Worker / archive | The maintained app failed cold start without main-realm structuredClone, then needed the locally backed IndexedDB seeded in the worker's realm. The repaired Quest build returned two commits, a contributor and a dependency at the start, progressed from January to October, and rendered the floor and skill-ring geometry. | Use the runtime's clone/transfer implementation and initialize the fallback worker store before dependent work. Preserve shared browser IndexedDB and byte-identical CDN gzip archives. |
| Last recorded day | Before correction, the app reached October 2 at 23:00 UTC but omitted the final cue. Cue lookup used the UTC day while the finish predicate compared against midnight at the start of that day. | Finish only at the next UTC day boundary, including on dates after a local daylight-saving change. The same browser fixture now exercises a 23:00 final-day cursor and the following midnight. |
| Playback data diagnostics | The initial ten-minute run logged 19 missing-previous-key diagnostics. The worker legitimately emits a first-seen released record as both an insertion and an update. | Ported the same-batch check from Factotum commit 3104f2c1; an incoming key of another record type must not hide a real missing update. Producer, consumer and reconstruction: 26/26 tests. |
| Stereo and focus | The headset previously showed an intermittent physically black right eye; the floor orientation and Touch ray improved and seven reference-space checks passed. After a normal reboot, the signed app created both Vulkan eyes and OpenXR reached SYNCHRONIZED, but the system's Tracking Lost dialog held focus. | The reboot succeeded. Leave the system prompt alone. Check both lenses throughout startup, slow turns and translation during playback when positional tracking is available. A mirror capture, an offscreen test and Home's 72 fps cannot certify this. |
| Performance and memory | The failing native immersive debug run reported mostly 1-3 app fps at cold startup, four 12-fps samples, one 16-fps sample, then 3 fps approaching failure. Process-scoped VrApi records showed stale frames. | No matched post-fix stereo release FPS yet. Separate shader/pipeline discovery, JS/worker cold start, UI-thread xrWaitFrame, terminal eye passes, and the CPU queue-completion wait. Keep same Vulkan queue/layout ordering and a wait at swapchain destruction when testing less per-frame synchronization. Sample app memory at equivalent playback timestamps, not Android's system-wide free-memory counter. |
| Menu, artwork and color | A missing dynamic-texture extension left four GUI canvases unready even with no assets pending. After registering the WebGPU extension on engine import, the settled welcome panel on Quest shows all three illustrations, captions and centered Start Story. The app rendered education geometry in the fixed-view screenshot. | Capture after scene readiness, not during panel fade-in. The button, stored image and material evidence is from an app-owned 640 x 400 Android surface; stereo color and angular resolution still need the headset. |

Additional performance evidence and discriminators:

- The production education release completes on Quest Vulkan with the fixed spectator
  camera. In one 680-second run it produced 30,776 acquired images. The worker reached
  October 2 at 23:00 UTC, the closing AAC started 60 ms later, and the real
  "Restart Tour? / Continue Exploring" dialog rendered after it ended. A second
  680-second run with the same-batch worker fix produced 30,597 images and no
  missing-previous-key diagnostics. Neither interval used driver tracing.
- PID-filtered normal VrApi logs: browser welcome menu 34-37 app fps (median 36;
  eight samples). The original failing debug APK: before Start Story 1-3 (median 1;
  28 samples); after Start Story 3-16 (median 12; seven samples). The post-reboot
  synchronized-only control has zero visible intervals. The app repository's
  `native/tests/quest-vrapi-report.mjs` reproduces the measurements from logcat.
  The same records report `GPU%` of 0.58-0.64 in Browser, 0.31-0.49 at native
  debug startup and 0.32-0.49 after Start Story. Some native records repeat
  identical counters across seconds. These are coarse runtime observations.
  They point away from pixel fill alone as the explanation for the old stalls:
  the browser rendered more pixels at higher utilization and a higher app rate.
- The browser reported native projection scale 1.68421. Native currently returns
  a literal 1.0 in `XRGPUBinding::GetNativeProjectionScaleFactor`, while the
  OpenXR backend never receives the JS layer's requested scale. Do not substitute
  the runtime's maximum 8192 dimension for its optical/native recommendation.
  See the [WebXR Layers scale definition](https://immersive-web.github.io/layers/#dom-xrwebglbinding-nativeprojectionscalefactor).
- Two snapshots of the same education process, 461 seconds apart, measured total PSS
  at 986,758 and 1,513,536 KiB, graphics at 684,036 and 1,173,152 KiB, and native heap
  at 142,560 and 184,864 KiB. The timeline and geometry changed. Compare settled state,
  resource counts and repeated replay before blaming a leak on GPU allocations or on V8.
- OpenXR eye release now uses tracked transitions on the existing wgpu queue rather than
  loading and storing both color attachments every frame. First use and potentially
  discarded images still go through initialization. The distinct-eye GPU test includes
  a delayed discard in only one eye. The per-frame CPU completion wait is still enabled.
- A prior four-second CPU sample of the debug menu included Canvas readback, temporary
  allocation and row copying. The current renderer avoids a redundant packed-row copy.
  Canvas readback versus native GUI upload remains worth isolating in the release menu.
- The readback cache used a Canvas wrapper address as device identity. Two wrappers
  for one GPU device were allocating separate staging buffers. Comparing wgpu-core
  IDs alone also failed a two-instance GPU test. The cache now reuses a buffer by
  the live HAL device identity and required capacity. Independent screenshot
  submissions own their buffers so a later frame cannot overwrite an unread one.
  M4 graphics coverage now passes 21/21, including cross-instance use, overlapping
  snapshots, padded/premultiplied Canvas upload and per-eye contents.
- In NativeWebGPU, createRenderPipelineAsync serializes the descriptor, dispatches back
  to the runtime and calls the synchronous native pipeline constructor there. Determine
  its share of cold-start stalls using coarse timing in a matched visible release run.
  Off-thread compilation needs device lifetime, cancellation and error-scope isolation.
  The Vulkan render and compute descriptors currently pass `cache: None`;
  compare cold and warm launches before investing in a persistent pipeline cache.
- The Android validation harness uses a fixed spectator camera only for the offscreen
  gate: a WebXRCamera left active without XR frames has no valid projection and yielded
  uniformly blue screenshots. The installed immersive APK uses real OpenXR cameras.
- The JS binding advertised BGRA even when the Vulkan runtime negotiated RGBA.
  It now resolves the preference from the same session format as the actual imported
  color image, prior to the first frame. Layer creation accepts frozen input
  dictionaries without writing default fields back to them. The numeric Vulkan image
  format and the WebGPU view format have different roles: format 43 is the sRGB
  compositor image and `rgba8unorm` is its mutable WebGPU view for display-encoded
  output. A different component order is not itself proof of bad gamma.
- Babylon created and replaced both attachments as each new swapchain image entered
  its cache, then tried to destroy the XR-owned texture on deferred release. The
  maintained app patch now supplies the existing color at construction, uses the
  existing depth/stencil (with stencil metadata intact), and leaves XR images alive.
  With four rotating images the real Babylon render-target code requested four
  temporary color and four depth allocations before, zero after. With no external
  depth it still allocates one private depth texture. Those three red-before-green
  cases are in the app's `native/tests/xr-attachments.test.mjs`. They measure
  allocation calls, not a headset FPS improvement; the flat replay uses no XR images.
  A fourth control keeps normal owned texture and private MSAA/copy cleanup intact.
  The on-device release test then cleared four separate RGBA8 Vulkan targets with
  depth/stencil, exercised the actual provider's left/right reuse, disposed the
  Babylon wrappers, drained deferred disposal, and read back red/green/blue/yellow
  from the original textures. It passed with zero replacement texture allocations
  (`quest1121-final-device.log`). Those are app-owned stand-ins, not headset images.
- Factotum requests Helvetica Neue for its Babylon text. On Android the canvas
  backend resolves regular and bold system Roboto faces (Droid Sans fallback).
  The captured welcome dialog has centered text; angular sharpness and perceived
  contrast still need a comparison in the headset at matching eye resolution.

### Matched allocator replay

Three consecutive 633-second release replays, same 640 x 400 spectator camera,
education data, GPU feature set and application bundle. Totals below are Android
`dumpsys meminfo` **graphics / total process PSS**, in MiB rounded to the nearest
whole MiB. Audio and worker state were checked separately from surface acquisition.

| Checkpoint | Default heaps | Smaller canvas heaps | Smaller canvas and 3D heaps |
| --- | ---: | ---: | ---: |
| 2 minutes | 668 / 963 | 583 / 867 | 332 / 627 |
| 6 minutes | 646 / 992 | 628 / 970 | 347 / 672 |
| 8 minutes, before closing panel | 1163 / 1487 | 1001 / 1349 | 477 / 826 |
| 620 seconds, after closing panel | 1144 / 1477 | 980 / 1337 | 455 / 794 |
| Acquired test surface images | 28,327 | 28,517 | 28,348 |

All three completed both narration cues and reached the last date. Both smaller-heap
closing captures have the same SHA-256 as the accepted closing dialog. At six and
eight minutes there were 43 scene textures and about 197 MiB of tracked WebGPU
buffers/textures. Thus a substantial part of the process footprint is allocator
policy, not the size of the live application resources. Smaller block sizes cut the
late increase but did not eliminate it (347 to 477 MiB); the last two samples
decreased. These runs do not distinguish retained capacity from a leak across
repeated tours in one process. Keep the memory hint local to Android Vulkan Adreno
5xx; other devices retain their previous default. Surface image counts aren't
compositor FPS. Receipts: `quest1121-{canvas-default,canvas-small,both-small}*`.

Local evidence: `build_quest_factotum_validation/quest1121-*`. The opening and final
frames are `quest1121-education-ready-menu.png` and `quest1121-education-final-screen.png`.
`quest1121-android-harness-source.tgz` contains the tracked Android host and test runner
sources at their tested working state, without a keystore or build products.

### Next worn-headset run

Use the arm64 Release validation APK `Factotum-Quest1-education-release.apk`.
Installed SHA-256, read back from the package manager's `base.apk`:
`9b0568e3d3756b40e55a345ec2f65e18b82daf8bc2abffa74a631727457d30a8`.
Its 331 webpack JavaScript assets were compared byte-for-byte to the rebuilt
education output. It uses real OpenXR cameras and automatic Start Story after
readiness; the fixed spectator camera, resource sampler and Vulkan pixel fixture
are absent. The deployment script `quest-normal-launch.sh` in the evidence
directory takes the ADB serial, APK path, output log path and optional duration.
It refuses detailed GPU profiling/debug layers, verifies the installed APK digest
and uses a normal launch intent. It never dismisses tracking prompts.

The last 633-second physical Quest fixed-camera run of the rebuilt application
acquired 28,331 images, passed the rotating Vulkan pixel test, ended both AACs,
and saved a closing dialog byte-identical to the earlier accepted frame.
The final immersive install succeeded. Android declined the launch because the
current activity was being kept for the user; the system capture shows Tracking
Lost in both eyes. There was no Factotum PID or visible frame in that attempt.
The device is responsive. See `quest1121-final-install-receipt.txt`,
`quest1121-current-system.png` and `quest1121-final-device-closing.png`.

Meta detailed profiling was off. A separate Android debug-layer selector for
`VK_LAYER_ADRENO_qprofiler` still targeted Quest Browser, not Factotum. The stale
selector was removed without launching Browser. Final checks: `enable_gpu_debug_layers=0`;
debug app, Vulkan and GLES layer names and layer app all null; EGL profiler abstract
socket disabled, named socket empty. No further driver capture is needed for this check.

1. Restore positional tracking in a lit space. If Android reports that the current
   activity is being kept for the user, check the system view; do not use Home's
   compositor samples for Factotum. No controller click is necessary for Start Story.
2. In the focused session, compare both lenses during startup, head rotation,
   translation, the education transition, narration and the closing dialog. Check
   right-eye continuity, label and illustration sharpness, gamma, floor and Touch ray.
   Compare app FPS, stale intervals and eye dimensions at the same menu state as Browser.
3. If sharpness differs, plumb the requested layer scale to the OpenXR swapchains
   with safe retirement and match the measured browser viewport. If cadence still
   differs, compare cold/warm pipeline creation and isolate the CPU completion wait
   at unchanged resolution. Do not remove the swapchain teardown wait or trade one
   eye's correctness for throughput.

The layout-padding precedent is
[Godot's Vulkan backend](https://github.com/godotengine/godot/blob/master/drivers/vulkan/rendering_device_driver_vulkan.cpp).
See the [WebGPU pipeline layout contract](https://www.w3.org/TR/webgpu/#pipeline-layout-creation),
[OpenXR Vulkan image state](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XR_KHR_vulkan_enable-swapchain-image-state.html),
and [swapchain destruction requirements](https://registry.khronos.org/OpenXR/specs/1.1/man/html/xrDestroySwapchain.html).

## Factotum stereo and react-native selection, November 21 evening

Quest 1, Adreno 540, V8 14.9.207.39, wgpu Vulkan. Compare the normal app's
process, not Home or a suspended browser tab. Browser 112 used the public
visualization URL. The comparison used the same repository, date and a 60 Hz
session; the active mesh counts and headset poses were not identical. Native
rendered 2048 x 2263 per eye, matching the previously measured browser eye
dimensions. The recommended native extent alone (1216 x 1344) understates the
work submitted. No driver tracing or per-draw logging was enabled.

* Cold native inspection: Activity creation to OpenXR focus 14.03 s, to app
  welcome-ready 17.00 s. These are readiness events, not a physical first
  readable frame in each lens.
* Otherwise identical selection replays: first/eighth dependency label
  2192/2765 ms on the original implementation; 2274/2730 ms on the driver
  worker. No convincing navigation latency win. On the counted build all seven
  render pipeline creations in the first ten seconds were asynchronous. The
  first five seconds also created 510 buffers and 48 GPU canvas snapshots;
  there were zero CPU canvas snapshots.
* Native warm menu: 51 FPS median. On January 10, 2016, original geometry:
  native 19 FPS median, p10 19 (90 whole attributed compositor seconds, every
  one below 30). Browser scene median about 20 FPS; VrApi independently
  reported 19/60 shortly before switching away. FPS early in playback is not
  representative of this state. Browser's three biggest instanced meshes
  (commit cubes, avatar skill rings and visualization rings) accounted for
  about 6.1M of 8.45M active indices. Native reported 12.26M active indices.
  These Babylon counts are not post-transform hardware vertex counts.
* At the native plateau GPU memory estimate was stable at 355,396,920 bytes,
  with no new pipelines, buffers, bind groups or Canvas snapshots. VrApi's
  median App field was about 52 ms and GPU load 80%. Buffer creation during
  moving releases is a separate problem; neither shader compilation nor Canvas
  readback accounts for the sustained plateau.

Run the calendar-matched comparison with
`node Apps/scripts/summarizeQuestPlayback.mjs --date 2016-01-10 LOG` (the
adjacent `.pid` must contain the app PID). Receipts in
`build_quest_factotum_validation/`: `quest1121-v814-old-driver-js.log`,
`quest1121-v814-driver-worker.log`, `quest1121-v814-ring-worker.log`,
`quest1121-v814-worker-tests.log`, `quest1121-browser-react-native-iso.log`
(before native took foreground at 20:51:25), `quest1121-browser-year2-meshes.json`,
`quest1121-stereo-ttmfr-inspection-b.log`, and `quest1121-vulkan-capabilities.json`.
The Vulkan device reports six multiview views, multiDrawIndirect, and no
drawIndirectFirstInstance. Neither the native engine nor the shader path can
be declared multiview merely because a capability is present. Normal logs
provide no texture-cache, GMEM spill, register, CPU L1 or branch-miss counters.

See [patch ownership and upstream dependencies](FactotumQuestPatchInventory.md).

### Year-two geometry and final inspection

Within one process, hold January 10, 2016, change only ring tessellation and
whether fully transparent pick-only meshes enter the draw list. Retain
transform/picking, materials, eye density and the major circle segments. Use
eight radial segments for the tube in the reduced topology. Exclude intervals
crossing a switch. The reported App field is VrApi's, not a GPU timestamp.

| Geometry at checkpoint | Observed seconds | Active indices (median) | App ms (median) | GPU load (median) | Delivered FPS (median) |
| --- | ---: | ---: | ---: | ---: | ---: |
| Original | 30 | 12,204,636 | 51.72 | 81% | 20 |
| No hitbox draw | 50 | 12,084,252 | 49.30 | 81% | 20 |
| Reduced rings | 25 | 7,678,068 | 44.94 | 79% | 20 |
| Both | 25 | 7,557,684 | 42.19 | 78% | 20 |

All 130 attributed compositor seconds were below 30 FPS. The 9.53 ms
improvement matters, but does not meet the 33.33 ms budget; keep production
geometry unchanged. Next split the full-resolution eye render from the
linear-light downsample at fixed content using infrequent asynchronous GPU
timestamps, accounting for resolve work outside the JS pass. Follow with a
matched-color, matched-coverage comparison before changing the filter or
designing layered rendering. Separately replay webapp #1322's retained instance
buffers during navigation. Neither optimization is an established cache win.

The original-geometry interactive APK was restored after the comparison.
Android Activity creation at 21:20:24.866, OpenXR focused at 21:20:39.626
(14.76 s), app welcome-ready at 21:20:41.665 (16.80 s). The first Android
compositor capture began at 21:20:39.712 and finished at 21:20:43.106:
both views contain the Factotum floor and sky by 18.24 s from launch. The
second capture (21:20:44.190 to 21:20:47.550) shows the side controls in both
eyes, with a partial room overlay. The welcome dialog was outside the captured
view. Exact first simultaneous physical presentation and readable welcome
sharpness remain separate user checks. Receipts: `quest1121-worker-functional-stereo.log`
and its `-focus.png` and `-menu.png` files. Education autoplay fired 30 seconds
after welcome-ready; no qualification geometry toggle is in this APK.

### Measuring GPU eye and resolve passes

On this Android Vulkan backend, set `debug.babylon.eye_timing` to `1` **before
launch**, and leave GPU debug layers off. The renderer reads the property
once. Every 24 releases it can sample an XR frame with pass-boundary timestamp
queries. It identifies application eye attachments from the actual imported XR
texture (or its MSAA resolve target) rather than from render dimensions. It
checks that both distinct images were rendered and resolved; it skips a
sample if an application already owns the pass timestamps. It asynchronously
maps query words only, never pixels. There is at most one pending mapping.
Set the property back to `0` and restart for normal use.

The two reported times are **sums over both eyes**, excluding inter-pass queue
gaps and the system compositor; they are not presentation latency. Join them
to the scene date and to app-PID VrApi observations with:

```sh
node Apps/scripts/summarizeQuestEyeTiming.mjs build_quest_factotum_validation/quest1121-v814-eye-timing.log 2016-01-10
```

Original-geometry baseline, 2048 x 2263 per eye into 1216 x 1344 per-eye
swapchains: **41.753 ms** eye renders, **5.108 ms** downsampling, **46.915 ms**
paired sum (medians of 129 frames). During 165 attributed compositor seconds
the median was 19 FPS, none above 30. Eliminating the entire downsample would
still leave the median eye work over a 33.33 ms budget. Earlier in playback
the same downsample took roughly 5 ms with much shorter eye render times.
Timestamps localize the cost; they cannot identify DRAM traffic, texture-cache
misses, CPU branch misses or GMEM spills.

Six same-process controls held the same January 10 checkpoint, eye sizes and
downsample filter. Intervals crossing a mode switch were excluded. All used
the corrected XR depth attachment setter from Babylon.js draft #19215:
the backend estimate fell from 355,396,920 to 318,319,928 bytes, exactly two
2048 x 2263 four-byte depth images. This is a persistent allocation saving,
not measured traffic per frame.

| Control | Paired frames / compositor seconds | Both eye passes (ms) | Stereo downsample (ms) | Paired total (ms) | Delivered FPS |
| --- | ---: | ---: | ---: | ---: | ---: |
| Store depth, original geometry | 31 / 40 | 41.962 | 5.105 | 47.062 | 19 |
| Discard depth after use | 33 / 40 | 41.463 | 5.090 | 46.560 | 20 |
| Discard plus tiled triangle order | 31 / 40 | 41.436 | 5.107 | 46.635 | 19 |
| Store plus tiled triangle order | 29 / 40 | 42.456 | 5.103 | 47.568 | 18 |
| Store plus stereo instance culling | 29 / 35 | 40.706 | 5.099 | 45.929 | 20 |
| Discard plus stereo instance culling | 17 / 20 | 39.415 | 5.092 | 44.531 | 20 |

All timing and FPS columns are independent medians; zero of the 215 whole
compositor seconds exceeded 30 FPS. The discard control checked that later
passes did not reload discarded data. The triangle-order control retained
four eligible opaque meshes' original tessellation. Conservative left-or-right
frustum culling of blocks of eight commit cubes skipped 671,220 of 1,241,460
submitted instances across 990 eligible draws in the first store/cull interval.
That 54% is relative to that mesh, not to the entire scene. These were short,
fixed-order controls, not thermal- or clock-matched microbenchmarks. None
justifies changing interactive defaults or reducing menu density.

Reproduce the table with the same summarizer and `quest1121-v814-eye-depth.log`.
The replay inputs (including the stereo culling fixture and test) are in
`build_quest_factotum_validation/quest1121-eye-replay-inputs.tgz`.
The interactive restore is `quest1121-gpu-interactive-depth.apk`; it contains
the depth replacement fix but none of the benchmark geometry or discard switches.

On restoration, PID 30295 created both Vulkan view swapchains and reached
welcome-ready with timing disabled. The Quest advertised `XR_FB_foveation`,
`XR_FB_foveation_vulkan` and swapchain update state, but not
`XR_KHR_visibility_mask`. Foveation was not enabled in either comparison.
The Vulkan inventory also advertises `VK_EXT_fragment_density_map`.
The [OpenXR density image](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrSwapchainImageFoveationVulkanFB.html)
is compatible with its corresponding swapchain image. Turning it on for the
1216 x 1344 output alone does not reduce shading of the separate full-size
application eye. First split pixel cost from geometry cost at this checkpoint
with a reversible world-only resolution control, keeping GUI density fixed;
then test peripheral shading on the full-size eye if pixel cost scales.

### Building the welcome menu up from an empty XR scene

Quest 1 Vulkan, 72 Hz, stationary headset, both application eyes 2048 x 2263
into separate 1216 x 1344 swapchains. No playback, browser driver tracing,
per-draw probes, or geometry simplification. Two executables' JS entry points
were compared in the same host APK: a fresh Babylon 9.37.1 scene (no Factotum
entry, archive, workers, or simulation) and the real interactive bundle with
only welcome autoplay replaced. Each stage settled for four seconds then
measured for 16 seconds. These are short diagnostic windows, not sustained
thermal measurements. Compositor and pass medians need not add up.

| Stage | App FPS (VrApi median) | Both GPU eyes, ms | Stereo downsample, ms | Draws/frame |
| --- | ---: | ---: | ---: | ---: |
| Fresh Babylon clear / marker | 72 / 72 | 0.92 / 0.98 | 5.08 / 5.09 | 0 / 2 |
| Add untextured, opaque room | 58 | 4.20 | 5.11 | 4 |
| Add static textured label | 59 | 4.37 | 5.13 | 6 |
| Factotum all meshes hidden | 72 | 0.94 | 5.10 | 0 |
| Factotum sky only | 53 | 4.57 | 5.16 | 2 |
| Factotum sky and panels | 48 | 7.22 | 5.12 | 16 |
| Factotum panels only | 72 | 2.28 | 5.09 | 14 |
| Factotum full menu without sky | 62 | 5.20 | 5.10 | 32 |
| Factotum full menu, first / restored | 37.5 / 41 | 8.52 / 8.13 | 5.09 / 5.12 | 34 |

The full-screen draw matters even without a cube texture or any simulation.
Hiding the sky is a diagnostic, not a shippable optimization. Neither a
texture-cache bottleneck nor CPU/GPU bandwidth saturation is established.

**An extra framebuffer clear.** Babylon clears the parent's default framebuffer
before clearing and drawing the two independent rig targets. A second full-menu
A/B/A/B/A skipped only that parent clear, with both original eye clears, all
34 draws and the original downsample retained. Babylon pass count was 3/2/3/2/3
and VrApi medians were 37/49/40/48/40 FPS. Scope any production solution to
a verified projection-only path with no mirror, readback or framebuffer
observer needing the clear. Do not set scene.autoClear=false globally.
After the timing run, a separate normal-logging run compared 18,432 sampled
RGBA pixels per eye under live tracking. For consecutive baseline frames,
112 left / 147 right had a channel delta over 8; baseline vs skip: 41 / 142;
skip vs restore: 146 / 114. This tests application eyes, not final lens optics.
With both timings and readbacks separated, a near marker in the standalone
scene was found at (60, 7) and (35, 7) on a 128 x 16 grid. The two Babylon
rig-camera matrices predicted (60.70, 6.65) and (35.99, 6.65) on that grid.
The distinct eye images agree with the corresponding camera to one sample;
this does not test the headset's display calibration.

**Layer audit.** The Quest reported one projection texture slice, distinct
color resources and per-eye view-descriptor indices 0/0; no quad or cube
factory. Babylon 9.37.1 WebXRLayers forces dual-pass for WebGPU, and our
Vulkan rendering does not specify a multiview mask. The editor's draft
[projection allocation](https://immersive-web.github.io/webxr-webgpu-binding/#dom-xrgpubinding-createprojectionlayer)
calls for an array slice per view. Fix application-visible array storage and
per-eye clear/load/subimage handling first; prove per-eye pixels with
two ordinary draws before adding single-pass multiview with per-view
projection, masks, and shader support. A texture array is not single-pass.
Separately test a static sky compositor layer where supported, retaining
scene geometry as a fallback and verifying orientation, transfer function,
projection transparency and occlusion. Menus that must depth-test against
controllers or other scene geometry cannot simply be overlaid unconditionally.

Fixtures: `Apps/scripts/questMenuLadder`. The build script accepts the
published `babylonjs-9.37.1.tgz` and an output JS path; use the same Android
host and native bootstrap as the inspection APK. It applies the same relevant
XR fixes, guards the unsupported external-video pipeline and requests texture
sampling for post-timing checks. `factotum.mjs INPUT OUTPUT meshes|parent|pixels`
creates the real menu controls from the inspection entry. The timing logs
are `quest-menu-ladder-stereo.log`, `quest-menu-ladder-real.log` and
`quest-menu-ladder-parent.log` in `build_quest_factotum_validation`;
run `node Apps/scripts/questMenuLadder/summarize.mjs LOG` with its adjacent
PID file. Pixel checks are separate logs. No benchmark switch is installed
in the interactive application.

### Quest 1 projection-array qualification, November 22

The no-mirror parent clear is opt-in and the spectator/default canvas is
unchanged by default. `Apps/scripts/questMenuLadder/spectator.cjs` exercises a
real Babylon Scene with a recording framebuffer: both eye draws, spectator
background/foreground, and the untouched no-mirror canvas. Distinct slices of
the same texture must still clear independently; two viewports sharing the
same slice must not erase one another.

The Android OpenXR projection swapchain now has two slices, with a single
acquire/wait/release for the image and separate imageArrayIndex values. The
Babylon two-pass marker agrees with the left/right projection in both slices.
On the Quest Adreno 540 (driver feb9270) a layered depth attachment cleared
with only its load operation lost the near triangle in both eyes. It reproduced
without Babylon or OpenXR. A depth-only fullscreen clear restores the near
triangle and still rejects farther geometry. The standalone Vulkan test
`Core/GraphicsWgpu/Rust/examples/xr_multiview_pixels.rs --draw-clear` compared
independent targets, per-slice draws and single-pass view-indexed multiview:
16,384 identical color bytes for each of the four depth formats tested. See
`array-wgpu-occlusion.log` and its failing control `array-wgpu-depth-formats.log`.
This does not change Babylon 9.37.1's scene renderer to single-pass.

Normal-logging playback on the Quest (no driver trace, original geometry):

| Scene | Compositor rate | Median app FPS | Evidence |
| --- | --- | --- | --- |
| Warm interactive menu | 60 Hz | 60 | 20 one-second windows, range 55-61 |
| facebook/react-native, Jan 10 2016 | 60 Hz | 20 | 35/35 windows below 30; p10 19.4; 51.63 ms median App; 12,308,898 active indices |

The capture is `array-slice-year2-through-checkpoint.log` (PID 13984).
The earlier `array-depth-fixed-year2.log` measured the same 20 FPS median.
The two-pass 72 Hz diagnostic in `array-slice-ladder.log` passed the post-run
marker pixel test. It measured 72 FPS clear/marker, 53 for the room and
marker, and 52 adding a static label. A previous sample process also appears
in that system log; the summarizer filters on the current PID, but this is
not an isolated two-pass vs single-pass performance comparison.

The standalone `babylonnative-webxr-samples` checkout runs the community's unmodified barebones WebGPU draw
loop. On Quest it produced the rainbow triangle in both eyes and passed the
32-frame shared-array, view-matrix, reference-space and clip-range checks.
It exposed two independent API defects: missing `XRRigidTransform.inverse`
and a 0.50 m platform near plane despite a reported 0.10 m render state.
With the fix, a centered point 0.5 m away projects to clip Z/W of 0.80008
instead of zero. The shared UI desk test rendered the hand-menu selector and
modal, invoked close/reopen/run, ended the menu session, and passed the same
32-frame test in the new session. The local system captures contain Guardian
imagery; do not publish them. Chromium 151's flat WebGPU control passed RGB
triangle coverage (368/367/355 primary-color pixels); that host reports no
immersive device. No immersive browser result is recorded here.

### Full-resolution ring controls, November 22

Quest 1, Adreno 540, Vulkan, two eyes at 2048 x 2263, same January 10 2016
paused playback state throughout. No driver trace. Log: `029-rings.log`, app
PID 4404. Each row uses seconds 4 through 23 after the mode switch, excluding
transitions and unrelated processes. Reported medians; 12-13 paired GPU
samples per window. The GPU ran at 710 MHz and reported 35 C throughout
these windows. Resolve includes both eyes.

| Mode | Scene indices at end | Eye GPU ms | Resolve ms | App FPS |
| --- | ---: | ---: | ---: | ---: |
| Baseline | 12,251,106 | 49.34 | 5.08 | 16 |
| Cull both ring families against the union of the eyes | 7,706,190 | 44.94 | 5.09 | 17 |
| Cull plus projected-size LOD, more demanding eye | 7,666,206 | 44.73 | 5.09 | 17 |
| Cull again | 7,719,258 | 44.76 | 5.08 | 17 |
| Baseline again | 12,251,106 | 48.96 | 5.07 | 16 |

The 37% reduction in indices saved about 4.3 ms in the eye passes. Four
of 67 visible avatar rings dropped one LOD; all four container rings stayed
at full detail. These data do not identify a universal index threshold.
The second cull block issued 503 attribute uploads at the visibility boundary.
Keep the checked replay distinct from production instance ownership, picking
and animation. The separately tested exit dead band and partial buffer writes
were not in this measured APK.

In an independent scissor control at this checkpoint (670 MHz, equal indices),
the full eye render was 67.08 ms versus 34.83 ms with a one-pixel scissor.
Coverage therefore has a large cost in addition to processing geometry.
Shrinking covered area cannot distinguish fragment ALU from texture traffic,
blending or tile work. Composition and CPU optimizations alone cannot recover
the >37 FPS budget of 27 ms. Current native PMU access did not yield valid
per-stage shader, cache, GMEM spill or external bandwidth counters.

Quest CanvasWgpu and Chromium each passed eight comparisons of actual ink pixels
against TextMetrics for capitals, descenders, mixed case and whitespace at 24
and 48 px. The native source used the bottom rather than the top of femtovg's
ink extent. This affects baseline alignment independently of font selection,
panel texture density, headset optics and the final downsample.
