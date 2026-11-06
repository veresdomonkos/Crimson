#include "crimson_editor/ui/console_panel.hpp"
#include "crimson_editor/ui/editor_theme.hpp"
#include "crimson_editor/ui/icons.hpp"

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>

namespace crimson::editor::ui
{
    ConsolePanel::ConsolePanel()
    {
        m_LogSubscription = Logger::Subscribe([this](LogLevel level, const std::string& message)
        {
            AddLog(level, message);
        });
    }

    ConsolePanel::~ConsolePanel()
    {
        Logger::Unsubscribe(m_LogSubscription);
    }

    void ConsolePanel::AddLog(LogLevel level, const std::string& message, const std::string& stackTrace)
    {
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << std::put_time(std::localtime(&time_t_now), "%H:%M:%S");

        switch (level)
        {
            case LogLevel::Info:    m_InfoCount++; break;
            case LogLevel::Warn: m_WarningCount++; break;
            case LogLevel::Error:   m_ErrorCount++; break;
        }

        m_RawLogs.push_back({ level, message, stackTrace, ss.str(), 1 });

        if (!m_CollapsedLogs.empty() &&
            m_CollapsedLogs.back().Level == level &&
            m_CollapsedLogs.back().Message == message)
        {
            m_CollapsedLogs.back().Count++;
        }
        else
        {
            m_CollapsedLogs.push_back({ level, message, stackTrace, ss.str(), 1 });
        }
    }

    void ConsolePanel::Clear()
    {
        m_RawLogs.clear();
        m_CollapsedLogs.clear();
        m_InfoCount = 0;
        m_WarningCount = 0;
        m_ErrorCount = 0;
        m_SelectedIndex = -1;
    }

    namespace
    {
        const char* LevelIcon(LogLevel level)
        {
            switch (level)
            {
                case LogLevel::Warn:  return kIconWarning;
                case LogLevel::Error: return kIconError;
                default:              return kIconInfo;
            }
        }

        ImVec4 LevelColor(LogLevel level)
        {
            switch (level)
            {
                case LogLevel::Warn:  return theme::Warning;
                case LogLevel::Error: return theme::Error;
                default:              return theme::TextMuted;
            }
        }

        float ButtonWidth(const char* label)
        {
            return ImGui::CalcTextSize(label, nullptr, true).x + ImGui::GetStyle().FramePadding.x * 2.0f;
        }

        // A button that shows or hides a level's messages, tinted with the level's color while they are shown
        bool LevelToggle(const char* label, LogLevel level, bool shown)
        {
            const ImVec4 color = LevelColor(level);

            ImGui::PushStyleColor(ImGuiCol_Button, shown ? ImVec4(color.x, color.y, color.z, 0.25f) : theme::ToggleOff);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(color.x, color.y, color.z, 0.40f));
            ImGui::PushStyleColor(ImGuiCol_Text, shown ? theme::Text : theme::TextMuted);
            const bool clicked = ImGui::Button(label);
            ImGui::PopStyleColor(3);

            return clicked;
        }
    }

    void ConsolePanel::OnImGui()
    {
        ImGui::Begin("Console");

        DrawToolbar();

        const std::vector<LogEntry>& logs = m_Collapse ? m_CollapsedLogs : m_RawLogs;

        std::vector<int> visible;
        visible.reserve(logs.size());

        for (int i = 0; i < static_cast<int>(logs.size()); ++i)
        {
            const LogEntry& log = logs[i];

            if (log.Level == LogLevel::Info && !m_ShowInfo) continue;
            if (log.Level == LogLevel::Warn && !m_ShowWarnings) continue;
            if (log.Level == LogLevel::Error && !m_ShowErrors) continue;
            if (!m_Filter.PassFilter(log.Message.c_str())) continue;

            visible.push_back(i);
        }

        const bool hasSelection = m_SelectedIndex >= 0 && m_SelectedIndex < static_cast<int>(logs.size());

        // The details only take room while there is something to show in them
        const float detailsHeight = hasSelection ? ImGui::GetTextLineHeightWithSpacing() * 4.0f + ImGui::GetFrameHeightWithSpacing() : 0.0f;
        const float listHeight = std::max(ImGui::GetContentRegionAvail().y - detailsHeight, ImGui::GetFrameHeight() * 3.0f);

        DrawLogList(logs, visible, listHeight);

        if (hasSelection)
            DrawDetails(logs[m_SelectedIndex]);

        ImGui::End();
    }

    void ConsolePanel::DrawToolbar()
    {
        const ImGuiStyle& style = ImGui::GetStyle();

        if (ImGui::Button("Clear"))
            Clear();

        ImGui::SameLine();

        // Repeats of the same message in a row go into one line, with how many there were
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(m_Collapse ? ImGuiCol_ButtonActive : ImGuiCol_Button));
        if (ImGui::Button("Group"))
        {
            m_Collapse = !m_Collapse;
            m_SelectedIndex = -1;
        }
        ImGui::PopStyleColor();
        ImGui::SetItemTooltip("Show repeats of the same message as one line");

        ImGui::SameLine();

        const std::string info = std::string(kIconInfo) + " " + std::to_string(m_InfoCount) + "##Info";
        const std::string warnings = std::string(kIconWarning) + " " + std::to_string(m_WarningCount) + "##Warnings";
        const std::string errors = std::string(kIconError) + " " + std::to_string(m_ErrorCount) + "##Errors";

        // The level toggles go on the right, sized to what they say, and the search takes what is left between
        const float togglesWidth = ButtonWidth(info.c_str()) + ButtonWidth(warnings.c_str()) + ButtonWidth(errors.c_str()) + style.ItemSpacing.x * 2.0f;
        const float searchWidth = ImGui::GetContentRegionAvail().x - togglesWidth - style.ItemSpacing.x;
        const bool oneLine = searchWidth >= ImGui::GetFontSize() * 6.0f;

        const std::string hint = std::string(kIconSearch) + "  Search";
        ImGui::SetNextItemWidth(oneLine ? searchWidth : -FLT_MIN);
        if (ImGui::InputTextWithHint("##Search", hint.c_str(), m_Filter.InputBuf, IM_ARRAYSIZE(m_Filter.InputBuf)))
            m_Filter.Build();

        if (ImGui::IsItemDeactivated() && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
            m_Filter.Clear();

        // A narrow panel puts the toggles on a line of their own, at its right, rather than past its edge
        if (oneLine)
            ImGui::SameLine();
        else
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - togglesWidth));

        if (LevelToggle(info.c_str(), LogLevel::Info, m_ShowInfo))
            m_ShowInfo = !m_ShowInfo;
        ImGui::SetItemTooltip(m_ShowInfo ? "Hide messages" : "Show messages");

        ImGui::SameLine();
        if (LevelToggle(warnings.c_str(), LogLevel::Warn, m_ShowWarnings))
            m_ShowWarnings = !m_ShowWarnings;
        ImGui::SetItemTooltip(m_ShowWarnings ? "Hide warnings" : "Show warnings");

        ImGui::SameLine();
        if (LevelToggle(errors.c_str(), LogLevel::Error, m_ShowErrors))
            m_ShowErrors = !m_ShowErrors;
        ImGui::SetItemTooltip(m_ShowErrors ? "Hide errors" : "Show errors");
    }

    void ConsolePanel::DrawLogList(const std::vector<LogEntry>& logs, const std::vector<int>& visible, float height)
    {
        constexpr ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersOuter
            | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings;

        const int columns = m_Collapse ? 4 : 3;
        if (!ImGui::BeginTable("##Logs", columns, flags, ImVec2(0.0f, height)))
            return;

        ImGui::TableSetupColumn("Level", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize());
        ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Message", ImGuiTableColumnFlags_WidthStretch);
        if (m_Collapse)
            ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthFixed);

        // Stays at the newest message, unless scrolled up to read an older one
        const bool atBottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f;

        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(visible.size()));

        while (clipper.Step())
        {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
            {
                const int index = visible[row];
                const LogEntry& log = logs[index];

                ImGui::PushID(index);
                ImGui::TableNextRow();

                // The whole row selects, and selecting it again closes the details
                ImGui::TableNextColumn();
                if (ImGui::Selectable("##Row", m_SelectedIndex == index, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap))
                    m_SelectedIndex = m_SelectedIndex == index ? -1 : index;

                ImGui::SameLine(0.0f, 0.0f);
                ImGui::TextColored(LevelColor(log.Level), "%s", LevelIcon(log.Level));

                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", log.Timestamp.c_str());

                // The first line only, the details have all of it
                ImGui::TableNextColumn();
                const size_t newline = log.Message.find('\n');
                const char* end = newline == std::string::npos ? nullptr : log.Message.c_str() + newline;

                const bool tinted = log.Level != LogLevel::Info;
                if (tinted)
                    ImGui::PushStyleColor(ImGuiCol_Text, LevelColor(log.Level));
                ImGui::TextUnformatted(log.Message.c_str(), end);
                if (tinted)
                    ImGui::PopStyleColor();

                if (m_Collapse)
                {
                    ImGui::TableNextColumn();
                    if (log.Count > 1)
                        ImGui::TextDisabled("%d", log.Count);
                }

                ImGui::PopID();
            }
        }

        if (atBottom)
            ImGui::SetScrollHereY(1.0f);

        ImGui::EndTable();
    }

    void ConsolePanel::DrawDetails(const LogEntry& log)
    {
        ImGui::Spacing();

        ImGui::TextColored(LevelColor(log.Level), "%s", LevelIcon(log.Level));
        ImGui::SameLine();
        ImGui::TextDisabled("%s", log.Timestamp.c_str());

        ImGui::SameLine(std::max(0.0f, ImGui::GetContentRegionMax().x - ButtonWidth("Copy")));
        if (ImGui::Button("Copy"))
            ImGui::SetClipboardText(log.Message.c_str());

        if (ImGui::BeginChild("##Details", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders))
        {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(log.Message.c_str());

            if (!log.StackTrace.empty())
            {
                ImGui::Separator();
                ImGui::TextDisabled("%s", log.StackTrace.c_str());
            }

            ImGui::PopTextWrapPos();
        }
        ImGui::EndChild();
    }
}
