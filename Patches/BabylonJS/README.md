# Babylon.js Patch Stack

Apps pins Babylon.js 9.36.0. The source patch series is intentionally empty:
the published package contains the recorder retention, atomicity, replay and
reporting contract (#19084), and snapshot recorded-draw reuse with the
pre-replay command-stream flush (#19089). It also retains #19076 direct-draw
fallback and the earlier NativeXR, KTX, SSAO2 and font-offset fixes.

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

The series file remains the explicit list of local source patches. The
check/apply script accepts an intentionally empty series. A missing listed
patch, or a patch which neither applies nor passes the reverse check, is still
a hard failure. Apply mode refuses a dirty Babylon.js checkout.

    node Apps/scripts/applyBabylonJsPatchStack.js --check
    node Apps/scripts/applyBabylonJsPatchStack.js --apply

Use the published ES2015+ UMD bundles directly. Do not downlevel them or use the
published ES5 bundles on this branch; see AGENTS.md for the texture-layout
failure. Generated bundles are validation inputs and are never source patches.
