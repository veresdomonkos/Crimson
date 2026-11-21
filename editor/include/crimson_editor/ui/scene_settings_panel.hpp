#pragma once

#include "crimson_editor/editor_context.hpp"
#include "crimson_editor/ui/icons.hpp"
#include "crimson_editor/ui/panel.hpp"

namespace crimson::editor::ui
{
    // What is saved with the scene apart from its entities: its ambient light, main camera and post-processing
    class SceneSettingsPanel : public Panel
    {
    public:
        explicit SceneSettingsPanel(EditorContext& context)
            : Panel({ .Title = "Scene Settings", .Icon = ui::icons::kScene, .Slot = DockSlot::RightBottom, .TabOrder = 2, .DefaultSize = { 15.0f, 0.0f } }),
              m_context(context) {}
    protected:
        void Draw() override;
    private:
        void DrawMainCamera();
        void DrawPostProcessing();
    private:
        EditorContext& m_context;
    };
}
