#include "crimson_editor/ui/inspector_panel.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <string>

#include <imgui.h>

#include <crimson/scene/scene_renderer.hpp>

namespace crimson::editor::ui
{
    // A collapsible section. Returns true while it is open, and needs EndComponentSection then.
    static bool BeginComponentSection(const char* label, bool removable, bool& removed)
    {
        constexpr ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_Framed;

        const bool open = ImGui::TreeNodeEx(label, flags);

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
        const AssetMetadata* metadata = id ? assets.GetMetadata(id) : nullptr;
        return metadata ? metadata->Source.generic_string() : "None";
    }

    // Returns true when another asset was picked
    static bool DrawAssetCombo(const char* label, const AssetManager& assets, AssetType type, AssetID& current)
    {
        bool changed = false;

        if (ImGui::BeginCombo(label, GetAssetName(assets, current).c_str()))
        {
            for (const AssetID id : assets.GetAssetsOfType(type))
            {
                const bool selected = id == current;

                if (ImGui::Selectable(GetAssetName(assets, id).c_str(), selected))
                {
                    changed = !selected;
                    current = id;
                }
            }

            ImGui::EndCombo();
        }

        return changed;
    }

    void InspectorPanel::OnImGui()
    {
        ImGui::Begin("Inspector");

        if (const Entity entity = m_context.Selected)
        {
            DrawHeader(entity);

            // Fields fill the panel, leaving room for their labels
            ImGui::PushItemWidth(-ImGui::GetFontSize() * 4.5f);
            DrawTransform(entity);
            DrawLight(entity);
            DrawMeshRenderer(entity);
            ImGui::PopItemWidth();

            DrawAddComponent(entity);
        }
        else
        {
            ImGui::TextDisabled("Nothing selected");
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
        if (!BeginComponentSection("Transform", false, removed))
            return;

        auto& transform = entity.GetComponent<TransformComponent>();

        ImGui::DragFloat3("Position", &transform.Position.x, 0.05f);
        ImGui::DragFloat3("Rotation", &transform.RotationDegrees.x, 0.5f);

        const glm::vec3 previousScale = transform.Scale;
        if (ImGui::DragFloat3("Scale", &transform.Scale.x, 0.01f) && m_uniformScale)
        {
            // The axis being dragged sets the ratio for the others
            for (int axis = 0; axis < 3; ++axis)
            {
                if (transform.Scale[axis] != previousScale[axis] && previousScale[axis] != 0.0f)
                {
                    transform.Scale = previousScale * (transform.Scale[axis] / previousScale[axis]);
                    break;
                }
            }
        }

        ImGui::Checkbox("Uniform Scale", &m_uniformScale);
        ImGui::TreePop();
    }

    void InspectorPanel::DrawLight(Entity entity)
    {
        auto* light = entity.TryGetComponent<LightComponent>();
        if (!light)
            return;

        bool removed = false;
        if (BeginComponentSection("Light", true, removed))
        {
            constexpr std::array kTypeNames = { "Directional", "Point", "Spot" };

            int type = static_cast<int>(light->Type);
            if (ImGui::Combo("Type", &type, kTypeNames.data(), static_cast<int>(kTypeNames.size())))
                light->Type = static_cast<LightType>(type);

            ImGui::ColorEdit3("Color", &light->Color.x);
            ImGui::DragFloat("Intensity", &light->Intensity, 0.01f, 0.0f, 1000.0f);

            if (light->Type != LightType::Directional)
                ImGui::DragFloat("Range", &light->Range, 0.05f, 0.01f, 10000.0f);

            if (light->Type == LightType::Spot)
            {
                ImGui::DragFloat("Outer Cone", &light->OuterConeDegrees, 0.25f, 1.0f, 89.0f);
                ImGui::DragFloat("Inner Cone", &light->InnerConeDegrees, 0.25f, 0.0f, light->OuterConeDegrees);
                light->InnerConeDegrees = std::min(light->InnerConeDegrees, light->OuterConeDegrees);
            }

            ImGui::Checkbox("Casts Shadow", &light->CastsShadow);
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
        if (BeginComponentSection("Mesh Renderer", true, removed))
        {
            bool changed = DrawAssetCombo("Mesh", m_context.Assets, AssetType::Mesh, renderer->MeshAsset);

            for (size_t slot = 0; slot < renderer->MaterialAssets.size(); ++slot)
            {
                const std::string label = slot < renderer->Resolved.MaterialSlots.size()
                    ? renderer->Resolved.MaterialSlots[slot]
                    : "Material " + std::to_string(slot);

                ImGui::PushID(static_cast<int>(slot));
                changed |= DrawAssetCombo(label.c_str(), m_context.Assets, AssetType::Material, renderer->MaterialAssets[slot]);
                ImGui::PopID();
            }

            ImGui::Checkbox("Casts Shadows", &renderer->CastsShadows);
            ImGui::TreePop();

            if (changed)
                ResolveMeshRenderer(*renderer, m_context.Assets, m_context.DefaultMaterial);
        }

        if (removed)
            entity.RemoveComponent<MeshRendererComponent>();
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
