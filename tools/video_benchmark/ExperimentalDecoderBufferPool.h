#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/buffer.h>
}

namespace media_benchmark {

struct ExperimentalDecoderBufferPoolStats {
    std::uint64_t callbackCount = 0;
    std::uint64_t prototypeFrameCount = 0;
    std::uint64_t fallbackCount = 0;
    std::uint64_t poolRebuildCount = 0;
    std::uint64_t planeAcquireCount = 0;
    std::uint64_t planeAllocationCount = 0;
};

class ExperimentalDecoderBufferPool final
{
public:
    explicit ExperimentalDecoderBufferPool(bool prototypeEnabled);
    ~ExperimentalDecoderBufferPool();

    ExperimentalDecoderBufferPool(const ExperimentalDecoderBufferPool&) = delete;
    ExperimentalDecoderBufferPool& operator=(const ExperimentalDecoderBufferPool&) = delete;

    bool attach(AVCodecContext* context);
    void detach(AVCodecContext* context);
    void close();
    [[nodiscard]] ExperimentalDecoderBufferPoolStats stats() const;

private:
    struct FormatKey {
        AVPixelFormat format = AV_PIX_FMT_NONE;
        int width = 0;
        int height = 0;

        friend bool operator==(const FormatKey&, const FormatKey&) = default;
    };

    struct PoolSet {
        PoolSet() = default;
        ~PoolSet();
        PoolSet(const PoolSet&) = delete;
        PoolSet& operator=(const PoolSet&) = delete;
        PoolSet(PoolSet&& other) noexcept;
        PoolSet& operator=(PoolSet&& other) noexcept;

        void clear();

        FormatKey key;
        std::array<AVBufferPool*, 4> pools {};
        std::array<int, 4> linesizes {};
        bool valid = false;
    };

    static int getBufferCallback(AVCodecContext* context, AVFrame* frame, int flags) noexcept;
    static AVBufferRef* allocatePlane(void* opaque, std::size_t size) noexcept;

    int getBuffer(AVCodecContext* context, AVFrame* frame, int flags);
    int fallback(AVCodecContext* context, AVFrame* frame, int flags);
    bool eligible(const AVCodecContext* context, const AVFrame* frame) const;
    bool rebuildPools(AVCodecContext* context, const AVFrame* frame, PoolSet& replacement);

    bool m_prototypeEnabled = false;
    mutable std::mutex m_mutex;
    PoolSet m_activePools;
    AVCodecContext* m_attachedContext = nullptr;
    int (*m_previousGetBuffer2)(AVCodecContext*, AVFrame*, int) = nullptr;
    void* m_previousOpaque = nullptr;
    std::atomic_uint64_t m_callbackCount { 0 };
    std::atomic_uint64_t m_prototypeFrameCount { 0 };
    std::atomic_uint64_t m_fallbackCount { 0 };
    std::atomic_uint64_t m_poolRebuildCount { 0 };
    std::atomic_uint64_t m_planeAcquireCount { 0 };
    std::atomic_uint64_t m_planeAllocationCount { 0 };
};

} // namespace media_benchmark
