#pragma once
#include <cassert>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

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

    struct FieldInfo
    {
        std::string Key;   // in files, like "innerCone"
        std::string Label; // in the Inspector, like "Inner Cone"
        FieldType Type = FieldType::Float;
        FieldValue Default;

        AssetType Asset = AssetType::None;  // what an Asset or AssetList field points to
        std::vector<std::string> EnumKeys;   // in files, by value
        std::vector<std::string> EnumLabels; // in the Inspector, the same order

        // For the Inspector: what a number can be, and how fast it changes when dragged
        std::optional<std::pair<float, float>> Range;
        float Speed = 0.01f;

        std::function<FieldValue(const void* component)> Get;
        std::function<void(void* component, const FieldValue& value)> Set;

        // Whether it means anything with the component's other values, like a light's range with a directional one.
        // Always when not set.
        std::function<bool(const void* component)> Visible;
    };

    // A kind of component, which scene files and the editor know by its fields. A C++ type registers with
    // ComponentRegistry::RegisterNative, one that only exists as data, like a script's, with RegisterRuntime.
    struct ComponentType
    {
        std::string Id;   // in files, never to change once something was saved with it
        std::string Name; // in the editor
        std::vector<FieldInfo> Fields;

        bool Addable = true;       // through the editor's Add Component, which a transform isn't as every entity has one
        bool InlineInFile = false; // its fields are the node's own in scene files, like the transform's

        std::function<bool(Entity)> Has;
        std::function<void*(Entity)> Get; // null if the entity doesn't have it
        std::function<void(Entity)> Add;  // with its fields' defaults
        std::function<void(Entity)> Remove;

        // Runs after its fields are set, to keep them consistent, like a spot light's inner cone inside its outer one
        std::function<void(void* component)> Constrain;

        // After it was created from a scene file or the editor, like a mesh renderer loading its assets
        std::function<void(Entity entity, AssetManager& assets)> OnLoaded;

        // Before its values are saved, like a mesh renderer leaving a slot empty that only has the default material
        std::function<void(std::vector<FieldValue>& values, const AssetManager& assets)> OnCaptured;

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

    class ComponentRegistry;

    // Adds the fields of a C++ component, read and written through pointers to its members
    template <typename T>
    class NativeComponentBuilder
    {
    public:
        NativeComponentBuilder(ComponentType& type, const AssetManager& assets) : m_type(type), m_assets(assets) {}

        // The field's Get, Set and Default come from the member. An enum member is an Enum field, a TypedAssetID an
        // Asset one, and a vector of them an AssetList.
        template <auto Member>
        NativeComponentBuilder& Field(FieldInfo info);

        NativeComponentBuilder& InlineInFile() { m_type.InlineInFile = true; m_type.Addable = false; return *this; }
        NativeComponentBuilder& Constrain(std::function<void(T&)> constrain);
        NativeComponentBuilder& OnLoaded(std::function<void(Entity, AssetManager&)> onLoaded) { m_type.OnLoaded = std::move(onLoaded); return *this; }
        NativeComponentBuilder& OnCaptured(std::function<void(std::vector<FieldValue>&, const AssetManager&)> onCaptured) { m_type.OnCaptured = std::move(onCaptured); return *this; }
    private:
        ComponentType& m_type;
        const AssetManager& m_assets;
    };

    // The kinds of components scene files and the editor know. The asset manager owns one, with the engine's own
    // components in it, as asset references in fields are checked against it.
    class ComponentRegistry
    {
    public:
        explicit ComponentRegistry(const AssetManager& assets) : m_assets(assets) {}

        ComponentRegistry(const ComponentRegistry&) = delete;
        ComponentRegistry& operator=(const ComponentRegistry&) = delete;

        template <typename T>
        NativeComponentBuilder<T> RegisterNative(std::string id, std::string name);

        // A component that only exists as data. Its values are kept by the scene, and the fields' Get and Set are made
        // here. A field's default of another alternative than its type holds becomes the empty one.
        // Null if the id is taken.
        const ComponentType* RegisterRuntime(std::string id, std::string name, std::vector<FieldInfo> fields);

        [[nodiscard]] const ComponentType* Find(std::string_view id) const;

        // In the order they were registered, which is the order an entity's components are saved and shown in
        [[nodiscard]] const std::deque<ComponentType>& Types() const { return m_types; }

        // A field's value in a component read from a file, if its type is registered and has that field
        [[nodiscard]] const FieldValue* FieldOf(const SceneComponent& component, std::string_view key) const;
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

    // ---

    namespace detail
    {
        template <typename T>
        struct IsTypedAssetID : std::false_type {};

        template <AssetType A>
        struct IsTypedAssetID<TypedAssetID<A>> : std::true_type {};

        template <typename T>
        struct IsTypedAssetList : std::false_type {};

        template <AssetType A>
        struct IsTypedAssetList<std::vector<TypedAssetID<A>>> : std::true_type {};

        template <typename Class, typename Member>
        Member MemberOf(Member Class::*);

        // Templated on the asset manager too, so it is only looked into where a field is registered, which includes it
        template <AssetType Type, typename Assets>
        TypedAssetID<Type> As(const Assets& assets, AssetID id)
        {
            return assets.template As<Type>(id);
        }
    }

    template <typename T>
    template <auto Member>
    NativeComponentBuilder<T>& NativeComponentBuilder<T>::Field(FieldInfo info)
    {
        using M = decltype(detail::MemberOf(Member));
        const AssetManager* assets = &m_assets;

        info.Get = [](const void* component) -> FieldValue
        {
            const M& value = static_cast<const T*>(component)->*Member;

            if constexpr (std::is_enum_v<M>)
                return static_cast<int32_t>(value);
            else if constexpr (detail::IsTypedAssetID<M>::value)
                return AssetID(value);
            else if constexpr (detail::IsTypedAssetList<M>::value)
                return std::vector<AssetID>(value.begin(), value.end());
            else
                return value;
        };

        info.Set = [assets](void* component, const FieldValue& value)
        {
            M& member = static_cast<T*>(component)->*Member;

            if constexpr (std::is_enum_v<M>)
            {
                member = static_cast<M>(std::get<int32_t>(value));
            }
            else if constexpr (detail::IsTypedAssetID<M>::value)
            {
                member = detail::As<M::Type>(*assets, std::get<AssetID>(value));
            }
            else if constexpr (detail::IsTypedAssetList<M>::value)
            {
                using Element = typename M::value_type;
                member.clear();
                for (const AssetID id : std::get<std::vector<AssetID>>(value))
                    member.push_back(detail::As<Element::Type>(*assets, id));
            }
            else
            {
                member = std::get<M>(value);
            }
        };

        if constexpr (detail::IsTypedAssetID<M>::value)
            info.Asset = M::Type;
        else if constexpr (detail::IsTypedAssetList<M>::value)
            info.Asset = M::value_type::Type;

        const T defaults{};
        info.Default = info.Get(&defaults);

        m_type.Fields.push_back(std::move(info));
        return *this;
    }

    template <typename T>
    NativeComponentBuilder<T>& NativeComponentBuilder<T>::Constrain(std::function<void(T&)> constrain)
    {
        m_type.Constrain = [constrain = std::move(constrain)](void* component) { constrain(*static_cast<T*>(component)); };
        return *this;
    }

    template <typename T>
    NativeComponentBuilder<T> ComponentRegistry::RegisterNative(std::string id, std::string name)
    {
        ComponentType* type = Add(std::move(id), std::move(name));
        assert(type && "A component's id is already taken");

        type->Has = [](Entity entity) { return entity.HasComponent<T>(); };
        type->Get = [](Entity entity) -> void* { return entity.TryGetComponent<T>(); };
        type->Add = [](Entity entity) { entity.AddComponent<T>(); };
        type->Remove = [](Entity entity) { entity.RemoveComponent<T>(); };

        return NativeComponentBuilder<T>(*type, m_assets);
    }
}
