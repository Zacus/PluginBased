#pragma once

#include "common/FFmpegUtils.h"

#include <QMutex>
#include <QWaitCondition>
#include <deque>
#include <functional>

/**
 * @brief 线程安全帧队列
 *
 * SDK Qt adapter 生产，渲染侧（VideoRenderer/AudioRenderer）消费。
 * 用双端队列 + mutex + wait condition 实现，简单可靠。
 *
 * 设计原则：
 *   - 队列满时，push 阻塞解码线程（背压），避免无限堆积内存
 *   - 队列空时，pop 阻塞消费线程，避免 CPU 空转
 *   - flush() 清空队列并唤醒所有等待者，用于 seek / stop
 *   - 独立 flush_serial 机制：seek 后递增 serial，消费侧丢弃旧帧
 *
 * @tparam T 帧类型（AVFramePtr 或其他）
 */
template<typename T>
class FrameQueue
{
public:
    struct Entry {
        T       frame;
        int     serial = 0;   // flush serial，用于 seek 后丢弃过期帧
        bool    eof    = false; // 文件结束标记帧
    };

    using WakeCallback = std::function<void()>;

    explicit FrameQueue(int maxSize = 16)
        : m_maxSize(maxSize)
    {}

    // ── 生产者接口（解码线程调用）────────────────────────────────────────────

    /**
     * @brief 推入帧（阻塞直到队列有空位，或 abort 被设置）
     * @return false 表示已 abort，调用方应退出循环
     */
    bool push(T frame, int serial = 0, bool eof = false)
    {
        WakeCallback wakeCallback;
        {
            QMutexLocker lk(&m_mutex);
            const int cancelSerial = m_cancelSerial;
            while (static_cast<int>(m_queue.size()) >= m_maxSize &&
                   !m_abort &&
                   cancelSerial == m_cancelSerial) {
                m_notFull.wait(&m_mutex);
            }
            if (m_abort) return false;
            if (cancelSerial != m_cancelSerial) return false;

            m_queue.push_back({ std::move(frame), serial, eof });
            m_notEmpty.wakeOne();
            wakeCallback = m_wakeCallback;
        }
        notifyWakeCallback(wakeCallback);
        return true;
    }

    bool tryPush(T frame, int serial = 0, bool eof = false)
    {
        WakeCallback wakeCallback;
        {
            QMutexLocker lk(&m_mutex);
            if (static_cast<int>(m_queue.size()) >= m_maxSize)
                return false; // 满了直接返回，不阻塞
            m_queue.push_back({ std::move(frame), serial, eof });
            m_notEmpty.wakeOne();
            wakeCallback = m_wakeCallback;
        }
        notifyWakeCallback(wakeCallback);
        return true;
    }

    /**
     * @brief 标记当前 serial 的生产端已到达流尾。
     *
     * EOF 是 drain marker，必须排在同一 serial 已接受帧之后。正常路径使用
     * 阻塞 push()，让生产端遵守队列背压，而不是在队列满时绕过顺序语义。
     */
    bool finish(int serial = 0)
    {
        return push(T {}, serial, true);
    }

    bool tryFinish(int serial = 0)
    {
        return tryPush(T {}, serial, true);
    }

    // ── 消费者接口（渲染线程调用）────────────────────────────────────────────

    /**
     * @brief 取出帧（阻塞直到队列有帧，或 abort 被设置）
     * @return false 表示已 abort
     */
    bool pop(Entry& out)
    {
        QMutexLocker lk(&m_mutex);
        while (m_queue.empty() && !m_abort)
            m_notEmpty.wait(&m_mutex);
        if (m_abort && m_queue.empty()) return false;

        out = std::move(m_queue.front());
        m_queue.pop_front();
        m_notFull.wakeOne();
        return true;
    }

    /**
     * @brief 非阻塞尝试取帧
     * @return false 表示队列空
     */
    bool tryPop(Entry& out)
    {
        QMutexLocker lk(&m_mutex);
        if (m_queue.empty()) return false;
        out = std::move(m_queue.front());
        m_queue.pop_front();
        m_notFull.wakeOne();
        return true;
    }

    // ── 控制接口 ─────────────────────────────────────────────────────────────

    /**
     * @brief 清空队列（seek/stop 时调用）
     * 唤醒所有等待者，使它们能检查 abort 或 serial
     */
    void flush()
    {
        QMutexLocker lk(&m_mutex);
        m_queue.clear();
        ++m_flushSerial;
        m_notFull.wakeAll();
        m_notEmpty.wakeAll();
    }

    /** 触发终止，所有阻塞的 push/pop 立即返回 false */
    void abort()
    {
        QMutexLocker lk(&m_mutex);
        m_abort = true;
        m_notFull.wakeAll();
        m_notEmpty.wakeAll();
    }

    /**
     * @brief 取消当前 generation 中可能阻塞的生产者，但保持消费者线程存活。
     *
     * seek 会废弃旧 generation 的数据路径。此时被满队列卡住的 push() 必须返回
     * false，让解码线程停止发布旧帧；但 AudioRenderer 这类长期消费者不能像 stop
     * 一样退出线程。
     */
    void cancelPendingPushes()
    {
        QMutexLocker lk(&m_mutex);
        m_queue.clear();
        ++m_cancelSerial;
        ++m_flushSerial;
        m_notFull.wakeAll();
        m_notEmpty.wakeAll();
    }

    void resetAbort()
    {
        QMutexLocker lk(&m_mutex);
        m_abort = false;
    }

    // ── 查询 ─────────────────────────────────────────────────────────────────
    int  size()         const { QMutexLocker lk(&m_mutex); return static_cast<int>(m_queue.size()); }
    bool empty()        const { QMutexLocker lk(&m_mutex); return m_queue.empty(); }
    int  flushSerial()  const { QMutexLocker lk(&m_mutex); return m_flushSerial; }

    void setWakeCallback(WakeCallback callback)
    {
        QMutexLocker lk(&m_mutex);
        m_wakeCallback = std::move(callback);
    }

private:
    void notifyWakeCallback(const WakeCallback& wakeCallback)
    {
        if (wakeCallback)
            wakeCallback();
    }

    mutable QMutex  m_mutex;
    QWaitCondition  m_notEmpty;
    QWaitCondition  m_notFull;
    std::deque<Entry> m_queue;
    WakeCallback m_wakeCallback;
    int  m_maxSize    = 16;
    int  m_flushSerial = 0;
    int  m_cancelSerial = 0;
    bool m_abort      = false;
};

// ── 具体类型别名 ──────────────────────────────────────────────────────────────
using VideoFrameQueue = FrameQueue<AVFramePtr>;
using AudioFrameQueue = FrameQueue<AVFramePtr>;
