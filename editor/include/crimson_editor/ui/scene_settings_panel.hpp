#pragma once

#include "crimson_editor/editor_context.hpp"
#include "crimson_editor/ui/panel.hpp"

namespace crimson::editor::ui
{
    inline constexpr const char* kSceneSettingsWindow = "Scene Settings";

    // What is saved with the scene apart from its entities: its ambient light, main camera and post-processing
    class SceneSettingsPanel : public Panel
    {
    public:
        explicit SceneSettingsPanel(EditorContext& context) : m_context(context) {}

        void OnImGui() override;
    private:
        void DrawMainCamera();
        void DrawPostProcessing();
    private:
        EditorContext& m_context;
    };
}
