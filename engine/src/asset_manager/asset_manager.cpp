#include "crimson/asset_manager/asset_manager.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <iterator>
#include <ranges>
#include <system_error>
#include <utility>
#include "crimson/asset_manager/meta_file.hpp"
#include "crimson/core/engine_resources.hpp"
#include "crimson/core/log.hpp"

#include "engine_renames.hpp"
#include <crimson/core/file_write.hpp>

namespace fs = std::filesystem;

namespace crimson
{
    constexpr std::string_view kEnginePathPrefix = "engine/";

    // An engine resource has no meta to store an id in, and scenes must find it again in any project.
    static AssetID EngineResourceId(std::string_view path)
    {
        return AssetID::FromName(kAssetPathNamespace, path);
    }

    static std::string NormalizeExt(const fs::path& p)
    {
        std::string e = p.extension().string();
        std::ranges::transform(e, e.begin(),[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return e;
    }

    std::optional<std::string> AssetSource::Read() const
    {
        if (Embedded)
            return std::string(Embedded->Text());

        std::ifstream file(File, std::ios::binary);
        if (!file)
        {
            LOG_ERROR("[Assets] Cannot open '{}'", File.string());
            return std::nullopt;
        }

        return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    }

    AssetManager::AssetManager(GpuResourceManager& gpuResources, fs::path root, fs::path cacheFolder)
        : m_gpuResources(gpuResources), m_root(fs::absolute(std::move(root)).lexically_normal()), m_cache(std::move(cacheFolder), m_root)
    {
        RegisterNativeComponents(m_components);
        RegisterBuiltinAssetTypes(*this);

        RegisterEngineResources();

        std::unordered_set<AssetID> seen;
        Scan(seen);

        // For the project's files to stop needing the old names
        UpdateReferencesTo(m_renamedPaths);
    }

    AssetManager::~AssetManager()
    {
        // Each type was registered after the ones it uses, so those go last
        for (const TypeRecord& record : std::views::reverse(m_types))
        {
            if (record.Store)
                record.Store->UnloadAll(m_gpuResources);
        }

        for (const TypeRecord& record : m_types)
            record.Importer->ReleaseShared(m_gpuResources);
    }

    void AssetManager::RegisterEngineResources()
    {
        for (const EmbeddedFile& file : GetEngineResources())
        {
            // Shader includes have no importer, the shader compiler finds those itself
            const auto importer = m_importersByExtension.find(NormalizeExt(file.Path));
            if (importer == m_importersByExtension.end())
                continue;

            AssetInfo info;
            info.Path = std::string(kEnginePathPrefix) + std::string(file.Path);
            info.Id = EngineResourceId(info.Path);
            info.Type = importer->second->Type();
            info.Settings = importer->second->DefaultSettings();
            info.ReadOnly = true;

            const AssetID id = info.Id;
            m_idsByPath[info.Path] = id;
            m_assets.emplace(id, Record{ .Info = std::move(info), .Importer = importer->second, .Embedded = &file });
        }

        const auto exists = [this](std::string_view path) { return m_idsByPath.contains(std::string(path)); };

        for (const auto& [oldPath, newPath] : kRenamedEngineAssets)
        {
            const std::string_view now = FollowRenames(kRenamedEngineAssets, newPath, exists);
            if (now.empty())
            {
                LOG_ERROR("[Assets] {} was renamed to {}, which doesn't exist", oldPath, newPath);
                continue;
            }

            const AssetID id = m_idsByPath.at(std::string(now));

            m_renamedPaths.emplace(oldPath, id);
            m_renamedIds.emplace(EngineResourceId(oldPath), id);
        }
    }

    void AssetManager::Scan(std::unordered_set<AssetID>& seen)
    {
        LOG_INFO("[Assets] Scanning {}", m_root.string());

        // An unreadable folder must not take the editor down, so errors are checked instead of thrown
        std::error_code error;
        fs::recursive_directory_iterator it(m_root, fs::directory_options::skip_permission_denied, error);

        if (error)
        {
            LOG_WARN("[Assets] Can't read {}: {}", m_root.string(), error.message());
            return;
        }

        for (const fs::recursive_directory_iterator end; it != end; it.increment(error))
        {
            if (error)
            {
                LOG_WARN("[Assets] Stopped scanning {}: {}", m_root.string(), error.message());
                break;
            }

            if (it->is_regular_file(error))
                RegisterFile(it->path(), seen);
        }

        LOG_INFO("[Assets] Scan done: {} assets", m_assets.size());
    }

    void AssetManager::RegisterFile(const fs::path& file, std::unordered_set<AssetID>& seen)
    {
        const std::string extension = NormalizeExt(file);
        if (extension == ".meta" || extension == ".saving" || extension == ".hlsli")
            return;

        std::string path = fs::relative(file, m_root).generic_string();

        if (path.starts_with(kEnginePathPrefix))
        {
            LOG_WARN("[Assets] Skipped {}: the engine/ folder is reserved for the engine's own assets", path);
            return;
        }

        const auto importer = m_importersByExtension.find(extension);
        if (importer == m_importersByExtension.end())
        {
            LOG_WARN("[Assets] Skipped (no importer): {}", path);
            return;
        }

        const fs::path metaPath = meta::PathFor(file);
        std::optional<AssetInfo> existing = meta::Read(metaPath, importer->second->Type());

        // Already known from an earlier scan. If only its meta is gone, it keeps the id it has.
        if (const AssetID known = FindId(path); known && (!existing || existing->Id == known))
        {
            if (!existing && !meta::Write(metaPath, m_assets.at(known).Info))
                LOG_ERROR("[Assets] Failed to write {}", metaPath.string());

            seen.insert(known);
            return;
        }

        bool duplicate = false;

        if (existing)
        {
            if (const auto other = m_assets.find(existing->Id); other != m_assets.end())
            {
                std::error_code error;
                const bool originalStillThere = !other->second.Info.ReadOnly && fs::exists(m_root / fs::path(other->second.Info.Path), error);

                // Moved along with its meta, so nothing using it notices
                if (!originalStillThere && !other->second.Info.ReadOnly)
                {
                    m_idsByPath.erase(other->second.Info.Path);
                    other->second.Info.Path = path;
                    m_idsByPath[path] = existing->Id;
                    seen.insert(existing->Id);
                    return;
                }

                // A file copied along with its meta would otherwise replace the original's entry
                duplicate = true;
            }
        }

        AssetInfo info;

        if (duplicate)
            LOG_WARN("[Assets] {} has the same id as {}, giving it a new one", path, m_assets.at(existing->Id).Info.Path);

        if (existing)
        {
            info = std::move(*existing);
        }
        else
        {
            info.Settings = importer->second->DefaultSettings();
        }

        info.Type = importer->second->Type();
        info.Path = std::move(path);

        if (!existing || duplicate)
        {
            info.Id = AssetID::Generate();

            if (!meta::Write(metaPath, info))
                LOG_ERROR("[Assets] Failed to write {}", metaPath.string());
        }

        LOG_INFO("[Assets] {} [{}] id={}", info.Path, info.Type.Name(), info.Id.ToString());

        m_idsByPath[info.Path] = info.Id;
        const AssetID id = info.Id;
        m_assets.emplace(id, Record{ .Info = std::move(info), .Importer = importer->second });
        seen.insert(id);
    }

    const AssetInfo* AssetManager::GetInfo(AssetID id) const
    {
        const auto it = m_assets.find(id);
        return it != m_assets.end() ? &it->second.Info : nullptr;
    }

    const MissingAssetRef* AssetManager::GetMissing(AssetID id) const
    {
        if (!IsMissing(id))
            return nullptr;

        const auto it = m_missing.find(id);
        return it != m_missing.end() ? &it->second : nullptr;
    }

    bool AssetManager::RememberMissing(AssetID id, MissingAssetRef ref) const
    {
        return id && m_missing.insert_or_assign(id, std::move(ref)).second;
    }

    AssetID AssetManager::FindId(std::string_view path) const
    {
        if (const auto it = m_idsByPath.find(std::string(path)); it != m_idsByPath.end())
            return it->second;

        const auto renamed = m_renamedPaths.find(std::string(path));
        return renamed != m_renamedPaths.end() ? renamed->second : AssetID{};
    }

    AssetID AssetManager::FindId(const AssetRef& ref) const
    {
        if (const AssetID id = Redirect(ref.Id); id && GetInfo(id))
            return id;

        return ref.Path.empty() ? AssetID{} : FindId(std::string_view(ref.Path));
    }

    AssetID AssetManager::Redirect(AssetID id) const
    {
        if (GetInfo(id))
            return id;

        const auto renamed = m_renamedIds.find(id);
        return renamed != m_renamedIds.end() ? renamed->second : id;
    }

    AssetID AssetManager::FindIdOrWarn(std::string_view path) const
    {
        const AssetID id = FindId(path);
        if (!id)
            LOG_WARN("[Assets] No asset at '{}'", path);
        return id;
    }

    std::vector<const AssetInfo*> AssetManager::List(AssetType type) const
    {
        std::vector<const AssetInfo*> assets;

        for (const auto& [id, record] : m_assets)
            if (record.Info.Type == type)
                assets.push_back(&record.Info);

        std::ranges::sort(assets, {}, &AssetInfo::Path);
        return assets;
    }

    std::vector<const AssetInfo*> AssetManager::ListAll() const
    {
        std::vector<const AssetInfo*> assets;
        assets.reserve(m_assets.size());

        for (const auto& [id, record] : m_assets)
            assets.push_back(&record.Info);

        std::ranges::sort(assets, {}, &AssetInfo::Path);
        return assets;
    }

    bool AssetManager::IsLoaded(AssetID id) const
    {
        const AssetInfo* info = GetInfo(id);
        const AssetStoreBase* store = info ? StoreOf(info->Type) : nullptr;
        return store && store->IsLoaded(id);
    }

    const AssetManager::TypeRecord* AssetManager::RecordOf(AssetType type) const
    {
        const auto it = std::ranges::find(m_types, type, &TypeRecord::Type);
        return it != m_types.end() ? &*it : nullptr;
    }

    AssetStoreBase* AssetManager::StoreOf(AssetType type) const
    {
        const TypeRecord* record = RecordOf(type);
        return record ? record->Store.get() : nullptr;
    }

    std::vector<AssetType> AssetManager::Types() const
    {
        std::vector<AssetType> types;
        types.reserve(m_types.size());

        for (const TypeRecord& record : m_types)
            types.push_back(record.Type);

        return types;
    }

    AssetSource AssetManager::SourceOf(const Record& record) const
    {
        return AssetSource{ record.Info, record.Embedded ? fs::path() : m_root / fs::path(record.Info.Path), record.Embedded };
    }

    void AssetManager::WarnWrongType(std::string_view path, AssetType wanted) const
    {
        LOG_WARN("[Assets] '{}' isn't a {}", path, wanted.Name());
    }

    fs::path AssetManager::GetFilePath(AssetID id) const
    {
        const AssetInfo* info = GetInfo(id);
        return info && !info->ReadOnly ? m_root / fs::path(info->Path) : fs::path();
    }

    std::vector<AssetID> AssetManager::Refresh()
    {
        std::unordered_map<std::string, AssetID> moved;

        for (const auto& [id, record] : m_assets)
            if (!record.Info.ReadOnly)
                moved.emplace(record.Info.Path, id);

        std::unordered_set<AssetID> seen;
        Scan(seen);

        std::vector<AssetID> removed;

        for (const auto& [id, record] : m_assets)
            if (!record.Info.ReadOnly && !seen.contains(id))
                removed.push_back(id);

        RemoveAssets(removed);

        // Only now, as a renamed folder's materials don't know where they are until the scan has been past them
        std::erase_if(moved, [this](const auto& asset)
        {
            const AssetInfo* info = GetInfo(asset.second);
            return !info || info->Path == asset.first;
        });

        UpdateReferencesTo(moved);
        ++m_version;
        return removed;
    }

    void AssetManager::UpdateReferencesTo(const std::unordered_map<std::string, AssetID>& moved)
    {
        if (moved.empty())
            return;

        for (const auto& [id, record] : m_assets)
        {
            if (record.Info.ReadOnly)
                continue;

            const fs::path file = m_root / fs::path(record.Info.Path);

            const std::optional<std::string> updated = record.Importer->UpdateReferences(AssetSource{ record.Info, file, nullptr }, moved, *this);
            if (!updated)
                continue;

            if (!WriteFileSafely(file, *updated))
                LOG_ERROR("[Assets] Can't write {}", record.Info.Path);
            else
                LOG_INFO("[Assets] Updated the references in {}", record.Info.Path);
        }
    }

    void AssetManager::RemoveAssets(std::span<const AssetID> ids)
    {
        std::vector<std::pair<AssetID, AssetType>> removed;

        // What the users say names the removed ones as their files do, which is lost once those are gone
        for (const AssetID id : ids)
        {
            for (const TypeRecord& record : m_types)
            {
                if (!record.Store)
                    continue;

                for (const AssetID user : record.Store->UsersOf(id))
                {
                    if (const auto it = m_assets.find(user); it != m_assets.end())
                        record.Store->ReadData(user, SourceOf(it->second), *this);
                }
            }
        }

        for (const AssetID id : ids)
        {
            const auto it = m_assets.find(id);
            if (it == m_assets.end())
                continue;

            RememberMissing(id, { .Path = it->second.Info.Path, .Type = it->second.Info.Type });
            removed.emplace_back(id, it->second.Info.Type);

            m_idsByPath.erase(it->second.Info.Path);
            m_assets.erase(it);
        }

        // Before they are forgotten, as what uses them has to move off them first
        for (const auto& [id, type] : removed)
            UpdateUsersOf(id, true, nullptr);

        for (const TypeRecord& record : std::views::reverse(m_types))
        {
            if (!record.Store)
                continue;

            for (const auto& [id, removedType] : removed)
            {
                if (removedType == record.Type)
                    record.Store->Forget(id, m_gpuResources);
            }
        }
    }

    void AssetManager::UpdateUsersOf(AssetID used, bool removed, std::vector<AssetID>* replaced)
    {
        for (const TypeRecord& record : m_types)
        {
            if (!record.Store)
                continue;

            // Collected first, as updating one can load others
            for (const AssetID user : record.Store->UsersOf(used))
            {
                const auto it = m_assets.find(user);
                if (it == m_assets.end())
                    continue;

                ImportContext ctx{ m_gpuResources, *this, CacheFor(it->second) };

                if (record.Store->UpdateUser(user, used, removed, SourceOf(it->second), ctx) && replaced)
                    replaced->push_back(user);
            }
        }
    }

    static bool IsUsableAssetPath(std::string_view path)
    {
        const fs::path parsed(path);

        if (path.empty() || parsed.is_absolute() || !parsed.has_filename() || path.starts_with(kEnginePathPrefix))
            return false;

        return std::ranges::none_of(parsed, [](const fs::path& part) { return part == ".."; });
    }

    AssetID AssetManager::CreateFile(std::string_view path, AssetType type, std::optional<std::string> contents)
    {
        const auto importer = m_importersByExtension.find(NormalizeExt(fs::path(path)));

        if (!IsUsableAssetPath(path) || importer == m_importersByExtension.end() || importer->second->Type() != type)
        {
            LOG_ERROR("[Assets] Can't create a {} file at '{}'", type.Name(), path);
            return {};
        }

        std::error_code error;
        const fs::path file = m_root / fs::path(path);

        if (fs::exists(file, error))
        {
            LOG_ERROR("[Assets] '{}' already exists", path);
            return {};
        }

        if (!contents)
            contents = importer->second->NewFile(*this);

        if (!contents)
        {
            LOG_ERROR("[Assets] A {} can't be made without contents", type.Name());
            return {};
        }

        fs::create_directories(file.parent_path(), error);

        if (!WriteFileSafely(file, *contents))
        {
            LOG_ERROR("[Assets] Can't write '{}'", path);
            return {};
        }

        std::unordered_set<AssetID> seen;
        RegisterFile(file, seen);
        ++m_version;
        return FindId(path);
    }

    bool AssetManager::Rename(AssetID id, std::string_view newPath)
    {
        const auto it = m_assets.find(id);

        if (it == m_assets.end() || it->second.Info.ReadOnly || !IsUsableAssetPath(newPath))
        {
            LOG_ERROR("[Assets] Can't rename an asset to '{}'", newPath);
            return false;
        }

        Record& record = it->second;

        // The extension decides what the file is, and an asset can't change what it is
        if (NormalizeExt(fs::path(newPath)) != NormalizeExt(fs::path(record.Info.Path)))
        {
            LOG_ERROR("[Assets] Renaming to '{}' would change the type of {}", newPath, record.Info.Path);
            return false;
        }

        std::error_code error;
        const fs::path from = m_root / fs::path(record.Info.Path);
        const fs::path to = m_root / fs::path(newPath);

        if (fs::exists(to, error))
        {
            LOG_ERROR("[Assets] '{}' already exists", newPath);
            return false;
        }

        fs::create_directories(to.parent_path(), error);
        fs::rename(from, to, error);

        if (error)
        {
            LOG_ERROR("[Assets] Can't rename {}: {}", record.Info.Path, error.message());
            return false;
        }

        fs::rename(meta::PathFor(from), meta::PathFor(to), error);
        if (error)
            LOG_WARN("[Assets] {} lost its meta file: {}", newPath, error.message());

        const std::string oldPath = std::exchange(record.Info.Path, std::string(newPath));

        m_idsByPath.erase(oldPath);
        m_idsByPath[record.Info.Path] = id;

        UpdateReferencesTo({ { oldPath, id } });

        ++m_version;
        return true;
    }

    bool AssetManager::Delete(AssetID id)
    {
        const auto it = m_assets.find(id);

        if (it == m_assets.end() || it->second.Info.ReadOnly)
        {
            LOG_ERROR("[Assets] This asset can't be deleted");
            return false;
        }

        std::error_code error;
        const fs::path file = m_root / fs::path(it->second.Info.Path);

        fs::remove(file, error);

        if (error)
        {
            LOG_ERROR("[Assets] Can't delete {}: {}", it->second.Info.Path, error.message());
            return false;
        }

        fs::remove(meta::PathFor(file), error);

        const std::array removed = { id };
        RemoveAssets(removed);
        ++m_version;
        return true;
    }

    std::optional<std::string> AssetManager::ReadSource(AssetID id) const
    {
        const auto it = m_assets.find(id);
        if (it == m_assets.end())
            return std::nullopt;

        const Record& record = it->second;

        if (!record.Info.ReadOnly)
        {
            if (const std::optional<std::string> filled = record.Importer->AddMissingIds(SourceOf(record)))
            {
                if (!WriteFileSafely(m_root / fs::path(record.Info.Path), *filled))
                {
                    LOG_ERROR("[Assets] {} has entities without an id and can't be written to give them one", record.Info.Path);
                    return std::nullopt;
                }

                LOG_INFO("[Assets] Gave the entities in {} their ids", record.Info.Path);
                return filled;
            }
        }

        return SourceOf(record).Read();
    }

    bool AssetManager::WriteFile(AssetID id, const std::string& contents)
    {
        const auto it = m_assets.find(id);

        if (it == m_assets.end() || it->second.Info.ReadOnly)
        {
            LOG_ERROR("[Assets] {} can't be saved", it != m_assets.end() ? it->second.Info.Path : "This asset");
            return false;
        }

        if (!WriteFileSafely(m_root / fs::path(it->second.Info.Path), contents))
        {
            LOG_ERROR("[Assets] Can't write {}", it->second.Info.Path);
            return false;
        }

        return true;
    }

    bool AssetManager::Reimport(AssetID id, nlohmann::json settings)
    {
        const auto it = m_assets.find(id);
        if (it == m_assets.end() || it->second.Info.ReadOnly)
        {
            LOG_ERROR("[Assets] This asset can't be reimported");
            return false;
        }

        Record& record = it->second;
        record.Info.Settings = std::move(settings);

        if (!meta::Write(meta::PathFor(m_root / fs::path(record.Info.Path)), record.Info))
            LOG_ERROR("[Assets] Failed to write the meta file of {}", record.Info.Path);

        AssetStoreBase* store = StoreOf(record.Info.Type);
        if (!store)
            return true;

        // Not in use yet, so the next load picks the settings up
        if (!store->IsLoaded(id))
        {
            store->Retry(id);
            return true;
        }

        return !Reload(id).empty();
    }

    std::vector<AssetID> AssetManager::ReloadChanged()
    {
        std::vector<AssetID> changed;

        // One that was never loaded gets the new version when it is
        for (const auto& [id, record] : m_assets)
        {
            const AssetStoreBase* store = StoreOf(record.Info.Type);

            if (!record.Info.ReadOnly && record.Importer->ReloadsWhenChanged() && store && store->HasChangedOnDisk(id))
                changed.push_back(id);
        }

        std::vector<AssetID> reloaded;

        for (const AssetID id : changed)
            std::ranges::move(Reload(id), std::back_inserter(reloaded));

        return reloaded;
    }

    std::vector<AssetID> AssetManager::Reload(AssetID id)
    {
        const Record& record = m_assets.at(id);
        ImportContext ctx{ m_gpuResources, *this, CacheFor(record) };

        std::vector<AssetID> replaced = { id };

        if (!StoreOf(record.Info.Type)->Reload(id, SourceOf(record), ctx, [&] { UpdateUsersOf(id, false, &replaced); }))
        {
            LOG_ERROR("[Assets] {} changed but doesn't load, so the last version that did stays", record.Info.Path);
            return {};
        }

        ++m_version;
        LOG_INFO("[Assets] Reloaded {}", record.Info.Path);
        return replaced;
    }
}
