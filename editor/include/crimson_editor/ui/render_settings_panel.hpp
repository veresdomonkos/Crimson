#pragma once

#include <crimson/scene/scene_renderer.hpp>

#include "crimson_editor/editor_context.hpp"
#include "crimson_editor/ui/panel.hpp"

namespace crimson::editor::ui
{
    // How the scene is lit and shown, which is saved with the scene
    class RenderSettingsPanel : public Panel
    {
    public:
        RenderSettingsPanel(EditorContext& context, SceneRenderer& renderer, SceneEnvironment& environment)
            : m_context(context), m_renderer(renderer), m_environment(environment) {}

        void OnImGui() override;
    private:
        EditorContext& m_context;
        SceneRenderer& m_renderer;
        SceneEnvironment& m_environment;
    };
}
