#pragma once
#include <cstdint>
#include <deque>
#include <functional>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <entt/core/type_info.hpp>
#include <glm/glm.hpp>

#include "crimson/asset_manager/asset_id.hpp"
#include "crimson/scene/entity.hpp"

namespace crimson
{
    class AssetManager;

    enum class FieldType { Bool, Int, Float, Vec3, Color, Enum, String, Asset, AssetList };

    // A field's value, whatever its component is. Int and Enum hold an int32_t, Vec3 and Color a vec3, Asset an AssetID.
    using FieldValue = std::variant<bool, int32_t, float, glm::vec3, std::string, AssetID, std::vector<AssetID>>;

    // The empty value of the alternative a field of this type holds
    [[nodiscard]] FieldValue EmptyValue(FieldType type);

    // One value of an Enum field, in the order of the values
    struct EnumOption
    {
        std::string Key;   // in files, like "directional"
        std::string Label; // in the Inspector, like "Directional"
    };

    // An entry of an AssetList in the Inspector
    struct AssetListItem
    {
        std::string Label;
        AssetID Empty; // what the entry uses while it is empty, shown in its place
    };

    struct FieldInfo
    {
        std::string Key;   // in files, like "innerCone"
        std::string Label; // in the Inspector, like "Inner Cone"
        FieldType Type = FieldType::Float;
        FieldValue Default;

        AssetType Asset;  // what an Asset or AssetList field points to
        std::vector<EnumOption> EnumOptions;

        // For the Inspector: what a number can be, and how fast it changes when dragged
        std::optional<std::pair<float, float>> Range;
        float Speed = 0.01f;

        // For a Vec3 the Inspector offers to change along all axes together, like a scale
        bool UniformToggle = false;

        // For an Asset or AssetList field: dropping an asset of its type in the Scene view makes an entity with this
        // component (SpawnsFrom), or sets it on the entity under the mouse (DropTarget, the first entry of a list)
        bool SpawnsFrom = false;
        bool DropTarget = false;

        std::function<FieldValue(const void* component)> Get;
        std::function<void(void* component, const FieldValue& value)> Set;

        // Whether it means anything with the component's other values, like a light's range with a directional one.
        // Always when not set.
        std::function<bool(const void* component)> Visible;

        // For an AssetList whose entries follow another of the component's assets, like a mesh's slots: one item per entry.
        // An edit sizes the list to them. When not set, each held id is an entry named "<Label> <index>".
        std::function<std::vector<AssetListItem>(const void* component, AssetManager& assets)> Items;
    };

    // A ready-made setup of a component, offered by the editor when creating an entity, like a light of one type
    struct ComponentPreset
    {
        std::string Menu; // the submenu, like "Light"
        std::string Name; // the entry, and what the entity is called
        std::function<void(void* component, const AssetManager& assets)> Apply;
    };

    // A kind of component, which scene files and the editor know by its fields. A C++ type registers with
    // ComponentRegistry::Register (component_builder.hpp), one that only exists as data, like a script's, with RegisterRuntime.
    struct ComponentType
    {
        std::string Id;   // in files, never to change once something was saved with it
        std::string Name; // in the editor
        std::vector<FieldInfo> Fields;

        bool Addable = true;       // through the editor's Add Component, which a transform isn't as every entity has one
        bool InlineInFile = false; // its fields are the node's own in scene files, like the transform's
        entt::id_type NativeType = 0; // of its C++ type, 0 for one that only exists as data

        std::function<bool(Entity)> Has;
        std::function<void*(Entity)> Get; // null if the entity doesn't have it
        std::function<void(Entity)> Add;  // with its fields' defaults
        std::function<void(Entity)> Remove;

        // Runs after its fields are set, to keep them consistent, like a spot light's inner cone inside its outer one
        std::function<void(void* component)> Constrain;

        // The Lucide name of its icon, which can depend on its values. Empty for none.
        std::function<std::string_view(const void* component)> Icon;

        std::vector<ComponentPreset> Presets;

        // Its index in Fields, if it has one with that key
        [[nodiscard]] std::optional<size_t> FindField(std::string_view key) const;
    };

    // The values of a component with no C++ type, in the order of its fields
    struct RuntimeComponent
    {
        std::vector<FieldValue> Values;
    };

    // A component as a scene file has it: the values of a registered one, in the order of its fields, or the text of one
    // that isn't registered, which is kept as it was for it not to be lost when the file is saved again
    struct SceneComponent
    {
        std::string Type;
        std::vector<FieldValue> Values;
        std::string Raw; // JSON, only for a type that isn't registered

        bool operator==(const SceneComponent&) const = default;
    };

    // What an entity had in its file that isn't registered, to be saved again with it
    struct UnknownComponents
    {
        std::vector<SceneComponent> Components;
    };

    template <typename T>
    class ComponentBuilder;

    // The kinds of components scene files and the editor know. The asset manager owns one, with the engine's own
    // components in it, as asset references in fields are checked against it.
    class ComponentRegistry
    {
    public:
        explicit ComponentRegistry(const AssetManager& assets) : m_assets(assets) {}

        ComponentRegistry(const ComponentRegistry&) = delete;
        ComponentRegistry& operator=(const ComponentRegistry&) = delete;

        // A C++ component, whose fields are added through the builder it gives. Defined in component_builder.hpp.
        template <typename T>
        ComponentBuilder<T> Register(std::string id, std::string name);

        // A component that only exists as data. Its values are kept by the scene, and the fields' Get and Set are made
        // here. A field's default of another alternative than its type holds becomes the empty one.
        // Null if the id is taken.
        const ComponentType* RegisterRuntime(std::string id, std::string name, std::vector<FieldInfo> fields);

        [[nodiscard]] const ComponentType* Find(std::string_view id) const;

        // The type registered for a C++ component
        template <typename T>
        [[nodiscard]] const ComponentType* Find() const
        {
            for (const ComponentType& type : m_types)
            {
                if (type.NativeType == entt::type_hash<T>::value())
                    return &type;
            }

            return nullptr;
        }

        // In the order they were registered, which is the order an entity's components are saved and shown in
        [[nodiscard]] const std::deque<ComponentType>& Types() const { return m_types; }

        // A field's value in a component read from a file, if its type is registered and has that field
        [[nodiscard]] const FieldValue* FieldOf(const SceneComponent& component, std::string_view key) const;

        // What a node of a scene file has for itself, which a component's id can't be: its id, name, active, instance
        // and children, and the fields of the components that are inline
        [[nodiscard]] bool IsNodeKey(std::string_view key) const;

        // A component as a scene file has it, with the fields' defaults except for the values given by field key.
        // A type or key that isn't registered is an error.
        [[nodiscard]] SceneComponent Compose(std::string_view id, std::initializer_list<std::pair<std::string_view, FieldValue>> overrides) const;
    private:
        ComponentType* Add(std::string id, std::string name);

        const AssetManager& m_assets;
        std::deque<ComponentType> m_types; // a deque, for the types to stay where they are as more are added
    };

    // The engine's own components: Transform, Light and Mesh Renderer
    void RegisterNativeComponents(ComponentRegistry& registry);

    // A component's values, in the order of its fields
    [[nodiscard]] std::vector<FieldValue> ReadFields(const ComponentType& type, const void* component);
    [[nodiscard]] std::vector<FieldValue> ReadComponent(const ComponentType& type, Entity entity);

    // Adds the component if the entity doesn't have it, and sets the values it is given in the order of its fields
    void WriteComponent(const ComponentType& type, Entity entity, const std::vector<FieldValue>& values);

    void CopyComponent(const ComponentType& type, Entity from, Entity to);

    // The component as a scene file saves it, with its values as they are
    [[nodiscard]] SceneComponent CaptureComponent(const ComponentType& type, Entity entity);

    // Adds the component the preset is of, and sets it up with the preset
    void ApplyPreset(const ComponentType& type, const ComponentPreset& preset, Entity entity, const AssetManager& assets);

    // A field of a type of component
    struct FieldRef
    {
        const ComponentType* Type = nullptr;
        size_t Field = 0;

        explicit operator bool() const { return Type != nullptr; }
    };

    // The first field that makes an entity from a dropped asset of this type, or none
    [[nodiscard]] FieldRef FindSpawnField(const ComponentRegistry& registry, AssetType asset);

    // The first field of a component the entity has that a dropped asset of this type is set on, or none
    [[nodiscard]] FieldRef FindDropField(const ComponentRegistry& registry, Entity entity, AssetType asset);

    // Sets an Asset field, or the first entry of an AssetList one, then constrains the component. A list with items is sized
    // to them first, and one without any has nowhere to put it.
    void AssignAsset(const ComponentType& type, size_t field, Entity entity, AssetID asset, AssetManager& assets);
}
