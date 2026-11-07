#include "crimson/asset_manager/material_data.hpp"

#include <nlohmann/json.hpp>

#include "crimson/asset_manager/asset_manager.hpp"
#include "crimson/core/log.hpp"

#include "asset_ref.hpp"

namespace crimson
{
    namespace
    {
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
            data.Shader = ResolveAssetRef(assets, *shader, name);
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
                // One that is missing stays, for the file not to lose it
                if (const AssetID texture = ResolveAssetRef(assets, ref, std::string(name) + " " + slot))
                    data.Textures[slot] = texture;
            }
        }

        return data;
    }

    std::string WriteMaterialData(const MaterialData& data, const AssetManager& assets)
    {
        nlohmann::ordered_json j;

        j["shader"] = WriteAssetRef(assets, data.Shader);
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
                textures[slot] = WriteAssetRef(assets, texture);

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
            updated = UpdateAssetRef(*shader, moved, assets);

        if (const auto textures = j.find("textures"); textures != j.end() && textures->is_object())
        {
            for (auto& [slot, ref] : textures->items())
                updated = UpdateAssetRef(ref, moved, assets) || updated;
        }

        return updated ? std::optional(j.dump(2) + "\n") : std::nullopt;
    }
}
