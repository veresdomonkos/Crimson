#include "crimson_editor/ui/asset_type_registry.hpp"

#include <imgui.h>

#include "crimson_editor/editor_context.hpp"
#include "crimson_editor/ui/icons.hpp"

namespace crimson::editor::ui
{
    namespace
    {
        // Named for what it draws, as the Scene view's SceneView is another thing
        class SceneAssetView : public AssetView
        {
        public:
            explicit SceneAssetView(EditorContext& context) : m_context(context) {}

            void Draw(const AssetInfo& info) override;
        private:
            EditorContext& m_context;
        };

        void SceneAssetView::Draw(const AssetInfo& info)
        {
            const SceneID scene = m_context.Assets.As<AssetType::Scene>(info.Id);
            const bool open = scene == m_context.ActiveSceneAsset;

            Section("Scene", false, [&]
            {
                ImGui::BeginDisabled(open);
                if (ImGui::Button(open ? "Open Already" : "Open", ImVec2(-1.0f, 0.0f)))
                    m_context.OpenScene(scene);
                ImGui::EndDisabled();

                ImGui::TextDisabled("Drag it into the Scene view to use it in another scene");
            });
        }
    }

    EditorAssetType SceneAssetType()
    {
        return {
            .Type = AssetType::Scene,
            .Name = "Scene",
            .Icon = icons::kScene,
            .Color = ImVec4(0.70f, 0.50f, 0.36f, 1.00f),
            .MakeView = [](EditorContext& context) { return std::make_unique<SceneAssetView>(context); }
        };
    }
}
