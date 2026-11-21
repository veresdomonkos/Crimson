#include "crimson_editor/ui/notifications.hpp"

#include <algorithm>
#include <string>
#include <utility>

#include "crimson_editor/ui/editor_theme.hpp"
#include "crimson_editor/ui/icons.hpp"
#include "crimson_editor/ui/input.hpp"
#include "crimson_editor/ui/layout.hpp"
#include "crimson_editor/ui/widgets.hpp"
#include "crimson_editor/ui/window.hpp"

namespace crimson::editor::ui
{
    constexpr size_t kMaxShown = 5;
    constexpr float kFadeSeconds = 0.6f;

    // Problems stay up long enough to be read, news only briefly
    static float Lifetime(Notifications::Kind kind)
    {
        switch (kind)
        {
            case Notifications::Kind::Error:   return 8.0f;
            case Notifications::Kind::Warning: return 6.0f;
            default:                           return 3.0f;
        }
    }

    static const char* Icon(Notifications::Kind kind)
    {
        switch (kind)
        {
            case Notifications::Kind::Error:   return ui::icons::kError;
            case Notifications::Kind::Warning: return ui::icons::kWarning;
            case Notifications::Kind::Success: return ui::icons::kSuccess;
            default:                           return ui::icons::kInfo;
        }
    }

    static ui::Color KindColor(Notifications::Kind kind)
    {
        switch (kind)
        {
            case Notifications::Kind::Error:   return ui::theme::Error;
            case Notifications::Kind::Warning: return ui::theme::Warning;
            case Notifications::Kind::Success: return ui::theme::Success;
            default:                           return ui::theme::Text;
        }
    }

    Notifications::Notifications(std::function<void()> showDetails)
        : m_showDetails(std::move(showDetails))
    {
        // Subscribing hands over what was logged before, which isn't news anymore
        m_subscribing = true;
        m_logSubscription = Logger::Subscribe([this](LogLevel level, const std::string& message)
        {
            if (!m_subscribing && level == LogLevel::Error)
                Push(Kind::Error, message);
        });
        m_subscribing = false;
    }

    Notifications::~Notifications()
    {
        Logger::Unsubscribe(m_logSubscription);
    }

    void Notifications::Push(Kind kind, std::string text)
    {
        const std::lock_guard lock(m_pendingMutex);
        m_pending.push_back(Message{ .Type = kind, .Text = std::move(text) });
    }

    void Notifications::Draw()
    {
        {
            const std::lock_guard lock(m_pendingMutex);

            for (Message& message : m_pending)
            {
                // The same message again counts up on the one already shown, instead of filling the corner
                const auto same = std::ranges::find_if(m_shown, [&](const Message& shown) { return shown.Type == message.Type && shown.Text == message.Text; });

                if (same != m_shown.end())
                {
                    ++same->Count;
                    same->Age = 0.0f;
                    continue;
                }

                message.Id = m_nextId++;
                m_shown.push_back(std::move(message));
            }

            m_pending.clear();
        }

        while (m_shown.size() > kMaxShown)
            m_shown.erase(m_shown.begin());

        const ui::Rect work = ui::WorkArea();
        const float margin = ui::theme::Scaled(ui::theme::SpaceL);
        const float deltaTime = ui::input::DeltaTime();

        // The newest at the bottom right, older ones stacked above it
        float bottom = work.Max.y - margin;
        const float right = work.Max.x - margin;

        std::vector<uint64_t> dismissed;
        int slot = 0;

        for (auto it = m_shown.rbegin(); it != m_shown.rend(); ++it, ++slot)
        {
            Message& message = *it;

            const float remaining = Lifetime(message.Type) - message.Age;
            const float alpha = std::clamp(remaining / kFadeSeconds, 0.0f, 1.0f);

            // Named by place rather than by message, so the UI keeps a few windows instead of one for each message ever shown
            const ui::OverlayWindow overlay("##Notification" + std::to_string(slot), glm::vec2(right, bottom), glm::vec2(1.0f, 1.0f), alpha);

            if (overlay)
            {
                {
                    ui::Row row;
                    ui::Text(Icon(message.Type), KindColor(message.Type));

                    ui::WrappedText(message.Count > 1 ? message.Text + "  (x" + std::to_string(message.Count) + ")" : message.Text, ui::theme::Text, ui::FontSize() * 22.0f);
                }

                const bool problem = message.Type == Kind::Error || message.Type == Kind::Warning;
                if (problem)
                    ui::Text("Click to open the Console", ui::TextStyle::Muted);

                const bool hovered = overlay.Hovered();
                if (hovered && ui::input::MouseClicked(MouseButton::Left))
                {
                    if (problem)
                        m_showDetails();

                    dismissed.push_back(message.Id);
                }

                // Reading it shouldn't race it fading out
                if (!hovered)
                    message.Age += deltaTime;

                bottom -= overlay.Height() + ui::ItemSpacing().y;
            }
        }

        std::erase_if(m_shown, [&](const Message& message)
        {
            return message.Age >= Lifetime(message.Type) || std::ranges::find(dismissed, message.Id) != dismissed.end();
        });
    }
}
