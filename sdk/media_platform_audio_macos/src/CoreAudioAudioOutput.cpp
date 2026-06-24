#include "media_sdk/platform/macos/CoreAudioAudioOutput.h"

#include "CoreAudioRingBuffer.h"
#include "media_sdk/Error.h"

#include <algorithm>
#include <AudioToolbox/AudioToolbox.h>
#include <array>
#include <atomic>
#include <chrono>
#include <mutex>

namespace media_sdk::platform::macos {
namespace {

Result<void> stateError(const char* message)
{
    return Result<void>::failure({
        .code = MediaErrorCode::InternalStateError,
        .message = message,
        .detail = {},
    });
}

Result<void> unsupportedFormat(const char* message)
{
    return Result<void>::failure({
        .code = MediaErrorCode::UnsupportedFormat,
        .message = message,
        .detail = {},
    });
}

} // namespace

struct CoreAudioAudioOutput::Impl {
    static constexpr std::size_t BufferCount = 3;

    ~Impl()
    {
        closeQueue();
    }

    static void outputCallback(
        void* userData,
        AudioQueueRef queue,
        AudioQueueBufferRef buffer)
    {
        auto* impl = static_cast<Impl*>(userData);
        if (!impl)
            return;

        std::span<std::byte> destination {
            static_cast<std::byte*>(buffer->mAudioData),
            buffer->mAudioDataBytesCapacity,
        };
        impl->ringBuffer.read(destination);
        buffer->mAudioDataByteSize = buffer->mAudioDataBytesCapacity;

        if (impl->acceptingCallbacks.load(std::memory_order_acquire))
            AudioQueueEnqueueBuffer(queue, buffer, 0, nullptr);
    }

    void closeQueue()
    {
        acceptingCallbacks.store(false, std::memory_order_release);
        if (queue) {
            AudioQueueStop(queue, true);
            AudioQueueDispose(queue, true);
            queue = nullptr;
        }
        buffers.fill(nullptr);
        queueStarted = false;
    }

    bool enqueueInitialBuffers()
    {
        if (!queue)
            return false;

        for (AudioQueueBufferRef buffer : buffers) {
            if (!buffer)
                return false;

            std::span<std::byte> destination {
                static_cast<std::byte*>(buffer->mAudioData),
                buffer->mAudioDataBytesCapacity,
            };
            ringBuffer.read(destination);
            buffer->mAudioDataByteSize = buffer->mAudioDataBytesCapacity;
            if (AudioQueueEnqueueBuffer(queue, buffer, 0, nullptr) != noErr)
                return false;
        }
        return true;
    }

    mutable std::mutex mutex;
    runtime::AudioFormat format {};
    CoreAudioRingBuffer ringBuffer { 48000 * 2 * 4 };
    AudioQueueRef queue = nullptr;
    std::array<AudioQueueBufferRef, BufferCount> buffers {};
    std::uint32_t bufferByteSize = 0;
    std::uint64_t generation = 1;
    std::atomic_bool acceptingCallbacks { false };
    bool open = false;
    bool paused = false;
    bool queueStarted = false;
};

CoreAudioAudioOutput::CoreAudioAudioOutput()
    : m_impl(std::make_unique<Impl>())
{
}

CoreAudioAudioOutput::~CoreAudioAudioOutput() = default;

Result<void> CoreAudioAudioOutput::open(const runtime::AudioFormat& format)
{
    if (format.sampleRate <= 0 || format.channels <= 0)
        return stateError("CoreAudioAudioOutput requires a valid audio format");
    if (format.sampleFormat != runtime::AudioSampleFormat::Float32)
        return unsupportedFormat("CoreAudioAudioOutput currently requires interleaved float32 audio");

    close();
    std::scoped_lock lock(m_impl->mutex);
    m_impl->format = format;
    m_impl->generation = 1;
    m_impl->ringBuffer.configure(format, m_impl->generation);

    AudioStreamBasicDescription streamDescription {};
    streamDescription.mSampleRate = format.sampleRate;
    streamDescription.mFormatID = kAudioFormatLinearPCM;
    streamDescription.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    streamDescription.mBytesPerPacket = static_cast<UInt32>(format.channels * sizeof(float));
    streamDescription.mFramesPerPacket = 1;
    streamDescription.mBytesPerFrame = static_cast<UInt32>(format.channels * sizeof(float));
    streamDescription.mChannelsPerFrame = static_cast<UInt32>(format.channels);
    streamDescription.mBitsPerChannel = 8 * sizeof(float);

    const auto newQueueStatus = AudioQueueNewOutput(
        &streamDescription,
        &Impl::outputCallback,
        m_impl.get(),
        nullptr,
        nullptr,
        0,
        &m_impl->queue);
    if (newQueueStatus != noErr) {
        m_impl->ringBuffer.close();
        return stateError("CoreAudioAudioOutput failed to create AudioQueue output");
    }

    constexpr int targetBufferMilliseconds = 10;
    const auto framesPerBuffer = std::max(1, format.sampleRate * targetBufferMilliseconds / 1000);
    m_impl->bufferByteSize = static_cast<std::uint32_t>(
        framesPerBuffer * format.channels * static_cast<int>(sizeof(float)));
    for (AudioQueueBufferRef& buffer : m_impl->buffers) {
        if (AudioQueueAllocateBuffer(m_impl->queue, m_impl->bufferByteSize, &buffer) != noErr) {
            m_impl->closeQueue();
            m_impl->ringBuffer.close();
            return stateError("CoreAudioAudioOutput failed to allocate AudioQueue buffer");
        }
    }

    m_impl->open = true;
    m_impl->paused = false;
    m_impl->queueStarted = false;
    return Result<void>::success();
}

Result<void> CoreAudioAudioOutput::write(runtime::AudioBufferView buffer)
{
    {
        std::scoped_lock lock(m_impl->mutex);
        if (!m_impl->open)
            return stateError("CoreAudioAudioOutput is not open");
    }

    if (!m_impl->ringBuffer.write(buffer))
        return stateError("CoreAudioAudioOutput rejected audio buffer");

    return Result<void>::success();
}

runtime::ClockSnapshot CoreAudioAudioOutput::clock() const
{
    std::scoped_lock lock(m_impl->mutex);
    auto snapshot = m_impl->ringBuffer.clock();
    snapshot.paused = m_impl->paused;
    return snapshot;
}

void CoreAudioAudioOutput::pause()
{
    std::scoped_lock lock(m_impl->mutex);
    m_impl->paused = true;
    if (m_impl->queue && m_impl->queueStarted)
        AudioQueuePause(m_impl->queue);
}

void CoreAudioAudioOutput::resume()
{
    std::scoped_lock lock(m_impl->mutex);
    if (!m_impl->open)
        return;

    m_impl->paused = false;
    if (m_impl->queue && !m_impl->queueStarted) {
        m_impl->acceptingCallbacks.store(true, std::memory_order_release);
        if (m_impl->enqueueInitialBuffers()
            && AudioQueueStart(m_impl->queue, nullptr) == noErr) {
            m_impl->queueStarted = true;
        } else {
            m_impl->acceptingCallbacks.store(false, std::memory_order_release);
        }
    } else if (m_impl->queue) {
        AudioQueueStart(m_impl->queue, nullptr);
    }
}

void CoreAudioAudioOutput::flush()
{
    std::scoped_lock lock(m_impl->mutex);
    ++m_impl->generation;
    m_impl->ringBuffer.flush();
    if (m_impl->queue)
        AudioQueueReset(m_impl->queue);
}

void CoreAudioAudioOutput::close()
{
    std::scoped_lock lock(m_impl->mutex);
    m_impl->closeQueue();
    m_impl->open = false;
    m_impl->paused = false;
    m_impl->ringBuffer.close();
}

} // namespace media_sdk::platform::macos
