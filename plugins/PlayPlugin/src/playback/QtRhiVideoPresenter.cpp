#include "playback/QtRhiVideoPresenter.h"

#include "common/FFmpegUtils.h"
#include "video/FFmpegSurface.h"

#include <QMetaObject>
#include <QQuickWindow>
#include <QThread>

#if defined(Q_OS_APPLE)
#include <CoreVideo/CoreVideo.h>
#endif

#include <memory>
#include <mutex>
#include <utility>

struct QtRhiVideoPresenterEventState
{
    std::mutex mutex;
    media_sdk::runtime::IVideoPresenterEvents* events = nullptr;
};

namespace {

bool isBt709(const media_sdk::VideoFrame& frame)
{
    return frame.colorSpace() == media_sdk::ColorSpace::Bt709 ||
        (frame.colorSpace() == media_sdk::ColorSpace::Unknown && frame.width() >= 1280);
}

NativeVideoFrame nativeVideoFrameFrom(const media_sdk::VideoFrame& frame, const AVFrame* avFrame)
{
    NativeVideoFrame native;
    if (frame.pixelFormat() != media_sdk::PixelFormat::Native ||
        frame.nativeHandle().kind != media_sdk::NativeHandleKind::VideoToolboxPixelBuffer) {
        return native;
    }

    native.kind = NativeFrameKind::VideoToolbox;
#if defined(Q_OS_APPLE)
    auto* pixelBuffer = avFrame
        ? reinterpret_cast<CVPixelBufferRef>(avFrame->data[3])
        : static_cast<CVPixelBufferRef>(frame.nativeHandle().handle);
    if (pixelBuffer) {
        const OSType cvFormat = CVPixelBufferGetPixelFormatType(pixelBuffer);
        native.pixelFormat = static_cast<int>(cvFormat);
        native.fullRange =
            cvFormat == kCVPixelFormatType_420YpCbCr8BiPlanarFullRange ||
            cvFormat == kCVPixelFormatType_420YpCbCr10BiPlanarFullRange;
        native.is10bit =
            cvFormat == kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange ||
            cvFormat == kCVPixelFormatType_420YpCbCr10BiPlanarFullRange;
    }
#else
    native.pixelFormat = frame.nativeHandle().pixelFormat;
    native.fullRange = frame.colorRange() == media_sdk::ColorRange::Full;
#endif
    native.bt709 = isBt709(frame);
    if (native.pixelFormat == 0)
        native.pixelFormat = frame.nativeHandle().pixelFormat;
    return native;
}

std::uint64_t counterDelta(std::uint64_t before, std::uint64_t after)
{
    return after >= before ? after - before : 0;
}

media_sdk::runtime::PresentDiagnostics diagnosticsDelta(
    const FFmpegSurfaceDiagnostics& before,
    const FFmpegSurfaceDiagnostics& after)
{
    return {
        .nativeTextureCreated = counterDelta(before.nativeTextureCreated, after.nativeTextureCreated),
        .nativeTextureFailed = 0,
        .nativeTextureDrawn = counterDelta(before.nativeTextureDrawn, after.nativeTextureDrawn),
        .cpuCopied = 0,
        .cpuTransferred = counterDelta(before.cpuTransferred, after.cpuTransferred),
        .cpuMemcpy = counterDelta(before.cpuMemcpy, after.cpuMemcpy),
    };
}

void dispatchCompletion(
    const std::weak_ptr<QtRhiVideoPresenterEventState>& weakState,
    media_sdk::runtime::PresentCompletion completion)
{
    const auto state = weakState.lock();
    if (!state)
        return;

    media_sdk::runtime::IVideoPresenterEvents* events = nullptr;
    {
        std::lock_guard lock(state->mutex);
        events = state->events;
    }

    if (events)
        events->onPresentComplete(std::move(completion));
}

} // namespace

QtRhiVideoPresenter::QtRhiVideoPresenter(FFmpegSurface* surface)
    : m_surface(surface)
    , m_eventState(std::make_shared<QtRhiVideoPresenterEventState>())
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
    std::lock_guard lock(m_eventState->mutex);
    m_eventState->events = events;
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

    auto surfaceFrame = makeSurfaceFrame(frame);
    if (!surfaceFrame) {
        complete(media_sdk::runtime::PresentCompletion {
            id,
            media_sdk::runtime::PresentStatus::Failed,
            "SDK video frame does not contain cloneable FFmpeg frame storage",
        });
        return { id, media_sdk::runtime::PresentStatus::Failed };
    }

    {
        std::lock_guard lock(m_mutex);
        m_pending.clear();
        m_pending.push_back(PendingPresent { id, std::move(frame), surfaceFrame, timing });
    }

    const auto beforeDiagnostics = surface->diagnosticsSnapshot();
    const std::weak_ptr<QtRhiVideoPresenterEventState> weakEventState = m_eventState;
    QMetaObject::invokeMethod(surface, [surface, surfaceFrame, id, weakEventState, beforeDiagnostics]() {
        if (surface) {
            surface->onFrameReady(surfaceFrame);

            auto* window = surface->window();
            if (!window) {
                const auto afterDiagnostics = surface->diagnosticsSnapshot();
                dispatchCompletion(weakEventState, {
                    .id = id,
                    .status = media_sdk::runtime::PresentStatus::Presented,
                    .detail = {},
                    .diagnostics = diagnosticsDelta(beforeDiagnostics, afterDiagnostics),
                });
                return;
            }

            QObject::connect(
                window,
                &QQuickWindow::afterRendering,
                surface,
                [surface, id, weakEventState, beforeDiagnostics]() {
                    if (!surface)
                        return;
                    const auto afterDiagnostics = surface->diagnosticsSnapshot();
                    dispatchCompletion(weakEventState, {
                        .id = id,
                        .status = media_sdk::runtime::PresentStatus::Presented,
                        .detail = {},
                        .diagnostics = diagnosticsDelta(beforeDiagnostics, afterDiagnostics),
                    });
                },
                static_cast<Qt::ConnectionType>(Qt::DirectConnection | Qt::SingleShotConnection));
        }
    }, Qt::QueuedConnection);

    return { id, media_sdk::runtime::PresentStatus::Queued };
}

bool QtRhiVideoPresenter::surfaceSupportsNativeRendering(
    const QPointer<FFmpegSurface>& surface) const
{
    if (!surface)
        return false;

    if (surface->thread() == QThread::currentThread())
        return surface->supportsNativeVideoToolboxRendering();

    bool supported = false;
    const bool invoked = QMetaObject::invokeMethod(
        surface,
        [&surface, &supported]() {
            if (surface)
                supported = surface->supportsNativeVideoToolboxRendering();
        },
        Qt::BlockingQueuedConnection);
    return invoked && supported;
}

VideoFrameDataPtr QtRhiVideoPresenter::makeSurfaceFrame(const media_sdk::VideoFrame& frame) const
{
    const auto storage = frame.storage();
    if (!storage)
        return {};

    auto sourceFrame = std::static_pointer_cast<AVFrame>(storage);
    if (!sourceFrame)
        return {};

    AVFramePtr clonedFrame(av_frame_clone(sourceFrame.get()));
    if (!clonedFrame)
        return {};

    const NativeVideoFrame native = nativeVideoFrameFrom(frame, clonedFrame.get());
    const bool fullRange = native.isValid()
        ? native.fullRange
        : frame.colorRange() == media_sdk::ColorRange::Full;
    const bool bt709 = native.isValid() ? native.bt709 : isBt709(frame);
    return make_video_frame(std::move(clonedFrame), fullRange, bt709, native);
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
    dispatchCompletion(m_eventState, std::move(completion));
}
