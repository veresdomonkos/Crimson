#include "crimson/asset_manager/slot_materials.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iterator>
#include <system_error>

#include "crimson/core/log.hpp"
#include "mtl_materials.hpp"

namespace fs = std::filesystem;

namespace crimson
{
    static constexpr std::string_view kDefaultMaterialPath = "engine/materials/M_Default.mat";

    // The folder an asset is in, with a slash at the end, or empty at the top
    static std::string FolderOf(const std::string& path)
    {
        const size_t folderEnd = path.rfind('/');
        return folderEnd == std::string::npos ? std::string{} : path.substr(0, folderEnd + 1);
    }

    // Where the material for one of a mesh's slots goes
    static std::string SlotMaterialPath(const std::string& meshPath, std::string_view slot)
    {
        return FolderOf(meshPath) + "materials/" + std::string(slot) + ".mat";
    }

    static bool IsObj(const std::string& path)
    {
        std::string extension = fs::path(path).extension().string();
        std::ranges::transform(extension, extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return extension == ".obj";
    }

    MaterialID DefaultMaterial(const AssetManager& assets)
    {
        return assets.Find<AssetType::Material>(kDefaultMaterialPath);
    }

    MaterialID FindSlotMaterial(const AssetManager& assets, MeshID mesh, std::string_view slot)
    {
        const AssetInfo* info = assets.GetInfo(mesh);
        if (!info || info->Type != AssetType::Mesh)
            return {};

        return assets.Find<AssetType::Material>(SlotMaterialPath(info->Path, slot));
    }

    SlotMaterials CreateSlotMaterials(AssetManager& assets, MeshID mesh)
    {
        SlotMaterials result;

        const AssetInfo* info = assets.GetInfo(mesh);
        if (!info || info->Type != AssetType::Mesh || info->ReadOnly || !IsObj(info->Path))
        {
            LOG_ERROR("[Assets] Only a project's .obj meshes can get materials from their .mtl");
            return result;
        }

        // Copied, as creating files adds assets, which can move the info
        const std::string meshPath = info->Path;
        std::vector<TextureID> dataTextures;

        for (const tinyobj::material_t& mtl : ReadMtlMaterials(assets.GetFilePath(mesh)))
        {
            const std::string path = SlotMaterialPath(meshPath, mtl.name);

            std::error_code error;
            if (assets.FindId(path) || fs::exists(assets.GetRoot() / fs::path(path), error))
            {
                ++result.Kept;
                continue;
            }

            MtlMaterial material = MaterialFromMtl(mtl, FolderOf(meshPath), assets);

            if (const MaterialID created = assets.Create<AssetType::Material>(path, material.Data))
            {
                result.Created.push_back(created);
                std::ranges::move(material.DataTextures, std::back_inserter(dataTextures));
            }
        }

        // Normal maps hold directions, which sRGB decoding would bend
        for (const TextureID texture : dataTextures)
        {
            const AssetInfo* textureInfo = assets.GetInfo(texture);
            if (!textureInfo || textureInfo->ReadOnly || !textureInfo->Settings.value("srgb", true))
                continue;

            nlohmann::json settings = textureInfo->Settings;
            settings["srgb"] = false;
            assets.Reimport(texture, std::move(settings));
        }

        LOG_INFO("[Assets] {}: made {} slot materials, and kept {} that were there", meshPath, result.Created.size(), result.Kept);
        return result;
    }
}
