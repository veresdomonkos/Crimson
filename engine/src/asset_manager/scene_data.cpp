#include "crimson/asset_manager/scene_data.hpp"

#include <array>
#include <charconv>
#include <exception>

#include <nlohmann/json.hpp>

#include "crimson/asset_manager/asset_manager.hpp"
#include "crimson/core/log.hpp"

#include "asset_ref.hpp"

namespace crimson
{
    static constexpr int kSceneVersion = 1;

    bool SceneNode::operator==(const SceneNode&) const = default;

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

        glm::vec3 ReadVec3(const nlohmann::json& j, const char* key, const glm::vec3& fallback)
        {
            const auto it = j.find(key);
            if (it == j.end())
                return fallback;

            return glm::vec3(it->at(0).get<float>(), it->at(1).get<float>(), it->at(2).get<float>());
        }

        std::string_view LightTypeName(LightType type)
        {
            switch (type)
            {
                case LightType::Directional: return "directional";
                case LightType::Spot:        return "spot";
                default:                     return "point";
            }
        }

        LightType ParseLightType(const std::string& name, std::string_view file)
        {
            if (name == "directional") return LightType::Directional;
            if (name == "spot")        return LightType::Spot;
            if (name != "point")       LOG_WARN("[Scene] '{}': unknown light type '{}', using point", file, name);
            return LightType::Point;
        }

        // A missing asset is worth a warning only if the file named one
        AssetID ReadRef(const AssetManager& assets, const nlohmann::json& ref, std::string_view file)
        {
            const AssetID id = ResolveAssetRef(assets, ref, file);
            if (!id && !ref.is_null())
                LOG_WARN("[Scene] '{}': no asset for {}", file, ref.dump());

            return id;
        }

        SceneNode ParseNode(const nlohmann::json& j, const AssetManager& assets, std::string_view file)
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

            if (const auto light = j.find("light"); light != j.end())
            {
                const LightComponent defaults;

                node.Light = LightComponent{
                    .Type = ParseLightType(light->value("type", std::string("point")), file),
                    .Color = ReadVec3(*light, "color", defaults.Color),
                    .Intensity = light->value("intensity", defaults.Intensity),
                    .Range = light->value("range", defaults.Range),
                    .InnerConeDegrees = light->value("innerCone", defaults.InnerConeDegrees),
                    .OuterConeDegrees = light->value("outerCone", defaults.OuterConeDegrees),
                    .CastsShadow = light->value("castsShadow", defaults.CastsShadow)
                };
            }

            if (const auto renderer = j.find("meshRenderer"); renderer != j.end())
            {
                SceneMeshRenderer& mesh = node.MeshRenderer.emplace();
                mesh.CastsShadows = renderer->value("castsShadows", true);

                if (const auto ref = renderer->find("mesh"); ref != renderer->end())
                    mesh.Mesh = ReadRef(assets, *ref, file);

                if (const auto materials = renderer->find("materials"); materials != renderer->end())
                {
                    for (const nlohmann::json& material : *materials)
                        mesh.Materials.push_back(ReadRef(assets, material, file));
                }
            }

            if (const auto instance = j.find("instance"); instance != j.end())
                node.Instance = ReadRef(assets, *instance, file);

            if (const auto children = j.find("children"); children != j.end())
            {
                for (const nlohmann::json& child : *children)
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

            if (node.Light)
            {
                j["light"] = {
                    { "type", LightTypeName(node.Light->Type) },
                    { "color", WriteVec3(node.Light->Color) },
                    { "intensity", ShortNumber(node.Light->Intensity) },
                    { "range", ShortNumber(node.Light->Range) },
                    { "innerCone", ShortNumber(node.Light->InnerConeDegrees) },
                    { "outerCone", ShortNumber(node.Light->OuterConeDegrees) },
                    { "castsShadow", node.Light->CastsShadow }
                };
            }

            if (node.MeshRenderer)
            {
                nlohmann::ordered_json materials = nlohmann::ordered_json::array();

                for (const AssetID material : node.MeshRenderer->Materials)
                    materials.push_back(WriteAssetRef(assets, material));

                j["meshRenderer"] = {
                    { "mesh", WriteAssetRef(assets, node.MeshRenderer->Mesh) },
                    { "materials", std::move(materials) },
                    { "castsShadows", node.MeshRenderer->CastsShadows }
                };
            }

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

            if (const auto renderer = node.find("meshRenderer"); renderer != node.end() && renderer->is_object())
            {
                if (const auto mesh = renderer->find("mesh"); mesh != renderer->end())
                    updated = UpdateAssetRef(*mesh, moved, assets);

                if (const auto materials = renderer->find("materials"); materials != renderer->end() && materials->is_array())
                {
                    for (nlohmann::ordered_json& material : *materials)
                        updated = UpdateAssetRef(material, moved, assets) || updated;
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
            const nlohmann::json j = nlohmann::json::parse(text);

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
                for (const nlohmann::json& entity : *entities)
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
