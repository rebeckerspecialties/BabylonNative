#pragma once

#include <Babylon/WebXr/Core.h>

#include <cstdint>
#include <optional>
#include <string_view>

namespace Babylon
{
    namespace Plugins
    {
        class XRFrame;
    }

    // WebXR identifiers are validated by the shared WebXR core (Core/WebXr), not by string
    // tables of this plugin. XRSessionMode carries the core's numbering.
    enum class XRSessionMode : uint32_t
    {
        Inline = BABYLON_WEBXR_SESSION_MODE_INLINE,
        ImmersiveVr = BABYLON_WEBXR_SESSION_MODE_IMMERSIVE_VR,
        ImmersiveAr = BABYLON_WEBXR_SESSION_MODE_IMMERSIVE_AR,
    };

    inline std::optional<XRSessionMode> ParseXRSessionMode(std::string_view name)
    {
        uint32_t mode{};
        if (!babylon_webxr_session_mode_parse(reinterpret_cast<const uint8_t*>(name.data()), name.size(), &mode))
        {
            return std::nullopt;
        }
        return static_cast<XRSessionMode>(mode);
    }

    // True when the WebXR specifications define `name` as a feature descriptor.
    inline bool IsXRFeatureDescriptor(std::string_view name)
    {
        return babylon_webxr_feature_parse(reinterpret_cast<const uint8_t*>(name.data()), name.size(), nullptr);
    }

    struct XRReferenceSpaceType
    {
        static constexpr auto VIEWER{"viewer"};
        // static constexpr auto LOCAL{"local"};
        // static constexpr auto LOCAL_FLOOR{"local-floor"};
        // static constexpr auto BOUNDED_FLOOR{"bounded-floor"};
        static constexpr auto UNBOUNDED{"unbounded"};
    };

    struct XRHitTestTrackableType
    {
        static constexpr auto POINT{"point"};
        static constexpr auto PLANE{"plane"};
        static constexpr auto MESH{"mesh"};
    };

    struct XRImageTrackingState
    {
        //static constexpr auto UNTRACKED{"untracked"};
        static constexpr auto TRACKED{"tracked"};
        static constexpr auto EMULATED{"emulated"};
    };

    struct XREye
    {
        static constexpr auto NONE{"none"};
        static constexpr auto LEFT{"left"};
        static constexpr auto RIGHT{"right"};

        static auto IndexToEye(size_t idx)
        {
            switch (idx)
            {
                case 0:
                    return LEFT;
                case 1:
                    return RIGHT;
                case 2:
                    return NONE;
                default:
                    throw std::runtime_error{"Unsupported index"};
            }
        }

        static uint32_t EyeToIndex(const std::string& eye)
        {
            if (eye == LEFT)
            {
                return 0;
            }
            else if (eye == RIGHT)
            {
                return 1;
            }
            else if (eye == NONE)
            {
                return 2;
            }
            else
            {
                throw std::runtime_error{"Unsupported eye"};
            }
        }
    };
} // Babylon
