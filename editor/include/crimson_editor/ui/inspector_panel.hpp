#pragma once
#include "crimson_editor/editor_context.hpp"
#include "panel.hpp"

namespace crimson::editor::ui
{
    class InspectorPanel : public Panel
    {
    public:
        explicit InspectorPanel(EditorContext& context) : m_context(context) {}
        void OnImGui() override;
    private:
        void DrawHeader(Entity entity);
        void DrawTransform(Entity entity);
        void DrawLight(Entity entity);
        void DrawMeshRenderer(Entity entity);
        void DrawAddComponent(Entity entity);
    private:
        EditorContext& m_context;
        bool m_uniformScale = false;
    };
}
