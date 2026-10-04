#pragma once
#include <iostream>
#include <memory>
#include <unordered_map>
#include "crimson/asset_manager/asset_importer.hpp"
#include "crimson/core/log.hpp"
#include "crimson/graphics/graphics_device.hpp"

namespace crimson
{
    class AssetManager
    {
    public:
        AssetManager(GpuResourceManager& gpuResources, std::filesystem::path assetRoot);
        ~AssetManager();

        AssetManager(const AssetManager&) = delete;
        AssetManager& operator=(const AssetManager&) = delete;

        template <AssetImporterType T, typename... Args> requires std::constructible_from<T, Args...>
        T& RegisterImporter(Args&&... args)
        {
            auto importer = std::make_unique<T>(std::forward<Args>(args)...);
            T& ref = *importer;
            AddImporter(std::move(importer));
            return ref;
        }

        void ScanDirectory();
        AssetID Import(const std::filesystem::path& source);

        [[nodiscard]] const AssetMetadata* GetMetadata(AssetID id) const;
        [[nodiscard]] AssetID FindByPath(const std::filesystem::path& source) const;

        template <typename T>
        [[nodiscard]] std::optional<T> GetAsset(AssetID id)
        {
            if (!id)
            {
                LOG_ERROR("[Assets] Invalid asset id {}", id.ToString());
                return std::nullopt;
            }

            const LoadedAsset& asset = Load(id);
            if (const auto* resource = std::get_if<T>(&asset))
                return *resource;

            LOG_ERROR("[Assets] Asset {} id doesn't belong to type {}", id.ToString(), typeid(T).name());
            return std::nullopt;
        }

        void Dump() const;
        void Unload(AssetID id);
    private:
        void AddImporter(std::unique_ptr<AssetImporter> importer);
    private:
        AssetImporter* FindImporter(const std::filesystem::path& source) const;
        const LoadedAsset& Load(AssetID id);

        GpuResourceManager& m_gpuResources;
        std::filesystem::path m_root;

        std::vector<std::unique_ptr<AssetImporter>> m_importers;
        std::unordered_map<std::string, AssetImporter*> m_extensionMap;
        std::unordered_map<AssetType, AssetImporter*> m_typeMap;

        std::unordered_map<AssetID, AssetMetadata> m_registry;
        std::unordered_map<std::string, AssetID> m_pathToId;
        std::unordered_map<AssetID, LoadedAsset> m_loaded;
    };
}
