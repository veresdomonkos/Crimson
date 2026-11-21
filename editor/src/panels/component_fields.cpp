#include "crimson_editor/panels/component_fields.hpp"

#include <array>
#include <string>
#include <vector>

#include "crimson_editor/panels/asset_field.hpp"
#include "crimson_editor/ui/icons.hpp"
#include "crimson_editor/ui/layout.hpp"
#include "crimson_editor/ui/property_fields.hpp"

namespace crimson::editor
{
    // One row per entry, as a list of assets is usually short and each entry has a meaning of its own, like a slot.
    // An entry that is empty shows what it uses instead. The list is sized to its rows only in the copy, so looking at it
    // changes nothing, and an edit takes the whole copy.
    static bool DrawAssetList(const FieldInfo& field, const void* component, std::vector<AssetID>& ids, EditorContext& context)
    {
        std::vector<AssetListItem> items;

        if (field.Items)
        {
            items = field.Items(component, context.Session.GetAssets());
        }
        else
        {
            for (size_t index = 0; index < ids.size(); ++index)
                items.push_back({ field.Label + " " + std::to_string(index), AssetID{} });
        }

        ids.resize(items.size());

        bool changed = false;

        for (size_t index = 0; index < items.size(); ++index)
        {
            const ui::IdScope scope(static_cast<int>(index));
            ui::props::BeginRow(items[index].Label.c_str());

            // The stored id is compared with the pick, so choosing what an empty entry shows stores it
            if (AssetField("##Asset", context, field.Asset, ids[index], items[index].Empty))
                changed = true;

            ui::props::ResetTarget();

            if (ui::props::EndRow(true) && ids[index])
            {
                ids[index] = {};
                changed = true;
            }

        }

        return changed;
    }

    const char* ComponentIcon(const ComponentType& type, const void* component)
    {
        return ui::icons::Named(type.Icon ? type.Icon(component) : std::string_view());
    }

    glm::vec3 ScaleUniformly(glm::vec3 previous, glm::vec3 edited, glm::vec3 shape)
    {
        // Scaled down to zero, the vector has no proportions left, so it grows back with those it had before
        const glm::vec3 base = previous == glm::vec3(0.0f) ? shape : previous;

        for (int axis = 0; axis < 3; ++axis)
        {
            if (edited[axis] != previous[axis] && base[axis] != 0.0f)
                return base * (edited[axis] / base[axis]);
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
                return ui::props::Checkbox(label, std::get<bool>(value), &std::get<bool>(field.Default));
            case FieldType::Int:
                return ui::props::Int(label, std::get<int32_t>(value), field.Speed, static_cast<int>(min), static_cast<int>(max), &std::get<int32_t>(field.Default));
            case FieldType::Float:
                return ui::props::Float(label, std::get<float>(value), field.Speed, min, max, &std::get<float>(field.Default));
            case FieldType::Vec3:
            {
                glm::vec3& vector = std::get<glm::vec3>(value);
                const glm::vec3 previous = vector;

                const bool changed = ui::props::Vec3(label, vector, field.Speed, &std::get<glm::vec3>(field.Default));
                if (!field.UniformToggle)
                    return changed;

                // Kept by the window, as it belongs to the editor rather than the component
                bool uniform = ui::stored::GetBool("##Uniform");

                // The last proportions that weren't all zero, for growing back from zero
                static constexpr std::array kShapeIds = { "##ShapeX", "##ShapeY", "##ShapeZ" };
                glm::vec3 shape;
                for (int axis = 0; axis < 3; ++axis)
                    shape[axis] = ui::stored::GetFloat(kShapeIds[axis], 1.0f);

                if (changed && uniform)
                    vector = ScaleUniformly(previous, vector, shape);

                if (vector != glm::vec3(0.0f))
                {
                    for (int axis = 0; axis < 3; ++axis)
                        ui::stored::SetFloat(kShapeIds[axis], vector[axis]);
                }

                ui::props::Checkbox(("Uniform " + field.Label).c_str(), uniform);
                ui::stored::SetBool("##Uniform", uniform);
                return changed;
            }
            case FieldType::Color:
                return ui::props::Color(label, &std::get<glm::vec3>(value).x, 3, &std::get<glm::vec3>(field.Default).x);
            case FieldType::String:
                return ui::props::String(label, std::get<std::string>(value), &std::get<std::string>(field.Default));
            case FieldType::Enum:
            {
                std::vector<const char*> labels;
                for (const EnumOption& option : field.EnumOptions)
                    labels.push_back(option.Label.c_str());

                return ui::props::Combo(label, std::get<int32_t>(value), labels.data(), static_cast<int>(labels.size()), &std::get<int32_t>(field.Default));
            }
            case FieldType::Asset:
            {
                AssetID& id = std::get<AssetID>(value);

                ui::props::BeginRow(label);
                bool changed = AssetField("##Asset", context, field.Asset, id);
                ui::props::ResetTarget();

                const AssetID reset = std::get<AssetID>(field.Default);
                if (ui::props::EndRow(true) && id != reset)
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
