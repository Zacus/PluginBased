#pragma once

#include "media_sdk/runtime/VideoPresenter.h"

#include <QPointer>

#include <atomic>
#include <deque>
#include <mutex>

class FFmpegSurface;

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
        media_sdk::VideoFrame frame;
        media_sdk::runtime::PresentTiming timing;
    };

    bool surfaceSupportsNativeRendering(const QPointer<FFmpegSurface>& surface) const;
    void complete(media_sdk::runtime::PresentCompletion completion);

    QPointer<FFmpegSurface> m_surface;
    media_sdk::runtime::IVideoPresenterEvents* m_events = nullptr;
    std::atomic_uint64_t m_nextPresentId { 0 };
    mutable std::mutex m_mutex;
    std::deque<PendingPresent> m_pending;
};
