#pragma once
#include <algorithm>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <imgui.h>

#include <crimson/asset_manager/asset_manager.hpp>

#include "crimson_editor/ui/editor_theme.hpp"

namespace crimson::editor
{
    class AssetThumbnails;
    struct EditorContext;
}

namespace crimson::editor::ui
{
    // How the Inspector draws an asset of one type: its sections, and the state they keep between frames, such as a
    // texture's import settings that aren't applied yet. One is made for a type when it is first selected.
    class AssetView
    {
    public:
        virtual ~AssetView() = default;

        virtual void Draw(const AssetInfo& info) = 0;
    protected:
        // A section whose content is disabled, as a read-only asset's is, while the section itself can still be collapsed
        template <typename Content>
        static void Section(const char* label, bool disabled, Content&& draw)
        {
            if (!theme::BeginSection(label))
                return;

            ImGui::BeginDisabled(disabled);
            draw();
            ImGui::EndDisabled();

            theme::EndSection();
        }

        template <typename T>
        static std::vector<std::pair<std::string, T>> SortedByName(const std::unordered_map<std::string, T>& map)
        {
            std::vector<std::pair<std::string, T>> items(map.begin(), map.end());
            std::ranges::sort(items, {}, [](const auto& item) { return item.first; });
            return items;
        }
    };

    // How the Assets panel's menus make a new asset of a type
    struct AssetCreation
    {
        std::string Label;       // the menu entry and the dialog's title
        std::string DefaultName; // what the dialog starts with
        std::function<AssetID(AssetManager&, const std::string& pathWithoutExtension)> Create; // gives an empty ID if it fails
    };

    // An entry of the Assets panel's context menu for the assets of a type
    struct AssetAction
    {
        std::string Label;
        std::string Tooltip;
        std::function<bool(const std::filesystem::path& file)> Shown;
        std::function<void(EditorContext&, AssetID)> Run;
    };

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

        // Makes the Inspector's view of this type
        std::function<std::unique_ptr<AssetView>(EditorContext&)> MakeView;

        // Empty for the types that aren't made from the Assets panel
        std::optional<AssetCreation> Creation;

        // What a double-click does in the editor itself, like opening a scene. Without it a double-click opens the file.
        std::function<void(EditorContext&, AssetID)> Open;

        // Opens the file in another program, from the context menu's Open and from a double-click without Open. Empty for
        // the types that are edited in the editor, whose files aren't meant to be changed by hand.
        std::function<void(EditorContext&, const std::filesystem::path&)> OpenFile;

        std::vector<AssetAction> Actions;
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
