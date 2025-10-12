#pragma once

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

typedef struct IXrContextOpenXR
{
    virtual bool IsInitialized() const = 0;
    virtual XrInstance GetXrInstance() const = 0;
    virtual XrSession GetXrSession() const = 0;
    virtual XrSpace GetXrSpace() const = 0;
    virtual XrSystemId GetXrSystemId() const = 0;
} IXrContextOpenXR;