#include "crimson/asset_manager/importers/material_importer.hpp"

#include <fstream>
#include <glm/glm.hpp>

#include "crimson/asset_manager/asset_manager.hpp"
#include "crimson/core/log.hpp"

namespace crimson
{
    namespace
    {
        AssetID ResolveRef(const AssetManager& assets, const nlohmann::json& ref, std::string_view context)
        {
            if (ref.is_string())
                return assets.FindByPath(ref.get<std::string>());

            if (!ref.is_object())
                return {};

            if (ref.contains("guid") && ref["guid"].is_string())
            {
                try
                {
                    const AssetID id = AssetID::FromString(ref["guid"].get<std::string>());
                    if (id && assets.GetMetadata(id))
                        return id;
                }
                catch (const std::exception&) {}
            }

            if (ref.contains("path") && ref["path"].is_string())
            {
                const std::string path = ref["path"].get<std::string>();
                if (const AssetID byPath = assets.FindByPath(path))
                {
                    LOG_WARN("[MaterialImporter] {}: stale guid, resolved by path '{}'", context, path);
                    return byPath;
                }
            }

            return {};
        }

        void ApplyProperty(GpuResourceManager& gpu, MaterialHandle material,
                           const std::string& name, const nlohmann::json& value)
        {
            try
            {
                if (value.is_number())
                {
                    gpu.SetMaterialPropertyByName(material, name, value.get<float>());
                    return;
                }

                if (value.is_array())
                {
                    const auto v = value.get<std::vector<float>>();
                    switch (v.size())
                    {
                        case 2: gpu.SetMaterialPropertyByName(material, name, glm::vec2(v[0], v[1])); return;
                        case 3: gpu.SetMaterialPropertyByName(material, name, glm::vec3(v[0], v[1], v[2])); return;
                        case 4: gpu.SetMaterialPropertyByName(material, name, glm::vec4(v[0], v[1], v[2], v[3])); return;
                        default:
                            LOG_WARN("[MaterialImporter] Property '{}': unsupported array size {}", name, v.size());
                            return;
                    }
                }

                LOG_WARN("[MaterialImporter] Property '{}': unsupported value type", name);
            }
            catch (const nlohmann::json::exception& e)
            {
                LOG_WARN("[MaterialImporter] Property '{}': {}", name, e.what());
            }
        }
    }

    std::span<const std::string_view> MaterialImporter::Extensions() const
    {
        static constexpr std::string_view exts[] = { ".mat" };
        return exts;
    }

    nlohmann::json MaterialImporter::DefaultSettings() const
    {
        return nlohmann::json::object();
    }

    LoadedAsset MaterialImporter::Load(const AssetMetadata&, const std::filesystem::path& path, ImportContext& ctx)
    {
        std::ifstream file(path);
        if (!file)
        {
            LOG_ERROR("[MaterialImporter] Cannot open '{}'", path.string());
            return {};
        }

        nlohmann::json j;
        try { j = nlohmann::json::parse(file); }
        catch (const nlohmann::json::exception& e)
        {
            LOG_ERROR("[MaterialImporter] Invalid JSON in '{}': {}", path.string(), e.what());
            return {};
        }

        if (!j.contains("shader"))
        {
            LOG_ERROR("[MaterialImporter] '{}': missing 'shader'", path.string());
            return {};
        }

        const AssetID shaderId = ResolveRef(ctx.Assets, j["shader"], path.filename().string());
        const auto shader = ctx.Assets.GetAsset<ShaderHandle>(shaderId);
        if (!shader)
        {
            LOG_ERROR("[MaterialImporter] '{}': shader not found or failed to load", path.string());
            return {};
        }

        const auto material = ctx.Gpu.CreateMaterial(shader.value());
        if (!material)
        {
            LOG_ERROR("[MaterialImporter] CreateMaterial failed for '{}'", path.string());
            return {};
        }

        if (j.contains("properties") && j["properties"].is_object())
            for (const auto& [name, value] : j["properties"].items())
                ApplyProperty(ctx.Gpu, material, name, value);

        if (j.contains("textures") && j["textures"].is_object())
        {
            for (const auto& [name, ref] : j["textures"].items())
            {
                const AssetID texId = ResolveRef(ctx.Assets, ref, name);
                const auto tex = ctx.Assets.GetAsset<TextureHandle>(texId);

                if (!tex)
                {
                    LOG_WARN("[MaterialImporter] '{}': texture '{}' not found", path.string(), name);
                    continue;
                }
                ctx.Gpu.SetMaterialTexture(material, name, tex.value());
            }
        }

        return material;
    }

    void MaterialImporter::Unload(const LoadedAsset& asset, GpuResourceManager& gpu)
    {
        if (const auto* material = std::get_if<MaterialHandle>(&asset))
            gpu.DestroyMaterial(*material);
    }
}