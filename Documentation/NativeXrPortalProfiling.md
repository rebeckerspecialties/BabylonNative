# NativeXR Portal Device Profiling

Checkpoint: November 12, 2026. This is a physical-device receipt, not a claim
that the XS performance target is met. The reconciled dependency pins are in
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
(327 ms), and `gBuffer` (293 ms). The AO kernel itself, `ssao`, was 53 ms.
These are accumulated labeled intervals, not exclusive per-frame wall times:
channels overlap and a pass can have several intervals. The result identifies
both scene/PBR fragment work and the AO pipeline, not merely a slow AO kernel.

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
