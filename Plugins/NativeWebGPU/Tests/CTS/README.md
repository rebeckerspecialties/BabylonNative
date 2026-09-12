# Headless WebGPU CTS

This runner executes unmodified gpuweb CTS fixtures in BabylonNative's existing
AppRuntime/JavaScriptCore/NativeWebGPU environment. It creates a headless native
wgpu device with validation enabled. No document, canvas, image, video, layout,
or test-only EventTarget mocks are installed. NativeWebGPU provides production
GPU interfaces and headless error-event delivery; Window supplies scheduling and
`queueMicrotask`. A monotonic `performance.now()` completes the runner's host services.

The approach follows Dawn's small [CTS provider adapter][dawn-provider]: install
real WebGPU globals and supply a GPU provider. Its Node binary cannot simply be
loaded into JavaScriptCore; BabylonNative already supplies the equivalent native
bindings. Static spec registration replaces filesystem/dynamic-import discovery;
upstream query parsing, case/subcase filtering, fixtures, device pooling, checks,
and cleanup remain unchanged. A recorder subclass preserves exception evidence
when cleanup hangs. No expectations are overridden.

[dawn-provider]: https://dawn.googlesource.com/dawn/+/80ee0043018a51532ea0fa2e77496cc66634157e/src/dawn/node/cts.cjs

## Reproduce

From the BabylonNative root, prepare build-only dependencies:

```sh
git clone https://github.com/gpuweb/cts.git build_wgpu_cts/cts
git -C build_wgpu_cts/cts checkout 32cc9f03ad14135b5f6f833dba83e7e80edc256f
npm install --prefix build_wgpu_cts/tools --no-audit --no-fund --ignore-scripts --save-exact esbuild@0.25.10 webgpu@0.6.0
node Plugins/NativeWebGPU/Tests/CTS/build.mjs
```

Reuse the configured canonical integration build. The test targets require
`BABYLON_NATIVE_BUILD_WEBGPU_TESTS=ON`:

```sh
PATH=/Users/matt/.rustup/toolchains/1.94.1-aarch64-apple-darwin/bin:$PATH CARGO_NET_OFFLINE=true cmake --build build_wgpu_pr_stack/canonical-build --target NativeWebGPUCtsRunner NativeWebGPUAsyncTests --parallel 6
node Plugins/NativeWebGPU/Tests/CTS/run.mjs
```

`run.mjs` accepts an alternative native executable and evidence directory as its
first two arguments. It compares the identical bundle against Dawn's packaged
Node/Metal implementation, then tries each native query family in a fresh
process. It records stdout/stderr, exit codes, signals, timeouts, queries, package
lock, CTS revision, bundle/executable hashes, repository head, and dirty state.
It exits nonzero if either provider fails. A third argument selects a query manifest:

```sh
caffeinate -i node Plugins/NativeWebGPU/Tests/CTS/run.mjs '' build_wgpu_cts/results/errors Plugins/NativeWebGPU/Tests/CTS/errors.json
```

For a single case, without the Dawn control:

```sh
build_wgpu_pr_stack/canonical-build/Plugins/NativeWebGPU/Tests/NativeWebGPUCtsRunner build_wgpu_cts/cts-smoke.js 45 'webgpu:api,operation,compute,basic:memcpy:*'
```

The 30-second per-case guard aborts on incomplete cleanup, never marks a running
case passed, and never reuses its device. The outer process watchdog also catches
synchronous hangs. A family aborted at its first case has **not** executed its
remaining cases. `contracts.js` is an additional binding diagnostic, **not CTS**.

`smoke.json` pins both source and selection. `build.mjs` refuses a different or
dirty CTS checkout. Upstream assertions are not patched, and the CTS's full npm
development toolchain is not needed. Generated bundles, dependencies, and logs
stay under `build_wgpu_cts`, not in source control.

## Sanitizers

Use a separate build directory. On the tested macOS 27 host, use upstream LLVM
23.1.0 (Homebrew `llvm`) and Rust 1.94.1 with `rust-src`. AppleClang's compiler-rt
ABI does not match Rust's; upstream LLVM 21's runtime deadlocks during dyld
initialization before `main` on this OS. A minimal executable with LLVM 23's
runtime passes. These startup failures are not CTS or adapter results.

```sh
export PATH="$(rustup run 1.94.1 rustc --print sysroot)/bin:$PATH"
rustup component add rust-src --toolchain 1.94.1
export CC="$(brew --prefix llvm)/bin/clang"
export CXX="$(brew --prefix llvm)/bin/clang++"
export OBJC="$CC" OBJCXX="$CXX"
node Plugins/NativeWebGPU/Tests/CTS/configure.mjs asan build_wgpu_cts/asan-current
cmake --build build_wgpu_cts/asan-current --target NativeWebGPUCtsRunner NativeWebGPUAsyncTests --parallel 6
export ASAN_OPTIONS=detect_stack_use_after_return=1:halt_on_error=1
export UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1
caffeinate -i node Plugins/NativeWebGPU/Tests/CTS/run.mjs --native-only build_wgpu_cts/asan-current/Plugins/NativeWebGPU/Tests/NativeWebGPUCtsRunner build_wgpu_cts/results/asan-operations
caffeinate -i node Plugins/NativeWebGPU/Tests/CTS/run.mjs --native-only build_wgpu_cts/asan-current/Plugins/NativeWebGPU/Tests/NativeWebGPUCtsRunner build_wgpu_cts/results/asan-errors Plugins/NativeWebGPU/Tests/CTS/errors.json
build_wgpu_cts/asan-current/Plugins/NativeWebGPU/Tests/NativeWebGPUAsyncTests
```

An optional fourth argument to `configure.mjs` seeds project options and source
overrides from an existing CMake build, without importing its compiler/linker
flags. The local runs used `build_wgpu_pr_stack/canonical-build` as that seed.
Omit it to fetch the repository's public dependency declarations. `none` selects
an unsanitized build. Configuration verifies that dependencies did not silently
change the requested sanitizer switches. Reconfigure once more to check that
the switches remain enabled across repeated configuration.

For TSan, use the same compiler environment but configure `tsan` into
`build_wgpu_cts/tsan-current`, build the same targets, and run both selections
and the native regressions with `TSAN_OPTIONS=halt_on_error=1`. Never combine
ASan and TSan in one binary. `--native-only` omits the packaged, unsanitized Dawn
control explicitly; the default comparison still reports its known failures.

`BABYLON_NATIVE_RUST_SANITIZERS=ON` instruments the Rust backend, wgpu-native,
wgpu/core/hal, other Rust dependencies and rebuilt `std`. It uses unstable Rust
sanitizer/build-std flags with `RUSTC_BOOTSTRAP=1` on the pinned compiler and
`-Zexternal-clangrt` to share the C++ link's runtime. C++ additionally uses UBSan
in the ASan build. The generated wgpu-native dependency exposes only `rlib`, not
unused standalone native libraries with separate runtime links. SDK frontend
compatibility flags do not suppress sanitizer checks. JavaScriptCore, system
frameworks and the GPU driver remain uninstrumented; this is not GPU-memory
instrumentation or exhaustive race/leak proof.

Source/configuration/binary hashes and sanitizer environment are recorded by
`run.mjs`. No suppressions or relaxed CTS assertions are used. Inspect stderr
as well as exit status. Keep pre-main runtime failures and interrupted attempts
separate from completed test results.

## M4 Results

Recorded September 12, 2026 UTC, with canonical `wgpu`
head `3ad51606` plus the pre-existing local changes preserved. Adapter evidence
reports **Apple M4 Max, Metal**. No iOS, performance, or full-CTS acceptance is
implied.

| Selected operation | Cases | Subcases |
| --- | ---: | ---: |
| Compute memcpy | 1 | 1 |
| Compute dispatch size 256, workgroup sizes and axes | 1 | 15 |
| Queue writeBuffer types, offsets, overlapping writes | 17 | 24 |
| clearBuffer ranges and defaults | 1 | 50 |
| copyBufferToBuffer signatures, ordering, transitions | 4 | 342 |
| RGBA8 2D texture copies, regions and mip levels | 1 | 112 |
| Total | 25 | 544 |

NativeWebGPU and the Dawn `webgpu@0.6.0` Metal control pass **25/25 cases, 544 subcases**, with
no warnings or skips. This is the packaged control version, not a claim that its
binary was built from the inspected current Dawn source commit above.

The expanded `errors.json` selection passes **75/75 cases, 370 subcases** natively:
37 scope cases, 3 uncaptured-event cases, 2 loss/identity cases, and 33 mapping
cases (328 mapping subcases). This includes 100,000-deep scopes, real invalid
GPU resources, early versus deferred rejection, cancellation precedence, mapped
range detachment, and default/bounds/overlap handling. No warnings or skips.
Combined native coverage is **100 cases / 914 subcases**, not the full CTS.
OOM-generating cases are deliberately not selected: they request hundreds of
GiB. GC/worker/canvas/external-image cases are outside this selection.

The same **100 cases / 914 subcases** also pass with C++/Rust ASan and C++ UBSan,
including stack-use-after-return checking. All 18 native/direct-C regressions
pass under the same instrumentation. Evidence: `build_wgpu_cts/results/asan-current-operations/`
and `asan-current-errors/`. The sanitizer effort fixed the lost CMake sanitizer
cache switch, unused standalone Rust dependency outputs, and missing explicit
`<system_error>`/`<cstdlib>` dependency includes. No additional runtime sanitizer
finding occurred in this selection.

A separate C++/Rust TSan build also passes **100 cases / 914 subcases** and
all 18 native/direct-C regressions, without suppressions or reported races.
Evidence: `build_wgpu_cts/results/tsan-current-operations/`,
`tsan-current-errors/`, and `build_wgpu_cts/tsan-current-regressions.log`.
Both use Rust 1.94.1 and LLVM 23.1.0's compiler-rt; ABI/runtime linkage was
checked separately. These are M4 host correctness results, not device performance.

The packaged Dawn control passes 67/75 expanded cases. Eight mapping cases fail
on early rejection timing or missing validation errors for pending mappings.
Do not weaken these upstream assertions or call the comparison fully passing.
This describes `webgpu@0.6.0`, not a freshly built current Dawn Node binary.

Final-source comparisons are in `build_wgpu_cts/results/final-operations/` and
`final-errors/`. Final integration evidence is in
`build_wgpu_pr_stack/m4-validation/2026-09-12T06-24-47.670Z/`: 18 native/direct-C
regressions, five JS test groups, eight harness checks, and four screenshot
cases pass (GUI3D SpherePanel, GUI Slate, PBR refraction, PBR texture repetition).
No thresholds changed. Earlier integration attempts include the obsolete
synchronous-throw test expectations; those were replaced with actual scoped
validation assertions, not removed. No iPhone or performance rerun is implied.

### Original Failure

Before the binding fixes, native runs reached upstream fixtures but could not finalize their first case in
each family. The first exception is `ReferenceError: Can't find variable:
GPUDevice` from `Fixture.trackForCleanup`. Native diagnostics establish:

- The `GPUDevice` constructor/type identity is absent.
- Popping an empty pushed scope resolves `undefined`, not `null`.
- Popping an unbalanced scope resolves `undefined`, rather than rejecting with
  `OperationError`.
- `device.destroy()` does not settle `device.lost`; the independent probe still
  sees it pending after 1000ms. The implementation explicitly uses a no-op
  destroy and never-resolving promise.

The CTS device pool detects the scope problem, destroys the device, and awaits
`device.lost` while replacing it. This explains the subsequent cleanup hang.
Do not count these incomplete cases as ordinary finalized CTS failures or claim
GPU dispatch/copy validation: native stats show resource creation but zero queue
submissions before the missing type identity stops execution. The four separate
contract diagnostics passed in Dawn and failed natively. Native async/C ABI
regressions at that original revision were **15/15 passing**.

The completed awake comparison is under
`build_wgpu_cts/results/2026-09-12T05-36-23.078Z/`: Dawn passes all 25 cases;
each of the six native families records its first case as incomplete and exits
1 after the 30-second cleanup deadline. No outer process timeout or signal
occurs. This is six incomplete native cases and 19 unrun cases, not 25 finalized
CTS failures.

Initial evidence is under `build_wgpu_cts/results/2026-09-12T03-39-26.870Z/`;
its last texture process was interrupted by clamshell sleep at 03:42:21 UTC.
The host watchdog fired during a subsequent dark wake; do not treat that
SIGKILL as a GPU hang. `native-texture-retry.jsonl` records the awake retry's
ordinary binding exception and 30-second cleanup timeout. Its process sample
shows the JS dispatcher idle, not blocked in a GPU call. The separate regression
log is `build_wgpu_cts/native-regression.log`. Keep the lid open during runs;
an idle-sleep assertion cannot prevent clamshell sleep.

Harness checks also reject both empty selections and overlapping queries with
nonzero exit status (`build_wgpu_cts/empty-selection.jsonl` and
`build_wgpu_cts/overlapping-selection.jsonl`). Neither is a CTS result.

## Implementation Boundaries

- Rust's real validation/OOM/internal error callbacks enter a typed queue; the
  wake callback never enters JS under a wgpu lock. DeviceCall sets logical
  ownership for each binding operation. JS scopes capture the first matching
  error, retain scope snapshots across deferred work, and deliver uncaptured
  errors on a later runtime task. Unowned renderer errors are logged, not
  attributed to arbitrary JS devices. Physical loss notifies all logical devices.
- All JS devices still share the renderer's physical device. Destroying a JS
  device invalidates its tracked allocations and recorded handles, detaches
  mappings, cancels pending maps, and resolves its stable lost promise; it must
  not destroy the renderer or another JS device. This is not independent-device
  scheduling/isolation, nor complete requiredFeatures/requiredLimits negotiation.
- Native command encoding now occurs at `finish`, so validation reaches that
  scope instead of a later submit scope. Queue writes are submitted first, in
  a separate upload command buffer, with finished command buffers batched into
  one queue submission. Performance has not been measured for this change.
- `writeBuffer` uses typed-array element units and validates before staging.
  Buffer copies support both overloads and omitted size. Zero-sized buffers
  retain size zero rather than silently allocating a four-byte logical buffer.
- Mapping resolves only after a real native map. The current implementation
  still blocks the dispatched host task in `device.poll(wait)`; replace this
  with callback-driven progress before claiming efficient asynchronous mapping.
  JSC byte-pointer access pins ArrayBuffers: fill a temporary, transfer it, and
  never request the returned mapping's pointer before detachment. The existing
  older-JSC no-transfer fallback is not full detachment conformance.
- Broader WebIDL conversions/receiver branding, mapped resources submitted
  while mapping is pending, query sets, shader compilation-info diagnostics,
  render-bundle validation and full device feature/limit negotiation remain
  follow-up work. These results do not certify that surface. No Babylon.js
  source patch or assertion bypass was needed.

This path primarily exercises
GraphicsWgpu's Rust wgpu API, **not** the wgpu-native public C ABI. Passing it
does not establish CTS coverage for the replacement wgpu-native PR stack.
Keep the [C API coverage ledger](../../../../Patches/wgpu-native/API_COVERAGE.md)
and direct C tests separate. A C-backed CTS provider must demonstrate that its
calls actually reach the feature-only C library; unchanged `deno_webgpu` does
not establish that either.
