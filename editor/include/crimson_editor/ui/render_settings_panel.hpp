#pragma once

#include <crimson/scene/scene_renderer.hpp>

#include "crimson_editor/editor_context.hpp"
#include "crimson_editor/ui/panel.hpp"

namespace crimson::editor::ui
{
    // How the scene is lit and shown. These last until the project closes, as nothing saves scenes yet.
    class RenderSettingsPanel : public Panel
    {
    public:
        RenderSettingsPanel(EditorContext& context, SceneRenderer& renderer, FrameLightingData& lighting)
            : m_context(context), m_renderer(renderer), m_lighting(lighting) {}

        void OnImGui() override;
    private:
        EditorContext& m_context;
        SceneRenderer& m_renderer;
        FrameLightingData& m_lighting;

        float m_exposure = 1.0f;
        ToneMapper m_toneMapper = ToneMapper::Aces;
    };
}
