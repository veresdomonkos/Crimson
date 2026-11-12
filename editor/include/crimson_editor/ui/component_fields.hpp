#pragma once
#include <crimson/asset_manager/asset_manager.hpp>
#include <crimson/scene/component_registry.hpp>

namespace crimson::editor::ui
{
    // The icon of a component, by its type and, for a light, by its values. Null for a type with no icon of its own.
    [[nodiscard]] const char* ComponentIcon(const ComponentType& type, const void* component);

    // A combo of the project's assets of one type, which an asset can also be dropped on. Returns true when another was picked.
    bool DrawAssetCombo(const char* id, const AssetManager& assets, AssetType type, AssetID& current);

    // A field's row in a props table, by its type, with its default to reset to. Returns true when the value changed.
    bool DrawField(const FieldInfo& field, FieldValue& value, const AssetManager& assets);

    // The fields that mean something with the component's values, each written back when it changed, then the
    // component's constraint. Returns true when any changed.
    bool DrawComponentFields(const ComponentType& type, void* component, const AssetManager& assets);
}
