#pragma once
#include <string>
#include <vector>

#include "crimson_editor/editor_settings.hpp"
#include "crimson_editor/script_editor.hpp"
#include "crimson_editor/ui/panel.hpp"

namespace crimson
{
    class Window;
}

namespace crimson::editor
{
    // The editor's own settings, for every project, in a window opened from Edit > Preferences or View
    class PreferencesPanel : public ui::Panel
    {
    public:
        PreferencesPanel(EditorSettings& settings, const Window& window)
            : ui::Panel({ .Title = "Preferences", .Slot = ui::DockSlot::Floating, .OpenAtStart = false, .DefaultSize = { 36.0f, 14.0f } }),
              m_settings(settings), m_window(window) {}
    protected:
        void Draw() override;
        void OnOpened() override;
    private:
        void DrawScriptEditor();
    private:
        EditorSettings& m_settings;
        const Window& m_window;

        // Looked for each time the window opens, as editors get installed while the editor runs
        std::vector<CodeEditor> m_found;

        std::string m_arguments;
        bool m_editingArguments = false;
    };
}
