#pragma once
#include <mutex>
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
        ~ConsolePanel() override;
        // From any thread. The entry shows up in the Console the next time it is drawn.
        void AddLog(LogLevel level, const std::string& message, const std::string& stackTrace = "");
        void Clear();
    protected:
        void Draw() override;
    private:
        // Moves what AddLog was given into the lists, on the main thread
        void DrainPending();
        void DrawToolbar();
        void DrawLogList(const std::vector<LogEntry>& logs, const std::vector<int>& visible, float height);
        void DrawDetails(const LogEntry& log);
    private:
        uint64_t m_LogSubscription = 0;

        std::mutex m_PendingMutex;
        std::vector<LogEntry> m_Pending;

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
