#pragma once
#include <concepts>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>
#include "crimson/asset_manager/asset_types.hpp"
#include "crimson/core/embedded_resource.hpp"
#include "crimson/renderer/resource_manager.hpp"

namespace crimson
{
    class AssetManager;
    class ImportCache;

    // What loading a type of asset gives, and what its file says, as the type's struct names them
    template <typename T>
    using LoadedAsset = typename T::Loaded;

    template <typename T>
    using LoadResult = typename T::Result;

    template <typename T>
    using AssetData = typename T::Data;

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
        ImportCache&        Cache; // where this import's results are kept, chosen by where its source comes from
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
    template <typename T>
    class AssetImporter : public AssetImporterBase
    {
    public:
        [[nodiscard]] AssetType Type() const final { return T::Tag; }

        virtual std::optional<LoadedAsset<T>> Load(const AssetSource& source, ImportContext& ctx) = 0;
        virtual void Unload(LoadedAsset<T>& asset, GpuResourceManager& gpu) = 0;

        // Makes the asset from data that may have been edited, instead of from its file. Nothing if the type can't.
        virtual std::optional<LoadedAsset<T>> Create(const AssetData<T>&, const AssetSource&, ImportContext&) { return std::nullopt; }

        // Shows other data on the loaded asset without making it again. False means it has to be made again.
        virtual bool Update(LoadedAsset<T>&, const AssetData<T>& /*from*/, const AssetData<T>& /*to*/, ImportContext&) { return false; }

        // Points the asset at one it uses that was loaded again or removed. False means it has to be made again.
        virtual bool UpdateUse(LoadedAsset<T>&, const AssetInfo&, AssetID /*used*/, ImportContext&) { return false; }
    };

    // Whether a type of asset loads as something. Scenes don't: they are read as data instead.
    template <typename T>
    concept LoadableAsset = requires { typename T::Loaded; };

    // What the struct of an asset type has to say for AssetManager::RegisterType to take it: what it is called,
    // what its file holds, how to parse and write that if it holds any, and the types it is made from
    template <typename T>
    concept CompleteAssetType =
        requires
        {
            { T::Tag } -> std::convertible_to<AssetType>;
            typename T::Data;
            { T::Uses } -> std::convertible_to<std::span<const AssetType>>;
        }
        && (!LoadableAsset<T> || requires { typename T::Result; })
        && (std::is_same_v<typename T::Data, NoAssetData> || requires { T::Parse; T::Write; });

    // Whether an importer can make the assets of a type: it is made without arguments, and loads what the type says it loads
    template <typename Importer, typename T>
    concept ImporterFor = std::derived_from<Importer, AssetImporterBase> && std::default_initializable<Importer>
        && (!LoadableAsset<T> || std::derived_from<Importer, AssetImporter<T>>);
}
