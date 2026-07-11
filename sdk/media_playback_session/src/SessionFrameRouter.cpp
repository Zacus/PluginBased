#include "SessionFrameRouter.h"

namespace media_sdk::session {

DecodeFramePushResult mapRuntimeFramePushResult(runtime::RuntimeFramePushResult result)
{
    using DecodeStatus = DecodeFramePushStatus;
    using RuntimeStatus = runtime::RuntimeFramePushStatus;

    switch (result.status) {
    case RuntimeStatus::Accepted:
        return { .status = DecodeStatus::Accepted, .waitTime = result.waitTime };
    case RuntimeStatus::Backpressured:
        return { .status = DecodeStatus::Backpressured, .waitTime = result.waitTime };
    case RuntimeStatus::RejectedGeneration:
        return { .status = DecodeStatus::StaleGeneration, .waitTime = result.waitTime };
    case RuntimeStatus::Cancelled:
        return { .status = DecodeStatus::Cancelled, .waitTime = result.waitTime };
    case RuntimeStatus::Closed:
        return { .status = DecodeStatus::Closed, .waitTime = result.waitTime };
    }

    return { .status = DecodeStatus::Closed, .waitTime = result.waitTime };
}

} // namespace media_sdk::session
