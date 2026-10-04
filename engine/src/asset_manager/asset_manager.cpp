#include "crimson/asset_manager/asset_manager.hpp"
#include <algorithm>
#include "crimson/asset_manager/meta_file.hpp"
#include "crimson/core/log.hpp"

namespace fs = std::filesystem;

namespace crimson
{
    static std::string NormalizeExt(const fs::path& p)
    {
        std::string e = p.extension().string();
        std::ranges::transform(e, e.begin(),[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return e;
    }

    AssetManager::AssetManager(GpuResourceManager& gpuResources, fs::path root)
        : m_gpuResources(gpuResources), m_root(fs::absolute(std::move(root)).lexically_normal())
    {

    }

    AssetManager::~AssetManager()
    {
        constexpr AssetType order[] = { AssetType::Material, AssetType::Mesh, AssetType::Texture,  AssetType::Shader };

        for (AssetType type : order)
        {
            std::vector<AssetID> ids;
            for (const auto& [id, _] : m_loaded)
            {
                auto reg = m_registry.find(id);
                if (reg != m_registry.end() && reg->second.Type == type)
                    ids.push_back(id);
            }
            for (AssetID id : ids)
                Unload(id);
        }
    }

    AssetImporter* AssetManager::FindImporter(const fs::path& source) const
    {
        auto it = m_extensionMap.find(NormalizeExt(source));
        return it != m_extensionMap.end() ? it->second : nullptr;
    }

    void AssetManager::ScanDirectory()
    {
        LOG_INFO("[Assets] Scanning {}", m_root.string());

        if (!fs::exists(m_root))
        {
            LOG_WARN("[Assets] Root does not exist: {}", m_root.string());
            return;
        }

        size_t imported = 0, skipped = 0;

        for (const auto& entry : fs::recursive_directory_iterator(m_root))
        {
            if (!entry.is_regular_file()) continue;
            if (entry.path().extension() == ".meta") continue;
            if (!FindImporter(entry.path()))
            {
                LOG_WARN("[Assets] Skipped (no importer): {}", fs::relative(entry.path(), m_root).generic_string());
                ++skipped;
                continue;
            }

            if (Import(entry.path())) ++imported;
        }

        LOG_INFO("[Assets] Scan done: {} imported, {} skipped", imported, skipped);
    }

    AssetID AssetManager::Import(const fs::path& source)
    {
        const fs::path absolute = source.is_absolute() ? source : m_root / source;
        const fs::path relative = fs::relative(absolute, m_root);

        AssetImporter* importer = FindImporter(absolute);
        if (!importer)
        {
            LOG_WARN("[Assets] No importer for {}", absolute.string());
            return {};
        }

        const fs::path metaPath = meta::MetaPathFor(absolute);
        AssetMetadata metadata;

        bool createdMeta = false;

        if (auto existing = meta::Read(metaPath))
        {
            metadata = std::move(*existing);
        }
        else
        {
            metadata.Id = AssetID::Generate();
            metadata.Type = importer->Type();
            metadata.Settings = importer->DefaultSettings();
            createdMeta = true;

            if (!meta::Write(metaPath, metadata))
                LOG_ERROR("[Assets] Failed to write {}", metaPath.string());
        }

        metadata.Source = relative;

        if (m_loaded.contains(metadata.Id))
            Unload(metadata.Id);

        const AssetID id = metadata.Id;
        m_pathToId[relative.generic_string()] = id;
        m_registry[id] = std::move(metadata);

        LOG_INFO("[Assets] {} {} [{}] id={}",
         createdMeta ? "Imported (new meta)" : "Imported",
         relative.generic_string(),
         ToString(metadata.Type),
         id.ToString());

        return id;
    }

    const AssetMetadata* AssetManager::GetMetadata(AssetID id) const
    {
        auto it = m_registry.find(id);
        return it != m_registry.end() ? &it->second : nullptr;
    }

    AssetID AssetManager::FindByPath(const fs::path& source) const
    {
        auto it = m_pathToId.find(source.generic_string());
        return it != m_pathToId.end() ? it->second : AssetID{};
    }

    const LoadedAsset& AssetManager::Load(AssetID id)
    {
        static const LoadedAsset kEmpty{};

        if (auto it = m_loaded.find(id); it != m_loaded.end())
            return it->second;

        auto reg = m_registry.find(id);
        if (reg == m_registry.end()) return kEmpty;

        auto imp = m_typeMap.find(reg->second.Type);
        if (imp == m_typeMap.end()) return kEmpty;

        ImportContext ctx{ m_gpuResources, *this };
        LoadedAsset asset = imp->second->Load(reg->second, m_root / reg->second.Source, ctx);
        if (std::holds_alternative<std::monostate>(asset)) return kEmpty;

        return m_loaded.emplace(id, std::move(asset)).first->second;
    }

    void AssetManager::Dump() const
    {
        for (const auto& [path, id] : m_pathToId)
        {
            const AssetMetadata* meta = GetMetadata(id);
            LOG_INFO("[Assets] {} [{}] guid={}", path, meta ? ToString(meta->Type) : "?", id.ToString());
        }
    }

    void AssetManager::Unload(AssetID id)
    {
        auto loaded = m_loaded.find(id);
        if (loaded == m_loaded.end()) return;

        if (auto reg = m_registry.find(id); reg != m_registry.end())
            if (auto imp = m_typeMap.find(reg->second.Type); imp != m_typeMap.end())
                imp->second->Unload(loaded->second, m_gpuResources);

        m_loaded.erase(loaded);
    }

    void AssetManager::AddImporter(std::unique_ptr<AssetImporter> importer)
    {
        AssetImporter* raw = importer.get();

        for (std::string_view ext : raw->Extensions())
            m_extensionMap[std::string(ext)] = raw;

        m_typeMap[raw->Type()] = raw;
        m_importers.push_back(std::move(importer));
    }
}
