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
a reference. GPU particle additions disable native readiness renders, matching
the source harness: even rendering with animations disabled advances GPU
particles and changes the simulation state before the requested capture frame.

Run the catalog with both scripts, in this order:

```sh
Playground --save-results true app:///Scripts/validation_dawn_webgpu.js app:///Scripts/validation_native.js
```

Use the existing `--test`/`--test-index` controls for focused runs. Indices in
this catalog differ from the primary catalog. These upstream tests still
fetch scene scripts and assets; packaging the reference images does not make
the suite network-independent.

The full-access M4 Max run on macOS 27 successfully acquires Metal and runs
Playground, the JavaScript unit suite, and all 15 native async/C API tests.
The first imported catalog sweep accepted 74 of 80 tests. Focused follow-ups
accepted the remaining six without relaxing thresholds: three particle tests
with readiness rendering disabled, Mansion and Geometry buffer renderer on
rerun, and Gaussian Splatting PLY SH Order 4 with the identical asset cached.
Those follow-ups do not replace a clean final-revision sweep.

The 24 MB splat PLY took 82 seconds to download, beyond the 30-second scene
load limit. A local cached copy with SHA-256
`7b8902ef5787ffaa40586176ad7db64a4dd8429cf0e50447241302e59106d363`
produced a 32-pixel difference (0.013%). Record any local URL substitution and
asset hash with the run; do not extend timeouts to conceal renderer stalls.
Mansion initially stalled with one material pending, then passed with 173
pixels different (0.072%). Geometry buffer renderer initially exited without
a validation result, then passed with zero differing pixels. Preserve these
intermittent failures and require an explicit validation result, not exit zero.

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
is a failure, not a skip. The broader upstream C-backend suite is still needed
for the remaining native API additions.

For a normal import from a fully fetched Dawn checkout:

```sh
node Apps/scripts/importDawnValidationCatalog.mjs --repo /path/to/Dawn-checkout --ref <full-commit>
```

When using `--config <downloaded-config>` with `--config-revision`, verify
separately that the references at `--ref` match that catalog revision. The
importer records provenance but cannot establish equivalence to a revision
missing from the local object database.
