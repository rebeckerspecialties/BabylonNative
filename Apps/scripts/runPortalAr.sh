#!/bin/sh
# Use the packaged assets so this also tests the signing/build resource list.
set -eu
if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
    echo "usage: sh runPortalAr.sh DEVICE [gt-vbao|ssao2]" >&2
    exit 2
fi
device=$1
mode=${2:-gt-vbao}
base=app:///Scripts
set -- "$base/native_xr_portal_manual_validation.js" "$base/native_xr_portal_profile_prepass.js"
case $mode in
    gt-vbao) set -- "$@" "$base/native_xr_portal_gtvbao_cosine_validation.js" "$base/native_xr_portal_world_normals_validation.js" "$base/native_xr_portal_ao_r8_validation.js" ;;
    ssao2) ;;
    *) echo "unknown AO mode: $mode" >&2; exit 2 ;;
esac
exec xcrun devicectl device process launch --device "$device" --terminate-existing --console \
    com.rebeckerspecialties.BabylonNative.Playground "$@" "$base/native_xr_babylon_portal_demo.js"
