#pragma once
#include "crimson_editor/editor_context.hpp"
#include "crimson_editor/ui/panel.hpp"

namespace crimson::editor
{
    // The project's settings, saved in project.crimson and shared by everyone on the project, unlike the per-user
    // Preferences. Opened from Edit > Project Settings or View.
    class ProjectSettingsPanel : public ui::Panel
    {
    public:
        explicit ProjectSettingsPanel(EditorContext& context)
            : ui::Panel({ .Title = "Project Settings", .Slot = ui::DockSlot::Floating, .OpenAtStart = false, .DefaultSize = { 30.0f, 10.0f } }),
              m_context(context) {}
    protected:
        void Draw() override;
    private:
        EditorContext& m_context;
    };
}
