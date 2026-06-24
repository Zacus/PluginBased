#include "playback/QtRhiVideoPresenter.h"

#include "video/FFmpegSurface.h"

#include <QMetaObject>
#include <QThread>

#include <utility>

QtRhiVideoPresenter::QtRhiVideoPresenter(FFmpegSurface* surface)
    : m_surface(surface)
{
}

QtRhiVideoPresenter::~QtRhiVideoPresenter()
{
    clear();
    setEvents(nullptr);
}

media_sdk::runtime::VideoPresenterCapabilities QtRhiVideoPresenter::capabilities() const
{
    media_sdk::runtime::VideoPresenterCapabilities result;
    const QPointer<FFmpegSurface> surface = m_surface;
    result.supportsVideoToolboxPixelBuffer = surfaceSupportsNativeRendering(surface);
    result.supportsCpuYuv = true;
    result.asyncPresent = true;
    result.maxPendingFrames = 1;
    return result;
}

void QtRhiVideoPresenter::setEvents(media_sdk::runtime::IVideoPresenterEvents* events)
{
    std::lock_guard lock(m_mutex);
    m_events = events;
}

media_sdk::runtime::PresentResult QtRhiVideoPresenter::present(
    media_sdk::VideoFrame frame,
    media_sdk::runtime::PresentTiming timing)
{
    const auto id = static_cast<media_sdk::runtime::PresentId>(
        m_nextPresentId.fetch_add(1, std::memory_order_relaxed) + 1);
    const QPointer<FFmpegSurface> surface = m_surface;

    if (!surface) {
        complete(media_sdk::runtime::PresentCompletion {
            id,
            media_sdk::runtime::PresentStatus::Failed,
            "Qt video surface is no longer available",
        });
        return { id, media_sdk::runtime::PresentStatus::Failed };
    }

    if (frame.pixelFormat() == media_sdk::PixelFormat::Native &&
        frame.nativeHandle().kind == media_sdk::NativeHandleKind::VideoToolboxPixelBuffer &&
        !surfaceSupportsNativeRendering(surface)) {
        complete(media_sdk::runtime::PresentCompletion {
            id,
            media_sdk::runtime::PresentStatus::UnsupportedNativeHandle,
            "Qt scene graph is not using the Metal RHI native VideoToolbox path",
        });
        return { id, media_sdk::runtime::PresentStatus::UnsupportedNativeHandle };
    }

    {
        std::lock_guard lock(m_mutex);
        m_pending.clear();
        m_pending.push_back(PendingPresent { id, std::move(frame), timing });
    }

    QMetaObject::invokeMethod(surface, [surface]() {
        if (surface) {
            surface->update();
        }
    }, Qt::QueuedConnection);

    return { id, media_sdk::runtime::PresentStatus::Queued };
}

bool QtRhiVideoPresenter::surfaceSupportsNativeRendering(
    const QPointer<FFmpegSurface>& surface) const
{
    if (!surface || surface->thread() != QThread::currentThread()) {
        return false;
    }
    return surface->supportsNativeVideoToolboxRendering();
}

void QtRhiVideoPresenter::clear()
{
    {
        std::lock_guard lock(m_mutex);
        m_pending.clear();
    }

    const QPointer<FFmpegSurface> surface = m_surface;
    if (!surface) {
        return;
    }

    QMetaObject::invokeMethod(surface, [surface]() {
        if (surface) {
            surface->clear();
        }
    }, Qt::QueuedConnection);
}

void QtRhiVideoPresenter::complete(media_sdk::runtime::PresentCompletion completion)
{
    media_sdk::runtime::IVideoPresenterEvents* events = nullptr;
    {
        std::lock_guard lock(m_mutex);
        events = m_events;
    }

    if (events) {
        events->onPresentComplete(std::move(completion));
    }
}
