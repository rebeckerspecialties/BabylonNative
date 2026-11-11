# wgpu-native Integration

The runtime is pinned to published takeover/parity commit
8322fe1a94ad0193c3393d03fd2a2f3c565e320b in rebeckerspecialties/wgpu-native.
It includes upstream trunk 589e6caa4e9fa5596f601e82ca5d3937a1afc829 and the
runtime fixes proposed in gfx-rs/wgpu-native#630. Dependent tests remain in #631.

0001-wgpu-30.0.1.patch aligns its five wgpu crate pins with the workspace's
released wgpu 30.0.1 (40f4a34e), uses that release's acceleration-structure field
names and reports only WGSL extensions implemented by its Naga version.
CMake applies it with a forward check, or verifies
the reverse check when already applied. A mismatch fails configuration. No
additional runtime behavior is hidden in this patch.

The earlier large runtime projection patch is retired because its source is now
published. Its original authorship remains in the dependency's history: the
parity implementation is by Inner-Daemons, with review and runtime fixes by Matt
Hargett. The published branch includes current upstream release profile settings
(lto = true, codegen-units = 1).

CMake creates a build-local rlib-only copy for the shared Cargo graph, retaining
the sanitizer and backend feature restrictions documented in CMakeLists.txt.
Source overrides must include the release-pin patch and runtime fixes. The
shared Cargo.lock must resolve one copy of each wgpu crate.

Upstream wgpu-native itself still targets wgpu 29.0.1. Replacing the parity
branch with upstream trunk would remove the required 30.x API support.

Keep CPU checks, C API tests, screenshots and physical-device performance
evidence distinct. Earlier acceptance details remain in API_COVERAGE.md and
Apps/Playground/Scripts/DAWN_VALIDATION.md.
