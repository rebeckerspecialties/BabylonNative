# A10X Hill Valley Resolution Scaling

## Workload and Build

November 15, 2026. Physical first-generation Apple TV 4K (`AppleTV6,2`,
UDID `4ea8fd2afc064771099439023bf48723efa66ba0`), tvOS 26.6 (`23L773`).
CoreDevice and Instruments both reported the device online before testing.

The dedicated `build_portal_tvos_release_lto` Playground uses Release/LTO,
Rust 1.94.1, the system JavaScriptCore framework, the existing patched wgpu
30.0.1 runtime, and Rebecker Specialties team `4GWK4YU2PW`. No JavaScript
runtime was built from source. Executable SHA-256:
`6ee2b7e5426717c8e910baab240f7de9524e62f2e0b97f1fa2aa51c38a24807b`.
The optimized Babylon.js 9.36 bundle is identical to the iPhone 12 retest:
`081c385eb68a58619ca6bfad4007435c384ce66397e720a9fb6c30a05ade5812`.

AR is disabled. Retained settings are GT-VBAO slab-v3, F16 blocks-v3,
seeded noise, world normals, R8 AO, shared pre-pass color, and the same
12-second moving-camera orbit. There is no material/visibility freezing,
per-call instrumentation, or effect-quality reduction. The landscape view
has a different visible-mesh workload from the portrait phone comparison.

Each timed run lasts 300 seconds after four fixed-pose screenshots and
480 warm-up frames. A nominal-thermal start gate samples the idle app over
at least 180 seconds before each resolution. This matches reported thermal
state, not measured die temperature. GPU/CPU traces are separate diagnostic
runs and are excluded from sustained FPS results. No wall-power measurement
is available.

The HDMI/display mode and native Metal drawable remain **3840x2160 at 60 Hz**.
This matrix used **SDR**, not HDR: its launch did not request `--hdr10`.
The later A15 investigation also repaired native HDR option forwarding.
Do not relabel these results as corrected-HDR measurements.
Only the scene backing changes between 1920x1080, 2560x1440 and 3840x2160,
using Babylon's public `engine.setSize`. Both engine dimensions and
`GPUCanvasContext.getCurrentTexture()` dimensions are checked. Native
screenshots represent the scaled presentation and therefore remain 4K;
their size is not evidence of the scene's internal rendering resolution.

The validation-only launch environment is:

```text
BABYLON_NATIVE_PLAYGROUND_PROFILE_MEMORY=1
BABYLON_NATIVE_PLAYGROUND_PROFILE_RENDER_SIZE=1920x1080
BABYLON_NATIVE_WEBGPU_PROFILE_BOUNDED_POLL=1
BABYLON_NATIVE_WEBGPU_PROFILE_COALESCE_RAF=1
BABYLON_NATIVE_WEBGPU_PROFILE_SKIP_UNCHANGED_PRESENT=1
MTL_HUD_ENABLED=0
MTL_HUD_LOG_ENABLED=0
```

The last two variables suppress the device's enabled Metal HUD and its log
stream. No FPS claim includes that instrumentation. Change only
`PROFILE_RENDER_SIZE` for the resolution matrix. Load these packaged scripts
in order after the usual Playground bundles:

```text
native_xr_portal_hillvalley_stress.js
native_xr_portal_ao_projection_validation.js
native_xr_portal_profile_workload.js
native_xr_portal_gpu_comparison.js
native_xr_portal_gtvbao_validation.js
native_xr_portal_f16_material_validation.js
native_xr_portal_sustained_benchmark.js
native_xr_portal_world_normals_validation.js
native_xr_portal_ao_r8_validation.js
native_xr_portal_color_reuse_validation.js
native_xr_portal_resolution_validation.js
native_xr_portal_moving_benchmark.js
native_xr_babylon_portal_demo.js
```

Raw logs, run receipts, screenshots, cooldown receipts and analysis scripts
are retained in `/private/tmp/bn-a10x-20261115`.

## Integration Fixes

Two concrete tvOS blockers were fixed before collecting results:

1. `ios-cmake` defines `IOS` for tvOS too. The fork's unconditional NativeXR
   enablement selected ARKit, which cannot compile for tvOS. NativeXR is now
   disabled for tvOS without changing the existing selection elsewhere.
2. The shared Apple host unconditionally called `setXrView`, which throws
   when the XR plugin is absent. That call now uses the existing
   `BABYLON_NATIVE_PLAYGROUND_HAS_NATIVEXR` compile-time capability.

Profiling-only keep-awake now also covers tvOS and restores the previous
setting when the view disappears. The resolution harness rejects invalid,
unsupported or unapplied sizes and distinguishes the canvas backing from
presentation readback. There are no new public JavaScript rendering APIs.

The full focused Node sweep passes 159 tests, including the tvOS option
matrix, XR host capability guard, resolution checks and moving benchmark.
The new/edited validation scripts pass syntax checking. tvOS Release/LTO
build, signing, installation and all three sustained runs succeeded. The first
failed build/launch and rejected screenshot-size assumption remain in the
artifact directory, excluded from performance results.

## Sustained Results

One five-minute run per resolution, in the order below. FPS is completed
scene/render-loop cadence, not independently measured unique display
presentations. Final-120-second FPS is frame-count/time-weighted. P95 is
the frame interval over the complete timed run.

| Scene backing | Completed frames | Overall FPS | Final 120 s FPS | Final 30 s FPS | Median / p95 frame ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1920x1080 | 10,980 | 36.60 | 36.63 | 36.33 | 26.61 / 31.13 |
| 2560x1440 | 9,319 | 31.06 | 31.01 | 30.63 | 31.28 / 35.72 |
| 3840x2160 | 5,575 | 18.58 | 18.56 | 18.43 | 36.94 / 98.34 |

Each run has 61 motion samples and 150 timed memory/thermal samples.
Reported thermal state remained nominal throughout. Camera matrices and
orbit parameters change, with 57-81 active meshes during timing. The
landscape workload is larger than the phone run's 46-59 active meshes;
the FPS numbers are not an apples-to-apples chip comparison.

Materials checks report zero issues at every resolution: 7/7 emissive
backdrops and 11/11 environment-reflective materials. Saved presentation
images retain the car reflections, asphalt/concrete materials and under-car
shadow. All four 1080p fixed-pose images match the earlier 1080p smoke run
pixel-for-pixel. These images are captured before timing, not on every
moving frame; material diagnostics are not a substitute for visual review.

### Memory

These are separate, overlapping accounting domains; do not add them.
Ranges cover the entire five-minute interval. Metal allocated size is
`MTLDevice.currentAllocatedSize`, not a working-set or bandwidth counter.

| Scene backing | Metal allocated MiB | Resident MiB range | Physical footprint MiB range | Footprint first -> last MiB |
| --- | ---: | ---: | ---: | ---: |
| 1920x1080 | 246.64, constant | 342.58-343.34 | 462.88-469.95 | 463.42 -> 463.19 |
| 2560x1440 | 299.00, constant | 340.67-341.42 | 519.00-519.50 | 519.50 -> 519.19 |
| 3840x2160 | 440.66, constant | 350.78-355.84 | 700.49-722.83 | 710.69 -> 705.36 |

There is no runaway per-frame Metal allocation growth in these intervals.
4K resident size increased about 4.7 MiB between the first and last sample,
while footprint oscillated and ended lower. This is not a proof of an
indefinitely flat process working set or a quantified allocation-churn rate.

### Command Traffic

All three runs retain seven JS-originated render passes, two command encoders,
one JS `queue.submit`, and five recorded command-stream batches per completed
scene frame. Average draw count is 69.3-69.5. No command recorder disables,
backend errors, or new JS-visible textures, views, buffers, bind groups or
pipelines occur during timing. The native presentation path has its own
commands and allocations outside these JS API counters.

Native frame callback cadence is about 59.94 Hz at 1080p and 1440p, even
though JS completes fewer scene frames. At 4K it falls to 23.55 Hz. Neither
callback count nor scene FPS alone proves the number of distinct images
actually displayed. The unchanged-frame presentation skip is enabled in
all three configurations.

## CPU and Presentation Attribution

### JavaScriptCore at 1080p

The separate Time Profiler retry completed its full 8.85-second recording
for Playground PID 7214. It contains 9,734 running samples across threads,
covering an 8.57-second sample span. JavaScriptCore accounts for **83.15% of
leaf CPU sample weight**. This percentage is a share of sampled process CPU
execution, not a percentage of wall time or total device CPU capacity.

JavaScriptCore addresses were resolved against the UUID-matched tvOS 26.6
system binary (`18829ADC-B32E-3E7F-A746-E53AB2FBDC9C`). Notable results:

- `JSC::LLInt::performLLIntGetByID`: 16.99% self, 25.73% inclusive.
- `llint_slow_path_get_by_id`: 27.09% inclusive, overlapping the preceding
  call chain. These inclusive percentages must not be added.
- `JSC::Interpreter::executeCall`: 2.89% self.
- The principal Playground JS thread accounts for 82.68% of sample weight.

This is residual interpreter/property-access work with the maintained
accessor-inlined/constant-folded bundle already installed. It does not prove
that one Babylon material method or N-API call is responsible: this trace
does not recover the individual JavaScript source frames. The next CPU
experiment should reduce repeated property traversal with an ordinary API-
compatible change and validate same-frame mutation/multiple views, rather
than infer a bridge bottleneck from CPU usage alone.

Elapsed scene-scope timings average 25.44, 29.94 and 28.38 ms at the three
resolutions. Those scopes include blocking and are **not CPU execution
measurements**. In particular, 4K's lower scope time does not contradict
its slower overall cadence.

### 4K Drawable Backpressure

The standard Metal System Trace for PID 7210 completes in 9.08 seconds.
It contains 116 `Wait for Next Drawable` events on the main thread:

- Nonoverlapping total: **4,903.71 ms**, about 54% of the recording.
- Median: **17.13 ms**; p95: **100.00 ms**; maximum: **133.35 ms**.
- The corresponding 1080p and 1440p diagnostic traces contain no such
  recorded events. Their counter template differs, so this is not a
  matched-template quantitative speedup comparison.

These are CPU-side drawable waits, not GPU shader durations. Together with
the 98.34 ms sustained p95 frame interval and reduced native callback rate,
they establish substantial presentation backpressure at 4K. They do not
separate shader execution, memory traffic and compositor scheduling as the
underlying cause. `InteropBackendContext::render` obtains the surface
texture before presenting; do not label its wait a global Rust mutex cost.

## Issue Inventory and Next Experiments

1. **tvOS integration failures: fixed.** ARKit was selected for a flat-only
   tvOS build, then the shared host called into an absent XR plugin. The
   platform/capability guards fix both without changing iOS XR behavior.
2. **CPU property-lookup/interpreter cost: measured, unresolved.** The 1080p
   trace identifies the LLInt call chain above. No per-call probes or new
   native-only Babylon interfaces were introduced to collect it.
3. **4K pacing/backpressure: measured, unresolved.** Investigate the
   offscreen-to-drawable handoff and outstanding submissions before adding
   effects or treating nominal thermals as proof of GPU headroom. Avoid an
   experiment that simply buffers more frames and increases latency/memory.
4. **Scaled presentation adds an avoidable-looking clear: candidate only.**
   The 1080p Metal trace records 303 `wgpu::util::TextureBlitter::pass`
   encoders and 303 `(wgpu internal) clear_texture clear pass` encoders for
   about 302 scene frames. Their CPU encoding totals are 95.43 and 41.25 ms,
   respectively; these are not GPU costs. The pinned `TextureBlitter::copy`
   uses `LoadOp::Load` even for this full-target, non-blended overwrite.
   A generic opaque-overwrite load policy is a focused candidate, but must
   preserve blending callers and be verified by encoder counts and visual
   A/B tests. No unmeasured wgpu utility override was added in this pass.
5. **Native presentation binding churn: confirmed in source, not timed in
   isolation.** `encode_canvas_presentation` creates source/destination views
   for each scaled presentation; `TextureBlitter::copy` creates its bind
   group each time. The blitter pipeline is already cached. Reusing the
   source view/bind group with correct resize/lifetime invalidation could
   help all platforms. Do not reuse a drawable view across acquisitions or
   confuse the JS API's zero creation deltas with this native path.
6. **GPU bandwidth, cache misses, register/tile spills: unqualified.** The
   custom Performance Limiters template reports an unsupported counter
   profile on A10X. Standard Metal System Trace removes that warning but
   still exports no GPU execution intervals or usable limiter/spill data.
   Zero rows are not evidence of zero spills. No claim that texture-cache
   misses dominate A10X, or that an individual AO shader causes 4K stalls,
   is justified by these captures. A separate validated Metal frame capture
   or another supported counter source is required for that attribution.

The 4K presentation takes the matching-size texture-copy path, while the
two smaller backings use the scaling blitter. Resolution results therefore
include a presentation-path difference, not only a fragment-count change.
No effect quality, material content or AO coverage was reduced. No sustained
60 fps, power-efficiency improvement or spill elimination is claimed.

## Evidence Ledger

All names below are relative to `/private/tmp/bn-a10x-20261115`:

- `matrix-summary.json`: exact frame, memory, thermal and API counter data.
- `<width>x<height>-baseline.{json,log}`: completed sustained run receipts
  and logs; matching `-baseline-images/` directories contain four poses.
- `1080p-repeat-images.json`: zero differing pixels for all four repeat poses.
- `1920x1080-cpu-retry.trace`: valid standalone CPU capture; the symbolicated
  export and summary are `1920x1080-cpu-symbols.xml` and
  `1920x1080-cpu-retry-summary.json`.
- `3840x2160-gpu.trace`, `3840x2160-application-summary.json`: valid
  CPU-side Metal encoding/drawable-wait evidence, not GPU execution metrics.
- `1920x1080-gpu.trace`, `2560x1440-gpu.trace`: encoding evidence with
  unsupported-counter-profile warnings; no valid spill/bandwidth results.
- `1920x1080-cpu.trace`: **excluded**, device disconnected after 0.94 seconds
  despite the recording command returning success. The retry replaces it.
- `final-node-tests.log`: 159 passing focused tests.

Analysis uses structured trace XML with process filtering and ID-reference
resolution. The raw traces are retained unchanged; symbolication writes a
separate XML file after checking the system binary UUID. No shortened or
failed trace is included in the sustained FPS table.

## JavaScript Follow-up

The accepted next TV operating target is **2560x1440 HDR at sustained 30 fps**.
That is a workload budget, not a measured common ceiling for A10X and A15.
The corrected A15 4K HDR run averaged 37.53 scene fps with uneven pacing;
this older A10X 1440p SDR run averaged 31.06. A matched HDR comparison is
still required.

The earlier A10X native CPU trace establishes expensive JSC property access,
but does not identify JavaScript source sites. A separate successful on-device
source capture is recorded below; the M4 samples are no longer the only leads.
The initial foreground launch was rejected with `System is asleep - foreground
app launch forbidden`. Two restricted retries timed out initializing CoreDevice;
the full-device-access retry succeeded. Those failed launches are not
profiler-support or performance results.

An audit-only loop-invariant transformer is now in
`Apps/scripts/loopInvariantTransformer.mjs`. It is **not enabled in the bundle**.
It lifts initialized, stable lexical predicates using non-coercing operations;
it rejects property/index reads, calls, allocation, coercion, dynamic scopes,
mutable/captured bindings and writes through nested callbacks. TypeScript
`readonly`/`private` and a const object reference do not prove a property read
is effect-free. The differential tests include 0/1/n iterations, getter and
Proxy effects, closure writes, copy-on-write, same-frame changes and views.

The current patched 9.36 source audit finds **69 eligible expressions in 3,382
files**. It rejects 24,625 property-access nodes; these are static sites, not
executed lookup counts. It deliberately does not lift the measured
`_bufferData`, `_engine._features`, pipeline-tree or binding-resource paths.
The existing hand-written uniform-policy hoist therefore is not evidence of
a completed generic property-hoisting pass. Its M4 A/B/A had no demonstrated
gain. Neither experiment has an A10X speedup receipt.

Reproduce the audit without changing source or bundle output:

```sh
node --test Apps/scripts/testLoopInvariantTransformer.mjs
node Apps/scripts/auditBabylonLoopInvariants.mjs build_babylon_bundle/source /tmp/loop-invariants.json
```

Before enabling a pass: use the A10X source attribution, prove effects/alias
constraints for the relevant property reads (including subclass/callback
mutation), preserve the build receipt and source maps, then run uninstrumented
moving-camera A/B/A with exact fixed-pose checks and matched thermals. More
compression passes or globally assuming pure getters does not supply that
proof.

### A10X Source Sampling Receipt

The installed older SDR/slab-v3 baseline ran at 1920x1080 with AR off, the
existing accessor-inlined bundle, and a moving camera. The source map is from
the retained baseline bundle whose SHA-256 is
`081c385eb68a58619ca6bfad4007435c384ce66397e720a9fb6c30a05ade5812`.
No replacement bundle, shader or native binary was installed for this run.

The public system JSC exposes its opt-in diagnostic profiler when launched
with `JSC_exposeProfilersOnGlobalObject=true`. The validation helper starts
five seconds after `moving-benchmark:timing`, not at scene creation, and
samples for 20 seconds with `JSC_useJIT=false` and `JSC_sampleCCode=false`.
There are no per-call probes. JSC's dump uses the application temp directory,
not the screenshot directory; its filename prefix must not be an absolute
path. This is a diagnostic facility, not a new runtime API.

Playground PID 7322 exited zero. Sampling ran from epoch ms 1794800306952 to
1794800326973; the recorded sample span is 20.018 seconds. The trace contains
**14,296 leaf samples**: 12,213 LLInt, 1,833 host, and 250 unknown-executable.
Largest named/source-resolved groups:

| Source / method | Leaf samples | Share of all samples |
| --- | ---: | ---: |
| PBR material `bindForSubMesh` | 882 | 6.17% |
| Uniform buffer `updateUniform` | 656 | 4.59% |
| Mesh `render` | 346 | 2.42% |
| Scene `_evaluateActiveMeshes` | 328 | 2.29% |
| PBR material `isReadyForSubMesh` | 322 | 2.25% |
| Transform node `_isSynchronized` | 302 | 2.11% |
| Command stream `tryAppend` | 289 | 2.02% |
| PBR material `_prepareDefines` | 280 | 1.96% |

Source-mapped hot expressions include `_bufferData` (118 samples),
`_uniformLocations` (107), transform dirty-flag expressions (95 and 51 at two
sites), pipeline-tree `values[this._states[i]]` (94), and material/sampler
state. These are samples at associated bytecode/source locations, **not**
executed read counts, cache-miss counts or an exclusive cost per property.
The source map identifies names/sites; retained minified source excerpts
also verify the expressions, since transformed `sourcesContent` line text
does not always align exactly with original TypeScript line numbers.

Four motion receipts within the sampling interval show changing alpha, beta
and view matrices, with 58-80 active meshes. Reported thermal state stayed
nominal; Metal allocation remained 246.64 MiB after warmup. The renderer
reported no error and no command-stream disable. Sampling/dumping inflated
resident memory and introduced a long pause, so **neither FPS nor process
memory from this diagnostic is a sustained baseline**.

The results support prioritizing repeated uniform/material-state work and
readiness/transform checks. Most of those sites are not invariant lexical
loop expressions. The generic hoist must not freeze dirty flags, cached
arrays that copy-on-write replaces, material overrides, or multiple-view
state. A narrowly tested state/version optimization is a separate candidate
from a general compiler pass; neither has a new speedup receipt yet.

Artifacts under `/private/tmp/bn-a15-20261115`:

- `a10x-jsc-receipt.json`, `a10x-jsc.log`: launch and completed motion/sampling.
- `a10x-jsc-stacks.json`: unchanged device profiler output.
- `a10x-jsc-hot-sites.json`, `a10x-jsc-summary.json`: derived attribution.
- `bundle-original/`: matching source map and baseline bundle.
- `analyze-jsc.mjs`: repeatable source attribution, using one-based JSC columns.

`native_xr_portal_jsc_sampling.js` loads after the moving benchmark and before
the demo. Its seven focused tests cover missing profiler functions, warmup,
duplicate timing events, normal completion and disposal before/during sampling:

```sh
cd Apps
npm run test:portal:jsc-sampling
```
