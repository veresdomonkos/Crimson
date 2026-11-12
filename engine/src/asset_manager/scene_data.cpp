#include "crimson/asset_manager/scene_data.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <exception>
#include <format>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "crimson/asset_manager/asset_manager.hpp"
#include "crimson/core/log.hpp"

#include "asset_ref.hpp"

namespace crimson
{
    static constexpr int kSceneVersion = 1;

    bool SceneNode::operator==(const SceneNode&) const = default;

    const SceneComponent* SceneNode::FindComponent(std::string_view type) const
    {
        const auto found = std::ranges::find(Components, type, &SceneComponent::Type);
        return found != Components.end() ? &*found : nullptr;
    }

    namespace
    {
        // A float's exact value as a double has many digits, so this goes for the shortest that reads back the same
        double ShortNumber(float value)
        {
            std::array<char, 32> buffer{};
            const auto written = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);

            double shortest = value;
            std::from_chars(buffer.data(), written.ptr, shortest);
            return shortest;
        }

        // Like a dump with two spaces, except for lists of numbers staying on one line, for vectors to read as vectors
        std::string DumpScene(const nlohmann::ordered_json& j)
        {
            const std::string text = j.dump(2);

            std::string result;
            result.reserve(text.size());

            size_t position = 0;
            while (position < text.size())
            {
                const size_t open = text.find("[\n", position);
                if (open == std::string::npos)
                    break;

                const size_t end = text.find_first_not_of("0123456789.+-eE, \n", open + 1);
                result.append(text, position, open + 1 - position);
                position = open + 1;

                if (end == std::string::npos || text[end] != ']')
                    continue;

                for (size_t i = open + 1; i < end; ++i)
                {
                    if (text[i] == ',')
                        result += ", ";
                    else if (text[i] != ' ' && text[i] != '\n')
                        result += text[i];
                }

                result += ']';
                position = end + 1;
            }

            result.append(text, position);
            return result + "\n";
        }

        nlohmann::ordered_json WriteVec3(const glm::vec3& value)
        {
            return { ShortNumber(value.x), ShortNumber(value.y), ShortNumber(value.z) };
        }

        glm::vec3 ReadVec3(const nlohmann::ordered_json& j)
        {
            return glm::vec3(j.at(0).get<float>(), j.at(1).get<float>(), j.at(2).get<float>());
        }

        glm::vec3 ReadVec3(const nlohmann::ordered_json& j, const char* key, const glm::vec3& fallback)
        {
            const auto it = j.find(key);
            return it != j.end() ? ReadVec3(*it) : fallback;
        }

        // A missing asset is worth a warning only if the file named one
        template <AssetType T>
        AssetID ReadRef(const AssetManager& assets, const nlohmann::ordered_json& ref, std::string_view file)
        {
            const TypedAssetID<T> id = ResolveAssetRef<T>(assets, ref, file);
            if (!id && !ref.is_null())
                LOG_WARN("[Scene] '{}': no asset for {}", file, ref.dump());

            return id;
        }

        AssetID ReadRef(const AssetManager& assets, const nlohmann::ordered_json& ref, AssetType type, std::string_view file)
        {
            switch (type)
            {
                case AssetType::Texture:  return ReadRef<AssetType::Texture>(assets, ref, file);
                case AssetType::Mesh:     return ReadRef<AssetType::Mesh>(assets, ref, file);
                case AssetType::Shader:   return ReadRef<AssetType::Shader>(assets, ref, file);
                case AssetType::Material: return ReadRef<AssetType::Material>(assets, ref, file);
                case AssetType::Scene:    return ReadRef<AssetType::Scene>(assets, ref, file);
                case AssetType::None:     break;
            }

            return AssetID{};
        }

        // A wrongly typed value throws, which makes the whole file fail to read, as for the node's own fields
        FieldValue ReadValue(const FieldInfo& field, const nlohmann::ordered_json& j, const AssetManager& assets, std::string_view file)
        {
            switch (field.Type)
            {
                case FieldType::Bool:   return j.get<bool>();
                case FieldType::Int:    return j.get<int32_t>();
                case FieldType::Float:  return j.get<float>();
                case FieldType::Vec3:
                case FieldType::Color:  return ReadVec3(j);
                case FieldType::String: return j.get<std::string>();
                case FieldType::Asset:  return ReadRef(assets, j, field.Asset, file);
                case FieldType::Enum:
                {
                    const std::string key = j.get<std::string>();
                    const auto found = std::ranges::find(field.EnumKeys, key);
                    if (found != field.EnumKeys.end())
                        return static_cast<int32_t>(found - field.EnumKeys.begin());

                    LOG_WARN("[Scene] '{}': unknown {} '{}', using the default", file, field.Key, key);
                    return field.Default;
                }
                case FieldType::AssetList:
                {
                    std::vector<AssetID> ids;
                    for (const nlohmann::ordered_json& ref : j)
                        ids.push_back(ReadRef(assets, ref, field.Asset, file));
                    return ids;
                }
            }

            return field.Default;
        }

        nlohmann::ordered_json WriteValue(const FieldInfo& field, const FieldValue& value, const AssetManager& assets)
        {
            switch (field.Type)
            {
                case FieldType::Bool:   return std::get<bool>(value);
                case FieldType::Int:    return std::get<int32_t>(value);
                case FieldType::Float:  return ShortNumber(std::get<float>(value));
                case FieldType::Vec3:
                case FieldType::Color:  return WriteVec3(std::get<glm::vec3>(value));
                case FieldType::String: return std::get<std::string>(value);
                case FieldType::Asset:  return WriteAssetRef(assets, std::get<AssetID>(value));
                case FieldType::Enum:
                {
                    const auto index = static_cast<size_t>(std::get<int32_t>(value));
                    return index < field.EnumKeys.size() ? field.EnumKeys[index] : field.EnumKeys.at(static_cast<size_t>(std::get<int32_t>(field.Default)));
                }
                case FieldType::AssetList:
                {
                    nlohmann::ordered_json refs = nlohmann::ordered_json::array();
                    for (const AssetID id : std::get<std::vector<AssetID>>(value))
                        refs.push_back(WriteAssetRef(assets, id));
                    return refs;
                }
            }

            return nullptr;
        }

        // What a node has besides its components
        bool IsNodeKey(std::string_view key)
        {
            constexpr std::array kNodeKeys = { "id", "name", "active", "position", "rotation", "scale", "instance", "children" };
            return std::ranges::find(kNodeKeys, key) != kNodeKeys.end();
        }

        // A registered type whose fields aren't the node's own
        const ComponentType* FindComponentType(const AssetManager& assets, std::string_view id)
        {
            const ComponentType* type = assets.Components().Find(id);
            return type && !type->InlineInFile ? type : nullptr;
        }

        SceneComponent ReadSceneComponent(const std::string& id, const nlohmann::ordered_json& j, const AssetManager& assets, std::string_view file)
        {
            const ComponentType* type = FindComponentType(assets, id);
            if (!type)
                return SceneComponent{ .Type = id, .Raw = j.dump() };

            if (!j.is_object())
                throw std::runtime_error(std::format("'{}' isn't an object", id));

            SceneComponent component{ .Type = id };

            for (const FieldInfo& field : type->Fields)
            {
                const auto value = j.find(field.Key);
                component.Values.push_back(value != j.end() ? ReadValue(field, *value, assets, file) : field.Default);
            }

            return component;
        }

        nlohmann::ordered_json WriteSceneComponent(const SceneComponent& component, const AssetManager& assets)
        {
            if (!component.Raw.empty())
                return nlohmann::ordered_json::parse(component.Raw);

            nlohmann::ordered_json j = nlohmann::ordered_json::object();

            const ComponentType* type = FindComponentType(assets, component.Type);
            if (!type)
                return j;

            for (size_t index = 0; index < std::min(type->Fields.size(), component.Values.size()); ++index)
                j[type->Fields[index].Key] = WriteValue(type->Fields[index], component.Values[index], assets);

            return j;
        }

        SceneNode ParseNode(const nlohmann::ordered_json& j, const AssetManager& assets, std::string_view file)
        {
            SceneNode node;

            const auto id = j.find("id");
            node.Id = id != j.end() ? AssetID::FromString(id->get<std::string>()) : AssetID{};
            if (!node.Id)
                node.Id = AssetID::Generate();

            node.Name = j.value("name", std::string{});
            node.Active = j.value("active", true);
            node.Position = ReadVec3(j, "position", glm::vec3(0.0f));
            node.RotationDegrees = ReadVec3(j, "rotation", glm::vec3(0.0f));
            node.Scale = ReadVec3(j, "scale", glm::vec3(1.0f));

            // Everything else is a component, by its type's id
            for (const auto& [key, value] : j.items())
            {
                if (!IsNodeKey(key))
                    node.Components.push_back(ReadSceneComponent(key, value, assets, file));
            }

            if (const auto instance = j.find("instance"); instance != j.end())
                node.Instance = assets.As<AssetType::Scene>(ReadRef<AssetType::Scene>(assets, *instance, file));

            if (const auto children = j.find("children"); children != j.end())
            {
                for (const nlohmann::ordered_json& child : *children)
                    node.Children.push_back(ParseNode(child, assets, file));
            }

            return node;
        }

        nlohmann::ordered_json WriteNode(const SceneNode& node, const AssetManager& assets)
        {
            nlohmann::ordered_json j;

            j["id"] = node.Id.ToString();
            j["name"] = node.Name;
            j["active"] = node.Active;
            j["position"] = WriteVec3(node.Position);
            j["rotation"] = WriteVec3(node.RotationDegrees);
            j["scale"] = WriteVec3(node.Scale);

            for (const SceneComponent& component : node.Components)
                j[component.Type] = WriteSceneComponent(component, assets);

            if (node.Instance)
                j["instance"] = WriteAssetRef(assets, node.Instance);

            if (!node.Children.empty())
            {
                nlohmann::ordered_json children = nlohmann::ordered_json::array();

                for (const SceneNode& child : node.Children)
                    children.push_back(WriteNode(child, assets));

                j["children"] = std::move(children);
            }

            return j;
        }

        bool UpdateNodeReferences(nlohmann::ordered_json& node, const std::unordered_map<std::string, AssetID>& moved, const AssetManager& assets)
        {
            if (!node.is_object())
                return false;

            bool updated = false;

            // The asset fields of the components that are registered. Those of the others can't be told from other values.
            for (auto& [key, component] : node.items())
            {
                const ComponentType* type = IsNodeKey(key) ? nullptr : FindComponentType(assets, key);
                if (!type || !component.is_object())
                    continue;

                for (const FieldInfo& field : type->Fields)
                {
                    const auto value = component.find(field.Key);
                    if (value == component.end())
                        continue;

                    if (field.Type == FieldType::Asset)
                    {
                        updated = UpdateAssetRef(*value, moved, assets) || updated;
                    }
                    else if (field.Type == FieldType::AssetList && value->is_array())
                    {
                        for (nlohmann::ordered_json& ref : *value)
                            updated = UpdateAssetRef(ref, moved, assets) || updated;
                    }
                }
            }

            if (const auto instance = node.find("instance"); instance != node.end())
                updated = UpdateAssetRef(*instance, moved, assets) || updated;

            if (const auto children = node.find("children"); children != node.end() && children->is_array())
            {
                for (nlohmann::ordered_json& child : *children)
                    updated = UpdateNodeReferences(child, moved, assets) || updated;
            }

            return updated;
        }
    }

    std::optional<SceneData> ParseSceneData(std::string_view text, const AssetManager& assets, std::string_view name)
    {
        try
        {
            const nlohmann::ordered_json j = nlohmann::ordered_json::parse(text);

            const int version = j.value("version", kSceneVersion);
            if (version < 1 || version > kSceneVersion)
            {
                LOG_ERROR("[Scene] '{}' has version {}, and this editor reads up to {}", name, version, kSceneVersion);
                return std::nullopt;
            }

            SceneData data;

            if (const auto environment = j.find("environment"); environment != j.end())
            {
                data.Environment.Ambient = ReadVec3(*environment, "ambient", data.Environment.Ambient);
                data.Environment.Exposure = environment->value("exposure", data.Environment.Exposure);
                data.Environment.Mapper = environment->value("toneMapper", std::string("aces")) == "none" ? ToneMapper::None : ToneMapper::Aces;
            }

            if (const auto entities = j.find("entities"); entities != j.end())
            {
                for (const nlohmann::ordered_json& entity : *entities)
                    data.Roots.push_back(ParseNode(entity, assets, name));
            }

            return data;
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("[Scene] Invalid scene file '{}': {}", name, e.what());
            return std::nullopt;
        }
    }

    std::string WriteSceneData(const SceneData& data, const AssetManager& assets)
    {
        nlohmann::ordered_json j;

        j["version"] = kSceneVersion;
        j["environment"] = {
            { "ambient", WriteVec3(data.Environment.Ambient) },
            { "exposure", ShortNumber(data.Environment.Exposure) },
            { "toneMapper", data.Environment.Mapper == ToneMapper::None ? "none" : "aces" }
        };

        nlohmann::ordered_json entities = nlohmann::ordered_json::array();

        for (const SceneNode& root : data.Roots)
            entities.push_back(WriteNode(root, assets));

        j["entities"] = std::move(entities);

        return DumpScene(j);
    }

    std::optional<std::string> UpdateSceneReferences(std::string_view text, const std::unordered_map<std::string, AssetID>& moved, const AssetManager& assets)
    {
        nlohmann::ordered_json j = nlohmann::ordered_json::parse(text, nullptr, false);
        if (!j.is_object())
            return std::nullopt;

        bool updated = false;

        if (const auto entities = j.find("entities"); entities != j.end() && entities->is_array())
        {
            for (nlohmann::ordered_json& entity : *entities)
                updated = UpdateNodeReferences(entity, moved, assets) || updated;
        }

        return updated ? std::optional(DumpScene(j)) : std::nullopt;
    }
}
