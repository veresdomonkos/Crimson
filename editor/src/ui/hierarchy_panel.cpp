#include "crimson_editor/ui/hierarchy_panel.hpp"

#include <algorithm>
#include <cfloat>
#include <filesystem>
#include <string>

#include <imgui.h>

#include "crimson_editor/ui/component_fields.hpp"
#include "crimson_editor/ui/editor_theme.hpp"
#include "crimson_editor/ui/icons.hpp"

namespace crimson::editor::ui
{
    constexpr const char* kEntityPayload = "CRIMSON_ENTITY";

    static constexpr Shortcut kRename{ Key::F2 };
    static constexpr Shortcut kDelete{ Key::Delete };

    // Where the commands sit in the main menu: Edit after Undo and Redo, and Create after it
    constexpr int kRenamePriority = 120;
    constexpr int kDeletePriority = 121;
    constexpr int kCreateEmptyPriority = 200;
    constexpr int kCreatePresetPriority = 210;

    // What kind of entity it is, at a glance
    static const char* EntityIcon(Entity entity, const ComponentRegistry& components)
    {
        if (entity.HasComponent<SceneInstanceComponent>())
            return icons::kSceneInstance;

        // The first component with an icon, in the order they were registered, which puts a light before a mesh
        for (const ComponentType& type : components.Types())
        {
            if (type.InlineInFile)
                continue;

            if (const void* component = type.Get(entity))
            {
                if (const char* icon = ComponentIcon(type, component))
                    return icon;
            }
        }

        return icons::kEntity;
    }

    HierarchyPanel::HierarchyPanel(EditorContext& context)
        : Panel({ .Title = "Hierarchy", .Icon = icons::kEntity, .Slot = DockSlot::Left }),
          m_context(context)
    {
        AddCommand({ .MenuPath = "Edit/Rename", .Priority = kRenamePriority, .Shortcuts = { kRename },
                     .Enabled = [this] { return IsSelectionEditable(); },
                     .Run = [this] { BeginRename(m_context.Session.GetSelected()); } });

        AddCommand({ .MenuPath = "Edit/Delete", .Priority = kDeletePriority, .Shortcuts = { kDelete },
                     .Enabled = [this] { return IsSelectionEditable(); },
                     .Run = [this] { DeleteEntity(m_context.Session.GetSelected()); } });

        const std::vector<CreateOption> options = ListCreateOptions();

        for (size_t i = 0; i < options.size(); ++i)
        {
            const CreateOption option = options[i];
            const std::string path = option.Menu.empty() ? "Create/" + option.Name : "Create/Create " + option.Menu + "/" + option.Name;

            AddCommand({ .MenuPath = path, .Priority = i == 0 ? kCreateEmptyPriority : kCreatePresetPriority + static_cast<int>(i) - 1,
                         .Run = [this, option] { Create(option, {}); } });
        }
    }

    bool HierarchyPanel::IsSelectionEditable() const
    {
        const Entity selected = m_context.Session.GetSelected();
        return selected && !selected.HasComponent<InstancedComponent>();
    }

    void HierarchyPanel::Draw()
    {
        Scene& scene = m_context.Session.GetScene();

        if (const AssetInfo* info = m_context.Session.GetAssets().GetInfo(m_context.Session.GetSceneAsset()))
        {
            const std::string name = std::filesystem::path(info->Path).stem().string();
            ImGui::TextDisabled("%s%s", m_context.Session.IsSceneChanged() ? "* " : "", name.c_str());
            ImGui::SetItemTooltip("%s", info->Path.c_str());
        }
        else
        {
            ImGui::TextDisabled("No scene is open, this won't be saved");
        }

        DrawSearch();
        ImGui::Separator();

        const bool searching = m_filter.IsActive();
        m_shown.clear();

        if (searching)
        {
            for (const entt::entity root : scene.GetRoots())
                CollectMatches(root);
        }

        for (const entt::entity root : scene.GetRoots())
            DrawEntity(root);

        if (scene.GetRoots().empty())
            theme::EmptyHint("Right-click to create an entity, or drop a mesh into the Scene view");
        else if (searching && m_shown.empty())
            theme::EmptyHint("No entity has that in its name");

        // The rest of the panel: clicking clears the selection, dropping makes a root
        const ImVec2 available = ImGui::GetContentRegionAvail();
        ImGui::InvisibleButton("##HierarchyBackground", ImVec2(std::max(available.x, 1.0f), std::max(available.y, 1.0f)));

        if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
            m_context.Session.SelectEntity({});

        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kEntityPayload))
            {
                const entt::entity dropped = *static_cast<const entt::entity*>(payload->Data);
                m_deferred.emplace_back([this, dropped]
                {
                    const Entity moved = m_context.Session.GetScene().GetEntity(dropped);
                    m_context.Session.GetSceneEdits().Record(moved);
                    m_context.Session.GetScene().SetParent(moved, {});
                });
            }

            ImGui::EndDragDropTarget();
        }

        if (ImGui::BeginPopupContextItem("##HierarchyContext"))
        {
            DrawCreateMenu({});
            ImGui::EndPopup();
        }

        // The entity may have gone while its name was being edited
        if (m_renaming != entt::null && !scene.GetRegistry().valid(m_renaming))
            m_renaming = entt::null;

        // They are edits made at once, so they are steps of their own right away
        if (!m_deferred.empty())
            m_context.Session.MarkEdited();

        for (const auto& action : m_deferred)
            action();

        m_deferred.clear();
    }

    void HierarchyPanel::DeleteEntity(Entity entity)
    {
        m_deferred.emplace_back([this, entity]
        {
            m_context.Session.GetSceneEdits().RecordDestroy(entity);
            m_context.Session.GetScene().DestroyEntity(entity);
        });
    }

    void HierarchyPanel::DrawSearch()
    {
        ImGui::SetNextItemWidth(-FLT_MIN);

        const std::string hint = std::string(icons::kSearch) + "  Search";
        if (ImGui::InputTextWithHint("##Search", hint.c_str(), m_filter.InputBuf, IM_ARRAYSIZE(m_filter.InputBuf)))
            m_filter.Build();

        // Escape leaves the box, and clears it too, as it does in most search boxes
        if (ImGui::IsItemDeactivated() && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
            m_filter.Clear();
    }

    // Marks an entity to be shown when its name matches or something under it does, and says whether it is
    bool HierarchyPanel::CollectMatches(entt::entity handle)
    {
        const Entity entity = m_context.Session.GetScene().GetEntity(handle);

        bool shown = m_filter.PassFilter(entity.GetComponent<NameComponent>().Name.c_str());

        // Every child, not only up to the first match, as each one can be shown on its own
        for (const entt::entity child : entity.GetComponent<RelationshipComponent>().Children)
            shown = CollectMatches(child) || shown;

        if (shown)
            m_shown.insert(handle);

        return shown;
    }

    void HierarchyPanel::DrawEntity(entt::entity handle)
    {
        // A search shows only what matches and what leads to it, opened up to it
        const bool searching = m_filter.IsActive();
        if (searching && !m_shown.contains(handle))
            return;

        Scene& scene = m_context.Session.GetScene();
        const Entity entity = scene.GetEntity(handle);

        const auto& relationship = entity.GetComponent<RelationshipComponent>();
        const auto& name = entity.GetComponent<NameComponent>();

        if (searching)
            ImGui::SetNextItemOpen(true);

        // What comes from an instance's scene can only be looked at
        const bool readOnly = entity.HasComponent<InstancedComponent>();
        const bool isInstance = entity.HasComponent<SceneInstanceComponent>();

        // Double-clicking renames, so only the arrow opens and closes
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;

        if (relationship.Children.empty())
            flags |= ImGuiTreeNodeFlags_Leaf;

        if (m_context.Session.GetSelected() == entity)
            flags |= ImGuiTreeNodeFlags_Selected;

        const bool dimmed = readOnly || !entity.GetComponent<TransformComponent>().ActiveInHierarchy;
        if (dimmed)
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));

        ImGui::PushID(static_cast<int>(entt::to_integral(handle)));
        const bool renaming = handle == m_renaming;
        const std::string label = renaming ? std::string() : isInstance ? name.Name + " [scene]" : name.Name;
        const bool open = theme::IconTreeNode("##Entity", flags, EntityIcon(entity, m_context.Session.GetAssets().Components()), theme::TextMuted, label.c_str());

        if (dimmed)
            ImGui::PopStyleColor();

        if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen())
            m_context.Session.SelectEntity(entity);

        if (!readOnly && !renaming && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen())
            BeginRename(entity);

        if (!readOnly && ImGui::BeginDragDropSource())
        {
            ImGui::SetDragDropPayload(kEntityPayload, &handle, sizeof(handle));
            ImGui::TextUnformatted(name.Name.c_str());
            ImGui::EndDragDropSource();
        }

        if (!readOnly && !isInstance && ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kEntityPayload))
            {
                const entt::entity dropped = *static_cast<const entt::entity*>(payload->Data);
                m_deferred.emplace_back([this, dropped, entity]
                {
                    const Entity moved = m_context.Session.GetScene().GetEntity(dropped);
                    m_context.Session.GetSceneEdits().Record(moved);
                    m_context.Session.GetScene().SetParent(moved, entity);
                });
            }

            ImGui::EndDragDropTarget();
        }

        if (!readOnly && ImGui::BeginPopupContextItem("##EntityContext"))
        {
            if (!isInstance)
            {
                DrawCreateMenu(entity);
                ImGui::Separator();
            }

            if (ImGui::MenuItem("Rename", ToText(kRename).c_str()))
                BeginRename(entity);

            if (ImGui::MenuItem("Delete", ToText(kDelete).c_str()))
                DeleteEntity(entity);

            ImGui::EndPopup();
        }

        if (renaming)
            DrawRenameField(entity);

        if (open)
        {
            for (const entt::entity child : relationship.Children)
                DrawEntity(child);

            ImGui::TreePop();
        }

        ImGui::PopID();
    }

    void HierarchyPanel::BeginRename(Entity entity)
    {
        const std::string& name = entity.GetComponent<NameComponent>().Name;

        m_renameBuffer.fill('\0');
        std::copy_n(name.begin(), std::min(name.size(), m_renameBuffer.size() - 1), m_renameBuffer.begin());

        m_renaming = entity.GetHandle();
        m_focusRename = true;
    }

    // Enter or clicking away keeps the new name, Escape keeps the old one
    void HierarchyPanel::DrawRenameField(Entity entity)
    {
        // The icon is drawn over the node and takes no room, so the field starts after it
        const char* icon = EntityIcon(entity, m_context.Session.GetAssets().Components());
        ImGui::SameLine(0.0f, ImGui::CalcTextSize(icon).x + ImGui::GetStyle().ItemInnerSpacing.x * 2.0f);
        ImGui::SetNextItemWidth(-FLT_MIN);

        if (m_focusRename)
        {
            ImGui::SetKeyboardFocusHere();
            m_focusRename = false;
        }

        const bool entered = ImGui::InputText("##Rename", m_renameBuffer.data(), m_renameBuffer.size(),
            ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_EnterReturnsTrue);

        if (!entered && !ImGui::IsItemDeactivated())
            return;

        m_renaming = entt::null;

        if (!entered && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
            return;

        // A name of only spaces would leave nothing to click in the tree
        std::string name(m_renameBuffer.data());
        name.erase(0, name.find_first_not_of(" \t"));
        name.erase(name.find_last_not_of(" \t") + 1);

        if (!name.empty())
            m_deferred.emplace_back([this, entity, name = std::move(name)]
            {
                m_context.Session.GetSceneEdits().Record(entity);
                entity.GetComponent<NameComponent>().Name = name;
            });
    }

    std::vector<HierarchyPanel::CreateOption> HierarchyPanel::ListCreateOptions() const
    {
        std::vector<CreateOption> options{ CreateOption{ .Name = "Create Empty" } };

        // A submenu for each menu the components' presets name, in the order the components were registered
        const ComponentRegistry& components = m_context.Session.GetAssets().Components();

        std::vector<std::string_view> menus;
        for (const ComponentType& type : components.Types())
        {
            for (const ComponentPreset& preset : type.Presets)
            {
                if (std::ranges::find(menus, preset.Menu) == menus.end())
                    menus.push_back(preset.Menu);
            }
        }

        for (const std::string_view menu : menus)
        {
            for (const ComponentType& type : components.Types())
            {
                for (const ComponentPreset& preset : type.Presets)
                {
                    if (preset.Menu == menu)
                        options.push_back(CreateOption{ .Menu = std::string(menu), .Name = preset.Name, .Type = &type, .Preset = &preset });
                }
            }
        }

        return options;
    }

    void HierarchyPanel::Create(const CreateOption& option, Entity parent)
    {
        m_deferred.emplace_back([this, option, parent]
        {
            const Entity created = m_context.Session.GetScene().CreateEntity(option.Preset ? option.Preset->Name : "Entity", parent);
            if (option.Type)
                ApplyPreset(*option.Type, *option.Preset, created, m_context.Session.GetAssets());

            m_context.Session.GetSceneEdits().RecordCreated(created);
            m_context.Session.SelectEntity(created);
        });
    }

    void HierarchyPanel::DrawCreateMenu(Entity parent)
    {
        const std::vector<CreateOption> options = ListCreateOptions();

        for (size_t i = 0; i < options.size();)
        {
            if (options[i].Menu.empty())
            {
                if (ImGui::MenuItem(options[i].Name.c_str()))
                    Create(options[i], parent);

                ++i;
                continue;
            }

            // The options of a menu are next to each other
            size_t end = i;
            while (end < options.size() && options[end].Menu == options[i].Menu)
                ++end;

            if (ImGui::BeginMenu(("Create " + options[i].Menu).c_str()))
            {
                for (size_t j = i; j < end; ++j)
                {
                    if (ImGui::MenuItem(options[j].Name.c_str()))
                        Create(options[j], parent);
                }

                ImGui::EndMenu();
            }

            i = end;
        }
    }
}
