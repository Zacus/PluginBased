#pragma once

#include <chrono>
#include <cstdint>

namespace media_sdk::runtime {

inline constexpr double kDefaultPlaybackRate = 1.0;
inline constexpr double kMinimumPlaybackRate = 0.5;
inline constexpr double kMaximumPlaybackRate = 2.0;
inline constexpr std::uint32_t kPlaybackRateScale = 1000000;

[[nodiscard]] bool isPlaybackRateSupported(double playbackRate) noexcept;
[[nodiscard]] bool playbackRatesEqual(double lhs, double rhs) noexcept;

// Converts a validated rate to fixed-point millionths for callback-safe clock math.
[[nodiscard]] std::uint32_t playbackRateMillionths(double playbackRate) noexcept;

[[nodiscard]] std::chrono::microseconds mediaDurationForPlaybackDuration(
    std::chrono::microseconds playbackDuration,
    double playbackRate) noexcept;
[[nodiscard]] std::chrono::microseconds playbackDurationForMediaDuration(
    std::chrono::microseconds mediaDuration,
    double playbackRate) noexcept;

} // namespace media_sdk::runtime
