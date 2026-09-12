# Reviewed wgpu-native Runtime

The public dependency pin remains
`c8df6cb99795a4cab585355582ce53e8111c9c0d`. The replacement runtime and test
PRs have not been published. This generated patch makes ordinary BabylonNative
builds use the reviewed runtime without referencing an unreachable remote SHA.

The patch projects the runtime files of `takeover/parity` at
`01a4f434a7bcc9d0909a8320a2c297a6e22ea1bc` onto that public pin. It includes the
compatible dependency refresh from upstream trunk
`85389b2bb523a0106cbdc44a02b09ccefe393040`, but leaves the wgpu core/hal pin at
`1a3a4cc27d8082dfd07beaf76dcf3dfb78dda321`. No replacement C-backend tests or CI
changes are bundled. Existing test files in the old public snapshot are not
the current test stack and should not be used to validate these fixes.

The original parity implementation remains attributed to Inner-Daemons
(`magnus.larsson.mn@gmail.com`), commit
`9b52924948ddfb9a0c23642099072debbd179264`. Review fixes and the new runtime
corrections are by Matt Hargett (`plaztiksyke@gmail.com`). Original commits,
author dates and source-commit trailers remain in the split branches;
`build_wgpu_pr_stack/attribution.json` and `verify-stack.mjs` verify them.
The upstream lockfile refresh is by renovate[bot], co-authored by Connor
Fitzgerald, as recorded in upstream commit `85389b2`.

Runtime corrections include initialized native limits, external-texture
layouts, capability mappings, adapter-info output-chain ownership, polling
ABI consistency and surface lifetime handling. Surface cleanup now matches the
acquired texture ID, retains the surface while texture handles exist, and
serializes acquire/configure/present/discard/release. A retained older frame
must never discard the current one.

## Regeneration

From the split wgpu-native repository, generate the patch with:

```sh
git diff --binary --full-index c8df6cb99795a4cab585355582ce53e8111c9c0d 01a4f434a7bcc9d0909a8320a2c297a6e22ea1bc -- CHANGELOG.md Cargo.toml Cargo.lock deny.toml build.rs ffi src
```

Apply to a clean checkout of the public pin using
`Patches/apply_patch_if_needed.cmake`, apply again to verify idempotence, and
compare the listed paths byte-for-byte with the feature-only branch. CMake's
`FETCHCONTENT_SOURCE_DIR_WGPU_NATIVE` override bypasses download/patch steps;
use the already-correct feature branch for that override, never the unpatched
public snapshot. Once the reviewed runtime is published, replace the public
pin and remove this patch only after verifying the same source projection.

## Acceptance Boundaries

The separate test branch has 12 public-C-API Metal surface cases with actual
render/submit/present and registry lifetime checks. They pass against the
feature-only runtime on the M4 Max. They do not prove complete backend
unconfiguration memory release: the pinned wgpu-core exposes no standalone
surface-unconfigure operation.

The remaining intermittent timestamp failure reproduces without wgpu in a
raw Metal program on macOS 27. GPU counter resolution can return zero while a
CPU resolution after completion reads valid samples. Fences, events and a
second queued command buffer do not fix it; waiting for sampling completion
before encoding resolution does. No CPU-wait workaround or weakened assertion
is included. Keep this investigation separate from surface correctness and
from iOS performance acceptance.
