#pragma once
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <tiny_obj_loader.h>

#include "crimson/asset_manager/material_data.hpp"

namespace crimson
{
    class AssetManager;

    // Reads an .obj's .mtl files from its folder. tinyobj's own reader can't open paths with non-ASCII characters on Windows.
    class MaterialFolderReader final : public tinyobj::MaterialReader
    {
    public:
        explicit MaterialFolderReader(std::filesystem::path folder) : m_folder(std::move(folder)) {}

        bool operator()(const std::string& matId, std::vector<tinyobj::material_t>* materials,
                        std::map<std::string, int>* matMap, std::string* warn, std::string* err) override;
    private:
        std::filesystem::path m_folder;
    };

    // The materials in the .mtl files an .obj names, without reading its geometry, which can take seconds
    [[nodiscard]] std::vector<tinyobj::material_t> ReadMtlMaterials(const std::filesystem::path& objFile);

    struct MtlMaterial
    {
        MaterialData Data;
        std::vector<AssetID> DataTextures; // the textures it uses that hold values rather than colors, which need sRGB off
    };

    // An .mtl material as one on the default lit shader. Its texture paths are relative to meshFolder, the asset folder of
    // the .obj with a slash at the end, or empty at the top. One that isn't there is logged and left out.
    [[nodiscard]] MtlMaterial MaterialFromMtl(const tinyobj::material_t& mtl, std::string_view meshFolder, AssetManager& assets);
}
