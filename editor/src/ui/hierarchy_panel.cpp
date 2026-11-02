#include "crimson_editor/ui/hierarchy_panel.hpp"

#include <algorithm>

#include <imgui.h>

namespace crimson::editor::ui
{
    static constexpr const char* kEntityPayload = "CRIMSON_ENTITY";

    void HierarchyPanel::OnImGui()
    {
        ImGui::Begin("Hierarchy");

        Scene& scene = m_context.ActiveScene;

        for (const entt::entity root : scene.GetRoots())
            DrawEntity(root);

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

        if (m_context.Selected && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)
            && !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Delete, false))
        {
            const Entity selected = m_context.Selected;
            m_deferred.emplace_back([this, selected] { m_context.ActiveScene.DestroyEntity(selected); });
        }

        ImGui::End();

        for (const auto& action : m_deferred)
            action();

        m_deferred.clear();
    }

    void HierarchyPanel::DrawEntity(entt::entity handle)
    {
        Scene& scene = m_context.ActiveScene;
        const Entity entity = scene.GetEntity(handle);

        const auto& relationship = entity.GetComponent<RelationshipComponent>();
        const auto& name = entity.GetComponent<NameComponent>();

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick
            | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;

        if (relationship.Children.empty())
            flags |= ImGuiTreeNodeFlags_Leaf;

        if (m_context.Selected == entity)
            flags |= ImGuiTreeNodeFlags_Selected;

        const bool activeInHierarchy = entity.GetComponent<TransformComponent>().ActiveInHierarchy;
        if (!activeInHierarchy)
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));

        ImGui::PushID(static_cast<int>(entt::to_integral(handle)));
        const bool open = ImGui::TreeNodeEx("##Entity", flags, "%s", name.Name.c_str());

        if (!activeInHierarchy)
            ImGui::PopStyleColor();

        if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen())
            m_context.SelectEntity(entity);

        if (ImGui::BeginDragDropSource())
        {
            ImGui::SetDragDropPayload(kEntityPayload, &handle, sizeof(handle));
            ImGui::TextUnformatted(name.Name.c_str());
            ImGui::EndDragDropSource();
        }

        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kEntityPayload))
            {
                const entt::entity dropped = *static_cast<const entt::entity*>(payload->Data);
                m_deferred.emplace_back([this, dropped, entity] { m_context.ActiveScene.SetParent(m_context.ActiveScene.GetEntity(dropped), entity); });
            }

            ImGui::EndDragDropTarget();
        }

        if (ImGui::BeginPopupContextItem("##EntityContext"))
        {
            DrawCreateMenu(entity);
            ImGui::Separator();

            if (ImGui::MenuItem("Delete"))
                m_deferred.emplace_back([this, entity] { m_context.ActiveScene.DestroyEntity(entity); });

            ImGui::EndPopup();
        }

        if (open)
        {
            for (const entt::entity child : relationship.Children)
                DrawEntity(child);

            ImGui::TreePop();
        }

        ImGui::PopID();
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

        if (ImGui::BeginMenu("Create Light"))
        {
            if (ImGui::MenuItem("Directional")) createLight("Directional Light", LightType::Directional);
            if (ImGui::MenuItem("Point"))       createLight("Point Light", LightType::Point);
            if (ImGui::MenuItem("Spot"))        createLight("Spot Light", LightType::Spot);
            ImGui::EndMenu();
        }
    }
}
