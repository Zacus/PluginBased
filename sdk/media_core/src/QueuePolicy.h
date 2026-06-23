#pragma once

namespace media_sdk {

class QueuePolicy
{
public:
    static bool shouldDropVideoWhenFull(bool hasAudioStream)
    {
        return hasAudioStream;
    }
};

} // namespace media_sdk
