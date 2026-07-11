#pragma once

#include "common/FFmpegUtils.h"
#include "media_sdk/Frame.h"

[[nodiscard]] VideoFrameDataPtr makeVideoFrameDataFromSdk(
    const media_sdk::VideoFrame& frame);
