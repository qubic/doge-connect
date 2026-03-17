#pragma once

#include <queue>
#include <mutex>
#include <condition_variable>
#include <optional>

/**
 * @brief A thread-safe queue class.
 * @tparam T Element type.
 */
template <typename T>
class ConcurrentQueue {
private:
    std::queue<T> m_queue;
    std::mutex m_mtx;
    std::condition_variable m_cv;

public:
    void push(T value)
    {
        {
            std::lock_guard<std::mutex> lock(m_mtx);
            m_queue.push(std::move(value));
        }
        m_cv.notify_one();
    }

    // This function blocks until data is available.
    T pop()
    {
        std::unique_lock<std::mutex> lock(m_mtx);
        // Wait until queue is not empty (handles spurious wakeups)
        m_cv.wait(lock, [this] { return !m_queue.empty(); });

        T value = std::move(m_queue.front());
        m_queue.pop();
        return value;
    }

    void clear()
    {
        std::lock_guard<std::mutex> lock(m_mtx);
        m_queue = {};
    }

    std::optional<T> try_pop()
    {
        std::lock_guard<std::mutex> lock(m_mtx);
        if (m_queue.empty())
            return std::nullopt;
        T value = std::move(m_queue.front());
        m_queue.pop();
        return value;
    }

    std::size_t size()
    {
        std::lock_guard<std::mutex> lock(m_mtx);
        return m_queue.size();
    }
};
