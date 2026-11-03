#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include <imgui.h>

#include <crimson/core/window.hpp>

#include "crimson_editor/editor_context.hpp"
#include "crimson_editor/ui/panel.hpp"

namespace crimson::editor::ui
{
    // Browses the project's assets, and the engine's under their own folder
    class AssetBrowserPanel : public Panel
    {
    public:
        AssetBrowserPanel(EditorContext& context, const Window& window) : m_context(context), m_window(window) {}

        void OnImGui() override;
    private:
        struct Item
        {
            AssetID Id;
            AssetType Type = AssetType::None;
            std::string Path;
            std::string Name;
            std::string Folder;
            bool ReadOnly = false;
        };

        enum class Dialog
        {
            None,
            NewFolder,
            NewMaterial,
            NewScene,
            NewShader,
            RenameAsset,
            RenameFolder,
            DeleteAsset,
            DeleteFolder
        };

        void Rebuild();
        void RefreshFromDisk();
        void RefreshWhenRefocused();

        void DrawToolbar();
        void DrawFolderTree(const std::string& parent);
        void DrawGrid();
        void DrawFolderTile(const std::string& folder, float tileSize);
        void DrawAssetTile(const Item& item, float tileSize);
        void DrawFolderMenu(const std::string& folder);
        void DrawBackgroundMenu();
        void DrawDialogs();

        void OpenDialog(Dialog dialog, std::string name);
        void Confirm();
        void MoveAssetToFolder(AssetID asset, const std::string& folder);
        [[nodiscard]] std::string JoinToCurrent(const std::string& name) const;
    private:
        EditorContext& m_context;
        const Window& m_window;

        bool m_builtOnce = false;
        uint64_t m_builtVersion = 0;
        bool m_wasFocused = true;

        std::vector<Item> m_items;
        std::vector<std::string> m_folders;
        std::string m_currentFolder; // empty for the top

        ImGuiTextFilter m_filter;

        Dialog m_dialog = Dialog::None;
        bool m_openDialog = false;
        std::array<char, 256> m_dialogName{};
        AssetID m_dialogAsset;
        std::string m_dialogFolder;
    };
}
