#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <crimson/core/window.hpp>

#include "crimson_editor/editor_context.hpp"
#include "crimson_editor/editor_settings.hpp"
#include "crimson_editor/ui/drag_drop.hpp"
#include "crimson_editor/ui/icons.hpp"
#include "crimson_editor/ui/panel.hpp"
#include "crimson_editor/ui/widgets.hpp"

namespace crimson::editor
{
    // Whether a folder, as a path in the assets, is the engine's: "engine" or under it
    [[nodiscard]] bool IsEngineFolder(std::string_view folder);

    // Whether the Assets panel lists an asset. The engine's only show when asked for, and the renderer's own never.
    [[nodiscard]] bool ListsAsset(const AssetInfo& info, bool showEngineContent);

    // Browses the project's assets, and the engine's under a root of their own when the settings ask for them
    class AssetBrowserPanel : public ui::Panel
    {
    public:
        AssetBrowserPanel(EditorContext& context, EditorSettings& settings, const Window& window)
            : ui::Panel({ .Title = "Assets", .Icon = ui::icons::kFolder, .Slot = ui::DockSlot::Bottom }),
              m_context(context), m_settings(settings), m_window(window) {}

        // Shows an asset in the grid, and flashes it, as when a field points at it. Opens the panel if it is closed.
        void Ping(AssetID asset);
    protected:
        void Draw() override;
    private:
        struct Item
        {
            AssetID Id;
            AssetType Type;
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
        void DrawFolderRoots();
        void DrawGrid();
        void DrawFolderTile(const std::string& folder, float tileSize);
        void DrawAssetTile(const Item& item, float tileSize);
        void DrawFolderMenu(const std::string& folder);
        void DrawBackgroundMenu();
        void DrawDialogs();
        void AcceptMovedAsset(const std::string& folder, ui::drag_drop::DropLook look);

        void OpenDialog(Dialog dialog, std::string name);
        void Confirm();
        void MoveAssetToFolder(AssetID asset, const std::string& folder);
        [[nodiscard]] std::string JoinToCurrent(const std::string& name) const;
    private:
        EditorContext& m_context;
        EditorSettings& m_settings;
        const Window& m_window;

        bool m_builtOnce = false;
        uint64_t m_builtVersion = 0;
        bool m_builtShowingEngine = false;
        bool m_reservedFolderFound = false; // the project has a folder named engine, whose files can't be assets
        bool m_wasFocused = true;

        std::vector<Item> m_items;
        std::vector<std::string> m_folders;
        std::string m_currentFolder; // empty for the top

        ui::SearchFilter m_filter;

        AssetID m_pinged; // flashes in the grid for a moment, after a field pointed at it
        double m_pingStart = 0.0;
        bool m_scrollToPinged = false;

        Dialog m_dialog = Dialog::None;
        std::string m_dialogName;
        AssetID m_dialogAsset;
        std::string m_dialogFolder;
    };
}
