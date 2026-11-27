# Babylon.js Patch Stack

Apps pins Babylon.js 9.36.0. The published package needs no recorder-contract patch:
the published package contains the recorder retention, atomicity, replay and
reporting contract (#19084), and snapshot recorded-draw reuse with the
pre-replay command-stream flush (#19089). It also retains #19076 direct-draw
fallback and the earlier NativeXR, KTX, SSAO2 and font-offset fixes.

`0009-invalidate-only-existing-submeshes.patch` handles a new mesh sharing
geometry with a mesh that already has draw caches. Until positions have been
uploaded, the new mesh can have no submesh list. Updating another attribute
must still invalidate existing submeshes on the other mesh. The original
unguarded loop interrupted portfolio rendering on Quest 1 Vulkan.

`0001-uniform-buffer-comparison-loop.patch` is a separate, API-neutral CPU
optimization: cache the Float32Array length once while comparing uniform-buffer
shadows. Interpreter-only Hill Valley profiling identified repeated comparisons
of unchanged data as a significant cost; no copies, upload rules, numeric
comparisons, draw order or WebGPU features are changed. Seventeen focused tests
cover empty and nonempty buffers, early/late mismatches, subarrays, NaNs and
signed zero. Existing owner-slot and recorded-draw tests must continue to pass.
Device measurements and qualification limits are in
`Documentation/NativeXrPortalProfiling.md`.

`0002-accessor-inlining.patch` preserves the existing type-directed UMD build
transformer from the `accessor-inline-prototype` branch (source commits
`64a99d25f6`, `c518afba76`, `96e541538c`). It replaces eligible trivial getter
reads with backing-field reads, without rewriting setters or write targets.
The maintained profile enables the restricted `all+expr` mode described below.
Analysis excludes known overrides, decorators,
runtime definitions, mixins, structural interfaces, narrowing assertions and
ambiguous sibling members, and audits the other Babylon packages.

This is a fork/application specialization, not a semantics-transparent
optimization for arbitrary third-party JavaScript. An override introduced by
an application after bundle compilation is outside the analyzed program.
Do not claim arbitrary monkey-patching/subclass compatibility or upstream
readiness from the tests below. New application extensions must be included
in the audit before shipping this profile with them.

`0007-accessor-state-expressions.patch` tightens expression substitution before
enabling it in the application profile. A getter expression must evaluate its
receiver exactly once, unconditionally, through a property access. Repeated or
lazy receiver evaluation, optional expression reads, receiver-only getters and
free identifiers are left alone. This preserves null errors, evaluation order,
and caller/getter lexical scope. Reads such as `UniformBuffer.useUbo`,
`UniformBuffer.isSync` and `Material.isFrozen` become current backing-state
expressions, not cached values. Setters and state changes between two draws or
views remain observable.

The patch also contains an independently opt-in static-getter analysis, with
tests for class identity, setters, redefinition, deletion and reassignment.
`accessorStatic` stays false: it did not show a reliable additional device win.
Neither it nor expression inlining removes the whole-program assumptions above.
This is a portable bundler optimization candidate, not a transparent default
for Babylon's public distribution.

The build contract now requires the uniform and material rewrite sites as well
as the prior vector/matrix sites. The differential emitted-bundle tests require
two distinct explicitly selected bundles, so a rebuilt candidate cannot silently
be compared with itself:

    BABYLON_CONTROL_BUNDLE=/path/to/control/babylon.max.js BABYLON_CANDIDATE_BUNDLE=/path/to/candidate/babylon.max.js node --test Apps/scripts/testOptimizedUniformBundle.mjs

Measurements, rejected variants and remaining qualification limits are recorded
in `Documentation/NativeXrPortalBundleOptimization.md`.

`0003-configurable-compression-passes.patch` exposes a validated Terser pass
count. The maintained profile uses three passes with dead-code elimination;
the explicit control uses two. This follows the corrected webapp plugin
(`tools/webpack/babylon-optimization-plugin.js`, merge `077a2f97`), not the
older `tools/plugins/babylon-constfold.js`: hardcoded WebGL2 capabilities,
numeric limits, mutable scene flags and call-dropping logical simplifications
are deliberately not imported. Literal immutable configuration can fold;
runtime capabilities and observable side effects must remain dynamic.

`0004-command-stream-inline-words.patch` reserves command storage once per
accepted draw and writes through a local array/cursor instead of calling two
helpers for each word. Resource validation, retention, atomic rejection/replay,
protocol words and draw ordering are unchanged. The focused differential tests
execute the original and patched TypeScript against the upstream fixture data:

    node --test Apps/scripts/testBabylonCommandEncoding.mjs

This is a browser-compatible source optimization, not a new native API. Moving
camera sampling attributed about 3% of JS samples to the removed word/capacity
helpers. A three-minute M4 pair with identical native binaries and no profilers
measured 78.15 to 81.72 fps, with scene elapsed time reduced by 2.67% and four
pixel-identical fixed-pose captures. This is one ordered pair, not a confidence
interval; a patch-only sustained phone speedup has not been established. See
the November 14 CPU-attribution section of `Documentation/NativeXrPortalProfiling.md`.

## Fast Snapshot Rendering

`0008-fast-snapshot-rendering-group-clears.patch` makes WebGPU fast snapshot
rendering usable when meshes are in more than one rendering group. The
depth/stencil clear before a group ends the current render pass. A snapshot
replays one recorded list of draws per render pass, in order, and in fast mode
the rendering groups are empty, so the clear was skipped: the replayed frame
had one render pass fewer than the recorded one and every later pass replayed
another pass's draws, which fails pipeline validation behind a pre-pass with
several attachments. The rendering manager now records the clear it issues
before each group, per render pass id, and issues it again when the engine
replays the groups. Ordinary rendering is unchanged.

The source patch carries eleven Vitest cases. The installed bundle is checked
by execution:

    node --test Apps/scripts/testBabylonSnapshotGroupClears.mjs

`native_xr_portal_fast_snapshot_validation.js` enables the mode for the static
portal workload. Measurements are in
`Documentation/NativeXrPortalAoStability.md`.

## Shared Material Buffers

`0005-shared-material-uniform-buffers.patch` makes the built-in WGSL PBR
material buffer genuinely material-owned, rather than forwarding its values
into each effect's leftover buffer. The material is shared across draws;
scene/view and mesh data remain separate. Uniform-buffer generations avoid
whole-buffer comparisons when the version is unchanged, and the existing
copy-on-write slots preserve earlier draws when values change within a frame.
Recorded draws additionally check the material GPU buffer identity and protect
it against later writes. No native extension or public JavaScript API is needed.

The internal Babylon WGSL preprocessor accepts `uniform<BufferName>` ownership
annotations and lowers them to ordinary WebGPU uniform-buffer bindings, keeping
the existing `uniforms.member` spelling in shader and plugin code. This is a
Babylon shader annotation, not new WGSL syntax sent to the GPU. PBR's base and
plugin declaration order is checked against its CPU layout. Loose uniforms,
other material types, GLSL, and custom PBR shader resolvers retain their existing
paths. A resolver installed or removed later rebuilds the ownership/layout.

Focused upstream-style Vitest tests are included in the source patch:

    npm --prefix Apps run test:babylonjs:uniforms

Run this after building the maintained bundle. The runner uses the generated
pinned source, not the unrelated dirty Babylon.js checkout. Tests cover 0/1/N
unchanged draws, same-frame A/B/A changes, multiple view matrices and per-view
material values, reuse over many frames, rebuilds, plugin layout, custom-shader
fallback, array padding, and recorded-draw invalidation. Moving-camera device
evidence is recorded in `Documentation/NativeXrPortalProfiling.md`.

## Fork Review Drafts

`0006-uniform-loop-invariant.patch` reads the static matrix-comparison policy
once per `updateUniform` call, rather than once per matrix element. It does not
cache the policy across calls or cache the buffer array, which can be replaced
by copy-on-write during the loop. Fourteen differential tests cover 0/1/N data,
matrix-check toggles, dynamic/static buffers, same-frame changes, independent
views, NaNs, signed zero, creation and errors:

    node --test Apps/scripts/testBabylonUniformLoop.mjs

This is a local profiling candidate, not a demonstrated device speedup. M4 moving-camera A/B/A measured 111.35 / 111.69 / 112.66 fps,
with all four fixed-pose images identical: no gain beyond run-to-run variation.
See `Documentation/NativeXrPortalA15Profiling.md` for validation boundaries.

The retained source patches have independent review branches based on upstream
master `d87d0ca6cf50c6608ec96653ff15418dfeaaae47`:

| Patch | Fork draft | Head |
| --- | --- | --- |
| 0001 comparison length | [#11](https://github.com/rebeckerspecialties/Babylon.js/pull/11) | `6d63a66e35ea` |
| 0002/0003 bundle specialization | [#12](https://github.com/rebeckerspecialties/Babylon.js/pull/12) | `52058e328da4` |
| 0004 command-word encoding | [#13](https://github.com/rebeckerspecialties/Babylon.js/pull/13) | `f068ba2f41df` |
| 0005 shared PBR buffers | [#14](https://github.com/rebeckerspecialties/Babylon.js/pull/14) | `1413360dbeaa` |
| 0006 matrix check policy | [#15](https://github.com/rebeckerspecialties/Babylon.js/pull/15) | `63ac80efa4` |
| 0007 accessor state expressions | [#16](https://github.com/rebeckerspecialties/Babylon.js/pull/16), stacked on #12 | `e0248aeeca` |
| 0008 fast snapshot group clears | [#17](https://github.com/rebeckerspecialties/Babylon.js/pull/17) | `10c4e01a03` |

These are fork drafts, not upstream submissions or merged changes. The bundle
specialization retains the compatibility limitations above. The test runs use
each rebased branch; phone measurements use the maintained 9.36 bundle and must
not be described as runs of newly packaged rebased PR heads. Validation-only
GT-VBAO/F16/color-reuse shader experiments and native scheduling changes are not
mixed into these source PRs. Keep the release-pinned patch series until each
change is actually available in the chosen published release.

## Verified Bundle Build

`bundle-profile.json` pins the release, transform mode, required rewrite sites
and compression passes. The builder archives that exact source revision from
the canonical Babylon.js repository, applies all patches without fuzz, and
builds under `build_babylon_bundle/source`. It does not change or include the
canonical checkout's dirty branch. Build dependencies must already be installed
in that checkout, or supplied explicitly with `--dependencies /path/to/node_modules`.

    node Apps/scripts/buildBabylonBundle.mjs --install
    node Apps/scripts/verifyBabylonBundle.mjs
    npm --prefix Apps run test:babylonjs:optimizations

Generated output includes a receipt with source/patch fingerprints, compiler
versions and bundle/report hashes. Verification checks the report's rewrite
counts and override audit, and executes a compiled method to ensure the actual
bundle is transformed. WebGPU Playground/UnitTests builds run this check every
time. Apple Playground builds also verify the packaged executable JS after
resource copying. Missing, stale, npm-replaced or untransformed bundles fail
the build. The receipt and report are packaged as diagnostics, not source.

To reuse an existing matching artifact without rebuilding it:

    node Apps/scripts/buildBabylonBundle.mjs --reuse --install

An A/B control requires both an explicit control artifact and an explicit CMake
selection. It retains the uniform-buffer patch but disables accessor inlining:

    node Apps/scripts/buildBabylonBundle.mjs --variant control --install
    cmake -S . -B <existing-build> -DBABYLON_NATIVE_BABYLON_BUNDLE_VARIANT=control

Restore the optimized artifact and configure with `...=optimized` after the
comparison. An ordinary npm reinstall may restore the published bundle; the
next native build then fails instead of silently profiling unoptimized JS.
Generated bundles/dependency trees are never committed.

## Release Boundary

- 9.35.0: release commit `00bc37c78dc519e229a709beebd5ed2099e8aba8`,
  published 2026-11-06 19:48:14 UTC. It includes #19076, but not #19084 or #19089.
- #19084: `acf2869e5a4eb6e2e211dd8f35c9656df1c226c1`, merged
  2026-11-07 00:39:25 UTC. Its stream retains pending objects, gates on
  `recordsAtomically`, replays rejected/throwing/missing recorders, reports
  `onDisabled`, and bounds each batch to 1024 draws.
- #19089: `f0a0639c2fdd257d2bdb7001e559f0a2cd5777b4`, merged
  2026-11-10 01:42:01 UTC. Snapshot reuse remains opt-in.
- 9.36.0: release commit `9495e735517f76d0a9a3661089ecf8f8d7e8b653`,
  published 2026-11-12 11:37:48 UTC, contains both fixes in its actual UMD.

Native commit `6ac2ee41d91055639bc0ba5d602a37817f3c591f` removed
`0002-webgpu-render-command-stream-contract.patch` on November 11, before
9.36.0 was published. The former claim that this contract shipped in 9.34/9.35
was incorrect. Upgrade the published package rather than restoring that older
patch over the later upstream fixes.

The 9.36.0 package's source map locates the retention/recovery implementation in
`webgpuRenderPassCommandStream.ts` (lines 308-405, 544-658), and the snapshot
ordering flush in `webgpuEngine.pure.ts` (lines 4400-4445, flush at 4415).
The regression check below executes the compiled UMD, not those mapped sources
or a version-string assertion. It fails against the former 9.35.0 bundle.

    npm --prefix Apps install --ignore-scripts
    npm --prefix Apps run test:babylonjs:bundle

Use `BABYLONJS_BUNDLE=/absolute/path/to/babylon.max.js` to check a separately
extracted package or packaged app asset. This CPU contract check complements,
but does not replace, native GPU tests and screenshot validation.

## Source Patches

The series file remains the explicit list of local source patches. A missing listed
patch, or a patch which neither applies nor passes the reverse check, is still
a hard failure. Apply mode refuses a dirty Babylon.js checkout. Check mode
defaults to the profile's pinned revision; `--ref` tests another revision.
The bundle contract additionally rejects a series without the accessor patch.

    node Apps/scripts/applyBabylonJsPatchStack.js --check
    node Apps/scripts/applyBabylonJsPatchStack.js --apply

Use the published ES2015+ UMD bundles directly. Do not downlevel them or use the
published ES5 bundles on this branch; see AGENTS.md for the texture-layout
failure. Generated bundles are validation inputs and are never source patches.
# Projection-only stereo

`0010-no-mirror-parent-clear.patch` adds an opt-in for the unused parent canvas
clear. Set `xr.camera.skipUnusedParentFramebufferClear = true` only when there is
no canvas mirror or readback consumer. The render loop checks the active cameras,
rig targets, currently bound framebuffer and prepass state at the point of the
clear; adding a spectator restores normal clearing. Eye clears are unchanged.

The pinned-release integration fixture can be run with
`node Apps/scripts/questMenuLadder/spectator.cjs /path/to/babylonjs-9.37.1.tgz`.

`0011-postprocess-camera-output-layer.patch` preserves the camera output target's
array slice on the last post-process. Previously the right eye wrote into layer
zero even though direct scene draws respected the eye's layer. Exercise the
installed bundle with `node --test Apps/scripts/testBabylonPostProcessOutput.mjs`;
compare both eyes on-device using the portal's `ssao2-geometry` control.

`0012-isolate-camera-prepass-presentation.patch` completes the backport of
[Babylon.js #19227](https://github.com/BabylonJS/Babylon.js/pull/19227) at
`f071d213f6`. Sequential cameras clear the shared pre-pass input separately.
Only a ready terminal pass clears the actual camera output; viewport and
array slice are independent. Keep the no-mirror spectator policy from 0010.
For a centered, fixed-pose Quest recording use
`node Apps/scripts/checkQuestPortalEyes.mjs CAPTURE.mp4 --centered` on the
release-test branch: checking bright pixel area alone missed a doubled jamb.
