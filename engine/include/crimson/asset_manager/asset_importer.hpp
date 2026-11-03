#pragma once
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include "crimson/asset_manager/asset_types.hpp"
#include "crimson/core/embedded_resource.hpp"
#include "crimson/renderer/mesh.hpp"
#include "crimson/renderer/resource_manager.hpp"

namespace crimson
{
    class AssetManager;

    // What loading each type of asset gives. Scenes have none, as they are read with ReadSceneFile instead.
    template <AssetType T> struct AssetTraits;
    template <> struct AssetTraits<AssetType::Texture>  { using Loaded = TextureHandle;  using Result = TextureHandle; };
    template <> struct AssetTraits<AssetType::Shader>   { using Loaded = ShaderHandle;   using Result = ShaderHandle; };
    template <> struct AssetTraits<AssetType::Material> { using Loaded = MaterialHandle; using Result = MaterialHandle; };
    template <> struct AssetTraits<AssetType::Mesh>     { using Loaded = Mesh;           using Result = const Mesh*; }; // valid as long as the manager

    template <AssetType T>
    using LoadedAsset = typename AssetTraits<T>::Loaded;

    template <AssetType T>
    using LoadResult = typename AssetTraits<T>::Result;

    struct ImportContext
    {
        GpuResourceManager& Gpu;
        AssetManager&       Assets;
        std::vector<std::filesystem::path> Dependencies{}; // other files the asset was made from, for reloading it when they change
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

    // What makes files of some extensions into assets of a type
    class AssetImporterBase
    {
    public:
        virtual ~AssetImporterBase() = default;

        [[nodiscard]] virtual AssetType Type() const = 0;
        [[nodiscard]] virtual std::span<const std::string_view> Extensions() const = 0;
        [[nodiscard]] virtual nlohmann::json DefaultSettings() const = 0;
    };

    // One that also loads them. Errors go to the log and give nothing.
    template <AssetType T>
    class AssetImporter : public AssetImporterBase
    {
    public:
        [[nodiscard]] AssetType Type() const final { return T; }

        virtual std::optional<LoadedAsset<T>> Load(const AssetSource& source, ImportContext& ctx) = 0;
        virtual void Unload(LoadedAsset<T>& asset, GpuResourceManager& gpu) = 0;
    };
}
