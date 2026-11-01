#include "crimson/core/log.hpp"

#include <cstdio>
#include <string>
#include <utility>

namespace crimson
{
    void Logger::Subscribe(LogCallback callback)
    {
        std::lock_guard<std::mutex> lock(s_Mutex);

        if (callback)
        {
            for (const Entry& e : s_History)
                callback(e.Level, e.Message);
        }

        s_Callbacks.push_back(std::move(callback));
    }

    void Logger::Write(const LogLevel level, const std::string& msg)
    {
        std::lock_guard<std::mutex> lock(s_Mutex);

        switch (level)
        {
            case LogLevel::Info:  fmt::print(fg(fmt::color::white),  "{}\n", msg); break;
            case LogLevel::Warn:  fmt::print(fg(fmt::color::yellow), "{}\n", msg); break;
            case LogLevel::Error: fmt::print(fg(fmt::color::red),    "{}\n", msg); break;
        }

        // stdout is fully buffered when redirected, the last messages before a crash would be lost
        std::fflush(stdout);

        s_History.push_back({ level, msg });
        if (s_History.size() > kMaxHistory)
            s_History.pop_front();

        for (const auto& callback : s_Callbacks)
        {
            if (callback)
                callback(level, msg);
        }
    }
}
