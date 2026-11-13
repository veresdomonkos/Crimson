#pragma once
#include <cassert>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include "crimson/asset_manager/asset_manager.hpp"
#include "crimson/scene/component_registry.hpp"

namespace crimson
{
    // What a field has besides its type, all of it optional
    template <typename T>
    struct FieldOptions
    {
        // For a number: what it can be, and how fast it changes when dragged
        std::optional<std::pair<float, float>> Range;
        float Speed = 0.01f;

        // Whether it means anything with the component's other values, like a light's range with a directional one
        std::function<bool(const T&)> VisibleIf;

        // For an AssetList: the name of an entry, like a mesh's material slot
        std::function<std::string(const T&, size_t index)> ItemLabel;

        // For a Vec3: a "Uniform <Label>" row, which makes all axes change together
        bool UniformToggle = false;

        // For an Asset or AssetList: what an empty reference ends up as. It is saved as empty.
        std::function<AssetID(const AssetManager&)> Fallback;

        // For an Asset: dropping such an asset in the Scene view makes an entity with this component
        bool SpawnsFrom = false;

        // For an Asset or AssetList: dropping such an asset on an entity sets it, the first entry of a list
        bool DropTarget = false;
    };

    // Describes a C++ component to the registry, a call for each field. The member's type picks the field's kind, and a
    // member of a type no kind holds doesn't compile. The fields' Get, Set and Default come from the member.
    //
    //   registry.Register<LightComponent>("light", "Light")
    //       .Icon("sun")
    //       .Field("intensity", "Intensity", &LightComponent::Intensity, { .Range = std::pair(0.0f, 1000.0f) });
    template <typename T>
    class ComponentBuilder
    {
    public:
        using Options = FieldOptions<T>;

        ComponentBuilder(ComponentType& type, const AssetManager& assets) : m_type(type), m_assets(assets) {}

        ComponentBuilder& Field(std::string key, std::string label, bool T::* member, Options options = {})
        {
            return Plain(FieldType::Bool, std::move(key), std::move(label), member, std::move(options));
        }

        ComponentBuilder& Field(std::string key, std::string label, int32_t T::* member, Options options = {})
        {
            return Plain(FieldType::Int, std::move(key), std::move(label), member, std::move(options));
        }

        ComponentBuilder& Field(std::string key, std::string label, float T::* member, Options options = {})
        {
            return Plain(FieldType::Float, std::move(key), std::move(label), member, std::move(options));
        }

        ComponentBuilder& Field(std::string key, std::string label, glm::vec3 T::* member, Options options = {})
        {
            return Plain(FieldType::Vec3, std::move(key), std::move(label), member, std::move(options));
        }

        ComponentBuilder& Field(std::string key, std::string label, std::string T::* member, Options options = {})
        {
            return Plain(FieldType::String, std::move(key), std::move(label), member, std::move(options));
        }

        ComponentBuilder& Color(std::string key, std::string label, glm::vec3 T::* member, Options options = {})
        {
            return Plain(FieldType::Color, std::move(key), std::move(label), member, std::move(options));
        }

        // The values are in the order of the enum's
        template <typename E> requires std::is_enum_v<E>
        ComponentBuilder& Enum(std::string key, std::string label, E T::* member, std::vector<EnumOption> values, Options options = {})
        {
            FieldInfo info = Describe(FieldType::Enum, std::move(key), std::move(label), std::move(options));
            info.EnumOptions = std::move(values);
            info.Get = [member](const void* component) -> FieldValue { return static_cast<int32_t>(static_cast<const T*>(component)->*member); };
            info.Set = [member](void* component, const FieldValue& value) { static_cast<T*>(component)->*member = static_cast<E>(std::get<int32_t>(value)); };
            return Push(std::move(info));
        }

        template <AssetType A>
        ComponentBuilder& Asset(std::string key, std::string label, TypedAssetID<A> T::* member, Options options = {})
        {
            FieldInfo info = Describe(FieldType::Asset, std::move(key), std::move(label), std::move(options));
            info.Asset = A;
            info.Get = [member](const void* component) -> FieldValue { return AssetID(static_cast<const T*>(component)->*member); };
            info.Set = [member, assets = &m_assets](void* component, const FieldValue& value)
            {
                static_cast<T*>(component)->*member = assets->As<A>(std::get<AssetID>(value));
            };
            return Push(std::move(info));
        }

        template <AssetType A>
        ComponentBuilder& AssetList(std::string key, std::string label, std::vector<TypedAssetID<A>> T::* member, Options options = {})
        {
            FieldInfo info = Describe(FieldType::AssetList, std::move(key), std::move(label), std::move(options));
            info.Asset = A;
            info.Get = [member](const void* component) -> FieldValue
            {
                const auto& ids = static_cast<const T*>(component)->*member;
                return std::vector<AssetID>(ids.begin(), ids.end());
            };
            info.Set = [member, assets = &m_assets](void* component, const FieldValue& value)
            {
                auto& ids = static_cast<T*>(component)->*member;
                ids.clear();
                for (const AssetID id : std::get<std::vector<AssetID>>(value))
                    ids.push_back(assets->As<A>(id));
            };
            return Push(std::move(info));
        }

        // Its fields are the node's own in scene files, and it can't be added or removed, like the transform
        ComponentBuilder& PartOfNode()
        {
            m_type.InlineInFile = true;
            m_type.Addable = false;
            return *this;
        }

        // The name of a Lucide icon, one for all of them or by the component's values
        ComponentBuilder& Icon(std::string lucideName)
        {
            m_type.Icon = [lucideName = std::move(lucideName)](const void*) -> std::string_view { return lucideName; };
            return *this;
        }

        ComponentBuilder& Icon(std::function<std::string_view(const T&)> lucideName)
        {
            m_type.Icon = [lucideName = std::move(lucideName)](const void* component) { return lucideName(*static_cast<const T*>(component)); };
            return *this;
        }

        // Runs after its fields are set, to keep them consistent, like a spot light's inner cone inside its outer one
        ComponentBuilder& Constrain(std::function<void(T&)> constrain)
        {
            m_type.Constrain = [constrain = std::move(constrain)](void* component) { constrain(*static_cast<T*>(component)); };
            return *this;
        }

        // Loads what its asset fields refer to, after they changed or it was made, like a mesh renderer's mesh
        ComponentBuilder& ResolveAssets(std::function<void(T&, AssetManager&)> resolve)
        {
            m_type.ResolveAssets = [resolve = std::move(resolve)](void* component, AssetManager& assets) { resolve(*static_cast<T*>(component), assets); };
            return *this;
        }

        // A way to set it up that the editor offers when creating an entity, in the submenu of the same menu name
        ComponentBuilder& Preset(std::string menu, std::string name, std::function<void(T&, const AssetManager&)> apply)
        {
            m_type.Presets.push_back(ComponentPreset{
                .Menu = std::move(menu),
                .Name = std::move(name),
                .Apply = [apply = std::move(apply)](void* component, const AssetManager& assets) { apply(*static_cast<T*>(component), assets); } });
            return *this;
        }
    private:
        template <typename M>
        ComponentBuilder& Plain(FieldType type, std::string key, std::string label, M T::* member, Options options)
        {
            FieldInfo info = Describe(type, std::move(key), std::move(label), std::move(options));
            info.Get = [member](const void* component) -> FieldValue { return static_cast<const T*>(component)->*member; };
            info.Set = [member](void* component, const FieldValue& value) { static_cast<T*>(component)->*member = std::get<M>(value); };
            return Push(std::move(info));
        }

        FieldInfo Describe(FieldType type, std::string key, std::string label, Options options) const
        {
            FieldInfo info{
                .Key = std::move(key),
                .Label = std::move(label),
                .Type = type,
                .Range = options.Range,
                .Speed = options.Speed,
                .UniformToggle = options.UniformToggle,
                .SpawnsFrom = options.SpawnsFrom,
                .DropTarget = options.DropTarget,
                .Fallback = std::move(options.Fallback) };

            if (options.VisibleIf)
                info.Visible = [visible = std::move(options.VisibleIf)](const void* component) { return visible(*static_cast<const T*>(component)); };

            if (options.ItemLabel)
                info.ItemLabel = [itemLabel = std::move(options.ItemLabel)](const void* component, size_t index) { return itemLabel(*static_cast<const T*>(component), index); };

            return info;
        }

        ComponentBuilder& Push(FieldInfo info)
        {
            const T defaults{};
            info.Default = info.Get(&defaults);

            m_type.Fields.push_back(std::move(info));
            return *this;
        }

        ComponentType& m_type;
        const AssetManager& m_assets;
    };

    template <typename T>
    ComponentBuilder<T> ComponentRegistry::Register(std::string id, std::string name)
    {
        ComponentType* type = Add(std::move(id), std::move(name));
        assert(type && "A component's id is already taken");

        type->NativeType = entt::type_hash<T>::value();
        type->Has = [](Entity entity) { return entity.HasComponent<T>(); };
        type->Get = [](Entity entity) -> void* { return entity.TryGetComponent<T>(); };
        type->Add = [](Entity entity) { entity.AddComponent<T>(); };
        type->Remove = [](Entity entity) { entity.RemoveComponent<T>(); };

        return ComponentBuilder<T>(*type, m_assets);
    }
}
