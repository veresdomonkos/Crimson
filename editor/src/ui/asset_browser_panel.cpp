#include "crimson_editor/ui/asset_browser_panel.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <set>
#include <system_error>

#include <crimson/asset_manager/slot_materials.hpp>
#include <crimson/core/log.hpp>
#include <crimson/project/project.hpp>

#include "crimson_editor/system_shell.hpp"
#include "crimson_editor/panels/asset_look.hpp"
#include "crimson_editor/ui/asset_drag.hpp"
#include "crimson_editor/ui/asset_tiles.hpp"
#include "crimson_editor/ui/editor_theme.hpp"
#include "crimson_editor/ui/icons.hpp"
#include "crimson_editor/ui/input.hpp"
#include "crimson_editor/ui/layout.hpp"
#include "crimson_editor/ui/menus.hpp"
#include "crimson_editor/ui/painter.hpp"
#include "crimson_editor/ui/widgets.hpp"

namespace fs = std::filesystem;

namespace crimson::editor::ui
{
    constexpr std::string_view kEngineFolder = "engine";
    constexpr float kPingSeconds = 1.5f;

    static std::string ParentOf(const std::string& path)
    {
        const size_t slash = path.find_last_of('/');
        return slash == std::string::npos ? std::string{} : path.substr(0, slash);
    }

    bool IsEngineFolder(std::string_view folder)
    {
        return folder == kEngineFolder || (folder.starts_with(kEngineFolder) && folder.size() > kEngineFolder.size() && folder[kEngineFolder.size()] == '/');
    }

    bool ListsAsset(const AssetInfo& info, bool showEngineContent)
    {
        return !IsInternal(info) && (showEngineContent || !info.ReadOnly);
    }

    // Whether an asset is edited in another program. Scenes and materials are edited in the editor, and their JSON
    // isn't meant to be changed by hand.
    static bool OpensOutside(AssetType type)
    {
        return type.Is<ShaderAsset>() || type.Is<TextureAsset>() || type.Is<MeshAsset>();
    }

    void AssetBrowserPanel::Rebuild()
    {
        m_builtOnce = true;
        m_builtVersion = m_context.Session.GetAssets().Version();
        m_builtShowingEngine = m_settings.GetShowEngineContent();
        m_reservedFolderFound = false;

        m_items.clear();
        std::set<std::string> folders;

        const auto addFolderAndParents = [&folders](std::string folder)
        {
            while (!folder.empty())
            {
                folders.insert(folder);
                folder = ParentOf(folder);
            }
        };

        for (const AssetInfo* info : m_context.Session.GetAssets().ListAll())
        {
            if (!ListsAsset(*info, m_builtShowingEngine))
                continue;

            const std::string folder = ParentOf(info->Path);
            m_items.push_back({ info->Id, info->Type, info->Path, AssetFileName(info->Path), folder, info->ReadOnly });
            addFolderAndParents(folder);
        }

        // Empty folders exist too, and only the disk knows about them
        std::error_code error;
        const fs::path root = m_context.Session.GetAssets().GetRoot();

        for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, error), end; !error && it != end; it.increment(error))
        {
            if (!it->is_directory(error))
                continue;

            // The project's own engine folder is skipped by the scan, as the engine's assets are there
            const std::string folder = fs::relative(it->path(), root, error).generic_string();
            if (IsEngineFolder(folder))
            {
                m_reservedFolderFound = true;
                it.disable_recursion_pending();
                continue;
            }

            addFolderAndParents(folder);
        }

        m_folders.assign(folders.begin(), folders.end());

        if (!m_currentFolder.empty() && !folders.contains(m_currentFolder))
            m_currentFolder.clear();
    }

    void AssetBrowserPanel::RefreshFromDisk()
    {
        m_context.Deferred.emplace_back([this]
        {
            const std::vector<AssetID> removed = m_context.Session.GetAssets().Refresh();
            m_context.OnAssetsRemoved(removed);
        });
    }

    void AssetBrowserPanel::RefreshWhenRefocused()
    {
        // Files may have been added or changed while the user was in another program
        const bool focused = m_window.IsFocused();
        if (focused && !m_wasFocused)
            RefreshFromDisk();

        m_wasFocused = focused;
    }

    std::string AssetBrowserPanel::JoinToCurrent(const std::string& name) const
    {
        return m_currentFolder.empty() ? name : m_currentFolder + "/" + name;
    }

    void AssetBrowserPanel::Ping(AssetID asset)
    {
        const AssetInfo* info = m_context.Session.GetAssets().GetInfo(asset);
        if (!info || IsInternal(*info))
            return;

        // The engine's assets are only in the grid while they are shown
        if (info->ReadOnly && !m_settings.GetShowEngineContent())
            m_settings.SetShowEngineContent(true);

        if (m_filter.IsActive() && !m_filter.Matches(info->Path))
            m_filter.Clear();

        m_currentFolder = ParentOf(info->Path);
        m_pinged = asset;
        m_pingStart = ui::input::Time();
        m_scrollToPinged = true;

        // Opens the panel if it is closed, and brings its tab to the front
        Show();
    }

    void AssetBrowserPanel::Draw()
    {
        RefreshWhenRefocused();

        if (!m_builtOnce || m_builtVersion != m_context.Session.GetAssets().Version() || m_builtShowingEngine != m_settings.GetShowEngineContent())
            Rebuild();

        DrawToolbar();

        const std::array columns = { ui::Column{ "Folders", ui::FontSize() * 11.0f }, ui::Column{ "Assets", 0.0f, true } };

        if (ui::Table table("##AssetLayout", columns, ui::TableOptions{ .Resizable = true, .InnerBorders = true }); table)
        {
            table.NextRow();

            table.NextColumn();
            if (const ui::Region folders("##Folders"); folders)
                DrawFolderRoots();

            table.NextColumn();
            if (const ui::Region grid("##Grid"); grid)
                DrawGrid();
        }

        DrawDialogs();
    }

    void AssetBrowserPanel::DrawToolbar()
    {
        {
            ui::Row row;

            if (ui::Button("Refresh"))
                RefreshFromDisk();

            ui::NextItemWidth(ui::FontSize() * 12.0f);
            ui::SearchBox("##Search", m_filter, "Search");

            if (bool show = m_settings.GetShowEngineContent(); ui::Checkbox("Show Engine Content", show))
                m_settings.SetShowEngineContent(show);

            // The path to the current folder, each part leading back to it. The engine's start at a root of their own.
            const bool inEngine = IsEngineFolder(m_currentFolder);

            if (ui::SmallButton(inEngine ? "Engine" : "Assets"))
                m_currentFolder = inEngine ? std::string(kEngineFolder) : std::string();

            size_t start = inEngine ? std::min(kEngineFolder.size() + 1, m_currentFolder.size()) : 0;
            while (start < m_currentFolder.size())
            {
                const size_t slash = m_currentFolder.find('/', start);
                const size_t end = slash == std::string::npos ? m_currentFolder.size() : slash;

                ui::Text("/", ui::TextStyle::Muted);

                const std::string part = m_currentFolder.substr(start, end - start);

                {
                    const ui::IdScope scope(static_cast<int>(start));
                    if (ui::SmallButton(part))
                        m_currentFolder.resize(end);
                }

                if (m_currentFolder.size() <= end)
                    break;

                start = end + 1;
            }
        }

        if (m_reservedFolderFound)
            ui::Text("The project's engine folder is reserved for the engine's assets, so what's in it doesn't show. Rename it.", ui::theme::Warning);
    }

    // Dropping an asset on the last item moves it to the folder. A target with rounded corners draws its own highlight.
    void AssetBrowserPanel::AcceptMovedAsset(const std::string& folder, ui::drag_drop::DropLook look)
    {
        const AssetManager& assets = m_context.Session.GetAssets();

        for (const AssetType type : { AssetType::Of<MaterialAsset>(), AssetType::Of<MeshAsset>(), AssetType::Of<ShaderAsset>(), AssetType::Of<TextureAsset>(), AssetType::Of<SceneAsset>() })
        {
            if (const AssetID dropped = asset_drag::AcceptOnLastItem(assets, type, look))
                MoveAssetToFolder(dropped, folder);

            if (look == ui::drag_drop::DropLook::None)
                asset_drag::DrawDropHighlight(assets, type, ui::item::Bounds(), ui::theme::Scaled(ui::theme::Radius));
        }
    }

    void AssetBrowserPanel::DrawFolderRoots()
    {
        // The project's folders, without the engine's
        {
            const bool selected = m_currentFolder.empty();
            const ui::TreeRow root("##AssetsRoot", ui::TreeRowOptions{
                .Icon = selected ? ui::icons::kFolderOpen : ui::icons::kFolder, .IconColor = ui::theme::Folder,
                .Label = "Assets", .Selected = selected, .DefaultOpen = true });

            if (root.Clicked())
                m_currentFolder.clear();

            AcceptMovedAsset({}, ui::drag_drop::DropLook::Default);

            if (root.IsOpen())
                DrawFolderTree({});
        }

        if (!m_settings.GetShowEngineContent())
            return;

        // The engine's, which can be looked at and used but not changed
        ui::Spacing();
        ui::Separator();
        ui::Spacing();

        const std::string engine(kEngineFolder);
        const bool selected = m_currentFolder == engine;

        const ui::TreeRow root("##EngineRoot", ui::TreeRowOptions{
            .Icon = selected ? ui::icons::kFolderOpen : ui::icons::kFolder, .IconColor = ui::theme::Folder,
            .Label = "Engine", .Selected = selected, .DefaultOpen = true, .Muted = true });

        ui::Tooltip("The engine's built-in assets, which can be used but not changed");

        if (root.Clicked())
            m_currentFolder = engine;

        if (root.IsOpen())
            DrawFolderTree(engine);
    }

    void AssetBrowserPanel::DrawFolderTree(const std::string& parent)
    {
        for (const std::string& folder : m_folders)
        {
            if (ParentOf(folder) != parent || (parent.empty() && IsEngineFolder(folder)))
                continue;

            const bool hasChildren = std::ranges::any_of(m_folders, [&folder](const std::string& other) { return ParentOf(other) == folder; });
            const bool current = folder == m_currentFolder;

            const ui::TreeRow row(folder, ui::TreeRowOptions{
                .Icon = current ? ui::icons::kFolderOpen : ui::icons::kFolder, .IconColor = ui::theme::Folder,
                .Label = AssetFileName(folder), .Selected = current, .Leaf = !hasChildren });

            if (row.Clicked())
                m_currentFolder = folder;

            if (!IsEngineFolder(folder))
            {
                AcceptMovedAsset(folder, ui::drag_drop::DropLook::Default);
                DrawFolderMenu(folder);
            }

            if (row.IsOpen())
                DrawFolderTree(folder);
        }
    }

    void AssetBrowserPanel::DrawGrid()
    {
        const float tileSize = ui::FontSize() * 5.5f;
        const ui::TileGrid grid(tileSize);

        // Searching looks through every folder
        const bool searching = m_filter.IsActive();

        if (!searching)
        {
            for (const std::string& folder : m_folders)
            {
                if (ParentOf(folder) == m_currentFolder && !(m_currentFolder.empty() && IsEngineFolder(folder)))
                    DrawFolderTile(folder, tileSize);
            }
        }

        for (const Item& item : m_items)
        {
            if (searching ? !m_filter.Matches(item.Path) : item.Folder != m_currentFolder)
                continue;

            DrawAssetTile(item, tileSize);
        }

        // Anywhere in the grid that isn't a tile: clicking clears the selection
        if (ui::BackgroundClicked())
            m_context.Session.SelectAsset({});

        DrawBackgroundMenu();
    }

    void AssetBrowserPanel::DrawFolderTile(const std::string& folder, float tileSize)
    {
        const ui::IdScope scope(folder);

        const ui::TileLayout tile = ui::Tile("##Folder", tileSize, false);

        if (ui::item::DoubleClicked())
            m_currentFolder = folder;

        ui::Painter painter;
        painter.Icon(tile.Icon, ui::icons::kFolder, ui::theme::Folder);
        painter.ClippedText(tile.LabelPosition, tile.LabelWidth, AssetFileName(folder), ui::theme::Text);

        if (!IsEngineFolder(folder))
        {
            AcceptMovedAsset(folder, ui::drag_drop::DropLook::None);
            DrawFolderMenu(folder);
        }
    }

    void AssetBrowserPanel::DrawAssetTile(const Item& item, float tileSize)
    {
        const ui::IdScope scope(std::hash<AssetID>{}(item.Id));

        const bool selected = m_context.Session.GetSelectedAsset() == item.Id;

        const ui::TileLayout tile = ui::Tile("##Asset", tileSize, selected);
        if (tile.Clicked)
            m_context.Session.SelectAsset(item.Id);

        ui::Painter painter;

        if (item.Id == m_pinged)
        {
            const float left = 1.0f - static_cast<float>(ui::input::Time() - m_pingStart) / kPingSeconds;
            if (left > 0.0f)
                painter.FillRect(ui::item::Bounds(), ui::WithAlpha(ui::theme::Accent, ui::theme::TintHoverAlpha * left), ui::theme::Scaled(ui::theme::Radius));

            if (m_scrollToPinged)
            {
                ui::ScrollHere(0.5f);
                m_scrollToPinged = false;
            }
        }

        // A scene opens in the editor. Shaders open in the script editor from the preferences, and textures and
        // meshes in the program the system has for them. A material's editor is the Inspector, so it only gets selected.
        const fs::path file = m_context.Session.GetAssets().GetFilePath(item.Id);
        const bool editedOutside = OpensOutside(item.Type);

        const auto openOutside = [&]
        {
            if (item.Type.Is<ShaderAsset>() && m_context.OpenCodeFile)
                m_context.OpenCodeFile(file);
            else
                shell::OpenInDefaultApp(file);
        };

        if (ui::item::DoubleClicked())
        {
            if (const SceneID scene = m_context.Session.GetAssets().As<SceneAsset>(item.Id))
                m_context.Session.OpenScene(scene);
            else if (editedOutside && !file.empty())
                openOutside();
        }

        ui::Tooltip(item.Path, ui::TooltipDelay::None);

        if (const AssetInfo* info = m_context.Session.GetAssets().GetInfo(item.Id))
            asset_drag::DragFromLastItem(*info);

        // Only pictures that are on screen get loaded
        TextureHandle picture;
        if (item.Type.Is<TextureAsset>() && ui::item::Visible())
            picture = m_context.Thumbnails.Get(m_context.Session.GetAssets().As<TextureAsset>(item.Id));

        DrawAssetPicture(painter, tile.Icon, item.Type, picture);

        const bool dirty = m_context.Session.GetAssetEdits().IsDirty(item.Id);
        const std::string name = dirty ? "* " + item.Name : item.Name;
        painter.ClippedText(tile.LabelPosition, tile.LabelWidth, name, item.ReadOnly ? ui::theme::TextMuted : ui::theme::Text);

        const ui::ContextMenu menu("##AssetMenu");
        if (!menu)
            return;

        // An engine asset is built into the editor, so there is no file to open or show
        const bool onDisk = !file.empty();
        const auto explainIfBuiltIn = [onDisk]
        {
            if (!onDisk)
                ui::Tooltip("Built into the editor, so there is no file");
        };

        if (editedOutside)
        {
            if (ui::MenuItem("Open", {}, onDisk))
                openOutside();

            explainIfBuiltIn();
        }

        if (ui::MenuItem(shell::RevealInFileManagerLabel(), {}, onDisk))
            shell::RevealInFileManager(file);

        explainIfBuiltIn();

        if (item.ReadOnly)
            return;

        ui::Separator();

        // The materials its .mtl describes, one for each slot that has none
        if (item.Type.Is<MeshAsset>() && file.extension() == ".obj")
        {
            if (ui::MenuItem("Create Materials from .mtl"))
            {
                m_context.Deferred.emplace_back([this, id = item.Id]
                {
                    slot_materials::Create(m_context.Session.GetAssets(), m_context.Session.GetAssets().As<MeshAsset>(id));
                });
            }

            ui::Tooltip("Makes materials/<slot>.mat next to it for each slot, and leaves the ones that are there");
            ui::Separator();
        }

        if (ui::MenuItem("Rename"))
        {
            m_dialogAsset = item.Id;
            OpenDialog(Dialog::RenameAsset, fs::path(item.Name).stem().string());
        }

        if (ui::MenuItem("Delete"))
        {
            m_dialogAsset = item.Id;
            OpenDialog(Dialog::DeleteAsset, item.Name);
        }
    }

    void AssetBrowserPanel::DrawFolderMenu(const std::string& folder)
    {
        const ui::ContextMenu menu("##FolderMenu");
        if (!menu)
            return;

        if (ui::MenuItem(shell::RevealInFileManagerLabel()))
            shell::RevealInFileManager(m_context.Session.GetAssets().GetRoot() / fs::path(folder));

        ui::Separator();

        // New things go where the folder is, so that is where the grid shows them
        if (ui::MenuItem("New Material"))
        {
            m_currentFolder = folder;
            OpenDialog(Dialog::NewMaterial, "New Material");
        }

        if (ui::MenuItem("New Scene"))
        {
            m_currentFolder = folder;
            OpenDialog(Dialog::NewScene, "New Scene");
        }

        if (ui::MenuItem("New Shader"))
        {
            m_currentFolder = folder;
            OpenDialog(Dialog::NewShader, "New Shader");
        }

        if (ui::MenuItem("New Folder"))
        {
            m_currentFolder = folder;
            OpenDialog(Dialog::NewFolder, "New Folder");
        }

        ui::Separator();

        if (ui::MenuItem("Rename"))
        {
            m_dialogFolder = folder;
            OpenDialog(Dialog::RenameFolder, AssetFileName(folder));
        }

        if (ui::MenuItem("Delete"))
        {
            m_dialogFolder = folder;
            OpenDialog(Dialog::DeleteFolder, AssetFileName(folder));
        }
    }

    void AssetBrowserPanel::DrawBackgroundMenu()
    {
        if (IsEngineFolder(m_currentFolder))
            return;

        const ui::BackgroundContextMenu menu("##BackgroundMenu");
        if (!menu)
            return;

        // The folder being shown, opened in the file manager
        if (ui::MenuItem(shell::RevealInFileManagerLabel()))
            shell::OpenInDefaultApp(m_context.Session.GetAssets().GetRoot() / fs::path(m_currentFolder));

        ui::Separator();

        if (ui::MenuItem("New Material"))
            OpenDialog(Dialog::NewMaterial, "New Material");

        if (ui::MenuItem("New Scene"))
            OpenDialog(Dialog::NewScene, "New Scene");

        if (ui::MenuItem("New Shader"))
            OpenDialog(Dialog::NewShader, "New Shader");

        if (ui::MenuItem("New Folder"))
            OpenDialog(Dialog::NewFolder, "New Folder");
    }

    void AssetBrowserPanel::OpenDialog(Dialog dialog, std::string name)
    {
        m_dialog = dialog;
        m_dialogName = std::move(name);
    }

    void AssetBrowserPanel::DrawDialogs()
    {
        const auto getTitle = [this]
        {
            switch (m_dialog)
            {
                case Dialog::NewFolder:    return "New Folder";
                case Dialog::NewMaterial:  return "New Material";
                case Dialog::NewScene:     return "New Scene";
                case Dialog::NewShader:    return "New Shader";
                case Dialog::RenameAsset:
                case Dialog::RenameFolder: return "Rename";
                case Dialog::DeleteAsset:
                case Dialog::DeleteFolder: return "Delete";
                case Dialog::None:         break;
            }

            return "";
        };

        const ui::Modal modal(getTitle(), "AssetDialog", [this] { return m_dialog != Dialog::None; });
        if (!modal)
            return;

        const bool deleting = m_dialog == Dialog::DeleteAsset || m_dialog == Dialog::DeleteFolder;
        std::string problem;

        if (deleting)
        {
            ui::Text("Delete " + m_dialogName + "?");
            ui::Text(m_dialog == Dialog::DeleteFolder ? "Everything in it goes too. This can't be undone." : "This can't be undone.", ui::TextStyle::Muted);
        }
        else
        {
            ui::NextItemWidth(ui::FontSize() * 18.0f);
            ui::InputText("##Name", m_dialogName, ui::InputOptions{ .Focus = modal.IsAppearing() });

            problem = Project::ValidateName(m_dialogName);
            if (!problem.empty())
                ui::Text(problem, ui::TextStyle::Muted);
        }

        bool accepted = false;
        bool cancelled = false;

        {
            const ui::Row row;

            {
                const ui::Disabled disabled(!problem.empty());
                accepted = (deleting ? ui::Button("Delete") : ui::PrimaryButton("OK")) || (!deleting && problem.empty() && ui::input::KeyPressed(Key::Enter));
            }

            cancelled = ui::Button("Cancel") || ui::input::KeyPressed(Key::Escape);
        }

        if (accepted)
            Confirm();

        if (accepted || cancelled)
            m_dialog = Dialog::None;
    }

    void AssetBrowserPanel::Confirm()
    {
        const std::string name = m_dialogName;
        const fs::path root = m_context.Session.GetAssets().GetRoot();

        const auto refreshAfter = [this](std::function<void()> action)
        {
            m_context.Deferred.emplace_back([this, action = std::move(action)]
            {
                action();

                const std::vector<AssetID> removed = m_context.Session.GetAssets().Refresh();
                m_context.OnAssetsRemoved(removed);
            });
        };

        switch (m_dialog)
        {
            case Dialog::NewFolder:
                refreshAfter([root, path = JoinToCurrent(name)]
                {
                    std::error_code error;
                    fs::create_directories(root / fs::path(path), error);

                    if (error)
                        LOG_ERROR("[Assets] Can't create {}: {}", path, error.message());
                });
                break;

            case Dialog::NewMaterial:
                refreshAfter([this, path = JoinToCurrent(name + ".mat")]
                {
                    if (const AssetID id = m_context.Session.GetAssets().Create<MaterialAsset>(path))
                        m_context.Session.SelectAsset(id);
                });
                break;

            case Dialog::NewScene:
                refreshAfter([this, path = JoinToCurrent(name + ".scene")]
                {
                    if (const AssetID id = m_context.Session.GetAssets().Create<SceneAsset>(path))
                        m_context.Session.SelectAsset(id);
                });
                break;

            case Dialog::NewShader:
                refreshAfter([this, path = JoinToCurrent(name + ".hlsl")]
                {
                    if (const AssetID id = m_context.Session.GetAssets().Create<ShaderAsset>(path))
                        m_context.Session.SelectAsset(id);
                });
                break;

            case Dialog::RenameAsset:
                refreshAfter([this, id = m_dialogAsset, name]
                {
                    if (const AssetInfo* info = m_context.Session.GetAssets().GetInfo(id))
                    {
                        const fs::path current(info->Path);
                        const std::string folder = ParentOf(info->Path);
                        const std::string file = name + current.extension().string();

                        m_context.Session.GetAssets().Rename(id, folder.empty() ? file : folder + "/" + file);
                    }
                });
                break;

            case Dialog::RenameFolder:
                refreshAfter([this, root, folder = m_dialogFolder, name]
                {
                    const std::string parent = ParentOf(folder);
                    const std::string renamed = parent.empty() ? name : parent + "/" + name;

                    std::error_code error;
                    if (fs::exists(root / fs::path(renamed), error))
                    {
                        LOG_ERROR("[Assets] '{}' already exists", renamed);
                        return;
                    }

                    fs::rename(root / fs::path(folder), root / fs::path(renamed), error);

                    if (error)
                        LOG_ERROR("[Assets] Can't rename {}: {}", folder, error.message());
                    else if (m_currentFolder == folder)
                        m_currentFolder = renamed;
                });
                break;

            case Dialog::DeleteAsset:
                m_context.Deferred.emplace_back([this, id = m_dialogAsset]
                {
                    if (m_context.Session.GetAssets().Delete(id))
                    {
                        const std::array removed = { id };
                        m_context.OnAssetsRemoved(removed);
                    }
                });
                break;

            case Dialog::DeleteFolder:
                refreshAfter([root, folder = m_dialogFolder]
                {
                    std::error_code error;
                    fs::remove_all(root / fs::path(folder), error);

                    if (error)
                        LOG_ERROR("[Assets] Can't delete {}: {}", folder, error.message());
                });
                break;

            case Dialog::None:
                break;
        }
    }

    void AssetBrowserPanel::MoveAssetToFolder(AssetID asset, const std::string& folder)
    {
        m_context.Deferred.emplace_back([this, asset, folder]
        {
            const AssetInfo* info = m_context.Session.GetAssets().GetInfo(asset);
            if (!info || info->ReadOnly || ParentOf(info->Path) == folder)
                return;

            m_context.Session.GetAssets().Rename(asset, folder.empty() ? AssetFileName(info->Path) : folder + "/" + AssetFileName(info->Path));
        });
    }
}
