#include "crimson_editor/ui/component_fields.hpp"

#include <string>
#include <vector>

#include <imgui.h>

#include "crimson_editor/ui/asset_field.hpp"
#include "crimson_editor/ui/icons.hpp"
#include "crimson_editor/ui/property_fields.hpp"

namespace crimson::editor::ui
{
    namespace
    {
        // One row per entry, as a list of assets is usually short and each entry has a meaning of its own, like a slot
        bool DrawAssetList(const FieldInfo& field, const void* component, std::vector<AssetID>& ids, EditorContext& context)
        {
            bool changed = false;

            for (size_t index = 0; index < ids.size(); ++index)
            {
                const std::string label = field.ItemLabel ? field.ItemLabel(component, index) : field.Label + " " + std::to_string(index);

                ImGui::PushID(static_cast<int>(index));
                props::BeginRow(label.c_str());
                changed |= AssetField("##Asset", context, field.Asset, ids[index]);
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
        return IconNamed(type.Icon ? type.Icon(component) : std::string_view());
    }

    glm::vec3 ScaleUniformly(glm::vec3 previous, glm::vec3 edited)
    {
        for (int axis = 0; axis < 3; ++axis)
        {
            if (edited[axis] != previous[axis] && previous[axis] != 0.0f)
                return previous * (edited[axis] / previous[axis]);
        }

        return edited;
    }

    bool DrawField(const FieldInfo& field, const void* component, FieldValue& value, EditorContext& context)
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
            {
                glm::vec3& vector = std::get<glm::vec3>(value);
                const glm::vec3 previous = vector;

                const bool changed = props::Vec3(label, vector, field.Speed, &std::get<glm::vec3>(field.Default));
                if (!field.UniformToggle)
                    return changed;

                // Kept by the window, as it belongs to the editor rather than the component
                ImGuiStorage* storage = ImGui::GetStateStorage();
                const ImGuiID toggle = ImGui::GetID("##Uniform");
                bool uniform = storage->GetBool(toggle);

                if (changed && uniform)
                    vector = ScaleUniformly(previous, vector);

                props::Checkbox(("Uniform " + field.Label).c_str(), uniform);
                storage->SetBool(toggle, uniform);
                return changed;
            }
            case FieldType::Color:
                return props::Color(label, &std::get<glm::vec3>(value).x, 3, &std::get<glm::vec3>(field.Default).x);
            case FieldType::String:
                return props::String(label, std::get<std::string>(value), &std::get<std::string>(field.Default));
            case FieldType::Enum:
            {
                std::vector<const char*> labels;
                for (const EnumOption& option : field.EnumOptions)
                    labels.push_back(option.Label.c_str());

                return props::Combo(label, std::get<int32_t>(value), labels.data(), static_cast<int>(labels.size()), &std::get<int32_t>(field.Default));
            }
            case FieldType::Asset:
            {
                AssetID& id = std::get<AssetID>(value);

                props::BeginRow(label);
                bool changed = AssetField("##Asset", context, field.Asset, id);
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
                return DrawAssetList(field, component, std::get<std::vector<AssetID>>(value), context);
        }

        return false;
    }

    bool DrawComponentFields(const ComponentType& type, void* component, EditorContext& context)
    {
        bool changed = false;

        for (const FieldInfo& field : type.Fields)
        {
            if (field.Visible && !field.Visible(component))
                continue;

            FieldValue value = field.Get(component);
            if (!DrawField(field, component, value, context))
                continue;

            field.Set(component, value);
            changed = true;
        }

        if (changed && type.Constrain)
            type.Constrain(component);

        return changed;
    }
}
