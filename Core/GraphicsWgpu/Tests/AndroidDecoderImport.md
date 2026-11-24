# Android decoder buffer import

This standalone NDK test decodes four real frames into an AImageReader, imports each
AHardwareBuffer into Vulkan and creates a YCbCr-converted view. Run it separately
from frame timing. It submits no GPU work and does not qualify a video texture path.

```sh
"$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/$NDK_HOST/bin/aarch64-linux-android29-clang++" \
  -std=c++17 -O2 -static-libstdc++ AndroidDecoderImport.cpp -landroid -lmediandk -lvulkan -o /tmp/decoder-import
adb -s "$SERIAL" push /tmp/decoder-import "$FILM" /data/local/tmp/
adb -s "$SERIAL" shell /data/local/tmp/decoder-import "/data/local/tmp/$(basename "$FILM")"
adb -s "$SERIAL" shell /data/local/tmp/decoder-import "/data/local/tmp/$(basename "$FILM")" yuv420
```

Use the unmodified 30 fps VP8 and the 24 fps stereo remux from the Samples APK.
On Quest 1 both returned external format 506 and feature mask `0x820000`.
Memory import, binding and view creation succeeded; the driver did not report
the [required sampled-image bit](https://docs.vulkan.org/refpages/latest/refpages/source/VkAndroidHardwareBufferFormatPropertiesANDROID.html).
Do not select a production sampler on extension names alone. Qualification still
requires producer/consumer fence handling, independent per-eye reference pixels,
and display cadence on the actual shared rendering device.
