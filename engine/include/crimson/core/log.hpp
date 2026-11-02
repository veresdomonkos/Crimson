#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <utility>
#include <vector>
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
        // Returns the id to unsubscribe with. The callback first gets the messages logged so far.
        static uint64_t Subscribe(LogCallback callback);
        static void Unsubscribe(uint64_t id);

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

        struct Subscriber
        {
            uint64_t Id;
            LogCallback Callback;
        };

        static constexpr size_t kMaxHistory = 2000;

        static inline std::deque<Entry> s_History;
        static inline std::vector<Subscriber> s_Subscribers;
        static inline uint64_t s_NextSubscriberId = 1;
        static inline std::mutex s_Mutex;
    };
}

#define LOG_INFO(...)  ::crimson::Logger::Info(__VA_ARGS__)
#define LOG_WARN(...)  ::crimson::Logger::Warn(__VA_ARGS__)
#define LOG_ERROR(...) ::crimson::Logger::Error(__VA_ARGS__)
