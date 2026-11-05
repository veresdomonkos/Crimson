#include "crimson_editor/ui/console_panel.hpp"
#include "crimson_editor/ui/editor_theme.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

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

    void ConsolePanel::OnImGui()
    {
        ImGui::Begin("Console");

        // -------------------------------------------------------------------
        // TOP TOOLBAR
        // -------------------------------------------------------------------
        if (ImGui::Button("Clear")) { Clear(); }
        ImGui::SameLine();

        ImGui::Checkbox("Collapse", &m_Collapse);
        ImGui::SameLine();

        m_Filter.Draw("Filter", 150.0f);

        // Align Log Level filter toggles to the right side of the toolbar
        char infoLabel[32], warnLabel[32], errLabel[32];
        snprintf(infoLabel, sizeof(infoLabel), "Info (%d)", m_InfoCount);
        snprintf(warnLabel, sizeof(warnLabel), "Warn (%d)", m_WarningCount);
        snprintf(errLabel, sizeof(errLabel), "Error (%d)", m_ErrorCount);

        ImGui::SameLine(ImGui::GetWindowWidth() - 260.0f);

        ImGui::PushStyleColor(ImGuiCol_Button, m_ShowInfo ? theme::ToggleInfo : theme::ToggleOff);
        if (ImGui::Button(infoLabel)) m_ShowInfo = !m_ShowInfo;
        ImGui::PopStyleColor();

        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, m_ShowWarnings ? theme::ToggleWarning : theme::ToggleOff);
        if (ImGui::Button(warnLabel)) m_ShowWarnings = !m_ShowWarnings;
        ImGui::PopStyleColor();

        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, m_ShowErrors ? theme::ToggleError : theme::ToggleOff);
        if (ImGui::Button(errLabel)) m_ShowErrors = !m_ShowErrors;
        ImGui::PopStyleColor();

        ImGui::Separator();

        // -------------------------------------------------------------------
        // MAIN LOG LIST (Top Pane)
        // -------------------------------------------------------------------
        const auto& activeLogs = m_Collapse ? m_CollapsedLogs : m_RawLogs;

        // 1. Pre-filter visible log indices
        std::vector<int> visibleIndices;
        visibleIndices.reserve(activeLogs.size());

        for (int i = 0; i < static_cast<int>(activeLogs.size()); ++i)
        {
            const auto& log = activeLogs[i];

            if (log.Level == LogLevel::Info && !m_ShowInfo) continue;
            if (log.Level == LogLevel::Warn && !m_ShowWarnings) continue;
            if (log.Level == LogLevel::Error && !m_ShowErrors) continue;
            if (!m_Filter.PassFilter(log.Message.c_str())) continue;

            visibleIndices.push_back(i);
        }

        const float availableHeight = ImGui::GetContentRegionAvail().y;
        const float preferredDetailsHeight = 120.0f;
        const float minLogListHeight = 100.0f;

        // Calculate remaining height for details after preserving min log list height
        float detailsPaneHeight = availableHeight - minLogListHeight;

        // Clamp details pane height
        if (detailsPaneHeight > preferredDetailsHeight)
            detailsPaneHeight = preferredDetailsHeight;

        // Hide details pane completely if window is resized very small
        if (detailsPaneHeight < 30.0f)
            detailsPaneHeight = 0.0f;

        const float logListHeight = (detailsPaneHeight > 0.0f) ? -detailsPaneHeight : 0.0f;

        if (ImGui::BeginChild("LogListPane", ImVec2(0, logListHeight), true, ImGuiWindowFlags_HorizontalScrollbar))
        {
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(visibleIndices.size()));

            while (clipper.Step())
            {
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++)
                {
                    int logIndex = visibleIndices[i];
                    const auto& log = activeLogs[logIndex];

                    ImGui::PushID(logIndex);

                    ImVec4 textColor;
                    switch (log.Level)
                    {
                        case LogLevel::Info:    textColor = theme::Text; break;
                        case LogLevel::Warn: textColor = theme::Warning; break;
                        case LogLevel::Error:   textColor = theme::Error; break;
                    }

                    std::string lineText = log.Timestamp + " " + log.Message;
                    bool isSelected = (m_SelectedIndex == logIndex);

                    ImGui::PushStyleColor(ImGuiCol_Text, textColor);
                    if (ImGui::Selectable(lineText.c_str(), isSelected, ImGuiSelectableFlags_SpanAllColumns))
                    {
                        m_SelectedIndex = logIndex;
                    }
                    ImGui::PopStyleColor();

                    if (m_Collapse && log.Count > 1)
                    {
                        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - 45.0f);
                        ImGui::TextDisabled("[%d]", log.Count);
                    }

                    ImGui::PopID();
                }
            }
            clipper.End();

            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
                ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();

        // -------------------------------------------------------------------
        // LOG DETAILS / STACK TRACE (Bottom Pane)
        // -------------------------------------------------------------------
        if (detailsPaneHeight > 0.0f)
        {
            ImGui::Separator();

            if (ImGui::BeginChild("LogDetailsPane", ImVec2(0, 0), true))
            {
                if (m_SelectedIndex >= 0 && m_SelectedIndex < static_cast<int>(activeLogs.size()))
                {
                    const auto& selectedLog = activeLogs[m_SelectedIndex];
                    ImGui::TextUnformatted(selectedLog.Message.c_str());
                    if (!selectedLog.StackTrace.empty())
                    {
                        ImGui::Separator();
                        ImGui::TextDisabled("%s", selectedLog.StackTrace.c_str());
                    }
                }
            }
            ImGui::EndChild();
        }

        ImGui::End();
    }
}
