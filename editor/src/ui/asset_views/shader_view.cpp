#include "crimson_editor/ui/asset_type_registry.hpp"

#include <filesystem>

#include <imgui.h>

#include "crimson_editor/editor_context.hpp"
#include "crimson_editor/system_shell.hpp"
#include "crimson_editor/ui/editor_theme.hpp"
#include "crimson_editor/ui/icons.hpp"

namespace crimson::editor::ui
{
    namespace
    {
        class ShaderView : public AssetView
        {
        public:
            explicit ShaderView(EditorContext& context) : m_context(context) {}

            void Draw(const AssetInfo& info) override;
        private:
            EditorContext& m_context;
        };

        void ShaderView::Draw(const AssetInfo& info)
        {
            const ShaderHandle shader = m_context.Assets.Load(m_context.Assets.As<AssetType::Shader>(info.Id));
            if (!shader)
            {
                ImGui::TextDisabled("This shader doesn't compile, see the console");
                return;
            }

            const ShaderInterface& shaderInterface = m_context.Assets.GetShaderInterface(shader);

            if (!shaderInterface.Properties.empty())
            {
                Section("Properties", false, [&]
                {
                    for (const auto& [name, property] : SortedByName(shaderInterface.Properties))
                        ImGui::BulletText("%s (%zu bytes)", name.c_str(), property.Size);
                });
            }

            if (!shaderInterface.Textures.empty())
            {
                Section("Textures", false, [&]
                {
                    for (const auto& [name, texture] : SortedByName(shaderInterface.Textures))
                        ImGui::BulletText("%s", name.c_str());
                });
            }
        }
    }

    EditorAssetType ShaderAssetType()
    {
        return {
            .Type = AssetType::Shader,
            .Name = "Shader",
            .Icon = icons::kShader,
            .Color = theme::CategoryViolet,
            .MakeView = [](EditorContext& context) { return std::make_unique<ShaderView>(context); },
            .Creation = AssetCreation{
                .Label = "New Shader",
                .DefaultName = "New Shader",
                .Create = [](AssetManager& assets, const std::string& path) { return AssetID(assets.Create<AssetType::Shader>(path + ".hlsl")); }
            },
            .OpenFile = [](EditorContext& context, const std::filesystem::path& file)
            {
                // In the script editor from the preferences, if there is one
                if (context.OpenCodeFile)
                    context.OpenCodeFile(file);
                else
                    shell::OpenInDefaultApp(file);
            }
        };
    }
}
