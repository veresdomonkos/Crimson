#include "crimson_editor/ui/hierarchy_panel.hpp"

#include <algorithm>
#include <cfloat>
#include <filesystem>
#include <string>

#include <imgui.h>

#include <crimson/scene/scene_renderer.hpp>

#include "crimson_editor/ui/editor_theme.hpp"
#include "crimson_editor/ui/icons.hpp"

namespace crimson::editor::ui
{
    static constexpr const char* kEntityPayload = "CRIMSON_ENTITY";

    // The engine's built-in meshes, in engine/resources/meshes
    struct BuiltInMesh
    {
        const char* Name;
        const char* Path;
    };

    static constexpr BuiltInMesh kBuiltInMeshes[] = {
        { "Cube",     "engine/meshes/cube.obj" },
        { "Sphere",   "engine/meshes/sphere.obj" },
        { "Cylinder", "engine/meshes/cylinder.obj" },
        { "Capsule",  "engine/meshes/capsule.obj" },
        { "Plane",    "engine/meshes/plane.obj" },
        { "Quad",     "engine/meshes/quad.obj" },
    };

    // What kind of entity it is, at a glance
    static const char* EntityIcon(Entity entity)
    {
        if (entity.HasComponent<SceneInstanceComponent>())
            return kIconSceneInstance;

        if (const auto* light = entity.TryGetComponent<LightComponent>())
        {
            switch (light->Type)
            {
                case LightType::Directional: return kIconDirectionalLight;
                case LightType::Point:       return kIconPointLight;
                case LightType::Spot:        return kIconSpotLight;
            }
        }

        if (entity.HasComponent<MeshRendererComponent>())
            return kIconMesh;

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
            ? ImGui::TreeNodeEx("##Entity", flags, "%s", EntityIcon(entity))
            : ImGui::TreeNodeEx("##Entity", flags, isInstance ? "%s  %s [scene]" : "%s  %s", EntityIcon(entity), name.Name.c_str());

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
        const auto create = [this, parent](const char* name, std::function<void(Entity)> setup)
        {
            m_deferred.emplace_back([this, parent, name, setup = std::move(setup)]
            {
                const Entity created = m_context.ActiveScene.CreateEntity(name, parent);
                if (setup)
                    setup(created);

                m_context.SelectEntity(created);
            });
        };

        const auto createLight = [&create](const char* name, LightType type)
        {
            create(name, [type](Entity entity)
            {
                entity.AddComponent<LightComponent>().Type = type;
            });
        };

        if (ImGui::MenuItem("Create Empty"))
            create("Entity", nullptr);

        if (ImGui::BeginMenu("Create 3D Object"))
        {
            for (const BuiltInMesh& mesh : kBuiltInMeshes)
            {
                const AssetID id = m_context.Assets.FindId(mesh.Path);

                // The default material fills the mesh's one slot when it is resolved
                if (ImGui::MenuItem(mesh.Name, nullptr, false, static_cast<bool>(id)))
                {
                    create(mesh.Name, [this, id](Entity entity)
                    {
                        auto& renderer = entity.AddComponent<MeshRendererComponent>();
                        renderer.MeshAsset = id;
                        ResolveMeshRenderer(renderer, m_context.Assets);
                    });
                }
            }

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Create Light"))
        {
            if (ImGui::MenuItem("Directional")) createLight("Directional Light", LightType::Directional);
            if (ImGui::MenuItem("Point"))       createLight("Point Light", LightType::Point);
            if (ImGui::MenuItem("Spot"))        createLight("Spot Light", LightType::Spot);
            ImGui::EndMenu();
        }
    }
}
