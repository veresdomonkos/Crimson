#pragma once
#include <span>
#include <string_view>
#include <variant>
#include "crimson/asset_manager/asset_types.hpp"
#include "crimson/renderer/mesh.hpp"
#include "crimson/renderer/resource_manager.hpp"

namespace crimson
{
    class AssetManager;

    struct ImportContext
    {
        GpuResourceManager& Gpu;
        AssetManager&       Assets;
    };

    using LoadedAsset = std::variant<std::monostate, TextureHandle, ShaderHandle, MaterialHandle, Mesh>;

    class AssetImporter
    {
    public:
        virtual ~AssetImporter() = default;

        [[nodiscard]] virtual AssetType Type() const = 0;
        [[nodiscard]] virtual std::span<const std::string_view> Extensions() const = 0;
        [[nodiscard]] virtual nlohmann::json DefaultSettings() const = 0;

        virtual LoadedAsset Load(const AssetMetadata& meta, const std::filesystem::path& absoluteSource, ImportContext& ctx) = 0;
        virtual void Unload(const LoadedAsset& asset, GpuResourceManager& gpu) = 0;
    };

    template <typename T>
    concept AssetImporterType = std::derived_from<T, AssetImporter>;
}