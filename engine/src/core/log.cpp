#include "crimson/core/log.hpp"

namespace crimson
{
    void Logger::Subscribe(LogCallback callback)
    {
        std::lock_guard<std::mutex> lock(s_Mutex);
        s_Callbacks.push_back(std::move(callback));
    }

    void Logger::Write(const LogLevel level, const std::string& msg)
    {
        std::lock_guard<std::mutex> lock(s_Mutex);

        switch (level)
        {
            case LogLevel::Info:
                fmt::print(fg(fmt::color::white),"{}\n", msg);
                break;

            case LogLevel::Warn:
                fmt::print(fg(fmt::color::yellow),"{}\n", msg);
                break;

            case LogLevel::Error:
                fmt::print(fg(fmt::color::red),"{}\n", msg);
                break;
        }

        for (const auto& callback : s_Callbacks)
        {
            if (callback)
                callback(level, msg);
        }
    }
}
