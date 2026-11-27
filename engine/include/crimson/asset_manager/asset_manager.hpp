#pragma once
#include <cstdint>
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
#include <type_traits>

#include "crimson/asset_manager/asset_importer.hpp"
#include "crimson/asset_manager/asset_store.hpp"
#include "crimson/asset_manager/import_cache.hpp"
#include "crimson/asset_manager/material_data.hpp"
#include "crimson/asset_manager/scene_data.hpp"
#include "crimson/core/log.hpp"
#include "crimson/scene/component_registry.hpp"

namespace crimson
{
    // A reference to an asset that isn't there, with what it was written as, for a save to write it back the same way
    struct MissingAssetRef
    {
        std::string Path;
        bool PathOnly = false; // written by hand without a guid, so its id was made up from the path
        AssetType Type; // what it was read as, so it keeps a typed id while its asset is away
    };

    // Knows every asset under one folder and loads them on first use. Destroying it frees what it loaded.
    // The engine's own shaders, materials and scenes are always available too, read-only, under "engine/".
    //
    // Asset paths are relative to the folder, with forward slashes. A lookup that can't give the asset returns null, logs
    // once and isn't tried again. An invalid id does the same without logging, so a
    // component's empty asset slot needs no check.
    //
    // What changes or removes GPU resources has to run between frames: Refresh, Delete, Apply, Reimport, ReloadChanged
    // and slot_materials::Create, which reimports.
    // Handles and pointers that were kept elsewhere stop working when these remove what they refer to.
    class AssetManager
    {
    public:
        // What importing the project's files makes is kept in cacheFolder, or only in memory if it is empty, with the
        // dependencies on files under root relative to it. The engine's own assets have their own, ImportCache::Engine().
        AssetManager(GpuResourceManager& gpuResources, std::filesystem::path root, std::filesystem::path cacheFolder = {});
        ~AssetManager();

        AssetManager(const AssetManager&) = delete;
        AssetManager& operator=(const AssetManager&) = delete;

        // Makes the assets of a type known, made by the importer, with a store for a type that loads. A type is registered after
        // the types it uses, so Types() lists each after what it uses, and unloading goes the other way round. It is refused,
        // with an error in the log, when it is registered already, uses a type that isn't yet, its importer is for another
        // type or one of its extensions has an importer. The files of a type registered after the manager was made are found
        // by the next Refresh.
        template <CompleteAssetType T, ImporterFor<T> Importer>
        bool RegisterType();

        // The registered types, in the order they were registered
        [[nodiscard]] std::vector<AssetType> Types() const;

        // What exists. None of these load anything.
        [[nodiscard]] const AssetInfo* GetInfo(AssetID id) const;
        [[nodiscard]] AssetID FindId(std::string_view path) const; // an engine asset's old path finds it under its new one

        // The reference's asset if it exists, else the one at its path. Empty if neither is there. Logs nothing.
        [[nodiscard]] AssetID FindId(const AssetRef& ref) const;

        // The id an engine asset that was renamed has now, for one that was saved with its old id. Any other id stays.
        [[nodiscard]] AssetID Redirect(AssetID id) const;
        [[nodiscard]] std::vector<const AssetInfo*> List(AssetType type) const; // sorted by path
        [[nodiscard]] std::vector<const AssetInfo*> ListAll() const; // sorted by path
        [[nodiscard]] const std::filesystem::path& GetRoot() const { return m_root; }
        [[nodiscard]] std::filesystem::path GetFilePath(AssetID id) const; // empty for an engine asset
        [[nodiscard]] bool IsLoaded(AssetID id) const;

        // Changes when assets are added, removed, renamed or loaded again (Refresh, Create, Rename, Delete, Reimport,
        // ReloadChanged). First loads and Apply leave it. For whatever keeps something worked out from the assets.
        [[nodiscard]] uint64_t Version() const { return m_version; }

        // A reference that was read while its asset wasn't there keeps its id, for it to be saved and work again once the
        // file is back. Reading a reference is only a read of the project, so remembering one counts as const.
        [[nodiscard]] bool IsMissing(AssetID id) const { return id && !GetInfo(id); }
        [[nodiscard]] const MissingAssetRef* GetMissing(AssetID id) const; // null unless IsMissing
        bool RememberMissing(AssetID id, MissingAssetRef ref) const; // whether it is new, for a warning only the first time
        // The id with its type, or an empty one if there is no such asset or it is of another type. A missing one has the
        // type it was read as, so a material can keep a texture that isn't there, and save it.
        template <typename T>
        [[nodiscard]] TypedAssetID<T> As(AssetID id) const
        {
            if (const AssetInfo* info = GetInfo(id))
                return info->Type.Is<T>() ? TypedAssetID<T>(id) : TypedAssetID<T>();

            const MissingAssetRef* missing = GetMissing(id);
            return missing && missing->Type.Is<T>() ? TypedAssetID<T>(id) : TypedAssetID<T>();
        }

        template <typename T>
        [[nodiscard]] TypedAssetID<T> Find(std::string_view path) const { return As<T>(FindId(path)); }

        // What it loaded as, loading it first if it isn't yet. Null if it can't be had: invalid, missing, of another type,
        // or failed to load. Valid until assets change between frames, so keep the id, not this.
        template <LoadableAsset T>
        [[nodiscard]] const LoadedAsset<T>* Get(TypedAssetID<T> id);

        // A shortcut for code that knows its asset's path, which warns if nothing of that type is there
        template <LoadableAsset T>
        [[nodiscard]] const LoadedAsset<T>* Get(std::string_view path) { return Get(FindOrWarn<T>(path)); }

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
        // A file that lacks ids its type keeps across loads gets them written in first. It is const as that changes
        // no manager state, only the asset's own file.
        [[nodiscard]] std::optional<std::string> ReadSource(AssetID id) const;

        // What an asset's file says, for a type whose files hold data (see its struct). Nothing if it can't be read or parsed.
        template <typename T>
        [[nodiscard]] std::optional<AssetData<T>> Read(TypedAssetID<T> id) const
        {
            const AssetInfo* info = GetInfo(id);
            if (!info || !info->Type.Is<T>())
                return std::nullopt;

            const std::optional<std::string> text = ReadSource(id);
            return text ? T::Parse(*text, *this, info->Path) : std::nullopt;
        }

        // Writes the data to the asset's file. Engine assets can't be saved.
        template <typename T>
        bool Save(TypedAssetID<T> id, const AssetData<T>& data)
        {
            return WriteFile(id, T::Write(data, *this));
        }

        // Makes a file at the path, a new one of the type or one with the data, and the asset for it. These report failures
        // in the log. Engine assets can't be changed, and project assets can't go under "engine/".
        template <typename T>
        TypedAssetID<T> Create(std::string_view path) { return As<T>(CreateFile(path, AssetType::Of<T>(), std::nullopt)); }

        template <typename T>
        TypedAssetID<T> Create(std::string_view path, const AssetData<T>& data) { return As<T>(CreateFile(path, AssetType::Of<T>(), T::Write(data, *this))); }

        // What the asset is using right now, which edits may have made different from its file
        template <typename T>
        [[nodiscard]] const AssetData<T>* GetData(TypedAssetID<T> id);

        // Shows the data on the asset, leaving its file alone
        template <typename T>
        AssetUpdate Apply(TypedAssetID<T> id, const AssetData<T>& data);

        // Stores the settings in the asset's meta file, and if it is loaded, loads it again for what uses it.
        bool Reimport(AssetID id, nlohmann::json settings);

        // Loads the project's assets again whose file or included files changed since they were loaded, if their type
        // does that, and updates what uses them. One that no longer loads keeps the old version. Returns the ids of
        // what was loaded or made again.
        std::vector<AssetID> ReloadChanged();
    private:
        // What is known about every asset, loaded or not. What it loads as is in its type's store.
        struct Record
        {
            AssetInfo Info;
            AssetImporterBase* Importer = nullptr;
            const EmbeddedFile* Embedded = nullptr;
        };

        // What a registered type is made of
        struct TypeRecord
        {
            AssetType Type;
            std::unique_ptr<AssetImporterBase> Importer;
            std::unique_ptr<AssetStoreBase> Store; // null for a type that doesn't load
        };

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

        template <typename T>
        AssetStore<T>& Store();

        // Null for a type that is not registered, and for one that doesn't load
        [[nodiscard]] AssetStoreBase* StoreOf(AssetType type) const;
        [[nodiscard]] const TypeRecord* RecordOf(AssetType type) const;

        // Where an asset of a type keeps what it loaded. Null if there is no such asset of that type.
        template <typename T>
        typename AssetStore<T>::Slot* SlotFor(AssetID id);

        [[nodiscard]] AssetSource SourceOf(const Record& record) const;
        [[nodiscard]] ImportCache& CacheFor(const Record& record) { return record.Embedded ? ImportCache::Engine() : m_cache; }
        AssetID FindIdOrWarn(std::string_view path) const;

        template <typename T>
        TypedAssetID<T> FindOrWarn(std::string_view path) const
        {
            const AssetID id = FindIdOrWarn(path);
            const TypedAssetID<T> typed = As<T>(id);
            if (id && !typed)
                WarnWrongType(path, AssetType::Of<T>());
            return typed;
        }

        void WarnWrongType(std::string_view path, AssetType wanted) const;

        GpuResourceManager& m_gpuResources;
        ComponentRegistry m_components{ *this };
        std::filesystem::path m_root;
        ImportCache m_cache;

        std::unordered_map<std::string, AssetImporterBase*> m_importersByExtension;

        std::unordered_map<AssetID, Record> m_assets;
        std::unordered_map<std::string, AssetID> m_idsByPath;
        std::unordered_map<std::string, AssetID> m_renamedPaths; // an engine asset's old path to its id
        std::unordered_map<AssetID, AssetID> m_renamedIds; // the id made from an engine asset's old path to its id
        mutable std::unordered_map<AssetID, MissingAssetRef> m_missing;

        std::vector<TypeRecord> m_types; // in the order they were registered, each after the types it uses
        uint64_t m_version = 0;
    };

    template <typename T>
    AssetStore<T>& AssetManager::Store()
    {
        // Made when the type was registered
        return static_cast<AssetStore<T>&>(*StoreOf(AssetType::Of<T>()));
    }

    template <typename T>
    typename AssetStore<T>::Slot* AssetManager::SlotFor(AssetID id)
    {
        const auto it = m_assets.find(id);
        if (it == m_assets.end() || !it->second.Info.Type.Is<T>())
            return nullptr;

        return &Store<T>().GetOrCreate(id, static_cast<AssetImporter<T>&>(*it->second.Importer));
    }

    template <LoadableAsset T>
    const LoadedAsset<T>* AssetManager::Get(TypedAssetID<T> id)
    {
        // Without a hash of anything but the id, nor any file access, as drawing looks up every frame. A removed one is
        // gone, even while it is still loaded for what used it to move off it.
        if (!m_assets.contains(id))
            return nullptr;

        const AssetStoreBase* base = StoreOf(AssetType::Of<T>());
        if (!base)
            return nullptr;

        if (const auto* found = static_cast<const AssetStore<T>&>(*base).Find(id); found && found->Loaded)
            return &*found->Loaded;

        auto* slot = SlotFor<T>(id);

        if (slot && !slot->Tried())
        {
            ImportContext ctx{ m_gpuResources, *this, CacheFor(m_assets.at(id)) };

            slot->Loaded = slot->Import(SourceOf(m_assets.at(id)), ctx);
            slot->Failed = !slot->Loaded;

            if (slot->Failed)
                LOG_ERROR("[Assets] Failed to load {}", GetInfo(id)->Path);
        }

        return slot && slot->Loaded ? &*slot->Loaded : nullptr;
    }

    template <typename T>
    const AssetData<T>* AssetManager::GetData(TypedAssetID<T> id)
    {
        auto* slot = SlotFor<T>(id);
        if (!slot)
            return nullptr;

        if (!slot->Data)
            slot->Data = Read(id);

        return slot->Data ? &*slot->Data : nullptr;
    }

    template <typename T>
    AssetUpdate AssetManager::Apply(TypedAssetID<T> id, const AssetData<T>& data)
    {
        const AssetInfo* info = GetInfo(id);
        if (!info || info->ReadOnly)
            return AssetUpdate::Failed;

        (void)Get(id);
        const AssetData<T>* current = GetData(id);

        auto& slot = *SlotFor<T>(id);
        ImportContext ctx{ m_gpuResources, *this, CacheFor(m_assets.at(id)) };

        if (slot.Loaded && current && slot.Importer->Update(*slot.Loaded, *current, data, ctx))
        {
            slot.Data = data;
            slot.Uses = std::move(ctx.Uses);
            return AssetUpdate::Updated;
        }

        ctx.Uses.clear();

        std::optional<LoadedAsset<T>> replacement = slot.Importer->Create(data, SourceOf(m_assets.at(id)), ctx);
        if (!replacement)
            return AssetUpdate::Failed;

        slot.Unload(m_gpuResources);
        slot.Loaded = std::move(replacement);
        slot.Failed = false;
        slot.Data = data;
        slot.Uses = std::move(ctx.Uses);
        return AssetUpdate::Replaced;
    }

    template <CompleteAssetType T, ImporterFor<T> Importer>
    bool AssetManager::RegisterType()
    {
        const AssetType type = AssetType::Of<T>();

        if (!type)
        {
            LOG_ERROR("[Assets] Can't register a type with no name");
            return false;
        }

        if (RecordOf(type))
        {
            LOG_ERROR("[Assets] {} is registered already", type.Name());
            return false;
        }

        for (const AssetType used : T::Uses)
        {
            if (!RecordOf(used))
            {
                LOG_ERROR("[Assets] Can't register {}: the {} it uses isn't registered yet", type.Name(), used.Name());
                return false;
            }
        }

        auto importer = std::make_unique<Importer>();

        if (importer->Type() != type)
        {
            LOG_ERROR("[Assets] Can't register {}: its importer is for {}", type.Name(), importer->Type().Name());
            return false;
        }

        for (const std::string_view extension : importer->Extensions())
        {
            if (m_importersByExtension.contains(std::string(extension)))
            {
                LOG_ERROR("[Assets] Can't register {}: {} files have an importer already", type.Name(), extension);
                return false;
            }
        }

        for (const std::string_view extension : importer->Extensions())
            m_importersByExtension[std::string(extension)] = importer.get();

        TypeRecord record{ .Type = type, .Importer = std::move(importer) };

        if constexpr (LoadableAsset<T>)
            record.Store = std::make_unique<AssetStore<T>>();

        m_types.push_back(std::move(record));
        return true;
    }

    // Registers the engine's own types of asset, each after the types it uses. This is the one place that names them all,
    // as RegisterNativeComponents is for components.
    void RegisterBuiltinAssetTypes(AssetManager& assets);
}
