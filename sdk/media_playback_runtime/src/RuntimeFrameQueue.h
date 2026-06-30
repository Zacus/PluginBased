#pragma once

#include "media_sdk/runtime/RuntimeTypes.h"

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <utility>

namespace media_sdk::runtime {

template<typename FrameType>
class RuntimeFrameQueue
{
public:
    enum class PushResult {
        Accepted,
        RejectedGeneration,
        Closed,
        Aborted
    };

    enum class PopResult {
        Frame,
        EndOfStream,
        Aborted,
        Closed
    };

    explicit RuntimeFrameQueue(std::size_t capacity)
        : m_capacity(capacity == 0 ? 1 : capacity)
    {
    }

    void reset(SessionId sessionId, Generation generation)
    {
        {
            std::scoped_lock lock(m_mutex);
            m_sessionId = sessionId;
            m_generation = generation;
            m_queue.clear();
            m_aborted = false;
            m_closed = false;
        }
        m_notEmpty.notify_all();
        m_notFull.notify_all();
    }

    [[nodiscard("push result determines whether a frame was accepted, stale, closed, or aborted")]]
    PushResult push(FrameType frame)
    {
        return pushEntry(Entry { std::move(frame), false });
    }

    [[nodiscard("end-of-stream publication can be rejected by stale generations, close, or abort")]]
    PushResult pushEndOfStream(SessionId sessionId, Generation generation)
    {
        FrameType frame;
        frame.sessionId = sessionId;
        frame.generation = generation;
        frame.endOfStream = true;
        return pushEntry(Entry { std::move(frame), true });
    }

    [[nodiscard("pop result distinguishes frames, EOF, abort, and close")]]
    PopResult waitPop(FrameType& frame)
    {
        std::unique_lock lock(m_mutex);
        m_notEmpty.wait(lock, [this]()
        {
            return m_aborted || m_closed || !m_queue.empty();
        });

        if (m_aborted)
            return PopResult::Aborted;

        if (m_queue.empty())
            return PopResult::Closed;

        Entry entry = std::move(m_queue.front());
        m_queue.pop_front();
        lock.unlock();
        m_notFull.notify_one();

        if (entry.endOfStream) {
            frame = std::move(entry.frame);
            return PopResult::EndOfStream;
        }

        frame = std::move(entry.frame);
        return PopResult::Frame;
    }

    void abort()
    {
        {
            std::scoped_lock lock(m_mutex);
            m_queue.clear();
            m_aborted = true;
            ++m_abortCount;
        }
        m_notEmpty.notify_all();
        m_notFull.notify_all();
    }

    void finish()
    {
        {
            std::scoped_lock lock(m_mutex);
            m_closed = true;
        }
        m_notEmpty.notify_all();
        m_notFull.notify_all();
    }

    [[nodiscard]]
    std::size_t size() const
    {
        std::scoped_lock lock(m_mutex);
        return m_queue.size();
    }

    [[nodiscard]]
    Generation generation() const
    {
        std::scoped_lock lock(m_mutex);
        return m_generation;
    }

    [[nodiscard]]
    std::uint64_t abortCount() const
    {
        std::scoped_lock lock(m_mutex);
        return m_abortCount;
    }

private:
    struct Entry {
        FrameType frame;
        bool endOfStream = false;
    };

    [[nodiscard]]
    PushResult pushEntry(Entry entry)
    {
        std::unique_lock lock(m_mutex);
        if (entry.frame.sessionId != m_sessionId || entry.frame.generation != m_generation)
            return PushResult::RejectedGeneration;

        m_notFull.wait(lock, [this]()
        {
            return m_aborted || m_closed || m_queue.size() < m_capacity;
        });

        if (m_aborted)
            return PushResult::Aborted;

        if (m_closed)
            return PushResult::Closed;

        m_queue.push_back(std::move(entry));
        lock.unlock();
        m_notEmpty.notify_one();
        return PushResult::Accepted;
    }

    const std::size_t m_capacity;
    mutable std::mutex m_mutex;
    std::condition_variable m_notEmpty;
    std::condition_variable m_notFull;
    std::deque<Entry> m_queue;
    SessionId m_sessionId = 0;
    Generation m_generation = 0;
    std::uint64_t m_abortCount = 0;
    bool m_aborted = false;
    bool m_closed = false;
};

} // namespace media_sdk::runtime
