#include "crimson/core/log.hpp"

namespace crimson
{
    void Logger::Write(const LogLevel level, const std::string& msg)
    {
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
    }
}
