# Babylon.js Patch Stack

Apps pins Babylon.js 9.35.0. The source patch series is currently empty: that
release includes the command recorder fallback, lifetime and atomicity contract
from BabylonJS/Babylon.js#19076 and #19084, as well as the earlier NativeXR, KTX,
SSAO2 and font-offset fixes.

The series file remains the explicit list of local source patches. The
check/apply script accepts an intentionally empty series. A missing listed
patch, or a patch which neither applies nor passes the reverse check, is still
a hard failure. Apply mode refuses a dirty Babylon.js checkout.

    node Apps/scripts/applyBabylonJsPatchStack.js --check
    node Apps/scripts/applyBabylonJsPatchStack.js --apply

Use the published ES2015+ UMD bundles directly. Do not downlevel them or use the
published ES5 bundles on this branch; see AGENTS.md for the texture-layout
failure. Generated bundles are validation inputs and are never source patches.
