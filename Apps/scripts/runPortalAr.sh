#!/bin/sh
# Use the packaged assets so this also tests the signing/build resource list.
set -eu
if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
    echo "usage: sh runPortalAr.sh IOS_DEVICE|adb:ANDROID_SERIAL [gt-vbao|gt-vbao-geometry|ssao2|ssao2-geometry]" >&2
    exit 2
fi
device=$1
mode=${2:-}
if [ -z "$mode" ]; then
    mode=gt-vbao
    case $device in
        adb:*)
            model=$(adb -s "${device#adb:}" shell getprop ro.product.device)
            if [ "$(printf '%s' "$model" | tr -d '\r')" = monterey ]; then
                mode=gt-vbao-geometry
                echo "Quest 1: using gt-vbao-geometry to preserve stereo scene color." >&2
            fi ;;
    esac
fi
base=app:///Scripts
set -- "$base/native_xr_portal_manual_validation.js"
case $mode in
    gt-vbao|gt-vbao-geometry)
        if [ "$mode" = gt-vbao ]; then set -- "$@" "$base/native_xr_portal_profile_prepass.js"; fi
        set -- "$@" "$base/native_xr_portal_gtvbao_cosine_validation.js" "$base/native_xr_portal_world_normals_validation.js" "$base/native_xr_portal_ao_r8_validation.js" ;;
    ssao2) set -- "$@" "$base/native_xr_portal_profile_prepass.js" ;;
    ssao2-geometry) ;;
    *) echo "unknown AO mode: $mode" >&2; exit 2 ;;
esac
set -- "$@" "$base/native_xr_babylon_portal_demo.js"
case $device in
    adb:*) exec adb -s "${device#adb:}" shell am start -S -W -n com.android.babylonnative.playground/.PlaygroundActivity --es args "'$*'" ;;
    *) exec xcrun devicectl device process launch --device "$device" --terminate-existing --console \
        com.rebeckerspecialties.BabylonNative.Playground "$@" ;;
esac
