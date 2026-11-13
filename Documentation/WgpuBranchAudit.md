# Monorepo Branch Recovery Audit

November 12, 2026. Started at `96f2c03f` on canonical `wgpu`.
The standalone JsRuntimeHost and UrlLib repositories are provenance/review
repositories; production code is vendored under `Dependencies/` here.
Commit reachability and patch identity were only the first pass. The decisions
below compare source behavior, including changes reshaped by the monorepo move.

## Findings

- A real Blob copy-reuse regression was found: `CopyBytes()` allocated and
  flattened every extraction. The old stream branch retained immutable storage
  and cached flattened slices/multipart Blobs. This affects loading and object
  URLs; it is not evidence of a per-frame GPU copy or a measured FPS gain.
  Whole-segment Blob/object-URL reads now share immutable storage. Writable JS
  outputs still copy. On JavaScriptCore, BYOB reads use a bounded scratch buffer
  before `TypedArray.set`: taking the request buffer's native pointer pins it
  and prevents the stream's required detachment. Regression tests caught that
  failure during this recovery; the unsafe direct-pointer variant was rejected.
- The Babylon.js command-stream retention/replay patch was removed before its
  published release. 9.35.0 still dropped rejected batches, did not retain their
  resources, and lacked disable reporting/bounded batches. 9.36.0 contains the
  actual upstream fixes. Ten tests execute the installed UMD contract, rather
  than inferring it from a version number or source checkout.
- Native buffer-write reuse, external-image borrowing/reuse, batched queue
  submit, compressed texture upload, and Metal texture import functions match
  all three September stack tips. The CanvasWgpu tree is also identical.
- SSAO2 world-space-normal conversion/normalization remains in the published
  package. A12 memoryless depth remains intentionally disabled because views
  of those textures failed on that device. No lost f16/subgroup SSAO shader
  optimization was found in the audited stack.

## Recovered Commits

Each recovery is a discrete commit on `wgpu`, not a wholesale merge of the old
dependency pins. Newer monorepo behavior was retained.

| Recovery | Commit | Provenance |
| --- | --- | --- |
| CanvasWgpu snapshots for XR image tracking | `49f886ab` | `c1fd7db6` |
| Explicit JSC interpreter-only Playground mode | `46edcb54` | `befeb064` |
| Saved Canvas gradient collection-pressure test | `1ec64598` | `207b1737` |
| Use host Blob/object URLs in screenshot validation | `744c5848` | `4468e05e` |
| Use runtime image bitmaps in screenshot validation | `fb7fbfc7` | JS portion of `8d4d72ad` |
| Monorepo Windows component crash artifacts | `8bdf0b88` | UrlLib `512e0882`, imported `96ee9b9f`, lost in `da9131c2` |
| Stable radix splat sorting and WebGPU test registration | `c65b1046` | Reviewed `549d499a`, not the superseded handwritten radix implementation |
| Blob identity and writable URL operations | `f85df4c5` | JsRuntimeHost `cf3904ad`, `a5c341dd`, `78a9a7fa` |
| Per-environment monotonic performance clocks | `cfbbd771` | JsRuntimeHost `1ff52630`, `5c6cd00c` |
| Parent scheduler ownership during worker teardown | `e4661273` | JsRuntimeHost `177b8325` |
| Desktop test argument forwarding | `95dd81ee` | JsRuntimeHost `a6c56b80` |
| Desktop-only WebGPU test build guard | `f8ee6eb8` | `4b58d758`, extended to tvOS and 18 condition-matrix tests |
| Published recorder contract via Babylon.js 9.36 | `3dcf0889` | Upstream #19084/#19089; see `Patches/BabylonJS/README.md` |
| Data URL asset/texture integration tests | `46be9dbb` | `bfc4464e`, moved from generated output to maintained source |
| Compression output and FileReader callback lifetimes | `976b5c91` | JsRuntimeHost `c3893a42`, `b3978622`, `c712eda5`, `51cebc83`, `bead2d63` |
| JSC cached intrinsics, runtime type probes, references and registry ownership | `5c68a071` | JsRuntimeHost `f8c8335`, `9a9e06c`, `2503f2`, `cefd4561`, `f8e8152`; retain newer detached-buffer metadata and strict receiver handling |
| Drain child-process output before waiting | `35bac055` | JsRuntimeHost `9de995b`; three bounded regression cases |
| Shared Blob storage and reusable stream callbacks | `ea83ae76` | JsRuntimeHost `226ed90b`, `1da1abbf`, `760ae3cd`; additional JSC BYOB detachment coverage |
| Shared data URL decoder for Fetch and URL consumers | `c17c1fc8` | JsRuntimeHost `f30a41c6303d`; percent/base64, header, malformed-input and asset-load tests |
| Arbitrary abort-reason lifetime and identity | `e1be115f` | JsRuntimeHost `263980909150`, `078d629b92de`; explicit null and inherited-accessor regressions |

## September Native Stack

Archive tips preserve the original history: `b8b35d4e` (integration),
`88b82e2e` (Canvas/lifetimes), and `0d0f9f90` (XR audit).

| Source commit(s) | Disposition |
| --- | --- |
| `bb2ac236` | Dependency integration superseded by current monorepo and published runtime pins; runtime contents audited separately below. |
| `bfc4464e` | Recovered as source integration tests. |
| `b967ed32` | Archive with optional Bun JSC/ThinLTO work; no Bun consumer exists in the current monorepo. Forwarding its option alone would do nothing. |
| `4468e05e`, `8d4d72ad` | Host Blob/image bitmap paths recovered; obsolete runtime pin and duplicated scheduler changes omitted. |
| `851873f8` | Canvas work already present through `64f0683a` (#1855). |
| `3119748d` | Vulkan boundary coverage already present through `4af724a6` (#1859). |
| `9f06f43d`, `945f271a` | Upstream-equivalent framebuffer texture binding and queued instance data fixes already present. |
| `f57aaa9b`, `db9ae269`, `449099b6` | NativeXR owned image/callback state and tests already present through `95f29ce1`; retain newer width validation. |
| `c1fd7db6` | CanvasWgpu image snapshot adapter genuinely missing; recovered. |
| `f889384f`, `f81140d8` | Gradient runtime ownership already present. |
| `207b1737` | Additional saved-gradient GC regression recovered. |
| `d5ec0bf1` | Teardown-safe WebGPU scheduling already present through `8b13e86f`. |
| `29c35f32`, `f7ee435b` | Obsolete cstdlib dependency patch removal superseded by vendored source. |
| `4b58d758` | Apple mobile exclusion genuinely missing; recovered with platform tests. |
| `6ca3bb49` | Superseded by reviewed `bx::radixSort` implementation `549d499a`, which was recovered instead. |
| `a7d455cf` | Old 9.26 package pin superseded by verified 9.36. |
| `befeb064` | Interpreter-only mode recovered. |
| `a5364997` | Main/launch storyboard keys already present in current iOS plist. |
| `b8b35d4e` | Launch instructions superseded by current documented device commands. |

The remaining Native commits are dependency-pin changes, not standalone native
implementations: `4b679b5c`, `33c4b06e`, `d501d8c7`, `1258f1e7`,
`4330db61`, `bf2efd6e`, `b7fea07e`, `7df90ae7`, `387da15c`,
`95422ba4`, `805f7a0d`, `d956222e`, `550822d7`, `18862661`,
`88b82e2e`. Their relevant runtime changes are tracked individually, rather
than declaring a pin covered merely because a newer dependency was imported.

## Standalone Runtime Disposition

Already integrated: scheduler lifetime (`5f246d73` -> `8b13e86f`), the newer
Apple outer dispatch-loop pump (`d60d12b1`), V8 deferred finalizers (`033d42d5`),
detached-buffer wrapper metadata refresh and registry mutex protection
(`dcb2e355`). Do not reintroduce the old per-callback CFRunLoop pump.

The following remain preserved research, not production recovery candidates
without further qualification:

| Source | Reason not merged wholesale |
| --- | --- |
| Bun `af7e60eb`: `86bd72e4`, `b42a8b5d`, `3852b20f`, `a7ae6172`, `f5fe5b11`, `4b9ef41b`; Native `b967ed32` | Optional Linux/Android JSC backend and LLVM-version/static-link constraints need platform qualification. Keep system JSC selection available. No Apple FPS claim. |
| `ca88eb09` | JSC watchdog rearming needs termination tests and separation from private Apple API enablement. |
| `9924974f`, `6e094faf`, `c502dcb2`, `997b37a6` | Android JSC revision, QuickJS stack and cross-engine type-tag experiments need their target engines/devices. |
| `28c198f3` | Response disturbance proposal wraps host stream methods. Probes found rejected cancellation of a locked stream incorrectly marks `bodyUsed`, borrowed reader methods mark the wrong stream, and prototype calls bypass tracking. The current host-stream interoperability gap remains, but this proposal is not a safe recovery. |
| `ad09b33a` | Replacing a partial host Streams suite changes host globals; qualify that policy together with Response/Streams interoperability. |
| `cd581b4b`, `84f8836e`, `d0dd17e4` | Hermes source-URL overload and JSI Eval exception conversion require matching engine builds; host-function exception propagation is already integrated separately. |

The standalone canonical JsRuntimeHost checkout's uncommitted Android loading
experiments remain untouched. They are not dependencies of Apple builds.

A separate adapter follow-up remains: JavaScriptCore's `napi_define_properties`
builds ordinary descriptor objects, which are sensitive to inherited property
accessors. Its descriptor attribute mapping also needs a focused audit. The
abort-reason recovery avoids that path and tests prototype interference; it
does not claim to fix all descriptor operations.

## UrlLib

No lost September transport implementation was found. Transport-error work
`e86ffb34` is represented by `c3a70ed2`; abort `5683cc4d` by `8c4e72e6`;
Win32 errors `425c9f10` by `94aeffb1`; shared response storage `0c991337`
by `6bf23f20`. Current Android UTF-8, response/header/redirect fixes remain.
HTTP/3 remains opt-in through `999f4c3f`: restoring an unconditional old
attempt/pin would undo that measured decision. The Windows crash-dump setup
was the actual missing CI piece and was recovered separately.

## Archive Policy

Removed branch names have exact-tip tags under `archive/2026-11-12/` in their
original repository. An archive tag means preserved history, not a claim that
every experimental change was merged or validated. Active upstream review
branches and the independent NativeDawn renderer remain separate.

All registered auxiliary worktrees in these three canonical repositories were
removed. Two dirty worktrees were preserved first:

- BabylonNative `archive/2026-11-12/bench-test` (`5fc50259`): six benchmark,
  timing and deferred-disposal files, intentionally not production changes.
- JsRuntimeHost `archive/2026-11-12/agent-validate-bun-jsc-napi-v7`
  (`55fc796`): five Android runtime/test configuration experiments.

Clean Android/Bun worktrees contained generated test assets and build caches,
not additional untracked source. The canonical dirty JsRuntimeHost checkout
and any separate durable integration repositories were not removed.

The cleanup removed 69 local branch names (31 BabylonNative, 33 JsRuntimeHost,
5 UrlLib), six auxiliary worktree directories, and one already-missing UrlLib
worktree registration. The removed directories occupied about 1.4 GiB before
removal; this is not a claim about net free-space change while builds ran.
All 74 archive tags were published to the corresponding forks before remote
branch retirement. Different local/remote tips have separate tags.

Twenty obsolete remote branches were retired with exact-tip leases and
published-tag checks: 14 in BabylonNative, three runtime integration stacks in
JsRuntimeHost, and three UrlLib integration/CI/transport branches. Superseded
fork PRs BabylonNative #1, #2, #4, #5 and JsRuntimeHost #22 were closed with
archive and recovery links. Individual upstream review branches/PRs were not
closed or rewritten. The canonical `wgpu` recovery commits were published
before those old integration branches were removed.

Older remote rebase/merge snapshots are historical backups, not new production
patches. The October merge is already an ancestor of `wgpu`; the July snapshot's
image encoding, splat API, and buffer readback are superseded by current code.
The distinct screenshot-review tip uses the same canonical playground revisions;
do not restore its old bgfx/Metal PBR exclusion or loosen the current Slate
threshold. Current PBR/Slate screenshots pass without those accommodations.

## Validation Boundary

Final recovered source head: `e1be115f`. Both builds use the system
JavaScriptCore framework and Release configuration. These existing integration
build directories have LTO disabled and are not performance baselines.

| Check | Result |
| --- | --- |
| Runtime unit suite | 90 native tests passed, including 426 JavaScript cases |
| Node-API executable suite | 16 passed; one existing `test_reference_double_free/test_wrap.js` quarantine remains skipped |
| Child-process output tests | 3 passed; earlier stress repeated all three 20 times |
| UrlLib | 33 passed |
| NativeWebGPU async/ownership/Metal interop | 21 passed |
| Native integration under `JSC_collectContinuously=true` | 23 passed, including 55 WebGPU JavaScript cases and XR compositor/image ownership |
| Patch-stack, readiness, scene lifetime, installed UMD and platform guards | 37 Node tests passed |
| M4 Metal screenshots | 10 passed with unchanged thresholds: GUI3D SpherePanel, GUI Slate, both SOGS SH variants, Simple/PBR refraction, Standard/PBR texture repetition, Prepass SSAO + instances, FAST snapshot CPU particles |
| Command recording in that screenshot run | 231 batches, 396565 words, zero disable events |
| iOS Playground | Release build succeeded; strict signature verification passed; bundled Babylon.js is 9.36.0 |

Compression/FileReader, data URL, Blob and JSC adapter tests exposed failures
before their respective recoveries. Focused collection-pressure and repeated
teardown tests passed after the fixes. Portal and validation script syntax
checks passed. No local Babylon.js source patch is needed on top of 9.36.0;
the empty patch stack's alignment check passes without modifying the dirty
Babylon.js research checkout.

The iOS build retains bundle id
`com.rebeckerspecialties.BabylonNative.Playground`, team `4GWK4YU2PW`, and
`Apple Development: Matthew Donovan Hargett (79G4CD2XGC)`. Regenerate the
existing Xcode project after target/header changes, then preserve the local
bundle-id override when building:

```sh
cmake -S . -B build_int_ios
cmake --build build_int_ios --target Playground --config Release --parallel 8 -- PRODUCT_BUNDLE_IDENTIFIER=com.rebeckerspecialties.BabylonNative.Playground
```

Use the pinned Rust toolchain on `PATH` as documented in `AGENTS.md`. Build
success and signing are separate from deployment and physical-device testing.

These results do not establish restored 50+ FPS, physical XS placement after
this recovery, Windows execution, or Android/Linux backend qualification.
The earlier upright/floor-fixed user acceptance remains distinct from this
source audit. No framebuffer readback is used to claim camera passthrough.
