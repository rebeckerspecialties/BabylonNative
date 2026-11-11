#pragma once

// C ABI of the babylon_webxr_core crate (Core/WebXr/Rust). Strings are UTF-8 pointer + length.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    // XRSessionMode values stored by babylon_webxr_session_mode_parse.
    enum babylon_webxr_session_mode
    {
        BABYLON_WEBXR_SESSION_MODE_INLINE = 0,
        BABYLON_WEBXR_SESSION_MODE_IMMERSIVE_VR = 1,
        BABYLON_WEBXR_SESSION_MODE_IMMERSIVE_AR = 2
    };

    bool babylon_webxr_session_mode_parse(const uint8_t* name, size_t length, uint32_t* outMode);

    // Feature descriptors as the WebXR Device API and its modules define them.
    bool babylon_webxr_feature_parse(const uint8_t* name, size_t length, uint32_t* outFeature);
    uint32_t babylon_webxr_feature_count(void);
    const char* babylon_webxr_feature_name(uint32_t index);

#ifdef __cplusplus
}
#endif
