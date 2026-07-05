#pragma once

namespace media_sdk {

struct PlayerConfig {
    bool enableHardwareDecode = true;
    bool preferNativeVideoFrames = true;
    int videoQueueCapacity = 30;
    int audioQueueCapacity = 64;
    int accurateSeekMaxDiscardedVideoFrames = 300;
    int accurateSeekMaxDiscardedAudioFrames = 1000;
};

} // namespace media_sdk
