#pragma once
#include <cstdint>
#include <string>
#include <utility>

#include <crimson/core/log.hpp>

namespace crimson::tests
{
    // Counts the messages of one level that mention a text, logged while it exists, to tell apart "said once" from
    // "said on every frame"
    class LogCounter
    {
    public:
        LogCounter(LogLevel level, std::string text)
            : m_level(level), m_text(std::move(text))
        {
            // Subscribing replays what was logged before, which isn't counted
            m_id = Logger::Subscribe([this](LogLevel logged, const std::string& message)
            {
                if (m_started && logged == m_level && message.find(m_text) != std::string::npos)
                    ++m_count;
            });
            m_started = true;
        }

        ~LogCounter() { Logger::Unsubscribe(m_id); }

        LogCounter(const LogCounter&) = delete;
        LogCounter& operator=(const LogCounter&) = delete;

        [[nodiscard]] int Count() const { return m_count; }
    private:
        LogLevel m_level;
        std::string m_text;
        uint64_t m_id = 0;
        bool m_started = false;
        int m_count = 0;
    };
}
