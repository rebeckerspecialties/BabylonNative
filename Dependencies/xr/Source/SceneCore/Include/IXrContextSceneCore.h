#pragma once

namespace xr
{
    struct IXrContextSceneCore
    {
        virtual bool IsInitialized() const = 0;
        virtual ~IXrContextSceneCore() = default;
    };
}

