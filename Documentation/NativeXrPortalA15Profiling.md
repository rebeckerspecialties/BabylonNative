# A15 Apple TV Hill Valley Investigation

## Scope and Evidence

2026-11-15, Apple TV `team h`, AppleTV14,1, tvOS 27.3 (24K5286g),
UDID `00008110-00015D380283801E`. Release/LTO Playground, operating-system
JavaScriptCore, Rebecker Specialties signing. Non-AR moving-camera Hill Valley,
3840x2160 canvas backing verified through `getCurrentTexture`, preferred 60 Hz,
GT-VBAO, half-precision material blocks, world-space pre-pass normals, R8 AO,
and retained pre-pass color. No per-call probes in performance comparisons.

Raw receipts, images, scripts and traces are under
`/private/tmp/bn-a15-20261115`. This is an investigation, not a sustained-60-fps
or artifact-free visual qualification. The rendering FPS below counts scene
renders, not independently verified distinct frames displayed by the TV.

## Visual Correctness

The user reported washed-out HDR colors and distant shimmer. Two independent
HDR problems were found:

1. The Apple embedding path did not forward the HDR option into graphics at
   initial view creation. A TV output mode change alone did not establish a
   floating-point renderer surface.
2. Once forwarded, native wgpu's `Auto` color-space choice for `Rgba16Float`
   used a linear transfer. The default WebGPU canvas and Babylon's final color
   were sRGB-encoded. Labeling those values as linear lifted the midtones.

The runtime now forwards the option, explicitly requests supported
`ExtendedSrgb` float presentation, and keeps the Apple layer metadata aligned.
Float canvas screenshot readback no longer applies a second sRGB encoding.
Unsupported encoded-HDR requests fail with an actionable error rather than
configuring an unsupported pair or silently changing the transfer. SDR remains
available by leaving HDR disabled. This is the default-sRGB path, not a
claim that all WebGPU canvas color-space/tone-mapping options are implemented.

The A15 GPU grayscale probe passed for values 0, 0.25, 0.5, 0.75 and 1.
Corrected HDR versus SDR fixed-pose captures differ by at most one 8-bit RGB
code value in each of four 4K images (mean absolute difference 0.0070-0.0102).
The user separately confirmed improved colors/contrast on the physical display.
Readback parity is not a measurement of the TV's luminance or HDR headroom.
The retained-color AO fixture still reproduces the previous 8-bit color clamp;
this workload therefore does not establish extended-range highlight fidelity.

The remaining bush/store-window shimmer was unchanged with AO disabled and
with material arithmetic restored to f32, according to physical inspection.
The loaded asset textures have complete mip chains, trilinear sampling and
anisotropy 4; the one-level BRDF lookup and SSAO noise textures are intentional.
These controls argue against AO, f16, or missing mips as the cause of that
shimmer. They do not distinguish transparency ordering, depth precision,
specular aliasing, and subpixel geometry. The current pre-pass is single-sample,
without temporal antialiasing. Increasing only the non-XR near plane from 0.05
to 0.5 m fixed the store-window problem in user inspection but left the bushes
unchanged. The reversed-depth control also fixed the window while retaining
the original 0.05/1000 m near/far range. The user confirmed the car, sky and AO
remain correct. That manual control was ended early after confirmation; its
partial timing is not a five-minute sustained result. XR is unchanged.

The original `.babylon` asset in commit `a7a0b6a4` confirms a separate converter
bug. Its foliage materials have alpha 1, a diffuse alpha texture, no opacity
texture, and no `useAlphaFromDiffuseTexture` opt-in. Real StandardMaterial
evaluation selects alpha testing with cutoff 0.4. The GLB converter instead
selected `BLEND` from texture `hasAlpha` alone. The converter now queries
`needAlphaBlending()`/`needAlphaTesting()`, retaining the source cutoff. Ten tests
cover default, explicit opaque/mask/blend, fractional alpha, opacity textures,
and intentional glass hints. The eleven-foliage-material physical control left
the reported shimmer unchanged, so the converter bug is not established as its
cause. The control was ended early after feedback and is not a sustained
performance comparison. The packaged GLB remains unchanged pending broader
cutout-material validation. Texture/specular/subpixel aliasing remains open.

The user also reported moving noise in the car/building shadows. Increasing
GT-VBAO from two slices/two steps per side (eight depth samples) to four/four
(32 samples) reduced that noise in physical inspection. It also exposed dark
horizontal bands across the asphalt, worse during upward camera movement.
That diagnostic reached thermal state 2 about 14 seconds into its timed segment
and was stopped after feedback. It is not a proposed default or a matched
performance result. Screen-pixel jitter and spatial filtering without temporal
history remain quality limitations independent of the banding defect.

The GT-VBAO center used `textureSampleLevel` and reconstructed at `input.vUV`,
while neighbors used `textureLoad` and texel-centered rays. Nearest depth at an
unsnapped center ray does not lie on the surface represented by that texel;
linear filtering of view-space depth is not perspective-correct either. A
real Metal GPU regression reproduced false AO on analytical sloped planes at
half and noninteger AO resolutions. In the four/four half-resolution case,
107 of 384 flat-plane pixels were darkened (minimum AO byte 104, expected 255).

The `gtvbao-slab-v4` kernel loads center depth and normal from the same texel
and reconstructs its texel-center ray, matching the neighbors. Radius, strength,
thickness, blur, and default eight-depth-sample budget are unchanged. On M4 and A15,
`webgpu_gtvbao_plane_probe.js` executes the actual kernel at both sample counts:
all 24 plane/blocker cases pass, including portrait/landscape, both depth signs,
flipped and off-center projections, and 1:1/half/noninteger sizes. Every flat
plane is exactly white; raised blockers still produce occlusion. Receipts are
`plane-before.log`, `plane-after.log`, and `plane-a15-after.log`. The focused
shader/material/color tests pass 84 cases. The M4 45-second moving-scene smoke
ran at 113.80 scene FPS with unchanged resource counts and no renderer errors;
this is integration evidence, not a matched performance win. Its captures
retain materials and under-car AO.

On the repeated moving-camera A15 HDR inspection, the user confirmed:
"Bands gone; shadow preserved." This accepts the center-texel correction at
the original eight-depth-sample budget on the physical TV, independently of
the GPU regression and framebuffer captures. The repeat used the same rendering
settings with a temporary ten-minute inspection duration; it was not another
performance comparison. Foliage shimmer and temporal AO noise remain open;
this confirmation does not qualify those unrelated artifacts or other devices.

## Performance Receipts

| Run | Scene FPS | Last-minute FPS | Qualification |
| --- | ---: | ---: | --- |
| HDR, five minutes, before transfer fix | 40.13 | 39.55 | Nominal thermals; visually incorrect HDR |
| SDR inspection, five minutes | 42.40 | 41.87 | Nominal thermals; not a matched cooldown A/B |
| Corrected HDR, AO on/off/on inspection | 37.81 | 37.34 | Mixed diagnostic workload; not an optimization comparison |
| Corrected HDR, center-texel AO fix, five minutes | 37.53 | 36.79 | Nominal thermals; bands gone and shadow preserved in subsequent physical inspection; no matched pre-fix control |

HDR pre-fix Metal allocations stayed at 602.06 MiB during the timed interval;
SDR at 443.62 MiB. Neither establishes long-duration lifetime correctness.
The f32 inspection's host log recording failed with `ENOSPC`, so it is excluded
from performance claims even though the user could compare its visuals.

The center-texel run (`3840x2160-hdr-ao-center-corrected.json`) retained 4K HDR,
default eight-sample GT-VBAO, materials, and reversed depth. The camera view and
active mesh count varied throughout the run. Metal allocations were exactly
603.06 MiB in all timed samples; texture, texture-view, buffer, bind-group, and
pipeline creation counters did not advance. Resident memory ranged 291.03-294.86
MiB and footprint 921.42-954.19 MiB. Renderer errors and disabled batch streams
remained zero. Scene instrumentation averaged 8.97 ms, which is not an isolated
CPU-active measurement. P95 frame interval was 67.15 ms: sustained 60 fps and
consistent presentation pacing are still unmet. The diagnostic exited normally
after five minutes; a later Home screen is expected, not evidence of a crash.

### Valid GPU Trace

`3840x2160-hdr-limiters.trace`: Playground PID 5897, duration 8.917506 s,
`Time Limit Reached`, before the color-transfer correction. Process-attributed
GPU-active interval union: 8502.733216 ms, **95.35%**. This is active duty cycle,
not percent-of-peak utilization for every GPU unit.

| Work | Mean GPU time per encoder |
| --- | ---: |
| Scene pre-pass fragment shading | 11.22 ms |
| GT-VBAO | 5.31 ms |
| Horizontal / vertical AO blur | 1.23 / 1.27 ms |
| Final AO/color combine | 2.39 ms |
| Offscreen presentation copy | 3.11 ms |

The trace has 328 scene fragment encoders but only 210 presentation-copy
encoders. The copy cost is 1.99 ms amortized per scene render, not 3.11 ms
per scene render. Channels can overlap; do not sum the table into wall time.
This discrepancy also requires distinct-present cadence validation before
reporting the scene counter as displayed FPS.

Raw hardware counters were joined to the actual A15 accelerator's metadata.
Only samples whose adjacent timestamps remain inside the same exclusive
process-attributed fragment phase were assigned to a pass. Sampled means:

| Phase | ALU limiter | Texture-sample limiter | Texture-cache limiter | LLC limiter | Read / write GB/s |
| --- | ---: | ---: | ---: | ---: | ---: |
| Scene | 99.52% | 28.13% | 21.38% | 6.86% | 0.93 / 5.24 |
| AO | 84.27% | 6.73% | 16.13% | 64.60% | 8.72 / 0.13 |
| Horizontal blur | 24.79% | 99.78% | 39.91% | 8.30% | 5.07 / 0.89 |
| Vertical blur | 24.16% | 99.77% | 38.69% | 9.14% | 5.36 / 0.73 |
| Combine | 20.95% | 35.11% | 10.04% | 21.39% | 11.93 / 13.58 |

These are counter-defined limiter values, not measured cache-miss percentages.
Texture-cache misses do not uniformly explain the frame. Scene ALU, AO's
ALU/cache traffic, blur sampling, and full-resolution output traffic are separate
targets. All 86,533 partial-render counter samples were zero. The exported spill
event table was empty; this short warmed trace does not prove every shader is
spill-free.

`hdr-limiters-values.xml.gz` preserves the raw exported counters losslessly;
`hdr-limiters-summary.json` and `hdr-limiters-gpu-summary.json` are derived
summaries. The template's derived profiler table was empty, so conclusions use
the raw counter tables, not that empty table.

### Invalid and Mislabeled Captures

- `hdr-baseline` actually used SDR because option forwarding was still broken.
  Its valid trace measured 94.98% GPU-active time; do not call it HDR evidence.
- `hdr-forwarded` disconnected after 0.918186 s despite trace exit code zero.
  Its prematurely generated GPU summary is invalid and excluded.
- The first newly packaged color-probe attempt failed because project
  regeneration had not copied the script. Only `hdr-color-probe-packaged.log`
  is the passing on-device result.
- The first shimmer-control build, f32 host recording, and first reversed-depth
  recording launch failed for lack of disk space. These are host failures, not
  renderer/device failures. Subsequent disk cleanup preserves raw traces/images;
  only regenerable build manifests/test objects were deleted, and the large raw
  counter XML was losslessly compressed.

## Remaining JavaScript Work

A separate M4 JSC sampling run collected 11,256 leaf samples. This sampling run
is not used for FPS comparisons. Largest named sites included `bindForSubMesh`
(813), `updateUniform` (597), `render` (300), `tryAppend` (281), active-mesh
evaluation (267), and `_draw` (263).

Source-mapped lookup sites include the matrix-check policy, `_bufferData` and
`_uniformLocations` in `updateUniform`; pipeline-cache `values[this._states[i]]`;
draw-context `buffers`; material-context `textures`; `_drawWrapper`,
`prePassRenderer`, `effectiveBuffer`, and sampler wrap/filter fields. Samples
identify hot expressions, not an exact cost for each individual property read.
Several getters are dynamic contracts and cannot safely become unconditional
field reads. Accessor specialization and three Terser compression passes are
already enabled by the maintained bundle contract.

The narrowly scoped `0006` candidate hoists the matrix-check policy once per
`updateUniform` call, without caching it across calls or retaining an array that
copy-on-write may replace. Fourteen differential tests passed; the larger
optimization and shared-uniform suites passed 78 and 201 tests respectively.
Uninstrumented moving-camera M4 A/B/A at 2560x1440:
**111.35 / 111.69 / 112.66 scene FPS**.
All four fixed-pose candidate images match the first control exactly.
There is **no demonstrated speedup**; do not present this as an upstream
performance win. Further devirtualization must preserve overrides and same-frame
state changes and earn its maintenance cost in matched measurements.

## Next Gates

The user accepts 1440p HDR / sustained 30 fps as the next TV workload budget.
It is not an established common hardware ceiling: the A10X resolution matrix
was SDR, while this corrected 4K HDR run averaged 37.53 scene fps but had
uneven pacing. Keep those evidence boundaries explicit.

The user now reports moving, smoke-like AO noise on the blue diner's side and
more subtly on the asphalt. This is **not** the already-fixed horizontal
center-texel banding. The current shader uses a screen-pixel hash with eight
depth samples, view-aligned slices and spatial blur, with no temporal history.
Movement changes which random samples land on each surface; the
receiver-plane thickness estimate is also view-dependent. Both are hypotheses
for the observed instability, not a confirmed single root cause.

Keep sample placement and thickness as separate controls at the same eight
samples. A temporal solution needs reprojection, depth/normal history rejection,
camera-cut/resize reset and per-view histories; simply blending old AO would
leave trails. Bevy's implementation uses structured Hilbert/R2 noise,
slope-aware spatial edges and recommends TAA. Its current shader is WESL, not
a drop-in WGSL replacement for this pipeline. References:
[SSAO integration](https://github.com/bevyengine/bevy/blob/main/crates/bevy_pbr/src/ssao/mod.rs),
[sampling](https://github.com/bevyengine/bevy/blob/main/crates/bevy_pbr/src/ssao/ssao.wesl),
[spatial filtering](https://github.com/bevyengine/bevy/blob/main/crates/bevy_pbr/src/ssao/spatial_denoise.wesl).
Do not reintroduce the thermally expensive 32-sample control as a fix or erase
the accepted under-car shadow with stronger global blur.

1. Preserve the accepted HDR transfer, reversed depth, and center-texel AO
   correction. Foliage shimmer and temporal AO noise remain open. Do not hide
   them with a global blur or assume noisy shadows are an accepted cutdown.
2. Use the corrected eight-sample workload as the visual baseline and repeat
   it with matched cooldowns around optimization comparisons. The five-minute
   37.53-fps receipt is not evidence of a speedup over the earlier controls.
3. Compare AO algebra, half irradiance, and direct presentation independently,
   then only combine winners. AO algebra alone changed no pixel by more than
   one code value but its 39.66-fps short pre-fix run did not prove a speedup.
4. Revalidate direct-canvas lifetime/resize behavior after any presentation
   change, count distinct presents, and measure counters with matched thermals.
5. Keep A10X results separate: there are no valid per-resolution GPU duty-cycle
   or spill measurements for that device in this investigation. Its 4K cliff
   cannot be attributed to reflection probes or cascaded shadows that this
   workload does not render.

## Focused Checks

```sh
node --test Apps/scripts/testEmbeddingHdrOptions.mjs Apps/scripts/testNativeCanvasColor.mjs
node --test Apps/scripts/testNativeXrPortalShimmerDiagnostic.mjs Apps/scripts/testNativeXrPortalShimmerControls.mjs
node --test Apps/scripts/testBabylonUniformLoop.mjs
node --test Apps/scripts/testNativeXrPortalGpuExperiments.mjs
```

Run the grayscale probe on an actual HDR surface, not just a TV configured for
HDR. Preserve user display observations separately from framebuffer captures.
For the GPU plane regression, load `native_xr_portal_ao_projection_validation.js`,
then `native_xr_portal_gtvbao_validation.js`, then `webgpu_gtvbao_plane_probe.js`.
The probe must report `gtvbao-plane:passed=24` and exit zero.
