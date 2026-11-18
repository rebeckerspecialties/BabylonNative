# Moving-camera AO stability investigation

Research checked 2026-11-16. This is an implementation/validation plan, not a
claim that temporal stability or the device performance budgets have passed.
The accepted eight-depth-sample GT-VBAO shader, material arithmetic, resolution,
and under-car shadow are unchanged during the JavaScript optimizer comparisons.

Implemented spatial controls, GPU regression tests and the A10X/A12 HDR
measurements are recorded in [Spatial measurements](NativeXrPortalSpatialMeasurements.md).
The controls remain opt-in: the longer A12 baseline also throttles below the
short-run 59 fps result. The combined IGN/slope-blur control failed the manual
check with crawling shadow stripes on both devices. Restoring original noise
initially appeared to remove the stripes, but longer inspection found residual
direction-dependent stripes and unchanged bubbling. V4 corrects a subsequently
reproduced mixed-resolution depth-spacing bug in the slope filter, but the user
also rejected v4 as unchanged or worse in motion. Both controls remain
experimental; the original noise and blur were restored on both devices.

## What other engines actually do

- [Unity URP's documented SSAO](https://docs.unity.com/en-us/engine/6000.5/manual/lighting-overview/lighting/shadows-in-urp/post-processing-ssao/ssao-renderer-feature-reference)
  supports static IGN or changing blue-noise textures. Its blur tiers are
  three-pass bilateral, two-pass Gaussian, and one-pass Kawase. The low-cost
  Kawase tier is not a guarantee of depth/normal edge preservation.
- [Current URP GTAO shader source](https://github.com/Unity-Technologies/Graphics/blob/master/Packages/com.unity.render-pipelines.universal/ShaderLibrary/SSAOCommon.hlsl)
  also has optional temporal filtering, depth pyramids, IGN direction/offset
  sequences, and history depth packing. These are current development-source
  paths, not evidence that every released/mobile URP configuration uses them.
- [Bevy's SSAO integration](https://github.com/bevyengine/bevy/blob/main/crates/bevy_pbr/src/ssao/mod.rs)
  strongly recommends TAA to reduce noise. Its
  [spatial denoiser](https://github.com/bevyengine/bevy/blob/main/crates/bevy_pbr/src/ssao/spatial_denoise.wesl)
  uses a single 3x3 edge-preserving pass, with packed directional depth edges
  and texture gathers. It is WESL plus Bevy render-resource plumbing, not a
  drop-in Babylon WGSL post-process.
- [XeGTAO](https://github.com/GameTechDev/XeGTAO#denoising) uses depth-aware
  spatial denoising and can rely on the engine's TAA, rather than owning a
  separate temporal AO accumulator. Its sampling notes describe why uncorrelated
  hash noise and merely rotating 2D blue noise are not interchangeable with a
  deliberately designed spatiotemporal sequence. Intel archived this project;
  it remains a useful reference, not a currently maintained mobile benchmark.
- [Filter-adapted spatiotemporal sampling](https://arxiv.org/abs/2310.15364)
  (2024 journal publication) optimizes noise for the spatial/temporal filter
  actually used, with an AO demonstration. This argues for evaluating the
  sample sequence and denoiser together, not assuming that any blue-noise or
  IGN sequence will have the same error after filtering.
- The [visibility-bitmask paper](https://arxiv.org/abs/2301.11376) models finite
  surface thickness so light can pass behind thin geometry. That model improves
  visibility, but does not remove screen-space disocclusion or supply temporal
  history validation by itself.

There is no general rule that mobile engines avoid temporal techniques. A
spatial-only path is nevertheless the lower-risk first experiment here: we
currently have neither a qualified stereo history path nor a measured bandwidth
budget for another history surface. IGN changes sample distribution; it is not
temporal reprojection and cannot by itself guarantee a stationary shadow.

## Current shader risks

`native_xr_portal_gtvbao_validation.js` uses a screen-pixel PCG hash, two slices,
two samples per side, a 32-bit visibility mask, and a receiver-plane-derived
thickness slab. The AO and pre-pass centers now agree at the texel center; keep
that fix. The existing SSAO2 blur is reused after AO evaluation.

Two distinct causes can produce the reported moving wall noise:

1. High spatial variance from only eight independent depth samples, made visible
   as geometry moves through a screen-fixed hash pattern.
2. View-dependent changes in reconstructed slab thickness or depth discontinuity
   selection. Denoising can hide this but cannot repair incorrect visibility.

The second cause must be tested independently. Keep the under-car silhouette,
the diner wall, a sloped plane, a thin post, and depth-disocclusion boundaries in
the moving-camera corpus. Retain the previous 32-sample variant as a short
reference only; it previously exceeded the thermal budget.

## Recommended experiment order

1. Replace only the PCG noise with static eye-local IGN, with decorrelated slice
   angle and radial-offset sequences. Keep eight samples, radius, thickness,
   resolution, and intensity identical. Measure motion noise and shadow extent,
   not just a still-image difference.
2. Compare the existing blur against a compact slope-aware edge filter. Begin
   with Bevy/XeGTAO-style directional depth edges and one small spatial pass;
   measure the edge texture/store cost against recomputing depth differences.
   A separable bilateral alternative needs two intermediates/passes unless it
   replaces existing blur passes. Count that traffic explicitly.
3. Test an edge-aware Kawase alternative only with rejection based on depth (and
   normals where needed). Ordinary color-only Kawase risks halos and removing
   contact shadows, especially under the car and around the tires.
4. If spatial filtering still crawls, prototype a short AO-only history. Reproject
   using the appropriate previous view/projection, reject disocclusions, clamp
   history to the local AO range, and limit history weight during motion. Do not
   animate the sampling pattern without a corresponding validated accumulation
   strategy. No CPU readback is needed.

The current pipeline has two legacy bilateral blur passes, not the tunable
eight-sample path suggested by its configuration. The portal sets
`expensiveBlur=false`, which `ThinSSAO2BlurPostProcess._getDefinesForBlur`
turns into `BLUR_LEGACY`. That shader ignores `bilateralSamples=8`,
`bilateralSoften=0.45` and `bilateralTolerance=0.25`. It uses a fixed center plus
three symmetric offset pairs (seven AO and seven depth texture sample
instructions per pass), a `0.003` depth-difference denominator and capped
weights. Actual texel/DRAM transactions are not equal to those instruction
counts because filtering and caching still apply.

On a sloped receiver, changing view-depth differences can change which neighbors
the legacy filter accepts. This is a concrete reason to test blur behavior
separately from IGN; it is not yet proof of the reported moving-wall artifact.
Use `expensiveBlur=true` with the existing eight-sample settings as one bounded
control, then compare a slope-aware replacement at the same AO sample budget.
Do not assume the previous soften/tolerance settings were already helping.

Adding two more passes is not the intended experiment. Replacing the existing
passes is preferable. Bevy's
current denoiser uses R16F storage when supported, otherwise R32F, plus packed
directional edges. A literal port would therefore not preserve our R8 traffic
budget. First evaluate an R8 fragment-pass adaptation and compare the cost of
an extra edge surface against depth-edge recomputation. A compute variant
needs measured benefit and supported storage formats, not just an 8x8 launch.

Keep IGN arithmetic and pixel coordinates f32, including at 4K. Coarse f16
coordinate/hash precision is not a useful power optimization if it changes
the noise distribution. Keep stochastic jitter stationary for spatial-only
tests. Compare motion-compensated AO error along the fixed camera path;
unwarped consecutive-frame differences include legitimate geometry motion
and cannot alone establish flicker improvement.

At a full 2560x1440 allocation, one R8 surface is 3.52 MiB; two RGBA8 history
surfaces are 28.13 MiB. Half width/height reduces these by four. These are storage
sizes, not measured DRAM traffic: bilinear taps, caches, render-pass stores and
loads still need GPU-counter validation. Sampled depth cannot be made transient
and discarded while later passes still need it; WGSL workgroup memory is not a
replacement for persistent attachment contents across passes.

## Stereo and acceptance gates

- Use eye-local pixel coordinates for noise and per-eye projection/viewport for
  reconstruction. Clamp every tap to its eye rectangle or array layer; never
  filter across side-by-side eye boundaries.
- Keep temporal matrices, depth validation, and history ownership separate per
  eye. Synchronize sequence progression, not history contents. Invalidate on
  camera cuts, resize, view-count changes, tracking discontinuities and session
  restart. Include asymmetric projections and reversed depth in tests.
- Compile spatial-only and temporal variants up front. Retain a non-subgroup
  fallback; subgroup width and support are not portable across A10X, A12, XR2,
  and visionOS hardware.
- Compare moving-camera HDR runs at matched brightness, power state and thermal
  windows. Preserve the car's under-body shadow, wall/curb edges, materials and
  sky. Targets remain at least 50 fps A12, about 30 fps A10X, and about 90 fps M4.
  Those targets are acceptance gates, not results of this research.
- Record frame time, actual JS-thread execution time, GPU time, texture traffic,
  registers/spills, attachment traffic, memory plateau and sustained thermals.
  A lower average FPS alone cannot identify a tile or register spill.

## Automated instability measurement and the cosine-weighted estimator (2026-11-17)

Still frames and planar probes could not show whether a change helped in
motion, so every moving-scene judgment went back to a person. The capture
prelude `native_xr_portal_stability_capture.js` and
`Apps/scripts/measurePortalStability.mjs` replace that loop for the non-AR
Hill Valley workload:

```sh
node Apps/scripts/measurePortalStability.mjs \
  --playground <build>/Apps/Playground/Playground.app/Contents/MacOS/Playground \
  --out <directory> --label candidate [--modes whole-pixel,sub-pixel] [--ao-script <prelude>]
```

Five fixed poses (orbit, cafe wall, car, tree crown, hedges) are rendered while
the whole image is shifted through the projection matrix. No reprojection or
depth is needed to align frames.

- **Whole-pixel shifts** leave rasterization identical. The temporal standard
  deviation of the aligned frames is instability locked to the screen. With AO
  strength zero this control reads 0.01 to 0.02 code values, so everything
  above that floor comes from the AO pipeline.
- **Sub-pixel motion** at a constant 0.25 x 0.125 pixels per frame makes a
  band-limited image change smoothly. The mean temporal second difference
  isolates popping, including alpha-coverage flips that whole-pixel shifts
  cannot show.

Values are 8-bit luma code values at 2560x1440 with the HDR surface on the M4.
Shifts emulate camera motion locally; they do not include parallax or
disocclusion. The measure does not replace a look at the device.

### What the measure showed about the accepted estimator

`native_xr_portal_gtvbao_validation.js` (uniform hemisphere, hashed noise, legacy
blur) against `native_xr_portal_gtvbao_cosine_validation.js`, mean temporal
standard deviation under whole-pixel shifts:

| Pose / region | AO term, old | AO term, new | Final frame, old | Final frame, new |
| --- | ---: | ---: | ---: | ---: |
| Orbit, whole frame | 0.93 | 0.70 | 0.28 | 0.23 |
| Cafe wall, whole frame | 1.31 | 1.13 | 0.48 | 0.39 |
| Car, whole frame | 1.24 | 1.17 | 0.40 | 0.38 |
| Trees, crown | 10.27 | 0.32 | 3.43 | 0.13 |
| Trees, hedge | 7.81 | 0.99 | 2.34 | 0.37 |
| Bushes, hedge | 4.54 | 0.64 | 1.35 | 0.22 |

Split by where it occurs, using a converged run (8 slices, 12 steps per side)
as the reference for what counts as shadow or edge:

| AO term, mean | Old | New | Converged, 0.3 scale | Converged, 0.5 scale | Converged, full scale |
| --- | ---: | ---: | ---: | ---: | ---: |
| Open surfaces | 0.24-0.38 | 0.02-0.05 | 0.08-0.24 | 0.14-0.25 | 0.25-0.29 |
| Smooth shadow | 1.34-1.64 | 1.24-1.55 | 0.95-1.65 | 0.82-1.33 | 0.55-0.73 |
| Shadow and silhouette edges | 3.41-4.16 | 3.71-4.97 | 4.27-6.38 | 2.18-3.27 | 0.71-0.97 |

Findings:

1. **Foliage was the largest instability by an order of magnitude.** Leaf cards
   occlude each other as thick slabs, giving a near-binary AO speckle that
   changes with every sub-texel of motion. Under sub-pixel motion the old
   estimator doubles foliage popping over no AO at all (tree crown 4.34 against
   2.00); with alpha-textured cutouts excluded from the AO geometry it returns
   to the no-AO floor (2.05).
2. **Alpha-tested foliage is worse, not better.** Switching the eleven foliage
   materials to cutouts with `native_xr_portal_foliage_alpha_validation.js`
   triples crown popping (6.13). Without multisampling the blended asset is the
   stabler choice; do not regenerate the packaged GLB with `MASK` foliage on the
   strength of the converter's alpha-mode fix alone. Matte foliage shading
   (roughness 1, no specular) changes nothing, so specular aliasing is not the
   cause of the remaining floor.
3. **The pane-wide darkening on the cafe is uniform weighting.** A 15 cm recess
   half a meter away covers 29% of a uniformly weighted hemisphere and 8.5% of a
   cosine-weighted one. With cosine weighting the cafe wall's mean AO rises from
   223 to 239 while the ground around the car stays at 170.
4. **Open surfaces no longer sizzle.** The ordered 4x4 sequence with a filter
   that gives every phase an equal share removes the static noise field; a
   random field of the same sample count cannot be filtered out by any kernel
   this small.
5. **What remains is the AO grid, not sampling noise.** A converged estimator at
   the same 0.3 scale is no steadier in shadow or at edges. Edge instability
   falls with AO resolution (about half at 0.5 scale, one sixth at full scale)
   and smooth-shadow instability barely moves. More samples per texel will not
   buy stability; resolution or temporal accumulation will.
6. **Eight samples under-estimate occlusion.** The new estimator is 22 to 32
   code values lighter than the converged run in shadowed regions. Three steps
   per side (12 samples) recover 18 to 24; one slice of four steps has less
   bias and much more instability. The slice and step counts are unchanged.

Rejected along the way, with the same measure: a Hammersley sequence with
full-stratum radial jitter (lighter and less stable), soft or bilinear
fallbacks in the upsample (edges 5 to 15% worse), and placing partly covered
AO texels on their plane along the center ray (no change).

The new prelude costs one AO-resolution RGBA32F target: Metal allocations on
the M4 rise from 126.71 to 130.98 MiB. Its AO, filter and geometry passes all
run at AO resolution; the old blur ran at 0.45 scale against full-resolution
depth.

### Lit walls and glazing

The teal cafe wall facing the car showed two faults that the stability measure
does not see, because both are steady.

**A shadow climbing the wall from the pavement.** AO treats every occluder as
black and the combine multiplies it into fully lit color. With the 0.85 m
radius the pavement shaded the whole wall below the window sills. Luma of that
wall from sill to pavement, AO off 115 throughout:

| Estimator | At the sill | Halfway | At the pavement |
| --- | ---: | ---: | ---: |
| Old (uniform, 0.85 m) | 110 | 86 | 52 |
| Cosine, 0.85 m | 115 | 88 | 55 |
| Cosine, 0.5 m, multi-bounce | 115 | 112 | 82 |

Cosine weighting alone does not help: a floor meeting a wall occludes half of
either hemisphere. Two changes do. The cosine prelude now defaults the radius
to 0.5 m unless `__nativeArPortalSSAORadius` is already set, which confines the
gradient to the lowest third of that wall and keeps the contact shadow under the
car. `native_xr_portal_ao_multibounce.js` then applies the interreflection fit
of Jimenez et al. (2016) in the combine, with the lit scene color as the
reflectance, so bright surfaces get most of their occluded light back and dark
asphalt under the car keeps its occlusion. It costs three multiply-adds per
pixel and no texture reads.

**Black marks inside the windows.** Blended materials were written into the AO
geometry. A pane wrote its depth, then every blended object behind it that
happened to draw later overwrote single patches, which the estimator saw as pits
several meters deep behind a flat surface. The AO geometry now holds opaque
surfaces only; blended and alpha-cutout materials draw color alone.

Whole-pixel instability with both changes, mean temporal standard deviation:

| Pose / region | AO term | Final frame | Final frame, old estimator |
| --- | ---: | ---: | ---: |
| Orbit, whole frame | 0.99 | 0.26 | 0.28 |
| Cafe wall, whole frame | 1.69 | 0.43 | 0.48 |
| Car, whole frame | 1.44 | 0.36 | 0.40 |
| Trees, crown | 0.28 | 0.08 | 3.43 |
| Trees, hedge | 0.69 | 0.19 | 2.34 |
| Bushes, hedge | 0.53 | 0.16 | 1.35 |

The AO term is less steady than before on the cafe and orbit poses because the
estimator now resolves the cluttered interiors behind the glazing, where it
previously saw one flat pane. The final frame stays at or below the old
estimator everywhere.

### Frame rate

Moving-camera benchmark, 45 seconds, HDR surface, plain Release build (no
link-time optimization) with the operating-system JavaScriptCore. Phone runs
alternate variants with rests of three to four minutes; each figure is one run.
These are short runs, not sustained thermal results.

| Device, render size | Old estimator | Cosine, surface-aware upsample | Cosine, bilinear upsample |
| --- | --- | --- | --- |
| iPhone 16 Pro Max (A18 Pro), 2868x1320 | 93.4, 97.4, 99.7 fps | 88.5, 89.4 fps | 96.9 fps |
| iPhone 12 (A14), 2532x1170 | 56.7, 56.9 fps | 54.9, 56.1 fps | 56.6 fps |
| iPhone XS Max (A12), 2689x1242 | 48.6, 48.8 fps | 45.2 fps | 48.2 fps |
| M4 Max, 2560x1440 | 55.5, 58.7 fps | 53.1, 59.6 fps | not run |

The surface-aware upsample is the only measurable cost: about 8% on the A18
Pro and A12 and 1 to 3% on the A14. With the bilinear read
(`native_xr_portal_ao_bilinear_upsample.js`) the cosine estimator runs at the
old estimator's speed, at the price of 10 to 20% more AO instability and worse
outliers beside silhouettes (car-ground p99 25 against 13). A second A12 run
with the surface-aware upsample reached the fair thermal state and its result
line was truncated in the device console; it is not counted.

The M4 run is paced near 60 Hz and its spread is run-to-run noise. Launch the
macOS Playground through LaunchServices for timing: a window started from a
background shell is timer-throttled into 100 ms frames. On the iPhone 16 Pro
Max the first run after a rest paces at 120 Hz and immediate repeats fall to
60 Hz pacing whatever the variant, so unrested back-to-back runs are not
comparable.

JavaScript thread time per frame for the old estimator is 6.7 ms (A18 Pro),
9.8 ms (A14) and 14.0 ms (A12) for about 72 draws and 7 passes. The A12 spends
84% of a 60 Hz frame on that thread before any GPU wait, so it misses vertical
sync on the interpreter, not on AO.

### Interpreter cost and the render-bundle path

`native_xr_portal_render_bundle_validation.js` turns off WebGPU compatibility
mode, so Babylon.js caches each draw in a render bundle instead of re-encoding
it through the command stream every frame. Same workload with the old
estimator, two runs each, rested:

| Device | Frame rate | JavaScript thread per frame |
| --- | --- | --- |
| iPhone XS Max (A12) | 48.6 to 51.4 fps (+5.8%) | 13.9 to 12.9 ms (-7%) |
| iPhone 12 (A14) | 56.5 to 58.1 fps (+2.8%) | 9.9 to 8.8 ms (-10%) |

The four fixed-pose captures on the M4 are identical to the default path in
every pixel, with no validation errors. This is the non-AR scene over 45
seconds; the AR portal, sustained thermals and the A10X are not measured.

### Frozen materials

`native_xr_portal_frozen_materials_validation.js` freezes every material once
the scene has been ready for sixty frames, on the render-bundle path. Babylon.js
then stops re-deriving defines and rewriting each material's uniform block per
draw. JavaScript thread time per frame on the M4 with the cosine estimator, 30
seconds each, all four fixed-pose captures identical in every pixel:

| Path | ms per frame | Change |
| --- | ---: | ---: |
| Default | 9.71 | |
| Render bundles | 8.43 | -13% |
| Frozen materials, default path | 7.40 | -24% |
| Render bundles and frozen materials | 6.98 | -28% |
| The same with frozen world matrices and the dirty mechanism blocked | 6.88 | -29% |
| WebGPU snapshot rendering, fast mode | 3.39 | invalid |

Freezing the active mesh list was slower in this scene, because it draws all
136 meshes where culling draws about 72. Fast snapshot rendering is not usable
yet: draws that leave the pre-pass targets record single-target pipelines
inside the three-target pass, which fails validation, and the camera stops
updating. It is the largest remaining interpreter lever if Babylon.js can
record that pass correctly.

Five-minute runs, cosine estimator with the surface-aware upsample, HDR:

| Device | Path | JavaScript thread, first to last minute | Frame rate per 30 s |
| --- | --- | --- | --- |
| iPhone XS Max (A12) | Default, old estimator | 13.8 to 16.3 ms | 50, 46, 33, then 27 to 33 |
| iPhone XS Max (A12) | Default | 14.0 to 15.8 ms | 47, 35, then 27 to 28 |
| iPhone XS Max (A12) | Frozen | 9.9 to 12.9 ms | 49, 40, then 28 to 32 |
| iPhone XS Max (A12) | Frozen, bilinear upsample | 9.7 to 11.8 ms | 56, 49, 36, then 31 to 34 |
| iPhone XS Max (A12), 2016x932 backing | Frozen, bilinear upsample | 9.7 to 11.3 ms | 56, 55, 54, then 38 to 44 |
| Apple TV 4K (A10X), 2560x1440 | Default | 28.6 ms | 16 to 17 |
| Apple TV 4K (A10X), 2560x1440 | Frozen | 21.6 ms | 18 |

The A12 rows other than the last render 2689x1242. The interpreter saving is
durable: 24% on the A10X and 25 to 30% on the A12 over the whole five minutes. The frame rate does not follow it, for the reason
in the next section. A frozen material ignores later changes to lights, clip
planes and its own properties, so the AR portal needs its own validation before
this path is used there.

### Presentation stalls

Both devices lose their frame rate to stalls of 100 to 140 ms between frames,
not to slow frames. In the A12 default run the median frame interval is 18.7 ms
and the 95th percentile is 104 ms; the scene's own render time stays near 14
ms throughout. The stalls start when the phone reports a fair thermal state,
35 to 60 seconds into a run, and the phone reaches the serious state within 60
to 90 seconds in every variant, the old estimator included. At three quarters
of the linear resolution the stalls are shorter (95th percentile 61 ms) and
the phone holds 54 to 56 fps for 90 seconds before settling near 40. None of
these runs sustains 50 fps on the A12.

On the A10X the stalls do not need heat. Short runs at 2560x1440, nominal
thermal state, old estimator unless noted:

| Surface and backing | Path | Frame rate | Median / p95 interval | JavaScript thread |
| --- | --- | ---: | --- | ---: |
| HDR, 2560x1440 | Default | 16.6 fps | 46 / 121 ms | 29.7 ms |
| HDR, 2560x1440 | Default, cosine bilinear | 16.3 fps | 48 / 127 ms | 30.2 ms |
| HDR, 2560x1440 | Default, cosine surface-aware | 16.0 fps | 45 / 143 ms | 28.9 ms |
| HDR, 2560x1440 | Frozen | 18.9 fps | 36 / 123 ms | 22.3 ms |
| HDR, 1920x1080 | Default | 20.1 fps | 42 / 92 ms | 29.6 ms |
| HDR, 1920x1080 | Frozen | 23.1 fps | 35 / 90 ms | 22.3 ms |
| SDR, 2560x1440 | Default | 21.7 fps | 43 / 63 ms | 31.1 ms |
| SDR, 2560x1440 | Frozen | 26.2 fps | 34 / 57 ms | 25.1 ms |

The AO estimator is not the cost on this device: the three estimators are
within 4% of each other. The stalls follow the HDR surface, a 3840x2160
`Rgba16Float` drawable, and remain at a 1080p backing. They match the
`Wait for Next Drawable` waits recorded for the 4K SDR backing in
[A10X profiling](NativeXrPortalA10xProfiling.md). In SDR, where the device is
bound by the interpreter, frozen materials raise the frame rate by 21%.

A diagnostic build ruled out the layer's presentation attributes. Turning off
`toneMapMode`, dropping the dynamic-range and headroom request, and tagging the
layer linear BT.2020 instead of extended sRGB each leave the HDR run between
15.5 and 17.4 fps with the same stalls. What remains is the float drawable
itself and the scaled copy into it.

The build that was already installed on the Apple TV measures 16.3 fps with the
same launch, so these figures are not a regression in the newer build. They are
well below the 27.5 fps recorded for this workload a day earlier with the same
interpreter cost per frame. The display reported an HDR headroom of 4.0 during
these runs; the output mode of the television was not checked and is the first
thing to compare.

### Status

Validation only, for the single-camera non-AR workload. The geometry pass is
attached as the camera's first post-process, and the combine pass binds the
pre-pass depth directly; neither is qualified for rig cameras, packed stereo or
the AR portal, whose default SSAO2 path is unchanged. Edge crawl at the AO
texel scale is inherent to 0.3-scale AO without history and is slightly worse
than the old blur hid it. A short per-eye AO history with sub-texel grid jitter
is the remaining lever if that crawl is visible on device.
