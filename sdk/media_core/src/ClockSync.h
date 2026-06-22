#pragma once

#include <atomic>
#include <chrono>
#include <mutex>
#include <optional>

namespace media_sdk {

class ClockSync
{
public:
    using Clock = std::chrono::steady_clock;

    ClockSync();

    void setAudioClock(std::chrono::microseconds position);
    void invalidate();
    void setPaused(bool paused);

    std::optional<std::chrono::microseconds> currentTime() const;

private:
    struct Snapshot {
        std::chrono::microseconds base { 0 };
        Clock::time_point anchor {};
        bool valid = false;
        bool paused = false;
    };

    mutable std::mutex m_mutex;
    Snapshot m_snapshot;
};

} // namespace media_sdk
