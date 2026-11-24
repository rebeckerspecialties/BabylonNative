#pragma once

#include <XR.h>
#include <algorithm>

namespace Babylon::Plugins
{
    // An XR gamepad is live, but its axes and buttons are distinct snapshots.
    class GamepadSnapshot
    {
        using Gamepad = xr::System::Session::Frame::GamePad;
        Gamepad m_previous{};
        bool m_initialized{};

    public:
        template<typename Publish>
        void Update(const Gamepad& current, Publish&& publish)
        {
            const bool axes = !m_initialized || m_previous.Axes != current.Axes;
            const bool buttons = !m_initialized || !std::equal(m_previous.Buttons.begin(), m_previous.Buttons.end(),
                current.Buttons.begin(), current.Buttons.end(), [](const auto& a, const auto& b) {
                    return a.Pressed == b.Pressed && a.Touched == b.Touched && a.Value == b.Value;
                });
            if (!buttons && !axes) return;
            publish(buttons, axes, !m_initialized);
            if (buttons) m_previous.Buttons = current.Buttons;
            if (axes) m_previous.Axes = current.Axes;
            m_initialized = true;
        }
    };
}
