# Android decoder buffer import

This standalone NDK test decodes four real frames into an AImageReader and queries
each AHardwareBuffer. It imports the memory and creates a view only for a sampleable
format. Exit 3 means decoded but missing sampling support; 1 means decode/import
failure and 0 means all four views bound. Run it separately from frame timing.
It submits no GPU work and does not qualify a video texture path.

```sh
"$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/$NDK_HOST/bin/aarch64-linux-android29-clang++" \
  -std=c++17 -O2 -static-libstdc++ AndroidDecoderImport.cpp -landroid -lmediandk -lvulkan -o /tmp/decoder-import
adb -s "$SERIAL" push /tmp/decoder-import "$FILM" /data/local/tmp/
adb -s "$SERIAL" shell /data/local/tmp/decoder-import "/data/local/tmp/$(basename "$FILM")"
adb -s "$SERIAL" shell /data/local/tmp/decoder-import "/data/local/tmp/$(basename "$FILM")" yuv420
```

The optional third argument pins a codec instead of accepting software fallback:

```sh
adb -s "$SERIAL" shell /data/local/tmp/decoder-import /data/local/tmp/vp9-p0.webm private OMX.qcom.video.decoder.vp9
```

On Quest 1, VP8 and hardware VP9 Profile 0 produced external format 506 with
`VkFormat=UNDEFINED` and features `0x820000`. For VP9 the PRIVATE, flexible YUV
and `yuv420-linear` readers all returned vendor format `0x7fa30c06`; requesting
`rgba` yielded no usable image. Do not infer a plane layout from that vendor ID.
The initial binding-only probe could create a view, but the driver omitted the
[required sampled-image bit](https://docs.vulkan.org/refpages/latest/refpages/source/VkAndroidHardwareBufferFormatPropertiesANDROID.html).
The named decoder advertised only Profile 0; the Profile 2 HDR10 fixture
yielded no decoded images. Keep these gates device-specific.

For presentation, prefer an XR media layer: Android's decoder feeds the OpenXR
swapchain Surface without an application texture upload. For a shader-readable
movie, qualify the *actual decoded buffer* on the shared Vulkan device; require
the advertised sampling features, producer/consumer fences, correct YCbCr
conversion and independent per-eye reference pixels before enabling import.
