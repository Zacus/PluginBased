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

namespace media_sdk {

struct DecoderBufferPoolStats {
    std::uint64_t callbackCount = 0;
    std::uint64_t pooledFrameCount = 0;
    std::uint64_t fallbackCount = 0;
    std::uint64_t poolRebuildCount = 0;
    std::uint64_t planeAcquireCount = 0;
    std::uint64_t planeAllocationCount = 0;
};

class DecoderBufferPool final
{
public:
    DecoderBufferPool() = default;
    ~DecoderBufferPool();

    DecoderBufferPool(const DecoderBufferPool&) = delete;
    DecoderBufferPool& operator=(const DecoderBufferPool&) = delete;
    DecoderBufferPool(DecoderBufferPool&&) = delete;
    DecoderBufferPool& operator=(DecoderBufferPool&&) = delete;

    [[nodiscard]] bool attach(AVCodecContext* context);
    void detach(AVCodecContext* context);
    void close();
    [[nodiscard]] DecoderBufferPoolStats stats() const;

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
    int fallback(AVCodecContext* context, AVFrame* frame, int flags) noexcept;
    static bool supportsContext(const AVCodecContext* context);
    static bool supportsFrame(const AVCodecContext* context, const AVFrame* frame);
    bool rebuildPools(AVCodecContext* context, const AVFrame* frame, PoolSet& replacement);

    mutable std::mutex m_mutex;
    PoolSet m_activePools;
    AVCodecContext* m_attachedContext = nullptr;
    int (*m_previousGetBuffer2)(AVCodecContext*, AVFrame*, int) = nullptr;
    std::atomic_uint64_t m_callbackCount { 0 };
    std::atomic_uint64_t m_pooledFrameCount { 0 };
    std::atomic_uint64_t m_fallbackCount { 0 };
    std::atomic_uint64_t m_poolRebuildCount { 0 };
    std::atomic_uint64_t m_planeAcquireCount { 0 };
    std::atomic_uint64_t m_planeAllocationCount { 0 };
};

} // namespace media_sdk
