#pragma once
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include "crimson/asset_manager/asset_types.hpp"
#include "crimson/asset_manager/material_data.hpp"
#include "crimson/asset_manager/scene_data.hpp"
#include "crimson/core/embedded_resource.hpp"
#include "crimson/renderer/mesh.hpp"
#include "crimson/renderer/resource_manager.hpp"

namespace crimson
{
    class AssetManager;

    // What an asset's file says, for the types whose files hold nothing to edit
    struct NoAssetData {};

    // What loading each type of asset gives, and what its file says. Scenes load as nothing: they are read as data instead.
    // A type whose files hold data also says how to parse and write it.
    template <AssetType T> struct AssetTraits;
    template <> struct AssetTraits<AssetType::Texture>  { using Loaded = TextureHandle;  using Result = TextureHandle; using Data = NoAssetData; };
    template <> struct AssetTraits<AssetType::Shader>   { using Loaded = ShaderHandle;   using Result = ShaderHandle;  using Data = NoAssetData; };
    template <> struct AssetTraits<AssetType::Mesh>     { using Loaded = Mesh;           using Result = const Mesh*; using Data = NoAssetData; }; // valid as long as the manager

    template <> struct AssetTraits<AssetType::Material>
    {
        using Loaded = MaterialHandle;
        using Result = MaterialHandle;
        using Data = MaterialData;

        static constexpr auto Parse = &ParseMaterialData;
        static constexpr auto Write = &WriteMaterialData;
    };

    template <> struct AssetTraits<AssetType::Scene>
    {
        using Data = SceneData;

        static constexpr auto Parse = &ParseSceneData;
        static constexpr auto Write = &WriteSceneData;
    };

    template <AssetType T>
    using LoadedAsset = typename AssetTraits<T>::Loaded;

    template <AssetType T>
    using LoadResult = typename AssetTraits<T>::Result;

    template <AssetType T>
    using AssetData = typename AssetTraits<T>::Data;

    enum class AssetUpdate
    {
        Failed,
        Updated,  // the asset's handle is the same
        Replaced  // the asset has a new handle, so anything holding on to the old one has to get it again
    };

    struct ImportContext
    {
        GpuResourceManager& Gpu;
        AssetManager&       Assets;
        std::vector<std::filesystem::path> Dependencies{}; // other files the asset was made from, for reloading it when they change
        std::vector<AssetID> Uses{}; // the assets this one was made from, for it to be updated when they change
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

        // The contents of a new file of this type. Nothing means the type can't be created.
        [[nodiscard]] virtual std::optional<std::string> NewFile(const AssetManager&) const { return std::nullopt; }

        // The file's text with its references to assets that moved (old path to id) pointed at where they are now.
        // Nothing if it has none, or none moved. Only a type that has references reads its file.
        [[nodiscard]] virtual std::optional<std::string> UpdateReferences(const AssetSource&, const std::unordered_map<std::string, AssetID>&, const AssetManager&) const
        {
            return std::nullopt;
        }

        // Whether a change to the file on disk should load the asset again
        [[nodiscard]] virtual bool ReloadsWhenChanged() const { return false; }

        // Whether a loaded asset can be replaced: false when what uses it holds a pointer that nothing would resolve again
        [[nodiscard]] virtual bool CanReimport() const { return true; }

        // Frees what the importer made to share between its assets, once they are all unloaded
        virtual void ReleaseShared(GpuResourceManager&) {}
    };

    // One that also loads them. Errors go to the log and give nothing.
    template <AssetType T>
    class AssetImporter : public AssetImporterBase
    {
    public:
        [[nodiscard]] AssetType Type() const final { return T; }

        virtual std::optional<LoadedAsset<T>> Load(const AssetSource& source, ImportContext& ctx) = 0;
        virtual void Unload(LoadedAsset<T>& asset, GpuResourceManager& gpu) = 0;

        // Makes the asset from data that may have been edited, instead of from its file. Nothing if the type can't.
        virtual std::optional<LoadedAsset<T>> Create(const AssetData<T>&, const AssetSource&, ImportContext&) { return std::nullopt; }

        // Shows other data on the loaded asset without making it again. False means it has to be made again.
        virtual bool Update(LoadedAsset<T>&, const AssetData<T>& /*from*/, const AssetData<T>& /*to*/, ImportContext&) { return false; }

        // Points the asset at one it uses that was loaded again or removed. False means it has to be made again.
        virtual bool UpdateUse(LoadedAsset<T>&, const AssetInfo&, AssetID /*used*/, ImportContext&) { return false; }
    };
}
