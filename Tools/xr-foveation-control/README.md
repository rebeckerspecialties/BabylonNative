# Vulkan fixed foveation control

Compares ordinary rendering with a static hardware fragment density map and
a subsampled color attachment. Both paths draw the same 2048 x 2263 eye, then
sample it in a second pass to produce a 1216 x 1344 output. Only the normal
output is read back, after the final timestamp. An immutable combined sampler
is used for each path: ordinary for the reference, subsampled for foveation.

This is a single-eye headless diagnostic sized for Quest 1, not an XR session
or benchmark of the compositor. At its fixed extents it verifies center and
edge checker transitions and a non-empty peripheral color channel. Each
off/on/on/off block reports median GPU milliseconds over the last 32 of 48
frames and the observed Qualcomm GPU clock range. No private counters, debug
layers, root, or clock changes are required. Compare only comparable clocks.
Exit zero requires all four blocks to pass the pixel checks.

From this directory, set `ANDROID_NDK_HOME` to an installed NDK, `HOST_TAG` to
its host tool directory (such as `darwin-x86_64`), and `ANDROID_SERIAL` to the
target. Build outside the source tree:

```sh
OUT=$(mktemp -d)
CC="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/$HOST_TAG/bin/aarch64-linux-android29-clang"
GLSLC="$ANDROID_NDK_HOME/shader-tools/$HOST_TAG/glslc"
"$CC" -O2 -Wall main.c -lvulkan -lm -o "$OUT/foveation-control"
"$GLSLC" eye.vert -o "$OUT/eye.vert.spv"
"$GLSLC" resolve.frag -o "$OUT/resolve.frag.spv"
for n in 1 20 80; do
    "$GLSLC" "-DSTEPS=$n" eye.frag -o "$OUT/eye-$n.frag.spv"
done
adb shell mkdir -p /data/local/tmp/xr-foveation-control
adb push "$OUT/." /data/local/tmp/xr-foveation-control/
for n in 1 20 80; do
    adb shell "cd /data/local/tmp/xr-foveation-control && ./foveation-control eye.vert.spv eye-$n.frag.spv resolve.frag.spv" || break
done
```

Keep other rendering workloads stopped for the duration. This probe measures
the eye plus a shader-based reconstruction; it does not measure direct
composition of an OpenXR runtime-owned foveated swapchain.
