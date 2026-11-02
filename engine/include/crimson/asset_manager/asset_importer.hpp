#pragma once
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include "crimson/asset_manager/asset_types.hpp"
#include "crimson/core/embedded_resource.hpp"
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

    // The asset an importer is loading. Importers get its bytes from here instead of opening the file,
    // as an engine resource has no file.
    struct AssetSource
    {
        const AssetInfo& Info;
        std::filesystem::path File; // empty for an engine resource
        const EmbeddedFile* Embedded = nullptr;

        // Logs when it fails
        [[nodiscard]] std::optional<std::string> Read() const;
    };

    using LoadedAsset = std::variant<std::monostate, TextureHandle, ShaderHandle, MaterialHandle, Mesh>;

    class AssetImporter
    {
    public:
        virtual ~AssetImporter() = default;

        [[nodiscard]] virtual AssetType Type() const = 0;
        [[nodiscard]] virtual std::span<const std::string_view> Extensions() const = 0;
        [[nodiscard]] virtual nlohmann::json DefaultSettings() const = 0;

        virtual LoadedAsset Load(const AssetSource& source, ImportContext& ctx) = 0;
        virtual void Unload(const LoadedAsset& asset, GpuResourceManager& gpu) = 0;
    };
}
