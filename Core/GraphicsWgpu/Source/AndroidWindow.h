#pragma once

#include <android/native_window.h>
#include <memory>

namespace Babylon::Graphics
{
    inline std::shared_ptr<ANativeWindow> RetainWindow(ANativeWindow* window)
    {
        if (!window)
        {
            return {};
        }
        ANativeWindow_acquire(window);
        return {window, &ANativeWindow_release};
    }
}
