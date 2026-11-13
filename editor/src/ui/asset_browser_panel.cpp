#include "crimson_editor/ui/asset_browser_panel.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <filesystem>
#include <set>
#include <system_error>

#include <crimson/asset_manager/slot_materials.hpp>
#include <crimson/core/log.hpp>
#include <crimson/project/project.hpp>

#include "crimson_editor/system_shell.hpp"
#include "crimson_editor/ui/asset_drag.hpp"
#include "crimson_editor/ui/editor_theme.hpp"
#include "crimson_editor/ui/icons.hpp"

namespace fs = std::filesystem;

namespace crimson::editor::ui
{
    static constexpr std::string_view kEngineFolder = "engine";

    static std::string ParentOf(const std::string& path)
    {
        const size_t slash = path.find_last_of('/');
        return slash == std::string::npos ? std::string{} : path.substr(0, slash);
    }

    static std::string NameOf(const std::string& path)
    {
        const size_t slash = path.find_last_of('/');
        return slash == std::string::npos ? path : path.substr(slash + 1);
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
        return type == AssetType::Shader || type == AssetType::Texture || type == AssetType::Mesh;
    }

    // The first character of a UTF-8 string, such as an icon
    static unsigned int FirstCodepoint(const char* text)
    {
        const auto* bytes = reinterpret_cast<const unsigned char*>(text);

        if (bytes[0] < 0x80) return bytes[0];
        if ((bytes[0] & 0xE0) == 0xC0) return ((bytes[0] & 0x1Fu) << 6) | (bytes[1] & 0x3Fu);
        if ((bytes[0] & 0xF0) == 0xE0) return ((bytes[0] & 0x0Fu) << 12) | ((bytes[1] & 0x3Fu) << 6) | (bytes[2] & 0x3Fu);
        return ((bytes[0] & 0x07u) << 18) | ((bytes[1] & 0x3Fu) << 12) | ((bytes[2] & 0x3Fu) << 6) | (bytes[3] & 0x3Fu);
    }

    // An icon as big as fits in a box, centered by what it draws rather than by its place in the font, which leaves
    // room around icons that isn't the same on every side
    static void DrawTileIcon(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, const char* icon, const ImVec4& color)
    {
        const float size = std::min(max.x - min.x, max.y - min.y);
        ImFont* font = ImGui::GetFont();
        const ImVec2 center((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);

        ImVec2 position;
        if (const ImFontGlyph* glyph = font->GetFontBaked(size)->FindGlyphNoFallback(static_cast<ImWchar>(FirstCodepoint(icon))))
        {
            position = ImVec2(center.x - (glyph->X0 + glyph->X1) * 0.5f, center.y - (glyph->Y0 + glyph->Y1) * 0.5f);
        }
        else
        {
            const ImVec2 textSize = font->CalcTextSizeA(size, FLT_MAX, 0.0f, icon);
            position = ImVec2(center.x - textSize.x * 0.5f, center.y - textSize.y * 0.5f);
        }

        drawList->AddText(font, size, position, ImGui::GetColorU32(color), icon);
    }

    // Where a tile's parts go. Its frame is drawn here rather than by a Selectable, whose highlight reaches past the
    // tile into the spacing around it, so the icon and the name are centered in exactly what shows as the tile.
    struct TileLayout
    {
        ImVec2 IconMin;
        ImVec2 IconMax;
        ImVec2 LabelPosition;
        float LabelWidth = 0.0f;
        bool Clicked = false;
    };

    static TileLayout DrawTileFrame(const char* id, float width, bool selected)
    {
        const ImGuiStyle& style = ImGui::GetStyle();
        const float pad = std::round(width * 0.08f);
        const float iconHeight = std::round(width * 0.62f);
        const float height = pad + iconHeight + style.ItemInnerSpacing.y + ImGui::GetTextLineHeight() + pad;

        TileLayout layout;
        layout.Clicked = ImGui::InvisibleButton(id, ImVec2(width, height));

        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 max = ImGui::GetItemRectMax();

        const ImGuiCol background = selected ? ImGuiCol_Header : ImGui::IsItemHovered() ? ImGuiCol_HeaderHovered : ImGuiCol_COUNT;
        if (background != ImGuiCol_COUNT)
            ImGui::GetWindowDrawList()->AddRectFilled(min, max, ImGui::GetColorU32(background), style.FrameRounding * 2.0f);

        layout.IconMin = ImVec2(min.x + pad, min.y + pad);
        layout.IconMax = ImVec2(max.x - pad, min.y + pad + iconHeight);
        layout.LabelPosition = ImVec2(min.x + pad * 0.5f, layout.IconMax.y + style.ItemInnerSpacing.y);
        layout.LabelWidth = width - pad;
        return layout;
    }

    // A name centered under its tile, and cut off if it would run into the next one
    // A name centered under its tile, shortened with "..." if it is wider than the tile. The tooltip has the whole name.
    static void DrawClippedText(ImDrawList* drawList, const ImVec2& position, float width, const char* text, ImU32 color)
    {
        static constexpr const char* kEllipsis = "...";

        std::string shown = text;
        if (ImGui::CalcTextSize(shown.c_str()).x > width)
        {
            const float ellipsisWidth = ImGui::CalcTextSize(kEllipsis).x;

            // Whole UTF-8 characters come off the end, never half of one: continuation bytes, then the byte they follow
            while (!shown.empty() && ImGui::CalcTextSize(shown.c_str()).x + ellipsisWidth > width)
            {
                while (!shown.empty())
                {
                    const auto byte = static_cast<unsigned char>(shown.back());
                    shown.pop_back();

                    if ((byte & 0xC0) != 0x80)
                        break;
                }
            }

            shown += kEllipsis;
        }

        const float textWidth = ImGui::CalcTextSize(shown.c_str()).x;
        const ImVec2 start(position.x + std::max((width - textWidth) * 0.5f, 0.0f), position.y);
        const ImVec4 clip(position.x, position.y, position.x + width, position.y + ImGui::GetTextLineHeight());

        drawList->AddText(nullptr, 0.0f, start, color, shown.c_str(), nullptr, 0.0f, &clip);
    }

    void AssetBrowserPanel::Rebuild()
    {
        m_builtOnce = true;
        m_builtVersion = m_context.AssetsVersion;
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

        for (const AssetInfo* info : m_context.Assets.ListAll())
        {
            if (!ListsAsset(*info, m_builtShowingEngine))
                continue;

            const std::string folder = ParentOf(info->Path);
            m_items.push_back({ info->Id, info->Type, info->Path, NameOf(info->Path), folder, info->ReadOnly });
            addFolderAndParents(folder);
        }

        // Empty folders exist too, and only the disk knows about them
        std::error_code error;
        const fs::path root = m_context.Assets.GetRoot();

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
            const std::vector<AssetID> removed = m_context.Assets.Refresh();
            m_context.OnAssetsRemoved(removed);
            ++m_context.AssetsVersion;
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

    void AssetBrowserPanel::OnImGui()
    {
        ImGui::Begin("Assets");

        RefreshWhenRefocused();

        if (!m_builtOnce || m_builtVersion != m_context.AssetsVersion || m_builtShowingEngine != m_settings.GetShowEngineContent())
            Rebuild();

        DrawToolbar();

        constexpr ImGuiTableFlags kTableFlags = ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_NoSavedSettings;

        if (ImGui::BeginTable("##AssetLayout", 2, kTableFlags))
        {
            ImGui::TableSetupColumn("Folders", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 11.0f);
            ImGui::TableSetupColumn("Assets", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            if (ImGui::BeginChild("##Folders"))
                DrawFolderRoots();
            ImGui::EndChild();

            ImGui::TableNextColumn();
            if (ImGui::BeginChild("##Grid"))
                DrawGrid();
            ImGui::EndChild();

            ImGui::EndTable();
        }

        DrawDialogs();

        ImGui::End();
    }

    void AssetBrowserPanel::DrawToolbar()
    {
        if (ImGui::Button("Refresh"))
            RefreshFromDisk();

        ImGui::SameLine();
        m_filter.Draw("##Search", ImGui::GetFontSize() * 12.0f);

        if (!m_filter.IsActive() && !ImGui::IsItemActive())
        {
            const ImGuiStyle& style = ImGui::GetStyle();
            const ImVec2 min = ImGui::GetItemRectMin();
            ImGui::GetWindowDrawList()->AddText(ImVec2(min.x + style.FramePadding.x, min.y + style.FramePadding.y), ImGui::GetColorU32(ImGuiCol_TextDisabled), "Search");
        }

        ImGui::SameLine();
        if (bool show = m_settings.GetShowEngineContent(); ImGui::Checkbox("Show Engine Content", &show))
            m_settings.SetShowEngineContent(show);

        // The path to the current folder, each part leading back to it. The engine's start at a root of their own.
        const bool inEngine = IsEngineFolder(m_currentFolder);

        ImGui::SameLine();
        if (ImGui::SmallButton(inEngine ? "Engine" : "Assets"))
            m_currentFolder = inEngine ? std::string(kEngineFolder) : std::string();

        size_t start = inEngine ? std::min(kEngineFolder.size() + 1, m_currentFolder.size()) : 0;
        while (start < m_currentFolder.size())
        {
            const size_t slash = m_currentFolder.find('/', start);
            const size_t end = slash == std::string::npos ? m_currentFolder.size() : slash;

            ImGui::SameLine();
            ImGui::TextDisabled("/");
            ImGui::SameLine();

            const std::string part = m_currentFolder.substr(start, end - start);

            ImGui::PushID(static_cast<int>(start));
            if (ImGui::SmallButton(part.c_str()))
                m_currentFolder.resize(end);
            ImGui::PopID();

            if (m_currentFolder.size() <= end)
                break;

            start = end + 1;
        }

        if (m_reservedFolderFound)
            ImGui::TextColored(theme::Warning, "The project's engine folder is reserved for the engine's assets, so what's in it doesn't show. Rename it.");
    }

    void AssetBrowserPanel::DrawFolderRoots()
    {
        constexpr ImGuiTreeNodeFlags kRootFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;

        // The project's folders, without the engine's
        {
            const bool selected = m_currentFolder.empty();
            const bool open = ImGui::TreeNodeEx("##AssetsRoot", kRootFlags | (selected ? ImGuiTreeNodeFlags_Selected : 0), "%s  Assets", selected ? kIconFolderOpen : kIconFolder);

            if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen())
                m_currentFolder.clear();

            for (const AssetType type : { AssetType::Material, AssetType::Mesh, AssetType::Shader, AssetType::Texture, AssetType::Scene })
            {
                if (const AssetID dropped = AcceptAssetOnLastItem(m_context.Assets, type))
                    MoveAssetToFolder(dropped, {});
            }

            if (open)
            {
                DrawFolderTree({});
                ImGui::TreePop();
            }
        }

        if (!m_settings.GetShowEngineContent())
            return;

        // The engine's, which can be looked at and used but not changed
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        const std::string engine(kEngineFolder);
        const bool selected = m_currentFolder == engine;

        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        const bool open = ImGui::TreeNodeEx("##EngineRoot", kRootFlags | (selected ? ImGuiTreeNodeFlags_Selected : 0), "%s  Engine", selected ? kIconFolderOpen : kIconFolder);
        ImGui::PopStyleColor();

        ImGui::SetItemTooltip("The engine's built-in assets, which can be used but not changed");

        if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen())
            m_currentFolder = engine;

        if (open)
        {
            DrawFolderTree(engine);
            ImGui::TreePop();
        }
    }

    void AssetBrowserPanel::DrawFolderTree(const std::string& parent)
    {
        for (const std::string& folder : m_folders)
        {
            if (ParentOf(folder) != parent || (parent.empty() && IsEngineFolder(folder)))
                continue;

            const bool hasChildren = std::ranges::any_of(m_folders, [&folder](const std::string& other) { return ParentOf(other) == folder; });

            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
            if (!hasChildren)
                flags |= ImGuiTreeNodeFlags_Leaf;
            if (folder == m_currentFolder)
                flags |= ImGuiTreeNodeFlags_Selected;

            const bool open = ImGui::TreeNodeEx(folder.c_str(), flags, "%s  %s", folder == m_currentFolder ? kIconFolderOpen : kIconFolder, NameOf(folder).c_str());

            if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen())
                m_currentFolder = folder;

            if (!IsEngineFolder(folder))
            {
                for (const AssetType type : { AssetType::Material, AssetType::Mesh, AssetType::Shader, AssetType::Texture, AssetType::Scene })
                {
                    if (const AssetID dropped = AcceptAssetOnLastItem(m_context.Assets, type))
                        MoveAssetToFolder(dropped, folder);
                }

                DrawFolderMenu(folder);
            }

            if (open)
            {
                DrawFolderTree(folder);
                ImGui::TreePop();
            }
        }
    }

    void AssetBrowserPanel::DrawGrid()
    {
        const float tileSize = ImGui::GetFontSize() * 5.5f;
        const float right = ImGui::GetWindowPos().x + ImGui::GetContentRegionMax().x;
        const float spacing = ImGui::GetStyle().ItemSpacing.x;

        const auto continueRow = [&]
        {
            if (ImGui::GetItemRectMax().x + spacing + tileSize < right)
                ImGui::SameLine();
        };

        // Searching looks through every folder
        const bool searching = m_filter.IsActive();

        if (!searching)
        {
            for (const std::string& folder : m_folders)
            {
                if (ParentOf(folder) == m_currentFolder && !(m_currentFolder.empty() && IsEngineFolder(folder)))
                {
                    DrawFolderTile(folder, tileSize);
                    continueRow();
                }
            }
        }

        for (const Item& item : m_items)
        {
            if (searching ? !m_filter.PassFilter(item.Path.c_str()) : item.Folder != m_currentFolder)
                continue;

            DrawAssetTile(item, tileSize);
            continueRow();
        }

        ImGui::NewLine();

        // Anywhere in the grid that isn't a tile: clicking clears the selection
        if (ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            m_context.SelectAsset({});

        DrawBackgroundMenu();
    }

    void AssetBrowserPanel::DrawFolderTile(const std::string& folder, float tileSize)
    {
        ImGui::PushID(folder.c_str());

        const TileLayout tile = DrawTileFrame("##Folder", tileSize, false);

        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            m_currentFolder = folder;

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        DrawTileIcon(drawList, tile.IconMin, tile.IconMax, kIconFolder, theme::Folder);

        const std::string name = NameOf(folder);
        DrawClippedText(drawList, tile.LabelPosition, tile.LabelWidth, name.c_str(), ImGui::GetColorU32(ImGuiCol_Text));

        if (!IsEngineFolder(folder))
        {
            for (const AssetType type : { AssetType::Material, AssetType::Mesh, AssetType::Shader, AssetType::Texture, AssetType::Scene })
            {
                if (const AssetID dropped = AcceptAssetOnLastItem(m_context.Assets, type))
                    MoveAssetToFolder(dropped, folder);
            }

            DrawFolderMenu(folder);
        }

        ImGui::PopID();
    }

    void AssetBrowserPanel::DrawAssetTile(const Item& item, float tileSize)
    {
        ImGui::PushID(static_cast<int>(std::hash<AssetID>{}(item.Id)));

        const bool selected = m_context.SelectedAsset == item.Id;

        const TileLayout tile = DrawTileFrame("##Asset", tileSize, selected);
        if (tile.Clicked)
            m_context.SelectAsset(item.Id);

        // A scene opens in the editor. Shaders open in the script editor from the preferences, and textures and
        // meshes in the program the system has for them. A material's editor is the Inspector, so it only gets selected.
        const fs::path file = m_context.Assets.GetFilePath(item.Id);
        const bool editedOutside = OpensOutside(item.Type);

        const auto openOutside = [&]
        {
            if (item.Type == AssetType::Shader && m_context.OpenCodeFile)
                m_context.OpenCodeFile(file);
            else
                OpenInDefaultApp(file);
        };

        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            if (const SceneID scene = m_context.Assets.As<AssetType::Scene>(item.Id))
                m_context.OpenScene(scene);
            else if (editedOutside && !file.empty())
                openOutside();
        }

        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", item.Path.c_str());

        if (const AssetInfo* info = m_context.Assets.GetInfo(item.Id))
            DragAssetFromLastItem(*info);

        ImDrawList* drawList = ImGui::GetWindowDrawList();

        // Only pictures that are on screen get loaded
        ImTextureID picture = 0;
        if (item.Type == AssetType::Texture && ImGui::IsItemVisible())
            picture = m_context.Thumbnails.Get(m_context.Assets.As<AssetType::Texture>(item.Id));

        if (picture)
        {
            // A square picture, centered in the icon's box
            const float side = std::min(tile.IconMax.x - tile.IconMin.x, tile.IconMax.y - tile.IconMin.y);
            const ImVec2 center((tile.IconMin.x + tile.IconMax.x) * 0.5f, (tile.IconMin.y + tile.IconMax.y) * 0.5f);
            drawList->AddImage(picture, ImVec2(center.x - side * 0.5f, center.y - side * 0.5f), ImVec2(center.x + side * 0.5f, center.y + side * 0.5f));
        }
        else
        {
            DrawTileIcon(drawList, tile.IconMin, tile.IconMax, AssetIcon(item.Type), theme::AssetColor(item.Type));
        }

        const bool dirty = item.Type == AssetType::Material && m_context.Edits.IsDirty(m_context.Assets.As<AssetType::Material>(item.Id));
        const std::string name = dirty ? "* " + item.Name : item.Name;
        DrawClippedText(drawList, tile.LabelPosition, tile.LabelWidth, name.c_str(), ImGui::GetColorU32(item.ReadOnly ? ImGuiCol_TextDisabled : ImGuiCol_Text));

        if (ImGui::BeginPopupContextItem("##AssetMenu"))
        {
            // An engine asset is built into the editor, so there is no file to open or show
            const bool onDisk = !file.empty();
            const auto explainIfBuiltIn = [onDisk]
            {
                if (!onDisk && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    ImGui::SetTooltip("Built into the editor, so there is no file");
            };

            if (editedOutside)
            {
                if (ImGui::MenuItem("Open", nullptr, false, onDisk))
                    openOutside();

                explainIfBuiltIn();
            }

            if (ImGui::MenuItem(RevealInFileManagerLabel(), nullptr, false, onDisk))
                RevealInFileManager(file);

            explainIfBuiltIn();

            if (item.ReadOnly)
            {
                ImGui::EndPopup();
                ImGui::PopID();
                return;
            }

            ImGui::Separator();

            // The materials its .mtl describes, one for each slot that has none
            if (item.Type == AssetType::Mesh && file.extension() == ".obj")
            {
                if (ImGui::MenuItem("Create Materials from .mtl"))
                {
                    m_context.Deferred.emplace_back([this, id = item.Id]
                    {
                        if (CreateSlotMaterials(m_context.Assets, m_context.Assets.As<AssetType::Mesh>(id)).Created.empty())
                            return;

                        ++m_context.AssetsVersion;
                        ResolveSceneAssets(m_context.ActiveScene, m_context.Assets);
                    });
                }

                ImGui::SetItemTooltip("Makes materials/<slot>.mat next to it for each slot, and leaves the ones that are there");
                ImGui::Separator();
            }

            if (ImGui::MenuItem("Rename"))
            {
                m_dialogAsset = item.Id;
                OpenDialog(Dialog::RenameAsset, fs::path(item.Name).stem().string());
            }

            if (ImGui::MenuItem("Delete"))
            {
                m_dialogAsset = item.Id;
                OpenDialog(Dialog::DeleteAsset, item.Name);
            }

            ImGui::EndPopup();
        }

        ImGui::PopID();
    }

    void AssetBrowserPanel::DrawFolderMenu(const std::string& folder)
    {
        if (!ImGui::BeginPopupContextItem("##FolderMenu"))
            return;

        if (ImGui::MenuItem(RevealInFileManagerLabel()))
            RevealInFileManager(m_context.Assets.GetRoot() / fs::path(folder));

        ImGui::Separator();

        // New things go where the folder is, so that is where the grid shows them
        if (ImGui::MenuItem("New Material"))
        {
            m_currentFolder = folder;
            OpenDialog(Dialog::NewMaterial, "New Material");
        }

        if (ImGui::MenuItem("New Scene"))
        {
            m_currentFolder = folder;
            OpenDialog(Dialog::NewScene, "New Scene");
        }

        if (ImGui::MenuItem("New Shader"))
        {
            m_currentFolder = folder;
            OpenDialog(Dialog::NewShader, "New Shader");
        }

        if (ImGui::MenuItem("New Folder"))
        {
            m_currentFolder = folder;
            OpenDialog(Dialog::NewFolder, "New Folder");
        }

        ImGui::Separator();

        if (ImGui::MenuItem("Rename"))
        {
            m_dialogFolder = folder;
            OpenDialog(Dialog::RenameFolder, NameOf(folder));
        }

        if (ImGui::MenuItem("Delete"))
        {
            m_dialogFolder = folder;
            OpenDialog(Dialog::DeleteFolder, NameOf(folder));
        }

        ImGui::EndPopup();
    }

    void AssetBrowserPanel::DrawBackgroundMenu()
    {
        constexpr ImGuiPopupFlags kFlags = ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems;

        if (IsEngineFolder(m_currentFolder) || !ImGui::BeginPopupContextWindow("##BackgroundMenu", kFlags))
            return;

        // The folder being shown, opened in the file manager
        if (ImGui::MenuItem(RevealInFileManagerLabel()))
            OpenInDefaultApp(m_context.Assets.GetRoot() / fs::path(m_currentFolder));

        ImGui::Separator();

        if (ImGui::MenuItem("New Material"))
            OpenDialog(Dialog::NewMaterial, "New Material");

        if (ImGui::MenuItem("New Scene"))
            OpenDialog(Dialog::NewScene, "New Scene");

        if (ImGui::MenuItem("New Shader"))
            OpenDialog(Dialog::NewShader, "New Shader");

        if (ImGui::MenuItem("New Folder"))
            OpenDialog(Dialog::NewFolder, "New Folder");

        ImGui::EndPopup();
    }

    void AssetBrowserPanel::OpenDialog(Dialog dialog, std::string name)
    {
        m_dialog = dialog;
        m_openDialog = true;
        m_dialogName = {};
        name.copy(m_dialogName.data(), m_dialogName.size() - 1);
    }

    void AssetBrowserPanel::DrawDialogs()
    {
        // The part after ### is the popup's id, so the title can say what it is doing
        static constexpr const char* kPopupId = "###AssetDialog";

        if (m_openDialog)
        {
            ImGui::OpenPopup(kPopupId);
            m_openDialog = false;
        }

        const auto getTitle = [this]
        {
            switch (m_dialog)
            {
                case Dialog::NewFolder:    return "New Folder###AssetDialog";
                case Dialog::NewMaterial:  return "New Material###AssetDialog";
                case Dialog::NewScene:     return "New Scene###AssetDialog";
                case Dialog::NewShader:    return "New Shader###AssetDialog";
                case Dialog::RenameAsset:
                case Dialog::RenameFolder: return "Rename###AssetDialog";
                case Dialog::DeleteAsset:
                case Dialog::DeleteFolder: return "Delete###AssetDialog";
                case Dialog::None:         break;
            }

            return kPopupId;
        };

        if (!ImGui::BeginPopupModal(getTitle(), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            return;

        const bool deleting = m_dialog == Dialog::DeleteAsset || m_dialog == Dialog::DeleteFolder;
        std::string problem;

        if (deleting)
        {
            ImGui::Text("Delete %s?", m_dialogName.data());
            ImGui::TextDisabled(m_dialog == Dialog::DeleteFolder ? "Everything in it goes too. This can't be undone." : "This can't be undone.");
        }
        else
        {
            if (ImGui::IsWindowAppearing())
                ImGui::SetKeyboardFocusHere();

            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 18.0f);
            ImGui::InputText("##Name", m_dialogName.data(), m_dialogName.size());

            problem = Project::ValidateName(m_dialogName.data());
            if (!problem.empty())
                ImGui::TextDisabled("%s", problem.c_str());
        }

        ImGui::BeginDisabled(!problem.empty());
        const bool accepted = ImGui::Button(deleting ? "Delete" : "OK") || (!deleting && problem.empty() && ImGui::IsKeyPressed(ImGuiKey_Enter, false));
        ImGui::EndDisabled();

        ImGui::SameLine();
        const bool cancelled = ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape, false);

        if (accepted)
            Confirm();

        if (accepted || cancelled)
        {
            m_dialog = Dialog::None;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    void AssetBrowserPanel::Confirm()
    {
        const std::string name = m_dialogName.data();
        const fs::path root = m_context.Assets.GetRoot();

        const auto refreshAfter = [this](std::function<void()> action)
        {
            m_context.Deferred.emplace_back([this, action = std::move(action)]
            {
                action();

                const std::vector<AssetID> removed = m_context.Assets.Refresh();
                m_context.OnAssetsRemoved(removed);
                ++m_context.AssetsVersion;
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
                    if (const AssetID id = m_context.Assets.Create<AssetType::Material>(path))
                        m_context.SelectAsset(id);
                });
                break;

            case Dialog::NewScene:
                refreshAfter([this, path = JoinToCurrent(name + ".scene")]
                {
                    if (const AssetID id = m_context.Assets.Create<AssetType::Scene>(path))
                        m_context.SelectAsset(id);
                });
                break;

            case Dialog::NewShader:
                refreshAfter([this, path = JoinToCurrent(name + ".hlsl")]
                {
                    if (const AssetID id = m_context.Assets.Create<AssetType::Shader>(path))
                        m_context.SelectAsset(id);
                });
                break;

            case Dialog::RenameAsset:
                refreshAfter([this, id = m_dialogAsset, name]
                {
                    if (const AssetInfo* info = m_context.Assets.GetInfo(id))
                    {
                        const fs::path current(info->Path);
                        const std::string folder = ParentOf(info->Path);
                        const std::string file = name + current.extension().string();

                        m_context.Assets.Rename(id, folder.empty() ? file : folder + "/" + file);
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
                    if (m_context.Assets.Delete(id))
                    {
                        const std::array removed = { id };
                        m_context.OnAssetsRemoved(removed);
                        ++m_context.AssetsVersion;
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
            const AssetInfo* info = m_context.Assets.GetInfo(asset);
            if (!info || info->ReadOnly || ParentOf(info->Path) == folder)
                return;

            if (m_context.Assets.Rename(asset, folder.empty() ? NameOf(info->Path) : folder + "/" + NameOf(info->Path)))
                ++m_context.AssetsVersion;
        });
    }
}
