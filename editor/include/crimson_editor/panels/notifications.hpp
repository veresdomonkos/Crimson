#pragma once
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include <crimson/core/log.hpp>

namespace crimson::editor
{
    // Short messages in the corner of the window, for what shouldn't go unnoticed while the Console is closed:
    // every error that is logged, and what the editor reports, such as a finished save. They fade out on their own,
    // stay while the mouse is on them, and clicking one opens the Console.
    class Notifications
    {
    public:
        // Clicking a message about a problem calls showDetails, which opens what has the details
        explicit Notifications(std::function<void()> showDetails);
        ~Notifications();
        Notifications(const Notifications&) = delete;
        Notifications& operator=(const Notifications&) = delete;

        enum class Kind { Info, Success, Warning, Error };

        void Push(Kind kind, std::string text);

        // Call last in the frame, so the messages are above the panels
        void Draw();
    private:
        struct Message
        {
            Kind Type = Kind::Info;
            std::string Text;
            int Count = 1;
            float Age = 0.0f; // seconds shown, which stops while the mouse is on it
            uint64_t Id = 0;
        };

        std::function<void()> m_showDetails;

        uint64_t m_logSubscription = 0;
        bool m_subscribing = false;

        // Errors can be logged from other threads
        std::mutex m_pendingMutex;
        std::vector<Message> m_pending;

        std::vector<Message> m_shown;
        uint64_t m_nextId = 1;
    };
}
