#include "crimson/asset_manager/asset_manager.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <iterator>
#include <system_error>
#include <typeinfo>
#include <utility>
#include <glm/gtc/quaternion.hpp>
#include "crimson/asset_manager/importers/material_importer.hpp"
#include "crimson/asset_manager/importers/mesh_importer.hpp"
#include "crimson/asset_manager/importers/scene_importer.hpp"
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
    static constexpr std::string_view kDefaultShaderPath = "engine/shaders/main.hlsl";

    // What a new shader starts as: lit like the default one, with fewer slots, to have something to change
    static constexpr std::string_view kNewShaderSource = R"(// VSMain and PSMain are the entry points. The material's properties go in the one cbuffer in space2,
// and its textures in space2 too. A comment like "@default 0.5 @range 0 1" sets how a property starts.
// Time and DeltaTime, in seconds, come from crimson/bindings.hlsli. crimson/lighting.hlsli also brings
// crimson/math.hlsli (PI, TAU, EPSILON, ...) and crimson/transforms.hlsli (ObjectToWorldPosition, WorldToClip, ...).
#include "crimson/lighting.hlsli"

cbuffer MaterialBlock : register(b0, space2)
{
    float4 Color; // @default 1 1 1 1
    float Metallic; // @default 0 @range 0 1
    float Roughness; // @default 0.5 @range 0 1
};

Texture2D ColorMap : register(t0, space2);

struct VertexInput
{
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float2 UV : TEXCOORD0;
};

struct VertexOutput
{
    float4 ClipPosition : SV_Position;
    float3 Normal : NORMAL;
    float3 WorldPosition : WORLDPOSITION;
    float2 UV : TEXCOORD0;
};

VertexOutput VSMain(VertexInput input)
{
    const float3 worldPosition = ObjectToWorldPosition(input.Position);

    VertexOutput output;
    output.ClipPosition = WorldToClip(worldPosition);
    output.Normal = ObjectToWorldNormal(input.Normal);
    output.WorldPosition = worldPosition;
    output.UV = input.UV;
    return output;
}

float4 PSMain(VertexOutput input) : SV_Target
{
    const float4 base = Color * ColorMap.Sample(LinearWrap, input.UV);
    const float3 normal = normalize(input.Normal);

    Surface surface;
    surface.WorldPosition = input.WorldPosition;
    surface.Normal = normal;
    surface.GeometricNormal = normal;
    surface.Albedo = base.rgb;
    surface.Metallic = Metallic;
    surface.Roughness = Roughness;
    surface.Occlusion = 1.0;

    return float4(ComputeLighting(surface), base.a);
}
)";

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
        AddImporter(std::make_unique<SceneImporter>());

        RegisterEngineResources();

        std::unordered_set<AssetID> seen;
        Scan(seen);
    }

    AssetManager::~AssetManager()
    {
        // Each one can be used by the ones before it
        constexpr AssetType order[] = { AssetType::Material, AssetType::Mesh, AssetType::Texture, AssetType::Shader };

        for (AssetType type : order)
            for (auto& [id, entry] : m_assets)
                if (entry.Info.Type == type)
                    Unload(entry);

        if (m_flatNormal)
            m_gpuResources.DestroyTexture(m_flatNormal);
    }

    TextureHandle AssetManager::EmptySlotTexture(ShaderHandle shader, const std::string& slot)
    {
        const auto& textures = m_gpuResources.GetShaderInterface(shader).Textures;
        const auto it = textures.find(slot);

        if (it == textures.end() || it->second.Default != ShaderTextureDefault::Normal)
            return TextureHandle::Invalid();

        if (!m_flatNormal)
        {
            // Pointing straight out of the surface
            constexpr std::array<unsigned char, 4> pixel = { 128, 128, 255, 255 };
            m_flatNormal = m_gpuResources.CreateTexture({ .Width = 1, .Height = 1, .Format = TextureFormat::RGBA8, .Usage = TextureUsage::Sampled }, pixel.data());
        }

        return m_flatNormal;
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
            info.ReadOnly = true;

            const AssetID id = info.Id;
            m_idsByPath[info.Path] = id;
            m_assets.emplace(id, Entry{ .Info = std::move(info), .Importer = importer->second, .Embedded = &file });
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

        LOG_INFO("[Assets] {} [{}] id={}", info.Path, ToString(info.Type), info.Id.ToString());

        m_idsByPath[info.Path] = info.Id;
        const AssetID id = info.Id;
        m_assets.emplace(id, Entry{ .Info = std::move(info), .Importer = importer->second });
        seen.insert(id);
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

    std::vector<const AssetInfo*> AssetManager::ListAll() const
    {
        std::vector<const AssetInfo*> assets;
        assets.reserve(m_assets.size());

        for (const auto& [id, entry] : m_assets)
            assets.push_back(&entry.Info);

        std::ranges::sort(assets, {}, &AssetInfo::Path);
        return assets;
    }

    bool AssetManager::IsLoaded(AssetID id) const
    {
        const auto it = m_assets.find(id);
        return it != m_assets.end() && !std::holds_alternative<std::monostate>(it->second.Loaded);
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

        entry.Loaded = Import(entry);
        entry.Failed = std::holds_alternative<std::monostate>(entry.Loaded);

        if (entry.Failed)
            LOG_ERROR("[Assets] Failed to load {}", entry.Info.Path);

        return entry.Loaded;
    }

    LoadedAsset AssetManager::Import(Entry& entry)
    {
        ImportContext ctx{ m_gpuResources, *this };
        const AssetSource source{ entry.Info, entry.Embedded ? fs::path() : m_root / fs::path(entry.Info.Path), entry.Embedded };

        // Before the import, for a save during it to count as a change
        std::error_code error;
        if (!entry.Embedded)
            entry.FileTime = fs::last_write_time(source.File, error);

        LoadedAsset loaded = entry.Importer->Load(source, ctx);

        entry.Dependencies.clear();

        for (fs::path& dependency : ctx.Dependencies)
        {
            const fs::file_time_type time = fs::last_write_time(dependency, error);
            entry.Dependencies.emplace_back(std::move(dependency), time);
        }

        return loaded;
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
        entry.Material.reset();

        if (std::holds_alternative<std::monostate>(entry.Loaded))
            return;

        entry.Importer->Unload(entry.Loaded, m_gpuResources);
        entry.Loaded = {};
    }

    fs::path AssetManager::GetFilePath(AssetID id) const
    {
        const AssetInfo* info = GetInfo(id);
        return info && !info->ReadOnly ? m_root / fs::path(info->Path) : fs::path();
    }

    std::vector<AssetID> AssetManager::Refresh()
    {
        std::unordered_map<std::string, AssetID> moved;

        for (const auto& [id, entry] : m_assets)
            if (!entry.Info.ReadOnly)
                moved.emplace(entry.Info.Path, id);

        std::unordered_set<AssetID> seen;
        Scan(seen);

        std::vector<AssetID> removed;

        for (const auto& [id, entry] : m_assets)
            if (!entry.Info.ReadOnly && !seen.contains(id))
                removed.push_back(id);

        RemoveAssets(removed);

        // Only now, as a renamed folder's materials don't know where they are until the scan has been past them
        std::erase_if(moved, [this](const auto& asset)
        {
            const AssetInfo* info = GetInfo(asset.second);
            return !info || info->Path == asset.first;
        });

        UpdateReferencesTo(moved);
        return removed;
    }

    void AssetManager::UpdateReferencesTo(const std::unordered_map<std::string, AssetID>& moved)
    {
        if (moved.empty())
            return;

        for (const auto& [id, entry] : m_assets)
        {
            const bool isMaterial = entry.Info.Type == AssetType::Material;
            if ((!isMaterial && entry.Info.Type != AssetType::Scene) || entry.Info.ReadOnly)
                continue;

            const fs::path file = m_root / fs::path(entry.Info.Path);
            const std::optional<std::string> text = AssetSource{ entry.Info, file, nullptr }.Read();
            if (!text)
                continue;

            const std::optional<std::string> updated = isMaterial
                ? UpdateMaterialReferences(*text, moved, *this)
                : UpdateSceneReferences(*text, moved, *this);
            if (!updated)
                continue;

            if (std::ofstream stream(file, std::ios::binary); !(stream << *updated))
                LOG_ERROR("[Assets] Can't write {}", entry.Info.Path);
            else
                LOG_INFO("[Assets] Updated the references in {}", entry.Info.Path);
        }
    }

    void AssetManager::RemoveAssets(std::span<const AssetID> ids)
    {
        const auto isRemoved = [ids](AssetID id) { return std::ranges::find(ids, id) != ids.end(); };

        // Materials first, as they can use the textures and shaders being removed
        for (auto& [id, entry] : m_assets)
        {
            if (entry.Info.Type != AssetType::Material || isRemoved(id) || std::holds_alternative<std::monostate>(entry.Loaded))
                continue;

            const MaterialData* data = GetMaterialData(id);
            if (!data)
                continue;

            if (isRemoved(data->Shader))
            {
                Unload(entry);
                continue;
            }

            MaterialData updated = *data;
            const MaterialHandle material = std::get<MaterialHandle>(entry.Loaded);

            for (const auto& [slot, texture] : data->Textures)
            {
                if (isRemoved(texture))
                {
                    m_gpuResources.SetMaterialTexture(material, slot, EmptySlotTexture(LoadShader(data->Shader), slot));
                    updated.Textures.erase(slot);
                }
            }

            entry.Material = std::move(updated);
        }

        for (const AssetType type : { AssetType::Scene, AssetType::Material, AssetType::Mesh, AssetType::Texture, AssetType::Shader })
        {
            for (const AssetID id : ids)
            {
                const auto it = m_assets.find(id);
                if (it != m_assets.end() && it->second.Info.Type == type)
                {
                    Unload(it->second);
                    m_idsByPath.erase(it->second.Info.Path);
                    m_assets.erase(it);
                }
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

    AssetID AssetManager::CreateMaterial(std::string_view path)
    {
        MaterialData data;
        data.Shader = FindId(kDefaultShaderPath);
        data.Properties["Color"] = { 1.0f, 1.0f, 1.0f, 1.0f };

        return CreateAssetFile(path, ".mat", WriteMaterialData(data, *this));
    }

    AssetID AssetManager::CreateScene(std::string_view path)
    {
        SceneNode sun;
        sun.Id = AssetID::Generate();
        sun.Name = "Sun";
        sun.Position = glm::vec3(0.0f, 8.0f, 0.0f);
        sun.RotationDegrees = glm::degrees(glm::eulerAngles(glm::quatLookAt(glm::normalize(glm::vec3(-0.5f, -1.0f, -0.3f)), glm::vec3(0.0f, 1.0f, 0.0f))));
        sun.Light = LightComponent{ .Type = LightType::Directional, .Intensity = 1.2f, .CastsShadow = true };

        SceneData data;
        data.Roots.push_back(std::move(sun));

        return CreateAssetFile(path, ".scene", WriteSceneData(data, *this));
    }

    AssetID AssetManager::CreateShader(std::string_view path)
    {
        return CreateAssetFile(path, ".hlsl", std::string(kNewShaderSource));
    }

    AssetID AssetManager::CreateAssetFile(std::string_view path, std::string_view extension, const std::string& contents)
    {
        if (!IsUsableAssetPath(path) || NormalizeExt(fs::path(path)) != extension)
        {
            LOG_ERROR("[Assets] Can't create a {} file at '{}'", extension, path);
            return {};
        }

        std::error_code error;
        const fs::path file = m_root / fs::path(path);

        if (fs::exists(file, error))
        {
            LOG_ERROR("[Assets] '{}' already exists", path);
            return {};
        }

        fs::create_directories(file.parent_path(), error);

        if (std::ofstream stream(file, std::ios::binary); !(stream << contents))
        {
            LOG_ERROR("[Assets] Can't write '{}'", path);
            return {};
        }

        std::unordered_set<AssetID> seen;
        RegisterFile(file, seen);
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

        Entry& entry = it->second;

        // The extension decides what the file is, and an asset can't change what it is
        if (NormalizeExt(fs::path(newPath)) != NormalizeExt(fs::path(entry.Info.Path)))
        {
            LOG_ERROR("[Assets] Renaming to '{}' would change the type of {}", newPath, entry.Info.Path);
            return false;
        }

        std::error_code error;
        const fs::path from = m_root / fs::path(entry.Info.Path);
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
            LOG_ERROR("[Assets] Can't rename {}: {}", entry.Info.Path, error.message());
            return false;
        }

        fs::rename(meta::MetaPathFor(from), meta::MetaPathFor(to), error);
        if (error)
            LOG_WARN("[Assets] {} lost its meta file: {}", newPath, error.message());

        const std::string oldPath = std::exchange(entry.Info.Path, std::string(newPath));

        m_idsByPath.erase(oldPath);
        m_idsByPath[entry.Info.Path] = id;

        UpdateReferencesTo({ { oldPath, id } });

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

        fs::remove(meta::MetaPathFor(file), error);

        const std::array removed = { id };
        RemoveAssets(removed);
        return true;
    }

    AssetID AssetManager::FindSlotMaterial(AssetID mesh, std::string_view slot) const
    {
        const AssetInfo* info = GetInfo(mesh);
        if (!info || info->Type != AssetType::Mesh)
            return {};

        const size_t folderEnd = info->Path.rfind('/');
        const std::string folder = folderEnd == std::string::npos ? std::string{} : info->Path.substr(0, folderEnd + 1);

        const AssetID material = FindId(folder + "materials/" + std::string(slot) + ".mat");
        const AssetInfo* materialInfo = GetInfo(material);

        return materialInfo && materialInfo->Type == AssetType::Material ? material : AssetID{};
    }

    TextureHandle AssetManager::CreateTexturePreview(AssetID id) const
    {
        const auto it = m_assets.find(id);
        if (it == m_assets.end() || it->second.Info.Type != AssetType::Texture)
            return TextureHandle::Invalid();

        const Entry& entry = it->second;
        const AssetSource source{ entry.Info, entry.Embedded ? fs::path() : m_root / fs::path(entry.Info.Path), entry.Embedded };

        const std::optional<std::string> file = source.Read();
        if (!file)
            return TextureHandle::Invalid();

        const TextureLoadOptions options{ .GenerateMips = true, .FlipY = entry.Info.Settings.value("flipY", false) };
        return CreateTextureFromMemory(m_gpuResources, std::as_bytes(std::span(*file)), options);
    }

    std::optional<MaterialData> AssetManager::ReadMaterialFile(AssetID id) const
    {
        const auto it = m_assets.find(id);
        if (it == m_assets.end() || it->second.Info.Type != AssetType::Material)
            return std::nullopt;

        const Entry& entry = it->second;
        const AssetSource source{ entry.Info, entry.Embedded ? fs::path() : m_root / fs::path(entry.Info.Path), entry.Embedded };

        const std::optional<std::string> text = source.Read();
        return text ? ParseMaterialData(*text, *this, entry.Info.Path) : std::nullopt;
    }

    std::optional<SceneData> AssetManager::ReadSceneFile(AssetID id) const
    {
        const auto it = m_assets.find(id);
        if (it == m_assets.end() || it->second.Info.Type != AssetType::Scene)
            return std::nullopt;

        const Entry& entry = it->second;
        const AssetSource source{ entry.Info, m_root / fs::path(entry.Info.Path), nullptr };

        const std::optional<std::string> text = source.Read();
        return text ? ParseSceneData(*text, *this, entry.Info.Path) : std::nullopt;
    }

    const MaterialData* AssetManager::GetMaterialData(AssetID id)
    {
        const auto it = m_assets.find(id);
        if (it == m_assets.end() || it->second.Info.Type != AssetType::Material)
            return nullptr;

        if (!it->second.Material)
            it->second.Material = ReadMaterialFile(id);

        return it->second.Material ? &*it->second.Material : nullptr;
    }

    MaterialApplyResult AssetManager::ApplyMaterial(AssetID id, const MaterialData& data)
    {
        const auto it = m_assets.find(id);
        if (it == m_assets.end() || it->second.Info.Type != AssetType::Material || it->second.Info.ReadOnly)
            return MaterialApplyResult::Failed;

        const MaterialHandle current = LoadMaterial(id);
        const MaterialData* currentData = GetMaterialData(id);

        Entry& entry = it->second;
        ImportContext ctx{ m_gpuResources, *this };

        // A material's GPU resources are laid out for its shader, so another shader needs a new material
        if (!current || !currentData || currentData->Shader != data.Shader)
        {
            const MaterialHandle replacement = CreateMaterialFromData(data, ctx, entry.Info.Path);
            if (!replacement)
                return MaterialApplyResult::Failed;

            Unload(entry);
            entry.Loaded = replacement;
            entry.Failed = false;
            entry.Material = data;
            return MaterialApplyResult::Replaced;
        }

        // Only what changed: replacing a texture has to wait for the GPU, and a dragged color slider gets here every frame
        // The defaults first, for a property that is no longer in the data to go back to its default
        const ShaderHandle shader = LoadShader(data.Shader);
        ApplyShaderPropertyDefaults(shader, current, ctx);

        MaterialData values = data;
        values.Textures.clear();
        ApplyMaterialValues(values, current, ctx);

        for (const auto& [slot, texture] : data.Textures)
        {
            const auto previous = currentData->Textures.find(slot);
            if (previous != currentData->Textures.end() && previous->second == texture)
                continue;

            if (const TextureHandle handle = LoadTexture(texture))
                m_gpuResources.SetMaterialTexture(current, slot, handle);
        }

        for (const auto& [slot, texture] : currentData->Textures)
        {
            if (!data.Textures.contains(slot))
                m_gpuResources.SetMaterialTexture(current, slot, EmptySlotTexture(shader, slot));
        }

        entry.Material = data;
        return MaterialApplyResult::Updated;
    }

    bool AssetManager::SaveMaterial(AssetID id, const MaterialData& data)
    {
        const auto it = m_assets.find(id);
        if (it == m_assets.end() || it->second.Info.Type != AssetType::Material || it->second.Info.ReadOnly)
        {
            LOG_ERROR("[Assets] This material can't be saved");
            return false;
        }

        const fs::path file = m_root / fs::path(it->second.Info.Path);

        if (std::ofstream stream(file, std::ios::binary); !(stream << WriteMaterialData(data, *this)))
        {
            LOG_ERROR("[Assets] Can't write {}", it->second.Info.Path);
            return false;
        }

        return true;
    }

    bool AssetManager::SaveScene(AssetID id, const SceneData& data)
    {
        const auto it = m_assets.find(id);
        if (it == m_assets.end() || it->second.Info.Type != AssetType::Scene)
        {
            LOG_ERROR("[Assets] This scene can't be saved");
            return false;
        }

        // A scene is a lot of work, so a write that fails halfway shouldn't take the old file with it
        const fs::path file = m_root / fs::path(it->second.Info.Path);
        fs::path temporary = file;
        temporary += ".saving";

        {
            std::ofstream stream(temporary, std::ios::binary);
            stream << WriteSceneData(data, *this);
            stream.close();

            if (!stream)
            {
                LOG_ERROR("[Assets] Can't write {}", it->second.Info.Path);
                return false;
            }
        }

        std::error_code error;
        fs::rename(temporary, file, error);

        if (error)
        {
            LOG_ERROR("[Assets] Can't write {}: {}", it->second.Info.Path, error.message());
            fs::remove(temporary, error);
            return false;
        }

        return true;
    }

    bool AssetManager::ReimportTexture(AssetID id, nlohmann::json settings)
    {
        const auto it = m_assets.find(id);
        if (it == m_assets.end() || it->second.Info.Type != AssetType::Texture || it->second.Info.ReadOnly)
        {
            LOG_ERROR("[Assets] This texture can't be reimported");
            return false;
        }

        Entry& entry = it->second;
        const fs::path file = m_root / fs::path(entry.Info.Path);

        entry.Info.Settings = std::move(settings);

        if (!meta::Write(meta::MetaPathFor(file), entry.Info))
            LOG_ERROR("[Assets] Failed to write the meta file of {}", entry.Info.Path);

        // Not in use yet, so the next load picks the settings up
        if (std::holds_alternative<std::monostate>(entry.Loaded))
        {
            entry.Failed = false;
            return true;
        }

        ImportContext ctx{ m_gpuResources, *this };
        const AssetSource source{ entry.Info, file, nullptr };

        LoadedAsset replacement = entry.Importer->Load(source, ctx);
        if (std::holds_alternative<std::monostate>(replacement))
        {
            LOG_ERROR("[Assets] Failed to reimport {}", entry.Info.Path);
            return false;
        }

        const TextureHandle texture = std::get<TextureHandle>(replacement);

        for (auto& [materialId, material] : m_assets)
        {
            if (material.Info.Type != AssetType::Material || std::holds_alternative<std::monostate>(material.Loaded))
                continue;

            const MaterialData* data = GetMaterialData(materialId);
            if (!data)
                continue;

            for (const auto& [slot, used] : data->Textures)
                if (used == id)
                    m_gpuResources.SetMaterialTexture(std::get<MaterialHandle>(material.Loaded), slot, texture);
        }

        entry.Importer->Unload(entry.Loaded, m_gpuResources);
        entry.Loaded = std::move(replacement);
        return true;
    }

    // A file that can't be looked at counts as unchanged, as it may be in the middle of being saved
    static bool HasChanged(const fs::path& file, fs::file_time_type time)
    {
        std::error_code error;
        const fs::file_time_type now = fs::last_write_time(file, error);
        return !error && now != time;
    }

    std::vector<AssetID> AssetManager::ReloadChangedShaders()
    {
        std::vector<AssetID> reloaded;

        for (auto& [id, entry] : m_assets)
        {
            // One that was never loaded gets the new version when it is
            const bool tried = entry.Failed || !std::holds_alternative<std::monostate>(entry.Loaded);
            if (entry.Info.Type != AssetType::Shader || entry.Info.ReadOnly || !tried)
                continue;

            const bool changed = HasChanged(m_root / fs::path(entry.Info.Path), entry.FileTime)
                || std::ranges::any_of(entry.Dependencies, [](const auto& dependency) { return HasChanged(dependency.first, dependency.second); });

            if (changed)
                std::ranges::move(ReloadShader(id, entry), std::back_inserter(reloaded));
        }

        return reloaded;
    }

    std::vector<AssetID> AssetManager::ReloadShader(AssetID id, Entry& entry)
    {
        LoadedAsset replacement = Import(entry);

        if (std::holds_alternative<std::monostate>(replacement))
        {
            LOG_ERROR("[Assets] {} changed but doesn't compile, so the last version that did stays", entry.Info.Path);
            return {};
        }

        // In place before the materials are made again, as they get their shader from here
        const LoadedAsset previous = std::exchange(entry.Loaded, std::move(replacement));
        entry.Failed = false;

        std::vector<AssetID> reloaded = { id };
        ImportContext ctx{ m_gpuResources, *this };

        for (auto& [materialId, material] : m_assets)
        {
            const bool tried = material.Failed || !std::holds_alternative<std::monostate>(material.Loaded);
            if (material.Info.Type != AssetType::Material || !tried)
                continue;

            // What the material is using, unsaved edits included
            const MaterialData* data = GetMaterialData(materialId);
            if (!data || data->Shader != id)
                continue;

            // It may have failed because of the shader, so it gets another go on its next load
            if (material.Failed)
            {
                material.Failed = false;
                continue;
            }

            const MaterialHandle recreated = CreateMaterialFromData(*data, ctx, material.Info.Path);

            // The old one can't stay, as its shader is about to go
            material.Importer->Unload(material.Loaded, m_gpuResources);
            material.Loaded = recreated ? LoadedAsset(recreated) : LoadedAsset();
            material.Failed = !recreated;

            reloaded.push_back(materialId);
        }

        if (!std::holds_alternative<std::monostate>(previous))
            entry.Importer->Unload(previous, m_gpuResources);

        LOG_INFO("[Assets] Reloaded {}", entry.Info.Path);
        return reloaded;
    }
}
