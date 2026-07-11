#pragma once

#include "media_sdk/Error.h"
#include "media_sdk/Frame.h"
#include "media_sdk/Result.h"
#include "media_sdk/runtime/AudioOutput.h"

namespace media_sdk::session {

struct ConvertedAudioFrame {
    AudioFrame frame;
    runtime::AudioSampleFormat runtimeFormat = runtime::AudioSampleFormat::Unknown;
};

[[nodiscard("audio conversion can fail for unsupported SDK sample formats")]]
Result<ConvertedAudioFrame> convertToRuntimeAudioFrame(const AudioFrame& frame);

} // namespace media_sdk::session
