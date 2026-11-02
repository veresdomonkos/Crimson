#include "crimson/asset_manager/asset_manager.hpp"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <system_error>
#include <typeinfo>
#include <utility>
#include "crimson/asset_manager/importers/material_importer.hpp"
#include "crimson/asset_manager/importers/mesh_importer.hpp"
#include "crimson/asset_manager/importers/shader_importer.hpp"
#include "crimson/asset_manager/importers/texture_importer.hpp"
#include "crimson/asset_manager/meta_file.hpp"
#include "crimson/core/engine_resources.hpp"
#include "crimson/core/log.hpp"

namespace fs = std::filesystem;

namespace crimson
{
    static constexpr std::string_view kEnginePathPrefix = "engine/";
    static constexpr std::string_view kDefaultMaterialPath = "engine/materials/default.mat";

    // FNV-1a. An engine resource has no meta to store an id in, and scenes must find it again in any project.
    static AssetID EngineResourceId(std::string_view path)
    {
        uint64_t hash = 0xcbf29ce484222325ull;

        for (const unsigned char c : path)
        {
            hash ^= c;
            hash *= 0x100000001b3ull;
        }

        return AssetID{ hash };
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

    AssetManager::AssetManager(GpuResourceManager& gpuResources, fs::path root)
        : m_gpuResources(gpuResources), m_root(fs::absolute(std::move(root)).lexically_normal())
    {
        AddImporter(std::make_unique<TextureImporter>());
        AddImporter(std::make_unique<ShaderImporter>());
        AddImporter(std::make_unique<MaterialImporter>());
        AddImporter(std::make_unique<MeshImporter>());

        RegisterEngineResources();
        Scan();
    }

    AssetManager::~AssetManager()
    {
        // Each one can be used by the ones before it
        constexpr AssetType order[] = { AssetType::Material, AssetType::Mesh, AssetType::Texture, AssetType::Shader };

        for (AssetType type : order)
            for (auto& [id, entry] : m_assets)
                if (entry.Info.Type == type)
                    Unload(entry);
    }

    void AssetManager::AddImporter(std::unique_ptr<AssetImporter> importer)
    {
        for (std::string_view ext : importer->Extensions())
            m_importersByExtension[std::string(ext)] = importer.get();

        m_importers.push_back(std::move(importer));
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

            const AssetID id = info.Id;
            m_idsByPath[info.Path] = id;
            m_assets.emplace(id, Entry{ .Info = std::move(info), .Importer = importer->second, .Embedded = &file });
        }
    }

    void AssetManager::Scan()
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
                RegisterFile(it->path());
        }

        LOG_INFO("[Assets] Scan done: {} assets", m_assets.size());
    }

    void AssetManager::RegisterFile(const fs::path& file)
    {
        const std::string extension = NormalizeExt(file);
        if (extension == ".meta" || extension == ".hlsli")
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

        const fs::path metaPath = meta::MetaPathFor(file);
        std::optional<AssetInfo> existing = meta::Read(metaPath);
        AssetInfo info;

        // A file copied along with its meta would otherwise replace the original's entry
        const bool duplicate = existing && m_assets.contains(existing->Id);
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

        LOG_INFO("[Assets] {} [{}] id={}", info.Path, ToString(info.Type), info.Id.ToString());

        m_idsByPath[info.Path] = info.Id;
        const AssetID id = info.Id;
        m_assets.emplace(id, Entry{ .Info = std::move(info), .Importer = importer->second });
    }

    const AssetInfo* AssetManager::GetInfo(AssetID id) const
    {
        const auto it = m_assets.find(id);
        return it != m_assets.end() ? &it->second.Info : nullptr;
    }

    AssetID AssetManager::FindId(std::string_view path) const
    {
        const auto it = m_idsByPath.find(std::string(path));
        return it != m_idsByPath.end() ? it->second : AssetID{};
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

        for (const auto& [id, entry] : m_assets)
            if (entry.Info.Type == type)
                assets.push_back(&entry.Info);

        std::ranges::sort(assets, {}, &AssetInfo::Path);
        return assets;
    }

    AssetID AssetManager::DefaultMaterial() const
    {
        return FindId(kDefaultMaterialPath);
    }

    const LoadedAsset& AssetManager::Load(AssetID id)
    {
        static const LoadedAsset kEmpty{};

        const auto it = m_assets.find(id);
        if (it == m_assets.end())
            return kEmpty;

        Entry& entry = it->second;

        if (entry.Failed || !std::holds_alternative<std::monostate>(entry.Loaded))
            return entry.Loaded;

        ImportContext ctx{ m_gpuResources, *this };
        const AssetSource source{ entry.Info, entry.Embedded ? fs::path() : m_root / fs::path(entry.Info.Path), entry.Embedded };

        entry.Loaded = entry.Importer->Load(source, ctx);
        entry.Failed = std::holds_alternative<std::monostate>(entry.Loaded);

        if (entry.Failed)
            LOG_ERROR("[Assets] Failed to load {}", entry.Info.Path);

        return entry.Loaded;
    }

    template <typename T>
    const T* AssetManager::LoadAs(AssetID id)
    {
        const LoadedAsset& asset = Load(id);

        if (const T* resource = std::get_if<T>(&asset))
            return resource;

        if (!std::holds_alternative<std::monostate>(asset))
            LOG_ERROR("[Assets] {} isn't a {}", GetInfo(id)->Path, typeid(T).name());

        return nullptr;
    }

    TextureHandle AssetManager::LoadTexture(AssetID id)
    {
        const auto* texture = LoadAs<TextureHandle>(id);
        return texture ? *texture : TextureHandle::Invalid();
    }

    ShaderHandle AssetManager::LoadShader(AssetID id)
    {
        const auto* shader = LoadAs<ShaderHandle>(id);
        return shader ? *shader : ShaderHandle::Invalid();
    }

    MaterialHandle AssetManager::LoadMaterial(AssetID id)
    {
        const auto* material = LoadAs<MaterialHandle>(id);
        return material ? *material : MaterialHandle::Invalid();
    }

    const Mesh* AssetManager::LoadMesh(AssetID id)
    {
        return LoadAs<Mesh>(id);
    }

    TextureHandle AssetManager::LoadTexture(std::string_view path) { return LoadTexture(FindIdOrWarn(path)); }
    ShaderHandle AssetManager::LoadShader(std::string_view path) { return LoadShader(FindIdOrWarn(path)); }
    MaterialHandle AssetManager::LoadMaterial(std::string_view path) { return LoadMaterial(FindIdOrWarn(path)); }
    const Mesh* AssetManager::LoadMesh(std::string_view path) { return LoadMesh(FindIdOrWarn(path)); }

    void AssetManager::Unload(Entry& entry)
    {
        if (std::holds_alternative<std::monostate>(entry.Loaded))
            return;

        entry.Importer->Unload(entry.Loaded, m_gpuResources);
        entry.Loaded = {};
    }
}
