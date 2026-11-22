# WebGPU layered stereo qualification

This patch targets Babylon.js 9.37.1 (`dea672fe0d07d8f7e07a95504af62966ef277a5f`).
It is separate from the default Apps bundle patch series. Apply it with
`git apply --check`, then `git apply`, from that Babylon.js checkout.
Build the core including its generated WGSL modules as usual.

Single-pass rendering requires all three: a renderer that reports the
internal stereo capability, a request with `preferMultiviewOnInit`, and
two-layer color **and** depth subimages on acquisition. It is not a browser
WebGPU feature name. Keep the two-pass path for ordinary browser devices
and for devices/workloads where it is faster.

The fixture under `Apps/scripts/questMenuLadder` uses a StandardMaterial
and an off-axis marker. Build its control with `buildStereoPixels.cjs
WORKSPACE NODE_MODULES OUTPUT mono-passes` and its test with the same
arguments except `multiview COMPILED_CORE_DIST`. WORKSPACE supplies
esbuild; NODE_MODULES supplies the pinned packages. Collect separate
process logs for the two runs. `node compareStereo.mjs TWO_PASS_LOG
SINGLE_PASS_LOG` checks every sampled RGBA value in each eye, rejects a
silent fallback and verifies stereo disparity. The Quest 1 matched all
4,096 sampled pixels. This fixture does not measure through-lens aliasing.

The opt-in is deliberate: on Quest 1 the large playback scene submitted
roughly half the indices but was slower in single-pass stereo. Do not select
it solely because the adapter supports multiview.
