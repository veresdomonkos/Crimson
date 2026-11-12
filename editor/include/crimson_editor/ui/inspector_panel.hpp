#pragma once
#include "crimson_editor/editor_context.hpp"
#include "crimson_editor/ui/asset_inspector.hpp"
#include "panel.hpp"

namespace crimson::editor::ui
{
    class InspectorPanel : public Panel
    {
    public:
        explicit InspectorPanel(EditorContext& context) : m_context(context), m_assetInspector(context) {}
        void OnImGui() override;
    private:
        void DrawHeader(Entity entity);

        // Any registered component from its fields, except for the few that have a look of their own below
        void DrawComponent(Entity entity, const ComponentType& type);
        void DrawTransform(TransformComponent& transform);
        void DrawMeshRenderer(MeshRendererComponent& renderer);

        void DrawUnknownComponents(Entity entity);
        void DrawSceneInstance(Entity entity);
        void DrawAddComponent(Entity entity);
    private:
        EditorContext& m_context;
        AssetInspector m_assetInspector;
        bool m_uniformScale = false;
    };
}
