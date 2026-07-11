#pragma once

#include "media_sdk/runtime/VideoPresenter.h"

#include <QPointer>

#include <atomic>
#include <deque>
#include <memory>
#include <mutex>

class FFmpegSurface;
struct VideoFrameData;
using VideoFrameDataPtr = std::shared_ptr<VideoFrameData>;

class QtRhiVideoPresenter final : public media_sdk::runtime::IVideoPresenter
{
public:
    explicit QtRhiVideoPresenter(FFmpegSurface* surface);
    ~QtRhiVideoPresenter() override;

    media_sdk::runtime::VideoPresenterCapabilities capabilities() const override;
    void setEvents(media_sdk::runtime::IVideoPresenterEvents* events) override;
    media_sdk::runtime::PresentResult present(
        media_sdk::VideoFrame frame,
        media_sdk::runtime::PresentTiming timing) override;
    void clear() override;

private:
    struct PendingPresent {
        media_sdk::runtime::PresentId id = 0;
        media_sdk::VideoFrame sdkFrame;
        VideoFrameDataPtr frame;
        media_sdk::runtime::PresentTiming timing;
    };

    bool surfaceSupportsNativeRendering(const QPointer<FFmpegSurface>& surface) const;
    VideoFrameDataPtr makeSurfaceFrame(const media_sdk::VideoFrame& frame) const;

    QPointer<FFmpegSurface> m_surface;
    std::atomic_uint64_t m_nextPresentId { 0 };
    mutable std::atomic_bool m_nativeRenderingSupported { false };
    mutable std::mutex m_mutex;
    std::deque<PendingPresent> m_pending;
};
