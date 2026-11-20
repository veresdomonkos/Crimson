#include "crimson_editor/ui/inspector_panel.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <string>

#include <imgui.h>

#include "crimson_editor/ui/asset_drag.hpp"
#include "crimson_editor/ui/component_fields.hpp"
#include "crimson_editor/ui/editor_theme.hpp"
#include "crimson_editor/ui/icons.hpp"
#include "crimson_editor/ui/property_fields.hpp"

namespace crimson::editor::ui
{
    void InspectorPanel::OnImGui()
    {
        ImGui::Begin("Inspector");

        if (m_context.SelectedAsset)
        {
            m_assetInspector.Draw(m_context.SelectedAsset);
        }
        else if (const Entity entity = m_context.Selected)
        {
            // What comes from an instance's scene is only changed by opening that scene
            ImGui::BeginDisabled(entity.HasComponent<InstancedComponent>());

            DrawHeader(entity);

            for (const ComponentType& type : m_context.Assets.Components().Types())
                DrawComponent(entity, type);

            DrawUnknownComponents(entity);

            // An instance's own components wouldn't be saved
            if (!entity.HasComponent<SceneInstanceComponent>())
                DrawAddComponent(entity);

            ImGui::EndDisabled();

            DrawSceneInstance(entity);
        }
        else
        {
            theme::EmptyHint("Select an entity in the Hierarchy or the Scene view, or an asset in the Assets panel");
        }

        ImGui::End();
    }

    void InspectorPanel::DrawHeader(Entity entity)
    {
        auto& name = entity.GetComponent<NameComponent>();

        if (theme::Checkbox("##Active", name.Active))
            m_context.Edited = true;
        ImGui::SameLine();

        std::array<char, 256> buffer{};
        std::snprintf(buffer.data(), buffer.size(), "%s", name.Name.c_str());

        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputText("##Name", buffer.data(), buffer.size()))
        {
            name.Name = buffer.data();
            m_context.Edited = true;
        }
    }

    void InspectorPanel::DrawComponent(Entity entity, const ComponentType& type)
    {
        void* component = type.Get(entity);
        if (!component)
            return;

        const char* icon = ComponentIcon(type, component);

        // What can be added can be removed, which every entity's own, like the transform, can't
        bool removed = false;
        const bool open = theme::BeginSection(type.Name.c_str(), icon ? icon : icons::kEntity);

        if (type.Addable && ImGui::BeginPopupContextItem())
        {
            if (ImGui::MenuItem("Remove Component"))
                removed = true;

            ImGui::EndPopup();
        }

        if (open)
        {
            if (props::BeginTable(type.Id.c_str()))
            {
                if (DrawComponentFields(type, component, m_context))
                    m_context.Edited = true;

                props::EndTable();
            }

            theme::EndSection();
        }

        if (removed)
        {
            type.Remove(entity);
            m_context.Edited = true;
        }
    }

    void InspectorPanel::DrawUnknownComponents(Entity entity)
    {
        const auto* unknown = entity.TryGetComponent<UnknownComponents>();
        if (!unknown)
            return;

        // Like one from a script that isn't loaded. It can't be shown, but the scene keeps it.
        for (const SceneComponent& component : unknown->Components)
        {
            ImGui::TextDisabled("%s  %s", icons::kWarning, component.Type.c_str());
            ImGui::SetItemTooltip("A component this editor doesn't know. It stays as it is when the scene is saved.");
        }
    }

    void InspectorPanel::DrawSceneInstance(Entity entity)
    {
        const auto* instance = entity.TryGetComponent<SceneInstanceComponent>();
        if (!instance)
            return;

        ImGui::Spacing();

        if (m_context.Assets.IsMissing(instance->Scene))
        {
            ImGui::TextColored(theme::Warning, "Instance of %s", AssetLabel(m_context.Assets, instance->Scene).c_str());
            ImGui::TextDisabled("It shows again once the file is back and the scene is opened again");
            return;
        }

        ImGui::TextDisabled("Instance of %s", AssetLabel(m_context.Assets, instance->Scene).c_str());

        if (ImGui::Button("Open Source", ImVec2(-1.0f, 0.0f)))
            m_context.OpenScene(instance->Scene);
    }

    void InspectorPanel::DrawAddComponent(Entity entity)
    {
        ImGui::Spacing();

        if (ImGui::Button("Add Component", ImVec2(-1.0f, 0.0f)))
            ImGui::OpenPopup("AddComponent");

        if (ImGui::BeginPopup("AddComponent"))
        {
            for (const ComponentType& type : m_context.Assets.Components().Types())
            {
                if (!type.Addable)
                    continue;

                // Between frames, as adding a component can move the others, which this frame's panels still point to
                if (ImGui::MenuItem(type.Name.c_str(), nullptr, false, !type.Has(entity)))
                {
                    m_context.Deferred.emplace_back([this, &type, entity]
                    {
                        if (!entity || type.Has(entity))
                            return;

                        type.Add(entity);
                    });
                }
            }

            ImGui::EndPopup();
        }
    }
}
