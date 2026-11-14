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
    // A collapsible section. Returns true while it is open, and needs EndComponentSection then.
    static bool BeginComponentSection(const char* icon, const char* label, bool removable, bool& removed)
    {
        constexpr ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_Framed;

        // The id is the label alone, so the section stays open or closed if its icon changes
        bool open = false;
        {
            // Header is the selection color, and a framed node would take it as its background
            ImGui::PushStyleColor(ImGuiCol_Header, theme::FrameBackground);
            const theme::ScopedBold bold;
            open = theme::IconTreeNode(label, flags, icon, theme::TextMuted, label);
            ImGui::PopStyleColor();
        }

        if (removable && ImGui::BeginPopupContextItem())
        {
            if (ImGui::MenuItem("Remove Component"))
                removed = true;

            ImGui::EndPopup();
        }

        return open;
    }

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

        if (ImGui::Checkbox("##Active", &name.Active))
            m_context.SceneEdited = true;
        ImGui::SameLine();

        std::array<char, 256> buffer{};
        std::snprintf(buffer.data(), buffer.size(), "%s", name.Name.c_str());

        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputText("##Name", buffer.data(), buffer.size()))
        {
            name.Name = buffer.data();
            m_context.SceneEdited = true;
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
        if (BeginComponentSection(icon ? icon : kIconEntity, type.Name.c_str(), type.Addable, removed))
        {
            if (props::BeginTable(type.Id.c_str()))
            {
                // Between frames, as resolving its assets may load them
                if (DrawComponentFields(type, component, m_context))
                {
                    m_context.SceneEdited = true;

                    if (type.ResolveAssets)
                    {
                        m_context.Deferred.emplace_back([this, &type, entity]
                        {
                            if (entity && type.Has(entity))
                                ResolveComponentAssets(type, type.Get(entity), m_context.Assets);
                        });
                    }
                }

                props::EndTable();
            }

            ImGui::TreePop();
        }

        if (removed)
        {
            type.Remove(entity);
            m_context.SceneEdited = true;
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
            ImGui::TextDisabled("%s  %s", kIconWarning, component.Type.c_str());
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

                // Between frames, as a component may load assets once it is there
                if (ImGui::MenuItem(type.Name.c_str(), nullptr, false, !type.Has(entity)))
                {
                    m_context.Deferred.emplace_back([this, &type, entity]
                    {
                        if (!entity || type.Has(entity))
                            return;

                        CreateComponent(type, entity, m_context.Assets);
                    });
                }
            }

            ImGui::EndPopup();
        }
    }
}
