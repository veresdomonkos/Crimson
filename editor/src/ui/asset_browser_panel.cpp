#include "crimson_editor/ui/asset_browser_panel.hpp"

#include <algorithm>
#include <filesystem>
#include <set>
#include <system_error>

#include <crimson/core/log.hpp>
#include <crimson/project/project.hpp>

#include "crimson_editor/ui/asset_drag.hpp"

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

    static bool IsEngineFolder(const std::string& folder)
    {
        return folder == kEngineFolder || folder.starts_with(std::string(kEngineFolder) + "/");
    }

    static ImU32 GetTileColor(AssetType type)
    {
        switch (type)
        {
            case AssetType::Material: return IM_COL32(150, 60, 70, 255);
            case AssetType::Mesh:     return IM_COL32(60, 110, 150, 255);
            case AssetType::Shader:   return IM_COL32(95, 75, 150, 255);
            case AssetType::Texture:  return IM_COL32(60, 130, 90, 255);
            case AssetType::Scene:    return IM_COL32(170, 105, 50, 255);
            default:                  return IM_COL32(90, 90, 90, 255);
        }
    }

    // A name centered under its tile, and cut off if it would run into the next one
    static void DrawClippedText(ImDrawList* drawList, const ImVec2& position, float width, const char* text, ImU32 color)
    {
        const float textWidth = ImGui::CalcTextSize(text).x;
        const ImVec2 start(position.x + std::max((width - textWidth) * 0.5f, 0.0f), position.y);
        const ImVec4 clip(position.x, position.y, position.x + width, position.y + ImGui::GetTextLineHeight());

        drawList->AddText(nullptr, 0.0f, start, color, text, nullptr, 0.0f, &clip);
    }

    void AssetBrowserPanel::Rebuild()
    {
        m_builtOnce = true;
        m_builtVersion = m_context.AssetsVersion;

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
            const std::string folder = ParentOf(info->Path);
            m_items.push_back({ info->Id, info->Type, info->Path, NameOf(info->Path), folder, info->ReadOnly });
            addFolderAndParents(folder);
        }

        // Empty folders exist too, and only the disk knows about them
        std::error_code error;
        const fs::path root = m_context.Assets.GetRoot();

        for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, error), end; !error && it != end; it.increment(error))
        {
            if (it->is_directory(error))
                addFolderAndParents(fs::relative(it->path(), root, error).generic_string());
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

        if (!m_builtOnce || m_builtVersion != m_context.AssetsVersion)
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
                DrawFolderTree({});
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

        // The path to the current folder, each part leading back to it
        ImGui::SameLine();
        if (ImGui::SmallButton("Assets"))
            m_currentFolder.clear();

        size_t start = 0;
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
    }

    void AssetBrowserPanel::DrawFolderTree(const std::string& parent)
    {
        for (const std::string& folder : m_folders)
        {
            if (ParentOf(folder) != parent)
                continue;

            const bool hasChildren = std::ranges::any_of(m_folders, [&folder](const std::string& other) { return ParentOf(other) == folder; });

            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
            if (!hasChildren)
                flags |= ImGuiTreeNodeFlags_Leaf;
            if (folder == m_currentFolder)
                flags |= ImGuiTreeNodeFlags_Selected;

            const bool open = ImGui::TreeNodeEx(folder.c_str(), flags, "%s", NameOf(folder).c_str());

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
                if (ParentOf(folder) == m_currentFolder)
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

        const float lineHeight = ImGui::GetTextLineHeightWithSpacing();
        ImGui::Selectable("##Folder", false, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(tileSize, tileSize + lineHeight));

        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            m_currentFolder = folder;

        const ImVec2 min = ImGui::GetItemRectMin();
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        const float pad = tileSize * 0.15f;
        drawList->AddRectFilled(ImVec2(min.x + pad, min.y + pad * 1.6f), ImVec2(min.x + tileSize - pad, min.y + tileSize - pad), IM_COL32(170, 140, 70, 255), 3.0f);
        drawList->AddRectFilled(ImVec2(min.x + pad, min.y + pad), ImVec2(min.x + tileSize * 0.5f, min.y + pad * 1.8f), IM_COL32(150, 120, 55, 255), 3.0f);

        const std::string name = NameOf(folder);
        DrawClippedText(drawList, ImVec2(min.x + 2.0f, min.y + tileSize), tileSize - 4.0f, name.c_str(), ImGui::GetColorU32(ImGuiCol_Text));

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

        const float lineHeight = ImGui::GetTextLineHeightWithSpacing();
        const bool selected = m_context.SelectedAsset == item.Id;

        if (ImGui::Selectable("##Asset", selected, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(tileSize, tileSize + lineHeight)))
            m_context.SelectAsset(item.Id);

        if (item.Type == AssetType::Scene && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            m_context.OpenScene(item.Id);

        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", item.Path.c_str());

        if (const AssetInfo* info = m_context.Assets.GetInfo(item.Id))
            DragAssetFromLastItem(*info);

        const ImVec2 min = ImGui::GetItemRectMin();
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        const float pad = tileSize * 0.1f;
        const ImVec2 tileMin(min.x + pad, min.y + pad);
        const ImVec2 tileMax(min.x + tileSize - pad, min.y + tileSize - pad);

        // Only pictures that are on screen get loaded
        ImTextureID picture = 0;
        if (item.Type == AssetType::Texture && ImGui::IsItemVisible())
            picture = m_context.Thumbnails.Get(item.Id);

        if (picture)
        {
            drawList->AddImage(picture, tileMin, tileMax);
        }
        else
        {
            drawList->AddRectFilled(tileMin, tileMax, GetTileColor(item.Type), 4.0f);

            const std::string_view label = ToString(item.Type);
            const ImVec2 labelSize = ImGui::CalcTextSize(label.data(), label.data() + label.size());

            drawList->AddText(ImVec2((tileMin.x + tileMax.x - labelSize.x) * 0.5f, (tileMin.y + tileMax.y - labelSize.y) * 0.5f),
                IM_COL32(255, 255, 255, 220), label.data(), label.data() + label.size());
        }

        const std::string name = m_context.Edits.IsDirty(item.Id) ? "* " + item.Name : item.Name;
        DrawClippedText(drawList, ImVec2(min.x + 2.0f, min.y + tileSize), tileSize - 4.0f, name.c_str(), ImGui::GetColorU32(item.ReadOnly ? ImGuiCol_TextDisabled : ImGuiCol_Text));

        if (!item.ReadOnly && ImGui::BeginPopupContextItem("##AssetMenu"))
        {
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
                    if (const AssetID id = m_context.Assets.CreateMaterial(path))
                        m_context.SelectAsset(id);
                });
                break;

            case Dialog::NewScene:
                refreshAfter([this, path = JoinToCurrent(name + ".scene")]
                {
                    if (const AssetID id = m_context.Assets.CreateScene(path))
                        m_context.SelectAsset(id);
                });
                break;

            case Dialog::NewShader:
                refreshAfter([this, path = JoinToCurrent(name + ".hlsl")]
                {
                    if (const AssetID id = m_context.Assets.CreateShader(path))
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

            if (m_context.Assets.Rename(asset, folder + "/" + NameOf(info->Path)))
                ++m_context.AssetsVersion;
        });
    }
}
