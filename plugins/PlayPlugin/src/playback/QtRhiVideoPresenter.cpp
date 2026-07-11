#include "playback/QtRhiVideoPresenter.h"

#include "playback/SdkVideoFrameBridge.h"
#include "video/FFmpegSurface.h"

#include <QThread>

#include <memory>
#include <mutex>
#include <utility>

QtRhiVideoPresenter::QtRhiVideoPresenter(FFmpegSurface* surface)
    : m_surface(surface)
{
    if (surface && surface->thread() == QThread::currentThread())
        m_nativeRenderingSupported.store(surface->supportsNativeVideoToolboxRendering(),
                                         std::memory_order_relaxed);
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
    Q_UNUSED(events);
}

media_sdk::runtime::PresentResult QtRhiVideoPresenter::present(
    media_sdk::VideoFrame frame,
    media_sdk::runtime::PresentTiming timing)
{
    const auto id = static_cast<media_sdk::runtime::PresentId>(
        m_nextPresentId.fetch_add(1, std::memory_order_relaxed) + 1);
    const QPointer<FFmpegSurface> surface = m_surface;

    if (!surface) {
        return { id, media_sdk::runtime::PresentStatus::Failed };
    }

    if (frame.pixelFormat() == media_sdk::PixelFormat::Native &&
        frame.nativeHandle().kind == media_sdk::NativeHandleKind::VideoToolboxPixelBuffer &&
        !surfaceSupportsNativeRendering(surface)) {
        return { id, media_sdk::runtime::PresentStatus::UnsupportedNativeHandle };
    }

    auto surfaceFrame = makeSurfaceFrame(frame);
    if (!surfaceFrame) {
        return { id, media_sdk::runtime::PresentStatus::Failed };
    }

    {
        std::lock_guard lock(m_mutex);
        m_pending.clear();
        m_pending.push_back(PendingPresent { id, std::move(frame), surfaceFrame, timing });
    }

    // Qt 的真实绘制由 Scene Graph 异步调度，切换应用/Space 时可能暂停或降频。
    // 对 SDK runtime 来说，presenter 已经接收并缓存最新帧，不能再用 Qt 绘制回调反压解码和音频。
    surface->onFrameReady(surfaceFrame);
    return { id, media_sdk::runtime::PresentStatus::Presented };
}

bool QtRhiVideoPresenter::surfaceSupportsNativeRendering(
    const QPointer<FFmpegSurface>& surface) const
{
    if (!surface)
        return false;

    if (surface->thread() == QThread::currentThread()) {
        const bool supported = surface->supportsNativeVideoToolboxRendering();
        m_nativeRenderingSupported.store(supported, std::memory_order_relaxed);
        return supported;
    }

    return m_nativeRenderingSupported.load(std::memory_order_relaxed);
}

VideoFrameDataPtr QtRhiVideoPresenter::makeSurfaceFrame(const media_sdk::VideoFrame& frame) const
{
    return makeVideoFrameDataFromSdk(frame);
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
