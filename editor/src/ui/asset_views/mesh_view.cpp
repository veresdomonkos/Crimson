#include "crimson_editor/ui/asset_type_registry.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>

#include <imgui.h>

#include <crimson/asset_manager/slot_materials.hpp>
#include <crimson/scene/component_registry.hpp>

#include "crimson_editor/editor_context.hpp"
#include "crimson_editor/system_shell.hpp"
#include "crimson_editor/ui/icons.hpp"
#include "crimson_editor/ui/property_fields.hpp"

namespace crimson::editor::ui
{
    namespace
    {
        class MeshView : public AssetView
        {
        public:
            explicit MeshView(EditorContext& context) : m_context(context) {}

            void Draw(const AssetInfo& info) override;
        private:
            EditorContext& m_context;
        };

        void MeshView::Draw(const AssetInfo& info)
        {
            const MeshID id = m_context.Assets.As<AssetType::Mesh>(info.Id);

            if (!m_context.Assets.IsLoaded(id))
            {
                // It can take seconds, so only when asked
                if (ImGui::Button("Load to see details"))
                    m_context.Deferred.emplace_back([this, id] { m_context.Assets.Load(id); });

                return;
            }

            const Mesh* mesh = m_context.Assets.Load(id);
            if (!mesh)
            {
                ImGui::TextDisabled("This mesh can't be read, see the console");
                return;
            }

            uint64_t indices = 0;
            for (const SubMesh& subMesh : mesh->SubMeshes)
                indices += subMesh.IndexCount;

            Section("Details", false, [&]
            {
                if (!props::BeginTable("##MeshDetails"))
                    return;

                const auto vector = [](const glm::vec3& v)
                {
                    std::array<char, 64> text{};
                    std::snprintf(text.data(), text.size(), "%.2f, %.2f, %.2f", v.x, v.y, v.z);
                    return std::string(text.data());
                };

                props::Text("Triangles", std::to_string(indices / 3).c_str());
                props::Text("Sub-meshes", std::to_string(mesh->SubMeshes.size()).c_str());
                props::Text("Bounds Min", vector(mesh->Bounds.Min).c_str());
                props::Text("Bounds Max", vector(mesh->Bounds.Max).c_str());
                props::EndTable();
            });

            Section("Material Slots", false, [&]
            {
                for (const std::string& slot : mesh->MaterialSlots)
                    ImGui::BulletText("%s", slot.c_str());
            });
        }
    }

    EditorAssetType MeshAssetType()
    {
        return {
            .Type = AssetType::Mesh,
            .Name = "Mesh",
            .Icon = icons::kMesh,
            .Color = ImVec4(0.38f, 0.54f, 0.68f, 1.00f),
            .MakeView = [](EditorContext& context) { return std::make_unique<MeshView>(context); },
            .OpenFile = [](EditorContext&, const std::filesystem::path& file) { shell::OpenInDefaultApp(file); },
            .Actions = {
                AssetAction{
                    .Label = "Create Materials from .mtl",
                    .Tooltip = "Makes materials/<slot>.mat next to it for each slot, and leaves the ones that are there",
                    .Shown = [](const std::filesystem::path& file) { return file.extension() == ".obj"; },
                    .Run = [](EditorContext& context, AssetID id)
                    {
                        // The materials its .mtl describes, one for each slot that has none
                        context.Deferred.emplace_back([&context, id]
                        {
                            if (slot_materials::Create(context.Assets, context.Assets.As<AssetType::Mesh>(id)).Created.empty())
                                return;

                            ++context.AssetsVersion;
                            ResolveSceneAssets(context.ActiveScene, context.Assets);
                        });
                    }
                }
            }
        };
    }
}
