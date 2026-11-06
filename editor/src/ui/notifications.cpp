#include "crimson_editor/ui/notifications.hpp"

#include <algorithm>
#include <string>
#include <utility>

#include <imgui.h>

#include "crimson_editor/ui/editor_theme.hpp"
#include "crimson_editor/ui/icons.hpp"

namespace crimson::editor::ui
{
    namespace
    {
        constexpr size_t kMaxShown = 5;
        constexpr float kFadeSeconds = 0.6f;

        // Problems stay up long enough to be read, news only briefly
        float Lifetime(Notifications::Kind kind)
        {
            switch (kind)
            {
                case Notifications::Kind::Error:   return 8.0f;
                case Notifications::Kind::Warning: return 6.0f;
                default:                           return 3.0f;
            }
        }

        const char* Icon(Notifications::Kind kind)
        {
            switch (kind)
            {
                case Notifications::Kind::Error:   return kIconError;
                case Notifications::Kind::Warning: return kIconWarning;
                case Notifications::Kind::Success: return kIconSuccess;
                default:                           return kIconInfo;
            }
        }

        ImVec4 Color(Notifications::Kind kind)
        {
            switch (kind)
            {
                case Notifications::Kind::Error:   return theme::Error;
                case Notifications::Kind::Warning: return theme::Warning;
                case Notifications::Kind::Success: return theme::Success;
                default:                           return theme::Text;
            }
        }
    }

    Notifications::Notifications()
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

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const ImGuiStyle& style = ImGui::GetStyle();
        const float margin = style.WindowPadding.x * 1.5f;
        const float deltaTime = ImGui::GetIO().DeltaTime;

        constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings
            | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoMove;

        // The newest at the bottom right, older ones stacked above it
        float bottom = viewport->WorkPos.y + viewport->WorkSize.y - margin;
        const float right = viewport->WorkPos.x + viewport->WorkSize.x - margin;

        std::vector<uint64_t> dismissed;
        int slot = 0;

        for (auto it = m_shown.rbegin(); it != m_shown.rend(); ++it, ++slot)
        {
            Message& message = *it;

            const float remaining = Lifetime(message.Type) - message.Age;
            const float alpha = std::clamp(remaining / kFadeSeconds, 0.0f, 1.0f);

            ImGui::SetNextWindowPos(ImVec2(right, bottom), ImGuiCond_Always, ImVec2(1.0f, 1.0f));
            ImGui::SetNextWindowViewport(viewport->ID);
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, style.FrameRounding * 2.0f);

            // Named by place rather than by message, so ImGui keeps a few windows instead of one for each message ever shown
            const std::string window = "##Notification" + std::to_string(slot);
            if (ImGui::Begin(window.c_str(), nullptr, flags))
            {
                ImGui::TextColored(Color(message.Type), "%s", Icon(message.Type));
                ImGui::SameLine();

                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetFontSize() * 22.0f);
                if (message.Count > 1)
                    ImGui::Text("%s  (x%d)", message.Text.c_str(), message.Count);
                else
                    ImGui::TextUnformatted(message.Text.c_str());
                ImGui::PopTextWrapPos();

                const bool problem = message.Type == Kind::Error || message.Type == Kind::Warning;
                if (problem)
                    ImGui::TextDisabled("Click to open the Console");

                const bool hovered = ImGui::IsWindowHovered();
                if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                {
                    if (problem)
                        ImGui::SetWindowFocus("Console");

                    dismissed.push_back(message.Id);
                }

                // Reading it shouldn't race it fading out
                if (!hovered)
                    message.Age += deltaTime;

                bottom -= ImGui::GetWindowHeight() + style.ItemSpacing.y;
            }

            ImGui::End();
            ImGui::PopStyleVar(2);
        }

        std::erase_if(m_shown, [&](const Message& message)
        {
            return message.Age >= Lifetime(message.Type) || std::ranges::find(dismissed, message.Id) != dismissed.end();
        });
    }
}
