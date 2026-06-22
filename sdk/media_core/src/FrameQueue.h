#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <utility>

namespace media_sdk {

template<typename T>
class FrameQueue
{
public:
    struct Entry {
        T frame {};
        int serial = 0;
        bool eof = false;
    };

    using WakeCallback = std::function<void()>;

    explicit FrameQueue(std::size_t maxSize)
        : m_maxSize(maxSize)
    {
    }

    bool push(T frame, int serial = 0, bool eof = false)
    {
        WakeCallback wakeCallback;
        {
            std::unique_lock lock(m_mutex);
            m_notFull.wait(lock, [this]()
            {
                return m_abort || m_queue.size() < m_maxSize;
            });
            if (m_abort)
                return false;

            m_queue.push_back({ std::move(frame), serial, eof });
            wakeCallback = m_wakeCallback;
        }
        m_notEmpty.notify_one();
        notifyWakeCallback(wakeCallback);
        return true;
    }

    bool tryPush(T frame, int serial = 0, bool eof = false)
    {
        WakeCallback wakeCallback;
        {
            std::scoped_lock lock(m_mutex);
            if (m_abort || m_queue.size() >= m_maxSize)
                return false;

            m_queue.push_back({ std::move(frame), serial, eof });
            wakeCallback = m_wakeCallback;
        }
        m_notEmpty.notify_one();
        notifyWakeCallback(wakeCallback);
        return true;
    }

    bool pop(Entry& out)
    {
        std::unique_lock lock(m_mutex);
        m_notEmpty.wait(lock, [this]()
        {
            return m_abort || !m_queue.empty();
        });
        if (m_abort && m_queue.empty())
            return false;

        out = std::move(m_queue.front());
        m_queue.pop_front();
        lock.unlock();
        m_notFull.notify_one();
        return true;
    }

    bool tryPop(Entry& out)
    {
        {
            std::scoped_lock lock(m_mutex);
            if (m_queue.empty())
                return false;

            out = std::move(m_queue.front());
            m_queue.pop_front();
        }
        m_notFull.notify_one();
        return true;
    }

    void flush()
    {
        {
            std::scoped_lock lock(m_mutex);
            m_queue.clear();
            ++m_flushSerial;
        }
        m_notFull.notify_all();
        m_notEmpty.notify_all();
    }

    void abort()
    {
        {
            std::scoped_lock lock(m_mutex);
            m_abort = true;
        }
        m_notFull.notify_all();
        m_notEmpty.notify_all();
    }

    void resetAbort()
    {
        std::scoped_lock lock(m_mutex);
        m_abort = false;
    }

    std::size_t size() const
    {
        std::scoped_lock lock(m_mutex);
        return m_queue.size();
    }

    bool empty() const
    {
        std::scoped_lock lock(m_mutex);
        return m_queue.empty();
    }

    int flushSerial() const
    {
        std::scoped_lock lock(m_mutex);
        return m_flushSerial;
    }

    void setWakeCallback(WakeCallback callback)
    {
        std::scoped_lock lock(m_mutex);
        m_wakeCallback = std::move(callback);
    }

private:
    void notifyWakeCallback(const WakeCallback& wakeCallback)
    {
        if (wakeCallback)
            wakeCallback();
    }

    const std::size_t m_maxSize;
    mutable std::mutex m_mutex;
    std::condition_variable m_notEmpty;
    std::condition_variable m_notFull;
    std::deque<Entry> m_queue;
    WakeCallback m_wakeCallback;
    int m_flushSerial = 0;
    bool m_abort = false;
};

} // namespace media_sdk
