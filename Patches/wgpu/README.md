# wgpu Shader Patch

The renderer and wgpu-native remain on wgpu 30.0.1, revision
`40f4a34ebaf56f9a046231f54125ad046239d3f3`.
Only Naga and its shared `naga-types` dependency are resolved from a build-local
copy of that revision. Keeping `naga-types` in the same graph avoids duplicate
Rust type identities; it has no implementation changes.

`0002-explicit-shader-workspace.patch` keeps Cargo's inherited package metadata
bound to the wgpu workspace when FetchContent places these path dependencies
inside BabylonNative's workspace. This is build integration, separate from the
upstreamable Metal code-generation fix.

`0001-metal-snorm16-vertex-unpacking.patch` replaces Metal's signed-normalized
16-bit unpack intrinsic in vertex fetch helpers with explicit signed conversion,
division by 32767, and clamping at -1. Bounds checks, packed assets, input
formats, and public WebGPU behavior are unchanged. Existing generated fixtures
cover all vertex widths, offsets, and vertex/instance stepping.

On iPhone XS/A12 the original helper reports 16 bytes of compiler spilling per
`snorm16` input. The replacement reports none in the isolated 12-pipeline test.
The GPU point-grid test checks all 65,536 encodings in four lanes, including
-32768 and 32767; maximum difference from the float32 reference is one rounding
step (5.96e-8). These are code-generation results, not sustained FPS claims.
Scene measurements and qualification limits are in
`Documentation/NativeXrPortalProfiling.md`.

Fork draft: [wgpu #4](https://github.com/rebeckerspecialties/wgpu/pull/4),
`559ca93c45cace95a7ca66c43f87aa049b878448`, rebased onto upstream trunk
`a1d029a731855a6c8d49b717b0601ff3e2f05817`. Naga is in that monorepo;
there is no separate standalone Naga PR. The draft excludes the build-only
workspace patch. Current-trunk M4 vertex-format GPU tests pass. The subgroup
expected-failure mismatch and zero-timestamp failure found in the wider suite
also reproduce on unchanged upstream trunk; they are not hidden or fixed here.
The November 15 A14 recheck also passes the exhaustive encoding probe and
reports zero compiler-spill events across 4,500 scene encoders. Device tests
use this maintained 30.0.1 backport, not a rebuilt current-trunk phone runtime.

CMake pins the source revision, applies the patch without a three-way merge,
and verifies the patched source on every configure, including source overrides.
Cargo's dependency file tracks transitive Rust changes so incremental native
builds cannot silently retain an old shader compiler. Update or remove this
patch explicitly when advancing wgpu; do not point Naga at a different release.

Diagnostic GPU check, separate from performance runs:

```sh
xcrun devicectl device process launch --device <device> --terminate-existing --console com.rebeckerspecialties.BabylonNative.Playground app:///Scripts/webgpu_spill_probe.js
```

The probe deliberately performs one readback for correctness before its tiny
render loop. Ordinary portal runs do not use this probe or add GPU readbacks.

Patch application and pin checks (use the configured build's source directory):

```sh
BABYLON_WGPU_SHADER_SOURCE="$PWD/build_portal_ios_release_lto/_deps/wgpu_shader_codegen-src" node --test Apps/scripts/testWgpuShaderPatch.mjs
node --test Apps/scripts/testRustBuildProfiles.mjs
```
