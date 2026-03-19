#pragma once

#include <iostream>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <mutex>

/**
 * @brief Thread-safe logging with timestamp prefix.
 *
 * Usage:
 *   LOG() << "message" << std::endl;
 *   ERR() << "error message" << std::endl;
 *
 * Output format: [2026-03-19 14:30:05.123] message
 */

namespace logging {

inline std::mutex& getLogMutex()
{
    static std::mutex mtx;
    return mtx;
}

inline std::string timestamp()
{
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    std::ostringstream oss;
    oss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S")
        << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

// RAII helper that locks the log mutex for the lifetime of a chained << expression,
// and prepends a timestamp on construction.
class LogStream
{
public:
    LogStream(std::ostream& os) : m_lock(getLogMutex()), m_os(os)
    {
        m_os << "[" << timestamp() << "] ";
    }

    template <typename T>
    LogStream& operator<<(const T& value)
    {
        m_os << value;
        return *this;
    }

    // Support std::endl and other manipulators.
    LogStream& operator<<(std::ostream& (*manip)(std::ostream&))
    {
        manip(m_os);
        return *this;
    }

private:
    std::lock_guard<std::mutex> m_lock;
    std::ostream& m_os;
};

} // namespace logging

inline logging::LogStream LOG() { return logging::LogStream(std::cout); }
inline logging::LogStream ERR() { return logging::LogStream(std::cerr); }
