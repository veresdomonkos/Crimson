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
    static constexpr const char* kEntityPayload = "CRIMSON_ENTITY";

    // What kind of entity it is, at a glance
    static const char* EntityIcon(Entity entity, const ComponentRegistry& components)
    {
        if (entity.HasComponent<SceneInstanceComponent>())
            return kIconSceneInstance;

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

        return kIconEntity;
    }

    void HierarchyPanel::OnImGui()
    {
        ImGui::Begin("Hierarchy");

        Scene& scene = m_context.ActiveScene;

        if (const AssetInfo* info = m_context.Assets.GetInfo(m_context.ActiveSceneAsset))
        {
            const std::string name = std::filesystem::path(info->Path).stem().string();
            ImGui::TextDisabled("%s%s", m_context.SceneChanged ? "* " : "", name.c_str());
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
            m_context.SelectEntity({});

        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kEntityPayload))
            {
                const entt::entity dropped = *static_cast<const entt::entity*>(payload->Data);
                m_deferred.emplace_back([this, dropped] { m_context.ActiveScene.SetParent(m_context.ActiveScene.GetEntity(dropped), {}); });
            }

            ImGui::EndDragDropTarget();
        }

        if (ImGui::BeginPopupContextItem("##HierarchyContext"))
        {
            DrawCreateMenu({});
            ImGui::EndPopup();
        }

        if (m_context.Selected && !m_context.Selected.HasComponent<InstancedComponent>() && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)
            && !ImGui::GetIO().WantTextInput)
        {
            const Entity selected = m_context.Selected;

            if (ImGui::IsKeyPressed(ImGuiKey_Delete, false))
                DeleteEntity(selected);
            else if (ImGui::IsKeyPressed(ImGuiKey_F2, false))
                BeginRename(selected);
        }

        // The entity may have gone while its name was being edited
        if (m_renaming != entt::null && !scene.GetRegistry().valid(m_renaming))
            m_renaming = entt::null;

        ImGui::End();

        for (const auto& action : m_deferred)
            action();

        m_deferred.clear();
    }

    void HierarchyPanel::OnMainMenuBar()
    {
        // What comes from an instance's scene can only be looked at
        const Entity selected = m_context.Selected;
        const bool editable = selected && !selected.HasComponent<InstancedComponent>();

        if (ImGui::BeginMenu("Edit"))
        {
            if (ImGui::MenuItem("Rename", "F2", false, editable))
                BeginRename(selected);

            if (ImGui::MenuItem("Delete", "Delete", false, editable))
                DeleteEntity(selected);

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Create"))
        {
            DrawCreateMenu({});
            ImGui::EndMenu();
        }
    }

    void HierarchyPanel::DeleteEntity(Entity entity)
    {
        m_deferred.emplace_back([this, entity] { m_context.ActiveScene.DestroyEntity(entity); });
    }

    void HierarchyPanel::DrawSearch()
    {
        ImGui::SetNextItemWidth(-FLT_MIN);

        const std::string hint = std::string(kIconSearch) + "  Search";
        if (ImGui::InputTextWithHint("##Search", hint.c_str(), m_filter.InputBuf, IM_ARRAYSIZE(m_filter.InputBuf)))
            m_filter.Build();

        // Escape leaves the box, and clears it too, as it does in most search boxes
        if (ImGui::IsItemDeactivated() && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
            m_filter.Clear();
    }

    // Marks an entity to be shown when its name matches or something under it does, and says whether it is
    bool HierarchyPanel::CollectMatches(entt::entity handle)
    {
        const Entity entity = m_context.ActiveScene.GetEntity(handle);

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

        Scene& scene = m_context.ActiveScene;
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

        if (m_context.Selected == entity)
            flags |= ImGuiTreeNodeFlags_Selected;

        const bool dimmed = readOnly || !entity.GetComponent<TransformComponent>().ActiveInHierarchy;
        if (dimmed)
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));

        ImGui::PushID(static_cast<int>(entt::to_integral(handle)));
        const bool renaming = handle == m_renaming;
        const bool open = renaming
            ? ImGui::TreeNodeEx("##Entity", flags, "%s", EntityIcon(entity, m_context.Assets.Components()))
            : ImGui::TreeNodeEx("##Entity", flags, isInstance ? "%s  %s [scene]" : "%s  %s", EntityIcon(entity, m_context.Assets.Components()), name.Name.c_str());

        if (dimmed)
            ImGui::PopStyleColor();

        if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen())
            m_context.SelectEntity(entity);

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
                m_deferred.emplace_back([this, dropped, entity] { m_context.ActiveScene.SetParent(m_context.ActiveScene.GetEntity(dropped), entity); });
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

            if (ImGui::MenuItem("Rename", "F2"))
                BeginRename(entity);

            if (ImGui::MenuItem("Delete", "Delete"))
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
        ImGui::SameLine();
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
            m_deferred.emplace_back([entity, name = std::move(name)] { entity.GetComponent<NameComponent>().Name = name; });
    }

    void HierarchyPanel::DrawCreateMenu(Entity parent)
    {
        const auto create = [this, parent](std::string name, std::function<void(Entity)> setup)
        {
            m_deferred.emplace_back([this, parent, name = std::move(name), setup = std::move(setup)]
            {
                const Entity created = m_context.ActiveScene.CreateEntity(name, parent);
                if (setup)
                    setup(created);

                m_context.SelectEntity(created);
            });
        };

        if (ImGui::MenuItem("Create Empty"))
            create("Entity", nullptr);

        // A submenu for each menu the components' presets name, in the order the components were registered
        const ComponentRegistry& components = m_context.Assets.Components();

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
            if (!ImGui::BeginMenu(("Create " + std::string(menu)).c_str()))
                continue;

            for (const ComponentType& type : components.Types())
            {
                for (const ComponentPreset& preset : type.Presets)
                {
                    if (preset.Menu == menu && ImGui::MenuItem(preset.Name.c_str()))
                        create(preset.Name, [this, &type, &preset](Entity entity) { ApplyPreset(type, preset, entity, m_context.Assets); });
                }
            }

            ImGui::EndMenu();
        }
    }
}
