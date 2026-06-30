#pragma once

#include <chrono>
#include <deque>
#include <optional>
#include <utility>

template<typename Request>
class PendingSeekRequests
{
public:
    void push(std::chrono::milliseconds position, Request request)
    {
        m_requests.push_back({ position, std::move(request) });
    }

    std::optional<Request> takeForCompletedPosition(std::chrono::milliseconds position)
    {
        for (auto it = m_requests.rbegin(); it != m_requests.rend(); ++it)
        {
            if (it->position != position)
                continue;

            Request request = std::move(it->request);
            m_requests.erase(m_requests.begin(), it.base());
            return request;
        }

        return std::nullopt;
    }

    void clear()
    {
        m_requests.clear();
    }

    bool empty() const
    {
        return m_requests.empty();
    }

private:
    struct Entry {
        std::chrono::milliseconds position {};
        Request request {};
    };

    std::deque<Entry> m_requests;
};
