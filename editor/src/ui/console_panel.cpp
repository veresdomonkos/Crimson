#include "crimson_editor/ui/console_panel.hpp"
#include "crimson_editor/ui/editor_theme.hpp"
#include "crimson_editor/ui/icons.hpp"
#include "crimson_editor/ui/input.hpp"
#include "crimson_editor/ui/layout.hpp"
#include "crimson_editor/ui/widgets.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>

namespace crimson::editor::ui
{
    ConsolePanel::ConsolePanel()
        : Panel({ .Title = "Console", .Icon = ui::icons::kInfo, .Slot = DockSlot::Bottom, .TabOrder = 1 })
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

        // Under the lock too, as localtime shares one buffer
        const std::lock_guard lock(m_PendingMutex);
        ss << std::put_time(std::localtime(&time_t_now), "%H:%M:%S");
        m_Pending.push_back({ level, message, stackTrace, ss.str(), 1 });
    }

    void ConsolePanel::DrainPending()
    {
        std::vector<LogEntry> pending;
        {
            const std::lock_guard lock(m_PendingMutex);
            pending.swap(m_Pending);
        }

        for (LogEntry& entry : pending)
        {
            switch (entry.Level)
            {
                case LogLevel::Info:    m_InfoCount++; break;
                case LogLevel::Warn: m_WarningCount++; break;
                case LogLevel::Error:   m_ErrorCount++; break;
            }

            m_RawLogs.push_back(entry);

            if (!m_CollapsedLogs.empty() &&
                m_CollapsedLogs.back().Level == entry.Level &&
                m_CollapsedLogs.back().Message == entry.Message)
            {
                m_CollapsedLogs.back().Count++;
            }
            else
            {
                m_CollapsedLogs.push_back(std::move(entry));
            }
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

    static const char* LevelIcon(LogLevel level)
    {
        switch (level)
        {
            case LogLevel::Warn:  return ui::icons::kWarning;
            case LogLevel::Error: return ui::icons::kError;
            default:              return ui::icons::kInfo;
        }
    }

    static ui::Color LevelColor(LogLevel level)
    {
        switch (level)
        {
            case LogLevel::Warn:  return ui::theme::Warning;
            case LogLevel::Error: return ui::theme::Error;
            default:              return ui::theme::TextMuted;
        }
    }

    void ConsolePanel::Draw()
    {
        DrainPending();
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
            if (!m_Filter.Matches(log.Message)) continue;

            visible.push_back(i);
        }

        const bool hasSelection = m_SelectedIndex >= 0 && m_SelectedIndex < static_cast<int>(logs.size());

        // The details only take room while there is something to show in them
        const float detailsHeight = hasSelection ? ui::LineHeightWithSpacing() * 4.0f + ui::FrameHeightWithSpacing() : 0.0f;
        const float listHeight = std::max(ui::Available().y - detailsHeight, ui::FrameHeight() * 3.0f);

        DrawLogList(logs, visible, listHeight);

        // Clicking in the list can change the selection, so it is checked again
        if (m_SelectedIndex >= 0 && m_SelectedIndex < static_cast<int>(logs.size()))
            DrawDetails(logs[m_SelectedIndex]);
    }

    void ConsolePanel::DrawToolbar()
    {
        const std::string info = std::string(ui::icons::kInfo) + " " + std::to_string(m_InfoCount) + "##Info";
        const std::string warnings = std::string(ui::icons::kWarning) + " " + std::to_string(m_WarningCount) + "##Warnings";
        const std::string errors = std::string(ui::icons::kError) + " " + std::to_string(m_ErrorCount) + "##Errors";

        // The level toggles go on the right, sized to what they say, and the search takes what is left between
        const float togglesWidth = ui::ButtonWidth(info) + ui::ButtonWidth(warnings) + ui::ButtonWidth(errors) + ui::ItemSpacing().x * 2.0f;

        const auto drawToggles = [&](ui::Row& row)
        {
            row.PushRight(togglesWidth);

            if (ui::TintedToggle(info, m_ShowInfo, LevelColor(LogLevel::Info)))
                m_ShowInfo = !m_ShowInfo;
            ui::Tooltip(m_ShowInfo ? "Hide messages" : "Show messages");

            if (ui::TintedToggle(warnings, m_ShowWarnings, LevelColor(LogLevel::Warn)))
                m_ShowWarnings = !m_ShowWarnings;
            ui::Tooltip(m_ShowWarnings ? "Hide warnings" : "Show warnings");

            if (ui::TintedToggle(errors, m_ShowErrors, LevelColor(LogLevel::Error)))
                m_ShowErrors = !m_ShowErrors;
            ui::Tooltip(m_ShowErrors ? "Hide errors" : "Show errors");
        };

        bool oneLine = false;

        {
            ui::Row row;

            if (ui::Button("Clear"))
                Clear();

            // Repeats of the same message in a row go into one line, with how many there were
            if (ui::ToggleButton("Group", m_Collapse))
            {
                m_Collapse = !m_Collapse;
                m_SelectedIndex = -1;
            }
            ui::Tooltip("Show repeats of the same message as one line");

            const float searchWidth = ui::AvailableAfterItem() - togglesWidth - ui::ItemSpacing().x;
            oneLine = searchWidth >= ui::FontSize() * 6.0f;

            if (oneLine)
                ui::NextItemWidth(searchWidth);
            else
                ui::NextItemFills();

            ui::SearchBox("##Search", m_Filter, std::string(ui::icons::kSearch) + "  Search");

            if (oneLine)
                drawToggles(row);
        }

        // A narrow panel puts the toggles on a line of their own, at its right, rather than past its edge
        if (!oneLine)
        {
            ui::Row row;
            drawToggles(row);
        }
    }

    void ConsolePanel::DrawLogList(const std::vector<LogEntry>& logs, const std::vector<int>& visible, float height)
    {
        std::vector<ui::Column> columns = { { "Level", ui::FontSize() }, { "Time" }, { "Message", 0.0f, true } };
        if (m_Collapse)
            columns.push_back({ "Count" });

        ui::Table table("##Logs", columns, ui::TableOptions{ .OuterBorders = true, .RowBackgrounds = true, .ScrollY = true, .FixedFit = true, .Height = height });
        if (!table)
            return;

        // Stays at the newest message, unless scrolled up to read an older one
        const bool atBottom = ui::IsScrolledToBottom();

        ui::ForVisibleRows(static_cast<int>(visible.size()), [&](int row)
        {
            const int index = visible[row];
            const LogEntry& log = logs[index];

            const ui::IdScope scope(index);
            table.NextRow();

            // The whole row selects, and selecting it again closes the details
            table.NextColumn();
            {
                ui::Row line;
                if (ui::Selectable("##Row", m_SelectedIndex == index, ui::SelectableOptions{ .SpanColumns = true, .AllowOverlap = true }))
                    m_SelectedIndex = m_SelectedIndex == index ? -1 : index;

                line.Gap(0.0f);
                ui::Text(LevelIcon(log.Level), LevelColor(log.Level));
            }

            table.NextColumn();
            ui::Text(log.Timestamp, ui::TextStyle::Muted);

            // The first line only, the details have all of it
            table.NextColumn();
            const std::string_view firstLine = std::string_view(log.Message).substr(0, log.Message.find('\n'));

            if (log.Level != LogLevel::Info)
                ui::Text(firstLine, LevelColor(log.Level));
            else
                ui::Text(firstLine);

            if (m_Collapse)
            {
                table.NextColumn();
                if (log.Count > 1)
                    ui::Text(std::to_string(log.Count), ui::TextStyle::Muted);
            }
        });

        if (atBottom)
            ui::ScrollHere(1.0f);
    }

    void ConsolePanel::DrawDetails(const LogEntry& log)
    {
        ui::Spacing();

        {
            ui::Row row;
            ui::Text(LevelIcon(log.Level), LevelColor(log.Level));
            ui::Text(log.Timestamp, ui::TextStyle::Muted);

            row.PushRight(ui::ButtonWidth("Copy"));
            if (ui::Button("Copy"))
                ui::input::SetClipboard(log.Message);
        }

        if (const ui::Region details("##Details", {}, true); details)
        {
            ui::WrappedText(log.Message, ui::theme::Text);

            if (!log.StackTrace.empty())
            {
                ui::Separator();
                ui::WrappedText(log.StackTrace, ui::theme::TextMuted);
            }
        }
    }
}
