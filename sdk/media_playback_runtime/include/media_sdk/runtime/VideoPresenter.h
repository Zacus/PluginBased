#pragma once

#include "media_sdk/Frame.h"

#include <chrono>
#include <cstdint>
#include <string>

namespace media_sdk::runtime {

using PresentId = std::uint64_t;

struct VideoPresenterCapabilities {
    bool supportsVideoToolboxPixelBuffer = false;
    bool supportsCpuYuv = true;
    bool asyncPresent = true;
    std::uint32_t maxPendingFrames = 1;
};

enum class PresentStatus {
    Presented,
    Queued,
    UnsupportedNativeHandle,
    DeviceLost,
    Failed
};

struct PresentTiming {
    std::chrono::microseconds pts { 0 };
    std::chrono::microseconds clock { 0 };
    std::chrono::microseconds lateness { 0 };
};

struct PresentResult {
    PresentId id = 0;
    PresentStatus status = PresentStatus::Failed;
};

struct PresentCompletion {
    PresentId id = 0;
    PresentStatus status = PresentStatus::Failed;
    std::string detail;
};

class IVideoPresenterEvents {
public:
    virtual ~IVideoPresenterEvents() = default;
    virtual void onPresentComplete(PresentCompletion completion) = 0;
};

class IVideoPresenter {
public:
    virtual ~IVideoPresenter() = default;

    virtual VideoPresenterCapabilities capabilities() const = 0;
    virtual void setEvents(IVideoPresenterEvents* events) = 0;
    virtual PresentResult present(VideoFrame frame, PresentTiming timing) = 0;
    virtual void clear() = 0;
};

} // namespace media_sdk::runtime
