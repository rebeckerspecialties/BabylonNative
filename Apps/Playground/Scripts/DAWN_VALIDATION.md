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

The source catalog was retrieved at that revision. Reference image bytes came
from the locally available commit `abcfa98d35363303c51f1ffa5d371cb60dc524b3`;
the upstream comparison through the source revision showed no changes to any
of these images. Per-image SHA-256 values and both revisions are recorded in
`config.dawn-webgpu.provenance.json`. No generated renderer output was used as
a reference.

Run the catalog with both scripts, in this order:

```sh
Playground --save-results true app:///Scripts/validation_dawn_webgpu.js app:///Scripts/validation_native.js
```

Use the existing `--test`/`--test-index` controls for focused runs. Indices in
this catalog differ from the primary catalog. These upstream tests still
fetch scene scripts and assets; packaging the reference images does not make
the suite network-independent.

The import was syntax-checked and the references hash-verified. Playground,
UnitTests, and NativeWebGPUAsyncTests built against the feature-only
wgpu-native checkout, but no visual result is accepted: the execution
environment exposed no Metal adapter to the test binaries, and Playground
aborted during macOS application registration before the scripts ran.

For a normal import from a fully fetched Dawn checkout:

```sh
node Apps/scripts/importDawnValidationCatalog.mjs --repo /path/to/Dawn-checkout --ref <full-commit>
```

When using `--config <downloaded-config>` with `--config-revision`, verify
separately that the references at `--ref` match that catalog revision. The
importer records provenance but cannot establish equivalence to a revision
missing from the local object database.
