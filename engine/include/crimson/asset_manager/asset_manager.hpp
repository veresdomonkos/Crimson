#pragma once
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "crimson/asset_manager/asset_importer.hpp"

namespace crimson
{
    // Knows every asset under one folder and loads them on first use. Destroying it frees what it loaded.
    // The engine's own shaders and materials are always available too, read-only, under "engine/".
    //
    // Asset paths are relative to the folder, with forward slashes. A load that fails returns an invalid
    // handle or null, logs once and isn't tried again. An invalid id does the same without logging, so a
    // component's empty asset slot needs no check.
    class AssetManager
    {
    public:
        AssetManager(GpuResourceManager& gpuResources, std::filesystem::path root);
        ~AssetManager();

        AssetManager(const AssetManager&) = delete;
        AssetManager& operator=(const AssetManager&) = delete;

        // What exists. None of these load anything.
        [[nodiscard]] const AssetInfo* GetInfo(AssetID id) const;
        [[nodiscard]] AssetID FindId(std::string_view path) const;
        [[nodiscard]] std::vector<const AssetInfo*> List(AssetType type) const; // sorted by path

        [[nodiscard]] AssetID DefaultMaterial() const;

        TextureHandle LoadTexture(AssetID id);
        ShaderHandle LoadShader(AssetID id);
        MaterialHandle LoadMaterial(AssetID id);
        const Mesh* LoadMesh(AssetID id); // stays valid as long as the manager

        TextureHandle LoadTexture(std::string_view path);
        ShaderHandle LoadShader(std::string_view path);
        MaterialHandle LoadMaterial(std::string_view path);
        const Mesh* LoadMesh(std::string_view path);
    private:
        struct Entry
        {
            AssetInfo Info;
            AssetImporter* Importer = nullptr;
            const EmbeddedFile* Embedded = nullptr;
            LoadedAsset Loaded;
            bool Failed = false;
        };

        void AddImporter(std::unique_ptr<AssetImporter> importer);
        void RegisterEngineResources();
        void Scan();
        void RegisterFile(const std::filesystem::path& file);

        const LoadedAsset& Load(AssetID id);
        AssetID FindIdOrWarn(std::string_view path) const;
        void Unload(Entry& entry);

        template <typename T>
        const T* LoadAs(AssetID id);

        GpuResourceManager& m_gpuResources;
        std::filesystem::path m_root;

        std::vector<std::unique_ptr<AssetImporter>> m_importers;
        std::unordered_map<std::string, AssetImporter*> m_importersByExtension;

        std::unordered_map<AssetID, Entry> m_assets;
        std::unordered_map<std::string, AssetID> m_idsByPath;
    };
}
