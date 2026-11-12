#include "crimson_editor/ui/inspector_panel.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <string>

#include <imgui.h>

#include <crimson/scene/scene_renderer.hpp>

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
            const theme::ScopedBold bold;
            open = ImGui::TreeNodeEx(label, flags, "%s  %s", icon, label);
        }

        if (removable && ImGui::BeginPopupContextItem())
        {
            if (ImGui::MenuItem("Remove Component"))
                removed = true;

            ImGui::EndPopup();
        }

        return open;
    }

    // A typed id through the combo that works on any type
    template <AssetType T>
    static bool DrawAssetCombo(const char* label, const AssetManager& assets, TypedAssetID<T>& current)
    {
        AssetID id = current;
        if (!DrawAssetCombo(label, assets, T, id))
            return false;

        current = assets.As<T>(id);
        return true;
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

        ImGui::Checkbox("##Active", &name.Active);
        ImGui::SameLine();

        std::array<char, 256> buffer{};
        std::snprintf(buffer.data(), buffer.size(), "%s", name.Name.c_str());

        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputText("##Name", buffer.data(), buffer.size()))
            name.Name = buffer.data();
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
            if (type.Id == "transform")
            {
                DrawTransform(*static_cast<TransformComponent*>(component));
            }
            else if (type.Id == "meshRenderer")
            {
                DrawMeshRenderer(*static_cast<MeshRendererComponent*>(component));
            }
            else if (props::BeginTable(type.Id.c_str()))
            {
                DrawComponentFields(type, component, m_context.Assets);
                props::EndTable();
            }

            ImGui::TreePop();
        }

        if (removed)
            type.Remove(entity);
    }

    void InspectorPanel::DrawTransform(TransformComponent& transform)
    {
        static const TransformComponent kDefaults;

        // Not from its fields, for the uniform scale, which belongs to the editor rather than the transform
        if (props::BeginTable("##Transform"))
        {
            props::Vec3("Position", transform.Position, 0.05f, &kDefaults.Position);
            props::Vec3("Rotation", transform.RotationDegrees, 0.5f, &kDefaults.RotationDegrees);

            const glm::vec3 previousScale = transform.Scale;
            if (props::Vec3("Scale", transform.Scale, 0.01f, &kDefaults.Scale) && m_uniformScale)
            {
                // The axis that changed sets the ratio for the others
                for (int axis = 0; axis < 3; ++axis)
                {
                    if (transform.Scale[axis] != previousScale[axis] && previousScale[axis] != 0.0f)
                    {
                        transform.Scale = previousScale * (transform.Scale[axis] / previousScale[axis]);
                        break;
                    }
                }
            }

            props::Checkbox("Uniform Scale", m_uniformScale);
            props::EndTable();
        }
    }

    void InspectorPanel::DrawMeshRenderer(MeshRendererComponent& renderer)
    {
        static const MeshRendererComponent kDefaults;
        bool changed = false;

        // Not from its fields, as its slots are named after the mesh's and can't be reset to one material
        if (props::BeginTable("##MeshRenderer"))
        {
            props::BeginRow("Mesh");
            changed |= DrawAssetCombo("##Asset", m_context.Assets, renderer.MeshAsset);
            props::EndRow(false);

            for (size_t slot = 0; slot < renderer.MaterialAssets.size(); ++slot)
            {
                const std::string label = slot < renderer.Resolved.MaterialSlots.size()
                    ? renderer.Resolved.MaterialSlots[slot]
                    : "Material " + std::to_string(slot);

                // Without one of its own, a slot goes back to the material that fits it, or the default one
                ImGui::PushID(static_cast<int>(slot));
                props::BeginRow(label.c_str());
                MaterialID& material = renderer.MaterialAssets[slot];
                changed |= DrawAssetCombo("##Asset", m_context.Assets, material);
                props::ResetTarget();

                if (props::EndRow(true) && material)
                {
                    material = {};
                    changed = true;
                }

                ImGui::PopID();
            }

            props::Checkbox("Casts Shadows", renderer.CastsShadows, &kDefaults.CastsShadows);
            props::EndTable();
        }

        if (changed)
            ResolveMeshRenderer(renderer, m_context.Assets);
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

                        type.Add(entity);

                        if (type.OnLoaded)
                            type.OnLoaded(entity, m_context.Assets);
                    });
                }
            }

            ImGui::EndPopup();
        }
    }
}
