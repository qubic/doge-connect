#pragma once

#include <unordered_map>
#include <mutex>
#include <optional>

/**
 * @brief A thread-safe hash map class.
 * @tparam K Key type.
 * @tparam V Value type.
 */
template <typename K, typename V>
class ConcurrentHashMap {
private:
    std::unordered_map<K, V> m_map;
    mutable std::mutex m_mtx;

public:
    void insert(const K& key, const V& value)
    {
        std::lock_guard<std::mutex> lock(m_mtx);
        m_map[key] = value;
    }

    std::optional<V> get(const K& key) const
    {
        std::lock_guard<std::mutex> lock(m_mtx);
        auto it = m_map.find(key);
        if (it != m_map.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    bool erase(const K& key)
    {
        std::lock_guard<std::mutex> lock(m_mtx);
        return m_map.erase(key) > 0;
    }

    void clear()
    {
        std::lock_guard<std::mutex> lock(m_mtx);
        m_map.clear();
    }

    bool contains(const K& key) const
    {
        std::lock_guard<std::mutex> lock(m_mtx);
        return m_map.find(key) != m_map.end();
    }

    size_t size() const
    {
        std::lock_guard<std::mutex> lock(m_mtx);
        return m_map.size();
    }
};
