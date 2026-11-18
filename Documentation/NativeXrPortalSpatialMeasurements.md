# Spatial AO and interpreter measurements

Work date: 2026-11-16. AR disabled throughout. No PR, commit or push.
Raw receipts, hashes, logs, screenshots, traces and derived tables are in
`build_portal_spatial_20261116`. Negative results are retained.

## Controls

The maintained Babylon.js bundle remains SHA-256
`1b0a49e25149a876cee915f41b12387b5b438451faf61b7b62e9cf64e15f655d`.
The native Release/LTO executable, system JavaScriptCore interpreter, materials,
camera path, eight-depth-sample GT-VBAO estimator and R8 allocation are unchanged.
A10X uses 2560x1440 HDR; A12 uses 2689x1242 HDR at 25% brightness. Auto-Brightness
is disabled; USB charging remains connected. Results are not battery-power tests.

Four fixed-pose captures precede 480 warmup frames and timing. View-matrix and
camera-angle receipts prove motion during timing. CPU time is cumulative Mach
execution time on the JS thread, sampled only at timing boundaries and five-second
intervals. It includes native calls on that thread, excludes blocked waits, and
is not total process CPU or exclusively JavaScriptCore CPU. No per-call probes.

## Spatial variants

`native_xr_portal_ao_ign_validation.js` replaces only the noise sequence with
static f32 IGN. There is no animated sequence or temporal history.
`native_xr_portal_ao_slope_blur_validation.js` replaces the two existing legacy
blur passes, retaining seven AO and seven depth reads per axis and the R8 targets.
It fits the smaller same-sign reciprocal-depth derivative to a local plane and
rejects silhouette/clear-depth neighbors. It does not add passes or reduce AO
sample count, radius, thickness or scene resolution.

The first looped filter regressed A12. Generating six constant tap blocks once
at shader setup recovered performance. A GPU readback probe then caught a real
boundary bug: subtracting one pixel from a valid center could round below
`halfTexel`, incorrectly rejecting the first-column tap. Version 3 tests the
view's [0, 1) UV bounds and still clamps actual texture sampling to texel centers.

`webgpu_ao_spatial_probe.js` extracts and executes the installed shader function,
not a duplicate WGSL implementation. Its 48 cases cover both depth signs, two
non-square resolutions, positive/negative/zero plane slopes, clear depth,
silhouettes, isolated occlusion, screen boundaries and distinct view-local
inputs. M4 and A12 matched the R8 reference exactly; A10X's maximum error was
one quantization step. The original boundary check failed this probe.

The later manual replay exposed a gap in that 48-case test: depth and AO had
matching sizes in every case. Version 4 extends the probe to 240 cases, including
both blur axes and five depth/AO size pairs. See the mixed-resolution correction
below; the earlier v3 counters are not v4 performance qualification.

Textures must be local to one view. These tests do not qualify packed stereo
atlases, asymmetric XR projections, multiview rendering or temporal stability.
Packed stereo needs explicit per-eye bounds before this filter can be enabled.

### Short unprofiled runs

Each row is one 45-second run, not a sustained result or confidence interval.

| Device / variant | FPS | JS-thread CPU ms/frame | Thermal states |
| --- | ---: | ---: | --- |
| A10X control | 28.336 | 30.083 | nominal |
| A10X static IGN | 28.207 | 30.219 | nominal |
| A10X looped slope blur alone | 27.423 | 29.672 | nominal |
| A10X IGN + looped blur | 27.349 | 29.721 | nominal |
| A10X IGN + unrolled blur v2 | 27.992 | 30.272 | nominal |
| A12 control | 59.205 | 14.271 | nominal |
| A12 static IGN | 59.157 | 14.292 | nominal/fair |
| A12 IGN + looped blur | 51.580 | 14.673 | nominal/fair |
| A12 IGN + unrolled blur v2, cooled repeat | 58.802 | 14.484 | nominal/fair |

A separate v2 A12 attempt reached serious thermals and is not a matched
comparison. The M4 smoke rendered correctly but its older binary ignored the
HDR request; its reported FPS is not an HDR performance result.

### Five-minute unprofiled runs

These retain the same camera cycle, four captures, warmup, brightness and power
state. Each A12 run starts after 120 consecutive seconds of nominal thermal
state in the idle app. This matches the start gate, not the devices' exact
internal temperatures or clocks.

| Device / variant | First 30 s FPS | Full 300 s FPS | Last 30 s FPS | JS-thread CPU ms/frame |
| --- | ---: | ---: | ---: | ---: |
| A10X control | 27.673 | 27.518 | 27.308 | 30.685 |
| A10X IGN + unrolled blur v3 | 27.777 | 27.408 | 27.153 | 30.614 |
| A12 control | 59.493 | 43.713 | 40.144 | 15.996 |
| A12 IGN + unrolled blur v3 | 59.492 | 43.007 | 39.668 | 15.685 |

Both A12 runs reach serious thermals and settle near 40 fps. The new filter is
about 1.6% slower over the full interval; the larger loss from the initial 59 fps
also happens in the control. Neither demonstrates durable 50/60 fps. A10X stays
nominal with the new filter and reaches fair in the control, at roughly 27.5 fps.
The small CPU difference between these GPU variants is not an optimizer win.
The runner's `windows[].cpuMs` is Babylon elapsed frame instrumentation, not
Mach thread CPU; only `result.threadCpuMs / result.frames` is used above.

Metal allocations stay exactly 439.94 MiB on A10X and 279.50 MiB on A12 across
all 150 timed samples in each run. A10X footprint ranges 699-713 MiB, with
first-to-last changes below 3 MiB. A12 footprint fluctuates by tens of MiB and
ends about 32-35 MiB above its first sample; resident changes are -2.57 MiB in
control and +8.78 MiB in v3. Stable Metal allocation does not prove the entire
process leak-free. Receipts are `*-sustained.json`, with memory/window summaries
in `sustained-summary.json`.

The final A12 still capture retains car reflections and under-body shading.
The subsequent manual replay failed visual acceptance: the user reports visible
stripes in shadows on both A10X and A12, with the stripes flickering/bubbling
during camera motion. They are more noticeable on the TV. Still images and the
synthetic plane test did not establish temporal stability.

### Manual stripe isolation

`manual-replay-1` reproduces the combined IGN/slope-blur configuration. On A10X,
restarting with only IGN removed (`manual-slope-only-1`) retains the same blur,
camera path, materials, sample count and resolution. The user's first report was
**"Stripes gone"**, but longer inspection revised that: faint stripes remain
during particular sweeps away from the car's right side, and the bubbling/sizzle
never improved. Do not retain the first report as visual acceptance or evidence
that IGN was the sole cause.

Restoring both original noise and original blur (`a10x-manual-original-1`) was
somewhat improved near the car, but the cafe and parking-meter shadows remained
very noisy frame to frame. Thus the baseline has a separate unresolved noise
problem; neither the IGN nor v3 spatial-filter experiment solves it.

The IGN helper is retained as a labeled negative control, not an accepted
optimization. The default demo was never changed to use it. Whether the
corrected original-noise/slope-blur combination reduces the pre-existing bubbling
remains open. The initial A10X combined replay was deliberately interrupted to switch
controls; do not treat it as a completed sustained performance run. These
manual replays have no matched cooldown gate and are visual diagnostics, not
new performance A/B results.

### Mixed-resolution correction, v4

The v3 filter estimated a reciprocal-depth slope per nominal AO texel. Its
nearest depth reads actually land at unevenly spaced depth texels when depth
and AO resolutions differ. Treating those reads as equally spaced creates
nonzero residuals on a perfectly planar surface, so the filter can reject
neighbors as the camera/plane changes. The plane probe originally could not
catch this because it used equal depth/AO dimensions and only horizontal blur.

The expanded GPU test fails on v3 for 43x29 depth and 19x13 AO: at (0, 0),
negative-slope plane, expected R8 value 31 versus actual 0. V4 returns the actual
depth-texel coordinate alongside reciprocal depth, divides the left/right
derivatives by their respective spacings, and predicts each neighbor at that
neighbor's depth-texel coordinate. It retains seven depth and seven AO reads
per axis, two passes, R8, the AO estimator and the original noise.

The same 240-case GPU test passes on M4 and A12 with exact R8 agreement and on
A10X with maximum error one quantization step. Ten Node tests pass, including
a nonuniform-spacing case that explicitly fails the old model. The final
shared-module probe repeats successfully on M4. The v3 failure is retained as
`m4-ao-spatial-mixed-v3.log`; corrected results are `*-ao-spatial-mixed-v4*.log`.

V4 failed moving-scene visual acceptance too: on `manual-slope-v4-1` without IGN,
the user reports **"unchanged or worse"**. It remains validation-only. The
calculation fix and regression coverage are useful, but must not be presented
as a solution to thin-occluder sampling noise or the moving cafe shadow. V4 has
not had matched thermal/performance or spill-counter qualification. Both v4
replays were deliberately interrupted to restore the original noise and blur
in `visual-restored-control-1`; their partial receipts are not sustained results.

The next quality test needs a moving occluder/receiver fixture and an appropriate
reference, not another claim based on planar invariance or still frames. Static
IGN and this seven-tap spatial filter have not solved the user's reported
artifact. No candidate is promoted to the maintained demo. Further device-side
visual comparisons should first establish an objective improvement in that
motion fixture instead of repeatedly asking the user to judge small variants.

### GPU evidence

Six-second A12 captures contain valid process-attributed Metal activity and
limiter counters. Compiler-spill events are absent for Playground in baseline,
looped blur and unrolled v2 captures (3,002, 3,063 and 3,182 encoder records).
The sampled Partial Renders Count is zero, including scene, AO and blur activity.
This is evidence of no reported register/tile spills in those captures, not an
exact register count, SRAM occupancy measurement or universal no-spill proof.
The final v3 capture also completes (6.403 seconds, Playground PID 10828), with
3,094 encoder records, zero reported compiler-spill events, and zero sampled
Partial Renders Count in each exclusively attributed fragment group.

| A12 fragment work | Baseline ms/encoder | Unrolled v2 ms/encoder | Recorded v3 ms/encoder |
| --- | ---: | ---: | ---: |
| Scene pre-pass, two encoders per frame | 3.265 | 3.287 | 3.258 |
| AO producer (target labeled BlurH) | 2.844 | 2.633 | 2.689 |
| Horizontal blur (target labeled BlurV) | 0.716 | 0.753 | 0.753 |
| Vertical blur (target labeled ssaoCombine) | 0.475 | 0.523 | 0.528 |
| Main pass | 1.218 | 1.263 | 1.240 |

These are active fragment intervals, not whole-frame GPU completion time.
Clocks/thermal histories differ, so small timing differences are not proof of
a speedup. Counts and process IDs are retained in the pass summaries.
The legacy blur's texture-cache limiter averages 62.6%/69.6%; the new unrolled
blur shifts toward ALU (90.9%/94.5%), with cache limiter 41.6%/45.4%.
The scene pre-pass is predominantly ALU-limited (about 86%), and the AO producer
about 73%. Limiter percentages are not GPU utilization percentages.

Final v3 confirms approximately 91%/95% ALU limiter and 42%/46% texture-cache
limiter for the blur. The scene pre-pass remains about 86% ALU / 11% cache.
This is not evidence that texture-cache misses dominate the whole workload.

The A10X limiter recording disconnected at 1.147 seconds. A standard Metal
System Trace retry also disconnected, after 1.326 seconds. Its template/table
names and a successful file save do not establish valid counters. No A10X
no-spill claim is made. Both failed recordings are retained separately.

## CPU hypotheses

Five further controls were tested with the original AO, not the new filter.
They remain ignored experiments, not maintained patches. The last two were
source-form prototypes to test the opportunity before investing in a compiler
transform:

| A10X bundle | JS-thread CPU ms/frame | FPS |
| --- | ---: | ---: |
| Maintained control | 30.083 | 28.336 |
| Audited zero-argument field methods | 30.151 | 27.824 |
| Field methods plus separately proven object-literal assignments | 30.097 | 27.967 |
| Statement-preserving minification | 30.433 | 27.882 |
| Inline frame guard, outline cold frame-reset body | 30.674 | 27.668 |
| Skip redundant rounding for the private f32 temporary buffer | 30.433 | 27.686 |
| Restored maintained control, after experiments | 30.702 | 27.630 |

Method candidates remove 1,699 or 2,256 additional calls at compile time, but
neither demonstrates the requested reduction. The method, frame-guard and
rounding controls' four A10X fixed-pose captures are byte-identical to control.
The statement control retains `if` statements instead of minifier
conditional/comma rewrites; it also fails the performance gate. One of its four
captures differs by 701 pixels (maximum channel difference 26), so it does not
pass exact visual parity either. All five passed the 14 emitted-bundle
uniform/material differential tests. The frame-guard and rounding prototypes
also passed 201 source tests each. Both source edits were reverted and the
generated source's UMD output restored to the maintained bundle.
The method transformer has same-frame, independent-view, 0/1/n, override,
alias, receiver-side-effect, optional-call and null-access tests.

The closed-world method experiment does not promise compatibility with unknown
application monkey-patching. Public method definitions remain, but compiled
reads cannot observe arbitrary replacements outside the analyzed program.
No such new assumption is added to the maintained bundle.

The final control's CPU cost is about 2% above the initial control. The rounding
candidate is only 0.9% below that later control, not a repeatable 5% gain. This
drift is why a tiny isolated improvement is insufficient evidence.
The additional 5% CPU reduction requested by the user is **not demonstrated**.
Do not obtain that claim by comparing the existing inlined bundle against a
newly disabled-inlining control, or by counting GPU-induced CPU idle time.

### A14 efficiency-core PMU

Normal QoS uses performance cores for the interpreter. Startup background QoS
stalled rendering and is excluded. Utility QoS produced a real moving-camera
run; the logged JS thread ID joins to PMU rows on efficiency cores. The first
two captures use E cores exclusively; the restored repeat has 11,231 E-core
samples and three P-core samples, excluded from the E-core event ratios below.
The controls use the same source patches and three minification passes; only
accessor inlining is disabled in the control. Eight actual event counters are
exported, with duplicate-sample checks and thread attribution.

| JS-thread event | Maintained inlining | No inlining | Restored inlining |
| --- | ---: | ---: | ---: |
| L1 data-load misses / 1,000 instructions | 22.871 | 23.342 | 22.430 |
| L1 instruction-cache misses / 1,000 instructions | 0.548 | 0.484 | 0.557 |
| Branch mispredicts / 1,000 instructions | 6.341 | 5.917 | 6.336 |
| L1 data-TLB misses / 1,000 instructions | 9.566 | 11.442 | 9.447 |
| Cycles / instruction | 0.804 | 0.773 | 0.802 |
| Diagnostic JS-thread CPU ms/frame | 36.149 | 39.172 | 36.073 |

Inlining has a small absolute instruction-cache-miss increase, but better
measured CPU/frame in this diagnostic (about 7.7-7.9% below disabled inlining).
This does not support disabling it to fix a dominant instruction-cache problem.
The captures are short and instrumented; all three A/B/A receipts are retained.
They are not the requested new 5% unprofiled A10X/A12 improvement.

`/usr/share/kpep/cpu_100000c_2_1b588bb3.plist` identifies itself as `a14` and
exposes L2 TLB events, but no L2 cache-miss event. No L2 cache claim is made.
The normal-QoS Time Profiler capture attributes 5,325/6,518 ms of process running
sample weight to JavaScriptCore; LLInt property reads remain prominent.

## Qualification status

Both spatial controls remain validation-only. The maintained bundle verification
still passes with the exact original hash. No screenshot reference, tolerance,
readiness behavior or default demo shader was changed. The preceding full
screenshot baseline/particle-readiness audit remains in
`NativeXrPortalBundleOptimization.md`; it is not reclassified as a clean sweep.
This turn's final checks pass 38 spatial/accessor/experimental compiler tests,
43 script syntax checks, and the unchanged maintained-bundle receipt. The
48-case spatial GPU probe passes on M4, A10X and A12. The whole-tree
`git diff --check` still flags pre-existing CRLF/trailing whitespace in
`Embedding/Include/Shared/Babylon/Embedding/RuntimeOptions.h`; this work does
not rewrite that unrelated file.

Disk-full A10X and A14 attempts are excluded. The subsequent A14 PMU captures
saved and exported successfully. Only regenerable copied playground web assets
were reclaimed locally; the user separately freed disk space.
