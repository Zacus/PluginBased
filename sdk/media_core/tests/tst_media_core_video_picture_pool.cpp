#include "CpuVideoPicturePool.h"

#include <cassert>

namespace {

media_sdk::VideoPictureKey validKey()
{
    return {
        .width = 64,
        .height = 48,
        .pixelFormat = AV_PIX_FMT_YUV420P,
        .alignment = 32,
    };
}

void rejectsInvalidKeysWithoutRecordingAcquire()
{
    media_sdk::CpuVideoPicturePool pool;

    assert(!pool.acquire({}));
    assert(pool.stats().acquireCount == 0);
}

void closeIsIdempotentAndRejectsAcquire()
{
    media_sdk::CpuVideoPicturePool pool;

    pool.close();
    pool.close();

    assert(!pool.acquire(validKey()));
    assert(pool.stats().acquireCount == 0);
}

void allocatesAndReusesFrameStorage()
{
    media_sdk::CpuVideoPicturePool pool({ .capacity = 12, .initialRetained = 1 });
    auto first = pool.acquire(validKey());
    assert(first);
    assert(first->width == 64);
    assert(first->height == 48);
    assert(first->format == AV_PIX_FMT_YUV420P);
    assert(av_frame_is_writable(first.get()) != 0);
    AVFrame* firstFrame = first.get();
    std::uint8_t* firstData = first->data[0];

    first.reset();
    auto second = pool.acquire(validKey());

    assert(second);
    assert(second.get() == firstFrame);
    assert(second->data[0] == firstData);
    const auto stats = pool.stats();
    assert(stats.allocationCount == 1);
    assert(stats.reuseCount == 1);
    assert(stats.recycleCount == 1);
    assert(stats.retainedCount == 1);
    assert(stats.inFlightCount == 1);
}

void sharedReferencesDelayRecycle()
{
    media_sdk::CpuVideoPicturePool pool({ .capacity = 2, .initialRetained = 0 });
    auto first = pool.acquire(validKey());
    auto held = first;
    AVFrame* firstFrame = first.get();

    first.reset();
    assert(pool.stats().recycleCount == 0);

    auto second = pool.acquire(validKey());
    assert(second);
    assert(second.get() != firstFrame);
    assert(pool.stats().reuseCount == 0);

    held.reset();
    assert(pool.stats().recycleCount == 1);
}

void resetsPerFrameMetadataOnRecycle()
{
    media_sdk::CpuVideoPicturePool pool;
    auto frame = pool.acquire(validKey());
    assert(frame);
    frame->pts = 1234;
    frame->pkt_dts = 1200;
    frame->duration = 40;
    frame->color_range = AVCOL_RANGE_JPEG;
    frame->colorspace = AVCOL_SPC_BT709;
    frame->crop_top = 2;
    frame->flags = AV_FRAME_FLAG_KEY;

    frame.reset();
    auto reused = pool.acquire(validKey());

    assert(reused);
    assert(reused->pts == AV_NOPTS_VALUE);
    assert(reused->pkt_dts == AV_NOPTS_VALUE);
    assert(reused->duration == 0);
    assert(reused->color_range == AVCOL_RANGE_UNSPECIFIED);
    assert(reused->colorspace == AVCOL_SPC_UNSPECIFIED);
    assert(reused->crop_top == 0);
    assert(reused->flags == 0);
}

void prewarmsInitialRetainedFrames()
{
    media_sdk::CpuVideoPicturePool pool({ .capacity = 4, .initialRetained = 3 });

    auto frame = pool.acquire(validKey());

    assert(frame);
    const auto stats = pool.stats();
    assert(stats.allocationCount == 3);
    assert(stats.retainedCount == 3);
    assert(stats.inFlightCount == 1);
}

void capacityExhaustionUsesTransientFrame()
{
    media_sdk::CpuVideoPicturePool pool({ .capacity = 1, .initialRetained = 1 });
    auto retained = pool.acquire(validKey());
    auto transient = pool.acquire(validKey());

    assert(retained);
    assert(transient);
    assert(retained.get() != transient.get());
    const auto stats = pool.stats();
    assert(stats.retainedCount == 1);
    assert(stats.transientAllocationCount == 1);
    assert(stats.inFlightCount == 2);

    transient.reset();
    assert(pool.stats().retainedCount == 1);
    assert(pool.stats().inFlightCount == 1);
}

void formatChangeRetiresOldEpochFrames()
{
    media_sdk::CpuVideoPicturePool pool({ .capacity = 2, .initialRetained = 1 });
    auto oldFrame = pool.acquire(validKey());
    auto newKey = validKey();
    newKey.width = 128;
    auto newFrame = pool.acquire(newKey);

    assert(oldFrame);
    assert(newFrame);
    assert(newFrame->width == 128);
    assert(pool.stats().retainedCount == 2);

    oldFrame.reset();
    const auto stats = pool.stats();
    assert(stats.incompatibleReturnCount == 1);
    assert(stats.retainedCount == 1);
}

void closeReleasesIdleAndDoesNotWaitForInFlightFrame()
{
    media_sdk::CpuVideoPicturePool pool({ .capacity = 2, .initialRetained = 2 });
    auto inFlight = pool.acquire(validKey());
    assert(inFlight);
    assert(pool.stats().retainedCount == 2);

    pool.close();

    assert(pool.stats().retainedCount == 1);
    assert(!pool.acquire(validKey()));
    inFlight.reset();
    assert(pool.stats().retainedCount == 0);
    assert(pool.stats().inFlightCount == 0);
}

void inFlightFrameOutlivesPoolFacade()
{
    media_sdk::VideoPictureRef inFlight;
    {
        media_sdk::CpuVideoPicturePool pool;
        inFlight = pool.acquire(validKey());
        assert(inFlight);
    }

    assert(inFlight->data[0] != nullptr);
    inFlight.reset();
}

} // namespace

int main()
{
    rejectsInvalidKeysWithoutRecordingAcquire();
    closeIsIdempotentAndRejectsAcquire();
    allocatesAndReusesFrameStorage();
    sharedReferencesDelayRecycle();
    resetsPerFrameMetadataOnRecycle();
    prewarmsInitialRetainedFrames();
    capacityExhaustionUsesTransientFrame();
    formatChangeRetiresOldEpochFrames();
    closeReleasesIdleAndDoesNotWaitForInFlightFrame();
    inFlightFrameOutlivesPoolFacade();
    return 0;
}
