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

void clampsZeroCapacityConfiguration()
{
    media_sdk::CpuVideoPicturePool pool({ .capacity = 0, .initialRetained = 4 });

    assert(!pool.acquire(validKey()));
    assert(pool.stats().acquireCount == 1);
}

} // namespace

int main()
{
    rejectsInvalidKeysWithoutRecordingAcquire();
    closeIsIdempotentAndRejectsAcquire();
    clampsZeroCapacityConfiguration();
    return 0;
}
