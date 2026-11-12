#include "crimson_editor/ui/component_fields.hpp"

#include <string>
#include <vector>

#include <imgui.h>

#include "crimson_editor/ui/asset_drag.hpp"
#include "crimson_editor/ui/icons.hpp"
#include "crimson_editor/ui/property_fields.hpp"

namespace crimson::editor::ui
{
    namespace
    {
        // One row per entry, as a list of assets is usually short and each entry has a meaning of its own, like a slot
        bool DrawAssetList(const FieldInfo& field, std::vector<AssetID>& ids, const AssetManager& assets)
        {
            bool changed = false;

            for (size_t index = 0; index < ids.size(); ++index)
            {
                const std::string label = field.Label + " " + std::to_string(index);

                ImGui::PushID(static_cast<int>(index));
                props::BeginRow(label.c_str());
                changed |= DrawAssetCombo("##Asset", assets, field.Asset, ids[index]);
                props::ResetTarget();

                if (props::EndRow(true) && ids[index])
                {
                    ids[index] = {};
                    changed = true;
                }

                ImGui::PopID();
            }

            return changed;
        }
    }

    const char* ComponentIcon(const ComponentType& type, const void* component)
    {
        if (type.Id == "transform")
            return kIconTransform;

        if (type.Id == "meshRenderer")
            return kIconMesh;

        if (type.Id == "light")
        {
            switch (static_cast<const LightComponent*>(component)->Type)
            {
                case LightType::Directional: return kIconDirectionalLight;
                case LightType::Point:       return kIconPointLight;
                case LightType::Spot:        return kIconSpotLight;
            }
        }

        return nullptr;
    }

    bool DrawAssetCombo(const char* id, const AssetManager& assets, AssetType type, AssetID& current)
    {
        bool changed = false;

        if (BeginAssetCombo(id, assets, current))
        {
            for (const AssetInfo* info : assets.List(type))
            {
                if (IsInternal(*info))
                    continue;

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

    bool DrawField(const FieldInfo& field, FieldValue& value, const AssetManager& assets)
    {
        const char* label = field.Label.c_str();
        const float min = field.Range ? field.Range->first : 0.0f;
        const float max = field.Range ? field.Range->second : 0.0f;

        switch (field.Type)
        {
            case FieldType::Bool:
                return props::Checkbox(label, std::get<bool>(value), &std::get<bool>(field.Default));
            case FieldType::Int:
                return props::Int(label, std::get<int32_t>(value), field.Speed, static_cast<int>(min), static_cast<int>(max), &std::get<int32_t>(field.Default));
            case FieldType::Float:
                return props::Float(label, std::get<float>(value), field.Speed, min, max, &std::get<float>(field.Default));
            case FieldType::Vec3:
                return props::Vec3(label, std::get<glm::vec3>(value), field.Speed, &std::get<glm::vec3>(field.Default));
            case FieldType::Color:
                return props::Color(label, &std::get<glm::vec3>(value).x, 3, &std::get<glm::vec3>(field.Default).x);
            case FieldType::String:
                return props::String(label, std::get<std::string>(value), &std::get<std::string>(field.Default));
            case FieldType::Enum:
            {
                std::vector<const char*> labels;
                for (const std::string& enumLabel : field.EnumLabels)
                    labels.push_back(enumLabel.c_str());

                return props::Combo(label, std::get<int32_t>(value), labels.data(), static_cast<int>(labels.size()), &std::get<int32_t>(field.Default));
            }
            case FieldType::Asset:
            {
                AssetID& id = std::get<AssetID>(value);

                props::BeginRow(label);
                bool changed = DrawAssetCombo("##Asset", assets, field.Asset, id);
                props::ResetTarget();

                const AssetID reset = std::get<AssetID>(field.Default);
                if (props::EndRow(true) && id != reset)
                {
                    id = reset;
                    changed = true;
                }

                return changed;
            }
            case FieldType::AssetList:
                return DrawAssetList(field, std::get<std::vector<AssetID>>(value), assets);
        }

        return false;
    }

    bool DrawComponentFields(const ComponentType& type, void* component, const AssetManager& assets)
    {
        bool changed = false;

        for (const FieldInfo& field : type.Fields)
        {
            if (field.Visible && !field.Visible(component))
                continue;

            FieldValue value = field.Get(component);
            if (!DrawField(field, value, assets))
                continue;

            field.Set(component, value);
            changed = true;
        }

        if (changed && type.Constrain)
            type.Constrain(component);

        return changed;
    }
}
