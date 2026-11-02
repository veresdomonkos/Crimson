#include "crimson/asset_manager/material_data.hpp"

#include <exception>

#include <nlohmann/json.hpp>

#include "crimson/asset_manager/asset_manager.hpp"
#include "crimson/core/log.hpp"

namespace crimson
{
    namespace
    {
        // The asset a reference's guid names, if it still exists
        template <typename Json>
        AssetID ResolveGuid(const AssetManager& assets, const Json& ref)
        {
            if (ref.contains("guid") && ref["guid"].is_string())
            {
                try
                {
                    const AssetID id = AssetID::FromString(ref["guid"].template get<std::string>());
                    if (id && assets.GetInfo(id))
                        return id;
                }
                catch (const std::exception&) {}
            }

            return {};
        }

        AssetID ResolveRef(const AssetManager& assets, const nlohmann::json& ref, std::string_view context)
        {
            if (ref.is_string())
                return assets.FindId(ref.get<std::string>());

            if (!ref.is_object())
                return {};

            if (const AssetID id = ResolveGuid(assets, ref))
                return id;

            if (ref.contains("path") && ref["path"].is_string())
            {
                const std::string path = ref["path"].get<std::string>();
                if (const AssetID byPath = assets.FindId(path))
                {
                    LOG_WARN("[MaterialImporter] {}: stale guid, resolved by path '{}'", context, path);
                    return byPath;
                }
            }

            return {};
        }

        nlohmann::json WriteRef(const AssetManager& assets, AssetID id)
        {
            const AssetInfo* info = assets.GetInfo(id);
            return info ? nlohmann::json{ { "guid", id.ToString() }, { "path", info->Path } } : nlohmann::json{};
        }

        bool UpdateRef(nlohmann::ordered_json& ref, const std::unordered_map<std::string, AssetID>& moved, const AssetManager& assets)
        {
            const bool hasPath = ref.is_object() && ref.contains("path") && ref["path"].is_string();
            if (!ref.is_string() && !hasPath)
                return false;

            const auto it = moved.find(hasPath ? ref["path"].get<std::string>() : ref.get<std::string>());
            if (it == moved.end())
                return false;

            // The id wins over the path when reading, so this one was about something else that used to be there
            if (hasPath)
            {
                if (const AssetID id = ResolveGuid(assets, ref); id && id != it->second)
                    return false;
            }

            ref = WriteRef(assets, it->second);
            return true;
        }

        MaterialSettings ParseSettings(const nlohmann::json& j, std::string_view file)
        {
            MaterialSettings settings;

            if (const auto it = j.find("renderMode"); it != j.end())
            {
                const std::string value = it->is_string() ? it->get<std::string>() : std::string{};

                if (value == "opaque")
                    settings.RenderMode = MaterialRenderMode::Opaque;
                else if (value == "transparent")
                    settings.RenderMode = MaterialRenderMode::Transparent;
                else
                    LOG_WARN("[MaterialImporter] '{}': unknown renderMode, using opaque", file);
            }

            if (const auto it = j.find("cull"); it != j.end())
            {
                const std::string value = it->is_string() ? it->get<std::string>() : std::string{};

                if (value == "back")
                    settings.Cull = CullMode::Back;
                else if (value == "front")
                    settings.Cull = CullMode::Front;
                else if (value == "none")
                    settings.Cull = CullMode::None;
                else
                    LOG_WARN("[MaterialImporter] '{}': unknown cull mode, using back", file);
            }

            return settings;
        }

        std::vector<float> ParseProperty(const std::string& name, const nlohmann::json& value)
        {
            try
            {
                if (value.is_number())
                    return { value.get<float>() };

                if (value.is_array())
                {
                    std::vector<float> values = value.get<std::vector<float>>();

                    if (!values.empty() && values.size() <= 4)
                        return values;

                    LOG_WARN("[MaterialImporter] Property '{}': unsupported array size {}", name, values.size());
                    return {};
                }

                LOG_WARN("[MaterialImporter] Property '{}': unsupported value type", name);
            }
            catch (const nlohmann::json::exception& e)
            {
                LOG_WARN("[MaterialImporter] Property '{}': {}", name, e.what());
            }

            return {};
        }
    }

    std::optional<MaterialData> ParseMaterialData(std::string_view text, const AssetManager& assets, std::string_view name)
    {
        nlohmann::json j;

        try
        {
            j = nlohmann::json::parse(text);
        }
        catch (const nlohmann::json::exception& e)
        {
            LOG_ERROR("[MaterialImporter] Invalid JSON in '{}': {}", name, e.what());
            return std::nullopt;
        }

        if (!j.is_object())
        {
            LOG_ERROR("[MaterialImporter] '{}': expected an object", name);
            return std::nullopt;
        }

        MaterialData data;
        data.Settings = ParseSettings(j, name);

        if (const auto shader = j.find("shader"); shader != j.end())
            data.Shader = ResolveRef(assets, *shader, name);
        else
            LOG_ERROR("[MaterialImporter] '{}': missing 'shader'", name);

        if (const auto properties = j.find("properties"); properties != j.end() && properties->is_object())
        {
            for (const auto& [property, value] : properties->items())
            {
                if (std::vector<float> values = ParseProperty(property, value); !values.empty())
                    data.Properties[property] = std::move(values);
            }
        }

        if (const auto textures = j.find("textures"); textures != j.end() && textures->is_object())
        {
            for (const auto& [slot, ref] : textures->items())
            {
                if (const AssetID texture = ResolveRef(assets, ref, slot))
                    data.Textures[slot] = texture;
                else
                    LOG_WARN("[MaterialImporter] '{}': texture '{}' not found", name, slot);
            }
        }

        return data;
    }

    std::string WriteMaterialData(const MaterialData& data, const AssetManager& assets)
    {
        nlohmann::ordered_json j;

        j["shader"] = WriteRef(assets, data.Shader);
        j["renderMode"] = data.Settings.RenderMode == MaterialRenderMode::Transparent ? "transparent" : "opaque";
        j["cull"] = data.Settings.Cull == CullMode::Back ? "back" : data.Settings.Cull == CullMode::Front ? "front" : "none";

        if (!data.Properties.empty())
        {
            nlohmann::ordered_json properties = nlohmann::ordered_json::object();

            for (const auto& [name, values] : data.Properties)
                properties[name] = values.size() == 1 ? nlohmann::ordered_json(values[0]) : nlohmann::ordered_json(values);

            j["properties"] = std::move(properties);
        }

        if (!data.Textures.empty())
        {
            nlohmann::ordered_json textures = nlohmann::ordered_json::object();

            for (const auto& [slot, texture] : data.Textures)
                textures[slot] = WriteRef(assets, texture);

            j["textures"] = std::move(textures);
        }

        return j.dump(2) + "\n";
    }

    std::optional<std::string> UpdateMaterialReferences(std::string_view text, const std::unordered_map<std::string, AssetID>& moved, const AssetManager& assets)
    {
        nlohmann::ordered_json j = nlohmann::ordered_json::parse(text, nullptr, false);
        if (!j.is_object())
            return std::nullopt;

        bool updated = false;

        if (const auto shader = j.find("shader"); shader != j.end())
            updated = UpdateRef(*shader, moved, assets);

        if (const auto textures = j.find("textures"); textures != j.end() && textures->is_object())
        {
            for (auto& [slot, ref] : textures->items())
                updated = UpdateRef(ref, moved, assets) || updated;
        }

        return updated ? std::optional(j.dump(2) + "\n") : std::nullopt;
    }
}
