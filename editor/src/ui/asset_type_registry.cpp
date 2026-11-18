#include "crimson_editor/ui/asset_type_registry.hpp"

#include <algorithm>
#include <utility>

#include "crimson_editor/ui/editor_theme.hpp"
#include "crimson_editor/ui/icons.hpp"

namespace crimson::editor::ui
{
    void AssetTypeRegistry::Register(EditorAssetType type)
    {
        m_types.push_back(std::move(type));
    }

    const EditorAssetType* AssetTypeRegistry::Find(AssetType type) const
    {
        const auto it = std::ranges::find(m_types, type, &EditorAssetType::Type);
        return it == m_types.end() ? nullptr : &*it;
    }

    const EditorAssetType& AssetTypeRegistry::Get(AssetType type) const
    {
        static const EditorAssetType kFile = { .Name = "File", .Icon = icons::kFile, .Color = theme::CategoryNeutral };

        const EditorAssetType* found = Find(type);
        return found ? *found : kFile;
    }

    void RegisterAssetTypes(AssetTypeRegistry& registry)
    {
        registry.Register(MaterialAssetType());
        registry.Register(MeshAssetType());
        registry.Register(SceneAssetType());
        registry.Register(ShaderAssetType());
        registry.Register(TextureAssetType());
    }
}
