#pragma once
#include <deque>
#include <functional>
#include <string>

#include <imgui.h>

#include <crimson/asset_manager/asset_manager.hpp>

namespace crimson::editor
{
    class AssetThumbnails;
    struct EditorContext;
}

namespace crimson::editor::ui
{
    // What the editor knows about one type of asset, registered in RegisterAssetTypes. The editor's panels draw and offer
    // any type from this, as they do a component from its ComponentType.
    struct EditorAssetType
    {
        AssetType Type = AssetType::None;
        std::string Name;
        const char* Icon = nullptr;
        ImVec4 Color;

        // The picture an asset of this type is shown as in the grid, the picker and fields, or 0 if it can't be loaded.
        // Empty for the types that are shown by their icon.
        std::function<ImTextureID(const AssetManager&, AssetThumbnails&, AssetID)> Thumbnail;
    };

    class AssetTypeRegistry
    {
    public:
        void Register(EditorAssetType type);

        // Null for a type that isn't registered
        [[nodiscard]] const EditorAssetType* Find(AssetType type) const;

        // A plain file's look for a type that isn't registered
        [[nodiscard]] const EditorAssetType& Get(AssetType type) const;

        // In the order they were registered. A deque, so pointers to the types stay valid.
        [[nodiscard]] const std::deque<EditorAssetType>& Types() const { return m_types; }
    private:
        std::deque<EditorAssetType> m_types;
    };

    // The one place that lists the asset types, in the order the editor offers them
    void RegisterAssetTypes(AssetTypeRegistry& registry);

    // Each type's description, defined next to its view in asset_views/
    EditorAssetType MaterialAssetType();
    EditorAssetType MeshAssetType();
    EditorAssetType SceneAssetType();
    EditorAssetType ShaderAssetType();
    EditorAssetType TextureAssetType();
}
