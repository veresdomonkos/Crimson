#pragma once
#include <vector>
#include <string>

#include <imgui.h>
#include "crimson/core/log.hpp"
#include "crimson_editor/ui/panel.hpp"

namespace crimson::editor::ui
{
    struct LogEntry
    {
        LogLevel Level;
        std::string Message;
        std::string StackTrace;
        std::string Timestamp;
        int Count = 1;
    };

    class ConsolePanel : public Panel
    {
    public:
        ConsolePanel();
        void AddLog(LogLevel level, const std::string& message, const std::string& stackTrace = "");
        void Clear();
        void OnImGui() override;
    private:
        std::vector<LogEntry> m_RawLogs;
        std::vector<LogEntry> m_CollapsedLogs;

        ImGuiTextFilter m_Filter;
        int m_SelectedIndex = -1;

        bool m_Collapse = false;
        bool m_ShowInfo = true;
        bool m_ShowWarnings = true;
        bool m_ShowErrors = true;

        int m_InfoCount = 0;
        int m_WarningCount = 0;
        int m_ErrorCount = 0;
    };
}
