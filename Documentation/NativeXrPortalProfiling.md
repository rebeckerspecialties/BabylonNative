# NativeXR Portal Device Profiling

Chronological checkpoints: November 12-13, 2026. The latest profiling workload
and measurements are in "Accepted AO Workload and Buffer-Size Comparison" below.
These are physical-device receipts, not a claim that the XS performance target is met. The reconciled dependency pins are in
[WgpuIntegration.md](WgpuIntegration.md).

## Configuration and Acceptance

- iPhone XS Max, A12, iOS 18.7.10, UDID `00008020-001C292A2190003A`.
- Release build in `build_int_ios`, public JavaScriptCore, LTO disabled in this
  build cache. Do not label these measurements Release/LTO results.
- Rebecker Specialties signing, team `4GWK4YU2PW`, certificate
  `Apple Development: Matthew Donovan Hargett (79G4CD2XGC)`.
- Local ASTC Hill Valley GLB, environment cube, glow and post-placement SSAO2.
  No quality or resolution changes were made for the comparisons below.
- SSAO2 settings: ratio 0.30, blur ratio 0.45, eight samples, radius 0.85,
  strength 0.42, base 0.76. It remains deferred until placement.
- Material audit: 136 meshes, 134 scene materials, 123 PBR materials;
  backdrops 7/7, emissive backdrops 7/7, reflective environment 11/11,
  lamp emissive 1/1, zero reported override issues.
- User confirmed the corrected portal is upright and fixed to the floor
  while raising/lowering and tilting the phone. CoreDevice display screenshots
  show the camera, doorway, buildings and Delorean. This is separate from
  full visual-parity acceptance, frame rate and memory-lifetime acceptance.

The ARKit compositor previously sampled the app texture's row zero at the
bottom of the screen. Correcting only app UV V to `(1 - clipY) / 2` restores
WebGPU's top-left texture convention without altering ARKit camera UVs or
world-space poses. Two tests pin top/bottom/center and projected points on
both sides of the screen at multiple drawable heights.

## Post-placement CPU and GPU Baseline

The synchronized-compositor run at `0657e830`, PID 3616, rendered about
9-12 fps after placement, versus 58-61 fps before placement. Its steady logs
reported 17 passes per frame, approximately 103-160 draws depending on the
phone's view, and 336 MiB estimated GPU storage. Submit count was one per
rendered frame. The command recorder was active; this is not a comparison
against the older unbatched prototype.

Metal System Trace recorded six seconds of the placed, upright scene:

| Signal | Measurement |
| --- | --- |
| Display intervals | 56 |
| Mean displayed interval | 102.39 ms, 9.77 fps |
| Median displayed interval | 100.01 ms |
| Interval range | 83.34-133.35 ms |
| Process GPU active-time union | 3.354 s over 6.009 s, about 56% |
| Fragment channel cumulative time | 3391 ms |
| Vertex channel cumulative time | 325 ms |
| Compute channel cumulative time | 7.7 ms |

The process GPU intervals include substantial `SSAOOriginalSceneColor`
(1714 ms cumulative), `BlurH` (661 ms), `BlurV` (340 ms), `ssaoCombine`
(327 ms), and `gBuffer` (293 ms). The target labeled `ssao` was 53 ms.
These are accumulated labeled intervals, not exclusive per-frame wall times:
channels overlap and a pass can have several intervals. The result identifies
both scene/PBR fragment work and the AO pipeline, not merely a slow AO kernel.
The destination names must not be read as shader names: PostProcessManager
activates the next post-process input before drawing the current one. In this
chain, `ssao` receives the original-color copy, `BlurH` receives the AO kernel,
`BlurV` receives horizontal blur, and `ssaoCombine` receives vertical blur.

A separate 12-second Time Profiler recording of a placed run, PID 3424,
contained 14.719 weighted sample-seconds across process threads. JavaScriptCore
was the largest leaf library at 6.470 weighted seconds (about 44%); AppleCV3D
and Recon3D accounted for 1.587 and 1.574 respectively. Sample weights are not
literal elapsed CPU seconds. JavaScriptCore includes engine/binding activity,
so this does not isolate application JavaScript from marshaling. The XS does
not supply the iPhone 12's low-level CPU counter evidence.

## Queue Ordering

NativeXR now receives wgpu's actual Metal command queue, not a second queue
created only from the same Metal device. A native GPU test uploads sixteen
distinct frames through wgpu and reads them through a native Metal blit on
that queue, with no CPU wgpu-completion wait, checking all four pixels.

`BABYLON_NATIVE_XR_ASYNC_COMPOSITE=1` remains an Apple-only, opt-in experiment.
The default retains the CPU wait. A shared-queue device run briefly reached
15 fps but later fell to 7-9 fps, with a different phone view/draw count. It is
not a matched-view speedup or justification for enabling asynchronous
compositing by default.

## Memory Investigation

Device kernel logs identify two earlier exits as `per-process-limit` jetsam,
about 2,148,355 KiB, after 256 seconds in the placed run and 146 seconds in an
unplaced run. The latter stayed near 102 MiB estimated GPU storage. Thus this
failure also occurs without the full placed scene or SSAO chain.

Allocations plus one-second VM Tracker snapshots showed growth mainly in the
WebKit/JSC region, rather than live ordinary malloc allocations. A 2 GiB JS VM
reservation is mostly uncommitted and is not itself evidence of a 2 GiB leak.

The first scoped change shares render-pass functions on the realm's
`GPURenderPassEncoder.prototype`, instead of creating fresh functions for
every pass. Device ownership is taken from the receiver at invocation; no
first-device capture or JS-owned prototype reference is added to a device.
Tests cover repeated passes, two devices, method identity, successful submits,
and foreign-resource error delivery in both device directions.
Temporarily removing receiver-based dispatch from `setVertexBuffer` made the
ownership test fail; restoring it returned all 21 native tests to green.

The same 75-second Allocations settings and portal entry point gave:

| VM Tracker end snapshot | Before | Shared pass methods |
| --- | --- | --- |
| WebKit Malloc dirty | 490.92 MiB | 182.19 MiB |
| Total dirty | 871.52 MiB | 635.84 MiB |
| WebKit Malloc resident | 504.05 MiB | 211.81 MiB |
| IOAccelerator dirty | 147.14 MiB | 198.78 MiB |
| Live ordinary malloc allocations | 146.30 MiB | 134.52 MiB |

The WebKit dirty reduction is about 63%, total dirty about 27%. These are
single-run, instrumented memory comparisons, not a frame-rate benchmark or
proof of a stable heap. Placement was not accepted during these memory-only
launches; scene-state and ARKit variability limit whole-process comparison.
No sustained memory-lifetime fix is claimed from these snapshots alone.

A subsequent uninstrumented run, PID 3919, stayed alive for more than five
minutes before an explicit test-owned termination. It held about 58-60 fps
before placement, with 102.29 MiB estimated GPU storage, and no new
`per-process-limit` receipt. The phone did not place during this memory-only
soak. This exceeds the earlier 146-second unplaced termination window, but
does not prove a stable heap or qualify placed-scene performance.

## Evidence and Next Gates

Raw logs, display captures and traces stay local under `/private/tmp`; display
captures include the user's room and must not be committed or uploaded.
Current receipts:

- `bn-xs-portal-uv-clean-run.log`, `bn-xs-portal-upright-placed.png`.
- `bn-xs-portal-post-cpu-all.trace` and its symbolicated samples export.
- `bn-xs-portal-placed-gpu.trace` and GPU/display interval XML exports.
- `bn-xs-portal-system.log` for the kernel termination reasons.
- `bn-xs-portal-allocations-growth.trace` and
  `bn-xs-portal-allocations-prototype.trace` for the 75-second comparison.
- `bn-xs-portal-prototype-long-run.log` for the unplaced five-minute soak.
- `bn-xs-prototype-all-native-tests.log`: 21 passing native WebGPU tests.
- `bn-xs-prototype-focused-tests.log`: 14 passing XR/JavaScript tests,
  including 49 embedded JavaScript WebGPU cases.
- `bn-xs-prototype-screenshots.log`: six passing material/GUI screenshot tests
  at existing thresholds; no reference or tolerance changes.

Next gates are a longer post-placement memory run, a fresh fixed-view placed CPU/GPU
comparison, and native pass/resource/synchronization improvements followed by
the measured scene/AO cost. Keep portal masking, material semantics, full-scene
visibility and the accepted floor lock intact. Neither snapshot replay with
validation errors nor empty-scene 60 fps qualifies as the 50+ fps target.

## November 13 Profiling and Build Cleanup

Source baseline: `d020af9e`, Babylon.js 9.36.0. The preserved baseline app used
Release without LTO. A fresh `build_portal_ios_release_lto` build enables native
and Rust bitcode LTO, keeps the Rebecker signing identity, and links the public
operating-system JavaScriptCore framework. No JS runtime was built from source.
Build success is not a performance result for that new binary.

Project-local Cargo cleanup removed 2.7 GiB from the previous iOS build, 311 MiB
of unused Debug outputs from the new build, and 551 MiB from the previous macOS
build. Built baseline applications and prior test evidence were retained.
With user approval, the Homebrew downloads directory (3.0 GiB) and npm download
cache (516 MiB) were removed. Git metadata, source checkouts and active npm tool
environments were not deleted. Free space subsequently measured about 8 GiB;
this is not the sum reclaimed because builds, traces and other processes also
use the volume.

The multi-config Cargo rule previously invoked both Debug and Release even
for a Release-only build. It now builds the selected configuration only, and
single-config MinSizeRel selects Cargo release. Twelve fake-Cargo integration
cases cover Debug, Release, RelWithDebInfo and MinSizeRel with Ninja, Ninja
Multi-Config and Xcode, including profile flags, environment and selected
library paths. Regenerate existing Xcode projects after changing CMake files.

New device receipts under `/private/tmp/bn-portal-20261113`:

| Capture | Display intervals | Mean interval | Thermal state |
| --- | ---: | ---: | --- |
| XS baseline, `xs-baseline-gpu.trace` | 61 | 94.82 ms (10.55 fps) | Nominal |
| iPhone 12 baseline, `iphone12-placed-gpu.trace` | 91 | 70.46 ms (14.19 fps) | Fair |
| iPhone 12 async compositor, `iphone12-async-gpu.trace` | 86 | 52.37 ms (19.09 fps) | Serious |

The handheld views and thermal states differ; the last two rows do not establish
a controlled speedup. The asynchronous compositor remains opt-in. Both phones
reported active command recording, 17 passes and one WebGPU submit per frame;
draw count varied with view. No scene-resolution or effect settings were reduced.
The XS GPU active-time union was 3.786 seconds in a 6.465-second trace. The
iPhone 12 baseline had 3.700 seconds in 6.806 seconds. Fragment work dominates
the labeled channel intervals; scene-color rendering is the largest target.

The iPhone 12 limiter capture contains real raw `gpu-counter-value` samples
despite an empty derived `metal-gpu-counter-profile` table. Resolve counter IDs
through `gpu-counter-info` and the actual A14 accelerator ID, 4294968855.
There are 35,265 samples per counter spanning 0.093-6.736 seconds. Texture sample,
texture cache and ALU limiters reach 100%; every recorded Partial Renders Count
sample is zero. These are GPU-wide samples, not process-exclusive counters or
proof that any one shader spills registers. Percentage counters require weighted
accumulation; simple sample means must not be presented as time utilization.
The 479 MiB derived XML export was deleted after inspection; its compact original
trace and counter definitions were retained. Do not export all derived interval
tables on a nearly full disk.

The XS 10-second CPU trace contains 12.098 weighted sample-seconds across app
threads: JavaScriptCore 4.196, AppleCV3D 2.393 and Recon3D 1.540. The iPhone 12
CPU trace identifies LLInt property access as a prominent leaf. Its JS-thread
samples were predominantly on performance cores (177 of 181), not evidence of
an efficiency-core-controlled benchmark. The nominal eight-second PMU capture
only has counter buckets from 0.280 to 0.735 seconds; this is inadequate for a
steady-state cache/branch conclusion. A subsequent manual-counter recording
failed during host-side packaging when disk space was exhausted. An app-scoped
retry could not attach even though CoreDevice still listed the process. Neither
attempt is a valid cache-miss or branch-miss result.

CoreDevice display captures show camera composition, buildings, the reflective
Delorean and doorway glow on the iPhone 12. The user reports the anchor is not
exactly on the floor and low cadence makes jitter difficult to judge. Therefore
floor registration/stability is not accepted for this checkpoint. The captured
sky is very bright/white and is not proof of the original blue-sky parity.
The 50+/60 fps post-placement target remains unmet.

For the next CPU attribution run, optionally load
`app:///Scripts/native_xr_portal_profile.js` between the manual-validation and
portal scripts. It uses SceneInstrumentation after placement creates the AO
geometry renderer, reports two-second window averages, and removes its sampling
on scene disposal. Its phase times are overlapping CPU-side wall times, not GPU
durations. Without this prelude, normal portal runs do not install instrumentation.
Five Node tests cover unavailable instrumentation, zero/one/multiple frames,
window reset and disposal.

The new Release/LTO XS run (`xs-lto-phases.log`) measured 1,744 frames after
discarding the first four sampling windows: frame CPU wall time 34.29 ms,
active-mesh evaluation 2.82 ms, render targets 1.82 ms, drawing 22.13 ms,
particles 1.87 ms and animation 0.011 ms. These overlap and must not be summed.
Its scene still ran near 11-12 fps. LTO alone did not recover the target.
All 103 packaged KTX files contain mip chains (six to twelve levels); the
environment cube has eight. This checks asset contents, not every runtime view.

### AO Configuration and Invalid Comparison

The published 9.36 WGSL computes
`clamp(1 - totalStrength * occlusion / samples + base, 0, 1)`.
For finite valid samples, the normalized occlusion lies between zero and one.
The portal's strength 0.42 and base 0.76 therefore clamp even maximum occlusion
to white: these settings cannot produce AO contact darkening. Do not describe
the enabled chain as evidence of working contact shadows. Restoring useful AO
requires retuning and visual validation, not simply disabling the pipeline.

The optional `native_xr_portal_profile_ao.js` prelude, loaded after the CPU
profile prelude, schedules 30-second baseline/bypass/restored windows. It
detaches the AO pipeline during bypass and sets the retained geometry target
to refresh-once; it restores the prior refresh rate and camera attachment.
Six Node tests cover missing prerequisites, timing, restoration and disposal.
This is diagnostic only and does not alter normal demo settings.

`xs-lto-ao-comparison.log` recorded about 11-12 fps, 17 passes and 158 draws
per frame before bypass, versus 25-27 fps, 11 passes and about 82 draws during
the steadier bypass windows. CPU frame wall time dropped from about 34 to
25 ms. The console disconnected before the restored phase was recorded;
CoreDevice still listed the application, so this was not proof of a crash.
The user reported a tiny/sliding portal that could not be entered. These are
diagnostic costs, not an accepted visual or performance improvement. The
display capture `xs-ao-comparison-display.png` does not establish its phase.

### Placement Correctness Follow-up

The problematic comparison accepted an infinite-plane extension 1.432 metres
outside the floor's already-expanded bounding box. Its placed position was
about 4.5 metres from the camera and 2.2 metres below it. The demo now checks
the actual world-space floor polygon with a 5 cm edge tolerance, rather than
extending its bounding box by 18 cm plus a further 1.5 metres. Nine Node tests
cover empty/invalid geometry, both winding orders, rotated/transformed and
concave polygons, boundary noise, multiple floors, and the observed extension.
This rejects a demonstrated bad candidate; it does not alone explain sliding.

NativeXR also applied reference-space offsets to viewer poses but ignored them
in hit-test, plane/anchor, joint and bulk pose reads. Anchor creation likewise
ignored the supplied reference space. Those reads now apply the inverse offset;
anchor creation applies the forward offset. Four native tests exercise identity,
positive/negative translation, both rotation directions, ordered offset chains
and round trips through the real JavaScriptCore-backed reference-space objects.
All 16 reference-space/compositor/image-lifetime tests pass on M4. The iOS
Release/LTO build compiles and is signed with the same Rebecker identity.
Temporarily replacing the read conversion with the old identity behavior makes
three of the four reference-space tests fail. Restoring the conversion returns
all 16 focused native tests to green (`reference-space-tests-final.json`).

The cube registration log contains a nonzero reference-space translation, so
its raw ARKit-world point and unadjusted Babylon-local point are not the same
point. Do not report their screen-coordinate difference as projection error.
The ARKit source currently pulls camera textures and pose from one retained
frame; inspection did not establish a camera/pose frame-mismatch regression.
The corrected normal portal is being tested separately in
`xs-reference-space-fixed.log`, without AO bypass or asynchronous composition.
Floor lock, real-world scale, walking through the doorway and sky appearance
require renewed manual acceptance before post-placement FPS comparisons resume.
The first corrected launch remained unplaced during observation, reporting no
semantic floor planes. This is not a failure receipt without an active manual
placement attempt. A final normal launch is left open for the user's check;
the diagnostic AO prelude and CPU phase instrumentation are not loaded.

### Corrected XS Acceptance and AO Follow-up

The user confirmed the next corrected XS run was full-size, floor-fixed, and
walkable, with a normal sky. `xs-corrected-manual-rerun.log` records that run;
`xs-corrected-accepted-display.png` is a device display-compositor capture from
inside Hill Valley. The same user reported missing contact shading under the
Delorean. Placement acceptance does not establish AO correctness or 60 fps.

The AO base setting is now zero, and explicit zero base/strength overrides are
preserved instead of replaced through `||`. Seven tests exercise the real
configuration routine, including unoccluded/partial/full occlusion, zero and
nonzero overrides, unchanged resolution/sample count, and unavailable inputs.
Two tests fail against the old settings and pass after the correction.

`xs-ao-base-fixed.log` confirms base zero on device, but the user then observed
a dark screen-following stripe rather than scene contact shading. This remains
an AO rendering defect; the setting correction alone is not a visual fix.
The optional `native_xr_portal_ao_diagnostics.js` prelude, after the CPU timing
prelude, cycles combined output, linear depth (0-10 metres), normals, raw AO,
filtered AO, original color and restored combined output in 15-second windows.
Three tests cover absent inputs, ordering/bindings/restoration and disposal.
Do not use its screenshots or frame times as normal-render performance results.

XS Activity Monitor, Metal and CPU capture attempts disconnected immediately
while the application remained available through CoreDevice. The memory trace
contains only 0.594 seconds and no Playground process. These captures are not
valid memory/CPU/GPU evidence. For device-side sampling independent of that
connection, launch with `BABYLON_NATIVE_PLAYGROUND_PROFILE_MEMORY=1`. The iOS
host logs process physical footprint, resident bytes, Metal device allocated
bytes and thermal state every two seconds; the timer is absent by default.
These unified-memory accounting categories overlap and must not be summed.
The renderer's existing estimated GPU bytes remain a separate, narrower metric.

### AO Projection Reproduction and Memory Growth

#### Upstream SSAO2 Fix Audit

[Babylon.js #18539](https://github.com/BabylonJS/Babylon.js/pull/18539), merged
June 8, 2026 as `0ff49b31a813b2a79f4db1e24276707c2cf59529`, fixes world-space
normal handling, not position reconstruction. Its normal-space selection,
inverse-transpose mat3 upload, and GLSL/WGSL normalization and conversion are
all still present in the actual 9.36 UMD packaged in the XS app. Four executable
checks in `Apps/scripts/testBabylonPublishedSsao.mjs` pass against both the Apps
input and packaged app file (matching SHA-256
`7edf5405501eee0ea55e31d7501f4ac17c7ca87dfc8e3227bb3cd99499309dbc`).

The FOV/window-aspect reconstruction predates #18539: its form is present in
November 2025 commit `a4594aabee` (#17388), and is unchanged by #18539. That PR's
tests cover normal-space contracts, not differing XR/window aspect ratios.
The May 20 asset baseline `5bdf34a7` already set AO base to 0.76 with strength
0.42, which saturates the result to one. Thus the available history supports
a retained normal-space fix plus a separate, previously masked projection bug,
not a reverted upstream fix. It does not establish when an earlier manually
accepted build last had correctly configured, effective AO.

`xs-ao-projection-manual.log` records a placed validation run with screen aspect
1.7781040268456376 and projection aspect 0.4620536010711669; principal-point
offsets are -0.0009465813636779785 and 0.0025517940521240234. This directly
confirms the aspect mismatch on XS, not only the synthetic Mac reproduction.
Manual shading acceptance failed: the user saw shading under the body disappear
while panning and could not see tire/curb contact shading. Do not promote the
projection-only candidate as a complete fix.

#### Follow-Up Coordinates and Kernel Validation

`xs-ao-coordinates.log` and the seven `xs-coordinates-*.png` CoreDevice display
captures record a placed input/filtered-output inspection. The G-buffer and AO
use identical view/projection matrices and normalized full viewports. The next
run, `xs-ao-kernel-manual.log`, also records full pixel viewports and no active
scissor. The G-buffer is nevertheless 4239x2384, while the XR input is
1242x2688 and the AO output is 372x806. Different normalized texture dimensions
do not by themselves prove a coordinate defect. The oversized G-buffer is a
separate allocation/bandwidth investigation, not yet a validated optimization.

The eight-element `sampleSphere` array is indexed modulo 16 in the published
shader. The validation prelude now bounds that index by `SAMPLES` (leaving the
independent 16-entry radius-scale table alone) and subtracts the asymmetric
projection offsets during reconstruction. It fails hard if the published WGSL
pattern changes. Existing zero-depth rejection and #18539 normal conversion
remain intact. This is still opt-in validation, not a generalized source fix.

The user again could not reliably see tire/contact shadows after the kernel
candidate. Fixed-camera M4 captures `m4-ao-strength-{off,current,strength,contact}.png`
compare strength 0, 0.42, and 1.2, then reduce epsilon from 0.04 to 0.01. The
stronger/smaller-bias candidate shows a clearer shadow under the car in that
view. `native_xr_portal_ao_contact_validation.js` packages a 15-second-per-phase
current/off/contact/off/contact comparison at unchanged sample count and
resolution. It leaves the contact candidate active for inspection; ordinary
demo defaults are unchanged. On-device visual acceptance is pending.

#### JavaScriptCore Class Metadata Retention

The Node-API bridge created a new `JSClassRef` for every function, external,
wrapper, and reference sentinel. JavaScriptCore retains per-class context
metadata even after the JS instances are collected; see
[WebKit's context-data ownership](https://github.com/WebKit/WebKit/blob/main/Source/JavaScriptCore/API/JSClassRef.h).
The fix shares the immutable class within each sentinel kind while retaining
per-object callback/data/finalizer state. Constructor classes remain separate
to preserve independent user-visible prototypes.

A system-framework-only M4 reproduction creates 100,000 short-lived objects:
per-object classes grow physical footprint from 4.16 to 55.56 MiB; a shared
class remains near 5.48 MiB. Finalizers run in both cases. No runtime was built
from source. Two new class-reuse tests fail before the fix and pass after it;
opposing constructor-prototype and multi-context-finalization tests also pass.
The complete native runtime suite passes 93 tests, and JavaScript.All passes
426 cases. An initial Worker test run from the repository root failed to find
its fixtures; the recorded passing run uses the test executable directory.

Rebecker-signed Release/LTO installation sequence 10200 was run on XS with the
same graphics settings (`xs-jsc-classes-memory.log`). Excluding 60 seconds of
warmup in each phase:

| Window | Duration | Physical footprint | Metal allocated |
| --- | ---: | ---: | ---: |
| Earlier unplaced control | 220 s | 588.83 -> 976.53 MiB | 303.91-311.84 MiB |
| Fixed unplaced | 84 s | 479.85 -> 475.99 MiB | 296.97-312.84 MiB |
| Earlier placed shader candidate | 220 s | 906.97 -> 1059.31 MiB | 594.19-641.77 MiB |
| Fixed placed | 90 s | 809.78 -> 811.92 MiB | 591.17-652.06 MiB |

The fixed placed window includes a transient peak of 832.16 MiB. These are
different-duration manual views, not a controlled FPS benchmark or proof of
unbounded-session stability. The strong monotonic growth is no longer present
in this bounded check. Pre-placement remains 60 fps; placed rendering remains
about 10-12 fps, so this fixes a memory-retention mechanism, not the frame-rate
goal. The run was stopped after collecting the comparison.

#### Earlier Projection Diagnostics (Historical)

The first diagnostic shader omitted the default `scale` and `textureSampler`
bindings. Those bindings are now explicit and covered by the diagnostic test;
discard `m4-ao-diagnostics.log`. The corrected non-AR run
`m4-ao-diagnostics-final.log` produced aligned depth/normal buffers and AO
following scene geometry. This does not qualify the XR AO path.

A second fixed-camera test deliberately froze a portrait projection while
retaining the landscape render surface. `m4-ao-aspect-repro.log` and
`m4-aspect-raw-ao.png` reproduce a center-band AO artifact. Babylon.js 9.36
SSAO2 reconstructs positions using `camera.fov` and
`engine.getAspectRatio(camera, true)`, which reads the screen dimensions,
instead of the supplied projection scales. Binding `1 / projection[0]` and
`1 / projection[5]` removes that band in the same fixed scene: see
`m4-ao-projection-corrected.log` and `m4-projection-corrected-raw-ao.png`.
AO then follows the Delorean wheels/body and building edges across the frame.
The earlier `m4-ao-projection-fixed.log` trial is invalid: its `onApply`
correction was overwritten by `EffectWrapper.bind`.

`native_xr_portal_ao_projection_validation.js` packages this opt-in experiment,
using `onBeforeRender` after normal uniform binding. Load it after the profile
and optional AO diagnostic preludes, before the demo. Six tests cover absent
inputs, no/one/multiple frames, live projection updates, orthographic cameras,
invalid scales and disposal. It does not run in the normal demo. This is a
centered-perspective validation, not the complete upstream fix: the durable
Babylon.js change must also account for asymmetric projection offsets in both
GLSL and WGSL. No source changes were mixed into the unrelated dirty Babylon.js
branch, and no patched Babylon.js bundle has been shipped.

The XS diagnostic retries did not place, so no device AO input captures were
obtained. Do not interpret this as a placement regression without an active
manual attempt. The latest run was stopped rather than left allocating.
On-device confirmation of the projection fix is still required before timing
comparisons or any claim that AO is visually corrected.

The final Release/LTO app passed strict signature verification with Rebecker
Specialties team `4GWK4YU2PW` and was installed on the XS (installation sequence
10176). It is left stopped. The next manual normal-color candidate run is:

```sh
xcrun devicectl device process launch --device C7A1D5AD-C89D-5E03-99B9-5E65EEC30486 --terminate-existing --console --environment-variables '{"BABYLON_NATIVE_PLAYGROUND_PROFILE_MEMORY":"1"}' com.rebeckerspecialties.BabylonNative.Playground app:///Scripts/native_xr_portal_manual_validation.js app:///Scripts/native_xr_portal_profile.js app:///Scripts/native_xr_portal_ao_projection_validation.js app:///Scripts/native_xr_babylon_portal_demo.js
```

Add `native_xr_portal_ao_diagnostics.js` before the projection prelude only when
collecting diagnostic attachments. The normal-color candidate still needs a
full-size/floor-lock/walk-through check, plus tire/road contact and curb shading
while panning. The projection prelude logs screen and projection aspect ratios
and principal-point offsets on its first AO frame.

The first memory sampler read `MTKView.device`, which is nil because the native
renderer assigns `CAMetalLayer.device` directly. Its zero Metal values in
`xs-ao-diagnostics.log` are invalid. The corrected sampler reads the layer's
device and returns NaN if unavailable. In `xs-ao-diagnostics-observed.log`,
after 60 seconds of warmup, 111 samples cover 220 seconds: physical footprint
rises from 588.83 to 976.53 MiB (105.74 MiB/min), while Metal allocated bytes
stay in 303.91-311.84 MiB and renderer-estimated GPU memory stays at 102.29 MiB.
The run is unplaced at about 60 fps, starting thermally nominal and ending fair.
This is real process-footprint growth, but not evidence of monotonically
retained full-resolution Metal textures. CPU/JS, ARKit and driver allocations
still require attribution; the categories must not be added together.

The reference-space correction follows the current
[WebXR pose contract](https://immersive-web.github.io/webxr/#dom-xrframe-getpose),
[hit-test pose contract](https://immersive-web.github.io/hit-test/#dom-xrhittestresult-getpose)
and [anchor creation contract](https://immersive-web.github.io/anchors/#dom-xrframe-createanchor):
poses are relative to the supplied effective origin, and anchor input is converted
back to the native origin at frame time. This is not a claim of full draft
conformance. NativeXR still needs separate coverage for arbitrary base XRSpaces,
session ownership, inactive-frame errors, null untracked poses and immutable
retained frame/pose snapshots. None of the profiling changes should weaken those
contracts or substitute stale viewer poses to improve frame-rate numbers.

## Accepted AO Workload and Buffer-Size Comparison

On November 13 the user accepted the stronger AO candidate as a profiling
placeholder: the under-car shadow is visible and no longer cuts off while
panning left. Tire/curb contact shadows remain unresolved; this is not full
visual-parity acceptance. The normal sky, real-world size, floor lock and
walk-through had previously been accepted separately.

`native_xr_portal_profile_workload.js` fixes strength at 1.2 and epsilon at 0.01
without changing the eight samples, 0.30 AO ratio or 0.45 blur ratio. Load it
after `native_xr_portal_ao_projection_validation.js`. The projection and bounded
sample-index corrections are still validation-only; they have not yet been
promoted to Babylon.js source or its durable patch stack. PR #18539 remains
present in the published bundle and was not lost.

The M4 diagnostic's deliberately mismatched portrait projection was removed
from the ordinary preview. `m4-profile-preview.png` records the normal landscape
view. A separate interpreter-only M4 run (`m4-cpu-workload.log`) maintained
60 fps with roughly 12-13 ms instrumented scene CPU time, 135 draws and ten
passes per frame. This is non-AR, a different viewpoint, and the local macOS
Release build has LTO disabled; it is not an iPhone performance comparison.

The iPhone 12 received the Rebecker-signed Release/LTO build, installation
sequence 9624, and ran PID 5452 (`iphone12-gbuffer-comparison.log`). Its synthetic
canvas is 1280x720 logical pixels at DPR 3, giving 3840x2160 backing pixels.
SSAO's geometry buffer inherited those dimensions even though the XR target is
2532x1170. The opt-in `native_xr_portal_profile_gbuffer.js` compares original,
XR-sized, original, XR-sized geometry buffers in 30-second phases. It uses
public render-target sizing and only reallocates on a dimension change. The
AO output, samples, materials, projection and native synchronization stay fixed.

| Signal | Original geometry buffer | XR-sized geometry buffer |
| --- | ---: | ---: |
| Dimensions | 3840x2160 | 2532x1170 |
| Estimated live GPU storage | 286.21 MiB | 194.68 MiB |
| Draws / passes per frame, stable view | 164 / 17 | 164 / 17 |
| Early uninstrumented cadence | about 16 fps | about 17 fps |
| Early scene CPU wall time | about 22 ms | about 22 ms |

This is a 91.53 MiB storage reduction, not a demonstrated durable FPS gain.
Later Instruments activity increased CPU time and reduced cadence; thermal
state also changed from nominal to fair. Do not compare those later windows
as if they were controlled uninstrumented results. A compositor screenshot
(`iphone12-gbuffer-xr-hold.png`) confirms the placed scene remained rendered.
The app exited normally after its bounded ten-minute run.

The valid XR-sized GPU trace (`iphone12-gbuffer-xr-gpu.trace`) contains 110
labeled scene frames and 6.526 seconds of process-attributed interval coverage:

- GPU active-time union: 3.642 seconds, about 55.8% of that interval span.
- Scene/PBR fragment intervals: 2074.5 ms cumulative (about 18.9 ms per labeled
  frame, not an exclusive frame-duration measurement).
- SSAO kernel, whose destination is `BlurH`: 774.0 ms cumulative, about 7.0 ms
  per labeled frame. Do not mistake the destination name for the blur shader.
- Geometry-buffer fragment intervals: 110.0 ms cumulative; vertex: 148.4 ms.
- Camera/screen compositor fragment intervals: 70.0 ms cumulative.

Raw GPU counters contain 29,040 samples per counter in the selected 0.5-6.5s
window. Texture sample/cache and ALU limiters reach 100%; F16 utilization and
Partial Renders Count remain zero. These are GPU-wide counter samples, not
process-exclusive or time-weighted utilization. They do not establish register
spills or attribute cache stalls to a particular shader. The control GPU trace
crosses the final buffer-resize transition and must not be treated as a clean
single-size comparison without time filtering.

The 12-second CPU trace has 13,849 app samples, but no library-load mappings;
its function addresses cannot be reliably attributed to JSC versus native
code. Resymbolication with a matching generated dSYM cannot repair missing
image mappings. Process/thread/core attribution remains usable. The eight-second
CPU Counters capture has 7,675 precise process buckets and 13,788,275,459 counted
cycles. It uses the bottleneck preset, not direct branch-miss or cache-miss
sampling. Keep its precise and coarse duplicate aggregations separate.

Eight new Node tests cover absent geometry, parent/non-XR cameras, the A/B/A/B
schedule, repeated frames without resource churn, rotation, invalid dimensions,
disposal, and the fixed AO workload. The focused Node sweep now passes 63 tests.
No runtime default has been changed to the asynchronous compositor based on
these results; removing the CPU wait remains a separate native-side comparison.

### Native Viewport Sizing

The native Window polyfill now exposes live `innerWidth` and `innerHeight`
using the graphics device's existing logical dimensions. Previously Playground
fell back to 1280x720, then multiplied by DPR even when that did not describe
the real view. The getters preserve host-provided properties and implement
the [CSSOM View replaceable-attribute contract](https://drafts.csswg.org/cssom-view/#dom-window-innerwidth).
Hardware scaling does not change logical viewport dimensions. This does not
implement missing Window resize events or full DOM/Window conformance.

Three new native tests cover one-pixel initialization, landscape/portrait
resizing, scaling independence, property descriptors, assignment/replacement,
reinstallation, and existing host properties. All eight `JavaScript.*` native
fixtures pass, including the embedded WebGPU/Canvas cases. The focused Node
suite passes 66 tests, including the Rust build-profile fixtures (the Xcode
fixtures required access to compiler caches). The iOS Release/LTO app passed strict signature checking
and was installed on iPhone 12 with the Rebecker setup, sequence 9640.
The same build was installed on XS, sequence 10216, and left stopped. This is
an installation receipt, not XS validation of the viewport change.

In `iphone12-window-dimensions.log`, the canvas is now 844x390 logical and
2532x1170 backing pixels. The geometry-buffer comparison reports 2532x1170
in both its original and XR-sized phases: the initial allocation is already
correct. In this estimated-placement view, with the same 156 draws / 17 passes
and stronger AO settings, estimated live GPU storage is 153.99 MiB versus
286.19 MiB before the viewport fix (132.20 MiB less). The earlier geometry-only
resize reached 194.67 MiB; correcting the canvas also removes excess allocation
from other canvas-dependent targets. These are renderer storage estimates,
not measurements of exclusive process memory or an FPS improvement.

`native_xr_portal_profile_estimated_placement.js` is an explicit, three-minute
profiling prelude. It opts into the existing estimated placement; normal demo
floor-hit requirements are unchanged. Neither this run nor its predecessor
qualifies floor lock, tracking, or visual parity. Compositor captures show an
Apple Account Verification dialog over both runs. The older run also reached
serious thermal state. Exclude them from comparative FPS claims. The sizing
run was stopped after obtaining its allocation evidence, allowing the phone
to cool; dismissal of the system dialog and a fresh manual run remain needed.

The first estimated run's raw files are named `iphone12-estimated-wait*`, but
that name is misleading: `BABYLON_NATIVE_XR_ASYNC_COMPOSITE=0` still enables
the existing presence-based environment switch. Its startup log confirms
asynchronous composition. It is not a wait-enabled control. The later sizing
run omits that variable and retains the production CPU wait. No synchronization
default was changed, and these two runs are not a single-variable comparison.

Starting Time Profiler before launch recovered library mappings. The 15-35s
window of the estimated-placement trace has 26,314 app samples: 65.33% have
their leaf in JavaScriptCore, versus 1.85% in the app binary. Inclusive
`command_encoder_finish` is 2.50%, and native descriptor `ToJson` is 1.50%.
The 67.10% inclusive `napi_call_function` includes the entire JS render callback;
it must not be labeled bridge-marshaling cost. These are diagnostic samples
from the UI-obstructed run, not clean manual-run performance shares. The earlier
100s capture was unplaced and likewise cannot establish placed-scene speed.

The M4 `m4-window-dimensions-pumped.log` and screenshot show correct proportions
and material/AO rendering, with native frame submission and roughly 60 fps
under the validation frame driver. The preceding hidden-window attempt
(`m4-window-dimensions.log`) fell back to scene-only timer calls, submitted no
steady frames, and produced no screenshot. Its stale 694 fps value is invalid.
Do not count it as a successful render or benchmark.

Next performance gates: a cool, unobstructed iPhone 12 comparison of the CPU
wait versus ordered shared-queue composition; matching XS verification;
then renderer traversal/draw preparation and the measured PBR/SSAO GPU costs.
Keep AO quality, projection, floor placement and full-scene visibility fixed.
The validation-only SSAO projection/kernel corrections still need a separate
generalized Babylon.js source change and durable patch-stack integration.

### Manual Synchronization Comparison and XS Registration Gate

After the dialog was dismissed, the November 13 evening iPhone 12 runs used
real floor hits, portrait 1170x2532 rendering, the accepted stronger AO workload,
and no estimated-placement or geometry-buffer resizing prelude. The control
`iphone12-manual-sync-baseline.log` (PID 8676) omitted the presence-based async
environment switch. It measured about 18-20 fps, 21-22 ms scene CPU wall time,
159-162 draws, 17 passes, one submit and 15 command streams per frame before
Instruments activity. Estimated live GPU storage remained about 154 MiB.

`iphone12-manual-async.log` (PID 8697) explicitly enabled the no-CPU-wait path.
It initially reached about 30 fps, then declined toward 19-24 fps while scene
CPU wall time increased. The user confirmed full size, floor lock and camera
alignment on this phone. The view and run history differ from the control;
this is not a durable or isolated percentage-speedup claim. Its console stream
stopped reporting before the later GPU capture, although the capture recorded
continued rendering. Do not infer late memory stability from missing log lines.

The control GPU capture failed while saving with an Instruments package-lock
exception and is not valid GPU evidence. The retried async capture saved and
exported successfully: 113 labeled frames over 6.632 seconds, GPU active-time
union 3.313 seconds (49.96%). Cumulative fragment intervals were 1262.2 ms for
SSAO (`BlurH` destination), 1183.8 ms for scene color and 80.6 ms for the camera
compositor. These overlapping intervals are not exclusive frame durations.
The temporary Jest transform cache was removed, reclaiming approximately
1.46 GiB; source, builds and valid profiling traces were retained.

The matching XS async run (`xs-manual-async.log`, PID 6997) rendered at about
24 fps with roughly 160 draws, and about 30 fps after the view changed to
roughly 95 draws. It used 1242x2689 canvas/geometry targets and 1242x2688 XR
targets. Estimated GPU storage stayed near 168.4 MiB; the observed warmed
process footprint was roughly 610-622 MiB. Its valid GPU capture has 162
labeled frames over 6.383 seconds and a 54.92% active-time union. This is a
different view, not an XS-versus-iPhone-12 throughput comparison.

Crucially, the user observed sliding on XS while panning horizontally and
vertically. Restoring the original CPU wait (`xs-manual-sync-control.log`,
PID 7011) did not remove it. The async path remains opt-in; neither this
comparison nor the earlier iPhone 12 acceptance qualifies XS floor lock.
Later control windows reached serious thermal state and must not be used as
a cool-device performance baseline.

Review found a separate camera lifetime defect: retaining the `MTLTexture`
alone does not satisfy Apple's requirement to retain the `CVMetalTextureRef`
wrapper through GPU completion. `CameraTextureSnapshot` now holds both Core
Video owners in completion blocks for the normal compositor and optional
camera precomposition. No camera pixels are copied. A failed texture creation
also no longer releases an uninitialized output pointer. Five native lifetime
tests cover missing and individual planes, copied completion ownership,
multiple in-flight consumers, and actual Objective-C block capture; they pass
on M4 Metal, along with both existing compositor-coordinate tests.

The Rebecker-signed Release/LTO correction passed strict signature validation
and was installed on XS (sequence 10224, PID 7084). The user still observed
sliding with the original CPU wait. This fix is therefore not the established
cause of the registration defect. The warmed process footprint stayed near
607 MiB in the observed interval, not a long-session memory qualification.

The renderer is ARKit plus a custom Metal compositor, not RealityKit ARView.
It requests gravity-aligned world tracking and horizontal/vertical planes,
conditionally requests scene reconstruction on supported devices, and disables
light estimation. It does not request person-segmentation frame semantics or
RealityKit scene-understanding occlusion. Those occlusion features are not a
substitute for correct camera/pose registration.

The opt-in `native_xr_portal_registration_validation.js` prelude pairs with
`BABYLON_NATIVE_XR_TRACE_REGISTRATION=1` to compare a fixed native world point
through ARKit and Babylon. The native trace also reports the difference
between the cached projection and current ARCamera calibration. Five Node tests
cover handedness, translated/rotated reference spaces, absent cameras and
tracking, repeated frames and disposal. This verbose diagnostic is not a
performance benchmark.

The native calibration trace (`xs-registration.log`, PID 7089) found a stale
projection in 3211 of 3213 samples: median maximum matrix-element difference
0.0307891, maximum 0.1847341. The first frame also used a zero-sized viewport
and produced a nonfinite projection. Projection and camera UVs are now updated
from the current ARFrame every frame, after initializing/resizing the XR view
textures and assigning the current near/far planes. They are no longer cached
until orientation or viewport size changes.

The corrected Rebecker-signed build was installed on XS (sequence 10240,
PID 7105). All 1930 samples checked in
`xs-current-calibration-registration.log` had zero calibration difference,
without zero-sized or nonfinite projections. The user reported definitely
reduced sliding, but the low frame rate prevented full stability acceptance.
This establishes a calibration bug, not proof that all registration latency
has been eliminated. The subsequent uninstrumented async run reached serious
thermal pressure and was stopped to cool; it is not a normal-temperature
performance baseline.

The first diagnostic installation did not contain the new JS resource; an
explicit CMake reconfigure corrected packaging. Its native calibration trace
remains valid, but it has no paired JS projection evidence. The next JS
diagnostic failed to account for the offset XR reference space. The helper now
copies the reused base pose before requesting the current reference pose and
transforms the comparison point between those spaces. The corrected helper
passes the five tests above but has not yet been qualified on device. Do not
attribute the earlier apparent pixel discrepancy to the renderer.

`native_xr_portal_profile_materials.js` provides an opt-in 30-second A/B/A
comparison of normal, frozen, then restored materials after placement. It uses
the public material API, preserves initially frozen and newly added materials,
and restores changed materials on disposal. Four tests cover zero/one/many
materials, phase boundaries, repeated frames and disposal in each phase. No
mesh visibility, resolution or AO setting changes as part of this comparison.

The XS comparison (`xs-material-comparison.log`, PID 7132, installation
sequence 10248) produced no clear improvement at the freeze transition.
Excluding boundary/startup windows, frame-weighted scene CPU times were
30.48 ms normal, 33.56 ms frozen and 39.55 ms restored. Thermal state progressed
from nominal/fair through serious, so these are not isolated material-freezing
effects. GPU estimates stayed around 168.4 MiB and the warmed process footprint
around 590-596 MiB in this bounded run. The display capture retains the portal,
car materials and under-car shading. No material-freezing default was enabled.
The phone was stopped to cool.

### Optional Shared Pre-pass Comparison

The opt-in `native_xr_portal_profile_prepass.js` prelude selects Babylon's
existing SSAO2 pre-pass path, instead of its separate geometry-buffer renderer.
It preserves the AO sample budget, resolution and strengths. The ordinary demo
still uses the established geometry-buffer path. Capability absence is reported
explicitly rather than silently substituting a different workload. Configuration
tests cover present/absent renderers and preserve the existing default path.

In local M4 non-AR fixed-camera runs, the new path reduced 135 draws to 70,
10 passes to 8, and estimated GPU storage from 202.27 to 131.96 MiB. Excluding
startup and final windows, frame-weighted scene CPU wall time was 19.15 ms
(529 frames) versus 15.18 ms (958 frames); the draw phase itself stayed around
13.5-13.6 ms, with most of the reduction coming from removing the extra render
target traversal. These are short local measurements, not an iPhone or durable
60 fps claim. The host build is Release without LTO, unlike the iOS build.

`m4-geometry-fixed.png` and `m4-prepass-fixed.png` use identical logged camera
parameters. Materials and geometry remain visible. Mean absolute RGB channel
differences are about 1.4/255; 1166 of 921600 pixels exceed 25 in any channel.
This comparison records a small shading difference, not pixel-exact parity or
acceptance of XR transparency/stencil behavior. Earlier `*-check.png` captures
had different camera views and must not be used as matched visual controls.

The profiling workload now disables per-frame AO coordinate snapshots while
retaining the projection and sample-index corrections. A dedicated test checks
that both coordinate observers are absent in profiling mode and the correction
still runs. The focused portal and published-SSAO Node sweep passes 66 tests.

The optional pre-pass build passed Rebecker Release/LTO signing verification
and was installed on XS (sequence 10256). The launch in
`xs-prepass-validation.log` (PID 7150) remained unplaced for about three minutes;
the display capture showed a very dark camera image and no floor hits were
reported. It was stopped to avoid warming the phone while waiting for manual
placement. This is neither a tracking failure nor a post-placement pre-pass
validation. XR aperture/transparency and shading acceptance, then iPhone 12/XS
CPU/GPU performance comparisons, remain pending. All 21 `NativeXr*` unit tests
pass, including the new camera-owner tests; no experimental renderer or
synchronization default has been promoted.

### XS Shared Pre-pass Manual Acceptance

The rerun `xs-prepass-manual.log` (PID 7165, the same sequence-10256 build)
placed on a real floor hit. The user confirmed full size, floor lock, Hill
Valley confined to the doorway, passthrough outside, and preserved car materials
and under-body AO. `xs-prepass-manual-placed.png` is a display-compositor capture,
not an app-only framebuffer readback. This qualifies that observed view and
interaction, not arbitrary portal configurations or every scene material.

Early settled windows were about 30-31 fps, 25-26 ms scene CPU wall time,
roughly 75-79 draws, 17 passes, one submit and 15 command streams per frame.
The AO workload remained strength 1.2, epsilon 0.01, eight samples, with the
same 1242x2689 canvas and 1242x2688 XR target. The pre-pass did not remove two
passes in this XR workload as it did in the standalone M4 test; inspect the
GPU capture rather than extrapolating the M4 pass savings.

Twenty warmed memory samples before the GPU capture ranged from 593.27 to
605.41 MiB physical footprint and 353.53 to 373.38 MiB Metal allocation. The
renderer estimate settled around 156.7-156.8 MiB. Instruments and subsequent
thermal changes make this an unsuitable isolated FPS comparison with earlier
geometry-buffer runs. It is also too short to qualify long-session memory
stability. The XS reached serious thermal pressure and was stopped after the
captures, rather than kept running hot.

The GLB has 135 primitives using 130 materials, with only four materials shared
across multiple primitives. This limits the scope of state-compatible multi-draw
batching without changing asset/material organization. The remaining CPU work
must be attributed before choosing such a change.

#### Post-placement CPU and GPU Capture

`xs-prepass-manual-gpu.trace` is a valid six-second Metal System Trace of the
same process, including CPU stacks. It was captured at serious thermal pressure;
use it to locate work, not to claim a normal-temperature FPS improvement. For
PID 7165, 108 labeled frames span 6007.67 ms. The union of GPU-active intervals
is 2854.96 ms (47.52%). Summed fragment intervals are 2855.96 ms, vertex intervals
484.14 ms and compute intervals 26.18 ms. Stage intervals overlap and must not
be added together as frame time.

The shared scene pre-pass accounts for 1844.01 ms of cumulative fragment work
across its split passes. The SSAO kernel, whose destination is labeled
`PostProcessRTT-BlurH`, accounts for 306.24 ms. These are accumulated interval
durations, not exclusive per-frame costs. Removing duplicate geometry draws
has not eliminated the multiple scene-target passes, and further AO resolution
cuts are not justified by this capture's GPU headroom.

Of 9603 process CPU samples, 4681 (48.75%) have JavaScriptCore leaf frames.
The main JavaScript thread has 5057 samples; 4401 of those (87.03%) end in
JavaScriptCore. This thread-specific result excludes helper/GC thread samples.
Interpreter property access is prominent: `performLLIntGetByID` contributes
7.89% of process leaf samples and `PropertySlot::functionGetter` another 2.27%.
The 52.3% inclusive `napi_call_function` result includes the JS callback's work;
it is not a measurement of bridge marshaling overhead. Likewise, the 3.7%
inclusive command-encoder-finish samples overlap their native callers.

The separate 90-second Time Profiler attempt failed to save its trace while
free disk space was about 250 MiB. Offline import of its surviving raw recording
recovered process/thread/core samples, but not binary mappings: 99.89% of the
35-55 second window has unknown leaf libraries. Exact sampled-address matching
against the later trace produced no matches. Do not use that recovered window
for function/library attribution or substitute nearest-address guesses. The
CPU conclusions above come only from the valid Metal trace's mapped stacks.

Artifacts are under `/private/tmp/bn-portal-20261113/`: the `*-gpu-toc.xml`,
`*-gpu-intervals.xml`, `*-gpu-summary.json`, `*-gpu-cpu.xml` and
`*-gpu-cpu-summary.json` accompany the trace. `join-cpu-samples.py` records the
thread-specific denominator and unsuccessful recovery check. Completed raw
scratch traces and two older XML exports were compressed, preserving their
contents and recovering about 1.1 GiB. No source, installed build or valid trace
package was removed.

Next, isolate the remaining per-draw JS work in a fixed-camera control, then
repeat on a cooled device. Existing culling direct-field and command-word
encoding branches are candidates to test independently, not assumed wins:
published 9.36 still uses public vector getters in `Plane.dotCoordinate` and
per-word command-stream helpers. The culling phase is only about 2.5 ms here,
while scene drawing is about 20 ms, so culling alone cannot close the gap to
16.67 ms. Attribute readiness/binding, uniform updates, pipeline lookup and
command encoding separately before choosing a generalized fix. Preserve
camera calibration, aperture clipping, AO and material behavior; do not use
active-mesh freezing as a shortcut. One submit per frame and the small native
CPU share also argue against further submit batching as the first target.

Durable 60 fps and long-session memory stability remain unqualified. The
accepted shared pre-pass remains opt-in, and no runtime default was changed
during this profiling rerun.

### Per-draw Uniform Comparison and Moving-camera Checks

The opt-in `native_xr_portal_profile_calls.js` helper samples one in 30 frames
and records nested inclusive/self timings. Six Node tests cover forwarding,
exception handling, sampling and disposal/restoration. Wrappers still add
overhead outside sampled frames. These runs locate work; they do not establish
uninstrumented FPS or tracking parity.

In the M4 interpreter-only shared pre-pass control, 64 uniform-buffer comparisons
per frame consumed about 3.38 ms, nearly all of the 3.52 ms owner-keyed update
path. PBR binding was about 7.2 ms inclusive. Bind-group lookup was only about
0.064 ms and there were no steady-state buffer copies/uploads in that fixed
view. Changing the comparison loop to read the typed-array length once reduced
its sampled cost to about 1.72 ms in a compiled bundle; scene CPU windows were
about 13.6-13.9 ms versus roughly 15.8 ms in the control. A four-element unrolled
variant was slower and was discarded. This supports a narrow JS loop change,
not a bindless-resource or copy-elimination claim.

`Patches/BabylonJS/0001-uniform-buffer-comparison-loop.patch` contains the source
change and 17 focused tests. The comparison, owner-keyed, lifecycle and recorded
buffer suites total 46 passing tests; the ten bundle-contract tests and strict
patch-stack check also passed. The validation bundle was built from the exact
9.36.0 release commit `9495e735517f76d0a9a3661089ecf8f8d7e8b653` in
`build_portal_babylonjs_9_36`, not from the unrelated dirty Babylon.js branch.
The original published files are retained in that directory's
`published-control` folder. The canonical Babylon.js checkout also holds the
small source/test change for later contribution. Nothing has been committed
or published from this experiment.

XS sequence-10264, PID 7327, ran the compiled change with call probes and placed
on a real floor hit. Early windows were around 29 fps, 27 ms scene CPU, 82-85
draws and 17 passes per frame. The user observed vertical sliding. Later
thermal pressure and approximately 595-606 MiB physical footprint do not
qualify sustained performance or memory stability. The subsequent sequence-10272
control, PID 7347, restored the exact original published bundle and removed
the call probes. The user reported sliding gone or clearly reduced. That
changes two variables: the next device comparison must use the compiled change
without call probes before attributing the behavior to either. The XS was
stopped to cool; Apps dependencies and the current iOS app bundle were left on
the original published control. The M4 bundle still contains the compiled change.

The iPhone 12 non-AR run, PID 9619, used an unsuitable default view and is not
a visual/performance acceptance run. Its eight-second CPU Counters trace did
export actual process-attributed nonzero branch and cache events, unlike the
earlier counter attempts. Event availability is established, not an improvement
in miss rates. No normalized or matched-workload PMU comparison is claimed.

M4 validation now includes a moving orbit with changing active-mesh counts and
frame captures; material freezing and active-mesh freezing were not used. The
original-loop and compiled-loop runs had similar visible materials, but this
does not override the failed XS interaction check. Initial comparison PNGs used
the same `compiled-hoist` prefix as a later sky correction and were overwritten;
do not compare those surviving images as an isolated uniform-loop A/B test.
Use distinct artifact names for subsequent comparisons.

Artifacts are in `/private/tmp/bn-portal-20261113/`: the `m4-calls-*` logs,
`m4-moving-*` logs, `xs-ubo-hoist-calls.log`, `xs-original-bundle-control.log`,
and `iphone12-ubo-hoist-cache-branch.trace` with its counter export.

#### Sky Clipping and Door Material Findings

The standalone stress camera's 180 metre far plane clipped the authored `Ciel`
dome, whose node scale is approximately 556 metres. Environment reflections
remained visible because they use a separate cubemap. The stress camera now
uses a 1000 metre far plane, retaining its 0.05 metre near plane. A native M4
moving-camera rerun showed the sky. This changes no XR camera calibration.
`testNativeXrPortalSky.mjs` checks the actual camera setup against the GLB sky
bounds and preserves the asset's unlit/emissive contract (two passing tests).

Door materials `DLporte2` and `D1porte022` are omitted by both the conversion
and runtime car-material name rules. Their packaged metallic factor is zero,
with roughness 0.48 and approximately 0.616, while `Ext Carrosserie` is 0.82
metallic / 0.22 roughness. This is an asset/material parity gap, not evidence
that environment sampling failed. Direct GPU decoding of both ASTC door atlases
shows stainless exterior panels sharing each texture with dark interior trim.
A blanket metallic override would also make that trim metallic. A durable
correction needs appropriate metallic/roughness masks (or equivalent separated
materials), mirrored by the conversion/tuning/runtime rules. No door override
or GLB edit has been applied yet.

### Maintained Accessor Bundle: Non-AR Moving-camera A/B

On November 14, the accessor prototype was integrated into the maintained
source patch series with a pinned bundle profile and fail-hard verification.
The optimized 9.36.0 bundle contains 8,173 eligible field-read rewrites across
651 files. The audit scanned 2,276 external-package files. This remains an
application-specialized fork build: future application-defined overrides are
outside that closed-program analysis, not automatically semantics-preserving.
See `Patches/BabylonJS/README.md` for build commands and the compatibility limit.

Both variants were built from release
`9495e735517f76d0a9a3661089ecf8f8d7e8b653` with the uniform-buffer comparison
patch. Control disables accessor inlining and uses two Terser passes; optimized
enables the guarded field-read mode and three standard compression/DCE passes.
No hardcoded GPU capabilities or mutable scene configuration were substituted.
These measurements compare that combined bundle profile, not independently
attributed accessor and compression-pass effects.

Executed `babylon.max.js` SHA-256:

- Control: `e7548b285d2eefba1cf5e1b81b7acf848c26ac14fca99d2177db31716891d814`
- Optimized: `369c1286498bf9bfa392b028b08d32d1676c034db87c026fb8bccc162924af01`

The generated source/dependencies used the existing installed dependency tree
from `/Users/matt/src/Babylon.js/.claude/worktrees/interp-hotpath/node_modules`
via the builder's explicit `--dependencies` option. No source from that
worktree or the dirty canonical branch was incorporated. Compiler versions,
patch hashes and report hashes are retained in each build receipt. Generated
bundles are under `build_babylon_bundle/{control,optimized}`. The optimized
bundle is slightly larger (8,918,954 versus 8,909,065 bytes); no download-size
reduction is claimed.

Both native builds were Release/LTO using the public OS JavaScriptCore
framework. M4 used `build_int_macos`, iOS used
`build_portal_ios_release_lto`, Rebecker team `4GWK4YU2PW`, bundle
`com.rebeckerspecialties.BabylonNative.Playground`. The optimized installations
were sequence 9664 on iPhone 12 and 10288 on XS. The packaged hashes and Apple
signature were verified. No interpreter-only switch or per-call wrappers were
used in these A/B runs; do not compare these M4 figures directly to earlier
forced-interpreter profiles.

The new `native_xr_portal_moving_benchmark.js` uses four fixed captures, waits
through readback and warmup, then times 45 seconds of a continuous orbit with
changing azimuth, elevation and distance. It collects only frame-level scene
timing and initial/final backend counters. It never freezes active meshes or
materials. The shared pre-pass, stronger AO workload, full-resolution render
targets, material settings and 1000 metre sky far plane were identical within
each device comparison. Render sizes were 2560x1440 on M4, 1170x2532 on A14,
and 1242x2689 on XS. Saved readbacks are smaller drawable-sized images, not
full-resolution render-target captures.

| Device | Control FPS | Optimized FPS | Optimized repeat FPS | Scene CPU control / optimized / repeat (ms) | Optimized p95 interval (ms) |
| --- | ---: | ---: | ---: | --- | ---: |
| M4 Max | 60.82 | 83.48 | 84.67 | 9.15 / 7.91 / 7.90 | 17.87 |
| iPhone 12, A14 | 52.76 | 54.04 | 54.25 | 10.61 / 10.07 / 10.11 | 26.60 |
| iPhone XS, A12 | 41.30 | 50.17 | 49.79 | 14.95 / 13.93 / 14.02 | 31.80 |

M4's validation timer was hardcoded to 60 Hz despite a 120 Hz view request.
It now follows the view's requested cadence, preserving the default 60 Hz.
The table's control and optimized M4 runs both use that correction. The first
control log without the correction is retained but excluded from this table.
The requested/native display cadence is not the measured scene cadence.

All runs recorded eight render passes, two command encoders and one submit per
scene frame, with the recorder enabled and no disable/error reports. The active
mesh range was 57-81 on M4 and 46-59 on both portrait phones, matching between
variants. All four fixed capture poses also had identical active counts. M4
texture, view, pipeline, buffer and bind-group counts stayed constant throughout
timing; estimated GPU allocations stayed at 132.024 MiB. Each phone variant
added the same five buffers and ten bind groups while the orbit exposed new
draws, increasing estimated allocations by only 15,200 bytes: A14 112.650 to
112.664 MiB, XS 122.713 to 122.727 MiB. Later log windows had zero resource
creation. These are tracked GPU allocation estimates over short runs, not
process-resident-memory measurements or proof of long-session stability.

Visual inspection retained the sky, building textures, metallic reflections,
matte door areas and under-body AO. Across four A/B captures, mean absolute
8-bit channel differences ranged 0.80-1.30 on M4, 2.17-3.20 on A14 and
1.88-3.00 on XS. Identical-bundle repeat captures also differed (0.95-1.57,
1.65-2.45 and 1.66-2.48 respectively). No screenshot threshold was changed;
these are comparative inspection results, not pixel-exact conformance passes.
No new AR session was run, and floor lock/passthrough are not requalified by
these non-AR framebuffer images.

Repeat with the following script order, `--preferred-fps 120 --profile-frames`
on M4 and `--preferred-fps 60` on phones:

```text
app:///Scripts/native_xr_portal_hillvalley_stress.js
app:///Scripts/native_xr_portal_ao_projection_validation.js
app:///Scripts/native_xr_portal_profile_workload.js
app:///Scripts/native_xr_portal_moving_benchmark.js
app:///Scripts/native_xr_babylon_portal_demo.js
```

The 33 focused Node tests, 23 script syntax checks, source-stack application,
compiled bundle checks and native builds passed. Removing the receipt caused
the actual native build to fail; restoring it allowed the verification target
to pass. Both native caches and Apps are left on `optimized`. Evidence lives
in `/private/tmp/bn-portal-20261113/moving-{m4,a14,xs}-*.log` and matching image
directories, plus `build-*-receipt-*.log` and the bundle build/install logs.

The non-AR 60 fps phone / 90+ fps M4 targets remain unmet. The repeated optimized
results are consistent, but one control interval per device does not isolate
thermal/order effects. The next investigation should separate remaining scene
CPU work from GPU completion/presentation pacing using these same moving views,
without per-call wrappers or visual/resolution reductions. There is no new PMU
cache/branch measurement, GPU bandwidth/spill measurement, or bindless benefit
claim from this experiment. AR qualification remains deferred until non-AR
cadence is stable.

### November 14: Moving CPU/GPU Attribution and Presentation Retirement

The benchmark now continues orbiting after its timed interval. Only the four
fixed-pose readbacks pause it. Every five seconds it records azimuth, elevation,
distance, selected actual view-matrix entries and active-mesh count. The long
profile prelude selects 180 seconds instead of 45; four Node tests cover both
durations, movement after completion, readiness and readback exclusion. Scene
instrumentation adds frame-level active-mesh and render-phase timings, not
per-call wrappers. Its `cpuMs` fields are elapsed JS-thread scene timings and
can include native waits; they are not hardware CPU-time counters.

Artifacts for this investigation are in
`/private/tmp/bn-portal-20261114-profile`. Release/LTO still uses operating-system
JavaScriptCore. Accessor inlining remains enabled and the accepted bundle hash
is `369c1286498bf9bfa392b028b08d32d1676c034db87c026fb8bccc162924af01`.
No resolution, AO sample count, material settings or visibility freezing changed.

#### Attribution

- The M4 wall-clock stack sample found the main thread waiting in
  `nextDrawable` for about 60% of sampled time, while the JS thread was active
  in rendering for about 76%. Waiting time must not be reported as consumed
  CPU. Those diagnostic samples were collected before connecting AC power;
  later AC controls are separate measurements.
- The final M4 bounded-retirement Metal capture began after the FPS interval,
  while the camera continued orbiting (`m4-bounded-post-timing.trace`, PID
  97142). Across 291 labeled scene frames and 4.258 seconds, GPU interval union
  was 16.8%. Pre-pass fragment work averaged 0.79 ms per scene frame and SSAO
  0.45 ms. The trace perturbed cadence, so its roughly 68 scene frames/second
  is not the untraced benchmark result. This confirms substantial GPU idle
  time on M4, not GPU saturation; it does not by itself separate JS scheduling
  from presentation waits. The earlier `m4-moving-metal.trace` missed the app
  entirely and is not performance evidence.
- The moving A14 GPU segment (4.1-6.8 seconds of `a14-normal-metal.trace`, PID
  14637) contained 143 scene passes in 2.696 seconds. GPU interval union was
  74.9% of wall time. Scene pre-pass fragment work averaged 5.37 ms, and SSAO
  fragment work 4.73 ms. These stages overlap, so their durations must not be
  summed into total frame time. The `PostProcessRTT-BlurH` label names the next
  pass's input target: it is the SSAO producer, not the horizontal blur.
- A separate moving A14 efficiency-core capture ran from 14:00:18.053 to
  14:00:26.897 local time, after the orbit started at 13:59:15.470. PID 14931,
  JS TID 872200, used the profiling-only background QoS hint. Instruments
  confirmed all 14,222 nonzero counter records for this thread were Running
  on E cores, with no duplicate timestamp/thread/core records. Aggregated
  event ratios were CPI 0.641, branch misprediction 3.18%, L1 data-load misses
  15.66 per thousand instructions, and L1 instruction misses 0.60 per thousand.
  This is diagnostic low-priority/DVFS evidence, not normal-priority FPS.
- In the same CPU sample, JSC accounted for 73.7% of process sample weight;
  the JS thread accounted for 75.6%. Property lookup and indexed access were
  prominent LLInt leaves. This supports reducing interpreter work, but does
  not identify individual Babylon material functions or prove bandwidth
  saturation. The built-in JSC sampling report was 99.6% unknown frames and
  was not used for JavaScript source-function attribution. GPU register/tile
  spills and DRAM bandwidth were not measured by these interval exports.

#### Rejected Experiments

The following experiments are retained in the artifact directory, not in the
maintained Babylon.js patch series:

- Float32Array shadow bulk copies and matrix-check hoisting: A14 54.79 fps /
  9.76 ms scene elapsed time, XS 50.30 / 13.99, effectively unchanged from
  controls at 54.83 / 9.77 and 50.26 / 13.96. Adding guarded matrix bulk writes
  gave A14 54.13 / 10.02 and XS 49.68 / 13.95. Thirty-one focused candidate
  tests passed, but no repeatable phone gain justified keeping the patch.
- SSAO early return beyond `maxZ`: no resolution/sample reduction, but no
  observed phone gain. The isolated shader remains a discarded experiment.
- Resetting snapshot recorded-draw reuse every frame to preserve moving
  visibility regressed the M4 diagnostic from about 57 to 33 fps and increased
  passes from eight to eleven. It is not enabled in the demo.
- Skipping idle presentation alone gave XS 50.75 fps, not a meaningful win.
  It also needs complete canvas-write/resize invalidation before general use;
  the temporary native switch was removed.

#### Native Retirement Candidate

The native presenter drained the entire GPU queue every eighth iOS presentation
(every 120th macOS presentation). A nonblocking-poll diagnostic raised an A14
45-second moving run to 58.82 fps, p95 17.51 ms, versus roughly 54 fps and
26 ms. This is a pacing gain, not a demonstrated reduction in JS instructions.
Simply disabling the safeguard is not the retained implementation.

`PresentationRetirement` instead remembers the last two presentation submission
indices per runtime. On the third and subsequent submissions it waits for the
older index only, preserving two newer presentations and all associated queue
ordering. It allocates no per-frame storage and never mixes device indices.
Three Rust tests cover empty/one/two/many submissions and independent runtimes.
The candidate is opt-in through
`BABYLON_NATIVE_WEBGPU_PROFILE_BOUNDED_POLL=1`; with the switch unset, existing
physical-device and simulator retirement policies remain unchanged. The
candidate has not been qualified on simulators. This is native queue management,
not a new JavaScript or Babylon public API.

The final matched 45-second moving runs used the same final native binary and
accepted optimized bundle, ordinary phone JS priority, and AC power on M4:

| Device | Existing retirement FPS | Bounded retirement FPS | Scene elapsed ms, existing / bounded | p95 interval ms, existing / bounded |
| --- | ---: | ---: | --- | --- |
| M4 Max | 80.51 | 83.25 | 8.00 / 7.94 | 17.33 / 17.65 |
| iPhone 12, A14 | 53.17 | 58.95 | 10.55 / 11.10 | 26.80 / 17.48 |
| iPhone XS, A12 | 48.39 | 51.42 | 14.11 / 14.39 | 32.93 / 33.19 |

Evidence is `m4-final-{legacy,bounded}.log`, `a14-final-{legacy,bounded}.log`
and `xs-final-{legacy,bounded}.log`. Each contains ten motion samples with
changing view matrices. The A14 improvement reproduces the earlier pacing
diagnostic without removing the retirement bound. The smaller M4/XS average
changes need repeats; neither has improved p95. Both phone controls began at
thermal state 0, but the XS reached state 2 during these runs and A14 state 1.
This is not evidence of durable 60 fps on either phone or 90 fps on M4.

Frame-level active-mesh evaluation averaged 0.70 ms on M4, 1.06 ms on A14 and
1.53 ms on XS in the bounded runs. Render-phase elapsed time was 7.04, 9.66 and
12.21 ms respectively. The separate render-target instrumentation counter was
zero because this shared pre-pass executes inside the render path; zero does
not mean the GPU performed no render-target work. The remaining cost is not
mostly active-mesh selection, and these elapsed counters do not identify a
specific material function.

All variants retained eight passes, two encoders and one queue submit per
scene frame. M4 tracked GPU allocation stayed at 132.024 MiB; each phone added
only 15,200 bytes as new draws became visible, then stabilized at 112.664 MiB
(A14) and 122.727 MiB (XS). In the final bounded runs, actual Metal allocations
stayed at 175.55 and 185.89 MiB, with tail resident memory around 355 and
348 MiB respectively. Process footprint still fluctuated with collection.
There was no sustained allocation ramp in these short captures, but they do
not qualify long-session memory stability.

Four fixed poses per variant retained the bright sky, building textures,
metallic reflections, known matte door regions and under-body AO. Mean absolute
RGB channel differences between retirement variants ranged 1.36-1.58 on M4,
2.00-2.75 on A14 and 2.12-2.74 on XS (8-bit channels). These are comparative
readbacks with temporal variation, not exact screenshot conformance passes.
No thresholds were relaxed. All runs here were non-AR; floor lock and camera
passthrough were not requalified.

Both phones reached serious thermal state during the three-minute unbounded
diagnostic: A14 averaged 43.59 fps, XS 37.13, with later windows in the 30s.
That run started with A14 thermal state 1 and XS state 2 after repeated tests,
so it does not establish time-to-throttle from a cold start. Actual Metal
allocations stabilized at 207.19 MiB / 185.89 MiB and tracked GPU allocations
at 112.66 MiB / 122.73 MiB respectively. Process footprint oscillated with
collection rather than rising monotonically. These are short-run observations,
not a durable-60fps or long-session leak-free qualification.

#### Build and Architecture Notes

Project regeneration had reset the iOS bundle identifier, installing a second
app while the launch command still selected the previous Rebecker app. Earlier
`a14-uniform.log` and `xs-uniform.log` are therefore baseline controls, not
candidate results. Corrected runs verified installation receipts and new phase
fields. `BABYLON_NATIVE_PLAYGROUND_BUNDLE_IDENTIFIER` now persists the intended
identifier through CMake; signing remains Rebecker team `4GWK4YU2PW`.

The build hit disk exhaustion; only duplicated generated playground/test assets
and superseded Xcode build-description caches were removed. Source changes,
dependency checkouts and profile evidence were preserved. The accepted bundle
was rebuilt and restored after rejecting the uniform experiments; 34 maintained
optimization tests pass.

[Babylon-Lite's render pipeline design](https://github.com/BabylonJS/Babylon-Lite/blob/master/docs/lite/architecture/27-render-pipeline.md)
addresses the interpreter problem structurally: capture draw bindings when
renderables enter a task, update versioned data once per binding, bind scene
resources once per task, and cache opaque render bundles with explicit
visibility/resource invalidation. NativeWebGPU currently stores render-bundle
commands as C++ closures and replays them, rather than retaining real wgpu
render bundles. The next CPU-oriented implementation should combine cached
bindings with correctly invalidated native bundles, not freeze active meshes
or assume multi-draw/bindless resources remove material preparation. This is an
architecture comparison; Hill Valley has not been ported or qualified on Lite.

### Tile and Shader-Core Memory Counters, November 14

This follow-up kept AR disabled and used the same moving-camera workload,
accepted accessor-inlined bundle, stronger AO, eight samples, 0.30 AO ratio,
0.45 blur ratio, ordinary JS priority, and opt-in bounded presentation retirement.
There were no shader, resolution, material, sampling, or public-API changes.
Both packaged `babylon.max.js` files and the Apps bundle still hash to
`369c1286498bf9bfa392b028b08d32d1676c034db87c026fb8bccc162924af01`.
The checkout is `d020af9e2573a92296d7517b0d8e77d7c6d83300` plus the existing
uncommitted work described above.

Instruments 27.2 used saved Metal System Trace templates with the device's
Performance Limiters counter set, default GPU performance state, and shader
timeline disabled. These are hardware-counter runs, not uninstrumented FPS
comparisons. Artifacts remain in `/private/tmp/bn-portal-20261114-profile`:

| Device | Valid capture | Process | Recorded interval, local time | Thermal state |
| --- | --- | --- | --- | --- |
| iPhone 12 / A14 | `a14-tile-limiters.trace` | Playground 15797 | 15:53:41.758-15:53:54.646 | Nominal throughout |
| M4 Max | `m4-tile-limiters-targeted.trace` | Playground 7402 | 16:10:35.116-16:10:37.812 | Nominal throughout |

The A14 trace contains 745 labeled scene frames during the continuous orbit.
The M4 trace contains 120. The benchmark's changing view matrices and active
mesh counts were retained; no per-call probes or visibility freezing were used.
The full A14 run logged only thermal state 0 in 112 memory samples. Its
three-minute instrumented result was 55.99 fps, p95 30.38 ms. That is not a
durable-60fps result. Actual Metal allocations plateaued at 175.55 MiB; tracked
GPU allocations increased by 15,200 bytes as new geometry became visible, then
stabilized. This is not a long-session memory qualification.

#### A14: No Observed Partial Renders; Two Different Hot Workloads

`Partial Renders Count` was zero in all 137,984 exported samples. Their displayed
20-us intervals total 2.760 seconds distributed over the 12.89-second trace;
this is sampled coverage, not a continuous measurement of every GPU cycle.
Across those samples, imageblock/threadgroup load and store limiters averaged
0.071% and 1.196%, respectively. This does not support tile overflow as the
dominant problem in this capture. It does not prove that every kind of register
or SRAM spill is absent; the A14 set does not expose M4's L1 eviction/residency
breakdown or compiler spilled-byte statistics.

Counter intervals were correlated with active GPU work by process and pass.
The following averages exclude samples crossing activity boundaries and samples
overlapping `backboardd`. Concurrent vertex work from Playground can still
contribute, so these are pass-associated GPU samples, not isolated draw replay:

| Workload | Fragment interval ms per labeled frame | Samples | ALU limiter | Texture-sample limiter | Texture-cache limiter | GPU last-level-cache limiter | External GPU read / write GB/s |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| Shared scene shading / pre-pass | 5.180 | 45,885 | 98.31% | 23.55% | 17.02% | 4.44% | 1.00 / 3.45 |
| SSAO evaluation | 5.096 | 36,086 | 11.07% | 97.66% | 97.70% | 79.26% | 15.41 / 0.14 |

The SSAO external-memory/MMU limiter was only 9.41% despite high texture-cache
and last-level-cache pressure. Do not turn the measured bandwidth into a claim
that DRAM is saturated. Scene shading's F16 utilization was 0.36%; AO's was
about 0.001%. Precision changes are a candidate for material ALU work, not an
established solution to AO's sampling bottleneck.

Important label mapping: `PostProcessManager._finalizeFrame` activates the
*next* postprocess's input target before drawing the current effect. Thus
`PostProcessRTT-BlurH` contains SSAO evaluation; `PostProcessRTT-BlurV` contains
horizontal blur; `PostProcessRTT-ssaoCombine` contains vertical blur; and the
final main pass combines AO/color. The destination named `PostProcessRTT-ssao`
contains the original-color copy, not SSAO evaluation. The earlier approximately
4.73-ms AO measurement used this same destination mapping. Do not diagnose a
5-ms horizontal blur from the target name alone.

Horizontal and vertical blur fragment intervals were about 0.432 and 0.440 ms
per labeled frame here. Playground's union of active GPU intervals was 83.82%
of its recorded span. Pass/channel intervals can overlap; do not sum them to
derive frame latency or subtract them from CPU time.

#### M4: Occupancy Management, Not Sustained L1 Thrashing

The M4 counter set exposes L1 eviction, occupancy-manager target, memory
residency, and separate register/imageblock/stack traffic. Unlike A14's set,
it does not include `Partial Renders Count`. Desktop compositor/other-process
overlap and activity-boundary samples were excluded from the rows below.
Counter groups have different sampling periods; very short passes sometimes
have bandwidth samples but no fully contained L1-counter sample. Missing data
must not be reported as zero.

| Workload | L1 samples | Occupancy-manager target | L1 eviction mean / maximum | L1 cache limiter | F32 limiter | L1 imageblock / register residency |
| --- | ---: | ---: | --- | ---: | ---: | --- |
| Shared scene shading / pre-pass | 529 | 66.93% | 0.568% / 11.001% | 6.19% | 88.27% | 12.39% / 1.26% |
| SSAO evaluation | 282 | 87.73% | 0.002% / 0.291% | 3.36% | 16.90% | 3.12% / 3.34% |

Occupancy management is active and there are brief evictions in scene shading;
this is not evidence of zero memory pressure. The low averages and low L1
limiter do not support sustained shader-core cache thrashing as the leading
frame-time cause. Imageblock and register traffic here is on-chip L1 traffic,
not a spilled-byte counter. The aggregate eviction counter cannot identify
which individual allocation was evicted. Playground occupied the GPU for
13.89% of its recorded span, consistent with continuing the CPU/presentation
investigation for the 90+ fps target rather than assuming a GPU-capacity limit.

Apple's [shader-core memory profiling guidance](https://developer.apple.com/videos/play/tech-talks/111374/)
describes interpreting occupancy management together with eviction, residency,
and memory-type traffic on family-9-and-newer GPUs. Do not apply that memory
architecture directly to A14/A12. Apple's
[counter-statistics guidance](https://developer.apple.com/documentation/xcode/analyzing-apple-gpu-performance-using-counter-statistics)
also distinguishes isolated pass replay from overlapping timeline measurements.

#### Reproduction and Remaining Work

The saved templates are `A14-Portal-Limiters.tracetemplate` and
`M4-Portal-Limiters.tracetemplate` beside the traces. Record after the benchmark
prints `moving-benchmark:timing`, using a 12-second all-processes device trace
on A14 or a two-second `--attach <Playground PID>` trace on M4. Export the TOC,
`gpu-counter-info`, `metal-gpu-counter-intervals`, `metal-gpu-intervals`, and
`device-thermal-state-intervals`. Stream large exports through gzip with shell
`pipefail`; check complete XML parsing as well as command status. Attribution
and raw averages are in `a14-tile-attributed-summary.json` and
`m4-tile-attributed-summary.json`. `summarize-metal-counters.py` and its six
synthetic tests are retained beside them. All six tests and the four maintained
moving-benchmark tests pass.

The first A14 interval export hit disk exhaustion and produced truncated XML
despite an exit status of zero. It is retained as
`a14-tile-counter-intervals-truncated.xml.gz` and excluded from analysis; only
the fully parsed `a14-tile-counter-intervals-complete.xml.gz` was used. The first
M4 all-process capture stalled saving and was terminated after sampling the
stalled Instruments process. Its partial trace/log are not performance evidence.
The process-scoped retry saved successfully and exported valid tables.
Only generated web assets, iOS compiler/debug intermediates, and closed
temporary trace copies were removed to recover working space; signed apps,
source, and valid profiling receipts were preserved.

The next controlled GPU experiments should target selective, feature-gated
F16 material arithmetic and AO depth-fetch locality/working-set reduction,
with full-precision projection/depth math and the accepted image quality held
constant. Existing scalar depth formats must be inspected before assuming
there is an RGBA-to-R bandwidth saving left to take. Any narrower format or
sampling change still needs moving-camera image comparisons. There is no
measured justification yet for reducing resolution or changing depth bias.
Keep AR disabled until sustained, uninstrumented non-AR repeats establish FPS,
thermal, and memory stability on both phones; A14 counters are not XS hardware
qualification.

### Visibility Bitmask AO and F16 Experiments, November 14

These are opt-in, non-AR validation inputs, not default portal changes or an
upstream-ready replacement for SSAO2. The accepted bundle, accessor inlining,
shared pre-pass, resolution, blur, moving camera and bounded-retirement option
are retained. No material/visibility freezing or per-call probes are installed.

Bevy revision `7ea4827e16d5d50d48e22d9a249c554ad073f38a` was not used as a
binary dependency. The inspected shader is
[`ssao.wesl`](https://github.com/bevyengine/bevy/blob/7ea4827e16d5d50d48e22d9a249c554ad073f38a/crates/bevy_pbr/src/ssao/ssao.wesl),
not a standalone WGSL drop-in: it imports Bevy view/depth conventions, a depth
pyramid, Hilbert noise, storage outputs and its denoiser. It still uses
`fast_acos` and fixed thickness. The newer ground-truth corrections are discussed
separately in [Bevy #19713](https://github.com/bevyengine/bevy/issues/19713).
The validation shader is an independent mathematical implementation, not a
verbatim copy of Bevy or the linked ShaderToy implementations.

`native_xr_portal_gtvbao_validation.js` replaces only the SSAO evaluation
shader. Two view-local slices, two steps on each side, and a 32-bit visibility
mask retain eight depth taps. Integer nearest-depth loads, quadratic slice-local
sample spacing, bounded footprint/distance thickness and sample-camera-ray
extrusion target the measured texture bottleneck. Position reconstruction stays
F32 and honors projection sign and off-center projection. Depth, offscreen and
back-facing/invalid-normal cases are rejected explicitly. Orthographic cameras
are deliberately rejected by this validation path.

The mask estimates **uniform-hemisphere visibility**, not cosine-weighted GTAO.
For signed slice angle theta and projected-normal angle beta, the CDF of
`abs(sin(theta))/2` is `(cos(theta) - sin(beta))/2` on the negative side and
`1 - (cos(theta) + sin(beta))/2` on the positive side. Dot products evaluate it
without inverse trig. Endpoints are quantized independently as jittered point
samples; `insertBits` covers zero through 32 bits without a shift-by-32 hazard.
The second prototype uses stable per-pixel PCG hashes for slice/sample jitter:
SSAO's normalized RG vectors are not uniform random scalars. This is spatial
white noise, not Bevy's Hilbert/blue-noise scheme or temporal reconstruction.

The initial prototype compiled and rendered on M4 and A14, but substantially
weakened the accepted under-car shadow. It is therefore **not visually accepted**.
Do not claim a quality-equivalent speedup or enable it for AR based on these
results. Thickness, sparse-sample coverage, near-contact behavior, noise and
cosine weighting need isolated analytic fixtures and scene checks next.

A four-second requested A14 trace saved 4.889 seconds at
`gpu-experiments-a14-gtvbao.trace`, Playground PID 16153. It contains 269 labeled
frames, with a 2.373-ms mean AO fragment interval. Fully pass-contained samples
reported texture-sample/cache limiters of 8.77%/25.82%, ALU limiter 81.65%,
last-level-cache limiter 43.77%, and GPU reads 7.47 GB/s. All 37,763 sampled
partial-render counters were zero. This is directional evidence that the fetch
pattern merits further work, not a controlled percentage speedup against the
earlier 5.096-ms SSAO trace: view coverage, clocks, shader and image quality
differ. The later PCG-noise revision was not used for this counter capture.

`native_xr_portal_f16_material_validation.js` independently changes five
Schlick/Burley/environment-BRDF functions. Device creation explicitly requests
the standard `shader-f16` feature. Shader interfaces, position/depth math,
GGX distribution/visibility, EON and HDR accumulation remain F32. The validation
shim prepends `enable f16` after Babylon's WGSL finalizer has emitted structs,
on each marked stage only. It does no per-draw work. A production proposal
would need proper feature-gated precision handling in Babylon's processor,
not this compile-time test hook. Unsupported devices fail explicitly.

Image comparison exposed two randomized inputs in SSAO2: the eight-sample
hemisphere and its generated rotation texture. The optional
`native_xr_portal_gpu_comparison.js` resets both with a fixed seed during
synchronous setup, restores `Math.random` in `finally`, and disposes the old
rotation texture. It never overrides randomness during rendering. Runs tagged
`seeded-v2` use both fixed inputs; earlier runs do not establish pixel-level F16
parity. Moving-camera readbacks still occur before the timed interval.

All artifacts are in `/private/tmp/bn-portal-20261114-profile` with the prefix
`gpu-experiments-`. M4 used the Release/LTO native build. iOS testing refreshed
only script resources in the existing Release/LTO app and re-signed it with
the same Rebecker Specialties identity, provisioning profile and entitlements;
it was not a fresh native iOS rebuild. The Babylon bundle hash is unchanged.

Reproduce by inserting `native_xr_portal_gpu_comparison.js` after the workload
prelude and before the moving benchmark. Add exactly one of the two shader
experiment preludes after it; omit both for the control. Keep the projection
correction before GT-VBAO. Run `testNativeXrPortalGpuExperiments.mjs`,
`testNativeXrPortalAoProjection.mjs` and `testNativeXrPortalMovingBenchmark.mjs`.
The CPU references cover the analytic CDF, mirrored projections, mask edges,
bounded thickness and F16 arithmetic, but do not substitute for GPU execution.

#### Final Validation Results

The final seeded control/F16 pair uses identical hemisphere samples **and**
rotation textures. All four fixed-view screenshots on each of M4, A14 and XS
differ by at most **one 8-bit channel value**. Mean absolute RGB error ranges
from 0.00139 to 0.00402 channel values across the twelve comparisons. This
supports this selected BRDF subset on Hill Valley; it is not a general material
precision qualification for arbitrary HDR/out-of-range inputs. The camera
continues orbiting during the subsequent 45-second timing interval.

| Device | Control fps | F16 fps | Qualification |
| --- | ---: | ---: | --- |
| M4 | 77.02 | 77.39 | No meaningful FPS improvement established; CPU/presentation limits remain |
| A14 | 58.94 | 56.30 | Control nominal, F16 thermal state 1; F16 trace also overlaps the last six seconds of timing |
| XS | 52.11 | 45.35 | Control transitions 0 to 1, F16 state 2; not a matched performance comparison |

These phone rows must not be interpreted as evidence that F16 itself regresses
performance, nor as a power-efficiency win. Earlier cold controls were 59.31 fps
on A14 and 57.15 fps on XS. Sustained 60 fps remains unqualified. During the
final timed windows, actual Metal allocation ranged from 175.42 to 175.55 MiB
on A14 and 185.77 to 185.89 MiB on XS in both variants; these are short-session
plateaus, not a long-session leak qualification.

The F16 A14 trace saved successfully and its GPU tables exported and parsed.
Instruments reported a dylib timeline overlap; no CPU callstack conclusions
are drawn from it. Its scene-pass-associated F16 utilization is 3.94%, compared
with 0.42% in the separate control capture. Scene ALU limiter remains about
98% in both. This establishes real half-precision GPU execution, not a matched
pass-time or energy improvement. Thermal state and view coverage differ, and
the control capture predates deterministic rotation-texture seeding. The
five-function experiment leaves most material arithmetic and conversions F32;
coherent mixed-precision blocks need further measured work.

The PCG-jitter AO revision also compiled and rendered all four views on M4.
It still weakens the under-car shadow and remains rejected for default use.
Only the preceding revision has A14 GPU counter evidence; the revised AO has
not been qualified on XS or in AR. Its M4 run overlapped build/export work,
so its FPS is not comparative evidence.

Final checks: 27 focused Node tests pass, all new/edited helper scripts pass
`node --check`, `git diff --check` is clean, the M4 Release/LTO build packages
the final resources, and the Rebecker-signed iOS resource refresh passes strict
signature verification. No Babylon.js bundle or native rendering change was
made for these experiments. Default portal behavior is unchanged.

### GT-VBAO Slab Coverage and Coherent F16 (2026-11-14)

The opt-in GT-VBAO v3 keeps the same AO/blur resolution and eight depth taps
(two slices, two steps per side). The previous short fixed-thickness slab and
front-point radius rejection discarded occlusion from the car body even when
the inferred interior intersected the receiving point's AO sphere. The revised
shader infers thickness toward the receiver tangent plane, limits grazing-ray
growth and total thickness to the AO diameter, and clips the entire segment
against the sphere. Coplanar samples are rejected with a texel-footprint bias.
Footprint calculation uses both projection axes. This restores the under-car
shadow in the captured front/side views on M4, A14 and XS, without changing
sky, material or scene-visibility settings. It remains a screen-space thickness
heuristic, not knowledge of hidden geometry or a replacement for shadow maps.

F16 v3 additionally keeps the common metallic reflectivity/energy-bookkeeping
block and diffuse BRDF through cosine weighting in half precision. Conversion
back to F32 occurs at the existing output structure/HDR-light boundary. The
feature request and compile-only finalizer are still explicit validation
opt-ins. Detail/microsurface/debug/nonmetallic reflectivity paths retain the
original body; EON, GGX D/V, roughness, IOR, geometry, depth and HDR accumulation
remain F32. This is qualification of bounded Hill Valley material inputs, not
arbitrary out-of-range/HDR material parameters or custom material plugins.
An upstream implementation should use feature-gated precision support in the
shader processor rather than copying this include-replacement test shim.

The source guards fail before mutating any include when one of the three
expected blocks is absent or changed. GPU smoke testing caught and corrected
a mixed-type WGSL vector constructor; successful process exit alone was not
treated as shader success. The comparison runner rejects uncaptured GPU errors.
Regression tests cover sphere entry/exit, a front point outside the radius,
misses, tangent/empty segments, both depth signs, metallic endpoints, F16
numeric error, unchanged full-precision fallbacks and atomic source-guard
failure. Capture-only runs stop after four ready poses. Bounded timing runs
exit after their result to avoid unmeasured heating. Neither mode installs
per-call probes or freezes meshes/materials.

Artifacts for this follow-up are in
`/private/tmp/bn-portal-20261114-gtvbao-v3`. `run.mjs` records each run's thermal
preflight, timings and receipt; `summarize.mjs` restricts memory/thermal samples
to the measured window. `native_xr_portal_thermal_idle.js` collects native
thermal logs without constructing a scene. All phone trials use the same
Release/LTO OS-JSC executable and Rebecker signature until the final normal
build refresh, the same bundle hash recorded above, fixed seeded AO inputs,
four untimed readbacks, and a 45-second continuous camera orbit. The iPhone 12
is on iOS 26.5; the XS Max is on iOS 18.7.10. Nominal OS thermal state is a
coarse matching condition, not a measurement of equal die temperatures.

#### M4 Counter Evidence

The control and F16 captures both saved and exported their GPU and counter
tables. The F16 wrapper later hit ENOSPC while writing its app log; the saved
trace itself has a successful completion receipt and both exports parse.
The separate A14 GT-only trace failed during save and is excluded. Its earlier
completed FPS window is retained; the interrupted XS GT run is excluded and
was rerun. About 1.7 GiB of closed, redundant raw Instruments temporary files
were removed; saved traces, exports, source and build inputs were preserved.

`compare-counter-phase.py` restricts both M4 captures to camera-orbit seconds
47.0-48.5 and to samples fully contained in the target scene fragment pass,
excluding other-process overlap. Trace start offsets relative to the orbit
were 46.352 seconds (control) and 46.231 seconds (F16). In this matched portion:

| Scene-pass metric | Control | F16 v3 |
| --- | ---: | ---: |
| Fragment occupancy | 55.82% | 80.06% |
| F16 utilization | 0.001% | 7.06% |
| L1 register residency | 1.161% | 0.438% |
| Register L1 read bandwidth | 19.15 GB/s | 7.48 GB/s |
| Register L1 write bandwidth | 19.28 GB/s | 7.53 GB/s |
| GPU read bandwidth | 6.97 GB/s | 7.31 GB/s |
| L1 eviction rate | 0.321 | 0.399 |

There are 555 control and 533 F16 scene-pass occupancy/register samples.
The approximately 61% lower register-related L1 traffic and increased occupancy
are useful evidence beyond FPS, not a direct allocated-register count or a
proof of eliminated spills. GPU read traffic and eviction did not improve.
Full-capture scene fragment work averaged roughly 0.80 versus 0.75 ms per
labeled frame, but this short single counter pair does not establish an energy
saving. The uninstrumented M4 repeat is effectively flat: control 81.44-82.35
fps, combined 81.45-81.48 fps. GPU activity occupied only about 15% of the
instrumented interval; CPU/presentation constraints still dominate cadence.

Half arithmetic does not change texture storage formats, fetch counts or
readback byte sizes. A latency/occupancy benefit must not be described as
halving texture bandwidth. System-power readings also require care:
[Apple documents](https://developer.apple.com/documentation/xcode/measuring-your-app-s-power-use-with-power-profiler)
that charging devices report zero overall power, all-process traces omit
per-app power impact, and impact scores cannot be compared across device
models. Battery-powered wireless runs are needed for the system-energy leg of
this experiment; no FPS-per-mW claim follows from the current USB runs.

#### A14 Counter Evidence

The valid control/combined captures are restricted to the same camera-orbit
seconds 48.0-50.5. Counter samples must fit wholly inside the named target
fragment pass, without other-process overlap. The combined variant changes
both AO and material precision; it is not an isolated F16 timing comparison.
Babylon's `PostProcessRTT-BlurH` target contains the preceding AO evaluation.

| Metric | Control | GT-VBAO + F16 v3 |
| --- | ---: | ---: |
| AO fragment work per labeled frame | 4.451 ms | 2.357 ms |
| AO texture-cache limiter | 98.26% | 9.93% |
| AO GPU read bandwidth | 17.71 GB/s | 8.46 GB/s |
| AO ALU limiter | 11.75% | 85.15% |
| Scene fragment F16 utilization | 0.496% | 5.392% |
| Scene fragment occupancy | 43.61% | 49.45% |
| Scene fragment work per labeled frame | 5.781 ms | 6.841 ms |
| GPU active union per labeled frame | 14.085 ms | 13.875 ms |
| Time in minimum GPU performance state | 24.50% | 78.04% |

Both sampled intervals are about 82.3% GPU-active. The control spends 75.21%
in the medium state and 0.29% in maximum; combined spends its remaining
21.96% in medium. These are Instruments performance-state categories, not
measured MHz. Device conditions induced the state changes, so the slower
scene pass cannot be interpreted as an isolated shader regression at equal
frequency. The AO texture bottleneck is substantially reduced, and similar
cadence at a lower performance state is promising efficiency evidence, but
not a measurement of watts or a proven sustained energy reduction.

Partial-render counters are zero across all 29,834 control and 23,189 combined
samples in the matched phase. This does not establish absence of register
spills or every possible tile-memory cost. The scene remains strongly
ALU-limited (98.25% control, 97.84% combined).

#### Moving-Camera Results and Limits

Every timing row below is a 45-second moving-camera interval, following four
untimed fixed poses and warmup. Instruments starts only after timing completes.
Phone preflight requires repeated nominal thermal samples; cooldowns separate
runs, including ten minutes before the XS combined trial. Runs that become
non-nominal are retained but excluded from matched performance conclusions.

| Device | Control fps | GT-only fps | F16-only fps | Combined fps |
| --- | ---: | ---: | ---: | ---: |
| M4 | 82.35 / 81.44 | 80.93 | 81.78 | 81.45 / 81.48 |
| iPhone 12 | 59.28 / 59.08 | 59.28 | 58.97 | 59.22 |
| iPhone XS Max | 56.81 | 55.84 | 54.70, thermal 0 to 1 | 53.68 |

The final normal Release/LTO build repeats combined at 81.93 fps on M4 and
59.23 fps on iPhone 12. The XS combined row already uses that rebuilt app and
stays nominal. A same-build XS control repeat measures 54.46 fps but warms
from thermal state 0 to 1, so it is not a matched control. Even nominal runs
do not establish equal die temperatures. There is no demonstrated FPS win on
XS, and durable 60 fps on phones or 90 fps on M4 remains unqualified.

F16-only versus control differs by at most one 8-bit channel value in every
one of the four fixed poses on all three devices. The combined-versus-GT-only
phone comparisons also stay within one channel value. M4 combined captures
before and after the normal rebuild are pixel-identical at all four poses.
The restored under-car shadow, bright sky and reflective materials were
inspected in the captures; this is not an AR registration/floor-lock test.

Metal allocations plateau within each measured window, but their launch-to-
launch high-water values vary. A14 control/combined initially sit around
175-177 MiB, while GT-only, F16-only and the rebuilt combined run sit around
206 MiB. XS control/GT/F16 runs are around 186 MiB, but combined is stable at
221.53 MiB. Backend resource counts and estimated resource bytes are nearly
unchanged after warmup; this does not explain the additional 30-36 MiB of
actual Metal allocation. Process footprints are roughly 350-400 MiB, with
some XS windows growing by about 10 MiB. Do not claim memory reduction or
long-session leak freedom from these short plateaus. Allocation-lifetime
attribution remains open.

The charging A14 system-power trace exported but reports zero overall power;
attempts to attach the per-app power instrument could not find the active
process. XS does not support that newer instrument, and host `powermetrics`
was unavailable without administrator authentication. Consequently, no actual
energy/FPS-per-mW result is reported. A battery-powered wireless comparison
and longer alternating trials are still needed for sustained efficiency.

Both normal Release/LTO Playground builds completed using OS JavaScriptCore.
The iOS build retains bundle id
`com.rebeckerspecialties.BabylonNative.Playground`, Rebecker team `4GWK4YU2PW`
and signing identity `Apple Development: Matthew Donovan Hargett (79G4CD2XGC)`;
strict signature verification passed and that app was installed on both phones.
The optimized Babylon bundle hash stayed unchanged. All 99 portal Node tests
and syntax checks for 23 portal scripts pass. A later disk-full export was
rerun and verified after removing 3.3 GiB of reproducible Rust target outputs;
signed apps, source inputs and valid profiling evidence were retained.

These changes remain explicit validation opt-ins. AR and production defaults
are unchanged. Before promoting them, resolve the XS sustained-cadence and
allocation-high-water questions, then qualify the feature-gated shader paths
with broader materials. M4 CPU/presentation work and phone energy/clock
behavior remain relevant even when GPU pass costs improve.

### Native Presentation and Allocation Audit (2026-11-14 Evening)

Artifacts for this follow-up are under
`/private/tmp/bn-portal-20261114-sustained`. AR remains disabled. The same
seeded GT-VBAO slab and coherent F16 BRDF validation variants are used; this
audit does not add lighting effects or change AO coverage.

The presentation surface was initialized in logical points, although the
WebGPU canvas renders in device pixels. Unlike NativeEngine, WebGPUEngine
does not call the native hardware-scaling API. GraphicsWgpu now defaults to
`1 / devicePixelRatio`; explicit scaling still replaces that value, without
applying DPR twice. Logical window/input coordinates are unchanged.
The point-sized default is present in `7696dfe7` (February 2026), before
these GT-VBAO/F16 experiments; this audit does not attribute it to a recent
upstream Babylon.js change.

An opt-in `BABYLON_NATIVE_WEBGPU_PROFILE_SURFACE=1` diagnostic compares wgpu's
configured extent with the actual acquired Metal texture, including its
allocated byte size. It does not retain a drawable. iOS profiling also reports
the layer and MTKView extents, battery state, brightness and Low Power Mode.

| Device | Old acquired surface | Corrected acquired surface | Corrected drawable bytes |
| --- | --- | --- | ---: |
| M4 | 1280x720 | 2560x1440 | 14,745,600 |
| iPhone 12 | 390x844 | 1170x2532 | 12,320,768 |
| iPhone XS Max | 375x812 | 1242x2689 | 13,778,944 |

The XS startup resource trace contains three 1,261,568-byte drawables,
16 retained 1-MiB staging-belt buffers, and 770 internal staging allocations
which all retire by about 2.10 seconds. The trace does not show an indefinitely
accumulating per-frame staging allocation. Trace resource-size totals are not
deduplicated physical Metal usage and must not be added to `currentAllocatedSize`.

The old XS high/low plateau difference, 35.765625 MiB, exactly equals three
drawables changing from 375x812 to 1242x2688. The latter is the MTKView's
retained initial drawable size. The analogous A14 difference is 30.890625 MiB.
This strongly attributes the launch variance to presentation sizing, not a
new GT-VBAO/F16 resource. However, the historical high runs did not record
actual drawable sizes, and a competing setter's call stack has not been
captured. Do not describe that writer/race as conclusively identified.

The controlled native-pixel change raises the XS steady plateau from 185.77
to 233.52 MiB and A14 from 175.42 to 216.61 MiB. These deltas include three
larger drawables **and one larger cached validation readback buffer**. XS's
extra row relative to the MTKView's 2688-pixel height follows the existing
logical-size/DPR rounding used by the JS canvas. The higher plateau is not,
by itself, a leak or a shader regression. The earlier low-memory comparison
silently downscaled the final image and is not native-output qualification.

The audit also found TestUtils discarding captured screenshot dimensions and
reporting the logical viewport instead. The native readback now forwards
`ScreenShotData { Pixels, Width, Height }` through the asynchronous callback;
it does not infer the dimensions from possibly resized host state. The
moving benchmark rejects inconsistent RGBA byte lengths and records capture
dimensions. Native tests cover absent captures and a resize after capture;
viewport/scaling tests and Node contract tests cover the other boundaries.

#### Ground Materials

`New road` now uses dielectric asphalt roughness 0.95; `Trottoir` and
`Trottoir3` use concrete roughness 0.90. Runtime overrides preserve these
authored values instead of reducing them to 0.72, and no longer add an
artificial emissive fill to those three ground materials. Existing textures,
UVs and geometry are unchanged, so this adds no texture fetches or draws.
All other materials, including painted markings, unlit skies and the car,
retain their previous tuning. Ten tests check GLB/runtime agreement,
idempotency, byte-preserved geometry and non-target materials.

#### Tile Storage and Mask Reuse

No G-buffer-to-tile-input conversion has been made. The current
[WebGPU draft](https://gpuweb.github.io/gpuweb/#texture-usage) defines
`TRANSIENT_ATTACHMENT` as an attachment usage hint, not a WGSL address space
or a general tile-shader input. It cannot be combined with sampled/storage
usage, requires clear/discard, and cannot preserve depth/normals for another
pass. Current AO samples neighboring pixels across tile boundaries; changing
a usage bit cannot keep those reads entirely in tile SRAM. A compute tile
cache can reuse a bounded neighborhood but still loads persisted textures and
needs a halo/fallback for wider samples.

The [visibility-bitmask paper](https://arxiv.org/abs/2301.11376) does support
reusing sample traversal for indirect diffuse/ambient lighting. A final
`countOneBits` alone is insufficient: bent normals need directional weights;
direct-light penumbras need light extent/direction and blocker distances;
indirect lighting needs source radiance and newly covered sectors per sample.
Two sampled slices are not full three-dimensional visibility. Fusing these
accumulators may save redundant depth reads, but can increase register
pressure, texture traffic, output bandwidth and denoising needs. They remain
proposed experiments, not implemented or measured savings.

Prefer private/workgroup accumulation and ordered GPU dispatches. WGSL
[barriers](https://www.w3.org/TR/WGSL/#synchronization-builtin-functions) do
not provide a whole-grid execution barrier; cross-workgroup atomics/spin
waiting are not a safe substitute. Multiple GPU dispatches do not require
CPU readback.

#### Sustained-Test Gate

`native_xr_portal_sustained_benchmark.js` selects a five-minute orbit, with
30-second FPS/CPU windows and fixed-pose captures outside timing. It must be
loaded before `native_xr_portal_moving_benchmark.js`. Keep all variant inputs,
actual drawable sizes, brightness, Low Power Mode and battery state matched;
use alternating order and cooldowns, not just the coarse nominal thermal flag.
Both phones reported `batteryState=3` (full, externally powered) during the
initial diagnostic runs. Battery tests reject that state and require
`batteryState=1` throughout. Charging measurements cannot establish system
power savings. After the user disconnected both phones, wireless discovery
and idle telemetry confirmed local-network transport, battery state 1,
Low Power Mode off, and nominal thermals before the battery comparisons.

The fresh XS Time Profiler trace is verified against Playground PID 8169 and
the installed app path. Its 7,763 running samples place 82.42% of leaf work in
JavaScriptCore and 83.60% on the JS runtime thread. `performLLIntGetByID` alone
accounts for 9.42%, `llint_slow_path_get_by_val` 3.56%, and
`tryGetIndexQuickly` 2.36%. Inclusive native-WebGPU stacks are 10.51%, GC 5.31%;
inclusive categories overlap. These are sampled-work shares, not exact CPU
durations, GPU wait fractions, cache-miss counters or shader energy. The
capture records running threads only and cannot measure all blocked time.
95.25% of sample weight is on the cores Instruments labels S Core, so this
is not an efficiency-core-only experiment.

Direct process attachment failed; the successful eight-second all-process
retry was filtered by PID. Instruments emitted overlapping-dylib warnings;
the Playground/JSC symbols and process identity resolve, but no causal claim
is made from the affected system-library addresses. The first 30-second XS
window overlaps that trace and is excluded from uninstrumented cadence
comparisons. Later windows still retain the thermal history of profiling.
The optimized bundle hash remains
`369c1286498bf9bfa392b028b08d32d1676c034db87c026fb8bccc162924af01`;
accessor inlining was not lost.

Five-minute combined-variant diagnostics completed at corrected native output:

| Device | Mean fps | Final 30 s fps | Thermal states | Metal MiB throughout timing |
| --- | ---: | ---: | --- | ---: |
| iPhone 12 | 55.24 | 52.15 | 0, 1 | 216.61 |
| iPhone XS Max | 37.62 | 35.22 | 0, 1, 2 | 233.52 |

A14's first window is 59.56 fps/9.76 ms scene CPU, versus 52.15 fps/14.45 ms
in the last window. XS settles around 35-37 fps after the early profiled
window, with scene CPU around 15.6-16.1 ms. Its process footprint moves from
423.58 to 463.16 MiB (peak 471.33); A14 moves from 405.41 to 410.64 MiB
(peak 415.99). GPU allocation growth does not explain the falling cadence.
CPU footprint growth is still open: this is not proof of a JS leak, nor a
claim of long-session CPU-memory stability. Thermal state and slower CPU work
correlate with the drop; a matched battery control and concurrent GPU timing
are still needed to apportion CPU/GPU throttling and energy causally.

The final receipt exceeded a device-console chunk and devicectl inserted its
colored successful-exit notice inside JSON. `recover-long.mjs` removes only
that exact notice, retains the raw log, validates that window frame/time sums
equal the receipt, and records the transport repair. Both apps exited zero.
The maintained benchmark now retains only the last twelve motion samples in
its final receipt; all motion samples continue to stream separately. A long-
duration test covers this bound.

M4's short native-output control/combined repeat is 78.25/77.58 fps, with
8.12/8.26 ms scene CPU. No FPS win is established. Earlier M4 runs overlapped
an iOS rebuild and are retained only as rendering diagnostics. The repeat
does not establish battery efficiency or a sustained 90-fps result.

Full-resolution captures on M4, A14 and XS preserve the under-car shadow,
textured ground and reflective car; the sky is present in the upward poses.
114 focused Node tests and eight native screenshot/scaling/viewport tests
pass. Release/LTO builds use the OS JavaScriptCore framework; the phone build
keeps the Rebecker team and Matthew Donovan Hargett signing identity, passes
strict signature verification and was installed on both phones. The battery
comparisons below use that installed build. AR was not started, so these
results do not requalify placement or floor lock.

#### Battery-Powered Sustained Comparison

Both phones were disconnected from USB and chargers. Each run requires three
nominal thermal samples before launch, records battery state 1 throughout,
and keeps Low Power Mode off. Brightness stays at 0.05 in A14 control and
0.00 in both XS trials. A14 combined starts at 0.05 but changes among 0.00,
0.05 and 0.10 after about 138 seconds, making it unsuitable for a matched
whole-device energy comparison. These are low-brightness measurements, not a
normal-brightness battery-life estimate. Fixed captures precede timing; all
61 sampled view matrices differ during each five-minute orbit. There are no
per-call probes, frozen materials, or frozen active-mesh lists.

| Device / variant | Mean fps | First 30 s | Final 30 s | Last 120 s | Thermal states |
| --- | ---: | ---: | ---: | ---: | --- |
| XS control | 36.54 | 55.76 | 30.61 | 32.55 | 0, 1, 2 |
| XS GT-VBAO + F16 | 36.97 | 56.36 | 30.92 | 31.43 | 0, 1, 2 |
| A14 control | 54.28 | 59.39 | 49.78 | 51.35 | 0, 1 |
| A14 GT-VBAO + F16 | 56.33 | 59.31 | 52.28 | 54.18 | 0, 1 |

XS had five minutes without the app between runs. Its thermal transitions
to fair/serious occur at approximately 18/38 seconds in control and 16/36
seconds in combined, relative to the first timed memory sample. The similar
overall FPS and worse combined last-two-minute average do not establish a
sustained performance win. Nominal preflight is still not an equal-die-
temperature measurement, and this is one ordered pair, not a repeated ABBA
experiment. Both XS trials lose five reported battery percentage points;
that coarse gauge cannot distinguish their energy efficiency.

XS control Metal allocations stay at 233.52-233.64 MiB and combined at
233.47 MiB. Actual acquired height in the combined trial is 2688, versus
wgpu's configured 2689; the remaining single-row MTKView/DPR rounding
difference is recorded, not hidden as an exact-size match. The major
point-sized presentation mismatch is corrected, but ownership of that final
row still needs reconciliation. Framebuffer captures come from the canvas
at 1242x2689, not from the display compositor.

XS process footprint moves from 417.70 to 412.64 MiB in control and 420.92
to 410.60 MiB in combined. Resident memory changes by about 0.38 and
1.48 MiB respectively. Both trials create zero additional textures/views,
five buffers totaling 15,200 bytes, and ten bind groups as the orbit reveals
new geometry. This does not reproduce an unbounded per-frame GPU allocation
or the earlier 40-MiB footprint increase. It does not prove indefinite
CPU-memory stability. A14 control footprint rises 18.36 MiB while Metal
stays at 217.61-217.73 MiB, so CPU-side high-water behavior remains open.
A14 combined footprint moves from 409.71 to 406.08 MiB (peak 415.77), with
Metal fixed at 216.61 MiB. The A14 trials both acquire 1170x2532 drawables.
The recorder delay extends the combined cooldown to approximately twelve
minutes; together with brightness drift and a single ordered pair, this
prevents attributing the 3.8% overall FPS increase solely to the shaders.

The A14 control power recorder reached its 280-second limit, then stalled
while resolving remote system-library signatures. A sample of xctrace shows
`XRRemoteDevice createSignatureFromLibraryUUID` waiting through DTX, not
Playground execution. A graceful interrupt did not finish it. The preserved
raw `trace-data.atrc` imports successfully with a 281.484-second span, but
the recovered SystemPowerLevel table has no rows; it is **not power evidence**.
The original finalizer was stopped after this recovery attempt. A separate
12-second battery idle preflight does contain nonzero power samples, proving
the instrument can work when unplugged, but cannot substitute for workload
energy. A launch-service failure before the next trial is retained separately
from the subsequent retry.

The combined recorder encountered the same remote-signature wait after its
time limit. USB transport was verified after the user reconnected A14, well
after the app's measured interval ended, but the collector still did not
finalize. A graceful save request also failed to finish; the collector was
stopped and its raw trace and sampled stacks retained. Both app runs exited
zero, while both finalized power-recorder receipts have `traceExit: 1` after
the stuck collectors were terminated. Neither trace is counted as a successful energy
measurement. Brightness drift independently disqualifies combined as a
matched energy comparison. No watts, FPS/mW or battery-life improvement is
claimed from these trials.

`battery-summary.json` and `summarize-battery.mjs` record the four runs,
including changing brightness rather than silently accepting it. The energy
analyzer requires successful recording, complete interval coverage and
constant brightness. All benchmark and recording processes were stopped at
the end of this pass. The material-hint checker and optimized-bundle verifier
still pass, and the 21 focused ground/benchmark/readback tests were rerun
after the longer measurements.

Next priorities are to reconcile the final XS drawable row, make the energy
capture reliable with fixed brightness and shorter late-run recording
windows, and correlate sustained scene CPU cost with GPU/presentation waits.
The sampled interpreter cost and the thermal degradation justify continuing
CPU-work reduction; they do not prove that the GPU is idle or identify a
single thermal power source. Keep the experimental AO/F16 paths opt-in until
repeated, thermally controlled trials establish a durable benefit. Do not
add fused lighting outputs or reduce output resolution merely to conceal
these unresolved bottlenecks.

### Moving-Camera CPU Attribution and Command Encoding (November 14)

Evidence is in `/private/tmp/bn-portal-20261114-balance`. AR stays disabled,
GT-VBAO slab-v3 and coherent F16 blocks-v3 stay enabled, and the seeded
four-pose captures precede a continuously moving orbit. No material or
active-mesh freezing, AO quality reduction, or per-call probes are used.
M4 is on AC with `JSC_useJIT=false`; A14 is USB-powered and XS is on battery.
Those are not cross-device power comparisons. Brightness also changes during
the phone runs, so no energy-efficiency claim follows from them.

#### Reconcile the Earlier FPS Numbers

The earlier 48.39-51.42 XS and 53.17-58.95 A14 results were **45-second**
bounded-retirement trials, not sustained five-minute results. Current XS
still starts near 58 fps before slowing. In the preceding five-minute pair,
both the control and combined shader variant fell to approximately 31 fps;
that pair does not identify GT-VBAO as the cause of the loss. The new
three-minute encoder run starts at 58.53 fps and ends at 37.07 fps, with a
nominal-to-fair thermal transition. A14 starts at 59.52 and ends at 55.25
while its coarse thermal flag remains nominal. Do not label all of that
device's slowdown as proven thermal throttling from the flag alone.

Presentation was also corrected from point-sized to native-pixel drawables:
375x812 to 1242x2688 on XS and 390x844 to 1170x2532 on A14. The scene and AO
targets were already full-resolution, so this is **not** a nine-fold increase
in the whole scene workload. The present-offscreen compute work now consumes
about 5% of the phone trace wall time, including presentations that repeat the
last completed JS frame. The old/new end-to-end runs are not immutable-build,
equal-temperature controls; no specific upstream regression is established.

#### Texture Cache Is Not the Universal Limiter

The earlier valid A14 shader-counter pair reduced the AO texture-cache limiter
from 98.26% to 9.93%, AO reads from 17.71 to 8.46 GB/s, and AO fragment time
from 4.451 to 2.357 ms. The scene remained approximately 98% ALU-limited.
On M4, combined reduced register-related L1 traffic and improved occupancy,
but aggregate reads were 6.97 versus 7.31 GB/s, not a bandwidth reduction.
Localized AO cache pressure still exists. XS has no corresponding validated
texture-cache counter result. Do not describe all three as texture-cache-miss
bound or confuse a limiter percentage with a literal miss rate.

Fresh Metal interval exports, attributed to the Playground PID, separate GPU
activity from the CPU stacks. These are diagnostic captures, not FPS controls:

| Device / PID | Trace span | Labeled scene frames | GPU active union | Scene fragment total | AO fragment total |
| --- | ---: | ---: | ---: | ---: | ---: |
| M4 / 78959 | 3.469 s | 204 | 15.83% | 178.1 ms | 91.1 ms |
| A14 / 17957 | 8.572 s | 440 | 75.59% | 2830.9 ms | 1230.7 ms |
| XS / 8251 | 8.185 s | 252 | 70.56% | 2649.0 ms | 1392.8 ms |

XS scene fragment work includes two pre-pass encoders per scene frame; A14
shows about one. These totals cannot be added to JS elapsed time as though
CPU and GPU were serialized. GPU active union per labeled XS frame is about
23 ms at this late-run state, including repeated presentation work: CPU-only
optimization cannot make this particular GPU workload a sustained 60 fps.
M4's low GPU duty cycle is a different case. These fresh traces have no
shader hardware-counter set, so they do not refresh the earlier cache data.

#### Interpreter Work Versus Waiting

Instruments' context-switch records include blocked stacks even when their
state is rendered as `Running`. Summing those together with periodic samples
overcounts CPU execution. On the JS thread, the 10-ms periodic samples contain
4.28 seconds of JSC plus 0.44 seconds of other execution on XS, and 6.04 plus
0.72 seconds on A14. The large CFRunLoop/mach-message waits are separate.
The main thread mostly waits in `nextDrawable`; that is not JavaScript work.
Babylon's reported `cpuMs` is scene elapsed time, not pure CPU execution.

M4's diagnostic JSC sampler, with native-code sampling disabled, now resolves
10,298 stacks rather than the earlier unusable unknown-frame report:

| Hot path | Leaf share | Inclusive share |
| --- | ---: | ---: |
| `_buffersEqual` | 12.56% | 12.56% |
| `updateUniform` | 6.13% | - |
| `bindForSubMesh` | 5.69% | 32.32% |
| command-stream encoding | - | 5.33% |
| removed word/capacity helpers | - | 3.02% |

All 1,293 `_buffersEqual` leaf samples return to the second comparison in
`_updateOwnerKeyed`, checking a slot's CPU shadow before upload, not its
same-frame conflict check. Avoiding a hypothetical duplicate comparison
therefore does not address this measured workload. Forward, reverse,
countdown and four-way comparison loops were also tested under interpreter
execution; none displaced the current cached-length loop. No alternative
loop, unsafe shadow-check bypass, or new resource-freezing path is retained.

#### Retained Optimization

`Patches/BabylonJS/0004-command-stream-inline-words.patch` reserves the worst-case
command space once per accepted draw, then writes through a local array and
cursor. It removes per-word method calls and capacity checks, without changing
the command protocol, API, resource retention, atomic rejection/replay, or
draw order. It is based on the existing source experiment `a4035703a5`, adapted
to the maintained 9.36.0 patch stack; the canonical Babylon.js checkout's
unrelated working branch was not changed.

The original and patched TypeScript encoders produce byte-identical streams
across 24 differential tests, including zero/one/many capacity, large IDs and
offsets, invalid resources, exceptions, replay, pass changes, and 1024-draw
batch boundaries. The full bundle-optimization suite passes 62 tests. The
bundle still includes 8,173 accessor rewrites across 651 files and three
compression passes. Its verified SHA-256 changes from
`369c1286498bf9bfa392b028b08d32d1676c034db87c026fb8bccc162924af01` to
`8e5e1e5817f6b54f5c8935046186748ab58e827271c9e504859a7007dbdd234c`.

The first uninstrumented three-minute optimized runs measured 82.93 fps M4,
57.40 A14 and 43.54 XS. Their instrumented baseline runs are useful for
attribution, **not** clean whole-run speedup percentages. The M4 four-pose
captures are pixel-identical across the bundle change. Device captures retain
the textured ground, car materials and under-body shadow. A separate clean
M4 old/new bundle pair and final five-minute phone checks follow below.

The clean M4 pair uses identical native executable SHA-256
`6f323efc7ebe02d82f5a4831835b1acbf1f88dc1a65218d8dafa51be19d73008`.
An explicit comparison-only app copy contains the preserved old bundle and
its old receipt; the maintained app keeps the verified new bundle. There is
no sampler or Instruments capture in either three-minute interval.

| M4 bundle | Mean fps | Mean scene elapsed | p95 frame interval |
| --- | ---: | ---: | ---: |
| Before command encoding change | 78.15 | 8.169 ms | 17.914 ms |
| After command encoding change | 81.72 | 7.951 ms | 17.909 ms |

This single ordered pair observes 4.58% higher FPS and 2.67% lower scene
elapsed time, not a confidence interval or a sustained 90-fps result. All
37 sampled views change in each run; the four fixed-pose images are exactly
pixel-identical. Both have 8 passes, 2 encoders and 1 submit per scene frame,
zero new textures/views/buffers/bind groups during timing, and no disabled
command batches. The gain is not from dropping scene work or turning off AO.

#### Rejected Native Experiment

An opt-in native experiment attempted to fold an adjacent empty clear pass
into the following draw pass. Five real-GPU pixel/ordering/error checks passed
both enabled and disabled, including an invalid depth clear that must fail at
`finish`, before submission. Nevertheless, portal traces still show the extra
pre-pass and the FPS trials are effectively tied. The XS trace disconnected
early and contains only six scene frames, so it is not a valid sustained GPU
comparison despite xctrace exiting zero. The experiment was removed from the
working source and final builds; its source, fixture and logs remain only in
the evidence directory. No GPU pass-fusion improvement is claimed.

#### Final Sustained Phone Checks

The final Release/LTO build removes the rejected native experiment and keeps
the optimized command encoder. Its iOS signature verifies with Rebecker team
`4GWK4YU2PW` and `Apple Development: Matthew Donovan Hargett (79G4CD2XGC)`.
The installed sequences are 9832 on A14 and 10440 on XS. The binary links the
operating-system JavaScriptCore framework. Both apps complete successfully.

| Device | Five-minute mean | First 30 s | Final 30 s | Last 120 s | Mean scene elapsed |
| --- | ---: | ---: | ---: | ---: | ---: |
| A14, USB-powered | 53.96 fps | 59.22 | 50.77 | 51.19 | 13.31 ms |
| XS, battery-powered | 39.45 fps | 58.60 | 32.21 | 34.53 | 15.29 ms |

All 61 sampled view matrices differ on each phone. Both retain 8 passes,
2 encoders and 1 submit per scene frame, approximately 58 draws, no disabled
command batches, and zero additional textures or views during timing.
Each creates five buffers totaling 15,200 bytes and ten bind groups when the
orbit exposes new geometry. XS Metal allocation stays at 233.52 MiB; A14 at
216.61 MiB. XS footprint changes from 422.53 to 420.80 MiB (peak 428.27),
A14 from 408.44 to 409.74 MiB (peak 415.08). This does not reproduce unretired
full-frame allocations, nor does a five-minute test prove indefinite stability.

XS brightness stays at 0.05, battery state stays 1 and Low Power Mode stays
off; thermal transitions are nominal/fair/serious at approximately 0/20/40
seconds from the first timed sample. A14 stays externally powered, reaches
fair at approximately 90 seconds, and brightness varies from 0 to 0.10.
These runs do not establish watts or a matched before/after energy gain.
The 39.45-versus-prior-36.97 XS means are not a controlled patch-only speedup.

A14's console transport inserted its exact colored successful-exit notice
inside the final JSON line. `recover-a14.mjs` removes only that known notice,
retains the original log, requires the separate successful app-exit message,
and verifies all ten window frame/time sums against the recovered result.
The initial parser failure is not an app crash. `summary.json` and
`summarize.mjs` retain the memory, motion, thermal, allocation and FPS evidence.

The final four-pose captures on both phones are pixel-identical to their
earlier optimized-encoder captures. Final verification passes all 62 bundle
optimization tests and 41 ground-material/GPU-experiment/moving-camera
contract tests (these suites share some tests). The four-patch stack also
applies cleanly to the pinned source revision. No source or generated artifact
from the unrelated canonical Babylon.js working branch was included.

The remaining priorities are the owner-keyed uniform shadow comparisons and
material binding on the CPU, and scene/pre-pass plus AO cost on XS. Redundant
presentation is measurable but not the entire loss. A clean-frame reuse path
must account for canvas reconfiguration, queue writes, surface loss and
resource progress; skipping presentation blindly would not be a safe fix.
Keep the accepted materials and occlusion while testing narrower shader and
attachment-lifetime changes. Durable 60 fps, sustained 90+ fps on M4, and
renewed AR qualification remain unachieved.

## Shared/versioned PBR uniform buffers (2026-11-14)

This follow-up keeps AR disabled and the accepted GT-VBAO slab/thickness,
coherent F16 BRDF blocks, ground materials, resolution and moving camera.
The source change is maintained as
`Patches/BabylonJS/0005-shared-material-uniform-buffers.patch`, against the
pinned 9.36.0 source. It does not incorporate the unrelated dirty canonical
Babylon.js branch. The build archives the pinned revision and strictly applies
the entire stack; bundle fingerprints still fail closed.

### Ownership and correctness

The WGSL material path previously constructed a forced-no-UBO `UniformBuffer`:
its setters forwarded to the effect's leftover buffer. A startup-only inventory
found 760 floats (3,040 bytes) in each PBR leftover layout, including base and
plugin material fields, with 71 owner slots in the largest variant. Scene,
mesh and light buffers were already separate. This was CPU shadow-state
comparison, not depth-texture shadow comparison.

PBR now owns one shared material buffer across its draws and effect variants.
The shader preprocessor lowers explicit `uniform<Material>` ownership into
standard WebGPU uniform bindings while retaining `uniforms.member` references
for shader/plugin compatibility. Ordinary loose uniforms remain per-effect.
Other material types and GLSL retain their existing paths; custom PBR shader
resolvers retain the loose-uniform fallback, including a resolver installed or
removed after layout creation. There is no native-specific JS API.

The existing data generation also tracks uploaded/shared buffer versions.
Unchanged versions require neither a whole-buffer scan nor an upload.
Copy-on-write preserves previously encoded draws when values change within a
frame, including per-view camera parameters. Snapshot replay checks the actual
material GPU buffer identity and first-buffer state, and marks it used before
a subsequent write can overwrite it. This does not freeze materials, meshes,
transforms or camera data.

`npm --prefix Apps run test:babylonjs:uniforms` passes 201 tests in 11 files.
The new cases cover 0/1/N unchanged draws; same-frame A/B/A values; distinct
view matrices and per-view material values; 100-frame buffer reuse; separate
materials; context rebuild; matching CPU/plugin/shader field order; GLSL and
custom-shader fallback; ownership conflicts; padded arrays; and replay
invalidation. Four targeted mutants were killed by assertions: ignoring
versions (5 failing tests), disabling copy-on-write (9), dropping shader
routing (5), and ignoring recorded material-buffer identity (1). Source was
restored and the full 201-test suite rerun green afterward. Core TypeScript
checking and all 62 existing bundle-optimization tests pass.

### Matched workload results

Artifacts: `/private/tmp/bn-portal-20261115-uniforms/`, including raw logs,
receipts, four-pose images, summaries, mutation results and sampling profiles.
Control and candidate use identical native executable UUIDs:
`34568519-5BFA-3854-841C-731900ADC42A` on iOS and
`E5A556CF-78BD-374D-85E3-7F7B9DD71746` on macOS. Both are Release/LTO with OS
JavaScriptCore; M4 uses interpreter-only mode for these comparisons. The
verified bundle hashes are `8e5e1e5817f6b54f5c8935046186748ab58e827271c9e504859a7007dbdd234c`
(control) and `081c385eb68a58619ca6bfad4007435c384ce66397e720a9fb6c30a05ade5812`
(shared). The iOS build/install uses the Rebecker team `4GWK4YU2PW`, existing
Matthew Hargett development identity, provisioning profile and Playground ID.

The timing runs use no per-call probes or external profiler. Four fixed poses
and warmup precede a continuous 12-second camera orbit. M4 runs for 180 seconds;
each phone runs for 300 seconds. Logs confirm 37 distinct sampled views on M4
and 61 on each phone, with the same active-mesh ranges in both variants. All
12 control/candidate images are pixel-identical, including AO and reflections.
Actual render sizes are 2560x1440, 1170x2532 and 1242x2689 respectively.

| Device | Mean FPS control -> shared | Final 120s FPS control -> shared | Scene elapsed ms control -> shared |
| --- | --- | --- | --- |
| M4 Max | 83.17 -> 108.16 | 82.87 -> 106.97 | 7.873 -> 6.351 |
| iPhone 12 | 56.50 -> 59.51 | 53.71 -> 59.33 | 12.712 -> 8.494 |
| iPhone XS | 40.97 -> 48.77 | 37.36 -> 46.02 | 15.082 -> 12.379 |

Scene elapsed time is not pure CPU time. These are single ordered pairs, not
confidence intervals or an indefinite endurance qualification. M4 was on AC;
iPhone 12 remained externally powered and XS remained on battery. Both phone
runs began in nominal thermal state, but temperatures were not measured.
Brightness was 0.05 throughout the candidates; the controls varied from
0.05-0.15 on iPhone 12 and 0.05-0.10 on XS. Consequently this is not a matched
power/energy comparison. A14 reached fair thermal state around 236/248 seconds
(control/shared); XS reached serious around 206/80 seconds. Higher throughput
has not established lower power or better thermals on XS.

Separate 20-second JSC sampling captures, not used for the FPS table, contain
10,363 control and 11,158 shared samples. `_buffersEqual` falls from 1,313
samples (12.67%) to 2 (0.018%); `_updateOwnerKeyed` inclusive samples fall from
13.06% to 0.072%. PBR no longer has the large leftover buffer in the startup
inventory. Remaining material binding and `updateUniform` work is visible;
sampling percentages alone do not measure process CPU utilization or power.

### Memory and remaining work

No new textures, views or buffers were created in the candidate timed intervals.
Each phone created ten bind groups as the orbit exposed additional geometry;
M4 created none. The scene still uses eight render passes per frame. The
candidate's fixed buffer setup requests an additional 164,160 bytes on phones
(54 additional 3,040-byte buffer creations), not a per-frame allocation.
Reported Metal allocation stays flat at 217.61 MiB on iPhone 12 and 233.77 MiB
on XS, versus 216.61 and 233.52 MiB in the controls. XS footprint rises during
the first approximately 90 seconds, then plateaus at 435.14-435.16 MiB for the
remaining interval; resident memory likewise settles at 378.41 MiB. A14
footprint fluctuates in 403.91-410.92 MiB without a rising trend. M4's estimated
GPU resource bytes remain constant, but this run has no OS heap-memory series.

Additional M4 native screenshots pass unchanged thresholds: PBR refraction
(2 differing pixels), MeshDebugPluginMaterial (2,001), and PBR texture repetition
(761, below its 1% limit). No thresholds or scene settings were relaxed.
These complement the exact portal A/B captures; they are not browser/WebGL
end-to-end qualification.

The CPU comparison hotspot is removed, M4 exceeds the 90 fps target in this
run, and A14 is near 60 throughout five minutes. XS still falls to about
46 fps once hot. Next, re-profile XS/A14 GPU and presentation costs with this
new CPU baseline, preserving the same AO and materials, and measure power
under controlled brightness before claiming a performance-per-watt win.
AR remains disabled and has not been requalified by these runs.

## XS GPU and Presentation Follow-up (November 15)

Artifacts: `/private/tmp/bn-xs-20261115-gpu`. This uses the same 9.36 optimized
bundle (`081c385eb68a58619ca6bfad4007435c384ce66397e720a9fb6c30a05ade5812`),
native 1242x2689 backing, moving orbit, GT-VBAO slab-v3, F16 blocks-v3, and
accepted materials. AR remains off. No per-call probes or quality reductions
were added. The Release/LTO app uses the Rebecker signing identity and OS JSC.

The user disabled Auto-Brightness. The validation-only environment variable
`BABYLON_NATIVE_PLAYGROUND_PROFILE_BRIGHTNESS=0.25` sets brightness on app
appearance; the existing two-second memory log records actual brightness and
reports drift instead of repeatedly forcing the setting. It only takes effect
with `BABYLON_NATIVE_PLAYGROUND_PROFILE_MEMORY=1`. All timed samples in both
diagnostic runs read 0.250. The initial baseline log reads 0.000 immediately
after UIKit receives the setting, before scene setup; it has settled to 0.250
before warmup completes. Both runs report charging, Low Power Mode off. These
are not battery-power or watts measurements.

### Direct A12 Counter Evidence

Instruments offers Performance Limiters on this XS, contrary to the earlier
absence of an XS counter capture. The saved template is
`/private/tmp/XS-Portal-Limiters.tracetemplate`, with default GPU performance
state and shader timeline disabled. The baseline trace targets Playground
8316 and the A12 accelerator 4294968138; exported definitions and counter
intervals verify actual samples, not merely an available UI option.

During 8.064 seconds, 320 labeled scene frames consume 6.481 seconds of GPU
active union (80.37%). Summed per-pass fragment durations below are not additive
with CPU elapsed time, nor a serial frame budget; vertex/compute overlap is
preserved in the active-union calculation.

| Work | Fragment ms / labeled scene frame | ALU limiter | Texture-cache limiter | External GPU read / write GB/s |
| --- | ---: | ---: | ---: | ---: |
| Scene pre-pass / shading | 9.278 | 86.60% | 8.44% | 1.48 / 4.53 |
| GT-VBAO | 4.647 | 75.72% | 18.59% | 10.26 / 0.15 |
| Original-color copy | 0.846 | 4.95% | 29.05% | 14.45 / 5.99 |
| Horizontal blur | 1.135 | 32.37% | 66.89% | 3.42 / 0.83 |
| Vertical blur | 0.821 | 31.56% | 81.73% | 3.81 / 0.78 |
| Final combine | 1.150 | 11.09% | 32.38% | 6.25 / 6.94 |

Babylon names each post-process target for its next consumer: GT-VBAO renders
into `PostProcessRTT-BlurH`, horizontal blur into `...BlurV`, vertical blur into
`...ssaoCombine`, and original-color copy into `...ssao`. Do not attribute the
4.647 ms to horizontal blur based on the target label alone.

Counters are duration-weighted, pass-associated samples wholly contained in
Playground GPU activity with other processes excluded. Concurrent Playground
vertex work can still contribute. They are not isolated shader replay or
literal cache-miss percentages. See Apple's
[counter interpretation guidance](https://developer.apple.com/documentation/xcode/analyzing-apple-gpu-performance-using-counter-statistics).

All 64,206 baseline partial-render samples are zero, covering 1.284 seconds of
sample intervals distributed through the trace. Imageblock load/store limiters
average 0.38% / 7.91% across all samples. This does not support tile overflow as
the dominant cost. It does not rule out every register spill or unobserved
partial render. The limiter set has no per-register L1 traffic counters.
No memoryless/view workaround was enabled on A12.

A separate `graphics-compiler-spill-events` export does contain compiler
spill evidence: 7,549 baseline records report 32 bytes and join by command
buffer/encoder ID to `sceneprePassRT - RenderPass`; another 26 fall outside
the labeled encoder join. The comparison has 5,760 such scene records. None
join to the AO or blur encoders. These are reported compiler spill sizes,
not bytes of measured DRAM traffic, and the export does not identify the
individual shader or vertex/fragment stage within the scene encoder. Do not
multiply these sizes by pixels or interpret fewer records with fewer frames
as reduced register pressure. This makes scene-shader live values/register
pressure a concrete follow-up, distinct from tile attachment overflow. Apple's
[register-pressure guidance](https://developer.apple.com/videos/play/tech-talks/10580/)
describes why reducing live values or using narrower arithmetic can help;
these runs already use the existing F16 BRDF blocks.

The traces start 211.233 and 211.803 seconds into their moving-camera runs.
Restricting both to the common 212-219 second camera interval preserves the
finding: scene ALU limiters are 86.55% / 88.07%, AO 75.74% / 75.93%, and
partial-render samples remain zero. `phase-matched-counters.json` preserves
the filtered results.

### CPU and Presentation Separation

The baseline's 7,019 running CPU samples have 4,857 ms of leaf sample weight
in JavaScriptCore. The runtime thread accounts for 5,259 ms, versus 206 ms
on the main thread. These statistical weights are not wall-clock durations
or a per-frame CPU budget. Interpreter property lookup remains prominent,
despite the maintained accessor-inlining and shared-uniform optimizations.
The comparison has 7,147 samples, 5,154 ms in JSC and 5,551 ms on its runtime
thread. This is a mixed CPU/GPU workload, not evidence that the JS thread is
idle waiting for graphics throughout the frame.

Separately, the baseline has 479 main-thread drawable waits totaling
7,463 ms, averaging 15.58 ms; the skip-copy trace has none. Those waits overlap
other work and must not be added to the GPU pass timings. The embedding
render loop coalesces ticks while a JS animation frame is in flight, so
removing presentation backpressure also changes frame scheduling. A copy
count reduction alone does not establish a throughput improvement.

### Presentation Experiment

The baseline performs 477 distinct presentation copies for 320 scene frames,
costing 569.0 ms of compute activity (1.778 ms per scene frame including repeat
copies). A native opt-in experiment tracks pending image changes, coalesces
them, and skips acquiring/copying an unchanged drawable. Successful queue
submissions, texture writes, canvas creation/switching, and resize invalidate
the image. Only a successful presentation consumes the change, so a surface
timeout keeps it pending. Idle polling still services resource retirement.
It does not alter public JS APIs or use per-draw instrumentation.

`BABYLON_NATIVE_WEBGPU_PROFILE_SKIP_UNCHANGED_PRESENT=1` enables the experiment;
it is not enabled by default. Unit/GPU tests cover empty/single/many updates,
independent contexts, failed-acquisition retry state, queue submissions, direct
texture writes, canvas switches, resize, image scaling and format conversion.
All 11 graphics-crate tests and 41 moving-camera/material/AO contract tests pass.
All four 1242x2689 fixed-pose XS images are pixel-identical between variants.

The second diagnostic trace has 241 scene frames and 241 presentation copies,
with 199.7 ms of presentation compute work. However scene shading grows to
11.20 ms and AO to 5.83 ms per frame; the device reaches serious thermal state
earlier. Instrumented five-minute means are 47.88 versus 40.44 fps. This is not
an FPS win or a matched-temperature power comparison. Both begin nominal, but
only the second had a recorded 60-second nominal gate, and actual temperatures
and GPU frequencies in Hz are unavailable. The exported device-state track
uses opaque states 1 and 2 (with much less state 2 time in the comparison);
that is not enough to quantify a clock change or attribute its cause.
Lower sampled total bandwidth with fewer frames must not be called an
efficiency gain.

The comparison trace reports dylib-timeline warnings for background system
processes. GPU and counter exports nevertheless complete and parse with the
correct Playground PID. Early export attempts before trace saving completed
failed with `Document Missing Template Error`; those attempts are not results.

### Uninstrumented Confirmation

The same installed Release/LTO binary (UUID
`1185C386-13EA-3BDC-931C-073E75DE2D84`) was then tested in reverse order,
skip-copy first and control second, for 300 seconds each without Instruments.
Before each run, six-second idle checks every 30 seconds required at least
180 seconds of consecutive nominal thermal-state readings. Both kept
brightness exactly 0.250, charging state 2, Low Power Mode off, and the same
moving orbit with 46-59 active meshes. This matches the cooldown policy, not
an unavailable physical temperature measurement.

| Variant | Whole-run fps | Last 120 s fps | Frame interval p95 | First fair / serious thermal state |
| --- | ---: | ---: | ---: | ---: |
| Control, copy skipping off | 39.28 | 33.45 | 35.82 ms | 13.1 / 43.1 s |
| Copy skipping on | 40.51 | 34.54 | 33.71 ms | 15.0 / 45.0 s |

The clean repeat is 3.12% faster overall and 3.27% faster over the final
120 seconds with skipping. It does not reproduce the apparent regression
in the thermally unmatched instrumented pair, nor establish a repeatable
energy benefit. Both fall far below 60 fps despite starting around 58.8 fps.
Keep copy skipping **off by default**, available only as the named profiling
experiment. Do not change shaders, AO resolution, slice/sample counts,
material policy, or AR behavior based on this small single-pair gain.

Metal allocated memory is exactly 233.77 MiB at every timed sample in both
confirmation runs. There are no new textures, views, buffers, or render
pipelines during timing; each creates ten bind groups as camera visibility
changes. Native estimated GPU bytes remain 128,852,608. Process footprint
varies between 414.72 and 437.38 MiB for control and 420.16-434.85 MiB for
skipping, with repeated plateaus rather than per-frame accumulation. The
last 120 seconds start/end at 435.66/435.66 MiB and 434.81/434.81 MiB
respectively. This is a bounded five-minute observation, not a long-session
leak proof. The larger footprint changes in the Instruments runs must not
be confused with this uninstrumented result.

`run-summary.json`, `extra-summary.json`, the two cooldown receipts and
`xs-{skip,control}-confirm.{json,log}` retain the measurements. Four fixed-pose
RGBA captures from the diagnostic pair are identical, including materials
and under-car AO; the final control's car pose is also pixel-identical to
the baseline. The final control launch leaves the experiment disabled;
the benchmark exits normally. No device is left running the stress loop.

### Priorities After This Capture

1. Identify the specific scene vertex/fragment programs behind the 32-byte
   compiler spill reports, then shorten live ranges or narrow bounded
   arithmetic. A shader capture must retain the accepted F16 variant, MRT
   outputs, lighting, materials, and camera poses. The encoder-level records
   alone do not justify blaming the fragment shader or changing precision
   globally.
2. Test frame scheduling separately from duplicate-copy removal. In
   `Embedding/Source/View.cpp`, an in-flight JS callback prevents scheduling
   another JS frame at that host tick. Determine whether a bounded latest-tick
   handoff can preserve browser-shaped animation-frame ordering and improve
   pacing without accumulating work. Cover cancellation, suspension, teardown, multiple
   callbacks, exceptions, and timestamps before enabling a replacement.
3. Remove avoidable original-color copies through render-target reuse only
   after proving ownership across the whole AO chain. Preserve multisample
   resolve, texture format/color space, multiple cameras, resize, and
   pre-pass/geometry-buffer alternatives. Neighbor depth samples prevent
   treating AO as a memoryless same-pixel operation.

There is no evidence here to prioritize lower AO resolution or tile-overflow
workarounds over those targets. A12 tile-memory view restrictions remain in
force. Reduced copy counts or compiler spill-event counts are not substitutes
for sustained cadence and energy measurements.

### 2026-11-15: Shader Attribution and Bounded Frame Handoff

Working evidence is under `/private/tmp/bn-xs-20261115-spills`. These runs
retain native 1242x2689 rendering, the moving orbit, GT-VBAO slab-v3, F16
blocks-v3, accepted materials and shadow, and the verified optimized bundle
`081c385eb68a58619ca6bfad4007435c384ce66397e720a9fb6c30a05ade5812`.
AR remains disabled. No per-call JS probes are installed.

The new `AnimationFrameGate` can remember one display tick arriving while a
JS animation frame is running, then dispatch one continuation through the
ordinary JS queue. It does not recursively render or accumulate a backlog.
Suspend, view destruction, and runtime destruction invalidate the old gate;
resume creates a new lifetime. Nine focused C++ tests cover idle/queued/busy
states, drop-policy control, exceptions, repeated busy frames, and completion
and cancellation races. Coalescing is still opt-in through
`BABYLON_NATIVE_WEBGPU_PROFILE_COALESCE_RAF=1`.

Same-installed-binary 300-second runs, each preceded by 180 consecutive
seconds of observed nominal idle thermal state, produced:

| Scheduling | Whole run FPS | Final 120 s FPS | P95 frame interval |
| --- | ---: | ---: | ---: |
| Original drop policy | 43.86 | 38.03 | 44.33 ms |
| One pending display tick | 45.11 | 39.38 | 33.83 ms |

This is one ordered pair, candidate then control: +2.84% overall and +3.57%
in the final two minutes, not a durable 50 FPS result or an energy measurement.
Both runs used brightness 0.250, charging state 2, and Low Power Mode off.
Metal allocation remained 233.77 MiB. All four reference PNGs are byte-identical
between these runs and the earlier accepted comparison. Keep the candidate
opt-in pending reversed-order and lifecycle integration checks; AR was not
qualified in these runs.

Capture and attribution caveats:

- The opt-in iOS capture delegate records six host frames when launched with
  `MTL_CAPTURE_ENABLED=1` and
  `BABYLON_NATIVE_PLAYGROUND_CAPTURE_AFTER_SECONDS=<delay>`. Never enable it
  during FPS comparisons.
- `baseline.gputrace` originated on XS, but `gpudebug profile run` replayed it
  on M4. The capture's displayed A12 label is not the replay-device receipt:
  embedded `profile_info.json` identifies the local M4. Its zero-spill shader
  reports do **not** establish zero spills on XS.
- Scalar pre-pass depth removed unused varying components and produced four
  pixel-identical XS views, but the XS trace still contained 32-byte spill
  events. Do not promote this as a spill fix.
- The first scalar run did not package the new script and is excluded from
  shader comparisons. The harness now requires the expected variant suffix.
- Two limiter recordings ended after less than one second with
  `DeviceDisconnected`; they are not sustained counter evidence. Targeted
  attachment also failed to find a process that `devicectl` verified running.
  An all-processes Metal System Trace subsequently recorded 8.404536 seconds.
- The initial split-pass attribution helper replayed old pipeline bindings
  while restoring state. Compiler spill events occur on those rebindings,
  contaminating subsequent labels. Its counts are not per-shader attribution.
  The revised helper excludes old pipelines and splits only every sixtieth
  nonempty scene pass. Splitting forces attachment stores/loads and must never
  be used as performance evidence.

The scalar-depth, affine-UV matrix, world-space-normal, and single-channel AO
preludes are validation-only candidates. No resolution, precision, sample
count, material, or geometry reduction has been accepted as an optimization.

### Vertex Fetch Isolation

The corrected sampled split-pass trace attributes the same 32-byte compiler
spill report to all nine observed scene pipelines, including simple/unlit
ones. Scalar depth, affine UV arithmetic, and world-space normals do not remove
it. The affine-UV images match except for one already-variable pixel; the
world-normal images have mean absolute RGB differences below 0.004/255 with
one pixel above 8/255 across all four views. Neither is an established speedup.

`webgpu_spill_probe.js` isolates seven tiny, ordinary WebGPU pipelines. The
8-second `minimal-spills-supported.trace` includes 483 encoders per variant:
one/two/three float render targets, scalar depth output, and half arithmetic
with float outputs report no compiler spills. The two variants with packed
`snorm16x4` vertex buffers each report 32 bytes on all 483 encoders. Thus MRT
count and BRDF complexity are not required to reproduce the report. The
earlier half-output probe failed pipeline validation and is excluded.

The first Naga candidate replaced the local vertex-stride structure copy with
a `const device` reference. Its four scene captures were byte-identical, but
the XS still reported 16/32-byte spills. That candidate was rejected as a spill
fix; the source change is retained only in the experiment artifacts.

The expanded 12-pipeline probe isolates the actual cause: Metal's
`unpack_snorm2x16_to_float` in Naga's vertex fetch helpers. One `snorm16x4`
input reports 16 bytes, two report 32; float32, snorm8, storage-buffer fetches,
and the varying/MRT control programs report zero. The maintained Naga patch
uses signed-short conversion, division by 32767 and clamping at -1 instead.
This keeps packed input formats, bounds checks, strides and scene assets.
The existing five `msl-vpt` fixtures pass with only four generated helper
snapshots changed. A GPU point-grid checks all 65,536 signed encodings in four
lanes, including both endpoints, with at most 5.96e-8 error from the float32
reference. The shader does not read back during ordinary rendering.

Verified binary `594ddbd95f9894b229ce0e7d8bacfd4e0925be7ebc23755333c35bcf5d0fe5da`
reports zero compiler-spill events across 5,784 minimal-probe render encoders.
The separate full Hill Valley trace `xs-snorm-scene-attribution.trace` also
reports zero across 4,979 render encoders, including all nine scene pipelines.
This is measured A12 execution, not M4 replay. It removes the observed compiler
spill reports, not a proof about every shader or every hardware spill mechanism.

`minimal-spills-snorm-math.trace` is excluded: the JS probe had been updated but
CMake reused a stale Rust archive. The old intrinsic was still in the installed
binary. Cargo dependency files are now connected to CMake's `DEPFILE`, including
transitive Naga inputs. Twelve generator/configuration tests verify unchanged
builds stay idle and transitive edits rebuild. Naga is pinned to the same wgpu
revision and its patches are checked at configure time; apply/reapply/drift
tests fail on conflicting source. An explicit nested Cargo workspace patch is
build-only, separate from the upstreamable Metal change.

The R8 AO candidate has now rendered all four fixed poses pixel-identically.
Its three scalar intermediate targets preserve the original eight-bit AO
precision, filtering and sample count; Metal allocation drops from 233.77 to
229.09 MiB. The 45-second check is functional evidence, not sustained FPS.
Its subsequent 300-second run, after 180 seconds of nominal idle state, averages
44.38 FPS and 38.14 FPS in the final 120 seconds (P95 34.64 ms). Compared with
43.86 / 38.03 for the unchanged control, this is not a meaningful sustained
speed improvement. The memory saving and exact image match remain useful;
do not describe it as solving the thermal throughput limit.

### Spill-Free Combined Run

The 300-second `xs-snorm-combined-sustained` run uses the fixed unpacking,
world-space normals, R8 AO, one-pending-tick scheduling and unchanged-copy
skipping. It averages 48.03 FPS, starting at 59.99/59.93 in its first two
30-second windows and ending at 40.12. It has not met sustained 50 FPS.
The same 180-second nominal-idle gate, 0.250 brightness, charging state 2,
Low Power Mode off, moving orbit and native pixel dimensions are retained.
There are no Instruments or per-call probes during timing. Metal allocation
is constant at 228.69 MiB and no textures, views, buffers or pipelines are
created during the timed interval; ten bind groups appear as visibility changes.
The final 120 seconds average 42.13 FPS, versus 38.03 for the earlier control.
This combined comparison does not isolate the contribution of each change.

Four captured poses retain the sky, materials and under-car AO. They are not
byte-identical after the unpack arithmetic change: mean RGB differences are
below 0.015/255, with the largest view having 441 pixels above 25/255 out of
3,339,738 pixels. Do not call this an exact-image match. The source conversion
test above bounds numeric error; geometry, AO resolution and sample counts
are unchanged.

The subsequent eight-second `xs-snorm-scene-limiters.trace` contains 95,111
20-microsecond counter samples (1.90222 seconds of distributed coverage).
`Partial Renders Count` is zero in every sample. This rules out observed tile
overflow in that coverage, not all possible SRAM spills. Scene shading remains
ALU-limited (85.50% mean limiter versus 9.45% texture-cache limiter). GT-VBAO
is also primarily ALU-limited (74.16% versus 18.82% texture cache); bilateral
blur remains texture-sample limited (92.89%/98.56%). Pass labels name the
destination: `PostProcessRTT-ssao` is the original-color copy, `BlurH` receives
AO, `BlurV` receives horizontal blur, `ssaoCombine` receives vertical blur,
and `MainRenderPass` is the combine. Do not misattribute those labels to the
shader producing the input. Camera phase/clock states differ from the older
trace, so these counters locate remaining work rather than prove a speedup.

### Full-Screen Copy Experiments

A cached render-attachment presentation copy preserved every eight-bit value
in an M4 GPU test (RGBA/BGRA, linear/sRGB storage, alpha, changing source and
repeated use). After the same cooldown, its XS five-minute result was 47.15 FPS
overall / 42.05 in the final 120 seconds versus 48.03 / 42.13 with the ordinary
copy. It did not establish a throughput win and was removed from the working
implementation. Its source and exact run are retained under the experiment
directory as `rejected-presentation-copy.rs` and `xs-render-copy-sustained`.

`native_xr_portal_color_reuse_validation.js` separately removes SSAO2's
original-scene-color copy when the pre-pass retains that color. This is a
guarded non-AR, full-viewport, one-camera, single-sample experiment, not a
generalized Babylon.js change yet. It preserves the removed eight-bit
conversion in the final combine shader under a distinct shader-cache key;
the normal SSAO shader and other pipelines are unchanged. All four XS poses
are within one eight-bit level of the spill-free combined control, with no
pixels above 8/255. The under-car shadow, materials and sky remain visible.
Render passes drop from eight to seven per scene frame, command streams from
six to five, and Metal allocated memory from 228.69 to 215.19 MiB.

Four focused tests cover applicability/fallback guards, quantization, fresh
bindings and resize dimensions. The first attempted run lacked the packaged
script and is excluded; the harness rejects a missing variant and now checks
packaged scripts before launch. The correctly packaged 45-second check is
functional evidence only, not a matched sustained throughput result.

Animation-frame integration additionally passes the native JSC test for
peer callback order, cancellation during a frame, nested requests on the next
frame, common timestamps within a frame and nondecreasing timestamps between
frames. This complements the nine scheduler state/race tests; it is not a
claim that the entire browser animation-frame conformance suite was run.

### Matched Color-Copy Comparison

The final retained native binary for this pair is
`0d2d46f16449ffbf248ee2311c9a55f8fdc80920816d55518dec46dc88c877fb`.
Both five-minute runs start after 180 seconds of nominal idle thermal state,
at 0.250 brightness, charging state 2 and Low Power Mode off. Auto-Brightness
was disabled by the user. This matches the reported thermal-state protocol,
not a measurement of identical physical temperatures. Neither run uses
Instruments or per-call probes; both use the same native-resolution orbit.

| Variant | Whole run FPS | Final 120 seconds FPS | P95 interval |
| --- | ---: | ---: | ---: |
| Original color copy | 47.79 | 42.17 | 37.38 ms |
| Retained color reuse | 49.87 | 44.49 | 36.94 ms |

The single ordered pair is a 4.35% overall improvement, not a statistical
confidence interval. The optimized run ends at 42.93 FPS in its last window;
neither result establishes sustained 50 FPS. Its Metal allocation remains
215.19 MiB, with no new textures, views, buffers or pipelines during timing.
Ten bind groups are created as visibility changes. The camera view matrix
changes throughout and active meshes range from 46 to 59.

Receipts are `xs-color-reuse-sustained.json` and `xs-color-copy-control.json`
under `/private/tmp/bn-xs-20261115-spills`, with matching console logs and
`color-reuse-before-cooldown.json` / `color-control-before-cooldown.json`.

### Additional Arithmetic and Bundle Checks

`native_xr_portal_ao_algebra_validation.js` derives the second orthogonal
slice from the first sine/cosine pair and normalizes using inverse square root
after a squared-length guard. It does not change sample counts, thickness,
precision, projection, bitmask packing, blur or resolution. Four unit tests
cover the direction identity, guard, unchanged downstream shader and atomic
failure on source drift. All four XS poses are pixel-identical to color reuse.
Its warm-device 45-second run is only a functional check, not a matched
sustained performance comparison.

`native_xr_portal_render_bundle_validation.js` opts into Babylon's existing
dynamic cache (`compatibilityMode = false`), explicitly rejecting snapshots
and material/active-mesh freezing. Its four poses are also pixel-identical,
and visibility changes during motion. NativeWebGPU still replays C++ closures
for these bundles, including a per-bundle JS callback; this is not a real
wgpu render-bundle implementation. The unmatched warm check does not establish
a speedup, so the ordinary command-stream path remains the comparison default.

These two packaged checks use binary
`b69957a0cdacabdcb26537ad4053153dbb2c51ccaa5c1b662da0d68774e9e3d6`.
The maintained optimized Babylon bundle is unchanged. All 124 portal Node
tests pass, plus the two patch-stack tests, twelve generator/dependency tests
and nine scheduler state/race tests.

### Follow-up Arithmetic and Frame-Lifetime Results

The same 180-second nominal-state cooldown, 0.250 brightness, charging state 2,
Low Power Mode off and uninstrumented 300-second orbit were used below. These
are matched reported thermal states, not identical measured temperatures or
battery-power measurements. Neither arithmetic candidate improves on the
retained-color comparison, so both remain opt-in validation preludes.

| Variant added to retained color | Whole run FPS | Final 120 seconds FPS | P95 interval |
| --- | ---: | ---: | ---: |
| AO algebra | 47.83 | 42.46 | 37.11 ms |
| Bounded half-precision AO angle | 47.77 | 42.86 | 37.59 ms |
| Direct presentation plus frame scope | 48.05 | 42.40 | 33.49 ms |
| Frame scope alone, ordinary presentation | 47.58 | 40.26 | 33.75 ms |

The half-angle variant keeps reconstruction, depth, thickness, clipping and
mask operations in float32, narrowing only a guarded 2D cosine calculation.
Four tests exercise the finite range, fallback and shader boundaries. Its
four XS images differ by at most 2/255, with no pixels exceeding 8/255. Image
parity does not establish a performance win. Receipts are
`xs-ao-algebra-sustained.json` and `xs-ao-half-sustained.json` in the spill
experiment directory.

The first direct-presentation attempt is excluded: it reported destroyed
texture use and eventually terminated with signal 10. The host could finish
a frame and expire the canvas texture while its animation-frame callback was
still recording/submitting. A frame-completion scope now protects that callback
when `BABYLON_NATIVE_WEBGPU_PROFILE_FRAME_SCOPE=1`, also implied by
`BABYLON_NATIVE_WEBGPU_PROFILE_DIRECT_PRESENT=1`. Acquisition is nonblocking:
when the host frame is closed, the callback list is left intact for the next
display tick. A blocking acquisition could strand suspension while it waits
for JS to finish.

`TryAcquireFrameCompletionScope` is internal and implemented for both graphics
backends. The wgpu backend passes four tests covering zero/one/many scopes,
exception cleanup, between-frame acquisition and closed-frame deferral, plus
the two screenshot tests and nine scheduling state/race tests. The bgfx
implementation has not been built in this pass. The native JSC animation-frame
ordering/cancellation/nesting test also passes. These are focused checks, not
full WebXR or browser conformance qualification.

Corrected direct presentation produces four pixel-identical XS images and zero
compiler-spill events across 4,293 captured encoders. A separate 64-frame XS
GPU probe passes identity within a callback and Promise continuation, texture
expiration between frames, two independent canvases, readback colors, resizing
to an offscreen fallback and unconfigure. Readbacks are diagnostic only, never
inside the sustained comparison interval.

Direct presentation reduces reported Metal allocation to a stable 202.88 MiB,
but creates one JS/native texture and view wrapper per rendered frame. Its
19.16 ms mean JS frame elapsed time includes drawable acquisition waits; that
is not CPU utilization. The frame-scope-only control reports 13.41 ms. The two
builds have the same source behavior but different executable hashes after
resource packaging/signing, so this is not labeled a same-binary pair. The
direct experiment remains disabled by default. Its `_backendStats()`
`presentationPath` label still names the default offscreen path; use the
recorded environment and GPU encoders to identify this experiment.

### Bounded Environment-Irradiance Check

`native_xr_portal_irradiance_f16_validation.js` tests half-precision spherical
harmonics with a float32 fallback for HDR coefficients and non-rigid direction
transforms. It leaves the spherical-polynomial branch, geometry, depth,
roughness, specular evaluation and HDR accumulation unchanged. Both shader
stages explicitly require `shader-f16`; five tests cover source drift, both
stages, finite error bounds including flushed subnormals, and exact fallback.

The XS functional run `xs-irradiance-half-check` used binary
`42095ff5c643379b22ca90babc19b20efeac22c71b7e068e3a89bcbb01e22877`.
All four images differ from retained color by at most 1/255; mean channel
differences remain below 0.001407/255. The under-car shadow and materials remain
visible. Its 59.93 FPS over 45 seconds is only a functional warmup check, not
evidence of sustained 60 FPS. All 133 portal Node tests pass at this point.

The subsequent matched five-minute run, `xs-irradiance-half-sustained`, reports
48.67 FPS overall, 43.08 in its final 120 seconds and 36.86 ms P95. Metal
allocation stays at 215.19 MiB. This does not improve the retained-color
control, so the irradiance prelude is not enabled by default.

### Host-Side Drawable Acquisition

An isolated scheduling experiment added
`BABYLON_NATIVE_WEBGPU_PROFILE_PREFETCH_DRAWABLE=1` to direct presentation.
After a direct frame, the host acquires at most one next drawable before
opening the next frame to JS. `getCurrentTexture` consumes that reservation;
it does not expose or create a JS texture early. Unused reservations are
discarded at frame finish, including suspension. Idle frames stop further
prefetching, and resize/unconfigure clears the reservation. Failed acquisition
retains the ordinary fallback. Other canvases do not share the reserved image.
This was opt-in; AR was not qualified.

Binary `53108b57899107a4e24add6ae5530c1e0ed099f36541669ea3038ff68bc1f6a4`
passes the XS 64-frame GPU lifetime probe, now also interleaving idle frames.
All four moving-camera images are pixel-identical to retained color. The warm
45-second check reports 57.35 FPS and 11.40 ms JS frame elapsed time, but is
not a matched throughput comparison. Fifteen native scheduling/frame/screenshot
tests pass with this option enabled. The two shader-patch checks and twelve
build/dependency checks also pass at this revision.

The matched five-minute run `xs-prefetch-sustained` reports 45.13 FPS overall,
39.44 in the final 120 seconds, 34.12 ms P95 and 13.01 ms JS frame elapsed time.
Metal allocation stays at 202.92 MiB. Moving the wait reduced elapsed time on
the JS thread but did not improve throughput. The prefetch implementation and
its private C API were therefore removed. `prefetch-native-working-state.patch`
preserves the complete native working diff at the tested revision, including
other in-progress native changes; it is an evidence snapshot, not a standalone
patch to apply. Direct presentation itself remains an opt-in experiment.

### Retained Build and Scheduling Limit

The restored Release/LTO build is
`23e680a942e443f93865bee3d1cc9f5794a126a403c3ae9fae0154b923f660e4`,
signed with the existing Rebecker identity and installed on the XS. The
maintained Naga correction remains active. Direct presentation, additional
half-angle/irradiance arithmetic and frame-wide scoping remain opt-in; the
slower prefetch implementation is removed. AR has not been re-enabled.

An additional five-minute comparison, `xs-utility-sustained`, requests the
existing `BABYLON_PLAYGROUND_PROFILE_JS_QOS=utility` hint with the retained
color path. It starts after the same nominal thermal gate and remains nominal,
but averages only 17.23 FPS. No low-level core-residency capture was made, so
this is not a claim that a particular CPU cluster executed the thread. The
hint is unsuitable for this workload and stays disabled. Ordinary priority
is restored by launching a new process without that environment variable.

The best complete five-minute run remains 49.87 FPS overall / 44.49 in the
final 120 seconds, not durable 50 FPS. Its final two-minute samples have
215.19 MiB Metal allocation throughout and resident memory between 380.73 and
380.83 MiB. Physical footprint varies between 420.35 and 441.28 MiB; do not
claim that every process-memory metric is constant. No resolution, AO-sample,
geometry or material-quality reduction was used to reach this result.

### Final XS GPU Verification

`xs-retained-final-gpu.trace` is a valid 8.412450-second A12 capture of
Playground PID 9167 at the retained binary above. It reports zero compiler-spill
events across 4,337 application encoders. All 90,145 partial-render samples are
zero, covering 1.8029 seconds distributed through the trace. This verifies
removal of the observed spill reports; it does not prove every possible SRAM
or register spill mechanism is absent.

Pass-associated samples wholly inside Playground activity, excluding other
processes, retain the same dominant limiters:

| Producer | ALU limiter | Texture-cache limiter |
| --- | ---: | ---: |
| Scene pre-pass / PBR | 85.59% | 9.05% |
| GT-VBAO | 75.26% | 18.44% |
| Horizontal blur | 31.02% | 61.38% |
| Vertical blur | 30.19% | 69.02% |

These are limiter readings, not literal cache-miss percentages or a serial
frame-time budget. Horizontal and vertical blur remain strongly texture-sample
limited (93.38% and 98.49%). Attachment traffic must not be mislabeled a tile
spill when the partial-render counter is zero. Further gains need to address
the actual per-pixel shading/filter workload and CPU work, not assume that
unobserved tile overflow accounts for the remaining target gap.

The four final-build images are pixel-identical to the retained-color baseline.
Material diagnostics report 7/7 unlit/emissive backdrops, 11/11 reflective
materials with environment maps and zero material issues. The camera continues
moving with 46-59 active meshes. No new textures, views, buffers or pipelines
are created during the timed interval; ten bind groups accompany visibility
changes. The instrumented 45-second FPS value is not sustained-performance
evidence.

The trace contains a backdated-signpost visualization warning, but saves,
exports and parses successfully with the correct process and counter schemas.
Counter exports and attribution are `retained-final-counters.xml.gz`,
`retained-final-gpu.xml`, `retained-final-spills.xml` and
`retained-final-attributed.json` in the experiment directory. Final checks pass:
133 portal Node tests, 37 script syntax checks, 15 focused native scheduling /
frame / screenshot tests, one JSC animation-frame integration test, two shader
patch checks and twelve build/dependency tests. The existing five Naga MSL
vertex-pulling fixtures also passed during the shader-fix validation.

### USB Profiling Recheck

After the user confirmed the XS was connected and awake, both CoreDevice and
Instruments reported it online. `xs-usb-recheck-gpu.trace` completed and
exported successfully: 8.398421 seconds, Playground PID 9334, using the same
retained binary and non-AR moving-camera configuration. It reports zero
compiler-spill events across 4,347 application encoders. The trace again has
the backdated-signpost visualization warning, but no disconnect or export
failure.

The four reference images are pixel-identical to `retained-final-images`.
Material diagnostics report zero issues. The 45-second interval averages
59.95 FPS, with 46-59 active meshes, unchanged texture/view/buffer/pipeline
counts, ten additional bind groups and 215.19 MiB Metal allocation throughout
the timed interval. This is a diagnostic recheck, not a sustained performance
comparison: Instruments is attached, there is no new matched cooldown gate,
and battery state is now fully charged (3), rather than charging (2).
Brightness remains 0.250 and the reported thermal state remains nominal.
The earlier five-minute target gap remains unresolved.

All 89,195 partial-render samples are zero, covering 1.7839 seconds distributed
through the capture. Process-attributed samples again show scene/PBR and
GT-VBAO ALU limiters of 85.47% and 74.62%; horizontal/vertical blur texture-sample
limiters are 93.45% and 98.52%. These are limiter readings, not cache-miss rates
or frame-time shares, and the zero partial-render count does not establish
absence of every possible SRAM spill. The exports and attribution are
`usb-recheck-spills.xml`, `usb-recheck-counter-intervals.xml.gz`,
`usb-recheck-gpu.xml` and `usb-recheck-attributed.json` in the same experiment
directory. No rendering code or quality settings changed for this recheck.

## iPhone 12 Retest and Fork Drafts (November 15)

The A14 retest keeps AR disabled, native 1170x2532 resolution, the continuously
moving camera, GT-VBAO slab-v3, F16 blocks-v3, world normals, R8 AO, and the
retained color-reuse path. Bounded polling, frame-request coalescing and
unchanged-frame presentation skipping remain explicit profiling options.
No per-call probes, resolution reduction or material/active-mesh freezing are
enabled. The packaged Babylon bundle SHA-256 remains
`081c385eb68a58619ca6bfad4007435c384ce66397e720a9fb6c30a05ade5812`.

The first attempt, `a14-pr-retest-sustained`, stopped producing frames near
180 seconds and has no completed benchmark receipt. The user reported that
Playground remained visible and unlocked. Foreground reactivation later let
the app exit without a result. The cause is unproven; this run is excluded
from performance claims. Profiling-only keep-awake was then added to the iOS
controller, preserving/restoring the previous idle-timer setting. Ordinary
app behavior and rendering settings are unchanged. The rebuilt Release/LTO
app uses the existing Rebecker signing setup and executable SHA-256
`ed1b17831b5e9d4f49395f53b2ede1bd196b7e507b171f4038a428e7c6d812b4`.

After another 180 seconds at reported nominal thermal state, the repeat
`a14-pr-retest-keepawake` completed normally:

| Measurement | Result |
| --- | ---: |
| Timed duration / frames | 300.007 seconds / 17,901 |
| Overall FPS | 59.67 |
| Final 120 seconds FPS | 59.39 |
| Final 30 seconds FPS | 58.84 |
| Frame interval p95 | 18.09 ms |
| Largest interval | 34.27 ms |
| Scene elapsed time | 11.56 ms |
| Active meshes | 46-59 |
| Final 120 seconds Metal allocation | 200.23 MiB, constant |
| Final 120 seconds resident memory | 365.20-365.36 MiB |
| Final 120 seconds physical footprint | 386.44-396.41 MiB |

Scene elapsed time includes waits; it is not CPU execution time. Brightness
readbacks remain 0.250, USB power is connected (battery state 2), and low-power
mode is off. Reported thermal state progresses from nominal through fair to
serious. This is therefore not a power measurement, cool-running result or
proof of indefinite 60 FPS. Sixty-one motion samples show the changing camera.
No new textures, views, buffers or pipelines are created during timing; ten
bind groups accompany visibility changes. The recorder remains enabled with
no recorded errors. All four fixed-pose images are pixel-identical across the
keep-awake rebuild; materials, reflections and the under-car shadow remain.

The separate `a14-pr-retest-gpu.trace` capture lasts 8.894404 seconds and
attributes 4,500 application encoders to Playground PID 23146. There are zero
compiler-spill events. All 65,119 partial-render counter samples are zero,
covering 1.30238 seconds distributed through the capture. These results do not
establish absence of every possible register/SRAM spill mechanism. The trace
reports an overlapping-dylib visualization warning for `apfs_iosd`, but saves,
exports and parses successfully with the correct iPhone 12 and app identity.

Process-attributed samples wholly within scene pre-pass activity show a 97.87%
ALU limiter and 23.77% texture-cache limiter. These are limiter readings, not
literal miss rates or a serial frame-time budget. Postprocess render-target
labels are not sufficient to identify the shader in each pass and should not
be interpreted as standalone AO/blur timings. The instrumented 45-second run
is diagnostic only and is not included in the five-minute FPS result.

The separate `a14-pr-retest-snorm-probe.log` confirms all 65,536 snorm16
encodings plus endpoint lanes (262,144 values), maximum reference difference
5.96e-8, twelve pipeline variants and normal exit after 1,792 frames. Its one
correctness readback is outside scene performance measurements. Raw run logs,
JSON receipts, four-pose images, the trace, compressed counter export and
process-attributed summary are retained in `/private/tmp/bn-xs-20261115-spills`.

### Published Review Scope

Five independent draft PRs were pushed to the Rebecker Specialties forks:

- [wgpu #4](https://github.com/rebeckerspecialties/wgpu/pull/4): Naga Metal
  snorm16 vertex unpacking, five files, head `559ca93c45ca`.
- [Babylon.js #11](https://github.com/rebeckerspecialties/Babylon.js/pull/11):
  uniform comparison length, two files, head `6d63a66e35ea`.
- [Babylon.js #12](https://github.com/rebeckerspecialties/Babylon.js/pull/12):
  opt-in accessor/compression build controls, six files, head `52058e328da4`.
- [Babylon.js #13](https://github.com/rebeckerspecialties/Babylon.js/pull/13):
  command-word reservation/encoding, two files, head `f068ba2f41df`.
- [Babylon.js #14](https://github.com/rebeckerspecialties/Babylon.js/pull/14):
  shared/versioned PBR buffers, twelve files, head `1413360dbeaa`.

The bases are current upstream wgpu `a1d029a731855a6c8d49b717b0601ff3e2f05817`
and Babylon.js `d87d0ca6cf50c6608ec96653ff15418dfeaaae47`. Only fast-forwards
were applied to the fork base branches. Canonical working branches and dirty
experiments were preserved; isolated indexes and disposable source archives
were used, without adding worktrees. Naga lives inside wgpu, so there is no
duplicate PR against the archived standalone repository. No upstream PR has
been opened for these changes. Generated bundles, Cargo workspace plumbing,
native scheduling and validation-only shader replacements are excluded.

At the rebased heads, the focused Babylon.js fixtures pass: 17 comparison,
48 command-stream, 184 shared-material/related, 11 Node build-transform and
16 UMD compatibility cases. Core TypeScript checking and a 24-case differential
command-encoding check pass. Naga's snapshot selection covers five existing
vertex-format fixtures; both existing vertex-format GPU tests pass on M4 Metal.
Physical-device evidence uses the maintained wgpu 30.0.1/Babylon.js 9.36
backports, not newly rebuilt phone binaries from these rebased PR heads.

The broader wgpu run is not green: 1,216 passed, five failed, six skipped.
Three passthrough cases require unavailable DXC/Metal compiler tools. With
the user's approval, the subgroup expected-failure mismatch and zero written
timestamps were rerun against verified unchanged upstream trunk; both recur.
The matching selection also reports one executed subgroup pass and two
unsupported-feature timestamp passes. After restoring the candidate source,
both vertex-format GPU tests pass again. No expectations were weakened.
Test logs and published head/file-list receipts are in
`/private/tmp/bn-prs-20261115`. Its disposable 6.9 GiB Cargo test-output tree was
removed after validation, retaining source commits and evidence.

At publication, all five PRs are verified open drafts with exactly the intended
head commits and file lists. GitHub checks are still in progress. All four
Babylon.js dependency-review jobs fail with the same repository-configuration
message: dependency review is unsupported and Dependency Graph must be enabled.
No dependency files changed in these drafts. Repository settings and workflows
were left untouched; local test passes are not a claim of green hosted CI.

AR remains off. XS still has the previously documented final-two-minute
44.49 FPS limit in its best complete five-minute run; the A14 result does not
close that gap. Re-enabling AR next requires separate floor-lock, vertical
translation, clipping, camera-registration and AO/material validation.
