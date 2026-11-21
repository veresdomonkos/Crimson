#pragma once
#include "crimson_editor/editor_context.hpp"
#include "crimson_editor/panels/asset_inspector.hpp"
#include "crimson_editor/ui/icons.hpp"
#include "crimson_editor/ui/panel.hpp"

namespace crimson::editor
{
    class InspectorPanel : public ui::Panel
    {
    public:
        explicit InspectorPanel(EditorContext& context)
            : ui::Panel({ .Title = "Inspector", .Icon = ui::icons::kTransform, .Slot = ui::DockSlot::Right }),
              m_context(context), m_assetInspector(context) {}
    protected:
        void Draw() override;
    private:
        void DrawHeader(Entity entity);

        // Any registered component, from its fields
        void DrawComponent(Entity entity, const ComponentType& type);

        void DrawUnknownComponents(Entity entity);
        void DrawSceneInstance(Entity entity);
        void DrawAddComponent(Entity entity);
    private:
        EditorContext& m_context;
        AssetInspector m_assetInspector;
    };
}
