# Uniform/material bundle optimization

Work date: 2026-11-16. No PR, commit or push was made. AR remains disabled;
the HDR moving-camera workload retains GT-VBAO slab-v4, F16 material blocks-v3,
world-space normals, R8 AO and shared pre-pass/color reuse.

Further negative compiler experiments and A14 efficiency-core PMU A/B/A are in
[Spatial measurements](NativeXrPortalSpatialMeasurements.md). They retain the
existing inlining, but do not establish the requested additional 5% CPU reduction.

## Implementation and safety boundary

The maintained stack adds `0007-accessor-state-expressions.patch` and selects
`all+expr`, with static accessor rewriting disabled. Its 9,833 rewritten sites
include 18 `UniformBuffer.useUbo`, 11 `UniformBuffer.isSync` and 36
`Material.isFrozen` reads. These remain live reads at each call site; there is
no per-frame, per-material or per-view result cache. The pass is TypeScript/
bundler-level, with no NativeWebGPU or renderer-specific hook.

The extended mode needed correctness restrictions: one unconditionally
dereferenced receiver, no duplicated/lazy receiver, no caller-captured free
identifier, and no optional expression substitution. Regression tests cover
receiver side effects, null access, reassigned caller bindings, setter/override
dispatch, zero/one/many iterations, same-frame writes and independent views.
Emitted UMD tests also exercise real UniformBuffer and PBRMaterial instances.
They snapshot the uploaded values at each simulated draw and assert independent
expected material/view values, rather than comparing mutable buffer references
only after the sequence ends.

This remains an explicitly selected whole-program optimization. Unseen runtime
monkey-patching or application subclasses are not covered by a closed-world
source audit. Public getter/setter definitions are retained, but callers compiled
to backing reads cannot observe arbitrary later accessor replacement. This
limitation predates this change and must not be described as universal upstream
API compatibility. The static mode is separately opt-in and disabled here.

The build receipt verifies source revision, every patch, bundle hashes, mode,
external override audit, and required uniform/material rewrite sites. Missing
transformation fails the build. The fresh maintained build and its 119 focused
optimization tests passed. Differential tests require explicit distinct bundles.
The final prototype-deletion audit rebuild emits the exact same executable
bundle as the device/screenshot candidate (SHA-256
`1b0a49e25149a876cee915f41b12387b5b438451faf61b7b62e9cf64e15f655d`),
so the added compiler regression case does not invalidate those runs.
The adjacent shared-uniform/command-cache Vitest sweep also passed all 201
tests across 11 files. Forty portal/helper scripts passed `node --check`.
The emitted-bundle differential suite passed 14 tests; the sampling helper and
its resource-packaging check passed eight. That is 342 focused passing tests.

## Measurement method

- A10X Apple TV: 2560x1440 HDR; A12 iPhone XS Max: 2689x1242 landscape HDR,
  25% brightness. Do not compare these with older portrait/SDR measurements.
- Same native Release/LTO executable logic, public system JavaScriptCore,
  interpreter-only policy, camera path, scene, shaders, resolution and settings.
- Four fixed-pose captures precede warmup and a separate 45-second timing
  interval. Motion receipts show changing camera angles and view matrices.
- No per-call probes. `TestUtils.getCurrentThreadCpuTime()` uses Mach cumulative
  user+system time only at timing boundaries and five-second motion receipts.
  It excludes blocked presentation waits. It includes native functions called
  on the JS thread and does not include separate JSC GC threads, so it is not
  an exclusive all-process JSC counter.
- A valid 20-second A12 Time Profiler capture attributes 82.45% of process
  running sample weight to JSC, with property lookup and LLInt dispatch
  prominent. An A10X trace disconnected at 1.18 seconds and is not used as
  performance evidence. No PMU/cache-miss reduction is claimed.
- Nominal/fair A12 runs are retained separately from serious-thermal runs.
  Thermal categories alone are not proof of identical CPU/GPU clocks.

## Candidate ledger

CPU below is actual JS-thread execution milliseconds divided by rendered frames,
not the scene instrumentation's elapsed render time. Each row is a single
ordered run, not a statistical confidence interval.

| Candidate | A10X fps | A10X CPU ms/frame | A12 fps | A12 CPU ms/frame |
| --- | ---: | ---: | ---: | ---: |
| Existing maintained control | 28.206 | 30.528 | 58.526 | 14.835 |
| Guarded constant-argument method specialization | 28.005 | 30.675 | excluded | serious thermal |
| Specialization + four-element unrolling | 28.092 | 30.481 | 58.327 | 14.820 |
| Guarded expression methods + specialization/unrolling | 28.007 | 30.746 | not run | not run |
| Restricted accessor expressions G | 28.198 | 30.101 | 59.084 | 14.351 |
| Expressions + static flags H | 28.218 | 30.176 | 57.896 | 14.765 |
| H + lexical-constant loop unswitching I | 28.275 | 30.112 | 59.339 | 14.136 |
| I + guarded 16-element specialization J | 27.899 | 30.277 | not run | not run |
| Repeated existing control | 27.921 | 30.582 | excluded | serious thermal |
| Final maintained expression bundle K | 27.761 | 30.419 | 58.993 | 14.426 |

The accessor result is a small improvement, not the requested demonstrated 5%
reduction on both devices. Guarded call expansion, loop cloning and larger
unrolling do not establish that target and are not in the maintained stack.
Experiment source, compiled bundles, reports and negative results are retained
under the ignored `build_portal_optimizer_20261116` directory.

The final paired A10X reduction is only 0.54% versus the immediately preceding
control (0.36% versus the initial control), within the scale of run variation.
A12 is 2.76% below the initial matched nominal/fair control. Four fixed-pose
captures on each device are pixel-identical to control, including materials and
under-car AO. The requested 5% minimum is **not achieved**. These short runs
also do not establish a new sustained thermal/FPS milestone.
The final and initial-control runs both submit once per rendered frame. Draws
per frame remain approximately 69.1 on A10X and 72.0 on A12; the optimizer did
not obtain its result by removing meshes, passes or effects.

### Final A10X source attribution

A separate source-sampling run used the final bundle at 2560x1440 HDR, with
the same moving camera and nominal thermal state throughout. It collected
14,011 leaf samples over 20.016 seconds: 11,227 LLInt, 2,526 host, and 258
unknown-executable. Source mapping uses the exact installed bundle's map;
retained emitted-source excerpts verify the mapped expressions because
transformed `sourcesContent` lines are not always aligned with original lines.

| Source/method | Leaf samples | Share of samples |
| --- | ---: | ---: |
| PBR `bindForSubMesh` | 775 | 5.53% |
| Uniform buffer `updateUniform` | 563 | 4.02% |
| Mesh `render` | 364 | 2.60% |
| PBR `_prepareDefines` | 337 | 2.41% |
| PBR `isReadyForSubMesh` | 308 | 2.20% |
| Command stream `tryAppend` | 261 | 1.86% |
| Uniform buffer `_checkNewFrame` | 200 | 1.43% |

Sampled sites still include `_uniformLocations[name]`, uniform element
comparisons, pipeline-tree lookups, material define writes, and transform dirty
flags. These are not equivalent to invariant lexical expressions. A compiler
cannot cache the dirty flags across calls/views or keep a stale `_bufferData`
reference across copy-on-write. The next CPU candidate needs a narrower
alias/effect proof or explicit state/version invalidation, not unrestricted
property-hoisting or expanding more method bodies. The 5% gate remains open.

The first diagnostic attempt collected no samples because the helper was
missing from the generated app. It is retained as incomplete and excluded.
The helper is now in the Playground resource list, a regression test checks
that list, and the diagnostic runner requires both start and completed-stop
receipts. The retry completed and exited zero. Its FPS/memory are diagnostic
only, not additional uninstrumented performance results. Artifacts are
`a10x-final-source-diagnostic-retry.*`, `a10x-final-jsc-stacks.json`,
`a10x-final-jsc-hot-sites.json`, and `a10x-final-jsc-verified-sites.json` under
`build_portal_optimizer_20261116`.

## Screenshot audit

The unchanged baseline ran 831 tests: 713 passed, 118 failed, 64 were skipped.
The first accessor-expression sweep retained 712 passes, with one additional
failure: GPU Particles - Emitters - Directed Cylinder. The exact original bundle
reproduces that failure at 6,492 pixels when readiness advances four frames;
three readiness frames produce 5,881 pixels and pass the unchanged 2.5%
threshold. The final candidate also passed that three-frame case. This is
timing-sensitive fixture evidence, not permission to loosen the threshold.

The final maintained-bundle sweep ran the same 713 previously passing cases:
710 passed and three failed. All three failures reproduced on the exact original
control bundle, with byte-identical output images when the readiness frame count
matched. Each also passed on the final bundle with the original three-frame
readiness state:

| Fixture | Three readiness frames | Four readiness frames |
| --- | ---: | ---: |
| Color Gradient With Color2 | 5,701 pixels, pass | 6,040 pixels, fail |
| Directed Cylinder | 5,881 pixels, pass | 6,492 pixels, fail |
| Cone | 5,353 pixels, pass | 6,025 pixels, fail |

The threshold remains 2.5% of 240,000 pixels. The render pump advances GPU
particles even with `ignoreAnimations`, so an asynchronous fourth preparation
frame changes the tested simulation state. This is a separately reproducible
harness issue, not an established optimizer rendering regression. It still
prevents claiming a clean 713/713 full sweep. No screenshot reference, tolerance,
readiness policy or simulation frame count was changed to make these tests pass.
The original and candidate runs, images and logs are retained under
`build_portal_optimizer_20261116/{baseline,final-results,particles-control-repeat,particles-final-repeat,particles-color-control-repeat}`.

Upstream audit used BabylonNative master `432810cbad2d38b0266abe91448f10154e26e466`,
the `nativeDawn` branch `64e97e3e`, and Babylon.js master `44afd64385`.
The 118 baseline failures have no changed reference-image blobs in the checked
Native/main or NativeDawn heads. Several Babylon.js catalog entries now differ
in playground revision, render count or explicit WebGPU exclusions; the full
structured comparison is in `baseline/fixture-audit.json`. Examples include
glTF line-loop/triangle-fan primitive modes, shadow-only materials, selected
motion-blur and fluid cases. Those differences are not optimizer regressions.
All three particle playgrounds and references are unchanged in all three
catalogs; upstream did not provide corrected fixtures for this readiness drift.

There are relevant newer implementation fixes, not just catalog differences:
Babylon.js #19171/#19172 repair node-material WGSL view/reflection references,
and #19173 repairs shadow-wrapper WGSL normal bias. The older shadow-wrapper
fixture in this baseline instead fails through GLSL/glslang preparation, so
that fix is not automatically a remedy for its unsupported custom GLSL.
BabylonNative #2083 corrects premultiplied-alpha screenshot compositing, and
#2084 plus Babylon.js #19150 add integer-attachment capability reporting to
NativeEngine. These should be qualified in a separate integration run; they
were not mixed into the optimizer A/B baseline or asserted to fix all failures.

AO research and the stereo-aware experiment order are in
`NativeXrPortalAoStability.md`. No AO shader change is mixed into these CPU tests.
That investigation found the configured soften/tolerance/sample-count controls
are unused with the demo's selected legacy blur. Test that depth-edge filter
separately from the noise sequence before adding temporal history surfaces.
