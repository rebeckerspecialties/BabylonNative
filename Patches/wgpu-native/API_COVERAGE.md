# Runtime Scope and API Coverage

## What the Replacement Stack Delivers

[PR #617](https://github.com/gfx-rs/wgpu-native/pull/617) separates the expanded
wgpu 30 feature exposure from the dependent testing work in
[PR #594](https://github.com/gfx-rs/wgpu-native/pull/594). The replacements keep
that split and preserve original author/email/date and source-commit ancestry.
The runtime is a general native WebGPU implementation built on wgpu-core, not
a Babylon-specific rendering path. It exposes native extensions as well as
the shared webgpu.h API; native extensions are not WebGPU-spec features.

The runtime scope is materially implemented and tested beyond BabylonNative:
real C-backend Metal runs cover compute/render, multi-draw indirect, shader
diagnostics, resource validation, mesh shaders and ray-tracing examples. The
new public C API surface tests cover interleaved acquisitions, explicit
destruction, unconfiguration, cross-thread release and registry cleanup.
There are no Babylon-specific changes in the replacement runtime.

This is expanded wgpu 30 support, not complete WebGPU API parity or conformance
certification. The original broad "all features/all tests" claim is not an
appropriate description of the current acceptance state.

## Evidence at the Integrated Runtime

Runtime: `01a4f434a7bcc9d0909a8320a2c297a6e22ea1bc`.
Dependent tests: `a105c5af0722a2e7b5a28f077bf80bc76c99a4e4`.
Host: Apple M4 Max, macOS 27.0 (26A428), Xcode 27.0 (27A266a), Rust 1.94.1.

- 13 stack-check groups pass, including feature builds, mapping/default tests,
  generated-binding checks, standalone API consumers, lint, docs and attribution.
- All 12 standalone public C API Metal surface cases pass. Ten reproduce
  failures without the surface fix; the other two are controls.
- Two final full upstream C-backend sweeps each report 1002/1003 passed and
  eight explicit exclusions. The one failure is an intermittent timestamp
  resolve problem reproduced through direct Rust and raw Metal too.
- Of the 1003 selected cases, 325 are labeled `Executed` on Metal (324 passed,
  one failed). Other entries include CPU-only tests, unsupported-feature cases,
  expected failures and Noop-only validation. The C-backend dispatcher hands
  Noop-only instance requests back to wgpu-core. Do not call all 1003 C API GPU
  execution tests.
- The canonical BabylonNative build passes 90/90 screenshot cases, 15 native
  async/direct-C tests, five JavaScript tests and eight harness regressions.
- A Clang AST inventory found all 262 declared webgpu.h/wgpu.h functions in the
  static library's exported symbols. Symbol completeness is not behavioral
  completeness: some exported functions abort when called.

The standalone timestamp matrix has real compute work and verifies its output.
Shared/private counter storage, fences, events and separately queued command
buffers still produce zero GPU-resolved samples. CPU resolution after command
completion reads valid shared samples, and waiting for completion before
encoding a GPU resolve passes. No synchronous-wait workaround or weakened
timestamp assertion was added. The earlier nextest pipe-lifetime warning did
not reproduce in two final full sweeps or 20 C plus 20 direct-Rust repetitions;
its cause remains unknown. It is not evidence of a GPU memory leak.

## Standard C API Gaps

The following eleven explicit stubs remain. They are pre-existing API gaps,
not introduced by the new surface fix. Counting only `src/unimplemented.rs`
misses the five stubs in `src/lib.rs`.

| Area | Entry points |
| --- | --- |
| Function lookup | `wgpuGetProcAddress` |
| Async pipelines | `wgpuDeviceCreateComputePipelineAsync`, `wgpuDeviceCreateRenderPipelineAsync` |
| Futures | `wgpuDeviceGetLostFuture`, `wgpuInstanceWaitAny` |
| Texture query | `wgpuTextureGetTextureBindingViewDimension` |
| Instance capabilities | `wgpuGetInstanceFeatures`, `wgpuHasInstanceFeature`, `wgpuSupportedInstanceFeaturesFreeMembers` |
| Mapped copies | `wgpuBufferReadMappedRange`, `wgpuBufferWriteMappedRange` |

Implemented async entry points also return placeholder futures and do not
consistently respect callback modes. A direct C++ consumer on the M4 reproduced
`wgpuInstanceRequestAdapter` invoking both `WaitAnyOnly` and `AllowProcessEvents`
callbacks before any wait/process-events call, returning future ID zero.
Independent instance-feature and mapped-copy calls aborted; their ordinary
mapped-pointer control passed. This is a contract failure even for trusted
callers. Browser sandboxing requirements are separate from callback timing,
object lifetime, capability reporting and error-delivery semantics.

The current Rust C-backend adapter uses spontaneous callbacks, so passing its
tests does not establish the other callback modes. A C signature inventory
also does not replace exhaustive ABI/type-width checks. Surface-unconfigure
backend memory release remains limited by the pinned core API, which has no
standalone backend-unconfigure operation.

## Next Validation Priorities

1. Specify and implement shared future ownership, event delivery and
   cancellation. Test callback modes, exactly-once completion, device loss,
   instance/device release, reentrancy restrictions and cross-thread delivery
   before building async pipelines on top. Do not emulate this with synchronous
   callbacks or success-shaped placeholder futures.
2. Close the instance-capability and mapped-copy stubs with direct C tests,
   including unsupported requests, output-chain validation, mapping bounds,
   alignment, read-only mappings and status/error delivery. Audit exported
   signatures against the header, not only symbol presence.
3. Validate platform surfaces and extension features on Vulkan/DX12 and native
   iOS/Android/window systems. M4 correctness is not iPhone performance or
   cross-backend acceptance; unsupported features need explicit accounting.
4. Add WebGPU CTS coverage through a frontend that demonstrably calls the
   feature-only C library. The pinned `cts_runner` uses `deno_webgpu`, whose
   dependency and calls are directly on wgpu-core. Running it unmodified would
   validate the core, not wgpu-native. Keep that useful core baseline separate.

Local reproducible diagnostics and ledgers live under `build_wgpu_pr_stack`:
`webgpu-contract-probe.cpp`, `webgpu-contract-validation/`,
`metal-timestamp-probe.mm`, `timestamp-validation/final/`,
`probe-nextest-pipes.mjs`, `nextest-pipe-validation/`, and
`post-merge-c-api-validation/`. These known-gap probes are separate from the
passing dependent-test additions; do not silently skip them or describe them
as passing. No replacement upstream PR has been created or pushed.
