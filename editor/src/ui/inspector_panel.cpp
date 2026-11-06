#include "crimson_editor/ui/inspector_panel.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <string>

#include <imgui.h>

#include <crimson/scene/scene_renderer.hpp>

#include "crimson_editor/ui/asset_drag.hpp"
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

    static std::string GetAssetName(const AssetManager& assets, AssetID id)
    {
        const AssetInfo* info = assets.GetInfo(id);
        return info ? info->Path : "None";
    }

    // Returns true when another asset was picked
    static bool DrawAssetCombo(const char* label, const AssetManager& assets, AssetType type, AssetID& current)
    {
        bool changed = false;

        if (ImGui::BeginCombo(label, GetAssetName(assets, current).c_str()))
        {
            for (const AssetInfo* info : assets.List(type))
            {
                const bool selected = info->Id == current;

                if (ImGui::Selectable(info->Path.c_str(), selected))
                {
                    changed = !selected;
                    current = info->Id;
                }
            }

            ImGui::EndCombo();
        }

        if (const AssetID dropped = AcceptAssetOnLastItem(assets, type); dropped && dropped != current)
        {
            current = dropped;
            changed = true;
        }

        return changed;
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
            DrawTransform(entity);
            DrawLight(entity);
            DrawMeshRenderer(entity);

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

    void InspectorPanel::DrawTransform(Entity entity)
    {
        bool removed = false;
        if (!BeginComponentSection(kIconTransform, "Transform", false, removed))
            return;

        static const TransformComponent kDefaults;
        auto& transform = entity.GetComponent<TransformComponent>();

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

        ImGui::TreePop();
    }

    void InspectorPanel::DrawLight(Entity entity)
    {
        auto* light = entity.TryGetComponent<LightComponent>();
        if (!light)
            return;

        bool removed = false;
        const char* icon = light->Type == LightType::Directional ? kIconDirectionalLight : light->Type == LightType::Spot ? kIconSpotLight : kIconPointLight;
        if (BeginComponentSection(icon, "Light", true, removed))
        {
            static const LightComponent kDefaults;
            constexpr std::array kTypeNames = { "Directional", "Point", "Spot" };

            if (props::BeginTable("##Light"))
            {
                const int defaultType = static_cast<int>(kDefaults.Type);
                int type = static_cast<int>(light->Type);
                if (props::Combo("Type", type, kTypeNames.data(), static_cast<int>(kTypeNames.size()), &defaultType))
                    light->Type = static_cast<LightType>(type);

                props::Color("Color", &light->Color.x, 3, &kDefaults.Color.x);
                props::Float("Intensity", light->Intensity, 0.01f, 0.0f, 1000.0f, &kDefaults.Intensity);

                if (light->Type != LightType::Directional)
                    props::Float("Range", light->Range, 0.05f, 0.01f, 10000.0f, &kDefaults.Range);

                if (light->Type == LightType::Spot)
                {
                    props::Float("Outer Cone", light->OuterConeDegrees, 0.25f, 1.0f, 89.0f, &kDefaults.OuterConeDegrees);
                    props::Float("Inner Cone", light->InnerConeDegrees, 0.25f, 0.0f, light->OuterConeDegrees, &kDefaults.InnerConeDegrees);
                    light->InnerConeDegrees = std::min(light->InnerConeDegrees, light->OuterConeDegrees);
                }

                props::Checkbox("Casts Shadow", light->CastsShadow, &kDefaults.CastsShadow);
                props::EndTable();
            }

            ImGui::TreePop();
        }

        if (removed)
            entity.RemoveComponent<LightComponent>();
    }

    void InspectorPanel::DrawMeshRenderer(Entity entity)
    {
        auto* renderer = entity.TryGetComponent<MeshRendererComponent>();
        if (!renderer)
            return;

        bool removed = false;
        if (BeginComponentSection(kIconMesh, "Mesh Renderer", true, removed))
        {
            static const MeshRendererComponent kDefaults;
            bool changed = false;

            if (props::BeginTable("##MeshRenderer"))
            {
                props::BeginRow("Mesh");
                changed |= DrawAssetCombo("##Asset", m_context.Assets, AssetType::Mesh, renderer->MeshAsset);
                props::EndRow(false);

                for (size_t slot = 0; slot < renderer->MaterialAssets.size(); ++slot)
                {
                    const std::string label = slot < renderer->Resolved.MaterialSlots.size()
                        ? renderer->Resolved.MaterialSlots[slot]
                        : "Material " + std::to_string(slot);

                    // Without one of its own, a slot goes back to the material that fits it, or the default one
                    ImGui::PushID(static_cast<int>(slot));
                    props::BeginRow(label.c_str());
                    AssetID& material = renderer->MaterialAssets[slot];
                    changed |= DrawAssetCombo("##Asset", m_context.Assets, AssetType::Material, material);
                    props::ResetTarget();

                    if (props::EndRow(true) && material)
                    {
                        material = {};
                        changed = true;
                    }

                    ImGui::PopID();
                }

                props::Checkbox("Casts Shadows", renderer->CastsShadows, &kDefaults.CastsShadows);
                props::EndTable();
            }

            ImGui::TreePop();

            if (changed)
                ResolveMeshRenderer(*renderer, m_context.Assets);
        }

        if (removed)
            entity.RemoveComponent<MeshRendererComponent>();
    }

    void InspectorPanel::DrawSceneInstance(Entity entity)
    {
        const auto* instance = entity.TryGetComponent<SceneInstanceComponent>();
        if (!instance)
            return;

        ImGui::Spacing();
        ImGui::TextDisabled("Instance of %s", GetAssetName(m_context.Assets, instance->Scene).c_str());

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
            const bool hasLight = entity.HasComponent<LightComponent>();
            const bool hasRenderer = entity.HasComponent<MeshRendererComponent>();

            if (ImGui::MenuItem("Light", nullptr, false, !hasLight))
                entity.AddComponent<LightComponent>();

            if (ImGui::MenuItem("Mesh Renderer", nullptr, false, !hasRenderer))
                entity.AddComponent<MeshRendererComponent>();

            ImGui::EndPopup();
        }
    }
}
