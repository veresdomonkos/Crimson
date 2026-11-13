#pragma once
#include <array>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "crimson/asset_manager/asset_importer.hpp"
#include "crimson/asset_manager/material_data.hpp"
#include "crimson/asset_manager/scene_data.hpp"
#include "crimson/scene/component_registry.hpp"

namespace crimson
{
    // A reference to an asset that isn't there, with what it was written as, for a save to write it back the same way
    struct MissingAssetRef
    {
        std::string Path;
        bool PathOnly = false; // written by hand without a guid, so its id was made up from the path
        AssetType Type = AssetType::None; // what it was read as, so it keeps a typed id while its asset is away
    };

    class AssetStoreBase;
    template <AssetType T> class AssetStore;

    // Knows every asset under one folder and loads them on first use. Destroying it frees what it loaded.
    // The engine's own shaders, materials and scenes are always available too, read-only, under "engine/".
    //
    // Asset paths are relative to the folder, with forward slashes. A load that fails returns an invalid
    // handle or null, logs once and isn't tried again. An invalid id does the same without logging, so a
    // component's empty asset slot needs no check.
    //
    // What changes or removes GPU resources has to run between frames: Refresh, Delete, Apply, Reimport, ReloadChanged
    // and CreateSlotMaterials, which reimports.
    // Handles that were copied elsewhere stop working when these remove what they refer to.
    class AssetManager
    {
    public:
        AssetManager(GpuResourceManager& gpuResources, std::filesystem::path root);
        ~AssetManager();

        AssetManager(const AssetManager&) = delete;
        AssetManager& operator=(const AssetManager&) = delete;

        // What exists. None of these load anything.
        [[nodiscard]] const AssetInfo* GetInfo(AssetID id) const;
        [[nodiscard]] AssetID FindId(std::string_view path) const; // an engine asset's old path finds it under its new one

        // The id an engine asset that was renamed has now, for one that was saved with its old id. Any other id stays.
        [[nodiscard]] AssetID Redirect(AssetID id) const;
        [[nodiscard]] std::vector<const AssetInfo*> List(AssetType type) const; // sorted by path
        [[nodiscard]] std::vector<const AssetInfo*> ListAll() const; // sorted by path
        [[nodiscard]] const std::filesystem::path& GetRoot() const { return m_root; }
        [[nodiscard]] std::filesystem::path GetFilePath(AssetID id) const; // empty for an engine asset
        [[nodiscard]] bool IsLoaded(AssetID id) const;

        // A reference that was read while its asset wasn't there keeps its id, for it to be saved and work again once the
        // file is back. Resolving a reference is only a read of the project, so remembering one counts as const.
        [[nodiscard]] bool IsMissing(AssetID id) const { return id && !GetInfo(id); }
        [[nodiscard]] const MissingAssetRef* GetMissing(AssetID id) const; // null unless IsMissing
        bool RememberMissing(AssetID id, MissingAssetRef ref) const; // whether it is new, for a warning only the first time
        // The id with its type, or an empty one if there is no such asset or it is of another type. A missing one has the
        // type it was read as, so a material can keep a texture that isn't there, and save it.
        template <AssetType T>
        [[nodiscard]] TypedAssetID<T> As(AssetID id) const
        {
            if (const AssetInfo* info = GetInfo(id))
                return info->Type == T ? TypedAssetID<T>(id) : TypedAssetID<T>();

            const MissingAssetRef* missing = GetMissing(id);
            return missing && missing->Type == T ? TypedAssetID<T>(id) : TypedAssetID<T>();
        }

        template <AssetType T>
        [[nodiscard]] TypedAssetID<T> Find(std::string_view path) const { return As<T>(FindId(path)); }

        // An invalid handle or null if it can't be loaded
        template <AssetType T>
        LoadResult<T> Load(TypedAssetID<T> id);

        // A shortcut for code that knows its asset's path, which warns if nothing of that type is there
        template <AssetType T>
        LoadResult<T> Load(std::string_view path) { return Load(FindOrWarn<T>(path)); }

        // The kinds of components the project's scenes can have, the engine's own and any registered since
        [[nodiscard]] ComponentRegistry& Components() { return m_components; }
        [[nodiscard]] const ComponentRegistry& Components() const { return m_components; }

        // What a loaded shader's materials can set
        [[nodiscard]] const ShaderInterface& GetShaderInterface(ShaderHandle shader) const { return m_gpuResources.GetShaderInterface(shader); }

        // Looks at the folder again: new files appear, files moved along with their meta keep their id,
        // and the assets whose files are gone are removed. Returns the ids of those.
        std::vector<AssetID> Refresh();

        bool Rename(AssetID id, std::string_view newPath);
        bool Delete(AssetID id);

        // The bytes of a project or engine asset's file. Logs when it fails.
        [[nodiscard]] std::optional<std::string> ReadSource(AssetID id) const;

        // What an asset's file says, for a type whose files hold data (see AssetTraits). Nothing if it can't be read or parsed.
        template <AssetType T>
        [[nodiscard]] std::optional<AssetData<T>> Read(TypedAssetID<T> id) const
        {
            const AssetInfo* info = GetInfo(id);
            if (!info || info->Type != T)
                return std::nullopt;

            const std::optional<std::string> text = ReadSource(id);
            return text ? AssetTraits<T>::Parse(*text, *this, info->Path) : std::nullopt;
        }

        // Writes the data to the asset's file. Engine assets can't be saved.
        template <AssetType T>
        bool Save(TypedAssetID<T> id, const AssetData<T>& data)
        {
            return WriteFile(id, AssetTraits<T>::Write(data, *this));
        }

        // Makes a file at the path, a new one of the type or one with the data, and the asset for it. These report failures
        // in the log. Engine assets can't be changed, and project assets can't go under "engine/".
        template <AssetType T>
        TypedAssetID<T> Create(std::string_view path) { return As<T>(CreateFile(path, T, std::nullopt)); }

        template <AssetType T>
        TypedAssetID<T> Create(std::string_view path, const AssetData<T>& data) { return As<T>(CreateFile(path, T, AssetTraits<T>::Write(data, *this))); }

        // What the asset is using right now, which edits may have made different from its file
        template <AssetType T>
        [[nodiscard]] const AssetData<T>* GetData(TypedAssetID<T> id);

        // Shows the data on the asset, leaving its file alone
        template <AssetType T>
        AssetUpdate Apply(TypedAssetID<T> id, const AssetData<T>& data);

        // Stores the settings in the asset's meta file, and if it is loaded, loads it again for what uses it. Not for a type whose importer
        // says it can't be: what uses it holds a pointer that nothing would resolve again.
        bool Reimport(AssetID id, nlohmann::json settings);

        // Loads the project's assets again whose file or included files changed since they were loaded, if their type
        // does that, and updates what uses them. One that no longer loads keeps the old version. Returns the ids of
        // what was replaced, whose handles have to be gotten again.
        std::vector<AssetID> ReloadChanged();
    private:
        // What is known about every asset, loaded or not. What it loads as is in its type's store.
        struct Record
        {
            AssetInfo Info;
            AssetImporterBase* Importer = nullptr;
            const EmbeddedFile* Embedded = nullptr;
        };

        void AddImporter(std::unique_ptr<AssetImporterBase> importer);
        void RegisterEngineResources();
        void Scan(std::unordered_set<AssetID>& seen);
        void RegisterFile(const std::filesystem::path& file, std::unordered_set<AssetID>& seen);
        void RemoveAssets(std::span<const AssetID> ids);
        void UpdateReferencesTo(const std::unordered_map<std::string, AssetID>& moved); // old path to id

        bool WriteFile(AssetID id, const std::string& contents);

        // The contents are a new file of the type if none are given
        AssetID CreateFile(std::string_view path, AssetType type, std::optional<std::string> contents);

        // Loads it again and updates what uses it. Returns it and what was replaced, or nothing if it didn't load.
        std::vector<AssetID> Reload(AssetID id);

        // What uses the asset, which was loaded again or removed, gets updated. The ids of those that got a new handle go in replaced.
        void UpdateUsersOf(AssetID used, bool removed, std::vector<AssetID>* replaced);

        template <AssetType T>
        AssetStore<T>& Store();

        [[nodiscard]] AssetStoreBase* StoreOf(AssetType type);

        // Where an asset of a type keeps what it loaded. Null if there is no such asset of that type.
        template <AssetType T>
        typename AssetStore<T>::Slot* SlotFor(AssetID id);

        [[nodiscard]] AssetSource SourceOf(const Record& record) const;
        AssetID FindIdOrWarn(std::string_view path) const;

        template <AssetType T>
        TypedAssetID<T> FindOrWarn(std::string_view path) const
        {
            const AssetID id = FindIdOrWarn(path);
            const TypedAssetID<T> typed = As<T>(id);
            if (id && !typed)
                WarnWrongType(path, T);
            return typed;
        }

        void WarnWrongType(std::string_view path, AssetType wanted) const;

        GpuResourceManager& m_gpuResources;
        ComponentRegistry m_components{ *this };
        std::filesystem::path m_root;

        std::vector<std::unique_ptr<AssetImporterBase>> m_importers;
        std::unordered_map<std::string, AssetImporterBase*> m_importersByExtension;

        std::unordered_map<AssetID, Record> m_assets;
        std::unordered_map<std::string, AssetID> m_idsByPath;
        std::unordered_map<std::string, AssetID> m_renamedPaths; // an engine asset's old path to its id
        std::unordered_map<AssetID, AssetID> m_renamedIds; // the id made from an engine asset's old path to its id
        mutable std::unordered_map<AssetID, MissingAssetRef> m_missing;

        // One per type that loads, by AssetType. Scenes have none.
        std::array<std::unique_ptr<AssetStoreBase>, kAssetTypeCount> m_stores;
    };
}
