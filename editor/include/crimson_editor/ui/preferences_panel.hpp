#pragma once
#include <array>
#include <vector>

#include "crimson_editor/editor_settings.hpp"
#include "crimson_editor/script_editor.hpp"
#include "crimson_editor/ui/panel.hpp"

namespace crimson
{
    class Window;
}

namespace crimson::editor::ui
{
    // The editor's own settings, for every project, in a window opened from Edit > Preferences
    class PreferencesPanel : public Panel
    {
    public:
        PreferencesPanel(EditorSettings& settings, const Window& window) : m_settings(settings), m_window(window) {}

        void OnMainMenuBar() override;
        void OnImGui() override;
    private:
        void DrawScriptEditor();
    private:
        EditorSettings& m_settings;
        const Window& m_window;
        bool m_open = false;

        // Looked for each time the window opens, as editors get installed while the editor runs
        std::vector<CodeEditor> m_found;

        std::array<char, 1024> m_arguments{};
        bool m_editingArguments = false;
    };
}
