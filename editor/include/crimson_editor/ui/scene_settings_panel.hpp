#pragma once

#include <crimson/scene/scene_renderer.hpp>

#include "crimson_editor/editor_context.hpp"
#include "crimson_editor/ui/panel.hpp"

namespace crimson::editor::ui
{
    inline constexpr const char* kSceneSettingsWindow = "Scene Settings";

    // What is saved with the scene apart from its entities: its ambient light, main camera and post-processing
    class SceneSettingsPanel : public Panel
    {
    public:
        SceneSettingsPanel(EditorContext& context, SceneRenderer& renderer, SceneSettings& settings)
            : m_context(context), m_renderer(renderer), m_settings(settings) {}

        void OnImGui() override;
    private:
        void DrawMainCamera();
        void DrawPostProcessing();
    private:
        EditorContext& m_context;
        SceneRenderer& m_renderer;
        SceneSettings& m_settings;
    };
}
