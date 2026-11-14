#pragma once
#include <crimson/asset_manager/asset_manager.hpp>
#include <crimson/scene/component_registry.hpp>

#include "crimson_editor/editor_context.hpp"

namespace crimson::editor::ui
{
    // The icon a type registered, which can depend on the component's values. Null for a type with none.
    [[nodiscard]] const char* ComponentIcon(const ComponentType& type, const void* component);

    // A field's row in a props table, by its type, with its default to reset to. Returns true when the value changed.
    // The component is the one the field is of, for what the field takes from its values.
    bool DrawField(const FieldInfo& field, const void* component, FieldValue& value, EditorContext& context);

    // What a vector becomes when one of its axes was edited and the others follow: the axis that changed sets the ratio.
    // The edited one if none changed, or the one that changed was zero, as there is no ratio to keep then.
    [[nodiscard]] glm::vec3 ScaleUniformly(glm::vec3 previous, glm::vec3 edited);

    // The fields that mean something with the component's values, each written back when it changed, then the
    // component's constraint. Returns true when any changed.
    bool DrawComponentFields(const ComponentType& type, void* component, EditorContext& context);
}
