#pragma once

#include <deque>
#include <functional>
#include <mutex>
#include <fmt/format.h>
#include <fmt/color.h>

namespace crimson
{
    enum class LogLevel
    {
        Info,
        Warn,
        Error
    };

    using LogCallback = std::function<void(LogLevel level, const std::string& message)>;

    class Logger
    {
    public:
        static void Subscribe(LogCallback callback);

        template<typename... Args>
        static void Info(fmt::format_string<Args...> format, Args&&... args)
        {
            Write(LogLevel::Info, fmt::format(format, std::forward<Args>(args)...));
        }

        template<typename... Args>
        static void Warn(fmt::format_string<Args...> format, Args&&... args)
        {
            Write(LogLevel::Warn, fmt::format(format, std::forward<Args>(args)...));
        }

        template<typename... Args>
        static void Error(fmt::format_string<Args...> format, Args&&... args)
        {
            Write(LogLevel::Error, fmt::format(format, std::forward<Args>(args)...));
        }

    private:
        static void Write(LogLevel level, const std::string& msg);

        struct Entry
        {
            LogLevel Level;
            std::string Message;
        };

        static constexpr size_t kMaxHistory = 2000;

        static inline std::deque<Entry> s_History;
        static inline std::vector<LogCallback> s_Callbacks;
        static inline std::mutex s_Mutex;
    };
}

#define LOG_INFO(...)  ::crimson::Logger::Info(__VA_ARGS__)
#define LOG_WARN(...)  ::crimson::Logger::Warn(__VA_ARGS__)
#define LOG_ERROR(...) ::crimson::Logger::Error(__VA_ARGS__)
