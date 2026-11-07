#include "mtl_materials.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <sstream>

#include "crimson/asset_manager/asset_manager.hpp"
#include "crimson/core/log.hpp"

namespace fs = std::filesystem;

namespace crimson
{
    static constexpr std::string_view kLitShaderPath = "engine/shaders/S_Lit.hlsl";

    bool MaterialFolderReader::operator()(const std::string& matId, std::vector<tinyobj::material_t>* materials,
                                          std::map<std::string, int>* matMap, std::string* warn, std::string* err)
    {
        std::ifstream file(m_folder / fs::path(matId));
        if (!file)
        {
            if (warn)
                *warn += "Material file '" + matId + "' not found\n";
            return false;
        }

        tinyobj::LoadMtl(matMap, materials, &file, warn, err);
        return true;
    }

    std::vector<tinyobj::material_t> ReadMtlMaterials(const fs::path& objFile)
    {
        std::vector<tinyobj::material_t> materials;

        std::ifstream obj(objFile, std::ios::binary);
        if (!obj)
        {
            LOG_ERROR("[Assets] Can't read {}", objFile.string());
            return materials;
        }

        MaterialFolderReader reader(objFile.parent_path());
        std::map<std::string, int> names;
        std::string warning, error;

        // Each "mtllib" line names files, split by spaces like tinyobj does
        for (std::string line; std::getline(obj, line);)
        {
            std::istringstream words(line);
            std::string keyword;

            if (!(words >> keyword) || keyword != "mtllib")
                continue;

            for (std::string name; words >> name;)
                reader(name, &materials, &names, &warning, &error);
        }

        if (!warning.empty())
            LOG_WARN("[Assets] {}: {}", objFile.filename().string(), warning);
        if (!error.empty())
            LOG_ERROR("[Assets] {}: {}", objFile.filename().string(), error);

        return materials;
    }

    MtlMaterial MaterialFromMtl(const tinyobj::material_t& mtl, std::string_view meshFolder, AssetManager& assets)
    {
        MtlMaterial result;
        MaterialData& data = result.Data;

        data.Shader = assets.FindId(kLitShaderPath);

        // A value at the shader's own default isn't written, as the Inspector does
        const ShaderHandle shader = assets.LoadShader(data.Shader);
        const auto set = [&](const std::string& name, std::vector<float> values)
        {
            if (shader)
            {
                const auto& properties = assets.GetShaderInterface(shader).Properties;
                if (const auto it = properties.find(name); it != properties.end() && it->second.Default == values)
                    return;
            }

            data.Properties[name] = std::move(values);
        };

        const auto texture = [&](std::string name) -> AssetID
        {
            if (name.empty())
                return {};

            std::ranges::replace(name, '\\', '/');
            const std::string path = fs::path(std::string(meshFolder) + name).lexically_normal().generic_string();

            const AssetID id = assets.FindId(path);
            const AssetInfo* info = assets.GetInfo(id);

            if (!info || info->Type != AssetType::Texture)
            {
                LOG_WARN("[Assets] '{}': texture '{}' not found", mtl.name, path);
                return {};
            }

            return id;
        };

        // Exporters write 0 for an opaque one as often as 1, so only a value in between means see-through
        const bool transparent = mtl.dissolve > 0.0f && mtl.dissolve < 1.0f;
        if (transparent)
            data.Settings.RenderMode = MaterialRenderMode::Transparent;

        // With a texture, the color is the texture's. Exporters write a gray Kd next to one, which would only darken it.
        if (const AssetID color = texture(mtl.diffuse_texname))
        {
            data.Textures["ColorMap"] = color;

            if (transparent)
                set("Color", { 1.0f, 1.0f, 1.0f, mtl.dissolve });
        }
        else
        {
            set("Color", { mtl.diffuse[0], mtl.diffuse[1], mtl.diffuse[2], transparent ? mtl.dissolve : 1.0f });
        }

        // Normal maps come under any of these names, and the first one given is used
        for (const std::string* name : { &mtl.normal_texname, &mtl.bump_texname, &mtl.displacement_texname })
        {
            if (name->empty())
                continue;

            if (const AssetID normal = texture(*name))
            {
                data.Textures["NormalMap"] = normal;
                result.DataTextures.push_back(normal);
            }

            break;
        }

        // The PBR extension's roughness if it has one, and otherwise from how shiny it is
        const float roughness = mtl.roughness > 0.0f ? mtl.roughness : 1.0f - std::sqrt(std::clamp(mtl.shininess, 0.0f, 1000.0f) / 1000.0f);
        set("Roughness", { std::clamp(roughness, 0.0f, 1.0f) });
        set("Metallic", { std::clamp(mtl.metallic, 0.0f, 1.0f) });

        set("EmissiveColor", { mtl.emission[0], mtl.emission[1], mtl.emission[2] });
        if (const AssetID emissive = texture(mtl.emissive_texname))
            data.Textures["EmissiveMap"] = emissive;

        // Exporters often give the color texture as the ambient one too, which isn't worth mentioning
        const auto sameFile = [](std::string a, std::string b)
        {
            std::ranges::replace(a, '\\', '/');
            std::ranges::replace(b, '\\', '/');
            return a == b;
        };

        // What the default shader has no slot for. An alpha mask is a cutout, which materials can't do yet.
        std::vector<std::string_view> unused;
        if (!mtl.alpha_texname.empty())                                                           unused.push_back("map_d");
        if (!mtl.specular_texname.empty())                                                        unused.push_back("map_Ks");
        if (!mtl.ambient_texname.empty() && !sameFile(mtl.ambient_texname, mtl.diffuse_texname)) unused.push_back("map_Ka");
        if (!mtl.roughness_texname.empty())                                                       unused.push_back("map_Pr");
        if (!mtl.metallic_texname.empty())                                                        unused.push_back("map_Pm");

        if (!unused.empty())
        {
            std::string names;
            for (const std::string_view name : unused)
                names += (names.empty() ? "" : ", ") + std::string(name);

            LOG_INFO("[Assets] '{}': {} left out, as the default shader has nothing for them", mtl.name, names);
        }

        return result;
    }
}
