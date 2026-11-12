#include "crimson/scene/component_registry.hpp"

#include <algorithm>

#include <entt/core/hashed_string.hpp>

#include "crimson/core/log.hpp"

namespace crimson
{
    FieldValue EmptyValue(FieldType type)
    {
        switch (type)
        {
            case FieldType::Bool:      return false;
            case FieldType::Int:
            case FieldType::Enum:      return int32_t{0};
            case FieldType::Float:     return 0.0f;
            case FieldType::Vec3:
            case FieldType::Color:     return glm::vec3(0.0f);
            case FieldType::String:    return std::string();
            case FieldType::Asset:     return AssetID{};
            case FieldType::AssetList: return std::vector<AssetID>();
        }

        return false;
    }

    std::optional<size_t> ComponentType::FindField(std::string_view key) const
    {
        const auto found = std::ranges::find(Fields, key, &FieldInfo::Key);
        return found != Fields.end() ? std::optional(static_cast<size_t>(found - Fields.begin())) : std::nullopt;
    }

    ComponentType* ComponentRegistry::Add(std::string id, std::string name)
    {
        if (Find(id))
        {
            LOG_ERROR("[Components] There already is a component with the id '{}'", id);
            return nullptr;
        }

        ComponentType& type = m_types.emplace_back();
        type.Id = std::move(id);
        type.Name = std::move(name);
        return &type;
    }

    const ComponentType* ComponentRegistry::RegisterRuntime(std::string id, std::string name, std::vector<FieldInfo> fields)
    {
        ComponentType* type = Add(std::move(id), std::move(name));
        if (!type)
            return nullptr;

        std::vector<FieldValue> defaults;

        for (size_t index = 0; index < fields.size(); ++index)
        {
            FieldInfo& field = fields[index];

            // Set would otherwise give the scene a value of another type than the field's
            if (field.Default.index() != EmptyValue(field.Type).index())
                field.Default = EmptyValue(field.Type);

            field.Get = [index](const void* component) { return static_cast<const RuntimeComponent*>(component)->Values[index]; };
            field.Set = [index](void* component, const FieldValue& value) { static_cast<RuntimeComponent*>(component)->Values[index] = value; };
            defaults.push_back(field.Default);
        }

        type->Fields = std::move(fields);

        // Each runtime type has its own pool, named after its id
        const entt::id_type pool = entt::hashed_string::value(type->Id.c_str());
        const auto storage = [pool](Entity entity) -> auto& { return entity.GetScene()->GetRegistry().storage<RuntimeComponent>(pool); };

        type->Has = [storage](Entity entity) { return storage(entity).contains(entity.GetHandle()); };
        type->Get = [storage](Entity entity) -> void*
        {
            auto& values = storage(entity);
            return values.contains(entity.GetHandle()) ? &values.get(entity.GetHandle()) : nullptr;
        };
        type->Add = [storage, defaults](Entity entity) { storage(entity).emplace(entity.GetHandle(), RuntimeComponent{ defaults }); };
        type->Remove = [storage](Entity entity) { storage(entity).remove(entity.GetHandle()); };

        return type;
    }

    const ComponentType* ComponentRegistry::Find(std::string_view id) const
    {
        const auto found = std::ranges::find(m_types, id, &ComponentType::Id);
        return found != m_types.end() ? &*found : nullptr;
    }

    const FieldValue* ComponentRegistry::FieldOf(const SceneComponent& component, std::string_view key) const
    {
        const ComponentType* type = Find(component.Type);
        if (!type)
            return nullptr;

        const std::optional<size_t> index = type->FindField(key);
        return index && *index < component.Values.size() ? &component.Values[*index] : nullptr;
    }

    std::vector<FieldValue> ReadFields(const ComponentType& type, const void* component)
    {
        std::vector<FieldValue> values;
        values.reserve(type.Fields.size());

        for (const FieldInfo& field : type.Fields)
            values.push_back(field.Get(component));

        return values;
    }

    std::vector<FieldValue> ReadComponent(const ComponentType& type, Entity entity)
    {
        const void* component = type.Get(entity);
        return component ? ReadFields(type, component) : std::vector<FieldValue>();
    }

    void WriteComponent(const ComponentType& type, Entity entity, const std::vector<FieldValue>& values)
    {
        if (!type.Has(entity))
            type.Add(entity);

        void* component = type.Get(entity);

        for (size_t index = 0; index < std::min(values.size(), type.Fields.size()); ++index)
        {
            // A value of another type, like from a file that was edited by hand, keeps the field as it was
            const FieldInfo& field = type.Fields[index];
            if (values[index].index() == field.Default.index())
                field.Set(component, values[index]);
        }

        if (type.Constrain)
            type.Constrain(component);
    }

    void CopyComponent(const ComponentType& type, Entity from, Entity to)
    {
        if (type.Has(from))
            WriteComponent(type, to, ReadComponent(type, from));
    }
}
