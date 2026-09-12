# Imported Dawn validation additions

`config.dawn-webgpu.json` contains tests whose titles were absent from this
fork's primary native catalog. It is a separate catalog so established native
thresholds and regression policies are not overwritten.

Source: CedricGuillemet/BabylonNative `nativeDawn`, commit
`3263c363a4b86d56ce02a74ec140352bd3a8bced`.

- 139 additional tests, of which 80 are enabled for WebGPU by source policy.
- 117 unique original reference images, prefixed `dawn-` to prevent collisions.
- 20 upstream-excluded entries have no reference image and remain excluded.
- Source thresholds and exclusions are retained without relaxation.

The source catalog and reference image bytes come directly from that fetched
revision. Per-image SHA-256 values are recorded in
`config.dawn-webgpu.provenance.json`. No generated renderer output was used as
a reference. GPU particle additions and SpaceDeK disable native readiness
renders, matching the source harness: even rendering with animations disabled
advances particles and changes simulation state before the requested frame.

Run the catalog with both scripts, in this order:

```sh
Playground --save-results true app:///Scripts/validation_dawn_webgpu.js app:///Scripts/validation_native.js
```

Use the existing `--test`/`--test-index` controls for focused runs. Indices in
this catalog differ from the primary catalog. These upstream tests still
fetch scene scripts and assets; packaging the reference images does not make
the suite network-independent.

## M4 validation and harness fixes

The full-access M4 Max run on macOS 27.0 (26A428), Xcode 27.0 (27A266a)
successfully acquires Metal. The build uses Debug, JavaScriptCore, and
Babylon.js 9.22.1; these are correctness results, not performance evidence.
All 15 native async/direct C API tests and the five JavaScript unit tests pass.

The canonical `wgpu` sweep at commit `369e8b7e` accepted 80/80 eligible Dawn
additions and 10/10 native smoke cases. The run includes the preserved local
Android/OpenXR/visionOS changes, identified by the recorded dirty-source hash.
It also passed 15 native tests, five JavaScript tests, and eight harness tests.
Runtime `01a4f43` and dependent tests `a105c5a` were used. Evidence, source and
binary hashes, cached-input hashes and all 90 result images are under
`build_wgpu_pr_stack/m4-validation/2026-09-12T02-20-49.394Z/`.
No thresholds or reference images were changed.

The earlier isolated sweep at integration code commit `551b76ed` accepted
80/80 eligible Dawn additions and 10/10 native smoke cases with no threshold
relaxation, regenerated references, device-loss markers, or render panics.
It is recorded under
`build_wgpu_pr_stack/m4-validation/2026-09-12T01-27-05.931Z/` in the canonical
workspace, including source/binary/bundle hashes and all 90 result images.
The runtime head is `4032856` and the dependent test head is `61432ec`.
The surface lifetime fix and canonical integration were subsequently validated
in the newer sweep above.

The preceding sweep at `da6b12cb` accepted 80/80 Dawn additions and 9/10 native
cases. Its Simple refraction failure was traced to readiness renders advancing
the snippet's `registerBeforeRender` rotation callback. Commit `551b76ed`
disables the pump for that fixture; the final image is pixel-exact.

Earlier failed sweeps and diagnostic logs remain in that workspace. Three
harness defects found during this validation are now covered by tests:

- Readiness renders must call WebGPU `beginFrame` and `endFrame`. Otherwise
  reflection-probe passes and uploads accumulate until the first validation
  submit, causing Metal device loss. Unexpected device loss now logs at its
  origin, and the driver rejects such a run even if it reports a screenshot.
- The validation harness owns the readiness deadline. Changing Babylon's
  already-running readiness timeout to 30 seconds could clear its callbacks
  early; the harness now disables that competing timer while retaining its
  own unchanged 30-second limit.
- Pending scene promises stay separate from the active scene. Late results
  are disposed instead of replacing a subsequent test, including repeated
  runs of the same entry. Failure cleanup never calls `dispose` on a promise.

Run all eight focused regressions from the repository root:

```sh
node --test Apps/scripts/testReadinessFrameBoundaries.mjs Apps/scripts/testValidationSceneLifetime.mjs
```

These fixes require no Babylon.js source patch. Animation-ignore flags do not
suppress render observers or particles, so state-sensitive fixtures must
explicitly opt out of readiness rendering. Do not globally disable the pump:
some material paths need renders to finish effect preparation.

The 24 MB splat PLY took 82 seconds to download, beyond the 30-second scene
load limit. A local cached copy with SHA-256
`7b8902ef5787ffaa40586176ad7db64a4dd8429cf0e50447241302e59106d363`
produced a 32-pixel difference (0.013%). Record any local URL substitution and
asset hash with the run; do not extend timeouts to conceal renderer stalls.
Mansion, Sponza, Flat2009, and Espilit also use exact-byte scene caches in the
local sweep. The URL/hash manifests accompany each run; other assets still
use the network. Preserve failed runs and require an explicit validation
result plus a successful exit, not exit zero alone. A clamshell-sleep timeout
is not a renderer result; keep the host awake and the lid open during a sweep.

The macOS bundle template now uses CMake substitutions, so Ninja bundles no
longer ship unresolved Xcode executable or bundle-identifier placeholders.
The previous application-registration and zero-adapter failures were limited
to the restricted execution environment, not this machine's GPU capability.

Before claiming an integrated runtime build, verify that changes in the
wgpu-native checkout trigger the Rust build and match the staged rlib source.
CMake now tracks these inputs and uses a Cargo `[patch]`, not a graph-changing
`paths` override. A dependency input timestamp change was verified to trigger
configuration, snapshot refresh, and the Cargo command.

The rendering bridge currently uses Rust wgpu APIs. Linking wgpu-native and
calling its version function alone is not C API behavioral coverage. Run
`NativeWebGPUAsyncTests --gtest_filter=NativeWebGPUCAPI.*` as well: this direct
C API integration test requires an adapter, uploads/copies/reads back a GPU
buffer, and checks adapter-info and buffer map-state APIs. Missing hardware
is a failure, not a skip. The broader upstream C-backend suite separately
exercises the feature-only wgpu-native checkout. On this host its current-head
run passed 1002/1003 tests with eight documented exclusions. The remaining
written-timestamp test failed intermittently through both C dispatch and
direct Rust wgpu at the same pin (4/10 repetitions in each), so this is not
an unconditional passing suite. Two further full sweeps at runtime `01a4f43`
again passed 1002/1003, with only the written-timestamp failure. A raw Metal
reproducer now isolates GPU counter resolution returning zero even when CPU
resolution after completion reads valid samples; no CPU-wait workaround was
added. The earlier nextest process/pipe-lifetime LEAK warning did not recur in
those sweeps or 40 focused C/direct-Rust repetitions, but its cause is not
established. Neither issue is hidden by the screenshots.

These results are not WebGPU conformance certification. The test total includes
CPU tests, unsupported features and expected failures; the last C-backend sweep
has 325 cases labeled `Executed` on Metal, of which 324 passed. Noop-only Rust
validation tests fall back to wgpu-core. Separately, 12 public C API surface
tests pass against the feature-only runtime. See
`Patches/wgpu-native/API_COVERAGE.md` for the remaining standard C API contracts
and the distinction between this stack's feature scope and full API parity.

For a normal import from a fully fetched Dawn checkout:

```sh
node Apps/scripts/importDawnValidationCatalog.mjs --repo /path/to/Dawn-checkout --ref <full-commit>
```

When using `--config <downloaded-config>` with `--config-revision`, verify
separately that the references at `--ref` match that catalog revision. The
importer records provenance but cannot establish equivalence to a revision
missing from the local object database.
